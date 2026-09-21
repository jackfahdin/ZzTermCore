// ZzNativeLineSource：native 后端的统一物理行数据源（M5a）。
// 组合 ZzScrollback（历史）+ ZzScreen（屏幕）；Alternate 屏时历史归零。
#pragma once

#include "../ZzLineSource.h"

class ZzScreen;
class ZzScrollback;

class ZzNativeLineSource final : public ZzIPhysicalLineSource {
public:
    ZzNativeLineSource(const ZzScreen& screen, const ZzScrollback& scrollback) noexcept;

    [[nodiscard]] std::size_t historyLineCount() const override;
    [[nodiscard]] int screenRowCount() const override;
    [[nodiscard]] int cols() const override;
    void lineAt(std::size_t unifiedRow, ZzLine& out) const override;
    [[nodiscard]] bool lineWrapped(std::size_t unifiedRow) const override;
    [[nodiscard]] std::uint64_t droppedLineCount() const override;

private:
    const ZzScreen& screen_;
    const ZzScrollback& scrollback_;
};
