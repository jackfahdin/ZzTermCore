#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "ZzTerm/Export.h"
#include "ZzTerm/RenderView.h"
#include "ZzTerm/Screen.h"
#include "ZzTerm/Scrollback.h"
#include "ZzTerm/Types.h"

/**
 * @file Terminal.h
 * @brief ZzTerminal：终端模拟器顶层外观（Facade）。
 *
 * 职责（docs/Architecture-v2.md §7）：
 * - 对前端暴露唯一的后端无关入口；具体终端引擎经 ZzBackendKind
 *   在构造时显式选择（Native 自研引擎 / Contour vtbackend），
 *   运行期由 PImpl 持有的后端接口分派，公开 API 不暴露任何后端类型。
 * - ZzTerminal 只做委托：parser/screen/scrollback 等引擎组件归各后端
 *   实现持有（native 在 src/backend/native，contour 经 vtbackend），
 *   feed/resize/renderView 等调用转发给所选后端并聚合 ZzTermChanges。
 * - 远端输入一律视为不可信（Architecture.md 第 15 节），feed 不抛异常、
 *   不因畸形输入产生未定义行为。
 *
 * ownership：ZzTerminal 独占拥有所选后端实例；
 * renderView() 返回的视图借用 Terminal，不得比 Terminal 长寿。
 *
 * 线程安全：非线程安全。feed/resize/renderView 必须在同一线程调用
 * （通常为前端 UI 线程或专用终端线程，由集成方决定并保证互斥）。
 */

/**
 * @brief 一次 feed/resize 产生的终端状态变化摘要。
 *
 * 前端据此决定重绘范围、滚动条更新、标题栏刷新、响铃等。
 */
struct ZzTermChanges {
    bool        screenDirty        = false; ///< 屏幕内容变化（含光标移动）。
    bool        scrollbackChanged  = false; ///< 历史行数变化。
    bool        activeBufferChanged = false; ///< Primary/Alternate 发生切换。
    bool        titleChanged       = false; ///< 窗口/图标标题变化（OSC 0/1/2）。
    bool        bell               = false; ///< BEL 触发（前端决定响铃/闪烁）。

    /**
     * @brief 本次滚入历史的行数。
     * @note 后端语义差（M1b 现状，钉住待后续统一）：native 统计实际滚出行数，
     *       与 scrollback 容量无关，饱和后仍如实上报；Contour 以历史行数差值
     *       近似，scrollback 达容量上限后差值恒 0，scrollbackChanged 与本字段
     *       停止上报（历史内容仍在滚动，只是不再计数）。前端不得依赖本字段
     *       推断「不再有新行滚出」。
     */
    std::size_t scrolledOutLines   = 0;

    /**
     * @brief 合并另一份变化（连续多次 feed 聚合用）。
     * @param other 待合并的变化摘要；布尔字段按或聚合，scrolledOutLines 累加。
     */
    void merge(const ZzTermChanges& other) noexcept
    {
        screenDirty         = screenDirty || other.screenDirty;
        scrollbackChanged   = scrollbackChanged || other.scrollbackChanged;
        activeBufferChanged = activeBufferChanged || other.activeBufferChanged;
        titleChanged        = titleChanged || other.titleChanged;
        bell                = bell || other.bell;
        scrolledOutLines   += other.scrolledOutLines;
    }
};

/// \brief 终端引擎后端选择（运行期）。
enum class ZzBackendKind {
    Native,  ///< 自研引擎（一等后端，兼容性对照基准）
    Contour  ///< Contour vtbackend（默认方向；需 ZZTERM_WITH_CONTOUR=ON 构建）
};

/**
 * @brief 终端模拟器顶层对象。
 */
class ZZTERM_API ZzTerminal {
public:
    /**
     * @brief 构造终端。
     * @param cols 列数（> 0）。
     * @param rows 行数（> 0）。
     * @param backend 后端选择（显式，无默认值）。OFF 构建传 Contour 抛 std::logic_error。
     * @param scrollbackMaxLines 历史容量上限（行），0 表示不保留历史。
     */
    ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines = 10000);

    ~ZzTerminal();

    ZzTerminal(const ZzTerminal&)            = delete;
    ZzTerminal& operator=(const ZzTerminal&) = delete;

    /**
     * @brief 向终端模拟器输入原始字节流。
     *
     * 输入可在 UTF-8 字符或 VT 控制序列中间截断；
     * 解析器保存未完成状态，并在后续 feed() 中继续处理。
     * 数据被视作不可信远端输入：畸形/超长序列被安全丢弃并恢复，
     * 不会抛出异常或导致未定义行为。
     *
     * @param data 输入的原始字节流。
     * @return 本次输入产生的终端状态变化摘要。
     */
    ZzTermChanges feed(std::span<const std::byte> data);

    /**
     * @brief 调整终端尺寸。
     * @param cols 新列数（> 0；非法值忽略并返回 false）。
     * @param rows 新行数（> 0；非法值忽略并返回 false）。
     * @return 尺寸是否发生变化。
     * @note 当前为网格级 resize；列变化时的 soft-wrap reflow 与
     *       cursor/selection 映射在 M4 里程碑实现，届时本接口语义
     *       不变、行为增强。
     */
    bool resize(int cols, int rows);

    /**
     * @brief 获取只读渲染视图（Renderer 的唯一入口）。
     * @return 渲染视图常量引用；借用 Terminal，不得比 Terminal 长寿，
     *         帧内使用、跨帧重新读取其查询结果。
     */
    [[nodiscard]] const ZzRenderView& renderView() const noexcept;

    /**
     * @brief 当前网格尺寸。
     * @return 当前工作区尺寸。
     */
    [[nodiscard]] ZzSize size() const noexcept;

    /**
     * @brief 当前光标状态。
     * @return 光标完整状态（位置/形状/可见性/闪烁）。
     */
    [[nodiscard]] ZzCursorState cursor() const noexcept;

    /**
     * @brief 当前是否为 Alternate Screen。
     * @return true 表示活动缓冲区为 Alternate。
     */
    [[nodiscard]] bool isAlternateScreen() const noexcept;

    /**
     * @brief 窗口/图标标题（OSC 0/1/2 设置，UTF-8）。
     * @return 标题字符串常量引用。
     */
    [[nodiscard]] const std::string& title() const noexcept;

    /**
     * @brief 一帧渲染完成后复位 Dirty 状态。
     * @note Renderer 不可直接调用（它只见 RenderView）；由前端在
     *       完成绘制后经由本方法复位。代际号继续递增。
     */
    void clearDirty() noexcept;

    /**
     * @brief 设置终端回传字节的输出通道（DA 响应、光标上报等）。
     * @param handler 输出回调；Contour 后端有效，native 暂不回传。
     */
    void setOutputHandler(std::function<void(std::string_view)> handler);

    // ---- Core 内部访问（供 parser/terminal 模块协作，不属于 Renderer API） ----

    /**
     * @brief 可变访问工作区（Core 内部使用；仅 Native 后端可用）。
     * @return 工作区可变引用。
     * @note 仅 Native 后端可用，Contour 后端调用抛 std::logic_error。
     */
    [[nodiscard]] ZzScreen& screen();

    /**
     * @brief 可变访问历史后端（Core 内部使用；仅 Native 后端可用）。
     * @return 历史后端可变引用。
     * @note 仅 Native 后端可用，Contour 后端调用抛 std::logic_error。
     */
    [[nodiscard]] ZzScrollback& scrollback();

private:
    // PImpl：实现细节定义在 src/terminal/Terminal.cpp（持有后端接口指针，
    // 运行期按 ZzBackendKind 分派）；公开 API 不暴露任何后端类型
    //（docs/Architecture-v2.md §7）。Backend 抽象见 src/backend/ZzTerminalBackend.h。
    class Impl;
    std::unique_ptr<Impl> impl_;
};
