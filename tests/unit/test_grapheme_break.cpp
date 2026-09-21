// UAX #29 聚簇 segmenter golden 全量对照 + 属性边界用例（M7b T1）。
// golden 数据源：tests/data/GraphemeBreakTest.txt（Unicode 16.0.0，官方原样入库），
// 路径经编译宏 ZZ_GRAPHEME_TEST_DATA 注入（tests/CMakeLists.txt）。
#include "unicode/GraphemeBreak.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef ZZ_GRAPHEME_TEST_DATA
#error "ZZ_GRAPHEME_TEST_DATA 未定义（见 tests/CMakeLists.txt）"
#endif

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

// 解析一行 golden：÷ 断、× 续，码点十六进制。返回 false 表示空行/注释行。
bool parseGoldenLine(const std::string& line, std::u32string& cps, std::vector<bool>& expected)
{
    cps.clear();
    expected.clear();
    std::istringstream in(line);
    std::string tok;
    bool sawMarker = false;
    while (in >> tok) {
        if (tok == "#")
            break;
        if (tok == "\xC3\xB7") {          // ÷ U+00F7：断
            expected.push_back(true);
            sawMarker = true;
        } else if (tok == "\xC3\x97") {   // × U+00D7：续
            expected.push_back(false);
        } else {
            cps.push_back(static_cast<char32_t>(std::stoul(tok, nullptr, 16)));
        }
    }
    return sawMarker;
}

// 官方 golden 全量对照：逐行构造码点串，调 zzGraphemeBreaks 对照全部边界
// （含首尾），统计用例总数与失败明细，全量跑完断言零失败。
void testGolden()
{
    std::ifstream file(ZZ_GRAPHEME_TEST_DATA);
    ZZ_CHECK(file.is_open());
    if (!file.is_open())
        return;

    std::u32string cps;
    std::vector<bool> expected;
    std::vector<bool> actual;
    std::string line;
    int cases = 0;
    int caseFailures = 0;
    for (int lineno = 1; std::getline(file, line); ++lineno) {
        if (!parseGoldenLine(line, cps, expected))
            continue;
        ++cases;
        zzGraphemeBreaks(cps, actual);
        ZZ_CHECK(actual.size() == expected.size());
        if (actual.size() != expected.size()
            || actual != expected) {
            ++caseFailures;
            if (caseFailures <= 20) { // 失败明细限打前 20 条，防刷屏
                std::fprintf(stderr, "golden 第 %d 行失败：%s\n  期望：", lineno, line.c_str());
                for (std::size_t i = 0; i < expected.size(); ++i)
                    std::fprintf(stderr, "%s", expected[i] ? "÷" : "×");
                std::fprintf(stderr, "\n  实际：");
                for (std::size_t i = 0; i < actual.size(); ++i)
                    std::fprintf(stderr, "%s", actual[i] ? "÷" : "×");
                std::fprintf(stderr, "\n");
            }
        }
    }
    std::printf("golden 用例总数：%d，失败：%d\n", cases, caseFailures);
    ZZ_CHECK(cases > 1000); // 16.0.0 约 1000+ 用例；过少说明数据解析退化
    ZZ_CHECK(caseFailures == 0);
}

// 属性查询边界用例：区间端点、未列出默认、ExtPic/InCB 样例。
void testPropsOf()
{
    // 区间端点码点。
    ZZ_CHECK(zzGraphemePropsOf(0x0300).gcb == ZzGcb::Extend);
    ZZ_CHECK(zzGraphemePropsOf(0x036F).gcb == ZzGcb::Extend); // 区间另一端点
    ZZ_CHECK(zzGraphemePropsOf(0x1F1FA).gcb == ZzGcb::RegionalIndicator);
    ZZ_CHECK(zzGraphemePropsOf(0x200D).gcb == ZzGcb::ZWJ);
    // 未列出码位默认 Other/false/None。
    const ZzGraphemeProps a = zzGraphemePropsOf(U'a');
    ZZ_CHECK(a.gcb == ZzGcb::Other && !a.extPic && a.incb == ZzIncb::None);
    // ExtPic 样例。
    ZZ_CHECK(zzGraphemePropsOf(0x2764).extPic);
    ZZ_CHECK(zzGraphemePropsOf(0x1F469).extPic);
    // InCB 样例：Linker（virama，GCB 同时为 Extend）与 Consonant。
    const ZzGraphemeProps virama = zzGraphemePropsOf(0x094D);
    ZZ_CHECK(virama.incb == ZzIncb::Linker && virama.gcb == ZzGcb::Extend);
    const ZzGraphemeProps ka = zzGraphemePropsOf(0x0915);
    ZZ_CHECK(ka.incb == ZzIncb::Consonant);
}

// zzGraphemeContinues 直接用例（单一求值核的窗口求值路径）。
void testContinues()
{
    // a + 组合符 → 续（GB9）。
    ZZ_CHECK(zzGraphemeContinues(U"a", 0x0301));
    // ExtPic ZWJ × ExtPic → 续（GB11）。
    ZZ_CHECK(zzGraphemeContinues(std::u32string_view{ U"\U0001F468\x200D", 2 }, 0x1F469));
    // ZWJ 前非 ExtPic，GB11 不命中 → 断。
    ZZ_CHECK(!zzGraphemeContinues(std::u32string_view{ U"a\x200D", 2 }, 0x1F469));
    // RI 成对：奇数个 RI 后续，偶数个后断（GB12/13）。
    ZZ_CHECK(zzGraphemeContinues(std::u32string_view{ U"\U0001F1FA", 1 }, 0x1F1F8));
    ZZ_CHECK(!zzGraphemeContinues(std::u32string_view{ U"\U0001F1FA\U0001F1F8", 2 }, 0x1F1FA));
    // Consonant Linker × Consonant → 续（GB9c）。
    ZZ_CHECK(zzGraphemeContinues(std::u32string_view{ U"\x0915\x094D", 2 }, 0x0937));
}

} // namespace

int main()
{
    testPropsOf();
    testContinues();
    testGolden();
    if (g_failures == 0)
        std::printf("test_grapheme_break: 全部通过\n");
    return g_failures == 0 ? 0 : 1;
}
