#include "bench_common.h"

#include <ZzTerm/Cell.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#endif

ZzBenchTier zzBenchTierFromArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tier=10k") == 0)
            return {10000, "10k"};
        if (std::strcmp(argv[i], "--tier=100k") == 0)
            return {100000, "100k"};
        if (std::strcmp(argv[i], "--tier=1m") == 0)
            return {1000000, "1m"};
        if (std::strncmp(argv[i], "--profile=", 10) == 0)
            continue; // 画像参数由调用方自行解析（zz_bench_feed）
        std::fprintf(stderr, "unknown arg: %s (expect --tier=10k|100k|1m)\n", argv[i]);
        std::exit(2);
    }
    return {10000, "10k"};
}

std::size_t zzBenchRssCurrentBytes()
{
#if defined(__linux__)
    std::ifstream in("/proc/self/status");
    std::string   key;
    while (in >> key) {
        if (key == "VmRSS:") {
            std::size_t kb = 0;
            in >> kb;
            return kb * 1024;
        }
        in.ignore(4096, '\n');
    }
    return 0; // 解析失败：按 0 处理（调用方仅记录，不断言非零）
#elif defined(__APPLE__)
    task_vm_info_data_t    info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<std::size_t>(info.resident_size);
    return 0; // 采样失败：按 0 处理（约定同上）
#else
    return 0; // 其他平台未实现
#endif
}

std::size_t zzBenchRssPeakBytes()
{
#if defined(__linux__) || defined(__APPLE__)
    struct rusage ru {};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
        return static_cast<std::size_t>(ru.ru_maxrss); // macOS ru_maxrss 单位字节
#else
        return static_cast<std::size_t>(ru.ru_maxrss) * 1024; // Linux ru_maxrss 单位 KB
#endif
    }
    return 0;
#else
    return 0; // 其他平台未实现
#endif
}

double zzBenchMillisSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
        .count();
}

std::vector<ZzLine> zzBenchMakeLines(std::uint64_t seed, std::size_t count, int cols,
                                     ZzBenchProfile profile)
{
    std::vector<ZzLine> lines;
    lines.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        ZzLine line(cols);
        for (int c = 0; c < cols; ++c) {
            ZzCell cell;
            const std::uint64_t mix = seed + i + static_cast<std::size_t>(c);
            if (profile == ZzBenchProfile::Mixed && (c % 3) == 0) {
                cell.setWidth(ZzCellWidth::WideLead);
                cell.setCodePoint(static_cast<char32_t>(0x4E00 + (mix % 64)));
            } else if (profile == ZzBenchProfile::Mixed && (c % 3) == 1) {
                // 宽格配对：c%3==1 处放 WideContinuation，与前一格 WideLead 成对，
                // 保证 Mixed 画像行布局良构（80 % 3 == 2，行尾不残留半对）。
                cell.setWidth(ZzCellWidth::WideContinuation);
                cell.setCodePoint(0);
            } else {
                cell.setWidth(ZzCellWidth::Narrow);
                cell.setCodePoint(static_cast<char32_t>(U'a' + (mix % 26)));
            }
            line.setCell(c, cell);
        }
        // Ascii 全部硬换行；Mixed 每 3 行一条 wrapped 链（链内前两条 wrapped=true）。
        line.setWrapped(profile == ZzBenchProfile::Mixed && (i % 3) != 2);
        lines.push_back(line);
    }
    return lines;
}

std::string zzBenchMakeVtStream(std::size_t lineCount, ZzBenchProfile profile)
{
    std::string stream;
    stream.reserve(lineCount * 48); // 估算行长，减少扩容
    for (std::size_t i = 0; i < lineCount; ++i) {
        stream += "line " + std::to_string(i) + " payload text for search";
        if (profile == ZzBenchProfile::Mixed) {
            // CJK UTF-8 长行尾巴：超 80 列由终端自然 soft wrap 成链。
            stream += " 中文负载一二三四五六七八九十甲乙丙丁戊己庚辛壬癸"
                      "中文负载一二三四五六七八九十甲乙丙丁戊己庚辛壬癸";
        }
        stream += "\r\n";
    }
    return stream;
}

std::uint64_t zzBenchLinesChecksum(const std::vector<ZzLine>& lines)
{
    std::uint64_t h = 1469598103934665603ull; // FNV-1a 64 offset basis
    auto          mix = [&h](std::uint64_t v) {
        for (int b = 0; b < 8; ++b) {
            h ^= (v >> (b * 8)) & 0xFF;
            h *= 1099511628211ull; // FNV prime
        }
    };
    for (const ZzLine& line : lines) {
        mix(static_cast<std::uint64_t>(line.cellCount()));
        mix(line.wrapped() ? 1u : 0u);
        for (int c = 0; c < line.cellCount(); ++c)
            mix(static_cast<std::uint64_t>(line.cellAt(c).codePoint()));
    }
    return h;
}

std::uint64_t zzBenchStreamChecksum(const std::string& stream)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const unsigned char ch : stream) {
        h ^= ch;
        h *= 1099511628211ull;
    }
    return h;
}
