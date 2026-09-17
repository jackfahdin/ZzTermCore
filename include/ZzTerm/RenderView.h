#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ZzTerm/Cell.h"
#include "ZzTerm/Export.h"
#include "ZzTerm/Line.h"
#include "ZzTerm/Screen.h"
#include "ZzTerm/Types.h"

class ZzScrollback; // 前置声明，RenderView 只持有只读指针。

/**
 * @file RenderView.h
 * @brief ZzRenderView：Core 与 Renderer 之间的稳定只读渲染边界。
 *
 * 约束（Architecture.md 第 10 节）：
 * - Renderer（Qt/QPainter 或其他）只能通过本类读取渲染所需信息，
 *   不得接触 Core 私有容器（ZzScreen 的行存储、ZzScrollback 的块结构等）；
 * - 本类不提供任何修改入口；Dirty 状态的复位由 ZzTerminal::clearDirty()
 *   在一帧渲染完成后执行；
 * - 行寻址支持“回看”：row 0 起为当前可见屏幕，scrollbackOffset() > 0
 *   表示视图整体上移回看历史行（滚动条拖拽），此时光标可能不可见。
 *
 * lifetime：ZzRenderView 借用 ZzTerminal 内部对象，Terminal 销毁或
 * feed/resize 修改网格后，此前取得的 lineAt/cellAt 引用即失效；
 * 渲染帧内使用、帧间重新获取。非线程安全，必须与 Terminal 同线程
 * （或由前端在持有帧快照语义下调用）。
 */

/**
 * @brief Core 与 Renderer 之间的稳定只读渲染边界视图。
 *
 * 聚合 ZzScreen（工作区）与 ZzScrollback（历史）的只读查询入口，
 * 并暴露 Dirty Tracking 状态供增量重绘。设计约束、行寻址约定与
 * lifetime 规则见文件头注释；本类不提供任何修改入口。
 */
class ZZTERM_API ZzRenderView {
public:
    /**
     * @brief 构造渲染视图（Core 内部使用，由 ZzTerminal 装配）。
     * @param screen 工作区（不可为空，借用，不拥有）。
     * @param scrollback 历史后端（可为空表示无历史，借用，不拥有）。
     */
    ZzRenderView(const ZzScreen* screen, const ZzScrollback* scrollback) noexcept;

    /**
     * @brief 可见网格尺寸。
     * @return 当前可见屏幕尺寸。
     */
    [[nodiscard]] ZzSize size() const noexcept;

    /**
     * @brief 当前是否为 Alternate Screen。
     * @return true 表示活动缓冲区为 Alternate。
     */
    [[nodiscard]] bool isAlternateScreen() const noexcept;

    /**
     * @brief 当前回看偏移（行）。
     * @return 0 表示贴着屏幕底部（实时输出位置）；> 0 表示向上回看
     *         scrollback 的偏移量。
     * @note M0 恒为 0；滚动条回看由前端/Terminal 协作在后续里程碑实现，
     *       接口先行稳定。
     */
    [[nodiscard]] int scrollbackOffset() const noexcept;

    /**
     * @brief 历史总行数（滚动条范围计算用；无历史后端时为 0）。
     * @return 历史行数。
     */
    [[nodiscard]] std::size_t scrollbackLineCount() const noexcept;

    /**
     * @brief 只读访问可见行。
     * @param row 可见行号，0 <= row < size().rows（已计入回看偏移映射）。
     * @return 行的常量引用。
     * @warning 返回引用在下一次 feed/resize 后可能失效，帧内有效。
     */
    [[nodiscard]] const ZzLine& lineAt(int row) const noexcept;

    /**
     * @brief 只读访问单元格（lineAt(row).cellAt(col) 的便捷形式）。
     * @param row 可见行号。
     * @param col 列号。
     * @return 单元格常量引用。
     */
    [[nodiscard]] const ZzCell& cellAt(int row, int col) const noexcept;

    /**
     * @brief 光标状态（含形状/可见性）。
     * @return 当前光标状态。
     * @note 回看偏移非 0 时调用方应自行决定是否隐藏光标。
     */
    [[nodiscard]] ZzCursorState cursor() const noexcept;

    // ---- Dirty 查询（复位由 ZzTerminal::clearDirty 负责） ----

    /**
     * @brief Dirty 代际号；与上一帧比较即可判断是否有任何变化。
     * @return 代际号（单调递增，不复位）。
     */
    [[nodiscard]] std::uint64_t dirtyGeneration() const noexcept;

    /**
     * @brief 指定可见行是否标脏。
     * @param row 可见行号。
     * @return true 表示该行标脏。
     */
    [[nodiscard]] bool rowDirty(int row) const noexcept;

    /**
     * @brief 指定可见行的脏单元格合并范围（半开区间）。
     * @param row 可见行号。
     * @return 脏列合并范围；行未脏时为空范围。
     */
    [[nodiscard]] ZzCellRange dirtyRange(int row) const noexcept;

    /**
     * @brief 收集当前所有脏行行号（升序）。
     * @return 脏行行号列表。
     */
    [[nodiscard]] std::vector<int> dirtyRows() const;

private:
    const ZzScreen*     screen_     = nullptr; ///< 借用，不拥有。
    const ZzScrollback* scrollback_ = nullptr; ///< 借用，不拥有，可为空。
};
