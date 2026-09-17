// ZzTerminal CSI 语义测试（光标移动族）。
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

static ZzPosition cursorOf(const ZzTerminal& term)
{
    return term.cursor().position;
}

static bool posEq(ZzPosition p, int row, int col)
{
    return p.row == row && p.col == col;
}

static void testCupAndHvp()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[2;3H");       // CUP：行列 1 起始
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 1, 2));
    feedStr(term, "\x1b[H");          // 缺省 = 1;1
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 0, 0));
    feedStr(term, "\x1b[999;999f");   // HVP：钳到网格
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 9));
}

static void testCursorMoves()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[2;5H");
    feedStr(term, "\x1b[A");          // CUU 1
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 0, 4));
    feedStr(term, "\x1b[A");          // 顶到 0 行
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 0, 4));
    feedStr(term, "\x1b[3B");         // CUD 3，钳到末行
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 4));
    feedStr(term, "\x1b[10C");        // CUF 钳到末列
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 9));
    feedStr(term, "\x1b[2D");         // CUB 2
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 7));
    feedStr(term, "\x1b[E");          // CNL：下移 + 列首
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 0)); // 已在末行，行不变
    feedStr(term, "\x1b[2F");         // CPL：上移 2 + 列首
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 1, 0));
    feedStr(term, "\x1b[7G");         // CHA：列 7（1 起始）
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 1, 6));
}

static void testCsiSaveRestore()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[2;5H\x1b[s"); // SCOSC
    feedStr(term, "\x1b[1;1H");
    feedStr(term, "\x1b[u");          // SCORC
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 1, 4));
}

static void testPrivateMarkerIgnored()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[?25l");       // DEC 私有模式：M3 范围，安全忽略
    ZZ_TEST_EXPECT(term.cursor().visible); // 不受影响
}

static void testScrollRegionClamping()
{
    ZzTerminal term(10, 6, 100);
    term.screen().setScrollRegion(2, 4); // 区内行 2..4

    feedStr(term, "\x1b[4;1H");   // 光标进区：row=3
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 0));
    feedStr(term, "\x1b[5A");     // CUU 5：区内钳到区上沿 row=2，而非 0
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 2, 0));
    feedStr(term, "\x1b[4;1H");   // 回 row=3
    feedStr(term, "\x1b[99B");    // CUD：区内钳到区下沿 row=4，而非末行
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 4, 0));

    feedStr(term, "\x1b[1;1H");   // 光标出区：row=0
    feedStr(term, "\x1b[1A");     // 区外 CUU 钳到 0，不受区上沿影响
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 0, 0));
    feedStr(term, "\x1b[99B");    // 区外 CUD 钳到网格末行 row=5，不受区下沿影响
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 5, 0));
}

static void testOriginMode()
{
    ZzTerminal term(10, 6, 100);
    term.screen().setScrollRegion(2, 4);
    term.screen().setOriginMode(true); // DECOM

    feedStr(term, "\x1b[1;1H");   // CUP 相对区上沿：row=2
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 2, 0));
    feedStr(term, "\x1b[99;1H");  // 超出滚动区：钳到区下沿 row=4
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 4, 0));
}

static void testVpa()
{
    ZzTerminal term(10, 6, 100);
    feedStr(term, "\x1b[2;5H");   // row=1, col=4
    feedStr(term, "\x1b[3d");     // VPA：绝对行 3（1 起始），列不变
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 2, 4));
    feedStr(term, "\x1b[99d");    // VPA 钳到末行
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 5, 4));
}

int main()
{
    testCupAndHvp();
    testCursorMoves();
    testCsiSaveRestore();
    testPrivateMarkerIgnored();
    testScrollRegionClamping();
    testOriginMode();
    testVpa();
    if (g_failures == 0)
        std::puts("test_terminal_csi: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
