// ZzTerminal print 通路与 pending-wrap 行为测试。
#include <cstdio>
#include <cstring>
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

static char32_t cpAt(const ZzTerminal& term, int row, int col)
{
    return term.renderView().lineAt(row).cellAt(col).codePoint();
}

static void testPrintAscii()
{
    ZzTerminal term(10, 4, 100);
    const ZzTermChanges changes = [&term] {
        const std::string s = "hi";
        return term.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(s.data()), s.size()));
    }();
    ZZ_TEST_EXPECT(changes.screenDirty);
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'h');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'i');
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
}

static void testPrintUtf8()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "中文"); // M1 宽度占位：均按窄格落格
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'中');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'文');
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
}

static void testPendingWrap()
{
    ZzTerminal term(5, 3, 100);
    feedStr(term, "abcde");
    // 写满最后一列：光标停在最后一列，暂不换行（xterm pending-wrap）。
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 4);
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());

    feedStr(term, "f");
    // 下一个可打印字符到达才换行。
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'f');
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 1);
}

static void testPendingWrapClearedByCR()
{
    ZzTerminal term(5, 3, 100);
    feedStr(term, "abcde");
    feedStr(term, "\rX"); // CR 清除 pending-wrap：X 覆盖行首而非换行
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'X');
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(term.cursor().position.col == 1);
}

static void testNoWrapWhenAutoWrapOff()
{
    ZzTerminal term(5, 3, 100);
    term.screen().setAutoWrapMode(false); // DECAWM 关
    feedStr(term, "abcdefg");
    // 不换行：后续字符持续覆盖最后一列。
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 4);
    ZZ_TEST_EXPECT(cpAt(term, 0, 4) == U'g');
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());
}

int main()
{
    testPrintAscii();
    testPrintUtf8();
    testPendingWrap();
    testPendingWrapClearedByCR();
    testNoWrapWhenAutoWrapOff();
    if (g_failures == 0)
        std::puts("test_terminal_core: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
