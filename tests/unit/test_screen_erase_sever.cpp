// M17c 整行擦除斩链规则测试（Screen 级，mock 接缝回调）。
#include <cstdio>

#include "ZzTerm/Screen.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// 构造 5 行屏，rows 1-2-3 连成折链（r1.wrapped=1, r2.wrapped=1, r3 收尾）。
static ZzScreen makeChainScreen()
{
    ZzScreen scr(10, 5);
    scr.setLineWrapped(1, true);
    scr.setLineWrapped(2, true);
    return scr;
}

// 1. EL \e[K 从列 0（\r\e[K 形态）整行擦除：斩本行出链 + 前驱入链
static void testElFromCol0SeversChain()
{
    ZzScreen scr = makeChainScreen();
    scr.setCursorPosition(ZzPosition{2, 0});
    scr.eraseInLine(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped()); // 本行出链斩
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped()); // 前驱入链斩
}

// 2. EL 行尾部分擦除（起点非列 0）：不斩链（视为编辑）
static void testElPartialKeepsChain()
{
    ZzScreen scr = makeChainScreen();
    scr.setCursorPosition(ZzPosition{2, 5});
    scr.eraseInLine(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(scr.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());
}

// 3. EL \e[2K 整行擦除（任意光标列）：斩链
static void testElAllSeversChain()
{
    ZzScreen scr = makeChainScreen();
    scr.setCursorPosition(ZzPosition{2, 4});
    scr.eraseInLine(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
}

// 4. EL \e[1K 光标在末列=整行覆盖：斩链；光标在中列=部分：不斩
static void testElFromStartBoundary()
{
    ZzScreen full = makeChainScreen();
    full.setCursorPosition(ZzPosition{2, 9}); // 末列（宽 10）
    full.eraseInLine(ZzEraseMode::FromStart, ZzCell{});
    ZZ_TEST_EXPECT(!full.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(!full.lineAt(1).wrapped());

    ZzScreen part = makeChainScreen();
    part.setCursorPosition(ZzPosition{2, 8});
    part.eraseInLine(ZzEraseMode::FromStart, ZzCell{});
    ZZ_TEST_EXPECT(part.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(part.lineAt(1).wrapped());
}

// 5. ED \e[2J 全屏：所有链标清 + 接缝回调触发（Primary）
static void testEdAllSeversAllAndSeam()
{
    ZzScreen scr = makeChainScreen();
    int seamCalls = 0;
    scr.setSeverSeamLinkCallback([&] { ++seamCalls; });
    scr.setLineWrapped(0, true);
    scr.setCursorPosition(ZzPosition{4, 3});
    scr.eraseInDisplay(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(seamCalls == 1); // row 0 入链跨界斩，仅一次
}

// 6. ED \e[J（光标行起向下）：下方整行死亡；光标行出链随之下落斩断，
//    光标行自身（部分擦除）保留入链
static void testEdToEndSeversBelow()
{
    ZzScreen scr = makeChainScreen();          // 链 r1-r2-r3
    scr.setLineWrapped(3, true);               // 延长链到 r4：r1-r2-r3-r4
    scr.setCursorPosition(ZzPosition{3, 4});
    scr.eraseInDisplay(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(3).wrapped());  // r3 出链斩（r4 入链被斩的传导）
    ZZ_TEST_EXPECT(scr.lineAt(2).wrapped());   // r2→r3 链接保留（r3 是部分擦除）
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());
}

// 7. ED \e[J 光标在列 0：光标行算整行覆盖，入链也斩
static void testEdToEndCursorAtCol0()
{
    ZzScreen scr = makeChainScreen(); // 链 r1-r2-r3
    scr.setCursorPosition(ZzPosition{3, 0});
    scr.eraseInDisplay(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(3).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped()); // r3 入链斩
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());  // r1→r2 保留
}

// 8. ED \e[1J（向上）：上方整行死亡含 row 0 → 接缝斩；光标行部分保留
static void testEdFromStartSeversAboveAndSeam()
{
    ZzScreen scr = makeChainScreen(); // 链 r1-r2-r3
    int seamCalls = 0;
    scr.setSeverSeamLinkCallback([&] { ++seamCalls; });
    scr.setCursorPosition(ZzPosition{2, 3});
    scr.eraseInDisplay(ZzEraseMode::FromStart, ZzCell{});
    ZZ_TEST_EXPECT(seamCalls == 1);           // row 0 整行被擦 → 跨界斩
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped()); // r1 出链斩（自身被整行擦）
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(scr.lineAt(2).wrapped());  // r2 部分擦除，出链保留
}

// 9. EL 整行擦除 row 0：接缝回调触发
static void testElRow0FiresSeamCallback()
{
    ZzScreen scr(10, 5);
    scr.setLineWrapped(0, true);
    int seamCalls = 0;
    scr.setSeverSeamLinkCallback([&] { ++seamCalls; });
    scr.setCursorPosition(ZzPosition{0, 0});
    scr.eraseInLine(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(seamCalls == 1);
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
}

// 10. Alternate 缓冲不触发接缝回调（无历史）
static void testAlternateNeverFiresSeam()
{
    ZzScreen scr(10, 5);
    int seamCalls = 0;
    scr.setSeverSeamLinkCallback([&] { ++seamCalls; });
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    scr.setCursorPosition(ZzPosition{0, 0});
    scr.eraseInDisplay(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(seamCalls == 0);
}

// 11. 无回调武装时擦 row 0 不崩（Screen 单测无接线环境）
static void testRow0EraseWithoutCallbackNoCrash()
{
    ZzScreen scr(10, 5);
    scr.setLineWrapped(0, true);
    scr.setCursorPosition(ZzPosition{0, 0});
    scr.eraseInLine(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
}

// 12. 覆盖写不斩链（规格 §3.3：无擦除直接改写视为编辑）
static void testOverwriteKeepsChain()
{
    ZzScreen scr = makeChainScreen();
    ZzCell c;
    c.setWidth(ZzCellWidth::Narrow);
    c.setCodePoint(U'x');
    scr.putCell(ZzPosition{2, 3}, c);
    ZZ_TEST_EXPECT(scr.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());
}

// 13. ED \e[1J 光标在末列：光标行整行覆盖 → 出链+入链均斩（链延长到 r4
//     使出链斩非空断言）；上方整行死亡行按同规则斩——r1 出链随 r2 死亡
//     斩断（对照用例 8 同款传导）。非末列对照：光标行部分擦除保留出链。
static void testEdFromStartCursorAtLastCol()
{
    ZzScreen scr = makeChainScreen(); // 链 r1-r2-r3
    scr.setLineWrapped(3, true);      // 延长到 r4：r1-r2-r3-r4
    scr.setCursorPosition(ZzPosition{3, 9}); // 末列（宽 10）
    scr.eraseInDisplay(ZzEraseMode::FromStart, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(3).wrapped()); // r3 整行覆盖斩出链
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped()); // r3 入链斩
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped()); // r2 整行死亡 → r1 出链斩

    ZzScreen part = makeChainScreen();
    part.setLineWrapped(3, true);
    part.setCursorPosition(ZzPosition{3, 8}); // 非末列：光标行部分擦除
    part.eraseInDisplay(ZzEraseMode::FromStart, ZzCell{});
    ZZ_TEST_EXPECT(part.lineAt(3).wrapped());  // r3 出链保留
    ZZ_TEST_EXPECT(!part.lineAt(2).wrapped()); // r2 整行死亡斩出链
}

int main()
{
    testElFromCol0SeversChain();
    testElPartialKeepsChain();
    testElAllSeversChain();
    testElFromStartBoundary();
    testEdAllSeversAllAndSeam();
    testEdToEndSeversBelow();
    testEdToEndCursorAtCol0();
    testEdFromStartSeversAboveAndSeam();
    testElRow0FiresSeamCallback();
    testAlternateNeverFiresSeam();
    testRow0EraseWithoutCallbackNoCrash();
    testOverwriteKeepsChain();
    testEdFromStartCursorAtLastCol();
    if (g_failures == 0)
        std::puts("PASS test_screen_erase_sever");
    return g_failures == 0 ? 0 : 1;
}
