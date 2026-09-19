#include "ZzContourBackend.h"

#include "ZzContourPtyBridge.h"

#include <vtbackend/screen/Terminal.hpp>

#include <crispy/Environment.hpp>

#include <chrono>
#include <utility>

namespace {

vtbackend::PageSize makePageSize(int columns, int rows)
{
    return vtbackend::PageSize { vtbackend::LineCount(rows), vtbackend::ColumnCount(columns) };
}

} // namespace

struct ZzContourBackend::Impl
{
    // EventsImpl 先于 terminal 声明：Terminal 构造需要 Events&，且 EventsImpl 寿命须包住 Terminal。
    class EventsImpl : public vtbackend::Terminal::NullEvents
    {
    public:
        explicit EventsImpl(Impl& owner) : owner_(owner) {}
        // 事件转发在任务 4/5 逐个接线；本任务先保证构造/feed/resize/快照可用。
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
            for (int col = 0; col < snap.columns; ++col)
                snap.cells.emplace_back();
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
            }
            // 颜色与 flags 转换在任务 3 接线；光标在任务 4 接线。
            snap.cells.push_back(std::move(out));
        }
    }
    return snap;
}
