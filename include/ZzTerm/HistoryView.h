#pragma once

/// \file
/// \brief 后端无关的历史行只读视图契约（M14）。
/// 与 ZzRenderView 平行的第二只读边界：RenderView 覆盖屏幕区，本契约覆盖
/// scrollback 历史区。视图为借用式，须与 feed 同线程使用；feed/resize/clear
/// 后既有视图产出的 ZzLineView 句柄全部失效。非线程安全。
///
/// 性能红线：禁止每帧扫描全部历史；变化侦测用 generation()，滚动查看按需
/// lineAt 取可见行（native O(1)，contour 为单行快照拷贝）。
///
/// 接缝处理（M16b）：历史与屏幕分域重组，但横跨两域的逻辑行在列变
/// resize 时经 backend 归还机制统一重组，不再拆链；历史末行 wrapped=true
/// 即与屏幕首行续接为一条逻辑行（与 ZzLineSource 接缝规则一致）。

#include <ZzTerm/RenderView.h> // ZzLineView

#include <cstddef>
#include <cstdint>

/**
 * @brief 历史行只读视图接口（双后端统一）。
 *
 * 坐标模型：index 属于 [0, lineCount())，0 = 最旧历史行；
 * 绝对行号 = droppedLineCount() + index（供选区锚点平移与应用侧
 * 归档行号对齐）。Alternate 屏 lineCount() 恒 0（Alternate 无历史），
 * 历史数据本身保留，切回 Primary 后恢复可见。
 */
class ZZTERM_API ZzHistoryView {
public:
    virtual ~ZzHistoryView() = default;

    /**
     * @brief 当前可读历史行数。
     * @return 历史行数；Alternate 屏恒 0。
     */
    [[nodiscard]] virtual std::size_t lineCount() const noexcept = 0;

    /**
     * @brief Primary 历史真实行数（buffer 无关口径；应用侧读回对账用）。
     * @return Primary 历史行数；Alternate 屏活动期间仍返回真实行数
     *       （区别于 lineCount() 的契约性 0）。
     * @note Alternate 期间列变 reflow 照样重组 Primary 历史，对账锚点
     *       （如读回桥的追加游标复位）须用本口径而非可见口径。
     * @note 默认实现回退 lineCount()（后端无 buffer 无关口径时的保守
     *       降级）；native 后端覆写为 scrollback 真实行数。
     */
    [[nodiscard]] virtual std::size_t primaryLineCount() const noexcept
    {
        return lineCount();
    }

    /**
     * @brief 第 index 行只读句柄。
     * @param index 行号（0 = 最旧历史行，须小于 lineCount()）。
     * @return 该行只读访问句柄。
     * @note 句柄借用后端行（contour 为视图内部单行缓冲）：feed/resize/clear
     *       或下一次 lineAt 调用后失效，不得长期持有。
     * @note 行宽不变量：cellCount() 恒等于终端当前列宽（resize 经 reflow 维持）。
     */
    [[nodiscard]] virtual ZzLineView lineAt(std::size_t index) const = 0;

    /**
     * @brief 绝对行号基线（历史最旧端之前经过的行数）。
     * @return 基线 = 累计裁掉 - 累计注入归还（M19 起，E-2 勘误口径）。
     * @note 注入归还（prependHistoryLines）使基线回退——本计数不再
     *       单调不减（旧「单调不减的裁剪计数」口径作废）；容量裁剪单调
     *       累加方向不变。既有行绝对行号（基线+index）跨注入稳定。
     * @note contour 后端的计数以其下层 Grid 能力为限（列变 reflow 的
     *       行身份重建不计入，与内部 LineSource 同一口径）；contour
     *       不支持注入，基线不退。
     */
    [[nodiscard]] virtual std::uint64_t droppedLineCount() const noexcept = 0;

    /**
     * @brief 历史变化代计数。
     * @return 单调递增的代计数。
     * @note append/裁剪/reflow/clear/Alternate 切换时递增；允许保守多增，
     *       不得漏增。前端据此刷新滚动条上限，禁止每帧全扫历史。
     */
    [[nodiscard]] virtual std::uint64_t generation() const noexcept = 0;

    /**
     * @brief 历史清空累计计数（ED 3 等；应用侧对账精确侦测用）。
     * @return 单调递增的清空次数（命令执行口径，空历史再清亦计数）。
     * @note 默认实现回退 0（无清空语义/未实现后端的保守降级，调用方退回收缩
     *       向兜底）；native 覆写为真实计数。Alternate 期间 ED3 清 Primary
     *       历史同样计入（buffer 无关）。
     */
    [[nodiscard]] virtual std::uint64_t clearCount() const noexcept { return 0; }
};
