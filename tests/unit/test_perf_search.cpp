// 搜索性能门控（M6 基线）：10 万行历史单次子串搜索耗时门控，JSON 落盘 cwd。
// 门控宽松（防回归绊线，非精确基准）；CI 机器慢 2-3 倍仍应通过。
#include <ZzTerm/Terminal.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

int main()
{
    ZzTerminal term(80, 24, ZzBackendKind::Native, 100000);
    // 构造 10 万行历史（每行约 30 字符，分块喂入）
    {
        std::string chunk;
        for (int i = 0; i < 100000; ++i) {
            chunk += "line " + std::to_string(i) + " payload text for search\r\n";
            if (chunk.size() >= 256 * 1024) {
                term.feed(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(chunk.data()), chunk.size()));
                chunk.clear();
            }
        }
        if (!chunk.empty())
            term.feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(chunk.data()), chunk.size()));
    }

    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t matches = term.search("payload");
    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // 10 万行脚本每行一命中：末行换行符后光标落在第 100001 条空逻辑行，
    // 24 行屏 + 10 万容量下 10 万条含 payload 的行全部留存。
    ZZ_TEST_EXPECT(matches == 100000);
    // 门控标定（M6 基线）：本机（i7-14700，-O0）M6 优化后实测 568ms
    // （2026-09-21，见 tests/perf/records/2026-09-21-m6-perf.json；T2 A/B
    // 实测 662ms → 568ms）。m5b 基线 503ms 早于终审 I1 引入的
    // byteToCellEnd 表，不可直接对比。1800ms = 568×3 向上取整百，
    // 保证 CI runner 慢 2-3 倍仍不误报，劣化约 3 倍即触发（M4 惯例）。
    ZZ_TEST_EXPECT(ms < 1800);
    std::printf("perf: search 100k lines %lldms, %zu matches\n", static_cast<long long>(ms), matches);

    std::ofstream js("zzterm-perf-search.json");
    js << "{\n"
       << "  \"date\": \"2026-09-21\",\n"
       << "  \"milestone\": \"m6-perf\",\n"
       << "  \"note\": \"-O0 debug, 100k lines x 30 cols, substring search\",\n"
       << "  \"searchMs\": " << ms << ",\n"
       << "  \"matches\": " << matches << "\n"
       << "}\n";
    if (g_failures == 0)
        std::printf("test_perf_search: all passed\n");
    return g_failures;
}
