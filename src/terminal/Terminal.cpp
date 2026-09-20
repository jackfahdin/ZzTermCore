#include <ZzTerm/Terminal.h>

#include "../backend/ZzTerminalBackend.h"
#include "../backend/native/ZzNativeBackend.h"
#ifdef ZZTERM_WITH_CONTOUR
#include "../backend/contour/ZzContourBackendAdapter.h"
#endif

#include <stdexcept>
#include <utility>

class ZzTerminal::Impl {
public:
    Impl(int cols, int rows, ZzBackendKind kind, std::size_t scrollbackMaxLines)
    {
        switch (kind) {
        case ZzBackendKind::Native:
            backend = std::make_unique<ZzNativeBackend>(cols, rows, scrollbackMaxLines);
            break;
        case ZzBackendKind::Contour:
#ifdef ZZTERM_WITH_CONTOUR
            backend = zzCreateContourBackendAdapter(cols, rows, scrollbackMaxLines);
#else
            throw std::logic_error("ZzBackendKind::Contour 需要 ZZTERM_WITH_CONTOUR=ON 构建");
#endif
            break;
        }
    }
    std::unique_ptr<ZzTerminalBackend> backend;
};

ZzTerminal::ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines)
    : impl_(std::make_unique<Impl>(cols, rows, backend, scrollbackMaxLines)) {}
ZzTerminal::~ZzTerminal() = default;

ZzTermChanges ZzTerminal::feed(std::span<const std::byte> data) { return impl_->backend->feed(data); }
bool ZzTerminal::resize(int cols, int rows) { return impl_->backend->resize(cols, rows); }
const ZzRenderView& ZzTerminal::renderView() const noexcept { return impl_->backend->renderView(); }
ZzSize ZzTerminal::size() const noexcept { return impl_->backend->size(); }
ZzCursorState ZzTerminal::cursor() const noexcept { return impl_->backend->cursor(); }
bool ZzTerminal::isAlternateScreen() const noexcept { return impl_->backend->isAlternateScreen(); }
const std::string& ZzTerminal::title() const noexcept { return impl_->backend->title(); }
void ZzTerminal::clearDirty() noexcept { impl_->backend->clearDirty(); }
void ZzTerminal::setOutputHandler(std::function<void(std::string_view)> handler)
{
    impl_->backend->setOutputHandler(std::move(handler));
}

void ZzTerminal::setAmbiguousWidthMode(bool wide) noexcept
{
    impl_->backend->setAmbiguousWidthMode(wide);
}

void ZzTerminal::sendText(std::string_view utf8)
{
    impl_->backend->sendText(utf8);
}

void ZzTerminal::sendKey(const ZzKeyEvent& event)
{
    impl_->backend->sendKey(event);
}

void ZzTerminal::sendMouse(const ZzMouseEvent& event)
{
    impl_->backend->sendMouse(event);
}

void ZzTerminal::sendPaste(std::string_view utf8)
{
    impl_->backend->sendPaste(utf8);
}

void ZzTerminal::sendFocus(bool focused)
{
    impl_->backend->sendFocus(focused);
}

ZzScreen& ZzTerminal::screen()
{
    auto* native = dynamic_cast<ZzNativeBackend*>(impl_->backend.get());
    if (!native)
        throw std::logic_error("ZzTerminal::screen() 仅 Native 后端可用");
    return native->screen();
}

ZzScrollback& ZzTerminal::scrollback()
{
    auto* native = dynamic_cast<ZzNativeBackend*>(impl_->backend.get());
    if (!native)
        throw std::logic_error("ZzTerminal::scrollback() 仅 Native 后端可用");
    return native->scrollback();
}
