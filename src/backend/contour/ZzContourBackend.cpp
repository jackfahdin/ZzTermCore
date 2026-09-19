#include "ZzContourBackend.h"

#include "ZzContourPtyBridge.h"

#include <vtbackend/core/CellFlags.hpp>
#include <vtbackend/core/Color.hpp>
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

ZzColor zzColor(vtbackend::Color color)
{
    switch (color.type())
    {
        case vtbackend::ColorType::RGB: {
            auto const rgb = color.rgb();
            return { ZzColor::Tag::RGB,
                     (static_cast<std::uint32_t>(rgb.red) << 16)
                         | (static_cast<std::uint32_t>(rgb.green) << 8)
                         | static_cast<std::uint32_t>(rgb.blue) };
        }
        case vtbackend::ColorType::Indexed:
            return { ZzColor::Tag::Indexed, color.index() };
        case vtbackend::ColorType::Bright:
            // 亮 n 号色统一映射为索引 8+n，保留颜色身份。
            return { ZzColor::Tag::Indexed, static_cast<std::uint32_t>(8 + color.index()) };
        case vtbackend::ColorType::Default:
            return { ZzColor::Tag::Default, 0 };
        case vtbackend::ColorType::Undefined:
        default:
            return { ZzColor::Tag::Undefined, 0 };
    }
}

std::uint32_t zzFlags(vtbackend::CellFlags flags)
{
    std::uint32_t out = ZzCellFlag::None;
    if (flags.contains(vtbackend::CellFlag::Bold)) out |= ZzCellFlag::Bold;
    if (flags.contains(vtbackend::CellFlag::Faint)) out |= ZzCellFlag::Faint;
    if (flags.contains(vtbackend::CellFlag::Italic)) out |= ZzCellFlag::Italic;
    if (flags.contains(vtbackend::CellFlag::Underline)) out |= ZzCellFlag::Underline;
    if (flags.contains(vtbackend::CellFlag::Blinking)) out |= ZzCellFlag::Blinking;
    if (flags.contains(vtbackend::CellFlag::Inverse)) out |= ZzCellFlag::Inverse;
    if (flags.contains(vtbackend::CellFlag::Hidden)) out |= ZzCellFlag::Hidden;
    if (flags.contains(vtbackend::CellFlag::CrossedOut)) out |= ZzCellFlag::CrossedOut;
    if (flags.contains(vtbackend::CellFlag::WideCharContinuation)) out |= ZzCellFlag::WideCharContinuation;
    return out;
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
        // 锁内回调（cursorPositionChanged 等）不接线：M1a 无实时渲染方，
        // 且锁内禁止回读 Terminal；需要时只置标志 defer。
        // bufferChanged 在任务 5 接线。
    private:
        Impl& owner_;
    };

    ZzContourEvents& listener;
    std::string title;
    EventsImpl eventsImpl;
    vtbackend::PageSize pageSize;
    ZzContourPtyBridge* bridge = nullptr; // 所有权在 terminal
    std::unique_ptr<vtbackend::Terminal> terminal;

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
        bridge = bridgePtr.get();

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
    return impl_->terminal->currentScreen()
        .lineFlags(vtbackend::LineOffset(row))
        .contains(vtbackend::LineFlag::Wrapped);
}

void ZzContourBackend::flushReplies()
{
    impl_->terminal->flushInput();
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
            blank.flags = zzFlags(fillAttrs.flags);
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
                out.width = static_cast<int>(cell.width());
                out.foreground = zzColor(cell.foregroundColor());
                out.background = zzColor(cell.backgroundColor());
                out.flags = zzFlags(cell.flags());
            }
            snap.cells.push_back(std::move(out));
        }
    }
    impl_->terminal->refreshRenderBuffer();
    {
        auto ref = impl_->terminal->renderBuffer(); // RAII 读锁句柄
        auto const& renderBuffer = ref.get();
        if (renderBuffer.cursor)
        {
            snap.cursor = ZzContourCursor { renderBuffer.cursor->position.line.value,
                                            renderBuffer.cursor->position.column.value };
        }
    }
    return snap;
}
