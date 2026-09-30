#pragma once

#include <ZzTerm/HistoryView.h>

#include <cstdint>

class ZzScreen;
class ZzScrollback;

/// \brief native 后端的 scrollback 历史只读视图（M14）。
/// 借用 screen/scrollback/代计数（寿命须包住本对象）；
/// feed/resize 后经 lineAt 重新取行句柄即可。
class ZzNativeHistoryView final : public ZzHistoryView {
public:
    ZzNativeHistoryView(const ZzScreen& screen, const ZzScrollback& scrollback,
                        const std::uint64_t& generation) noexcept;
    [[nodiscard]] std::size_t lineCount() const noexcept override;
    [[nodiscard]] ZzLineView lineAt(std::size_t index) const override;
    [[nodiscard]] std::uint64_t droppedLineCount() const noexcept override;
    [[nodiscard]] std::uint64_t generation() const noexcept override;

private:
    static ZzCellView cellAtThunk(const void* storage, int col);
    static int cellCountThunk(const void* storage) noexcept;
    static bool wrappedThunk(const void* storage) noexcept;
    const ZzScreen* screen_;
    const ZzScrollback* scrollback_;
    const std::uint64_t* generation_;
};
