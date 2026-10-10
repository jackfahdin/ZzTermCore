#pragma once

#include <cstdint>

#include "ZzTerm/Export.h"

/**
 * @file Types.h
 * @brief ZzTermCore 公共基础类型：坐标、尺寸、矩形区域、光标形状等。
 *
 * 坐标约定：除特别注明外，所有公开 API 中的 (row, col) 均为 0 起始，
 * row 向下递增，col 向右递增；终端协议层面的 1 起始坐标在
 * Terminal 语义层完成换算，Core 内部不出现 1 起始坐标。
 */

/// @brief 终端网格尺寸（单位：单元格）。
struct ZzSize {
    int cols = 0; ///< 列数（每行单元格数）。
    int rows = 0; ///< 行数（屏幕行数）。

    /**
     * @brief 判断尺寸是否合法（均为正）。
     * @return true 表示 cols 与 rows 均大于 0。
     */
    [[nodiscard]] constexpr bool valid() const noexcept { return cols > 0 && rows > 0; }

    /// @brief 相等比较（列数与行数均相等）。
    friend constexpr bool operator==(ZzSize, ZzSize) noexcept = default;
};

/// @brief 网格坐标（0 起始）。
struct ZzPosition {
    int row = 0; ///< 行号，向下递增。
    int col = 0; ///< 列号，向右递增。

    /// @brief 相等比较（行号与列号均相等）。
    friend constexpr bool operator==(ZzPosition, ZzPosition) noexcept = default;
};

/**
 * @brief 逻辑行坐标（选区/复制，M5a）。
 *
 * line：统一空间逻辑行序号。0 = 当前最早一条有效逻辑行（历史区头部），
 *       屏幕区紧跟其后；append 与滚动不改变已有内容的序号；历史头部丢弃时
 *       序号整体下移，Core 自动平移选区锚点。
 * col：逻辑行内单元格偏移（0 起，按格不按字符）；落在宽字符续格上时
 *      提取层归一到 lead 格。偏移为内容坐标（不含 wrapped 行尾部填充格，
 *      M17a-4b 起逐行裁尾），跨 resize 稳定。
 */
struct ZzLogicalPos {
    std::int64_t line = 0; ///< 逻辑行序号（统一空间）
    std::int32_t col = 0;  ///< 逻辑行内单元格偏移

    /// @brief 相等比较（逻辑行序号与格偏移均相等）。
    friend constexpr bool operator==(ZzLogicalPos, ZzLogicalPos) noexcept = default;
};

/**
 * @brief 逻辑行坐标区间（搜索 match，M5b）。
 *
 * 半开区间 [start, end)：start 为命中首格，end 为命中末格之后一格。
 * 坐标语义见 ZzLogicalPos；match 为搜索时刻的坐标快照，此后内容漂移不校验。
 */
struct ZzLogicalRange {
    ZzLogicalPos start; ///< 区间起点（含）
    ZzLogicalPos end;   ///< 区间终点（不含）
    /// @brief 相等比较（起点与终点均相等）。
    friend constexpr bool operator==(ZzLogicalRange, ZzLogicalRange) noexcept = default;
};

/**
 * @brief 拼接坐标（M17b 拼接行视图）：拼接行索引 + 行内拼接列。
 *
 * 拼接行 = 折链（wrapped 链）拼回的完整行；坐标空间与 ZzLogicalPos
 * 的物理行空间经 ZzUnwrapView::toStitched/fromStitched 双向换算。
 */
struct ZzStitchedPos {
    std::int64_t line = 0; ///< 拼接行序号（0 起）
    std::int32_t col  = 0; ///< 拼接行内单元格偏移

    /// @brief 相等比较（拼接行序号与拼接列均相等）。
    friend constexpr bool operator==(ZzStitchedPos, ZzStitchedPos) noexcept = default;
};

/**
 * @brief 搜索选项（M5b）。
 */
struct ZzSearchOptions {
    bool caseSensitive = true; ///< true = 大小写敏感；false = ASCII 大小写折叠（Unicode 不折叠）
};

/// @brief 矩形区域（闭区间语义由使用方注明，默认可为空区域）。
struct ZzRect {
    int topRow = 0;    ///< 起始行（含）。
    int leftCol = 0;   ///< 起始列（含）。
    int bottomRow = -1; ///< 结束行（含）；小于 topRow 表示空区域。
    int rightCol = -1;  ///< 结束列（含）；小于 leftCol 表示空区域。

    /**
     * @brief 区域是否为空。
     * @return true 表示 bottomRow < topRow 或 rightCol < leftCol。
     */
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return bottomRow < topRow || rightCol < leftCol;
    }
};

/// @brief 行内单元格范围，半开区间 [startCol, endCol)。
struct ZzCellRange {
    int startCol = 0; ///< 起始列（含）。
    int endCol = 0;   ///< 结束列（不含）；endCol <= startCol 表示空范围。

    /**
     * @brief 范围是否为空。
     * @return true 表示 endCol <= startCol。
     */
    [[nodiscard]] constexpr bool empty() const noexcept { return endCol <= startCol; }
};

/// @brief 光标形状（对应 DECSCUSR）。
enum class ZzCursorShape : std::uint8_t {
    Block,     ///< 方块光标。
    Underline, ///< 下划线光标。
    Bar        ///< 竖线（I-Beam）光标。
};

/// \brief 光标状态（位置 + 形状 + 可见性）。
struct ZzCursorState {
    ZzPosition    position;                          ///< 光标位置（行/列）。
    ZzCursorShape shape    = ZzCursorShape::Block;   ///< 光标形状（默认方块）。
    bool          visible  = true;                   ///< 光标是否可见。
    bool          blinking = true;                   ///< 光标是否闪烁。
    /// \brief 相等比较（逐成员）。
    friend constexpr bool operator==(const ZzCursorState&, const ZzCursorState&) noexcept = default;
};
