// M8 单元轨 benchmark：直接驱动 ZzScrollback，10k/100k/1M 三档（本任务先立自检骨架，
// T2 扩展 append/lineAt/reflow/RSS 全矩阵）。默认无参跑 10k 短跑，注册默认 ctest；
// 100k/1M 长跑经 bench-long 目标手动触发。结果 JSON 落盘 cwd。
// 门控不在本程序内判决（T4 对照规格门控判定），本程序只测量、断言自洽、落盘。
#include <ZzTerm/Scrollback.h>

#include "bench_common.h"

#include <cstdio>
#include <fstream>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

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

    // T2 在此插入全矩阵测量（append/lineAt/reflow/RSS，Ascii+Mixed 双画像）。

    std::printf("zz_bench_scrollback[%s]: selfcheck done\n", tier.name);
    if (g_failures == 0)
        std::printf("zz_bench_scrollback: all passed\n");
    return g_failures;
}
