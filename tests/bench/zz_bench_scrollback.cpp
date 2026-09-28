// M8 单元轨 benchmark：直接驱动 ZzScrollback，10k/100k/1M 三档 x Ascii/Mixed 双画像。
// 默认无参跑 10k 短跑（含生成器确定性自检），注册默认 ctest；
// 100k/1M 长跑经 bench-long 目标手动触发。结果 JSON 落盘 cwd。
// 门控不在本程序内判决（T4 对照规格门控判定），本程序只测量、断言自洽、落盘。
#include <ZzTerm/Scrollback.h>

#include "bench_common.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

namespace {

struct ProfileResult {
    double      appendMs    = 0;
    double      lineAtSeqMs = 0;
    double      lineAtRndMs = 0;
    double      widenMs     = 0;
    double      narrowMs    = 0;
    std::size_t rssBytes    = 0;
    std::size_t approxBytes = 0;
};

ProfileResult runProfile(const ZzBenchTier& tier, ZzBenchProfile profile)
{
    constexpr int  kCols = 80;
    ProfileResult  r;
    constexpr std::size_t kBatch = 1000;

    auto sb = zzCreateChunkedScrollback(tier.lines);

    // 1. append 吞吐：kBatch 行一批，逐批生成并移交（生成耗时不计入——先停表式分段：
    //    生成与 append 交替会混入生成耗时，故每批分别计时仅累加 append 段）。
    auto             t0  = std::chrono::steady_clock::now();
    std::size_t      fed = 0;
    const std::size_t batches = (tier.lines + kBatch - 1) / kBatch;
    double           appendOnlyMs = 0;
    for (std::size_t b = 0; b < batches; ++b) {
        const std::size_t n = (fed + kBatch <= tier.lines) ? kBatch : (tier.lines - fed);
        auto              lines = zzBenchMakeLines(42 + b, n, kCols, profile);
        const auto        ta = std::chrono::steady_clock::now();
        sb->append(std::move(lines));
        appendOnlyMs += zzBenchMillisSince(ta);
        fed += n;
    }
    r.appendMs = appendOnlyMs;
    ZZ_TEST_EXPECT(sb->lineCount() == tier.lines);

    // 2. lineAt 顺序全扫 + 随机 10 万次
    t0 = std::chrono::steady_clock::now();
    std::size_t checksum = 0;
    for (std::size_t i = 0; i < tier.lines; ++i)
        checksum += static_cast<std::size_t>(sb->lineAt(i).cellCount());
    r.lineAtSeqMs = zzBenchMillisSince(t0);
    ZZ_TEST_EXPECT(checksum == tier.lines * static_cast<std::size_t>(kCols));

    std::mt19937                            rng(42);
    std::uniform_int_distribution<std::size_t> pick(0, tier.lines - 1);
    t0       = std::chrono::steady_clock::now();
    checksum = 0;
    for (int i = 0; i < 100000; ++i)
        checksum += static_cast<std::size_t>(sb->lineAt(pick(rng)).cellCount());
    r.lineAtRndMs = zzBenchMillisSince(t0);
    ZZ_TEST_EXPECT(checksum == 100000u * static_cast<std::size_t>(kCols));

    // 3. RSS（append 完成后、reflow 前）
    r.rssBytes    = zzBenchRssCurrentBytes();
    r.approxBytes = sb->stats().approxBytes;

    // 4. reflow：80 -> 120 -> 80
    t0 = std::chrono::steady_clock::now();
    sb->reflow(120);
    r.widenMs = zzBenchMillisSince(t0);
    t0 = std::chrono::steady_clock::now();
    sb->reflow(80);
    r.narrowMs = zzBenchMillisSince(t0);

    return r;
}

void writeJson(const ZzBenchTier& tier, const ProfileResult& ascii,
               const ProfileResult& mixed, std::size_t peakBytes)
{
    const std::string path = std::string("zzterm-bench-scrollback-") + tier.name + ".json";
    std::ofstream     js(path);
    js << "{\n"
       << "  \"milestone\": \"M8\",\n"
       << "  \"track\": \"unit-scrollback\",\n"
       << "  \"tier\": \"" << tier.name << "\",\n"
       << "  \"lines\": " << tier.lines << ",\n"
       << "  \"cols\": 80,\n"
       << "  \"ascii\": {\n"
       << "    \"append_ms\": " << ascii.appendMs << ",\n"
       << "    \"lineat_seq_ms\": " << ascii.lineAtSeqMs << ",\n"
       << "    \"lineat_rnd_ms\": " << ascii.lineAtRndMs << ",\n"
       << "    \"reflow_widen_ms\": " << ascii.widenMs << ",\n"
       << "    \"reflow_narrow_ms\": " << ascii.narrowMs << ",\n"
       << "    \"rss_bytes\": " << ascii.rssBytes << ",\n"
       << "    \"approx_bytes\": " << ascii.approxBytes << "\n"
       << "  },\n"
       << "  \"mixed\": {\n"
       << "    \"append_ms\": " << mixed.appendMs << ",\n"
       << "    \"lineat_seq_ms\": " << mixed.lineAtSeqMs << ",\n"
       << "    \"lineat_rnd_ms\": " << mixed.lineAtRndMs << ",\n"
       << "    \"reflow_widen_ms\": " << mixed.widenMs << ",\n"
       << "    \"reflow_narrow_ms\": " << mixed.narrowMs << ",\n"
       << "    \"rss_bytes\": " << mixed.rssBytes << ",\n"
       << "    \"approx_bytes\": " << mixed.approxBytes << "\n"
       << "  },\n"
       << "  \"peak_rss_bytes\": " << peakBytes << "\n"
       << "}\n";
}

} // namespace

int main(int argc, char** argv)
{
    const ZzBenchTier tier = zzBenchTierFromArgs(argc, argv);
    constexpr int     kCols = 80;

    // 负载生成器确定性自检：同种子同输出、异种子异输出、画像间异输出。
    {
        const auto a  = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Ascii);
        const auto b  = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Ascii);
        const auto c  = zzBenchMakeLines(43, 100, kCols, ZzBenchProfile::Ascii);
        const auto mx = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Mixed);
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) == zzBenchLinesChecksum(b));
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) != zzBenchLinesChecksum(c));
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) != zzBenchLinesChecksum(mx));
    }

    const ProfileResult ascii = runProfile(tier, ZzBenchProfile::Ascii);
    const ProfileResult mixed = runProfile(tier, ZzBenchProfile::Mixed);
    writeJson(tier, ascii, mixed, zzBenchRssPeakBytes());

    std::printf("zz_bench_scrollback[%s] ascii: append %.1fms (%.0f lines/s), lineAt seq %.1fms rnd %.1fms, reflow widen %.1fms narrow %.1fms, rss %zuMB\n",
                tier.name, ascii.appendMs,
                ascii.appendMs > 0 ? tier.lines / (ascii.appendMs / 1000.0) : 0.0,
                ascii.lineAtSeqMs, ascii.lineAtRndMs, ascii.widenMs, ascii.narrowMs,
                ascii.rssBytes / (1024 * 1024));
    std::printf("zz_bench_scrollback[%s] mixed: append %.1fms (%.0f lines/s), lineAt seq %.1fms rnd %.1fms, reflow widen %.1fms narrow %.1fms, rss %zuMB\n",
                tier.name, mixed.appendMs,
                mixed.appendMs > 0 ? tier.lines / (mixed.appendMs / 1000.0) : 0.0,
                mixed.lineAtSeqMs, mixed.lineAtRndMs, mixed.widenMs, mixed.narrowMs,
                mixed.rssBytes / (1024 * 1024));
    if (g_failures == 0)
        std::printf("zz_bench_scrollback: all passed\n");
    return g_failures;
}
