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

/**
 * @brief 流式 reflow 器（M8b）：逐批喂入物理行、产出重组后物理行。
 *
 * 跨批只携带未完成链，峰值内存 O(链长)——zzReflowLines 全量进/出为
 * O(全历史)，百万行下产生约 2 倍瞬时峰值。语义与 zzReflowLines 逐字节
 * 一致（共用 zzReflowChain 核）；不支持光标跟踪（scrollback 路径不需要，
 * screen 路径继续走 zzReflowLines）。
 *
 * 前置约定（调用方保证，debug 断言看护）：oldCols/newCols 均大于 0 且不相等
 *（恒等与非法路径由调用方前置过滤，对齐 zzReflowLines 早退分支语义）；
 * 喂入行均为 oldCols 列（全历史同宽不变量）。
 */
class ZzReflowStreamer {
public:
    /// @brief 构造。oldCols/newCols 语义同 zzReflowLines。
    ZzReflowStreamer(int oldCols, int newCols);

    /// @brief 喂入一批物理行（move 消费，返回后 lines 处于移后状态），产出追加到 out。
    /// @param lines 一批物理行（按 wrapped 链序）。
    /// @param out 重组产出行（追加写，调用方持有）。
    void feed(std::vector<ZzLine>& lines, std::vector<ZzLine>& out);

    /// @brief 收尾：冲刷最后一条未完成链（无暂存时为空操作）。
    /// @param out 重组产出行（追加写）。
    void finish(std::vector<ZzLine>& out);

private:
    int oldCols_;
    int newCols_;
    std::vector<ZzLine> pending_; ///< 未完成链暂存（复用缓冲，避免逐链分配）。
};
