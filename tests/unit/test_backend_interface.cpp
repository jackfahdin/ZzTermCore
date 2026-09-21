// ZzTerminalBackend 接口编译契约：证明接口可实例化实现且签名与
// ZzTerminal 公开语义面一致。不含任何真实终端行为（M1 才有实现）。
#include "backend/ZzTerminalBackend.h"

#include <cassert>
#include <stdexcept>

namespace {

class FakeRenderView final : public ZzRenderView {
public:
    ZzSize size() const noexcept override { return {}; }
    bool isAlternateScreen() const noexcept override { return false; }
    ZzLineView lineAt(int) const override
    {
        return ZzLineView(0, [](const void*, int) { return ZzCellView {}; },
                          [](const void*) noexcept { return 0; },
                          [](const void*) noexcept { return false; });
    }
    ZzCursorState cursor() const override { return {}; }
    std::uint64_t dirtyGeneration() const noexcept override { return 0; }
    bool rowDirty(int) const noexcept override { return false; }
    ZzCellRange dirtyRange(int) const noexcept override { return {}; }
};

class FakeLineSource final : public ZzIPhysicalLineSource {
public:
    std::size_t historyLineCount() const override { return 0; }
    int screenRowCount() const override { return 0; }
    int cols() const override { return 0; }
    void lineAt(std::size_t, ZzLine& out) const override { out = ZzLine{}; }
    bool lineWrapped(std::size_t) const override { return false; }
    std::uint64_t droppedLineCount() const override { return 0; }
};

class FakeBackend : public ZzTerminalBackend {
public:
    ZzTermChanges feed(std::span<const std::byte> data) override
    {
        fedBytes += data.size();
        return {};
    }
    bool resize(int cols, int rows) override
    {
        lastSize = ZzSize{cols, rows};
        return true;
    }
    const ZzRenderView& renderView() const noexcept override { return view_; }
    ZzSize size() const noexcept override { return lastSize; }
    ZzCursorState cursor() const noexcept override { return {}; }
    bool isAlternateScreen() const noexcept override { return false; }
    const std::string& title() const noexcept override { return title_; }
    void clearDirty() noexcept override { ++clearCount; }
    void setOutputHandler(std::function<void(std::string_view)> handler) override
    {
        outputHandlerSet = static_cast<bool>(handler);
    }
    void setAmbiguousWidthMode(bool) noexcept override {}
    void sendText(std::string_view /*utf8*/) override {}
    void sendKey(const ZzKeyEvent& /*event*/) override {}
    void sendMouse(const ZzMouseEvent& /*event*/) override {}
    void sendPaste(std::string_view /*utf8*/) override {}
    void sendFocus(bool /*focused*/) override {}
    const ZzIPhysicalLineSource& lineSource() const noexcept override { return lineSource_; }

    std::size_t fedBytes = 0;
    bool outputHandlerSet = false;
    ZzSize lastSize{80, 24};
    FakeRenderView view_;
    FakeLineSource lineSource_;
    std::string title_;
    int clearCount = 0;
};

} // namespace

int main()
{
    FakeBackend backend;
    ZzTerminalBackend& base = backend; // 可经由基类指针调用

    const char bytes[] = "hi";
    base.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bytes), 2));
    assert(backend.fedBytes == 2);

    assert(base.resize(100, 30));
    assert((base.size() == ZzSize{100, 30})); // 外层括号防止宏参数逗号切分
    assert(!base.isAlternateScreen());
    base.clearDirty();
    assert(backend.clearCount == 1);

    backend.setOutputHandler([](std::string_view) {});
    assert(backend.outputHandlerSet);

    // ZzBackendKind 显式构造签名（双后端 facade）编译期用法。
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    assert(term.size() == (ZzSize { 10, 4 }));

#ifndef ZZTERM_WITH_CONTOUR
    // OFF 构建下 Contour kind 必须抛 std::logic_error（facade 兜底分支）。
    bool thrown = false;
    try { ZzTerminal t(10, 4, ZzBackendKind::Contour, 100); (void)t; }
    catch (const std::logic_error&) { thrown = true; }
    assert(thrown);
#endif
    return 0;
}
