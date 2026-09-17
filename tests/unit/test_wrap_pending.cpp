// ZzScreen wrap-pending 状态原语测试（xterm 行尾延迟换行语义）。
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

static void testBasic()
{
    ZzScreen scr(5, 3);
    ZZ_TEST_EXPECT(!scr.wrapPending()); // 初始无标志

    scr.setWrapPending(true);
    ZZ_TEST_EXPECT(scr.wrapPending());
    scr.setWrapPending(false);
    ZZ_TEST_EXPECT(!scr.wrapPending());
}

static void testClearedByCursorMove()
{
    ZzScreen scr(5, 3);
    scr.setWrapPending(true);
    scr.setCursorPosition(ZzPosition{0, 0});
    ZZ_TEST_EXPECT(!scr.wrapPending());
}

static void testClearedByEraseAndScroll()
{
    ZzScreen scr(5, 3);
    scr.setWrapPending(true);
    scr.eraseInLine(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.eraseInDisplay(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.scrollUp(1, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.scrollDown(1, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.insertLines(1, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.deleteLines(1, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());
}

static void testSavedWithCursor()
{
    ZzScreen scr(5, 3);
    scr.setWrapPending(true);
    scr.saveCursor();
    scr.setWrapPending(false);
    scr.restoreCursor();
    ZZ_TEST_EXPECT(scr.wrapPending()); // DECRC 连带恢复标志
}

static void testPerBuffer()
{
    ZzScreen scr(5, 3);
    scr.setWrapPending(true);
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    ZZ_TEST_EXPECT(!scr.wrapPending()); // Alternate 独立状态
    scr.setActiveBuffer(ZzScreenBuffer::Primary);
    ZZ_TEST_EXPECT(scr.wrapPending());  // Primary 的标志仍在
}

int main()
{
    testBasic();
    testClearedByCursorMove();
    testClearedByEraseAndScroll();
    testSavedWithCursor();
    testPerBuffer();
    if (g_failures == 0)
        std::puts("test_wrap_pending: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
