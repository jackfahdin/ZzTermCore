#include "ZzContourBackendAdapter.h"

#include "ZzContourBackend.h"
#include "ZzContourEvents.h"
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
        , renderView_(*backend_, state_)
    {}

    ZzTermChanges feed(std::span<const std::byte> data) override
    {
        const int historyBefore = backend_->historyLineCount();
        screenDirty_ = activeBufferChanged_ = titleChanged_ = bell_ = false;
        backend_->feed(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        backend_->flushReplies(); // 回传字节经 onWriteToTransport（锁外）进 output handler
        const int historyAfter = backend_->historyLineCount();

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
        if (backend_->size() == std::make_pair(cols, rows))
            return false;
        backend_->resize(cols, rows);
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
