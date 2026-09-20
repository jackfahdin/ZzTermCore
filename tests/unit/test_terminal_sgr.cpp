// ZzTerminal SGR 画笔语义测试。
#include <cstdio>
#include <span>
#include <string>

#include "ZzTerm/Terminal.h"

static int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void feedStr(ZzTerminal& term, const std::string& s)
{
    term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size()));
}

static ZzCellView cellAt(const ZzTerminal& term, int row, int col)
{
    return term.renderView().lineAt(row).cellAt(col);
}

static void testBasicAttributes()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b[1;3mAB");     // bold + italic
    ZZ_TEST_EXPECT(cellAt(term, 0, 0).attributes.bold());
    ZZ_TEST_EXPECT(cellAt(term, 0, 0).attributes.italic());
    feedStr(term, "\x1b[22mC");       // bold off（italic 保留）
    ZZ_TEST_EXPECT(!cellAt(term, 0, 2).attributes.bold());
    ZZ_TEST_EXPECT(cellAt(term, 0, 2).attributes.italic());
    feedStr(term, "\x1b[0mD");        // reset
    ZZ_TEST_EXPECT(cellAt(term, 0, 3).attributes == ZzCellAttributes{});
    feedStr(term, "\x1b[mE");         // 空参数 = 0 = reset（先开属性再验证）
    feedStr(term, "\x1b[7mF\x1b[mG");
    ZZ_TEST_EXPECT(cellAt(term, 0, 5).attributes.inverse());
    ZZ_TEST_EXPECT(cellAt(term, 0, 6).attributes == ZzCellAttributes{});
}

static void testColors()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b[31mA");       // ANSI 红
    ZZ_TEST_EXPECT(cellAt(term, 0, 0).foreground == ZzColor::Indexed(1));
    feedStr(term, "\x1b[91mB");       // bright 红 = 索引 9
    ZZ_TEST_EXPECT(cellAt(term, 0, 1).foreground == ZzColor::Indexed(9));
    feedStr(term, "\x1b[42mC");       // 绿底
    ZZ_TEST_EXPECT(cellAt(term, 0, 2).background == ZzColor::Indexed(2));
    feedStr(term, "\x1b[38;5;196mD"); // 256 色
    ZZ_TEST_EXPECT(cellAt(term, 0, 3).foreground == ZzColor::Indexed(196));
    feedStr(term, "\x1b[38;2;1;2;3mE"); // RGB
    ZZ_TEST_EXPECT(cellAt(term, 0, 4).foreground == ZzColor::Rgb(1, 2, 3));
    feedStr(term, "\x1b[48;5;20mF");  // 256 色背景
    ZZ_TEST_EXPECT(cellAt(term, 0, 5).background == ZzColor::Indexed(20));
    feedStr(term, "\x1b[39;49mG");    // 恢复默认前景/背景
    ZZ_TEST_EXPECT(cellAt(term, 0, 6).foreground.isDefault());
    ZZ_TEST_EXPECT(cellAt(term, 0, 6).background.isDefault());
}

static void testSgrDoesNotTouchExistingCells()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "A");               // 默认属性落格
    feedStr(term, "\x1b[1m");         // 之后改画笔
    ZZ_TEST_EXPECT(!cellAt(term, 0, 0).attributes.bold()); // 旧格不受影响
    feedStr(term, "B");
    ZZ_TEST_EXPECT(cellAt(term, 0, 1).attributes.bold());
}

static void testUnderlineAndBlink()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b[4mA");        // 单下划线
    ZZ_TEST_EXPECT(cellAt(term, 0, 0).attributes.underline() == ZzUnderlineStyle::Single);
    feedStr(term, "\x1b[5mB");        // 慢闪
    ZZ_TEST_EXPECT(cellAt(term, 0, 1).attributes.blink() == ZzBlinkStyle::Slow);
    feedStr(term, "\x1b[24;25mC");
    ZZ_TEST_EXPECT(cellAt(term, 0, 2).attributes.underline() == ZzUnderlineStyle::None);
    ZZ_TEST_EXPECT(cellAt(term, 0, 2).attributes.blink() == ZzBlinkStyle::None);
}

int main()
{
    testBasicAttributes();
    testColors();
    testSgrDoesNotTouchExistingCells();
    testUnderlineAndBlink();
    if (g_failures == 0)
        std::puts("test_terminal_sgr: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
