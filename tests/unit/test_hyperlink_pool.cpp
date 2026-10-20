// ZzHyperlinkPool intern/LRU 测试（M22）。
// 约定：每个 tests/unit/*.cpp 含 main()，失败返回非零。

#include <cstdio>
#include <string>

#include "screen/HyperlinkPool.h"

static int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void testInternDedup()
{
    ZzHyperlinkPool pool;
    const std::uint16_t a = pool.intern("https://a.example", "");
    const std::uint16_t b = pool.intern("https://b.example", "");
    ZZ_TEST_EXPECT(a != 0 && b != 0 && a != b);
    // 同二元组复用
    ZZ_TEST_EXPECT(pool.intern("https://a.example", "") == a);
    // userId 参与区分：同 uri 不同 id → 不同 hyperlinkId
    const std::uint16_t c = pool.intern("https://a.example", "x");
    ZZ_TEST_EXPECT(c != a);
    // 同 uri 同 id 复用（跨段关联的根基）
    ZZ_TEST_EXPECT(pool.intern("https://a.example", "x") == c);
}

static void testLookup()
{
    ZzHyperlinkPool pool;
    const std::uint16_t a = pool.intern("https://a.example/path?q=1", "u1");
    const ZzHyperlinkEntry* e = pool.lookup(a);
    ZZ_TEST_EXPECT(e != nullptr);
    ZZ_TEST_EXPECT(e->uri == "https://a.example/path?q=1");
    ZZ_TEST_EXPECT(e->userId == "u1");
    ZZ_TEST_EXPECT(pool.lookup(0) == nullptr);     // 0 保留
    ZZ_TEST_EXPECT(pool.lookup(65535) == nullptr); // 未登记 id
}

static void testLruEviction()
{
    ZzHyperlinkPool pool;
    const std::uint16_t first = pool.intern("https://first.example", "");
    // 填满并溢出容量
    for (int i = 0; i < 1100; ++i)
        pool.intern("https://x.example/" + std::to_string(i), "");
    ZZ_TEST_EXPECT(pool.lookup(first) == nullptr); // 最旧被淘汰
}

int main()
{
    testInternDedup();
    testLookup();
    testLruEviction();

    if (g_failures == 0) {
        std::fprintf(stderr, "test_hyperlink_pool: PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_hyperlink_pool: %d failure(s)\n", g_failures);
    return 1;
}
