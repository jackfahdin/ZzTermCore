#pragma once

/// \file
/// \brief 拼接行视图（M17b）：把折链（wrapped 链）拼回完整行的只读视图。
/// 纯视图层——引擎存储/reflow/resize/搜索/选区语义不变。视图借用其来源
/// RenderView/HistoryView（经 ZzTerminal::unwrapView 获取），须与 feed 同
/// 线程使用；索引惰性重建，双代计数变化后的首次查询自动跟随最新内容。
/// 非线程安全。

#include <ZzTerm/Export.h>
#include <ZzTerm/HistoryView.h>
#include <ZzTerm/RenderView.h>
#include <ZzTerm/Types.h>

#include <cstddef>
#include <cstdint>
#include <memory>

class ZzUnwrapView;

/// \brief 拼接行只读句柄（M17b）：值语义，借用属主 ZzUnwrapView（须比句柄长寿）。
class ZZTERM_API ZzStitchedLineView final {
public:
    /**
     * @brief 拼接行有效全长（链内各物理行裁尾后有效段累加）。
     * @return 有效格数（空拼接行为 0）。
     */
    [[nodiscard]] int cellCount() const;
    /**
     * @brief 取拼接列 col 的单格视图（跨链寻址）。
     * @param col 拼接列（0 起；调用方保证 col < cellCount()）。
     * @return 该格的值语义单格视图。
     */
    [[nodiscard]] ZzCellView cellAt(int col) const;
    /**
     * @brief 链头统一物理行号（历史+屏幕统一物理空间；非 ZzLogicalPos::line
     *        的折链合并逻辑行号，两者仅无折链时相等）。
     * @return 统一物理行号（链头可能在历史区）。
     * @note 首次查询触发索引重建（分配内存），故不标 noexcept。
     */
    [[nodiscard]] std::int64_t sourceLine() const;
    /**
     * @brief 链行数。
     * @return 链内物理行数（1 = 无折）。
     * @note 首次查询触发索引重建（分配内存），故不标 noexcept。
     */
    [[nodiscard]] int sourceLineCount() const;

private:
    friend class ZzUnwrapView;
    ZzStitchedLineView(const ZzUnwrapView* owner, std::size_t index) noexcept
        : owner_(owner), index_(index) {}
    const ZzUnwrapView* owner_ = nullptr; ///< 属主视图（借用）
    std::size_t         index_ = 0;       ///< 拼接行索引
};

/// \brief 拼接行视图（M17b）：历史+屏幕统一空间按折链拼接的只读视图。
/// 经 ZzTerminal::unwrapView 获取；亦可由两个来源视图直接构造（测试/工具）。
class ZZTERM_API ZzUnwrapView final {
public:
    /**
     * @brief 以来源渲染视图与历史视图构造（ZzTerminal 内部接线使用）。
     * @param renderView 屏幕渲染视图（借用，须比本视图长寿）。
     * @param historyView 历史视图（借用，须比本视图长寿）。
     */
    ZzUnwrapView(const ZzRenderView& renderView, const ZzHistoryView& historyView);
    ~ZzUnwrapView();
    ZzUnwrapView(const ZzUnwrapView&) = delete;
    ZzUnwrapView& operator=(const ZzUnwrapView&) = delete;

    /**
     * @brief 拼接行数。
     * @return 拼接行条数（<= 统一空间物理行数；无折链时相等）。
     */
    [[nodiscard]] std::size_t lineCount() const;
    /**
     * @brief 第 index 条拼接行句柄。
     * @param index 拼接行索引（0 起；调用方保证 index < lineCount()）。
     * @return 拼接行只读句柄。
     */
    [[nodiscard]] ZzStitchedLineView lineAt(std::size_t index) const noexcept;
    /**
     * @brief 全部拼接行的最大有效全长（横向滚动 range 原料）。
     * @return 最大有效格数（空缓冲为 0）。
     */
    [[nodiscard]] int maxCellCount() const;
    /**
     * @brief 统一物理坐标 → 拼接（=逻辑）坐标。
     * @param pos 统一空间物理行坐标（物理行号 + 行内格偏移；复用
     *        ZzLogicalPos 类型承载，非折链合并逻辑坐标）。
     * @return 拼接行索引 + 拼接列；pos.line 越界时钳到首/末拼接行。
     */
    [[nodiscard]] ZzStitchedPos toStitched(ZzLogicalPos pos) const;
    /**
     * @brief 拼接（=逻辑）坐标 → 统一物理坐标。
     * @param line 拼接行索引（钳到 [0, lineCount-1]）。
     * @param col 拼接列（钳到 [0, cellCount-1]；空行钳到链头格 0）。
     * @return 统一空间物理行坐标（复用 ZzLogicalPos 类型承载；选区/搜索
     *         场景勿用本接口——拼接坐标本身即 ZzLogicalPos 逻辑坐标）。
     */
    [[nodiscard]] ZzLogicalPos fromStitched(std::int64_t line, int col) const;

private:
    friend class ZzStitchedLineView;
    class Impl;
    std::unique_ptr<Impl> impl_;

    // 供 ZzStitchedLineView 回调（detail，非公共契约）。
    [[nodiscard]] int stitchedCellCount(std::size_t index) const;
    [[nodiscard]] ZzCellView stitchedCellAt(std::size_t index, int col) const;
    [[nodiscard]] std::int64_t stitchedSourceLine(std::size_t index) const;
    [[nodiscard]] int stitchedSourceLineCount(std::size_t index) const;
};
