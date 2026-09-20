#pragma once

#include <ZzTerm/RenderView.h>

class ZzScreen;

/// \brief native ZzScreen 的零拷贝渲染视图适配（M1b）。
/// 借用 screen（寿命须包住本对象）；feed/resize 后经 lineAt 重新取行句柄即可。
class ZzNativeRenderView final : public ZzRenderView {
public:
    explicit ZzNativeRenderView(const ZzScreen& screen) noexcept;
    [[nodiscard]] ZzSize size() const noexcept override;
    [[nodiscard]] bool isAlternateScreen() const noexcept override;
    [[nodiscard]] ZzLineView lineAt(int row) const override;
    [[nodiscard]] ZzCursorState cursor() const override;
    [[nodiscard]] std::uint64_t dirtyGeneration() const noexcept override;
    [[nodiscard]] bool rowDirty(int row) const noexcept override;
    [[nodiscard]] ZzCellRange dirtyRange(int row) const noexcept override;

private:
    static ZzCellView cellAtThunk(const void* storage, int col);
    static int cellCountThunk(const void* storage) noexcept;
    static bool wrappedThunk(const void* storage) noexcept;
    const ZzScreen* screen_;
};
