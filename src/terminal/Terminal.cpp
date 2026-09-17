#include "ZzTerm/Terminal.h"

// ZzTerminal 实现骨架（M0）。
//
// 重要说明：Parser 模块（ZzVtParser）尚未接入，feed() 当前为占位实现——
// 仅处理可打印 ASCII 与 CR/LF/BS/HT/BEL，其余字节安全忽略。
// 接入 Parser 后，feed 的字节流先经 ZzVtParser 增量解析，再由 Terminal
// 做语义 dispatch；本文件中的写入/滚动协调逻辑（光标推进、DECAWM 换行、
// 滚出行入历史）届时保留，逐字节 switch 移除。

ZzTerminal::ZzTerminal(int cols, int rows, std::size_t scrollbackMaxLines)
    : screen_(cols, rows)
    , scrollback_(zzCreateChunkedScrollback(scrollbackMaxLines))
    , renderView_(&screen_, scrollback_.get())
{
    // Screen 不知道历史后端：滚出行经回调上移到 Terminal，由 Terminal 入栈。
    screen_.setScrollOutCallback([this](std::vector<ZzLine> lines) {
        scrolledOutPending_ += lines.size();
        scrollback_->append(std::move(lines));
    });
}

ZzTerminal::~ZzTerminal() = default;

ZzTermChanges ZzTerminal::feed(std::span<const std::byte> data)
{
    ZzTermChanges changes;
    scrolledOutPending_ = 0;

    const ZzSize sz = screen_.size();
    const ZzCellRange region = screen_.scrollRegionRows(); // [top, bottom+1)

    for (const std::byte b : data) {
        const unsigned char c = static_cast<unsigned char>(b);
        ZzPosition cur = screen_.cursor().position;

        if (c >= 0x20 && c < 0x7F) {
            // 可打印 ASCII：窄单元格写入（M0 占位，无 SGR 画笔，默认属性）。
            ZzCell cell;
            cell.setWidth(ZzCellWidth::Narrow);
            cell.setCodePoint(static_cast<char32_t>(c));
            screen_.putCell(cur, cell);
            changes.screenDirty = true;

            if (cur.col >= sz.cols - 1) {
                // 行末：DECAWM 开则软换行到下一行（必要时滚动）。
                if (screen_.autoWrapMode()) {
                    screen_.setLineWrapped(cur.row, true);
                    if (cur.row >= region.endCol - 1) {
                        screen_.scrollUp(1, ZzCell{});
                    } else {
                        cur.row += 1;
                    }
                    cur.col = 0;
                    screen_.setCursorPosition(cur);
                }
            } else {
                cur.col += 1;
                screen_.setCursorPosition(cur);
            }
            continue;
        }

        switch (c) {
        case '\r': // CR：回列首。
            screen_.setCursorPosition(ZzPosition{cur.row, 0});
            break;
        case '\n': // LF：下一行；位于滚动区下沿则上滚。
        case 0x0B: // VT
        case 0x0C: // FF
            if (cur.row >= region.endCol - 1)
                screen_.scrollUp(1, ZzCell{});
            else
                screen_.setCursorPosition(ZzPosition{cur.row + 1, cur.col});
            changes.screenDirty = true;
            break;
        case '\b': // BS：左移一格。
            screen_.setCursorPosition(ZzPosition{cur.row, cur.col > 0 ? cur.col - 1 : 0});
            break;
        case '\t': // HT：下一个 Tab Stop。
            screen_.setCursorPosition(ZzPosition{cur.row, screen_.nextTabStop(cur.col)});
            break;
        case 0x07: // BEL。
            changes.bell = true;
            break;
        default:
            // 其余字节（含 ESC 起始序列）：M0 安全忽略，待 Parser 接入。
            break;
        }
    }

    if (scrolledOutPending_ > 0) {
        changes.scrollbackChanged = true;
        changes.scrolledOutLines = scrolledOutPending_;
        scrolledOutPending_ = 0;
    }
    return changes;
}

bool ZzTerminal::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return false;
    if (screen_.size() == ZzSize{cols, rows})
        return false;
    // M0：网格级 resize，不做 reflow（见 Terminal.h 注释）。
    screen_.resize(cols, rows);
    return true;
}

const ZzRenderView& ZzTerminal::renderView() const noexcept
{
    return renderView_;
}

ZzSize ZzTerminal::size() const noexcept
{
    return screen_.size();
}

ZzCursorState ZzTerminal::cursor() const noexcept
{
    return screen_.cursor();
}

bool ZzTerminal::isAlternateScreen() const noexcept
{
    return screen_.activeBuffer() == ZzScreenBuffer::Alternate;
}

const std::string& ZzTerminal::title() const noexcept
{
    return title_;
}

void ZzTerminal::clearDirty() noexcept
{
    screen_.clearDirty();
}

ZzScreen& ZzTerminal::screen() noexcept
{
    return screen_;
}

ZzScrollback& ZzTerminal::scrollback() noexcept
{
    return *scrollback_;
}
