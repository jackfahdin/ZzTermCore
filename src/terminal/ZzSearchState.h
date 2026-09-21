// ZzSearchState：Core 持有的搜索状态（M5b）。pattern/options/matches 快照 +
// 锚定维护（平移/clamp 与 ZzSelection 同一口径：物理计数平移逻辑序号，近似语义）。
#pragma once

#include <ZzTerm/Types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ZzSearchState {
public:
    void set(std::string pattern, ZzSearchOptions options, std::vector<ZzLogicalRange> matches);
    void clear() noexcept;
    [[nodiscard]] std::size_t matchCount() const noexcept;
    // 查询第 index 个 match（坐标升序）；越界返回 false（start/end 不写入）。
    [[nodiscard]] bool match(std::size_t index, ZzLogicalPos& start, ZzLogicalPos& end) const noexcept;
    // 历史头部丢弃 delta 个物理行后平移全部 match：两端全丢的移除，
    // 存活起点负值 clamp 到 {0,0}（语义同 ZzSelection::onLinesDropped）。
    void onLinesDropped(std::uint64_t delta) noexcept;
    // line clamp 到 [0, lineCount)：起点越界的 match 移除，终点 clamp
    //（语义同 ZzSelection::clampTo；col 由提取/查询层按行 clamp）。
    void clampTo(std::int64_t lineCount) noexcept;

private:
    std::string pattern_;
    ZzSearchOptions options_{};
    std::vector<ZzLogicalRange> matches_; // 坐标升序（引擎扫描序天然有序）
};
