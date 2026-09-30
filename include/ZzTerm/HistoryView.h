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
/// 固有边界（分域 reflow）：历史与屏幕分域重组，跨域逻辑行在接缝处拆成
/// 两条链——内容零丢失，但折行位置可能与 contour 后端不同（与 ZzScrollback
/// 接口注释同一免责声明）。

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
     * @brief 第 index 行只读句柄。
     * @param index 行号（0 = 最旧历史行，须小于 lineCount()）。
     * @return 该行只读访问句柄。
     * @note 句柄借用后端行（contour 为视图内部单行缓冲）：feed/resize/clear
     *       或下一次 lineAt 调用后失效，不得长期持有。
     * @note 行宽不变量：cellCount() 恒等于终端当前列宽（resize 经 reflow 维持）。
     */
    [[nodiscard]] virtual ZzLineView lineAt(std::size_t index) const = 0;

    /**
     * @brief 历史头部累计裁掉的行数。
     * @return 单调不减的裁剪计数。
     * @note contour 后端的计数以其下层 Grid 能力为限（列变 reflow 的
     *       行身份重建不计入，与内部 LineSource 同一口径）。
     */
    [[nodiscard]] virtual std::uint64_t droppedLineCount() const noexcept = 0;

    /**
     * @brief 历史变化代计数。
     * @return 单调递增的代计数。
     * @note append/裁剪/reflow/clear/Alternate 切换时递增；允许保守多增，
     *       不得漏增。前端据此刷新滚动条上限，禁止每帧全扫历史。
     */
    [[nodiscard]] virtual std::uint64_t generation() const noexcept = 0;
};
