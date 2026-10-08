#include "ZzTerm/Screen.h"

#include <algorithm>
#include <iterator>

#include "Reflow.h"

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
    resizeBuffer(primary_, cols, rows, true);    // M15：Primary 行变条件语义
    resizeBuffer(alternate_, cols, rows, false); // Alternate 无历史：尾部截断/补空
    cols_ = cols;
    rows_ = rows;
    tabStops_.resize(static_cast<std::size_t>(cols), 0);
    scrollTop_ = 0;
    scrollBottom_ = rows - 1;
    ++dirtyGeneration_;
    markAllDirty();
}

// M15：行变条件语义（对齐 contour shrinkLines/growLines，规格 §4）。
// 缩行：先裁光标下方行（不入历史），不够裁时顶部行经 ScrollOutCallback
// 压入历史（无回调则丢弃，同 reflowBuffer 溢出语义）；光标随内容平移。
// 扩行：光标贴末行时经 HistoryPullCallback 回抽注入顶部，不足底部补空。
void ZzScreen::resizeBuffer(Buffer& buf, int cols, int rows, bool mayUseHistory)
{
    const int oldRows = static_cast<int>(buf.lines.size());
    if (rows < oldRows) {
        const int k = oldRows - rows;
        if (mayUseHistory) {
            const int below  = oldRows - 1 - buf.cursor.position.row;
            const int cutoff = std::min(k, below); // 光标下方行直接裁（不入历史）
            buf.lines.erase(buf.lines.end() - cutoff, buf.lines.end());
            const int pushUp = k - cutoff; // 不足部分顶部压入历史（此时光标必贴底）
            if (pushUp > 0) {
                if (scrollOutCallback_) {
                    std::vector<ZzLine> spilled;
                    spilled.reserve(static_cast<std::size_t>(pushUp));
                    for (int i = 0; i < pushUp; ++i)
                        spilled.push_back(std::move(buf.lines[static_cast<std::size_t>(i)]));
                    scrollOutCallback_(std::move(spilled));
                }
                buf.lines.erase(buf.lines.begin(), buf.lines.begin() + pushUp);
                buf.cursor.position.row -= pushUp;
            }
        } else {
            buf.lines.resize(static_cast<std::size_t>(rows)); // Alternate：尾部截断
        }
    } else if (rows > oldRows) {
        const int k = rows - oldRows;
        if (mayUseHistory && historyPullCallback_
            && buf.cursor.position.row == oldRows - 1) { // 光标贴末行才回抽
            auto pulled = historyPullCallback_(static_cast<std::size_t>(k));
            if (!pulled.empty()) {
                buf.lines.insert(buf.lines.begin(),
                                 std::make_move_iterator(pulled.begin()),
                                 std::make_move_iterator(pulled.end()));
                buf.cursor.position.row += static_cast<int>(pulled.size());
            }
        }
        buf.lines.resize(static_cast<std::size_t>(rows)); // 不足部分底部补空
    }
    // 列向：逐行截断/填充（无 reflow，维持 M0 语义）；新补空行同获列宽。
    for (auto& line : buf.lines)
        if (line.cellCount() != cols)
            line.resize(cols);
    buf.dirtyRows.assign(buf.lines.size(), 1);
    buf.dirtyRanges.assign(buf.lines.size(), ZzCellRange{0, cols});
    buf.cursor.position.row = std::clamp(buf.cursor.position.row, 0, rows - 1);
    buf.cursor.position.col = std::clamp(buf.cursor.position.col, 0, cols - 1);
    buf.wrapPending = false;
}

void ZzScreen::reflow(int newCols)
{
    if (newCols <= 0 || newCols == cols_)
        return;
    reflowBuffer(primary_, newCols, true);
    reflowBuffer(alternate_, newCols, false);
    cols_ = newCols;
    tabStops_.assign(static_cast<std::size_t>(cols_), 0); // tab stops 不跨列宽保留
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    ++dirtyGeneration_;
    markAllDirty();
}

void ZzScreen::prependPrimaryLines(std::vector<ZzLine> lines)
{
    if (lines.empty())
        return;
    const auto count = static_cast<int>(lines.size());
    primary_.lines.insert(primary_.lines.begin(),
                          std::make_move_iterator(lines.begin()),
                          std::make_move_iterator(lines.end()));
    // reflowBuffer 的光标→链坐标换算从 cursorRow 回找链头，必须随插入平移。
    primary_.cursor.position.row += count;
    primary_.wrapPending = false;
    // 行数瞬时超 rows_：由随后的 reflow() 溢出分支裁回（出口恒 rows_）；
    // dirtyRows/dirtyRanges 尺寸不动——对外查询以 rows_ 为界，reflow 后
    // markAllDirty 自洽（M16b 调研 §3 验证）。
}

void ZzScreen::reflowBuffer(Buffer& buf, int newCols, bool mayScrollOut)
{
    // 光标 -> 链坐标：向上找链起点，统计链序号，偏移 = 链内整行宽累加 + 光标列。
    const int cursorRow = buf.cursor.position.row;
    int chainStartRow = cursorRow;
    while (chainStartRow > 0 && buf.lines[static_cast<std::size_t>(chainStartRow - 1)].wrapped())
        --chainStartRow;
    ZzReflowCursor track;
    {
        std::size_t index = 0;
        int r = 0;
        const int total = static_cast<int>(buf.lines.size());
        while (r < chainStartRow) { // 逐链跳过
            ++index;
            while (r < total && buf.lines[static_cast<std::size_t>(r)].wrapped())
                ++r;
            ++r; // 链末行
        }
        track.chainIndex = index;
        track.chainOffset = (cursorRow - chainStartRow) * cols_ + buf.cursor.position.col;
    }

    // M17a：Primary 且扩列且光标在折链（链 >= 2 行）上时豁免收链——
    // readline 的 WINCH 重绘按旧布局帧发相对擦除，豁免让擦除命中
    // 提示符碎片行而非收链后的无辜内容行（规格 2026-10-08-m17a §3）。
    const bool preserveCursorChain =
        mayScrollOut && newCols > cols_
        && buf.lines[static_cast<std::size_t>(chainStartRow)].wrapped();
    std::vector<ZzLine> out =
        zzReflowLines(buf.lines, cols_, newCols, &track,
                      preserveCursorChain ? ZzReflowCursorChain::Preserve
                                          : ZzReflowCursorChain::Reflow);

    // 行数平衡：溢出上移（仅 Primary）或丢弃（Alternate）；
    // 不足时先经 HistoryPullCallback 从最新历史顶补（M16c，仅 Primary 且
    // 装有回调——对齐 contour 统一流尾部窗口的净效果），余量底部补空。
    if (static_cast<int>(out.size()) > rows_) {
        const int overflow = static_cast<int>(out.size()) - rows_;
        if (mayScrollOut && scrollOutCallback_) {
            std::vector<ZzLine> spilled;
            spilled.reserve(static_cast<std::size_t>(overflow));
            for (int k = 0; k < overflow; ++k)
                spilled.push_back(std::move(out[static_cast<std::size_t>(k)]));
            scrollOutCallback_(std::move(spilled));
        }
        out.erase(out.begin(), out.begin() + overflow);
        track.row -= overflow;
    } else if (static_cast<int>(out.size()) < rows_) {
        int deficit = rows_ - static_cast<int>(out.size());
        if (mayScrollOut && historyPullCallback_) {
            std::vector<ZzLine> pulled =
                historyPullCallback_(static_cast<std::size_t>(deficit));
            if (!pulled.empty()) {
                const auto pulledCount = static_cast<int>(pulled.size());
                out.insert(out.begin(), std::make_move_iterator(pulled.begin()),
                           std::make_move_iterator(pulled.end()));
                track.row += pulledCount;
                deficit -= pulledCount;
            }
        }
        for (; deficit > 0; --deficit)
            out.push_back(ZzLine(newCols));
    }

    buf.lines = std::move(out);
    buf.cursor.position.row = std::clamp(track.row, 0, rows_ - 1);
    buf.cursor.position.col = std::clamp(track.col, 0, newCols - 1);
    buf.wrapPending = false;
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

std::uint32_t ZzScreen::internClusterAt(int row, std::string_view utf8)
{
    // 与 putCell 同通道取可变行；row 界内前提同 lineAt 注释约定（越界忽略）。
    if (row < 0 || row >= rows_)
        return 0;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    return buf.lines[static_cast<std::size_t>(row)].internCluster(utf8);
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
    buf.wrapPending = false;
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
    const Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    savedCursor_ = cursor();
    savedWrapPending_ = buf.wrapPending;
    hasSavedCursor_ = true;
}

void ZzScreen::restoreCursor() noexcept
{
    if (!hasSavedCursor_) {
        setCursorPosition(ZzPosition{0, 0});
        return;
    }
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.cursor = savedCursor_;
    setCursorPosition(savedCursor_.position); // 最后钳制，防止 resize 后越界
    buf.wrapPending = savedWrapPending_;      // setCursorPosition 会清标志，须在其后恢复
}

bool ZzScreen::wrapPending() const noexcept
{
    const Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    return buf.wrapPending;
}

void ZzScreen::setWrapPending(bool pending) noexcept
{
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.wrapPending = pending;
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
    // 复用 ZzCellRange 的半开区间约定：startCol 存上沿 top（含），endCol 存 bottom + 1（不含）。
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
    buf.wrapPending = false;
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
    buf.wrapPending = false;
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
    buf.wrapPending = false;
    buf.lines[static_cast<std::size_t>(cur.row)].insertCells(cur.col, count, fill);
    markDirty(cur.row, cur.col);
    markDirty(cur.row, cols_ - 1);
}

void ZzScreen::deleteCells(int count, const ZzCell& fill) noexcept
{
    const ZzPosition cur = cursor().position;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.wrapPending = false;
    buf.lines[static_cast<std::size_t>(cur.row)].eraseCells(cur.col, count, fill);
    markDirty(cur.row, cur.col);
    markDirty(cur.row, cols_ - 1);
}

void ZzScreen::insertLines(int count, const ZzCell& fill)
{
    const int row = cursor().position.row;
    if (row < scrollTop_ || row > scrollBottom_)
        return;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.wrapPending = false;
    scrollRegionDown(row, scrollBottom_, count, fill);
}

void ZzScreen::deleteLines(int count, const ZzCell& fill)
{
    const int row = cursor().position.row;
    if (row < scrollTop_ || row > scrollBottom_)
        return;
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.wrapPending = false;
    scrollRegionUp(row, scrollBottom_, count, fill);
}

void ZzScreen::scrollUp(int count, const ZzCell& fill)
{
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.wrapPending = false;
    scrollRegionUp(scrollTop_, scrollBottom_, count, fill);
}

void ZzScreen::scrollDown(int count, const ZzCell& fill)
{
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.wrapPending = false;
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

void ZzScreen::setHistoryPullCallback(HistoryPullCallback callback)
{
    historyPullCallback_ = std::move(callback);
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
    for (auto it = last - count; it != last; ++it) {
        // std::move 移位留下被掏空的行（cells_ 为空的 husk），
        // clear 只是 std::fill 无法恢复列数，必须先 resize 回满列。
        it->resize(cols_, fill);
        it->clear(fill);
    }
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
    for (auto it = first; it != first + count; ++it) {
        // 同 scrollRegionUp：恢复被 move 掏空的行到满列。
        it->resize(cols_, fill);
        it->clear(fill);
    }
    for (int r = top; r <= bottom; ++r)
        markRowDirty(r);
}
