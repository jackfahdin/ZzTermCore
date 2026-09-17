#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include "ZzTerm/Export.h"
#include "ZzTerm/RenderView.h"
#include "ZzTerm/Screen.h"
#include "ZzTerm/Scrollback.h"
#include "ZzTerm/Types.h"

class ZzVtParser; // 前置声明：解析器由 parser 模块实现。
                  // 注意：在 ZzVtParser 完整定义可见前，ZzTerminal 暂不持有
                  // unique_ptr<ZzVtParser> 成员（不完整类型会导致构造函数
                  // 异常清理路径无法实例化）。parser 模块接入时添加成员，
                  // 并保证 Terminal 的构造/析构定义处包含其头文件。

/**
 * @file Terminal.h
 * @brief ZzTerminal：终端模拟器顶层外观（Facade）。
 *
 * 职责（Architecture.md 第 2/7 节）：
 * - 持有并协调 ZzVtParser（语法 dispatch）、ZzScreen（工作区）、
 *   ZzScrollback（历史）、ZzRenderView（渲染边界）；
 * - Parser 只负责语法，语义（模式解释、画笔状态、历史入栈、
 *   Alternate Screen 切换语义等）集中在 Terminal；
 * - 远端输入一律视为不可信（Architecture.md 第 15 节），feed 不抛异常、
 *   不因畸形输入产生未定义行为。
 *
 * ownership：ZzTerminal 独占拥有 parser/screen/scrollback；
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
    std::size_t scrolledOutLines   = 0;     ///< 本次滚入历史的行数。

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

/**
 * @brief 终端模拟器顶层对象。
 */
class ZZTERM_API ZzTerminal {
public:
    /**
     * @brief 构造终端。
     * @param cols 列数（> 0）。
     * @param rows 行数（> 0）。
     * @param scrollbackMaxLines 历史容量上限（行），0 表示不保留历史。
     */
    ZzTerminal(int cols, int rows, std::size_t scrollbackMaxLines = 10000);

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
     * @note M0 占位实现：Parser 模块接入前，仅处理可打印 ASCII 与
     *       CR/LF/BS/HT，其余字节安全忽略。接入 ZzVtParser 后此注释
     *       需更新为完整语义。
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

    // ---- Core 内部访问（供 parser/terminal 模块协作，不属于 Renderer API） ----

    /**
     * @brief 可变访问工作区（Core 内部使用）。
     * @return 工作区可变引用。
     */
    [[nodiscard]] ZzScreen& screen() noexcept;

    /**
     * @brief 可变访问历史后端（Core 内部使用）。
     * @return 历史后端可变引用。
     */
    [[nodiscard]] ZzScrollback& scrollback() noexcept;

private:
    ZzScreen                     screen_;     ///< 工作区（内含 Primary/Alternate）。
    std::unique_ptr<ZzScrollback> scrollback_; ///< 历史后端（接口指针，实现可替换）。
    ZzRenderView                 renderView_; ///< 渲染边界（借用上两者）。
    std::string                  title_;      ///< OSC 标题（UTF-8）。
    std::size_t                  scrolledOutPending_ = 0; ///< feed 内滚出行计数（回调聚合用）。
};
