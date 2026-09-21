#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "ZzTerm/Export.h"
#include "ZzTerm/Input.h"
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
     * @param scrollbackMaxLines 历史容量上限（行），0 表示不保留历史；默认 10 万行（M4 benchmark 门控保障）。
     */
    ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines = 100000);

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
     * @brief 调整终端尺寸；列变化触发 soft-wrap reflow（M4 起）。
     * @param cols 新列数（> 0）。
     * @param rows 新行数（> 0）。
     * @return true 表示尺寸实际变化。
     * @note 列变化：屏幕区与 scrollback 历史一起重组（logical line 合并后
     *       按新列宽重切，宽字符不拆半，硬行截断/补空），光标跟随内容；
     *       行变化仅做网格增减，不触发 reflow。两后端语义对齐
     *       （Contour 经 allowReflowOnResize）。resize 后 RenderView 失效，
     *       前端需重新获取视图。
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
     * @brief 设置 output 通道（send 编码字节、终端回传均经此发出）。
     * @param handler 输出回调；未设置时字节静默丢弃。
     * @note 两后端均有效：native 的 DA1/DSR/CPR 回传与 send 编码字节
     *       统一经此通道发出（M3a 起）。
     */
    void setOutputHandler(std::function<void(std::string_view)> handler);

    /**
     * @brief 设置 Ambiguous 宽度模式（UAX #11 A 类别码位列宽）。
     * @param wide true 按 2 列（CJK 环境）；false 按 1 列（xterm 默认，构造初值）。
     * @note 仅 native 后端生效；Contour 后端无对应配置项，调用为空操作
     *       （适配层注释钉住的已知分歧）。设置对其后的 feed 生效，
     *       已落格内容不 retroactive 重排。
     */
    void setAmbiguousWidthMode(bool wide) noexcept;

    /**
     * @brief 发送普通文本输入（Unicode 输入、IME commit text）。
     * @param utf8 已确认的合法 UTF-8 文本（前端契约，Core 不重复校验）。
     * @note 编码字节经 setOutputHandler 的 output 通道发出；
     *       未设置 handler 时字节静默丢弃。
     */
    void sendText(std::string_view utf8);

    /**
     * @brief 发送按键事件（功能键、组合键；普通字符优先 sendText）。
     * @param event 键盘语义事件（见 ZzTerm/Input.h）。
     * @note 编码依据后端当前终端模式（application cursor/keypad 等），
     *       与 feed 接收的 DEC 模式序列联动；未设置 handler 时静默丢弃。
     */
    void sendKey(const ZzKeyEvent& event);

    /**
     * @brief 发送鼠标事件（网格坐标，0 起始）。
     * @param event 鼠标语义事件（见 ZzTerm/Input.h）。
     * @note 编码依当前鼠标上报模式（?9/?1000/?1002/?1003）与编码格式（?1006），
     *       由 feed 接收的 DEC 序列联动；未开模式或未设 handler 时静默丢弃。
     */
    void sendMouse(const ZzMouseEvent& event);

    /**
     * @brief 发送粘贴文本。
     * @param utf8 已确认的合法 UTF-8 文本。
     * @note bracketed paste（?2004）开启时自动包裹 200~/201~；未设 handler 静默丢弃。
     */
    void sendPaste(std::string_view utf8);

    /**
     * @brief 发送焦点事件。
     * @param focused true = 获得焦点，false = 失去焦点。
     * @note focus reporting（?1004）开启时发 CSI I/O；未开或未设 handler 静默丢弃。
     */
    void sendFocus(bool focused);

    // ---- 选区与复制（M5a） ----

    /**
     * @brief 设置选区（替换现有选区）。
     * @param anchor 锚点（选区固定端）。
     * @param extent 活动端。anchor 与 extent 无序要求，内部规范化。
     * @note 坐标越界 clamp 到有效范围；鼠标/触摸换算由前端负责。
     *       历史头部丢弃时 Core 自动平移锚点（物理行计数近似，语义见
     *       docs/Architecture.md 选区条款）；切换 Alternate 屏时选区清空。
     */
    void setSelection(ZzLogicalPos anchor, ZzLogicalPos extent);

    /**
     * @brief 拖动选区活动端（anchor 不变）。
     * @param extent 新活动端。无选区时等价于 setSelection(extent, extent)。
     */
    void extendSelection(ZzLogicalPos extent);

    /// @brief 清空选区。
    void clearSelection() noexcept;

    /// @brief 是否有非空选区（anchor != extent）。
    /// @return true 表示存在非空选区。
    [[nodiscard]] bool hasSelection() const noexcept;

    /**
     * @brief 查询规范化选区区间（半开区间 [start, end)）。
     * @param start 输出：区间起点。
     * @param end 输出：区间终点。
     * @return false = 空选区（start/end 不写入）。
     * @note 供前端绘制高亮使用；feed/resize 后坐标可能已被平移/clamp。
     */
    bool selectionRange(ZzLogicalPos& start, ZzLogicalPos& end) const;

    /**
     * @brief 提取选区纯文本。
     * @return UTF-8 文本；空选区返回空串。规则：宽字符整取、cluster 整串、
     *         软换行不插换行、跨逻辑行插单个换行、行尾空白修剪。
     */
    [[nodiscard]] std::string selectedText() const;

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
