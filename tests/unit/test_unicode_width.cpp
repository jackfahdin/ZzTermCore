// EAW 区间表完整性 + 已知码位抽查（M2）。
#include <ZzTerm/UnicodeWidth.h>

#include <cstdio>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

// 表结构：升序、不重叠、码位合法、条目非空。
void testTableIntegrity()
{
    ZZ_CHECK(kZzEawIntervals.size() > 100); // 合并后约数百区间；过少说明生成退化
    for (std::size_t i = 0; i < kZzEawIntervals.size(); ++i) {
        const ZzEawInterval& e = kZzEawIntervals[i];
        ZZ_CHECK(e.lo <= e.hi);
        ZZ_CHECK(e.hi <= 0x10FFFFu);
        if (i > 0)
            ZZ_CHECK(e.lo > kZzEawIntervals[i - 1].hi);
    }
}

// 已知码位抽查（Unicode 16.0 稳定值）。
void testKnownCodePoints()
{
    ZZ_CHECK(zzCellWidthOf(U'中') == 2);  // U+4E2D W
    ZZ_CHECK(zzCellWidthOf(U'世') == 2);  // U+4E16 W
    ZZ_CHECK(zzCellWidthOf(U'Ａ') == 2);  // U+FF21 F（全角 A）
    ZZ_CHECK(zzCellWidthOf(U'A') == 1);   // U+0041 Na
    ZZ_CHECK(zzCellWidthOf(U'1') == 1);   // U+0031 Na
    ZZ_CHECK(zzCellWidthOf(U'·') == 1);   // U+00B7 A，默认窄
    ZZ_CHECK(zzCellWidthOf(U'·', true) == 2);  // Ambiguous 宽模式
    ZZ_CHECK(zzCellWidthOf(U'α') == 1);   // U+03B1 A（希腊字母），默认窄
    ZZ_CHECK(zzCellWidthOf(static_cast<char32_t>(0x110000)) == 1); // 越界防御
}

// 表内区间端点抽查：首/末区间两端点的判定与类别一致。
void testIntervalEndpoints()
{
    const ZzEawInterval& first = kZzEawIntervals.front();
    const ZzEawInterval& last = kZzEawIntervals.back();
    for (const ZzEawInterval* e : { &first, &last }) {
        const int expected = (e->cls == ZzEawClass::Ambiguous) ? 1 : 2;
        ZZ_CHECK(zzCellWidthOf(static_cast<char32_t>(e->lo)) == expected);
        ZZ_CHECK(zzCellWidthOf(static_cast<char32_t>(e->hi)) == expected);
    }
}

} // namespace

int main()
{
    testTableIntegrity();
    testKnownCodePoints();
    testIntervalEndpoints();
    if (g_failures != 0)
        std::fprintf(stderr, "test_unicode_width: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
