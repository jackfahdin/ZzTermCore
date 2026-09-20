#pragma once

// Contour 后端的零拷贝渲染视图（M1b）：ZzRenderView 契约实现。
// 视图为借用式：直接读 vtbackend::Screen 行状态，feed/resize 后失效；
// 不含任何 Contour 类型（头文件 C++20 干净），Contour 细节全部收口在 .cpp。

#include <ZzTerm/RenderView.h>

#include <cstdint>

class ZzContourBackend;

class ZzContourRenderView final : public ZzRenderView {
public:
    /// \brief dirty 共享状态，由 adapter 持有（寿命包住本视图）。
    /// Contour 为粗粒度 dirty：有脏时 rowDirty 恒 true、dirtyRange 恒全行。
    struct State {
        std::uint64_t dirtyGeneration = 0;
        bool          dirtySinceClear = false;
    };

    ZzContourRenderView(ZzContourBackend& backend, const State& state) noexcept;

    [[nodiscard]] ZzSize size() const noexcept override;
    [[nodiscard]] bool isAlternateScreen() const noexcept override;
    [[nodiscard]] ZzLineView lineAt(int row) const override;
    [[nodiscard]] ZzCursorState cursor() const override;
    [[nodiscard]] std::uint64_t dirtyGeneration() const noexcept override
    {
        return state_->dirtyGeneration;
    }
    [[nodiscard]] bool rowDirty(int) const noexcept override { return state_->dirtySinceClear; }
    [[nodiscard]] ZzCellRange dirtyRange(int) const noexcept override
    {
        return state_->dirtySinceClear ? ZzCellRange { 0, size().cols } : ZzCellRange {};
    }

private:
    static ZzCellView cellAtThunk(const void* storage, int col);
    static int cellCountThunk(const void* storage) noexcept;
    static bool wrappedThunk(const void* storage) noexcept;

    ZzContourBackend* backend_;
    const State*      state_;
};
