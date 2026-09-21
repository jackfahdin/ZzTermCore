// UAX #29 聚簇断行求值核与双口实现（Unicode 16.0.0，规则 GB1-GB999）。
#include "unicode/GraphemeBreak.h"

#include <array>
#include <cassert>

namespace {

// 生成表：区间按 lo 升序互不重叠，供 zzGraphemePropsOf 二分查找。
#include "ZzTerm/detail/GraphemeBreakData.inc"

[[nodiscard]] bool zzIsControlCrLf(ZzGcb gcb) noexcept
{
    return gcb == ZzGcb::Control || gcb == ZzGcb::CR || gcb == ZzGcb::LF;
}

// 单一求值核：判定 seq（长度 n）内 pos 处（seq[pos-1] 与 seq[pos] 之间）
// 是否断开。调用契约：1 <= pos < n。按 GB 规则顺序求值，左扫只在窗口内
// 进行（GB9c/GB11 的左上下文需求由窗口全文满足）。
[[nodiscard]] bool zzIsGraphemeBoundary(const ZzGraphemeProps* seq, std::size_t n, std::size_t pos) noexcept
{
    const ZzGraphemeProps& left = seq[pos - 1];
    const ZzGraphemeProps& right = seq[pos];

    // GB3：CR × LF。
    if (left.gcb == ZzGcb::CR && right.gcb == ZzGcb::LF)
        return false;
    // GB4/GB5：Control|CR|LF 两侧必断。
    if (zzIsControlCrLf(left.gcb) || zzIsControlCrLf(right.gcb))
        return true;
    // GB6：L × (L|V|LV|LVT)。
    if (left.gcb == ZzGcb::L
        && (right.gcb == ZzGcb::L || right.gcb == ZzGcb::V
            || right.gcb == ZzGcb::LV || right.gcb == ZzGcb::LVT))
        return false;
    // GB7：(LV|V) × (V|T)。
    if ((left.gcb == ZzGcb::LV || left.gcb == ZzGcb::V)
        && (right.gcb == ZzGcb::V || right.gcb == ZzGcb::T))
        return false;
    // GB8：(LVT|T) × T。
    if ((left.gcb == ZzGcb::LVT || left.gcb == ZzGcb::T) && right.gcb == ZzGcb::T)
        return false;
    // GB9：× (Extend|ZWJ)。
    if (right.gcb == ZzGcb::Extend || right.gcb == ZzGcb::ZWJ)
        return false;
    // GB9a：× SpacingMark。
    if (right.gcb == ZzGcb::SpacingMark)
        return false;
    // GB9b：Prepend ×。
    if (left.gcb == ZzGcb::Prepend)
        return false;
    // GB9c：Consonant [Linker Extend ZWJ]* Linker [Extend ZWJ]* × Consonant。
    // （规则中的 Extend 指 InCB Extend，即 GCB=Extend 且非 Linker。
    //   i 为排他右边界，逐段向左消费。）
    if (right.incb == ZzIncb::Consonant) {
        std::size_t i = pos;
        // 紧邻的 [Extend ZWJ]*（Extend 不含 Linker）。
        while (i > 0
               && ((seq[i - 1].gcb == ZzGcb::Extend && seq[i - 1].incb != ZzIncb::Linker)
                   || seq[i - 1].gcb == ZzGcb::ZWJ))
            --i;
        // 必须命中一个 Linker。
        if (i > 0 && seq[i - 1].incb == ZzIncb::Linker) {
            --i;
            // [Linker Extend ZWJ]*。
            while (i > 0
                   && (seq[i - 1].incb == ZzIncb::Linker || seq[i - 1].gcb == ZzGcb::Extend
                       || seq[i - 1].gcb == ZzGcb::ZWJ))
                --i;
            if (i > 0 && seq[i - 1].incb == ZzIncb::Consonant)
                return false;
        }
    }
    // GB11：ExtPic Extend* ZWJ × ExtPic（i 同样为排他右边界）。
    if (right.extPic && left.gcb == ZzGcb::ZWJ) {
        std::size_t i = pos - 1; // seq[i] 即 ZWJ
        while (i > 0 && seq[i - 1].gcb == ZzGcb::Extend)
            --i;
        if (i > 0 && seq[i - 1].extPic)
            return false;
    }
    // GB12/13：边界前连续 RI 计数为奇则续（成对拼接），为偶则断。
    if (left.gcb == ZzGcb::RegionalIndicator && right.gcb == ZzGcb::RegionalIndicator) {
        std::size_t count = 0;
        std::size_t i = pos;
        while (i > 0 && seq[i - 1].gcb == ZzGcb::RegionalIndicator) {
            ++count;
            --i;
        }
        return (count % 2) == 0;
    }
    // GB999：以上皆不命中则断。
    return true;
}

} // namespace

ZzGraphemeProps zzGraphemePropsOf(char32_t cp) noexcept
{
    const auto key = static_cast<std::uint32_t>(cp);
    std::size_t lo = 0;
    std::size_t hi = kZzGcbIntervals.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const ZzGcbInterval& e = kZzGcbIntervals[mid];
        if (key < e.lo)
            hi = mid;
        else if (key > e.hi)
            lo = mid + 1;
        else {
            return ZzGraphemeProps{
                static_cast<ZzGcb>(e.gcb),
                (e.flags & 0x1u) != 0,
                static_cast<ZzIncb>((e.flags >> 1) & 0x3u),
            };
        }
    }
    return ZzGraphemeProps{ ZzGcb::Other, false, ZzIncb::None };
}

void zzGraphemeBreaks(std::u32string_view cps, std::vector<bool>& out)
{
    const std::size_t n = cps.size();
    out.assign(n + 1, false);
    out[0] = true;  // GB1：sot ÷
    out[n] = true;  // GB2：÷ eot
    if (n < 2)
        return;

    std::vector<ZzGraphemeProps> seq(n);
    for (std::size_t i = 0; i < n; ++i)
        seq[i] = zzGraphemePropsOf(cps[i]);

    // O(n·k)：golden 场景 n 小，无性能要求。
    for (std::size_t pos = 1; pos < n; ++pos)
        out[pos] = zzIsGraphemeBoundary(seq.data(), n, pos);
}

bool zzGraphemeContinues(std::u32string_view prevCluster, char32_t next) noexcept
{
    // cluster 很短，上限断言 64：栈上小缓冲拼接 prevCluster + next。
    constexpr std::size_t kMaxCluster = 64;
    assert(!prevCluster.empty());
    assert(prevCluster.size() < kMaxCluster);

    std::array<ZzGraphemeProps, kMaxCluster> seq{};
    const std::size_t n = prevCluster.size() + 1;
    for (std::size_t i = 0; i < prevCluster.size(); ++i)
        seq[i] = zzGraphemePropsOf(prevCluster[i]);
    seq[prevCluster.size()] = zzGraphemePropsOf(next);

    return !zzIsGraphemeBoundary(seq.data(), n, prevCluster.size());
}
