// M8 facade 轨 benchmark：ZzTerminal + native 后端 feed 灌合成 VT 流，
// 10k/100k/1M 三档 x --profile=ascii|mixed 双画像（每进程一画像，RSS 隔离干净）。
// 默认无参 = 10k + ascii 短跑；ctest 注册 ascii/mixed 两个 10k 短跑；
// 100k/1M 长跑经 bench-long 目标手动触发。结果 JSON 落盘 cwd。
// 门控不在本程序内判决（T4 对照规格门控判定），本程序只测量、断言自洽、落盘。
#include <ZzTerm/Terminal.h>

#include "bench_common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <span>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

namespace {

ZzBenchProfile profileFromArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--profile=ascii") == 0)
            return ZzBenchProfile::Ascii;
        if (std::strcmp(argv[i], "--profile=mixed") == 0)
            return ZzBenchProfile::Mixed;
        if (std::strncmp(argv[i], "--tier=", 7) == 0)
            continue; // tier 由 zzBenchTierFromArgs 解析
        std::fprintf(stderr, "unknown arg: %s (expect --profile=ascii|mixed --tier=10k|100k|1m)\n",
                     argv[i]);
        std::exit(2);
    }
    return ZzBenchProfile::Ascii;
}

const char* profileName(ZzBenchProfile profile)
{
    return profile == ZzBenchProfile::Ascii ? "ascii" : "mixed";
}

} // namespace

int main(int argc, char** argv)
{
    const ZzBenchProfile profile = profileFromArgs(argc, argv);
    const ZzBenchTier    tier    = zzBenchTierFromArgs(argc, argv);

    // VT 流生成自检：两调用逐字节一致；Ascii 与 Mixed 不同；行数不同则流不同。
    {
        const std::string a = zzBenchMakeVtStream(100, ZzBenchProfile::Ascii);
        const std::string b = zzBenchMakeVtStream(100, ZzBenchProfile::Ascii);
        const std::string m = zzBenchMakeVtStream(100, ZzBenchProfile::Mixed);
        ZZ_TEST_EXPECT(zzBenchStreamChecksum(a) == zzBenchStreamChecksum(b));
        ZZ_TEST_EXPECT(zzBenchStreamChecksum(a) != zzBenchStreamChecksum(m));
    }

    const std::string stream = zzBenchMakeVtStream(tier.lines, profile);

    // 容量保证全部负载行留存在历史区：Ascii 每逻辑行恰 1 行历史；
    // Mixed 行长 127-133 列（CJK 串 48 字 = 96 列），80 列屏 soft wrap 为 2 行历史，
    // 故 Mixed 容量需按 2 倍行数预留（+24 行屏幕余量）。容量仅是上限、不产生
    // 预分配，Ascii 走精确值，Mixed 走 2 倍值——否则历史裁剪触发后 matches
    // 断言必失败（裁剪行丢失 payload）。
    const std::size_t historyCap =
        (profile == ZzBenchProfile::Mixed ? tier.lines * 2 : tier.lines) + 24;
    ZzTerminal term(80, 24, ZzBackendKind::Native, historyCap);

    // 1. feed 吞吐（256KB 分块，test_perf_search 先例）
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t off = 0; off < stream.size();) {
        const std::size_t n = std::min<std::size_t>(256 * 1024, stream.size() - off);
        term.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(stream.data() + off), n));
        off += n;
    }
    const double feedMs = zzBenchMillisSince(t0);

    // 2. 灌满后的 RSS
    const std::size_t rssBytes = zzBenchRssCurrentBytes();

    // 3. 全量 search（每行恰一次 "payload"）
    const auto       ts      = std::chrono::steady_clock::now();
    const std::size_t matches = term.search("payload");
    const double     searchMs = zzBenchMillisSince(ts);
    ZZ_TEST_EXPECT(matches == tier.lines);

    // 4. reflow：80 -> 120 -> 80（facade resize 路径）
    const auto tr = std::chrono::steady_clock::now();
    ZZ_TEST_EXPECT(term.resize(120, 24));
    const double widenMs = zzBenchMillisSince(tr);
    const auto tr2 = std::chrono::steady_clock::now();
    ZZ_TEST_EXPECT(term.resize(80, 24));
    const double narrowMs = zzBenchMillisSince(tr2);

    const std::size_t peakBytes = zzBenchRssPeakBytes();

    // 5. JSON 落盘
    const std::string path = std::string("zzterm-bench-feed-") + profileName(profile) + "-"
                             + tier.name + ".json";
    std::ofstream js(path);
    js << "{\n"
       << "  \"milestone\": \"M8\",\n"
       << "  \"track\": \"facade-feed\",\n"
       << "  \"profile\": \"" << profileName(profile) << "\",\n"
       << "  \"tier\": \"" << tier.name << "\",\n"
       << "  \"lines\": " << tier.lines << ",\n"
       << "  \"stream_bytes\": " << stream.size() << ",\n"
       << "  \"feed_ms\": " << feedMs << ",\n"
       << "  \"search_ms\": " << searchMs << ",\n"
       << "  \"search_matches\": " << matches << ",\n"
       << "  \"reflow_widen_ms\": " << widenMs << ",\n"
       << "  \"reflow_narrow_ms\": " << narrowMs << ",\n"
       << "  \"rss_bytes\": " << rssBytes << ",\n"
       << "  \"peak_rss_bytes\": " << peakBytes << "\n"
       << "}\n";

    std::printf("zz_bench_feed[%s/%s]: stream %zuMB, feed %.1fms (%.1f MB/s), search %.1fms (%zu matches), reflow widen %.1fms narrow %.1fms, rss %zuMB peak %zuMB\n",
                profileName(profile), tier.name, stream.size() / (1024 * 1024), feedMs,
                feedMs > 0 ? (stream.size() / (1024.0 * 1024.0)) / (feedMs / 1000.0) : 0.0,
                searchMs, matches, widenMs, narrowMs, rssBytes / (1024 * 1024),
                peakBytes / (1024 * 1024));
    if (g_failures == 0)
        std::printf("zz_bench_feed: all passed\n");
    return g_failures;
}
