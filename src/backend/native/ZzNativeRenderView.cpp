#include "ZzNativeRenderView.h"

#include <ZzTerm/Line.h>
#include <ZzTerm/Screen.h>

#include "unicode/Utf8Encode.h"

namespace {

// ZzLineView 内联存储的是 const ZzLine*（指针值），thunk 需先取指针再解引用。
const ZzLine& lineFrom(const void* storage)
{
    return **static_cast<const ZzLine* const*>(storage);
}

} // namespace

ZzNativeRenderView::ZzNativeRenderView(const ZzScreen& screen) noexcept : screen_(&screen) {}

ZzSize ZzNativeRenderView::size() const noexcept { return screen_->size(); }

bool ZzNativeRenderView::isAlternateScreen() const noexcept
{
    return screen_->activeBuffer() == ZzScreenBuffer::Alternate;
}

ZzLineView ZzNativeRenderView::lineAt(int row) const
{
    return ZzLineView(&screen_->lineAt(row), &cellAtThunk, &cellCountThunk, &wrappedThunk);
}

ZzCursorState ZzNativeRenderView::cursor() const { return screen_->cursor(); }

std::uint64_t ZzNativeRenderView::dirtyGeneration() const noexcept { return screen_->dirtyGeneration(); }

bool ZzNativeRenderView::rowDirty(int row) const noexcept { return screen_->rowDirty(row); }

ZzCellRange ZzNativeRenderView::dirtyRange(int row) const noexcept { return screen_->dirtyRange(row); }

ZzCellView ZzNativeRenderView::cellAtThunk(const void* storage, int col)
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

int ZzNativeRenderView::cellCountThunk(const void* storage) noexcept
{
    return lineFrom(storage).cellCount();
}

bool ZzNativeRenderView::wrappedThunk(const void* storage) noexcept
{
    return lineFrom(storage).wrapped();
}
