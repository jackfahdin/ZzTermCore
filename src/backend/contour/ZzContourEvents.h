#pragma once

#include <string>

/// \brief ZzContourBackend 的事件抽象接口。库使用方实现该接口接收终端事件。
/// 所有回调在 feed/resize 调用线程内同步触发。锁契约：onTitleChanged/onBell/
/// onActiveBufferChanged 在 Terminal 解析锁内触发，回调内禁止调用 backend 的
/// 任何方法（含 snapshot——其内部 refreshRenderBuffer 会再抢同一把非递归锁，
/// 必然自死锁），只允许置标志后置处理；onScreenDirty/onWriteToTransport 在锁外触发。
class ZzContourEvents
{
public:
    virtual ~ZzContourEvents() = default;
    /// \brief 窗口标题变化（OSC 0/2）。锁内触发：回调内禁止调用 backend 任何方法，只置标志 defer。
    virtual void onTitleChanged(std::string title) = 0;
    /// \brief BEL 字符。锁内触发：回调内禁止调用 backend 任何方法，只置标志 defer。
    virtual void onBell() = 0;
    /// \brief 屏幕内容脏（需要重绘）信号。锁外触发。
    virtual void onScreenDirty() = 0;
    /// \brief 主屏/备用屏切换；alternate 为 true 表示进入备用屏。锁内触发：回调内禁止调用 backend 任何方法，只置标志 defer。
    virtual void onActiveBufferChanged(bool alternate) = 0;
    /// \brief 终端回传给传输层的字节（DA 响应、光标上报等）。锁外触发。
    virtual void onWriteToTransport(std::string bytes) = 0;
};
