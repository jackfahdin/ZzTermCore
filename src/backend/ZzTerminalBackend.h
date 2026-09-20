#pragma once

// 内部头（不安装、不进 include/ZzTerm/）：终端后端抽象边界。
// 设计见 docs/Architecture-v2.md（v2.1）§7：Public API 不暴露任何后端类型，
// ZzContourBackend（M1）与 ZzNativeBackend 实现本接口。
// 接口面即 ZzTerminal 当前公开语义面；M1 接入首个实现时可按需修订
//（例如 renderView 的返回类型随 ZzRenderView/ZzCellView 设计演化）。

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "ZzTerm/RenderView.h"
#include "ZzTerm/Screen.h"
#include "ZzTerm/Terminal.h" // ZzTermChanges
#include "ZzTerm/Types.h"

/// 终端后端抽象：feed/resize/渲染访问/状态查询/dirty 复位。
/// 线程安全与 ownership 约定同 ZzTerminal（非线程安全；视图借用后端）。
class ZzTerminalBackend {
public:
    virtual ~ZzTerminalBackend() = default;

    virtual ZzTermChanges feed(std::span<const std::byte> data) = 0;
    virtual bool resize(int cols, int rows) = 0;
    [[nodiscard]] virtual const ZzRenderView& renderView() const noexcept = 0;
    [[nodiscard]] virtual ZzSize size() const noexcept = 0;
    [[nodiscard]] virtual ZzCursorState cursor() const noexcept = 0;
    [[nodiscard]] virtual bool isAlternateScreen() const noexcept = 0;
    [[nodiscard]] virtual const std::string& title() const noexcept = 0;
    virtual void clearDirty() noexcept = 0;
    /// \brief 设置终端回传字节（DA 响应、光标上报等）的输出通道；native 暂为空实现。
    virtual void setOutputHandler(std::function<void(std::string_view)> handler) = 0;
    /// Ambiguous 宽度模式（true=CJK 按 2 列）；Contour 无对应配置，空操作。
    virtual void setAmbiguousWidthMode(bool wide) noexcept = 0;
};
