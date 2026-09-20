// scrollback 性能门控与记录（M4）：10 万行 append/lineAt/reflow 耗时与内存上限。
// 结果写 cwd 下 zzterm-perf-scrollback.json；计划任务负责拷贝入 tests/perf/records/ 入库。
// 门控宽松（防回归绊线，非精确基准）；CI 机器慢 2-3 倍仍应通过。
#include <ZzTerm/Scrollback.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static ZzLine makeFullLine(int cols, bool wrapped)
{
    // 满行 RGB 场景：每格带独立 RGB 前景（内存上限断言的最差情形）。
    ZzLine line(cols);
    for (int i = 0; i < cols; ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint(U'a' + (i % 26));
        c.setForeground(ZzColor::Rgb((std::uint8_t)i, (std::uint8_t)(i * 2), (std::uint8_t)(i * 3)));
        line.setCell(i, c);
    }
    line.setWrapped(wrapped);
    return line;
}

static double millisSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int main()
{
    constexpr std::size_t kLines = 100000;
    constexpr int kCols = 80;

    auto sb = zzCreateChunkedScrollback(kLines);

    // 1. append 吞吐：100 批 x 1000 行（每 3 行一条 wrapped 链）
    auto t0 = std::chrono::steady_clock::now();
    for (int batch = 0; batch < 100; ++batch) {
        std::vector<ZzLine> lines;
        lines.reserve(1000);
        for (int i = 0; i < 1000; ++i)
            lines.push_back(makeFullLine(kCols, (i % 3) != 2));
        sb->append(std::move(lines));
    }
    const double appendMs = millisSince(t0);
    ZZ_TEST_EXPECT(sb->lineCount() == kLines);
    ZZ_TEST_EXPECT(appendMs < 5000); // 门控宽松

    // 2. lineAt 随机访问 10 万次
    std::mt19937 rng(42);
    std::uniform_int_distribution<std::size_t> pick(0, kLines - 1);
    std::size_t checksum = 0;
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i)
        checksum += (std::size_t)sb->lineAt(pick(rng)).cellCount();
    const double lineAtMs = millisSince(t0);
    ZZ_TEST_EXPECT(checksum == 100000u * (std::size_t)kCols);
    ZZ_TEST_EXPECT(lineAtMs < 200);

    // 3. 内存门控：10 万行 x 80 列满行 RGB
    const std::size_t bytes = sb->stats().approxBytes;
    ZZ_TEST_EXPECT(bytes < 160u * 1024u * 1024u); // 理论约 133MB，留 20% 余量

    // 4. reflow 耗时：80 -> 120 -> 40（10 万行全量重组）
    // 门控标定：本机（i7-14700，-O0）基线 widen 约 173ms / narrow 约 178ms
    // （见 tests/perf/records/2026-09-20-m4-reflow.json）；500ms 保证 CI runner
    // 慢 2-3 倍仍不误报，同时劣化约 3 倍即触发，仍是有效回归绊线。
    t0 = std::chrono::steady_clock::now();
    sb->reflow(120);
    const double widenMs = millisSince(t0);
    ZZ_TEST_EXPECT(widenMs < 500);
    t0 = std::chrono::steady_clock::now();
    sb->reflow(40);
    const double narrowMs = millisSince(t0);
    ZZ_TEST_EXPECT(narrowMs < 500);

    // 5. JSON 落盘（cwd，ctest 下为 build/linux-gcc-debug/tests）
    std::ofstream js("zzterm-perf-scrollback.json");
    js << "{\n"
       << "  \"milestone\": \"M4\",\n"
       << "  \"lines\": " << kLines << ",\n"
       << "  \"cols\": " << kCols << ",\n"
       << "  \"append_ms\": " << appendMs << ",\n"
       << "  \"lineat_ms\": " << lineAtMs << ",\n"
       << "  \"reflow_widen_ms\": " << widenMs << ",\n"
       << "  \"reflow_narrow_ms\": " << narrowMs << ",\n"
       << "  \"approx_bytes\": " << bytes << "\n"
       << "}\n";

    std::printf("append %.1fms, lineAt %.1fms, widen %.1fms, narrow %.1fms, bytes %zu\n",
                appendMs, lineAtMs, widenMs, narrowMs, bytes);
    if (g_failures == 0)
        std::printf("test_perf_scrollback: all passed\n");
    return g_failures;
}
