// ZzSelection 选区模型单测（M5a）：规范化区间、丢弃平移、clamp、判空。
#include "../../src/terminal/ZzSelection.h"

#include <cstdio>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void testEmptyByDefault()
{
    ZzSelection sel;
    ZZ_TEST_EXPECT(sel.empty());
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(!sel.range(s, e));
}

static void testRangeNormalizesOrder()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{10, 5}, ZzLogicalPos{3, 2}); // anchor 在后
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 3 && s.col == 2);
    ZZ_TEST_EXPECT(e.line == 10 && e.col == 5);
    ZZ_TEST_EXPECT(!sel.empty());
}

static void testRangeSameLine()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{7, 20}, ZzLogicalPos{7, 4});
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 7 && s.col == 4);
    ZZ_TEST_EXPECT(e.line == 7 && e.col == 20);
}

static void testExtendKeepsAnchor()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{1, 0}, ZzLogicalPos{2, 0});
    sel.extend(ZzLogicalPos{5, 3});
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 1 && s.col == 0);
    ZZ_TEST_EXPECT(e.line == 5 && e.col == 3);
}

static void testClear()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{1, 0}, ZzLogicalPos{2, 0});
    sel.clear();
    ZZ_TEST_EXPECT(sel.empty());
}

static void testZeroLengthSelectionIsEmpty()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{4, 9}, ZzLogicalPos{4, 9});
    ZZ_TEST_EXPECT(sel.empty());
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(!sel.range(s, e));
}

static void testOnLinesDroppedShiftsAnchors()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{10, 2}, ZzLogicalPos{20, 3});
    sel.onLinesDropped(6);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 4 && e.line == 14);
}

static void testOnLinesDroppedClampsAtZero()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{2, 5}, ZzLogicalPos{20, 3});
    sel.onLinesDropped(6); // start 平移为负 → clamp 到 0 行 0 列
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 0);
    ZZ_TEST_EXPECT(e.line == 14 && e.col == 3);
}

static void testOnLinesDroppedBeyondExtentClears()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{2, 5}, ZzLogicalPos{4, 3});
    sel.onLinesDropped(6); // 整个选区内容已丢弃 → 选区清空
    ZZ_TEST_EXPECT(sel.empty());
}

static void testClampToLineCount()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{0, 0}, ZzLogicalPos{99, 5});
    sel.clampTo(12);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(e.line == 11);
}

int main()
{
    testEmptyByDefault();
    testRangeNormalizesOrder();
    testRangeSameLine();
    testExtendKeepsAnchor();
    testClear();
    testZeroLengthSelectionIsEmpty();
    testOnLinesDroppedShiftsAnchors();
    testOnLinesDroppedClampsAtZero();
    testOnLinesDroppedBeyondExtentClears();
    testClampToLineCount();
    if (g_failures == 0)
        std::printf("test_selection: all passed\n");
    return g_failures;
}
