#include "ZzTerm/RenderView.h"

#include "ZzTerm/Scrollback.h"

// ZzRenderView 实现骨架（M0）：纯转发到 ZzScreen/ZzScrollback。
// 回看（scrollbackOffset > 0）的行映射在滚动条功能落地时实现，
// 当前恒为 0，接口先行稳定。

ZzRenderView::ZzRenderView(const ZzScreen* screen, const ZzScrollback* scrollback) noexcept
    : screen_(screen)
    , scrollback_(scrollback)
{
}

ZzSize ZzRenderView::size() const noexcept
{
    return screen_ ? screen_->size() : ZzSize{};
}

bool ZzRenderView::isAlternateScreen() const noexcept
{
    return screen_ && screen_->activeBuffer() == ZzScreenBuffer::Alternate;
}

int ZzRenderView::scrollbackOffset() const noexcept
{
    return 0; // M0：不回看。
}

std::size_t ZzRenderView::scrollbackLineCount() const noexcept
{
    return scrollback_ ? scrollback_->lineCount() : 0;
}

const ZzLine& ZzRenderView::lineAt(int row) const noexcept
{
    return screen_->lineAt(row);
}

const ZzCell& ZzRenderView::cellAt(int row, int col) const noexcept
{
    return screen_->lineAt(row).cellAt(col);
}

ZzCursorState ZzRenderView::cursor() const noexcept
{
    return screen_ ? screen_->cursor() : ZzCursorState{};
}

std::uint64_t ZzRenderView::dirtyGeneration() const noexcept
{
    return screen_ ? screen_->dirtyGeneration() : 0;
}

bool ZzRenderView::rowDirty(int row) const noexcept
{
    return screen_ && screen_->rowDirty(row);
}

ZzCellRange ZzRenderView::dirtyRange(int row) const noexcept
{
    return screen_ ? screen_->dirtyRange(row) : ZzCellRange{};
}

std::vector<int> ZzRenderView::dirtyRows() const
{
    return screen_ ? screen_->dirtyRows() : std::vector<int>{};
}
