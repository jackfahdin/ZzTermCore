#include "ZzContourBackend.h"

#include "ZzContourConvert.h"
#include "ZzContourPtyBridge.h"

#include <vtbackend/render/RenderBuffer.hpp>
#include <vtbackend/screen/Terminal.hpp>

#include <crispy/Environment.hpp>

#include <chrono>
#include <utility>

namespace {

vtbackend::PageSize makePageSize(int columns, int rows)
{
    return vtbackend::PageSize { vtbackend::LineCount(rows), vtbackend::ColumnCount(columns) };
}

// 末尾不构成完整 UTF-8 序列的字节数（0 表示尾部完整或非法）。
// contour 的 parseBulkText 在整段都是未完成 UTF-8 时会回退 FSM 重放前导字节，
// 多打 U+FFFD（split feed 场景实测复现），因此在进入 Terminal 前先缓冲尾部残缺序列。
size_t trailingIncompleteUtf8(std::string_view data)
{
    size_t continuationCount = 0;
    size_t i = data.size();
    while (i > 0 && continuationCount < 3
           && (static_cast<std::uint8_t>(data[i - 1]) & 0xC0) == 0x80)
    {
        --i;
        ++continuationCount;
    }
    if (i == 0)
        return 0; // 全是续字节而无前导字节：非法输入，不缓冲
    auto const lead = static_cast<std::uint8_t>(data[i - 1]);
    size_t expected = 0;
    if ((lead & 0x80) == 0)
        return 0; // 尾部是 ASCII，完整
    else if ((lead & 0xE0) == 0xC0)
        expected = 2;
    else if ((lead & 0xF0) == 0xE0)
        expected = 3;
    else if ((lead & 0xF8) == 0xF0)
        expected = 4;
    else
        return 0; // 非法前导字节（含孤立续字节），不缓冲
    size_t const present = continuationCount + 1;
    return present < expected ? present : 0;
}

} // namespace

struct ZzContourBackend::Impl
{
    // EventsImpl 先于 terminal 声明：Terminal 构造需要 Events&，且 EventsImpl 寿命须包住 Terminal。
    class EventsImpl : public vtbackend::Terminal::NullEvents
    {
    public:
        explicit EventsImpl(Impl& owner) : owner_(owner) {}
        void setWindowTitle(std::string_view newTitle) override
        {
            owner_.title = std::string(newTitle);
            owner_.listener.onTitleChanged(std::string(newTitle));
        }
        void bell() override { owner_.listener.onBell(); }
        void screenUpdated() override { owner_.listener.onScreenDirty(); }
        void bufferChanged(vtbackend::ScreenType type) override
        {
            owner_.listener.onActiveBufferChanged(type == vtbackend::ScreenType::Alternate);
        }
        // 锁内回调（cursorPositionChanged 等）不接线：M1a 无实时渲染方，
        // 且锁内禁止回读 Terminal；需要时只置标志 defer。
    private:
        Impl& owner_;
    };

    ZzContourEvents& listener;
    std::string title;
    EventsImpl eventsImpl;
    vtbackend::PageSize pageSize;
    std::unique_ptr<vtbackend::Terminal> terminal;
    std::string pendingUtf8; // 跨 feed 的残缺 UTF-8 尾部，下次 feed 前拼回

    Impl(int columns, int rows, ZzContourEvents& events, int scrollbackLines)
        : listener(events)
        , eventsImpl(*this)
        , pageSize(makePageSize(columns, rows))
    {
        auto settings = vtbackend::Settings {};
        settings.pageSize = pageSize;
        settings.historyLimits = vtbackend::HistoryLimits::plain(vtbackend::LineCount(scrollbackLines));
        settings.ptyReadBufferSize = 4096;
        settings.goodImageProtocol = false;

        auto bridgePtr = std::make_unique<ZzContourPtyBridge>(
            pageSize, [this](std::string_view data) { listener.onWriteToTransport(std::string(data)); });
        terminal = std::make_unique<vtbackend::Terminal>(eventsImpl,
                                                         crispy::defaultEnvironment(),
                                                         std::move(bridgePtr),
                                                         settings,
                                                         std::chrono::steady_clock::now());
    }
};

ZzContourBackend::ZzContourBackend(int columns, int rows, ZzContourEvents& events, int scrollbackLines)
    : impl_(std::make_unique<Impl>(columns, rows, events, scrollbackLines))
{
}

ZzContourBackend::~ZzContourBackend() = default;

void ZzContourBackend::feed(std::string_view data)
{
    std::string joined;
    if (!impl_->pendingUtf8.empty())
    {
        joined = impl_->pendingUtf8 + std::string(data);
        impl_->pendingUtf8.clear();
        data = joined;
    }
    auto const holdback = trailingIncompleteUtf8(data);
    if (holdback != 0)
    {
        impl_->pendingUtf8 = std::string(data.substr(data.size() - holdback));
        data.remove_suffix(holdback);
    }
    if (!data.empty())
        impl_->terminal->writeToScreen(data);
}

void ZzContourBackend::resize(int columns, int rows)
{
    impl_->pageSize = makePageSize(columns, rows);
    impl_->terminal->resizeScreen(
        impl_->pageSize,
        vtbackend::ImageSize { vtbackend::Width(static_cast<unsigned>(columns) * 8u),
                               vtbackend::Height(static_cast<unsigned>(rows) * 17u) });
}

std::pair<int, int> ZzContourBackend::size() const
{
    return { impl_->pageSize.columns.value, impl_->pageSize.lines.value };
}

bool ZzContourBackend::isAlternateScreen() const
{
    return impl_->terminal->isAlternateScreen();
}

std::string ZzContourBackend::title() const
{
    return impl_->title;
}

int ZzContourBackend::historyLineCount() const
{
    return impl_->terminal->currentScreen().historyLineCount().value;
}

bool ZzContourBackend::lineWrapped(int row) const
{
    // Contour 的 LineFlag::Wrapped 标在续行（被绕到的下一行）上；对外语义为
    // 「第 row 行内容自动续到下一行」，即看下一行是否带 Wrapped。末行无下一行，恒 false。
    if (row + 1 >= impl_->pageSize.lines.value)
        return false;
    return impl_->terminal->currentScreen()
        .lineFlags(vtbackend::LineOffset(row + 1))
        .contains(vtbackend::LineFlag::Wrapped);
}

void ZzContourBackend::flushReplies()
{
    impl_->terminal->flushInput();
}

void ZzContourBackend::sendKeyEvent(const ZzKeyEvent& event)
{
    // xterm 默认不上报 Release（与 native encoder 行为对齐）；Repeat 按 Press。
    if (event.action == ZzKeyEvent::Action::Release)
        return;
    const auto now = std::chrono::steady_clock::now();
    const vtbackend::KeyboardModifiers mods { zzModifiers(event.modifiers) };
    if (event.key == ZzKeyEvent::Key::Character) {
        if (event.character != 0)
            impl_->terminal->sendCharEvent(event.character, vtbackend::KeyIdentity{}, mods,
                                           vtbackend::KeyboardEventType::Press, now);
        return;
    }
    const std::optional<vtbackend::Key> key = zzKey(event.key);
    if (!key)
        return; // 未覆盖键：忽略（ZzContourConvert.h 注释钉住）
    impl_->terminal->sendKeyEvent(*key, mods, vtbackend::KeyboardEventType::Press, now);
}

void ZzContourBackend::sendText(std::string_view utf8)
{
    // encodeText 恒等：sendRawInput 原样写入 input generator 并 flush，
    // 字节经 PTY bridge → onWriteToTransport 上行（与回传同一出口）。
    impl_->terminal->sendRawInput(utf8);
}

void ZzContourBackend::sendMouseEvent(const ZzMouseEvent& event)
{
    const vtbackend::Modifiers mods { zzModifiers(event.modifiers) };
    const vtbackend::CellLocation pos { .line = vtbackend::LineOffset(event.row),
                                        .column = vtbackend::ColumnOffset(event.col) };
    switch (event.action) {
    case ZzMouseAction::Press:
        impl_->terminal->sendMousePressEvent(mods, zzMouseButton(event.button), pos,
                                             vtbackend::PixelCoordinate{}, false);
        break;
    case ZzMouseAction::Move:
        impl_->terminal->sendMouseMoveEvent(mods, pos, vtbackend::PixelCoordinate{}, false);
        break;
    case ZzMouseAction::Release:
        impl_->terminal->sendMouseReleaseEvent(mods, zzMouseButton(event.button),
                                               vtbackend::PixelCoordinate{}, false);
        break;
    }
}

void ZzContourBackend::sendPasteText(std::string_view utf8)
{
    impl_->terminal->sendPaste(utf8);
}

void ZzContourBackend::sendFocusEvent(bool focused)
{
    if (focused)
        impl_->terminal->sendFocusInEvent();
    else
        impl_->terminal->sendFocusOutEvent();
}

std::optional<std::pair<int, int>> ZzContourBackend::cursorPosition()
{
    impl_->terminal->refreshRenderBuffer();
    auto ref = impl_->terminal->renderBuffer(); // RAII 读锁句柄
    auto const& renderBuffer = ref.get();
    if (!renderBuffer.cursor)
        return std::nullopt;
    return std::make_pair(renderBuffer.cursor->position.line.value,
                          renderBuffer.cursor->position.column.value);
}

const void* ZzContourBackend::screenForView() const
{
    return &impl_->terminal->currentScreen();
}

ZzContourSnapshot ZzContourBackend::snapshot()
{
    auto const& screen = impl_->terminal->currentScreen();
    auto const& grid = screen.grid();
    ZzContourSnapshot snap;
    snap.columns = impl_->pageSize.columns.value;
    snap.rows = impl_->pageSize.lines.value;
    snap.alternateScreen = impl_->terminal->isAlternateScreen();
    snap.cells.reserve(static_cast<size_t>(snap.columns * snap.rows));
    for (int line = 0; line < snap.rows; ++line)
    {
        auto const& gridLine = grid.lineAt(vtbackend::LineOffset(line));
        // 行列同时增长时 Contour 先扩列（growColumns 的循环只覆盖旧行数范围）再扩行，
        // 新并入页面的行仍持旧宽度的未物化存储；按 CellProxy 契约以 isBlank() 守卫，
        // 空行直接产出默认格，不触碰 SoA 数组。
        if (gridLine.isBlank())
        {
            // 空行可携带非默认 fillAttrs 背景（BCE 擦除场景）；前景保持默认。
            auto const& fillAttrs = gridLine.storage().fillAttrs;
            ZzContourCell blank;
            blank.background = zzColor(fillAttrs.backgroundColor);
            blank.attributes = zzAttributes(fillAttrs.flags);
            for (int col = 0; col < snap.columns; ++col)
                snap.cells.push_back(blank);
            continue;
        }
        auto const lineColumns = gridLine.size().value;
        for (int col = 0; col < snap.columns; ++col)
        {
            ZzContourCell out;
            if (col < lineColumns)
            {
                auto const cell = screen.at(vtbackend::LineOffset(line), vtbackend::ColumnOffset(col));
                out.codepoints = cell.codepoints();
                out.width = zzWidth(cell);
                out.foreground = zzColor(cell.foregroundColor());
                out.background = zzColor(cell.backgroundColor());
                out.attributes = zzAttributes(cell.flags());
            }
            snap.cells.push_back(std::move(out));
        }
    }
    if (auto const cursor = cursorPosition())
        snap.cursor = ZzContourCursor { cursor->first, cursor->second };
    return snap;
}
