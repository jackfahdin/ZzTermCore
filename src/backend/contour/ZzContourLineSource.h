// ZzContourLineSource：contour 后端的统一物理行数据源（M5a）。
// 历史读取经 ZzContourBackend 新增只读口；丢弃计数由适配层双入口维护：
// feed/纯行数 resize 后调 noteFloor() 累计真实裁剪（stableFloor 前移量；
// zero-history 分支回退不计）；列变化 resize（reflow，RowIdentity::Destroyed）
// 后调 reanchorFloor() 直接对齐不累计。Alternate 屏时历史归零。
#pragma once

#include "../ZzLineSource.h"

class ZzContourBackend;

class ZzContourLineSource final : public ZzIPhysicalLineSource {
public:
    explicit ZzContourLineSource(const ZzContourBackend& backend) noexcept;

    [[nodiscard]] std::size_t historyLineCount() const override;
    [[nodiscard]] int screenRowCount() const override;
    [[nodiscard]] int cols() const override;
    void lineAt(std::size_t unifiedRow, ZzLine& out) const override;
    [[nodiscard]] bool lineWrapped(std::size_t unifiedRow) const override;
    [[nodiscard]] std::uint64_t droppedLineCount() const override;

    // 适配层在每次 feed/纯行数 resize 完成后调用：累计 stableFloor 前移量。
    void noteFloor() noexcept;
    // 适配层在列变化的 resize 完成后调用：直接对齐 floor 不累计。
    // 列变化触发 reflow，RowIdentity::Destroyed，floor 前移不对应真实丢弃
    // （见 ZzContourLineSource.cpp 文件头的调研结论）。
    void reanchorFloor() noexcept;

private:
    const ZzContourBackend& backend_;
    std::int64_t lastFloor_ = 0;
    std::uint64_t droppedAccum_ = 0;
};
