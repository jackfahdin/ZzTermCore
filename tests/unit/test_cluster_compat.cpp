// M7b T4：双后端聚簇 compat——同一聚簇脚本喂 Native/Contour 两个 ZzTerminal，
// 经统一 ZzRenderView 逐格比对（native 以 contour 为基准）。
// 用例集与 T2 探针/T3 集成测试同源（含 H1/H2 与 T4 并入项 A 四形态）；
// b 类分歧（I-1/I-2/I-6/I-7，控制者已裁定）按先例分别断言钉住，
// 其余用例目标零分歧。期望值直引裁定表：
// docs/superpowers/specs/2026-09-21-m7b-parity-probe.md
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

void feedBytes(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
}

std::vector<std::string_view> splitCodePoints(std::string_view bytes)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char lead = static_cast<unsigned char>(bytes[i]);
        std::size_t len = 1;
        if (lead >= 0xF0)
            len = 4;
        else if (lead >= 0xE0)
            len = 3;
        else if (lead >= 0xC0)
            len = 2;
        out.push_back(bytes.substr(i, len));
        i += len;
    }
    return out;
}

// 同一脚本喂两个后端（native 以 contour 为基准）。
struct Dual {
    ZzTerminal native { 80, 24, ZzBackendKind::Native, 1000 };
    ZzTerminal contour { 80, 24, ZzBackendKind::Contour, 1000 };
    void feedBoth(std::string_view bytes)
    {
        feedBytes(native, bytes);
        feedBytes(contour, bytes);
    }
};

struct Exp {
    int col;
    std::string_view text;
    ZzCellWidth width;
};

void expectCells(const ZzTerminal& t, int row, std::initializer_list<Exp> cells, const char* what)
{
    for (const Exp& e : cells) {
        const ZzCellView c = t.renderView().lineAt(row).cellAt(e.col);
        if (c.text != e.text || c.width != e.width) {
            ++g_failures;
            std::fprintf(stderr, "FAIL %s cell(%d,%d): got{text=%s,w=%d} want{w=%d}\n", what, row,
                         e.col, c.text.c_str(), static_cast<int>(c.width),
                         static_cast<int>(e.width));
        }
    }
}

void expectCursor(const ZzTerminal& t, int row, int col, const char* what)
{
    const ZzPosition p = t.cursor().position;
    if (p.row != row || p.col != col) {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s cursor: got(%d,%d) want(%d,%d)\n", what, p.row, p.col, row,
                     col);
    }
}

void expectFg(const ZzTerminal& t, int row, int col, const ZzColor& want, const char* what)
{
    const ZzCellView c = t.renderView().lineAt(row).cellAt(col);
    if (!(c.foreground == want)) {
        ++g_failures;
        std::fprintf(stderr, "FAIL %s fg(%d,%d)\n", what, row, col);
    }
}

constexpr ZzCellWidth N = ZzCellWidth::Narrow;
constexpr ZzCellWidth L = ZzCellWidth::WideLead;
constexpr ZzCellWidth W = ZzCellWidth::WideContinuation;

// 对齐用例：同一断言块作用于两个后端 × 整喂/切 feed 两式，并比对光标一致。
template <typename Check>
void dualAligned(std::string_view script, Check&& check)
{
    for (int mode = 0; mode < 2; ++mode) {
        Dual d;
        if (mode == 0) {
            d.feedBoth(script);
        } else {
            for (const std::string_view cp : splitCodePoints(script))
                d.feedBoth(cp);
        }
        check(d.native, mode);
        check(d.contour, mode);
        const ZzPosition a = d.native.cursor().position;
        const ZzPosition b = d.contour.cursor().position;
        if (!(a == b)) {
            ++g_failures;
            std::fprintf(stderr, "FAIL cursor parity(mode=%d): native(%d,%d) contour(%d,%d)\n",
                         mode, a.row, a.col, b.row, b.col);
        }
    }
}

// ---------------------------------------------------------------------------
// 对齐用例（T2 探针 01-15a / B2-B4 / SGR1-3 / N1-4 / H1-H2 / A2-A4）
// ---------------------------------------------------------------------------

void test01() { dualAligned("a\xCC\x81", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "a\xCC\x81", N} }, "01");
    expectCursor(t, 0, 1, "01");
}); }

void test02() { dualAligned("e\xCC\x81\xCC\xA7", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "e\xCC\x81\xCC\xA7", N} }, "02");
    expectCursor(t, 0, 1, "02");
}); }

void test03() { dualAligned("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", L}, {1, "", W} }, "03");
    expectCursor(t, 0, 2, "03");
}); }

void test04() { dualAligned("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",
    [](const ZzTerminal& t, int) {
        expectCells(t, 0,
            { {0, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", L},
              {1, "", W} },
            "04");
        expectCursor(t, 0, 2, "04");
    }); }

void test05() { dualAligned("\xF0\x9F\x87\xBA", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xF0\x9F\x87\xBA", L}, {1, "", W} }, "05");
    expectCursor(t, 0, 2, "05");
}); }

void test06() { dualAligned("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", L}, {1, "", W} }, "06");
    expectCursor(t, 0, 2, "06");
}); }

void test07() { dualAligned("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xBA",
    [](const ZzTerminal& t, int) {
        expectCells(t, 0,
            { {0, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", L}, {1, "", W}, {2, "\xF0\x9F\x87\xBA", L},
              {3, "", W} },
            "07");
        expectCursor(t, 0, 4, "07");
    }); }

void test08() { dualAligned("\xE2\x98\x9D\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE2\x98\x9D\xEF\xB8\x8F", L}, {1, "", W} }, "08");
    expectCursor(t, 0, 2, "08");
}); }

void test09() { dualAligned("\xE2\x9D\xA4\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE2\x9D\xA4\xEF\xB8\x8F", L}, {1, "", W} }, "09");
    expectCursor(t, 0, 2, "09");
}); }

void test10() { dualAligned("\xE2\x9D\xA4\xEF\xB8\x8E", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE2\x9D\xA4\xEF\xB8\x8E", N} }, "10");
    expectCursor(t, 0, 1, "10");
}); }

void test11() { dualAligned("1\xEF\xB8\x8F\xE2\x83\xA3", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "1\xEF\xB8\x8F\xE2\x83\xA3", L}, {1, "", W} }, "11");
    expectCursor(t, 0, 2, "11");
}); }

void test12() { dualAligned("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", L}, {1, "", W} }, "12");
    expectCursor(t, 0, 2, "12");
}); }

void test13() { dualAligned("\xE4\xB8\xAD\xCC\x81", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE4\xB8\xAD\xCC\x81", L}, {1, "", W} }, "13");
    expectCursor(t, 0, 2, "13");
}); }

void test14() { dualAligned("a\xE2\x80\x8D\xF0\x9F\x91\xA9", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "a\xE2\x80\x8D", N}, {1, "\xF0\x9F\x91\xA9", L}, {2, "", W} }, "14");
    expectCursor(t, 0, 3, "14");
}); }

void test15a() { dualAligned("\xD8\x80", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xD8\x80", N} }, "15a");
    expectCursor(t, 0, 1, "15a");
}); }

void testB2()
{
    std::string s(78, 'a');
    s += "\xE4\xB8\xAD\xCC\x81";
    dualAligned(s, [](const ZzTerminal& t, int) {
        expectCells(t, 0, { {77, "a", N}, {78, "\xE4\xB8\xAD\xCC\x81", L}, {79, "", W} }, "B2");
        expectCursor(t, 1, 0, "B2");
    });
}

void testB3()
{
    dualAligned("a\xE2\x98\x9D\xEF\xB8\x8F"
                "bc",
                [](const ZzTerminal& t, int) {
                    expectCells(t, 0,
                        { {0, "a", N}, {1, "\xE2\x98\x9D\xEF\xB8\x8F", L}, {2, "", W}, {3, "b", N},
                          {4, "c", N} },
                        "B3");
                    expectCursor(t, 0, 5, "B3");
                });
}

void testB4()
{
    std::string s(79, 'a');
    s += "\xE2\x98\x9D\xEF\xB8\x8F";
    dualAligned(s, [](const ZzTerminal& t, int) {
        expectCells(t, 0, { {78, "a", N}, {79, "\xE2\x98\x9D\xEF\xB8\x8F", N} }, "B4");
        expectCursor(t, 1, 0, "B4");
    });
}

void testSgr1() { dualAligned("\x1b[31ma\x1b[32m\xCC\x81", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "a\xCC\x81", N} }, "SGR1");
    expectFg(t, 0, 0, ZzColor::Indexed(1), "SGR1");
}); }

void testSgr2() { dualAligned("\x1b[31ma\xE2\x98\x9D\x1b[32m\xEF\xB8\x8F\x1b[33mbc",
    [](const ZzTerminal& t, int) {
        expectCells(t, 0,
            { {0, "a", N}, {1, "\xE2\x98\x9D\xEF\xB8\x8F", L}, {2, "", W}, {3, "b", N},
              {4, "c", N} },
            "SGR2");
        expectFg(t, 0, 1, ZzColor::Indexed(1), "SGR2-widen");
        expectFg(t, 0, 2, ZzColor::Indexed(1), "SGR2-cont");
        expectFg(t, 0, 3, ZzColor::Indexed(3), "SGR2-b");
        expectFg(t, 0, 4, ZzColor::Indexed(3), "SGR2-c");
    }); }

void testSgr3() { dualAligned("\x1b[31m\xF0\x9F\x91\xA8\x1b[32m\xE2\x80\x8D\xF0\x9F\x91\xA9",
    [](const ZzTerminal& t, int) {
        expectCells(t, 0, { {0, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", L}, {1, "", W} },
                    "SGR3");
        expectFg(t, 0, 0, ZzColor::Indexed(1), "SGR3");
    }); }

void testN1() { dualAligned("\xF0\x9F\x87\xBA\xCC\x81", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xF0\x9F\x87\xBA\xCC\x81", L}, {1, "", W} }, "N1");
    expectCursor(t, 0, 2, "N1");
}); }

void testN2() { dualAligned("\xE4\xB8\xAD\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE4\xB8\xAD\xEF\xB8\x8F", L}, {1, "", W} }, "N2");
    expectCursor(t, 0, 2, "N2");
}); }

void testN3() { dualAligned("1\xE2\x83\xA3", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "1\xE2\x83\xA3", N} }, "N3");
    expectCursor(t, 0, 1, "N3");
}); }

void testN4() { dualAligned("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", L}, {1, "", W} }, "N4");
    expectCursor(t, 0, 2, "N4");
}); }

void testH1() { dualAligned("\xE1\x84\x80\xE1\x85\xA1", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE1\x84\x80\xE1\x85\xA1", L}, {1, "", W} }, "H1");
    expectCursor(t, 0, 2, "H1");
}); }

void testH2() { dualAligned("\xEA\xB0\x80\xE1\x86\xA8", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xEA\xB0\x80\xE1\x86\xA8", L}, {1, "", W} }, "H2");
    expectCursor(t, 0, 2, "H2");
}); }

// V 系（M7c T3 实测）：Emoji 属性基字符 + VS16 判宽 2；V6 a+VS16 保窄。
void testV1() { dualAligned("0\xEF\xB8\x8F"
                            "9\xEF\xB8\x8F",
    [](const ZzTerminal& t, int) {
        expectCells(t, 0,
            { {0, "0\xEF\xB8\x8F", L}, {1, "", W}, {2, "9\xEF\xB8\x8F", L}, {3, "", W} }, "V1");
        expectCursor(t, 0, 4, "V1");
    }); }

void testV2() { dualAligned("#\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "#\xEF\xB8\x8F", L}, {1, "", W} }, "V2");
    expectCursor(t, 0, 2, "V2");
}); }

void testV3() { dualAligned("*\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "*\xEF\xB8\x8F", L}, {1, "", W} }, "V3");
    expectCursor(t, 0, 2, "V3");
}); }

void testV4() { dualAligned("\xC2\xA9\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xC2\xA9\xEF\xB8\x8F", L}, {1, "", W} }, "V4");
    expectCursor(t, 0, 2, "V4");
}); }

void testV5() { dualAligned("\xE2\x80\xBC\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE2\x80\xBC\xEF\xB8\x8F", L}, {1, "", W} }, "V5");
    expectCursor(t, 0, 2, "V5");
}); }

void testV6() { dualAligned("a\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "a\xEF\xB8\x8F", N} }, "V6");
    expectCursor(t, 0, 1, "V6");
}); }

// V7-V9 方向 2 反例（M7c T3 修复波）：非 variation base + VS16 保窄。
void testV7() { dualAligned("\xE2\x98\x85\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE2\x98\x85\xEF\xB8\x8F", N} }, "V7");
    expectCursor(t, 0, 1, "V7");
}); }

void testV8() { dualAligned("\xE2\x99\x94\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE2\x99\x94\xEF\xB8\x8F", N} }, "V8");
    expectCursor(t, 0, 1, "V8");
}); }

void testV9() { dualAligned("\xE2\x99\xA9\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "\xE2\x99\xA9\xEF\xB8\x8F", N} }, "V9");
    expectCursor(t, 0, 1, "V9");
}); }

// A2a/A2b 变宽覆盖既有内容：右格内容被覆盖（不右移），两后端对齐。
void testA2a() { dualAligned("abc\x1b[2D\xE2\x98\x9D\xEF\xB8\x8F", [](const ZzTerminal& t, int) {
    expectCells(t, 0, { {0, "a", N}, {1, "\xE2\x98\x9D\xEF\xB8\x8F", L}, {2, "", W} }, "A2a");
    expectCursor(t, 0, 3, "A2a");
}); }

void testA2b()
{
    dualAligned("a\xE4\xB8\xAD"
                "b\x1b[3D\xE2\x98\x9D\xEF\xB8\x8F",
                [](const ZzTerminal& t, int) {
                    expectCells(t, 0,
                        { {0, "a", N}, {1, "\xE2\x98\x9D\xEF\xB8\x8F", L}, {2, "", W}, {3, "b", N} },
                        "A2b");
                    expectCursor(t, 0, 3, "A2b");
                });
}

// A3 prev 在 c78 变宽恰好占满行尾：c78 lead + c79 续格，光标停末列。
void testA3()
{
    std::string s(78, 'a');
    s += "\xE2\x98\x9D\xEF\xB8\x8F";
    dualAligned(s, [](const ZzTerminal& t, int) {
        expectCells(t, 0, { {77, "a", N}, {78, "\xE2\x98\x9D\xEF\xB8\x8F", L}, {79, "", W} }, "A3");
        expectCursor(t, 0, 79, "A3");
    });
}

// A4 DECAWM 关 + wrap-pending + 组合符：续末格（native 实现选择与 contour 吻合）。
void testA4()
{
    std::string s = "\x1b[?7l";
    s += std::string(80, 'a');
    s += "\xCC\x81";
    dualAligned(s, [](const ZzTerminal& t, int) {
        expectCells(t, 0, { {78, "a", N}, {79, "a\xCC\x81", N} }, "A4");
        expectCursor(t, 0, 79, "A4");
    });
}

// ---------------------------------------------------------------------------
// b 类分歧（控制者已裁定，分别断言钉住；先例见 test_backend_compat.cpp
// 空单元格宽度类别分歧与 DEC 模式分歧）
// ---------------------------------------------------------------------------

// I-2（15b Prepend + a）：contour 整喂 w2 聚簇 / 切 feed 断成两窄格（跨 feed
// GB9b 缺失）；native 两式一致 w1 聚簇（无 emoji 表现一律基宽）。
void testBI2Prepend()
{
    // 整喂。
    {
        Dual d;
        d.feedBoth("\xD8\x80"
                   "a");
        expectCells(d.contour, 0, { {0, "\xD8\x80"
                                         "a",
                                     L},
                                    {1, "", W} },
                    "I2-contour-整喂");
        expectCells(d.native, 0, { {0, "\xD8\x80"
                                        "a",
                                    N} },
                    "I2-native-整喂");
        expectCursor(d.native, 0, 1, "I2-native-整喂");
    }
    // 切 feed。
    {
        Dual d;
        for (const std::string_view cp : splitCodePoints("\xD8\x80"
                                                         "a"))
            d.feedBoth(cp);
        expectCells(d.contour, 0, { {0, "\xD8\x80", N}, {1, "a", N} }, "I2-contour-切feed");
        expectCells(d.native, 0, { {0, "\xD8\x80"
                                        "a",
                                    N} },
                    "I2-native-切feed");
        expectCursor(d.native, 0, 1, "I2-native-切feed");
    }
}

// I-1（B1 79a + U+0301）：整喂 contour 错附 c0 / native 附当前行尾格 c78
// （contour 整缓冲"前格"定位缺陷，不作 parity 目标）；切 feed 式正常对齐。
void testBI1SoftWrapWhole()
{
    std::string s(79, 'a');
    s += "\xCC\x81";
    // 整喂：b 类分别断言。
    Dual d;
    d.feedBoth(s);
    expectCells(d.contour, 0, { {0, "a\xCC\x81", N}, {1, "a", N}, {78, "a", N} },
                "I1-contour-整喂");
    expectCells(d.native, 0, { {0, "a", N}, {77, "a", N}, {78, "a\xCC\x81", N} },
                "I1-native-整喂");
    expectCursor(d.native, 0, 79, "I1-native-整喂");
    // 切 feed：对齐断言。
    Dual d2;
    for (const std::string_view cp : splitCodePoints(s))
        d2.feedBoth(cp);
    for (const ZzTerminal* t : { &d2.native, &d2.contour }) {
        expectCells(*t, 0, { {77, "a", N}, {78, "a\xCC\x81", N} }, "I1-切feed对齐");
        expectCursor(*t, 0, 79, "I1-切feed对齐");
    }
}

// I-6（A1a ZWJ 链尾列变体 a）：contour 把 👩 跨尾列续入 c77 聚簇；
// native 宽字符末列换行块先于续接分支，链断开（👩 落次行）。
void testBI6ZwjTailA()
{
    std::string s(77, 'a');
    s += "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9";
    for (int mode = 0; mode < 2; ++mode) {
        Dual d;
        if (mode == 0) {
            d.feedBoth(s);
        } else {
            for (const std::string_view cp : splitCodePoints(s))
                d.feedBoth(cp);
        }
        expectCells(d.contour, 0,
            { {76, "a", N}, {77, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", L}, {78, "", W} },
            "I6-contour");
        expectCursor(d.contour, 0, 79, "I6-contour");
        expectCells(d.native, 0,
            { {76, "a", N}, {77, "\xF0\x9F\x91\xA8\xE2\x80\x8D", L}, {78, "", W} },
            "I6-native-row0");
        expectCells(d.native, 1, { {0, "\xF0\x9F\x91\xA9", L}, {1, "", W} }, "I6-native-row1");
        expectCursor(d.native, 1, 2, "I6-native");
    }
}

// I-7（A1b ZWJ 链尾列变体 b）：contour 在 wrap-pending 换行后仍把 👩 回续
// 上行尾聚簇；native ZWJ 续上但 👩 无前格可续而断开落次行。
void testBI7ZwjTailB()
{
    std::string s(78, 'a');
    s += "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9";
    for (int mode = 0; mode < 2; ++mode) {
        Dual d;
        if (mode == 0) {
            d.feedBoth(s);
        } else {
            for (const std::string_view cp : splitCodePoints(s))
                d.feedBoth(cp);
        }
        expectCells(d.contour, 0,
            { {77, "a", N}, {78, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", L}, {79, "", W} },
            "I7-contour");
        expectCursor(d.contour, 1, 0, "I7-contour");
        expectCells(d.native, 0,
            { {77, "a", N}, {78, "\xF0\x9F\x91\xA8\xE2\x80\x8D", L}, {79, "", W} },
            "I7-native-row0");
        expectCells(d.native, 1, { {0, "\xF0\x9F\x91\xA9", L}, {1, "", W} }, "I7-native-row1");
        expectCursor(d.native, 1, 2, "I7-native");
    }
}

} // namespace

int main()
{
    test01();
    test02();
    test03();
    test04();
    test05();
    test06();
    test07();
    test08();
    test09();
    test10();
    test11();
    test12();
    test13();
    test14();
    test15a();
    testB2();
    testB3();
    testB4();
    testSgr1();
    testSgr2();
    testSgr3();
    testN1();
    testN2();
    testN3();
    testN4();
    testH1();
    testH2();
    testV1();
    testV2();
    testV3();
    testV4();
    testV5();
    testV6();
    testV7();
    testV8();
    testV9();
    testA2a();
    testA2b();
    testA3();
    testA4();
    testBI2Prepend();
    testBI1SoftWrapWhole();
    testBI6ZwjTailA();
    testBI7ZwjTailB();
    if (g_failures == 0)
        std::printf("test_cluster_compat: 全部通过（40 对齐用例 + 4 组 b 类分歧断言）\n");
    return g_failures == 0 ? 0 : 1;
}
