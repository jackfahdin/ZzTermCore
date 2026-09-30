#include "ZzContourHistoryView.h"

#include "ZzContourBackend.h"
#include "ZzContourLineSource.h"

#include "unicode/Utf8Encode.h"

namespace {

// ZzLineView 内联存储的是 const ZzLine*（指针值），thunk 需先取指针再解引用。
const ZzLine& lineFrom(const void* storage)
{
    return **static_cast<const ZzLine* const*>(storage);
}

} // namespace

ZzContourHistoryView::ZzContourHistoryView(const ZzContourBackend& backend,
                                           const ZzContourLineSource& lineSource,
                                           const std::uint64_t& generation) noexcept
    : backend_(&backend), lineSource_(&lineSource), generation_(&generation)
{
}

std::size_t ZzContourHistoryView::lineCount() const noexcept
{
    if (backend_->isAlternateScreen())
        return 0; // Alternate 无历史（与内部 LineSource 同一规则）
    return static_cast<std::size_t>(backend_->historyLineCount());
}

ZzLineView ZzContourHistoryView::lineAt(std::size_t index) const
{
    lineBuf_ = backend_->historyLineSnapshot(static_cast<int>(index));
    return ZzLineView(&lineBuf_, &cellAtThunk, &cellCountThunk, &wrappedThunk);
}

std::uint64_t ZzContourHistoryView::droppedLineCount() const noexcept
{
    return lineSource_->droppedLineCount();
}

std::uint64_t ZzContourHistoryView::generation() const noexcept
{
    return *generation_;
}

ZzCellView ZzContourHistoryView::cellAtThunk(const void* storage, int col)
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

int ZzContourHistoryView::cellCountThunk(const void* storage) noexcept
{
    return lineFrom(storage).cellCount();
}

bool ZzContourHistoryView::wrappedThunk(const void* storage) noexcept
{
    return lineFrom(storage).wrapped();
}
