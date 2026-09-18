#include <algorithm>

#include "ZzTerm/Terminal.h"
#include "ZzTerm/Parser.h"

// CSI 语义（ZzTerminal 成员函数，分文件实现以控制单文件规模）。
// 约定：参数省略（kOmitted）或 <= 0 一律回退默认值；数值钳到网格范围；
// DEC 私有序列（privateMarker != 0）与带 intermediate 的序列本文件安全忽略：
// 备用屏幕（1049/1047/1048）、DECAWM（?7）、DECTCEM（?25）属 M2，
// 其余 DEC 私有模式（mouse/bracketed paste 等）与 intermediate 序列属 M3
//（里程碑划分见 Architecture.md 第 19 节）。

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

void ZzTerminal::dispatchCsi(const ZzParamSequence& seq)
{
    if (seq.privateMarker != 0 || !seq.intermediates.empty())
        return; // DEC 私有 / intermediate 序列：安全忽略（M2/M3 处理，见文件头注释）

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
    case 'm':
        sgr(seq); // 任务 6 实现
        break;
    default:
        return; // 未知 final 安全忽略（不标脏）
    }
    noteScreenDirty();
}
