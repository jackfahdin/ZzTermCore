#include "ZzContourBackendAdapter.h"

#include "ZzContourBackend.h"
#include "ZzContourEvents.h"
#include "ZzContourLineSource.h"
#include "ZzContourHistoryView.h"
#include "ZzContourRenderView.h"

#include <utility>

namespace {

/// ZzTerminalBackend 的 Contour 实现：feed 聚合 ZzTermChanges、共享 dirty 状态给视图、
/// 终端回传字节经 flushReplies → onWriteToTransport（锁外）进 output 通道。
class ZzContourBackendAdapter final : public ZzTerminalBackend {
public:
    ZzContourBackendAdapter(int cols, int rows, std::size_t scrollbackLines)
        : events_(std::make_unique<EventsImpl>(*this))
        , backend_(std::make_unique<ZzContourBackend>(cols, rows, *events_,
                                                      static_cast<int>(scrollbackLines)))
        // lineSource_ 借用 backend_：声明顺序须在 backend_ 之后（成员按声明序构造）。
        , lineSource_(*backend_)
        // historyView_ 借用 backend_/lineSource_/historyGeneration_：声明顺序须在它们之后。
        , historyView_(*backend_, lineSource_, historyGeneration_)
        , renderView_(*backend_, state_)
    {}

    ZzTermChanges feed(std::span<const std::byte> data) override
    {
        const int historyBefore = backend_->historyLineCount();
        screenDirty_ = activeBufferChanged_ = titleChanged_ = bell_ = false;
        backend_->feed(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        backend_->flushReplies(); // 回传字节经 onWriteToTransport（锁外）进 output handler
        const int historyAfter = backend_->historyLineCount();
        lineSource_.noteFloor(); // 累计 stableFloor 真实前移（容量裁剪）
        // M14：历史代计数——行数变化（append）、真实裁剪（floor 前移）、
        // Alternate 切换（可见历史归零/恢复）任一发生即递增；允许保守多增。
        if (historyAfter != historyBefore || lineSource_.droppedLineCount() != lastDropped_
            || activeBufferChanged_)
            ++historyGeneration_;
        lastDropped_ = lineSource_.droppedLineCount();

        ZzTermChanges changes;
        changes.screenDirty = screenDirty_;
        changes.activeBufferChanged = activeBufferChanged_;
        changes.titleChanged = titleChanged_;
        changes.bell = bell_;
        // scrolledOutLines 以 historyLineCount() 差值近似：scrollback 达容量上限后
        // 差值恒 0，饱和期间滚出的行不再计入（与 native 按实际滚出行计数、饱和后
        // 仍上报的语义差见 ZzTermChanges::scrolledOutLines 注释；contour Grid 无
        // 公开的单调滚出计数可取，stableBase 私有且 SD/unscroll 会回退，故钉住现状）。
        if (historyAfter > historyBefore) {
            changes.scrollbackChanged = true;
            changes.scrolledOutLines = static_cast<std::size_t>(historyAfter - historyBefore);
        }
        if (changes.screenDirty) {
            state_.dirtySinceClear = true;
            ++state_.dirtyGeneration;
        }
        return changes;
    }

    bool resize(int cols, int rows) override
    {
        if (cols <= 0 || rows <= 0)
            return false;
        const auto [oldCols, oldRows] = backend_->size();
        if (oldCols == cols && oldRows == rows)
            return false;
        backend_->resize(cols, rows);
        // 双入口划分（ZzContourLineSource.cpp 文件头结论①）：列变化触发 reflow，
        // floor 前移是行身份重建副产而非真实丢弃——reanchorFloor 直接对齐不累计；
        // 纯行数变化 floor 仅在真实裁剪时前移——noteFloor 累计。
        const int historyBefore = backend_->historyLineCount();
        if (cols != oldCols)
            lineSource_.reanchorFloor();
        else
            lineSource_.noteFloor();
        // M14：列变 reflow 历史内容必变（保守递增）；行变仅在历史行数或
        // 裁剪计数实际变化时递增。
        if (cols != oldCols || backend_->historyLineCount() != historyBefore
            || lineSource_.droppedLineCount() != lastDropped_)
            ++historyGeneration_;
        lastDropped_ = lineSource_.droppedLineCount();
        state_.dirtySinceClear = true;
        ++state_.dirtyGeneration;
        return true;
    }

    const ZzRenderView& renderView() const noexcept override { return renderView_; }
    ZzSize size() const noexcept override
    {
        const auto [cols, rows] = backend_->size();
        return ZzSize { cols, rows };
    }
    ZzCursorState cursor() const noexcept override { return renderView_.cursor(); }
    bool isAlternateScreen() const noexcept override { return backend_->isAlternateScreen(); }
    const std::string& title() const noexcept override { return title_; }
    void clearDirty() noexcept override { state_.dirtySinceClear = false; }
    void setOutputHandler(std::function<void(std::string_view)> handler) override
    {
        outputHandler_ = std::move(handler);
    }
    void setAmbiguousWidthMode(bool /*wide*/) noexcept override
    {
        // Contour 未暴露 ambiguous 宽度配置：空操作（已知分歧，规格 4.1 钉住；
        // compat 测试不含 Ambiguous 维度对照）。
    }
    void sendText(std::string_view utf8) override
    {
        backend_->sendText(utf8);
        backend_->flushReplies();
    }
    void sendKey(const ZzKeyEvent& event) override
    {
        backend_->sendKeyEvent(event);
        backend_->flushReplies(); // send 同步生成的编码字节立即上行
    }
    void sendMouse(const ZzMouseEvent& event) override
    {
        backend_->sendMouseEvent(event);
        backend_->flushReplies();
    }
    void sendPaste(std::string_view utf8) override
    {
        backend_->sendPasteText(utf8);
        backend_->flushReplies();
    }
    void sendFocus(bool focused) override
    {
        backend_->sendFocusEvent(focused);
        backend_->flushReplies();
    }
    [[nodiscard]] const ZzIPhysicalLineSource& lineSource() const noexcept override { return lineSource_; }
    [[nodiscard]] const ZzHistoryView& historyView() const noexcept override { return historyView_; }

private:
    // ZzContourEvents 实现：锁内回调（title/bell/altBuffer）只写 adapter 自有状态
    //（不触碰 backend，遵守锁契约）；锁外回调（dirty/output）直接转发。
    class EventsImpl final : public ZzContourEvents {
    public:
        explicit EventsImpl(ZzContourBackendAdapter& owner) : owner_(owner) {}
        void onTitleChanged(std::string title) override
        {
            owner_.title_ = std::move(title);
            owner_.titleChanged_ = true;
        }
        void onBell() override { owner_.bell_ = true; }
        void onScreenDirty() override { owner_.screenDirty_ = true; }
        void onActiveBufferChanged(bool) override { owner_.activeBufferChanged_ = true; }
        void onWriteToTransport(std::string bytes) override
        {
            if (owner_.outputHandler_)
                owner_.outputHandler_(bytes);
        }

    private:
        ZzContourBackendAdapter& owner_;
    };

    std::unique_ptr<EventsImpl>       events_;
    std::unique_ptr<ZzContourBackend> backend_;
    ZzContourLineSource               lineSource_; // 借用 backend_，须声明在其后
    std::uint64_t                     historyGeneration_ = 0; // M14 历史代计数（historyView_ 借用，须声明在其前）
    std::uint64_t                     lastDropped_ = 0;       // M14 裁剪计数快照（代计数递增判定用）
    ZzContourHistoryView              historyView_;  // 借用 backend_/lineSource_/historyGeneration_，须声明在它们之后
    ZzContourRenderView::State        state_;
    ZzContourRenderView               renderView_;
    std::string                       title_;
    bool screenDirty_ = false, activeBufferChanged_ = false, titleChanged_ = false, bell_ = false;
    std::function<void(std::string_view)> outputHandler_;
};

} // namespace

std::unique_ptr<ZzTerminalBackend> zzCreateContourBackendAdapter(int cols, int rows,
                                                                 std::size_t scrollbackLines)
{
    return std::make_unique<ZzContourBackendAdapter>(cols, rows, scrollbackLines);
}
