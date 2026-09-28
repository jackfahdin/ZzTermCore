// M8 百万行实验：benchmark 共享件（RSS 采样 / 计时 / 确定性负载生成）。
// 仅 tests/bench 内部使用，非公开 API，不进 doxygen。
#pragma once

#include <ZzTerm/Line.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// 负载画像：Ascii 纯窄格短行；Mixed 每 3 行一条 wrapped 链 + 约三成 CJK 宽格。
enum class ZzBenchProfile { Ascii, Mixed };

/// 档位：10k（短跑，默认 ctest）/ 100k / 1m（长跑，bench-long 目标手动触发）。
struct ZzBenchTier {
    std::size_t lines; ///< 负载行数。
    const char*  name; ///< 档位名（"10k"/"100k"/"1m"），用于输出与 JSON 文件名。
};

/// 解析 --tier=10k|100k|1m，无参默认 10k；未知参数打印用法并 exit(2)。
ZzBenchTier zzBenchTierFromArgs(int argc, char** argv);

/// 当前 RSS（字节）。Linux 经 /proc/self/status 的 VmRSS；解析失败或非 Linux 返回 0
///（macOS 采样适配属 M9，接口形态已预留）。
std::size_t zzBenchRssCurrentBytes();

/// 峰值 RSS（字节）。Linux 经 getrusage(RUSAGE_SELF) 的 ru_maxrss；非 Linux 返回 0。
std::size_t zzBenchRssPeakBytes();

/// t0 至今的毫秒数。
double zzBenchMillisSince(std::chrono::steady_clock::time_point t0);

/// 生成 count 行负载；seed 固定则输出确定（自检与复现依赖此性质）。
std::vector<ZzLine> zzBenchMakeLines(std::uint64_t seed, std::size_t count, int cols,
                                     ZzBenchProfile profile);

/// 生成 lineCount 行的合成 VT 字节流（行末 \r\n；每行恰含一次 ASCII 标记 "payload"
/// 供搜索断言；Mixed 画像掺 CJK UTF-8 长行，超 80 列自然形成 soft wrap 链）。
/// 内容确定性由构造保证（同行号绑定，无随机源）。
std::string zzBenchMakeVtStream(std::size_t lineCount, ZzBenchProfile profile);

/// 行批校验和（FNV-1a 64，生成器确定性自检用）。
std::uint64_t zzBenchLinesChecksum(const std::vector<ZzLine>& lines);

/// VT 流校验和（FNV-1a 64，自检用）。
std::uint64_t zzBenchStreamChecksum(const std::string& stream);
