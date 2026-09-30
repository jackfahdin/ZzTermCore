#include <algorithm>
#include <string>

#include "ZzNativeBackend.h"

// CSI 语义（ZzNativeBackend 方法，分文件实现以控制单文件规模）。
// 约定：参数省略（kOmitted）或 <= 0 一律回退默认值；数值钳到网格范围。
// DEC 私有序列（privateMarker == '?'）走 dispatchDecPrivate：M2 已交付
// 备用屏幕（1049/1047/1048）、DECAWM（?7）、DECTCEM（?25），M3a 交付
// DECCKM（?1，同步输入编码器），M3b 交付鼠标上报（?9/?1000/?1002/?1003）、
// SGR 1006 编码、bracketed paste（?2004）、焦点上报（?1004，同步输入编码器）；
// 其余 DEC 私有模式与 intermediate 序列安全忽略（里程碑划分见 Architecture.md 第 19 节）。

namespace {

/// 取 CSI 第 i 个参数；省略/0/缺槽回退 fallback。
int paramOr(const ZzParamSequence& seq, std::size_t i, int fallback)
{
    if (i >= seq.params.size())
        return fallback;
    const std::int32_t v = seq.params[i];
    return (v == ZzParamSequence::kOmitted || v <= 0) ? fallback : static_cast<int>(v);
}

} // namespace

void ZzNativeBackend::dispatchCsi(const ZzParamSequence& seq)
{
    if (seq.privateMarker == '?') {
        if (seq.intermediates.empty())
            dispatchDecPrivate(seq);
        return; // 带 intermediate 的 DEC 私有：安全忽略（M3）
    }
    if (seq.privateMarker != 0 || !seq.intermediates.empty())
        return; // 其余私有 / intermediate 序列：安全忽略（M3，见文件头注释）

    const ZzSize sz = screen_.size();
    const ZzCellRange region = screen_.scrollRegionRows();
    const int regionTop = region.startCol;
    const int regionBottom = region.endCol - 1;
    const ZzPosition cur = screen_.cursor().position;
    const bool inRegion = cur.row >= regionTop && cur.row <= regionBottom;

    switch (seq.final) {
    case 'A': { // CUU：上移到滚动区上沿为止（在区内时）
        const int n = paramOr(seq, 0, 1);
        const int limit = inRegion ? regionTop : 0;
        screen_.setCursorPosition(ZzPosition{std::max(limit, cur.row - n), cur.col});
        break;
    }
    case 'B': { // CUD：下移
        const int n = paramOr(seq, 0, 1);
        const int limit = inRegion ? regionBottom : sz.rows - 1;
        screen_.setCursorPosition(ZzPosition{std::min(limit, cur.row + n), cur.col});
        break;
    }
    case 'C': // CUF
        screen_.setCursorPosition(
            ZzPosition{cur.row, std::min(sz.cols - 1, cur.col + paramOr(seq, 0, 1))});
        break;
    case 'D': // CUB
        screen_.setCursorPosition(
            ZzPosition{cur.row, std::max(0, cur.col - paramOr(seq, 0, 1))});
        break;
    case 'E': { // CNL = CUD + CR
        const int n = paramOr(seq, 0, 1);
        const int limit = inRegion ? regionBottom : sz.rows - 1;
        screen_.setCursorPosition(ZzPosition{std::min(limit, cur.row + n), 0});
        break;
    }
    case 'F': { // CPL = CUU + CR
        const int n = paramOr(seq, 0, 1);
        const int limit = inRegion ? regionTop : 0;
        screen_.setCursorPosition(ZzPosition{std::max(limit, cur.row - n), 0});
        break;
    }
    case 'G': // CHA：绝对列（1 起始）
        screen_.setCursorPosition(
            ZzPosition{cur.row, std::clamp(paramOr(seq, 0, 1) - 1, 0, sz.cols - 1)});
        break;
    case 'd': // VPA：绝对行（1 起始）
        screen_.setCursorPosition(
            ZzPosition{std::clamp(paramOr(seq, 0, 1) - 1, 0, sz.rows - 1), cur.col});
        break;
    case 'H': // CUP
    case 'f': { // HVP
        int row = paramOr(seq, 0, 1) - 1;
        const int col = paramOr(seq, 1, 1) - 1;
        if (screen_.originMode()) {
            // DECOM：相对滚动区上沿，且钳在滚动区内。
            row = std::clamp(row + regionTop, regionTop, regionBottom);
        }
        screen_.setCursorPosition(ZzPosition{std::clamp(row, 0, sz.rows - 1),
                                             std::clamp(col, 0, sz.cols - 1)});
        break;
    }
    case 's': // SCOSC（无左右边距模式时 CSI s = 保存光标）
        screen_.saveCursor();
        break;
    case 'r': // DECSTBM：滚动区（1 起始，含端点；省略回退边界）。
              // 非法参数由 ZzScreen::setScrollRegion 复位为全屏。
        screen_.setScrollRegion(paramOr(seq, 0, 1) - 1, paramOr(seq, 1, sz.rows) - 1);
        break;
    case 'u': // SCORC
        screen_.restoreCursor();
        break;
    case 'J': { // ED 0/1/2；ED 3（清历史）不在 M1 范围，忽略
        const int p = paramOr(seq, 0, 0);
        if (p <= 2)
            screen_.eraseInDisplay(static_cast<ZzEraseMode>(p), eraseFill());
        break;
    }
    case 'K': { // EL 0/1/2
        const int p = paramOr(seq, 0, 0);
        if (p <= 2)
            screen_.eraseInLine(static_cast<ZzEraseMode>(p), eraseFill());
        break;
    }
    case 'X': { // ECH：原位擦除 n 格。Screen 无"原位擦除"原语，
                // 用 deleteCells + insertCells 组合实现：先删 n 格（左移、
                // 行尾补空），再在光标处插回 n 个空格（右移、截掉行尾补位），
                // 净效果即 [col, col+n) 置空、其余单元格不动。
        const int n = paramOr(seq, 0, 1);
        screen_.deleteCells(n, eraseFill());
        screen_.insertCells(n, eraseFill());
        break;
    }
    case '@': // ICH
        screen_.insertCells(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'P': // DCH
        screen_.deleteCells(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'L': // IL
        screen_.insertLines(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'M': // DL
        screen_.deleteLines(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'S': // SU
        screen_.scrollUp(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'T': // SD
        screen_.scrollDown(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'c': // DA1：省略/0 参数应答 VT102 级最小集（xterm 兼容）；回传不标脏
        if (paramOr(seq, 0, 0) == 0)
            emit("\x1B[?1;2c");
        return;
    case 'n': { // DSR：5=就绪；6=CPR（真实光标位置，1 起始）；其余安全忽略
        const int p = paramOr(seq, 0, 0);
        if (p == 5) {
            emit("\x1B[0n");
        } else if (p == 6) {
            const std::string cpr = "\x1B[" + std::to_string(cur.row + 1) + ";"
                                  + std::to_string(cur.col + 1) + "R";
            emit(cpr);
        }
        return;
    }
    case 'm':
        sgr(seq); // 任务 6 实现
        break;
    default:
        return; // 未知 final 安全忽略（不标脏）
    }
    noteScreenDirty();
}

void ZzNativeBackend::dispatchDecPrivate(const ZzParamSequence& seq)
{
    const bool set = (seq.final == 'h');
    if (!set && seq.final != 'l')
        return; // DEC 私有非 h/l final：安全忽略
    // CSI ? Pm h/l 可携带多个模式参数，逐个应用（如 ESC[?1049;25h）。
    for (const std::int32_t p : seq.params) {
        if (p == ZzParamSequence::kOmitted || p <= 0)
            continue;
        switch (static_cast<int>(p)) {
        case 1: // DECCKM：application cursor keys（同步到输入编码器）
            encoder_.setApplicationCursorKeys(set);
            break;
        case 7: // DECAWM 自动换行（默认开）
            screen_.setAutoWrapMode(set);
            break;
        case 25: { // DECTCEM 光标可见性（形状/闪烁位不动）
            const ZzCursorState cur = screen_.cursor();
            screen_.setCursorStyle(cur.shape, set, cur.blinking);
            break;
        }
        // 以下四档 l 一律清 None 而不论当前激活档位（xterm 分档语义的理论出入；
        // 现实应用只 reset 自己 set 的模式，无实际影响，钉住）。
        case 9: // X10 鼠标（仅按下）
            encoder_.setMouseReportMode(set ? ZzMouseReportMode::X10 : ZzMouseReportMode::None);
            break;
        case 1000: // Normal 鼠标（按下+释放）
            encoder_.setMouseReportMode(set ? ZzMouseReportMode::Normal : ZzMouseReportMode::None);
            break;
        case 1002: // Button-event 鼠标（+按下时拖动）
            encoder_.setMouseReportMode(set ? ZzMouseReportMode::ButtonEvent
                                            : ZzMouseReportMode::None);
            break;
        case 1003: // Any-event 鼠标（+任意移动）
            encoder_.setMouseReportMode(set ? ZzMouseReportMode::AnyEvent
                                            : ZzMouseReportMode::None);
            break;
        case 1004: // 焦点上报（CSI I/O）
            encoder_.setFocusReporting(set);
            break;
        case 1006: // SGR 1006 鼠标编码格式
            encoder_.setMouseSgrEncoding(set);
            break;
        case 2004: // bracketed paste
            encoder_.setBracketedPaste(set);
            break;
        case 1047: // 使用备用屏幕（进入清屏），不动光标保存
            if (set)
                switchToAlternate(false);
            else
                switchToPrimary(false);
            break;
        case 1048: // 仅保存/恢复光标
            if (set)
                screen_.saveCursor();
            else
                screen_.restoreCursor();
            break;
        case 1049: // = 1048（保存光标）+ 1047（切 alt 清屏）；退出恢复光标
            if (set)
                switchToAlternate(true);
            else
                switchToPrimary(true);
            break;
        default:
            continue; // 其余 DEC 私有模式：M3，安全忽略（不标脏）
        }
        noteScreenDirty();
    }
}

void ZzNativeBackend::switchToAlternate(bool saveCur)
{
    ++historyGeneration_; // M14：Alternate 切换改变可见历史（lineCount 归零/恢复）
    if (saveCur)
        screen_.saveCursor();
    if (screen_.activeBuffer() != ZzScreenBuffer::Alternate) {
        // 保存主屏滚动区；alt 期间复位为全屏（xterm 语义），回主屏时恢复。
        const ZzCellRange r = screen_.scrollRegionRows();
        savedScrollTop_ = r.startCol;
        savedScrollBottom_ = r.endCol - 1;
        hasSavedScrollRegion_ = true;
        screen_.setActiveBuffer(ZzScreenBuffer::Alternate);
        screen_.resetScrollRegion();
        if (activeChanges_)
            activeChanges_->activeBufferChanged = true;
    }
    // xterm：1049h/1047h 进入 alt 均清全屏；alt 光标为该 buffer 自存位置
    // （初次进入即原点），eraseInDisplay 不动光标。
    screen_.eraseInDisplay(ZzEraseMode::All, eraseFill());
    noteScreenDirty();
}

void ZzNativeBackend::switchToPrimary(bool restoreCur)
{
    ++historyGeneration_; // M14：Alternate 切换改变可见历史（lineCount 归零/恢复）
    if (screen_.activeBuffer() == ZzScreenBuffer::Primary)
        return; // 幂等
    screen_.setActiveBuffer(ZzScreenBuffer::Primary);
    if (activeChanges_)
        activeChanges_->activeBufferChanged = true;
    if (hasSavedScrollRegion_) {
        screen_.setScrollRegion(savedScrollTop_, savedScrollBottom_);
        hasSavedScrollRegion_ = false;
    }
    if (restoreCur)
        screen_.restoreCursor();
    noteScreenDirty();
}
