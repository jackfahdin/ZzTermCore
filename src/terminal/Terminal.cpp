#include <ZzTerm/Terminal.h>

#include "../backend/ZzTerminalBackend.h"
#include "../backend/native/ZzNativeBackend.h"
#ifdef ZZTERM_WITH_CONTOUR
#include "../backend/contour/ZzContourBackendAdapter.h"
#endif

#include "ZzSearch.h"
#include "ZzSearchState.h"
#include "ZzSelection.h"
#include "ZzSelectionText.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

class ZzTerminal::Impl {
public:
    Impl(int cols, int rows, ZzBackendKind kind, std::size_t scrollbackMaxLines)
    {
        switch (kind) {
        case ZzBackendKind::Native:
            backend = std::make_unique<ZzNativeBackend>(cols, rows, scrollbackMaxLines);
            break;
        case ZzBackendKind::Contour:
#ifdef ZZTERM_WITH_CONTOUR
            backend = zzCreateContourBackendAdapter(cols, rows, scrollbackMaxLines);
#else
            throw std::logic_error("ZzBackendKind::Contour 需要 ZZTERM_WITH_CONTOUR=ON 构建");
#endif
            break;
        }
    }
    std::unique_ptr<ZzTerminalBackend> backend;

    ZzSelection selection;
    ZzSearchState searchState;
    std::uint64_t lastDropped = 0; // lineSource().droppedLineCount() 的上次观测值

    // feed/resize 后维护选区与搜索锚点（规格 M5a 5.1 / M5b 5.4：
    // Alternate 切换清空；丢弃按物理计数平移，近似语义）
    void noteSelectionAfterFeed(const ZzTermChanges& changes)
    {
        const std::uint64_t dropped = backend->lineSource().droppedLineCount();
        if (changes.activeBufferChanged) {
            selection.clear();
            searchState.clear();
        } else if (dropped > lastDropped) {
            selection.onLinesDropped(dropped - lastDropped);
            searchState.onLinesDropped(dropped - lastDropped);
        }
        lastDropped = dropped;
    }
};

ZzTerminal::ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines)
    : impl_(std::make_unique<Impl>(cols, rows, backend, scrollbackMaxLines)) {}
ZzTerminal::~ZzTerminal() = default;

ZzTermChanges ZzTerminal::feed(std::span<const std::byte> data)
{
    ZzTermChanges changes = impl_->backend->feed(data);
    impl_->noteSelectionAfterFeed(changes);
    return changes;
}

bool ZzTerminal::resize(int cols, int rows)
{
    const bool changed = impl_->backend->resize(cols, rows);
    if (changed) {
        // 隐含前提：后端 resize 永不置 activeBufferChanged；若未来违反，
        // 语义仍安全（走 noteSelectionAfterFeed 的清空分支）。
        impl_->noteSelectionAfterFeed(ZzTermChanges{}); // resize 也可能丢弃（reflow 裁剪）
        const std::int64_t count = zzLogicalLineCount(impl_->backend->lineSource());
        impl_->selection.clampTo(count);
        impl_->searchState.clampTo(count);
    }
    return changed;
}
const ZzRenderView& ZzTerminal::renderView() const noexcept { return impl_->backend->renderView(); }
ZzSize ZzTerminal::size() const noexcept { return impl_->backend->size(); }
ZzCursorState ZzTerminal::cursor() const noexcept { return impl_->backend->cursor(); }
bool ZzTerminal::isAlternateScreen() const noexcept { return impl_->backend->isAlternateScreen(); }
const std::string& ZzTerminal::title() const noexcept { return impl_->backend->title(); }
void ZzTerminal::clearDirty() noexcept { impl_->backend->clearDirty(); }
void ZzTerminal::setOutputHandler(std::function<void(std::string_view)> handler)
{
    impl_->backend->setOutputHandler(std::move(handler));
}

void ZzTerminal::setAmbiguousWidthMode(bool wide) noexcept
{
    impl_->backend->setAmbiguousWidthMode(wide);
}

void ZzTerminal::sendText(std::string_view utf8)
{
    impl_->backend->sendText(utf8);
}

void ZzTerminal::sendKey(const ZzKeyEvent& event)
{
    impl_->backend->sendKey(event);
}

void ZzTerminal::sendMouse(const ZzMouseEvent& event)
{
    impl_->backend->sendMouse(event);
}

void ZzTerminal::sendPaste(std::string_view utf8)
{
    impl_->backend->sendPaste(utf8);
}

void ZzTerminal::sendFocus(bool focused)
{
    impl_->backend->sendFocus(focused);
}

ZzScreen& ZzTerminal::screen()
{
    auto* native = dynamic_cast<ZzNativeBackend*>(impl_->backend.get());
    if (!native)
        throw std::logic_error("ZzTerminal::screen() 仅 Native 后端可用");
    return native->screen();
}

ZzScrollback& ZzTerminal::scrollback()
{
    auto* native = dynamic_cast<ZzNativeBackend*>(impl_->backend.get());
    if (!native)
        throw std::logic_error("ZzTerminal::scrollback() 仅 Native 后端可用");
    return native->scrollback();
}

void ZzTerminal::setSelection(ZzLogicalPos anchor, ZzLogicalPos extent)
{
    const std::int64_t count = zzLogicalLineCount(impl_->backend->lineSource());
    if (count == 0) {
        impl_->selection.clear();
        return;
    }
    auto clampLine = [count](ZzLogicalPos& p) {
        p.line = std::clamp<std::int64_t>(p.line, 0, count - 1);
        p.col = std::max<std::int32_t>(p.col, 0);
    };
    clampLine(anchor);
    clampLine(extent);
    impl_->selection.set(anchor, extent);
}

void ZzTerminal::extendSelection(ZzLogicalPos extent)
{
    const std::int64_t count = zzLogicalLineCount(impl_->backend->lineSource());
    if (count == 0) {
        impl_->selection.clear();
        return;
    }
    extent.line = std::clamp<std::int64_t>(extent.line, 0, count - 1);
    extent.col = std::max<std::int32_t>(extent.col, 0);
    // 空选区时 extend 等价于放置一个零长选区（ZzSelection::extend 只动 extent_，
    // anchor_ 保持默认原点——与 "无选区时等价 setSelection(extent, extent)" 的
    // 声明语义不同，这里直接走 set 保证直觉一致）。
    if (impl_->selection.empty())
        impl_->selection.set(extent, extent);
    else
        impl_->selection.extend(extent);
}

void ZzTerminal::clearSelection() noexcept
{
    impl_->selection.clear();
}

bool ZzTerminal::hasSelection() const noexcept
{
    return !impl_->selection.empty();
}

bool ZzTerminal::selectionRange(ZzLogicalPos& start, ZzLogicalPos& end) const
{
    return impl_->selection.range(start, end);
}

std::string ZzTerminal::selectedText() const
{
    ZzLogicalPos start, end;
    if (!impl_->selection.range(start, end))
        return {};
    return zzExtractSelectionText(impl_->backend->lineSource(), start, end);
}

std::size_t ZzTerminal::search(std::string_view pattern, ZzSearchOptions options)
{
    if (pattern.empty()) {
        impl_->searchState.clear();
        return 0;
    }
    auto matches = zzSearchLines(impl_->backend->lineSource(), pattern, options);
    impl_->searchState.set(std::string(pattern), options, std::move(matches));
    return impl_->searchState.matchCount();
}

void ZzTerminal::clearSearch() noexcept
{
    impl_->searchState.clear();
}

std::size_t ZzTerminal::searchMatchCount() const noexcept
{
    return impl_->searchState.matchCount();
}

bool ZzTerminal::searchMatch(std::size_t index, ZzLogicalPos& start, ZzLogicalPos& end) const
{
    return impl_->searchState.match(index, start, end);
}
