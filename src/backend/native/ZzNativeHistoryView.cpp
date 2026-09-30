#include "ZzNativeHistoryView.h"

#include <ZzTerm/Line.h>
#include <ZzTerm/Screen.h>
#include <ZzTerm/Scrollback.h>

#include "unicode/Utf8Encode.h"

namespace {

// ZzLineView 内联存储的是 const ZzLine*（指针值），thunk 需先取指针再解引用。
const ZzLine& lineFrom(const void* storage)
{
    return **static_cast<const ZzLine* const*>(storage);
}

} // namespace

ZzNativeHistoryView::ZzNativeHistoryView(const ZzScreen& screen,
                                         const ZzScrollback& scrollback,
                                         const std::uint64_t& generation) noexcept
    : screen_(&screen), scrollback_(&scrollback), generation_(&generation)
{
}

std::size_t ZzNativeHistoryView::lineCount() const noexcept
{
    if (screen_->activeBuffer() == ZzScreenBuffer::Alternate)
        return 0; // Alternate 无历史（与内部 LineSource 同一规则）
    return scrollback_->lineCount();
}

ZzLineView ZzNativeHistoryView::lineAt(std::size_t index) const
{
    return ZzLineView(&scrollback_->lineAt(index), &cellAtThunk, &cellCountThunk,
                      &wrappedThunk);
}

std::uint64_t ZzNativeHistoryView::droppedLineCount() const noexcept
{
    return scrollback_->stats().totalDropped;
}

std::uint64_t ZzNativeHistoryView::generation() const noexcept
{
    return *generation_;
}

ZzCellView ZzNativeHistoryView::cellAtThunk(const void* storage, int col)
{
    const ZzLine& line = lineFrom(storage);
    const ZzCell& cell = line.cellAt(col);
    ZzCellView view;
    if (cell.isCluster()) {
        view.text = line.clusterText(cell.clusterIndex());
    } else if (cell.codePoint() != 0) {
        zzAppendCodePoint(view.text, cell.codePoint());
    }
    view.foreground = cell.foreground();
    view.background = cell.background();
    view.attributes = cell.attributes();
    view.width      = cell.width();
    return view;
}

int ZzNativeHistoryView::cellCountThunk(const void* storage) noexcept
{
    return lineFrom(storage).cellCount();
}

bool ZzNativeHistoryView::wrappedThunk(const void* storage) noexcept
{
    return lineFrom(storage).wrapped();
}
