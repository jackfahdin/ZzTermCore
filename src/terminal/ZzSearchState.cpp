#include "ZzSearchState.h"

#include <utility>

void ZzSearchState::set(std::string pattern, ZzSearchOptions options, std::vector<ZzLogicalRange> matches)
{
    pattern_ = std::move(pattern);
    options_ = options;
    matches_ = std::move(matches);
}

void ZzSearchState::clear() noexcept
{
    pattern_.clear();
    options_ = {};
    matches_.clear();
}

std::size_t ZzSearchState::matchCount() const noexcept
{
    return matches_.size();
}

bool ZzSearchState::match(std::size_t index, ZzLogicalPos& start, ZzLogicalPos& end) const noexcept
{
    if (index >= matches_.size())
        return false;
    start = matches_[index].start;
    end = matches_[index].end;
    return true;
}

void ZzSearchState::onLinesDropped(std::uint64_t delta) noexcept
{
    const auto shift = static_cast<std::int64_t>(delta);
    std::size_t w = 0;
    for (std::size_t i = 0; i < matches_.size(); ++i) {
        ZzLogicalRange m = matches_[i];
        m.end.line -= shift;
        if (m.end.line < 0)
            continue; // 两端全丢：移除
        m.start.line -= shift;
        if (m.start.line < 0) {
            m.start.line = 0;
            m.start.col = 0;
        }
        matches_[w++] = m;
    }
    matches_.resize(w);
}

void ZzSearchState::clampTo(std::int64_t lineCount) noexcept
{
    std::size_t w = 0;
    for (std::size_t i = 0; i < matches_.size(); ++i) {
        ZzLogicalRange m = matches_[i];
        if (m.start.line >= lineCount)
            continue; // 起点越界：无可锚内容，移除
        if (m.end.line >= lineCount)
            m.end.line = lineCount - 1;
        matches_[w++] = m;
    }
    matches_.resize(w);
}
