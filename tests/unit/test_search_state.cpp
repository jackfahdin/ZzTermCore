// ZzSearchState 搜索状态模型单测（M5b）：持有/查询、丢弃平移、clamp、clear。
#include "../../src/terminal/ZzSearchState.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

ZzSearchState makeState()
{
    ZzSearchState st;
    st.set("needle", ZzSearchOptions{},
           {ZzLogicalRange{{2, 1}, {2, 5}},
            ZzLogicalRange{{10, 0}, {10, 6}},
            ZzLogicalRange{{20, 3}, {20, 8}}});
    return st;
}

} // namespace

static void testTypesDefaults()
{
    ZzSearchOptions opt;
    ZZ_TEST_EXPECT(opt.caseSensitive);
    ZzLogicalRange a{{1, 2}, {3, 4}};
    ZzLogicalRange b{{1, 2}, {3, 4}};
    ZZ_TEST_EXPECT(a == b);
    b.end.col = 9;
    ZZ_TEST_EXPECT(!(a == b));
}

static void testSetAndQuery()
{
    ZzSearchState st = makeState();
    ZZ_TEST_EXPECT(st.matchCount() == 3);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.match(1, s, e));
    ZZ_TEST_EXPECT(s.line == 10 && s.col == 0 && e.line == 10 && e.col == 6);
    ZZ_TEST_EXPECT(!st.match(3, s, e)); // 越界
}

static void testClear()
{
    ZzSearchState st = makeState();
    st.clear();
    ZZ_TEST_EXPECT(st.matchCount() == 0);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(!st.match(0, s, e));
}

static void testOnLinesDroppedRemovesFullyDropped()
{
    ZzSearchState st = makeState();
    st.onLinesDropped(4); // {2,..} 两端 -2 全丢移除；{10,..}→6；{20,..}→16
    ZZ_TEST_EXPECT(st.matchCount() == 2);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.match(0, s, e));
    ZZ_TEST_EXPECT(s.line == 6 && e.line == 6);
    ZZ_TEST_EXPECT(st.match(1, s, e));
    ZZ_TEST_EXPECT(s.line == 16 && e.line == 16);
}

static void testOnLinesDroppedClampsSurvivingStart()
{
    ZzSearchState st;
    st.set("needle", ZzSearchOptions{}, {ZzLogicalRange{{2, 1}, {10, 5}}});
    st.onLinesDropped(4); // start -2 → clamp {0,0}；end 6 存活
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.matchCount() == 1);
    ZZ_TEST_EXPECT(st.match(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 0);
    ZZ_TEST_EXPECT(e.line == 6 && e.col == 5);
}

static void testClampToRemovesOutOfRangeStart()
{
    ZzSearchState st = makeState();
    st.clampTo(12); // {2,..} 存活；{10,..} 存活；{20,..} 起点越界移除
    ZZ_TEST_EXPECT(st.matchCount() == 2);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.match(1, s, e));
    ZZ_TEST_EXPECT(s.line == 10);
}

static void testClampToClampsEnd()
{
    ZzSearchState st;
    st.set("needle", ZzSearchOptions{}, {ZzLogicalRange{{2, 1}, {20, 5}}});
    st.clampTo(12); // start 2 存活；end 20 → clamp 到 11
    ZZ_TEST_EXPECT(st.matchCount() == 1);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.match(0, s, e));
    ZZ_TEST_EXPECT(e.line == 11);
}

int main()
{
    testTypesDefaults();
    testSetAndQuery();
    testClear();
    testOnLinesDroppedRemovesFullyDropped();
    testOnLinesDroppedClampsSurvivingStart();
    testClampToRemovesOutOfRangeStart();
    testClampToClampsEnd();
    if (g_failures == 0)
        std::printf("test_search_state: all passed\n");
    return g_failures;
}
