#pragma once

/**
 * @file UnicodeWidth.h
 * @brief East Asian Width 单元格宽度计算（UAX #11，per-codepoint）。
 *
 * Architecture.md 第 6 节禁止假设 1 code point 等于 1 cell，第 8 节要求
 * 支持 East Asian Width；第 2 节平台无关约束禁止依赖系统 wcwidth(3)，
 * 故宽度数据以紧凑区间表内嵌（由 scripts/gen_unicode_width.py 从
 * Unicode 官方钉版数据生成，版本见 UnicodeWidthData.inc 头部）。
 * grapheme 聚簇（UAX #29：combining/VS/emoji/ZWJ）在此接口之上的
 * 分层实现属 M3，本接口只回答单码位列宽。
 */
#include <array>
#include <cstdint>

/// @brief EAW 类别（仅收录需查表的三类；N/Na/H 与未列出码位默认窄，不入表）。
enum class ZzEawClass : std::uint8_t {
    Wide,      ///< W：全宽（CJK 表意文字等），2 列。
    Fullwidth, ///< F：全角（全角 ASCII 等），2 列。
    Ambiguous  ///< A：歧义（部分希腊/符号等），列宽由配置决定。
};

/// @brief EAW 区间表条目（闭区间码位范围 + 类别）。
struct ZzEawInterval {
    std::uint32_t lo; ///< 区间起点码位（含）。
    std::uint32_t hi; ///< 区间终点码位（含）。
    ZzEawClass cls;   ///< 类别。
};

#include "ZzTerm/detail/UnicodeWidthData.inc"

/**
 * @brief 返回码位的单元格宽度（1 或 2 列，UAX #11 East Asian Width）。
 * @param cp Unicode 码位（调用方保证 UTF-8 解码合法；大于 0x10FFFF 防御性返回 1）。
 * @param ambiguousWide true 时 Ambiguous 类别按 2 列（CJK 环境）；
 *        默认 false 按 1 列（xterm 兼容行为）。
 * @return 单元格宽度（1 或 2）。
 * @note W/F 返回 2；A 由 ambiguousWide 决定；其余（含 combining/控制区间）
 *       返回 1 并独立落格——聚簇归并属 M3，此处注释钉住。
 */
[[nodiscard]] inline constexpr int zzCellWidthOf(char32_t cp, bool ambiguousWide = false) noexcept
{
    const std::uint32_t u = static_cast<std::uint32_t>(cp);
    if (u > 0x10FFFFu)
        return 1;
    std::size_t lo = 0;
    std::size_t hi = kZzEawIntervals.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const ZzEawInterval& e = kZzEawIntervals[mid];
        if (u < e.lo) {
            hi = mid;
        } else if (u > e.hi) {
            lo = mid + 1;
        } else {
            switch (e.cls) {
            case ZzEawClass::Wide:
            case ZzEawClass::Fullwidth:
                return 2;
            case ZzEawClass::Ambiguous:
                return ambiguousWide ? 2 : 1;
            }
        }
    }
    return 1;
}
