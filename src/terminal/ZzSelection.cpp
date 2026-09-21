#include "ZzSelection.h"

namespace {
// 词典序比较：先 line 后 col
bool posLess(ZzLogicalPos a, ZzLogicalPos b) noexcept
{
    return a.line < b.line || (a.line == b.line && a.col < b.col);
}
} // namespace

void ZzSelection::set(ZzLogicalPos anchor, ZzLogicalPos extent) noexcept
{
    anchor_ = anchor;
    extent_ = extent;
}

void ZzSelection::extend(ZzLogicalPos extent) noexcept
{
    extent_ = extent;
}

void ZzSelection::clear() noexcept
{
    anchor_ = {};
    extent_ = {};
}

bool ZzSelection::empty() const noexcept
{
    return anchor_ == extent_;
}

bool ZzSelection::range(ZzLogicalPos& start, ZzLogicalPos& end) const noexcept
{
    if (empty())
        return false;
    if (posLess(extent_, anchor_)) {
        start = extent_;
        end = anchor_;
    } else {
        start = anchor_;
        end = extent_;
    }
    return true;
}

void ZzSelection::onLinesDropped(std::uint64_t delta) noexcept
{
    if (empty())
        return;
    const auto shift = static_cast<std::int64_t>(delta);
    ZzLogicalPos s, e;
    // 先规范化再平移：start 负值 clamp {0,0}；end 也负 → 内容全丢 → 清空
    (void) range(s, e);
    e.line -= shift;
    if (e.line < 0) {
        clear();
        return;
    }
    s.line -= shift;
    if (s.line < 0) {
        s.line = 0;
        s.col = 0;
    }
    anchor_ = s;
    extent_ = e;
}

void ZzSelection::clampTo(std::int64_t lineCount) noexcept
{
    if (empty())
        return;
    ZzLogicalPos s, e;
    (void) range(s, e);
    if (s.line >= lineCount) {
        clear(); // 起点已越出有效范围：无内容可选
        return;
    }
    if (e.line >= lineCount)
        e.line = lineCount - 1;
    anchor_ = s;
    extent_ = e;
}
