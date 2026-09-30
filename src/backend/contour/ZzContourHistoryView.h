#pragma once

#include <ZzTerm/HistoryView.h>
#include <ZzTerm/Line.h>

#include <cstdint>

class ZzContourBackend;
class ZzContourLineSource;

/// \brief Contour 后端的历史只读视图（M14）。
/// Contour 历史只能快照读：lineAt 把快照覆写进视图内部单行缓冲后借出，
/// 下一次 lineAt 或 feed/resize 后句柄失效（契约 §3.1 的 contour 形态）。
/// 借用 backend/lineSource/代计数（寿命须包住本对象）。
class ZzContourHistoryView final : public ZzHistoryView {
public:
    ZzContourHistoryView(const ZzContourBackend& backend,
                         const ZzContourLineSource& lineSource,
                         const std::uint64_t& generation) noexcept;
    [[nodiscard]] std::size_t lineCount() const noexcept override;
    [[nodiscard]] ZzLineView lineAt(std::size_t index) const override;
    [[nodiscard]] std::uint64_t droppedLineCount() const noexcept override;
    [[nodiscard]] std::uint64_t generation() const noexcept override;

private:
    static ZzCellView cellAtThunk(const void* storage, int col);
    static int cellCountThunk(const void* storage) noexcept;
    static bool wrappedThunk(const void* storage) noexcept;
    const ZzContourBackend* backend_;
    const ZzContourLineSource* lineSource_;
    const std::uint64_t* generation_;
    mutable ZzLine lineBuf_; ///< lineAt 快照缓冲（mutable：const 接口下覆写）
};
