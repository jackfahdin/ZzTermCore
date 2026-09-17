#pragma once

/**
 * @file UnicodeWidth.h
 * @brief East Asian Width / grapheme 宽度计算的接入点。
 *
 * Architecture.md §6 明确禁止假设 "1 code point == 1 cell"，§8 要求
 * 支持 East Asian Width、combining mark、variation selector、
 * emoji/ZWJ 的 grapheme 表达。当前 M1 只交付 zzCellWidthOf 占位
 * （恒窄），真实宽度表与聚簇属于后续里程碑。
 *
 * M2+ 计划中的接入设计，实现前需先评审：
 * - zz::zzEastAsianWidth(char32_t) -> ZzWidth 枚举（Narrow/Neutral/Wide/
 *   Fullwidth/Ambiguous），数据来自 Unicode EastAsianWidth.txt，
 *   以二分查找的紧凑区间表内嵌，禁止依赖系统 wcwidth(3)
 *   （平台行为不一致，违反 §2 平台无关约束）；
 * - Ambiguous 宽度必须可由终端配置（CJK 环境按 2 处理）；
 * - grapheme 聚簇（UAX #29）在 code point 流之上单独分层：
 *   combining mark / variation selector / ZWJ 序列 / regional
 *   indicator 归并为一个 grapheme，再由 screen 模块映射为
 *   Cell 的 Narrow/Wide/WideContinuation（§6）；
 * - Unicode 数据版本必须记录在生成文件的头部（§8 可追踪性要求），
 *   并配套生成脚本与区间表单元测试。
 */

/**
 * @brief 返回码位的单元格宽度（1 或 2 列，UAX #11 East Asian Width）。
 * @param cp Unicode 码位。
 * @return 单元格宽度。
 * @note M1 占位实现恒返回 1（全部按窄格）；M2 用真实 East Asian Width
 *       区间表替换本实现，调用方（ZzTerminal::putChar）不变。
 *       grapheme 聚簇（UAX #29）由更高层负责，不在本接口。
 */
[[nodiscard]] inline constexpr int zzCellWidthOf(char32_t) noexcept { return 1; }
