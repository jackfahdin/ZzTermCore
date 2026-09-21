#pragma once

// 内部头（不安装）：resize reflow 共享纯函数（M4）。
// 物理行序列沿 wrapped 链合并为逻辑行后按新列宽重切；
// ZzScreen::reflow 与 ZzChunkedScrollback::reflow 两端共用，杜绝算法漂移。

#include <cstddef>
#include <vector>

#include "ZzTerm/Line.h"

/// @brief reflow 光标跟踪：输入链坐标，输出重组后物理坐标。
struct ZzReflowCursor {
    std::size_t chainIndex = 0; ///< 输入：光标所在逻辑行链序号（0 起）。
    int chainOffset = 0;        ///< 输入：链内流偏移（单元格，含各物理行整行宽）。
    int row = 0;                ///< 输出：重组后物理行号。
    int col = 0;                ///< 输出：重组后物理列号。
};

/**
 * @brief 将物理行序列从旧列宽重组到新列宽（soft-wrap reflow）。
 * @param lines 物理行序列（按值传入，调用方可 move；函数不保留引用）。
 * @param oldCols 旧列宽（大于 0，不变量：所有行均为该宽度）。
 * @param newCols 新列宽（大于 0）。
 * @param cursor 可选光标跟踪（nullptr 表示不跟踪）。
 * @return 重组后的物理行序列（每行 newCols 列，wrapped 标记已重算）。
 * @note 硬行（未 wrapped 的单行链）截断/补空，永不多行化；
 *       链末尾的完全默认空白格被裁除（避免短行变窄产生幽灵行）；
 *       宽字符原子搬运不落边界（边界前移一格补默认空白）；
 *       cluster 格在新行重新 internCluster。
 */
std::vector<ZzLine> zzReflowLines(std::vector<ZzLine> lines, int oldCols, int newCols,
                                  ZzReflowCursor* cursor = nullptr);
