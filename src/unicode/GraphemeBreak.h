// UAX #29 聚簇（grapheme cluster）断行 segmenter——内部头，不进公开 API。
//
// 三件套：
//   - zzGraphemePropsOf：单码点属性查询（二分查找 GraphemeBreakData.inc）；
//   - zzGraphemeBreaks：全串边界（golden 全量对照用）；
//   - zzGraphemeContinues：续接判定（putChar 回望用）。
// 后两者共用同一求值核 zzIsGraphemeBoundary（.cpp 内部），杜绝两套规则漂移。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// GCB（Grapheme_Cluster_Break）类别，枚举值顺序与
// scripts/gen_grapheme_break.py 的 GCB_ENUM 一致（生成表 gcb 列即此值）。
enum class ZzGcb : std::uint8_t {
    Other,
    CR,
    LF,
    Control,
    Extend,
    Prepend,
    SpacingMark,
    L,
    V,
    T,
    LV,
    LVT,
    RegionalIndicator,
    ZWJ
};

// InCB（Indic_Conjunct_Break）类别；InCB Extend 已由 ZzGcb::Extend 表达。
enum class ZzIncb : std::uint8_t { None, Linker, Consonant };

// 单码点的聚簇断行相关属性打包。
struct ZzGraphemeProps {
    ZzGcb gcb;
    bool extPic;   // Extended_Pictographic
    ZzIncb incb;
};

// 生成表区间条目（include/ZzTerm/detail/GraphemeBreakData.inc 的行格式）：
// flags 位布局 bit0=Extended_Pictographic，bit1-2=InCB（0/1/2 对应 ZzIncb）。
struct ZzGcbInterval {
    std::uint32_t lo;
    std::uint32_t hi;
    std::uint8_t gcb;
    std::uint8_t flags;
};

// 单码点属性（二分查找 GraphemeBreakData.inc；未列出 → Other/false/None）。
[[nodiscard]] ZzGraphemeProps zzGraphemePropsOf(char32_t cp) noexcept;

// 全串边界（golden 用）：out 长度 n+1，out[i]=true 表示 cps[i-1] 与 cps[i]
// 之间有聚簇边界；out[0] 与 out[n] 恒 true（GB1/GB2）。
void zzGraphemeBreaks(std::u32string_view cps, std::vector<bool>& out);

// 续接判定（putChar 回望用）：prevCluster 为前格 cluster 码点串（非空），
// next 为新码点；true = 续接并入前格（无边界），false = 断开新格。
[[nodiscard]] bool zzGraphemeContinues(std::u32string_view prevCluster, char32_t next) noexcept;
