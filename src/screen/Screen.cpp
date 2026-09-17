#include "ZzTerm/Screen.h"

#include <algorithm>

// ZzScreen 实现骨架（M0）。
// 说明：本文件实现网格级原语；光标相对滚动区换算、DECAWM 换行、
// wide/continuation 一致性等语义由 ZzTerminal 写入流程负责。

void ZzScreen::Buffer::resize(int cols, int rows)
{
    lines.resize(static_cast<std::size_t>(rows));
    for (auto& line : lines) {
        if (line.cellCount() != cols)
            line.resize(cols);
    }
    dirtyRows.assign(static_cast<std::size_t>(rows), 1);
    dirtyRanges.assign(static_cast<std::size_t>(rows), ZzCellRange{0, cols});
    cursor.position.row = std::clamp(cursor.position.row, 0, rows - 1);
    cursor.position.col = std::clamp(cursor.position.col, 0, cols - 1);
}

ZzScreen::ZzScreen(int cols, int rows)
    : cols_(std::max(cols, 1))
    , rows_(std::max(rows, 1))
    , scrollTop_(0)
    , scrollBottom_(std::max(rows, 1) - 1)
    , tabStops_(static_cast<std::size_t>(std::max(cols, 1)), 0)
{
    primary_.resize(cols_, rows_);
    alternate_.resize(cols_, rows_);
    // 默认每 8 列一个 Tab Stop。
    for (int c = 8; c < cols_; c += 8)
        tabStops_[static_cast<std::size_t>(c)] = 1;
}

ZzSize ZzScreen::size() const noexcept
{
    return ZzSize{cols_, rows_};
}

void ZzScreen::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return;
    cols_ = cols;
    rows_ = rows;
    primary_.resize(cols_, rows_);
    alternate_.resize(cols_, rows_);
    tabStops_.resize(static_cast<std::size_t>(cols_), 0);
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    ++dirtyGeneration_;
    markAllDirty();
}

ZzScreenBuffer ZzScreen::activeBuffer() const noexcept
{
    return active_;
}

void ZzScreen::setActiveBuffer(ZzScreenBuffer buffer)
{
    if (active_ == buffer)
        return;
    active_ = buffer;
    ++dirtyGeneration_;
    markAllDirty();
}

const ZzLine& ZzScreen::lineAt(int row) const noexcept
{
    static const ZzLine kEmpty{};
    const Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    if (row < 0 || row >= rows_)
        return kEmpty;
    return buf.lines[static_cast<std::size_t>(row)];
}

void ZzScreen::putCell(ZzPosition pos, const ZzCell& cell) noexcept
{
    if (pos.row < 0 || pos.row >= rows_ || pos.col < 0 || pos.col >= cols_)
        return;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.lines[static_cast<std::size_t>(pos.row)].setCell(pos.col, cell);
    markDirty(pos.row, pos.col);
}

void ZzScreen::setLineWrapped(int row, bool wrapped) noexcept
{
    if (row < 0 || row >= rows_)
        return;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.lines[static_cast<std::size_t>(row)].setWrapped(wrapped);
    markRowDirty(row);
}

ZzCursorState ZzScreen::cursor() const noexcept
{
    return active_ == ZzScreenBuffer::Primary ? primary_.cursor : alternate_.cursor;
}

void ZzScreen::setCursorPosition(ZzPosition pos) noexcept
{
    pos.row = std::clamp(pos.row, 0, rows_ - 1);
    pos.col = std::clamp(pos.col, 0, cols_ - 1);
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.cursor.position = pos;
}

void ZzScreen::setCursorStyle(ZzCursorShape shape, bool visible, bool blinking) noexcept
{
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.cursor.shape = shape;
    buf.cursor.visible = visible;
    buf.cursor.blinking = blinking;
}

void ZzScreen::saveCursor() noexcept
{
    savedCursor_ = cursor();
    hasSavedCursor_ = true;
}

void ZzScreen::restoreCursor() noexcept
{
    if (!hasSavedCursor_) {
        setCursorPosition(ZzPosition{0, 0});
        return;
    }
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    setCursorPosition(savedCursor_.position);
    buf.cursor = savedCursor_;
}

void ZzScreen::setScrollRegion(int topRow, int bottomRow) noexcept
{
    if (topRow < 0 || bottomRow >= rows_ || topRow >= bottomRow) {
        resetScrollRegion();
        return;
    }
    scrollTop_ = topRow;
    scrollBottom_ = bottomRow;
}

void ZzScreen::resetScrollRegion() noexcept
{
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
}

ZzCellRange ZzScreen::scrollRegionRows() const noexcept
{
    // 复用 ZzCellRange 表达 [top, bottom] 闭区间：endCol 存 bottom + 1。
    return ZzCellRange{scrollTop_, scrollBottom_ + 1};
}

void ZzScreen::setTabStop(int col)
{
    if (col < 0 || col >= cols_)
        return;
    tabStops_[static_cast<std::size_t>(col)] = 1;
}

void ZzScreen::clearTabStop(int col)
{
    if (col < 0) {
        std::fill(tabStops_.begin(), tabStops_.end(), char{0});
        return;
    }
    if (col >= cols_)
        return;
    tabStops_[static_cast<std::size_t>(col)] = 0;
}

int ZzScreen::nextTabStop(int col) const noexcept
{
    for (int c = std::max(col + 1, 0); c < cols_; ++c) {
        if (tabStops_[static_cast<std::size_t>(c)])
            return c;
    }
    return cols_ - 1;
}

void ZzScreen::setOriginMode(bool on) noexcept { originMode_ = on; }
bool ZzScreen::originMode() const noexcept { return originMode_; }
void ZzScreen::setInsertMode(bool on) noexcept { insertMode_ = on; }
bool ZzScreen::insertMode() const noexcept { return insertMode_; }
void ZzScreen::setAutoWrapMode(bool on) noexcept { autoWrapMode_ = on; }
bool ZzScreen::autoWrapMode() const noexcept { return autoWrapMode_; }

void ZzScreen::eraseInLine(ZzEraseMode mode, const ZzCell& fill) noexcept
{
    const ZzPosition cur = cursor().position;
    int from = 0;
    int to = cols_ - 1;
    switch (mode) {
    case ZzEraseMode::ToEnd:     from = cur.col; break;
    case ZzEraseMode::FromStart: to = cur.col;   break;
    case ZzEraseMode::All:       break;
    }
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    ZzLine& line = buf.lines[static_cast<std::size_t>(cur.row)];
    for (int c = from; c <= to; ++c)
        line.setCell(c, fill);
    markDirty(cur.row, from);
    markDirty(cur.row, to);
}

void ZzScreen::eraseInDisplay(ZzEraseMode mode, const ZzCell& fill) noexcept
{
    const ZzPosition cur = cursor().position;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    ZzLine& cursorLine = buf.lines[static_cast<std::size_t>(cur.row)];
    switch (mode) {
    case ZzEraseMode::ToEnd:
        // 光标（含）到行尾 + 光标以下整屏。
        for (int c = cur.col; c < cols_; ++c)
            cursorLine.setCell(c, fill);
        for (int r = cur.row + 1; r < rows_; ++r)
            buf.lines[static_cast<std::size_t>(r)].clear(fill);
        for (int r = cur.row; r < rows_; ++r)
            markRowDirty(r);
        break;
    case ZzEraseMode::FromStart:
        // 屏幕开头到光标（含）。
        for (int r = 0; r < cur.row; ++r)
            buf.lines[static_cast<std::size_t>(r)].clear(fill);
        for (int c = 0; c <= cur.col; ++c)
            cursorLine.setCell(c, fill);
        for (int r = 0; r <= cur.row; ++r)
            markRowDirty(r);
        break;
    case ZzEraseMode::All:
        for (int r = 0; r < rows_; ++r)
            buf.lines[static_cast<std::size_t>(r)].clear(fill);
        markAllDirty();
        break;
    }
}

void ZzScreen::insertCells(int count, const ZzCell& fill) noexcept
{
    const ZzPosition cur = cursor().position;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.lines[static_cast<std::size_t>(cur.row)].insertCells(cur.col, count, fill);
    markDirty(cur.row, cur.col);
    markDirty(cur.row, cols_ - 1);
}

void ZzScreen::deleteCells(int count, const ZzCell& fill) noexcept
{
    const ZzPosition cur = cursor().position;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.lines[static_cast<std::size_t>(cur.row)].eraseCells(cur.col, count, fill);
    markDirty(cur.row, cur.col);
    markDirty(cur.row, cols_ - 1);
}

void ZzScreen::insertLines(int count, const ZzCell& fill)
{
    const int row = cursor().position.row;
    if (row < scrollTop_ || row > scrollBottom_)
        return;
    scrollRegionDown(row, scrollBottom_, count, fill);
}

void ZzScreen::deleteLines(int count, const ZzCell& fill)
{
    const int row = cursor().position.row;
    if (row < scrollTop_ || row > scrollBottom_)
        return;
    scrollRegionUp(row, scrollBottom_, count, fill);
}

void ZzScreen::scrollUp(int count, const ZzCell& fill)
{
    scrollRegionUp(scrollTop_, scrollBottom_, count, fill);
}

void ZzScreen::scrollDown(int count, const ZzCell& fill)
{
    scrollRegionDown(scrollTop_, scrollBottom_, count, fill);
}

std::uint64_t ZzScreen::dirtyGeneration() const noexcept
{
    return dirtyGeneration_;
}

bool ZzScreen::rowDirty(int row) const noexcept
{
    if (row < 0 || row >= rows_)
        return false;
    const Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    return buf.dirtyRows[static_cast<std::size_t>(row)] != 0;
}

ZzCellRange ZzScreen::dirtyRange(int row) const noexcept
{
    if (!rowDirty(row))
        return ZzCellRange{};
    const Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    return buf.dirtyRanges[static_cast<std::size_t>(row)];
}

std::vector<int> ZzScreen::dirtyRows() const
{
    std::vector<int> result;
    for (int r = 0; r < rows_; ++r) {
        if (rowDirty(r))
            result.push_back(r);
    }
    return result;
}

void ZzScreen::clearDirty() noexcept
{
    for (Buffer* buf : {&primary_, &alternate_}) {
        std::fill(buf->dirtyRows.begin(), buf->dirtyRows.end(), char{0});
        std::fill(buf->dirtyRanges.begin(), buf->dirtyRanges.end(), ZzCellRange{});
    }
}

void ZzScreen::setScrollOutCallback(ScrollOutCallback callback)
{
    scrollOutCallback_ = std::move(callback);
}

void ZzScreen::markDirty(int row, int col) noexcept
{
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.dirtyRows[static_cast<std::size_t>(row)] = 1;
    ZzCellRange& range = buf.dirtyRanges[static_cast<std::size_t>(row)];
    if (range.empty()) {
        range = ZzCellRange{col, col + 1};
    } else {
        range.startCol = std::min(range.startCol, col);
        range.endCol = std::max(range.endCol, col + 1);
    }
    ++dirtyGeneration_;
}

void ZzScreen::markRowDirty(int row) noexcept
{
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.dirtyRows[static_cast<std::size_t>(row)] = 1;
    buf.dirtyRanges[static_cast<std::size_t>(row)] = ZzCellRange{0, cols_};
    ++dirtyGeneration_;
}

void ZzScreen::markAllDirty() noexcept
{
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    std::fill(buf.dirtyRows.begin(), buf.dirtyRows.end(), char{1});
    std::fill(buf.dirtyRanges.begin(), buf.dirtyRanges.end(), ZzCellRange{0, cols_});
    ++dirtyGeneration_;
}

bool ZzScreen::regionIsFullHeight() const noexcept
{
    return scrollTop_ == 0 && scrollBottom_ == rows_ - 1;
}

void ZzScreen::scrollRegionUp(int top, int bottom, int count, const ZzCell& fill)
{
    if (count <= 0 || top > bottom)
        return;
    count = std::min(count, bottom - top + 1);
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;

    // 滚出上沿的行：仅在 Primary + 全屏滚动区时进入历史回调。
    if (active_ == ZzScreenBuffer::Primary && regionIsFullHeight() && scrollOutCallback_) {
        std::vector<ZzLine> scrolled;
        scrolled.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
            scrolled.push_back(std::move(buf.lines[static_cast<std::size_t>(top + i)]));
        scrollOutCallback_(std::move(scrolled));
    }

    const auto first = buf.lines.begin() + top;
    const auto last = buf.lines.begin() + bottom + 1;
    std::move(first + count, last, first);
    for (auto it = last - count; it != last; ++it)
        it->clear(fill);
    for (int r = top; r <= bottom; ++r)
        markRowDirty(r);
}

void ZzScreen::scrollRegionDown(int top, int bottom, int count, const ZzCell& fill)
{
    if (count <= 0 || top > bottom)
        return;
    count = std::min(count, bottom - top + 1);
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    const auto first = buf.lines.begin() + top;
    const auto last = buf.lines.begin() + bottom + 1;
    std::move_backward(first, last - count, last);
    for (auto it = first; it != first + count; ++it)
        it->clear(fill);
    for (int r = top; r <= bottom; ++r)
        markRowDirty(r);
}
