// 双后端搜索 compat（M5b）：同一 VT 脚本喂 Native/Contour，search 的
// match 列表逐一相等（native 为基准）；resize reflow 后 match 保持。
// 分歧按 b 类分别断言 + 注释钉住。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
#define ZZ_CHECK(cond)                                                        \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

struct Dual {
    ZzTerminal native { 10, 3, ZzBackendKind::Native, 100 };
    ZzTerminal contour { 10, 3, ZzBackendKind::Contour, 100 };
    void feedBoth(std::string_view bytes)
    {
        native.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
        contour.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
    }
};

std::vector<ZzLogicalRange> allMatches(ZzTerminal& t)
{
    std::vector<ZzLogicalRange> out;
    ZzLogicalPos s, e;
    for (std::size_t i = 0; t.searchMatch(i, s, e); ++i)
        out.push_back(ZzLogicalRange{s, e});
    return out;
}

void checkSearchEqual(Dual& d, std::string_view pattern, ZzSearchOptions options, const char* what)
{
    const std::size_t a = d.native.search(pattern, options);
    const std::size_t b = d.contour.search(pattern, options);
    if (a != b)
        std::fprintf(stderr, "  count mismatch [%s]: native=%zu contour=%zu\n", what, a, b);
    ZZ_CHECK(a == b);
    ZZ_CHECK(allMatches(d.native) == allMatches(d.contour));
}

} // namespace

// 1. 屏幕区 ASCII
static void testScreenSearch()
{
    Dual d;
    d.feedBoth("hello world");
    checkSearchEqual(d, "world", ZzSearchOptions{}, "screen ascii");
}

// 2. 历史+屏幕统一空间多命中
static void testHistorySearch()
{
    Dual d;
    d.feedBoth("ab\r\nab\r\nab\r\nab\r\nab");
    checkSearchEqual(d, "ab", ZzSearchOptions{}, "history multi");
}

// 3. 软换行链内命中（跨物理行边界）
static void testSoftWrapChainSearch()
{
    Dual d;
    d.feedBoth("0123456789abcde");
    checkSearchEqual(d, "89ab", ZzSearchOptions{}, "softwrap chain");
}

// 4. 宽字符回映
static void testWideCharSearch()
{
    Dual d;
    d.feedBoth("ab界面cd界");
    checkSearchEqual(d, "界", ZzSearchOptions{}, "wide char");
}

// 5. 大小写不敏感
static void testCaseInsensitiveSearch()
{
    Dual d;
    d.feedBoth("hello World");
    ZzSearchOptions insensitive;
    insensitive.caseSensitive = false;
    checkSearchEqual(d, "world", insensitive, "case insensitive");
}

// 6. 零命中
static void testNoMatch()
{
    Dual d;
    d.feedBoth("hello");
    checkSearchEqual(d, "zzz", ZzSearchOptions{}, "no match");
}

// 7. resize reflow 后 match 保持（规格 5.4：逻辑行集合不变，match 天然保持）。
//    正确姿势是 resize 前 search 一次、resize 后直接 allMatches 互比；
//    不能复用 checkSearchEqual——它内部重新 search，验证的是重搜而非保持。
static void testReflowKeepsMatches()
{
    Dual d;
    d.feedBoth("0123456789abcde"); // 10 列软链 15 格
    const std::size_t a = d.native.search("89ab", ZzSearchOptions{});
    const std::size_t b = d.contour.search("89ab", ZzSearchOptions{});
    ZZ_CHECK(a == b);
    ZZ_CHECK(a == 1);
    const std::vector<ZzLogicalRange> nativeBefore = allMatches(d.native);
    const std::vector<ZzLogicalRange> contourBefore = allMatches(d.contour);
    ZZ_CHECK(nativeBefore == contourBefore);
    d.native.resize(5, 3);
    d.contour.resize(5, 3);
    ZZ_CHECK(allMatches(d.native) == nativeBefore);   // resize 前后各自相等
    ZZ_CHECK(allMatches(d.contour) == contourBefore);
    ZZ_CHECK(allMatches(d.native) == allMatches(d.contour)); // resize 后互比
}

int main()
{
    testScreenSearch();
    testHistorySearch();
    testSoftWrapChainSearch();
    testWideCharSearch();
    testCaseInsensitiveSearch();
    testNoMatch();
    testReflowKeepsMatches();
    if (g_failures == 0)
        std::printf("test_search_compat: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
