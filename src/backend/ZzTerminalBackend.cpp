#include "ZzTerminalBackend.h"

#include <ZzTerm/Line.h>

namespace {

// 无历史后端的兜底空视图（M14）：lineCount 恒 0。lineAt 的契约前置条件是
// index < lineCount()，对本视图即永不满足；防御性返回 0 列空行句柄。
class ZzEmptyHistoryView final : public ZzHistoryView {
public:
    [[nodiscard]] std::size_t lineCount() const noexcept override { return 0; }

    [[nodiscard]] ZzLineView lineAt(std::size_t /*index*/) const override
    {
        static const ZzLine emptyLine; // 0 列空行
        return ZzLineView(&emptyLine, &cellAtThunk, &cellCountThunk, &wrappedThunk);
    }

    [[nodiscard]] std::uint64_t droppedLineCount() const noexcept override { return 0; }
    [[nodiscard]] std::uint64_t generation() const noexcept override { return 0; }

private:
    static ZzCellView cellAtThunk(const void* /*storage*/, int /*col*/) { return {}; }
    static int cellCountThunk(const void* /*storage*/) noexcept { return 0; }
    static bool wrappedThunk(const void* /*storage*/) noexcept { return false; }
};

ZzEmptyHistoryView g_emptyHistoryView; // 内部静态，经基类引用借出

} // namespace

const ZzHistoryView& ZzTerminalBackend::historyView() const noexcept
{
    return g_emptyHistoryView;
}
