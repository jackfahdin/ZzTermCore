#pragma once

#include <string>

/// \brief ZzContourBackend 的事件抽象接口。库使用方实现该接口接收终端事件。
/// 所有回调在 feed/resize 调用线程内同步触发；实现方不得在回调内回读 backend
/// 快照以外的 Terminal 内部状态（部分回调在 Terminal 锁内触发）。
class ZzContourEvents
{
public:
    virtual ~ZzContourEvents() = default;
    /// \brief 窗口标题变化（OSC 0/2）。
    virtual void onTitleChanged(std::string title) = 0;
    /// \brief BEL 字符。
    virtual void onBell() = 0;
    /// \brief 屏幕内容脏（需要重绘）信号。
    virtual void onScreenDirty() = 0;
    /// \brief 主屏/备用屏切换；alternate 为 true 表示进入备用屏。
    virtual void onActiveBufferChanged(bool alternate) = 0;
    /// \brief 终端回传给传输层的字节（DA 响应、光标上报等）。
    virtual void onWriteToTransport(std::string bytes) = 0;
};
