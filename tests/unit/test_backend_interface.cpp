// ZzTerminalBackend 接口编译契约：证明接口可实例化实现且签名与
// ZzTerminal 公开语义面一致。不含任何真实终端行为（M1 才有实现）。
#include "backend/ZzTerminalBackend.h"

#include <cassert>

namespace {

class FakeBackend : public ZzTerminalBackend {
public:
    FakeBackend() : view_(nullptr, nullptr) {}

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

    std::size_t fedBytes = 0;
    ZzSize lastSize{80, 24};
    ZzRenderView view_;
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
    return 0;
}
