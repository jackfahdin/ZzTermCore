// facade 搜索集成测试（M5b）：search/match 查询、feed 坐标稳定、丢弃平移、
// reflow 保持、Alternate 清空、重复搜索替换、大小写选项。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

void feed(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                      bytes.size()));
}

} // namespace

static void testBasicSearch()
{
    ZzTerminal term(16, 3, ZzBackendKind::Native, 100);
    feed(term, "hello world");
    ZZ_TEST_EXPECT(term.search("world") == 1);
    ZZ_TEST_EXPECT(term.searchMatchCount() == 1);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 6 && e.line == 0 && e.col == 11);
    ZZ_TEST_EXPECT(!term.searchMatch(1, s, e));
    term.clearSearch();
    ZZ_TEST_EXPECT(term.searchMatchCount() == 0);
}

static void testCaseOption()
{
    ZzTerminal term(16, 3, ZzBackendKind::Native, 100);
    feed(term, "hello World");
    ZZ_TEST_EXPECT(term.search("world") == 0); // 默认敏感
    ZzSearchOptions insensitive;
    insensitive.caseSensitive = false;
    ZZ_TEST_EXPECT(term.search("world", insensitive) == 1);
}

static void testReSearchReplaces()
{
    ZzTerminal term(16, 3, ZzBackendKind::Native, 100);
    feed(term, "hello world");
    ZZ_TEST_EXPECT(term.search("hello") == 1);
    ZZ_TEST_EXPECT(term.search("world") == 1); // 替换旧状态
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.col == 6); // 是 world 不是 hello
    ZZ_TEST_EXPECT(term.search("") == 0); // 空 pattern 清空
    ZZ_TEST_EXPECT(term.searchMatchCount() == 0);
}

static void testFeedKeepsMatchCoords()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 100);
    feed(term, "aaaa\r\nbbbb\r\ncccc"); // aaaa/bbbb 入历史
    ZZ_TEST_EXPECT(term.search("aaaa") == 1);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 0 && e.col == 4);
    feed(term, "dddd\r\neeee\r\nffff\r\ngggg"); // 新内容不触发重搜，旧坐标稳定
    ZZ_TEST_EXPECT(term.searchMatchCount() == 1);
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && e.col == 4);
    // 坐标仍指向 "aaaa"：经选区提取自洽验证
    term.setSelection(s, e);
    ZZ_TEST_EXPECT(term.selectedText() == "aaaa");
}

static void testDroppedShiftsMatches()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 4); // 历史容量 4
    feed(term, "aaaaaaaaaa\r\n"); // 逻辑行 0 = aaaaaaaaaa，硬终结
    ZZ_TEST_EXPECT(term.search("aaaaaaaaaa") == 1);
    for (int i = 0; i < 6; ++i)
        feed(term, "x" + std::to_string(i) + "\r\n"); // 挤出容量，逻辑行 0 被丢弃
    ZZ_TEST_EXPECT(term.searchMatchCount() == 0); // 两端全丢的 match 已移除
}

static void testResizeReflowKeepsMatches()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "0123456789abcde"); // 软链 15 格
    ZZ_TEST_EXPECT(term.search("789ab") == 1);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 7 && e.col == 12);
    term.resize(5, 3); // 软链重切为 3 物理行，逻辑行集合不变
    ZZ_TEST_EXPECT(term.searchMatchCount() == 1);
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 7 && e.col == 12);
    term.setSelection(s, e);
    ZZ_TEST_EXPECT(term.selectedText() == "789ab");
}

static void testAlternateSwitchClearsSearch()
{
    ZzTerminal term(16, 3, ZzBackendKind::Native, 100);
    feed(term, "hello world");
    ZZ_TEST_EXPECT(term.search("world") == 1);
    feed(term, "\x1b[?1049h");
    ZZ_TEST_EXPECT(term.searchMatchCount() == 0);
}

int main()
{
    testBasicSearch();
    testCaseOption();
    testReSearchReplaces();
    testFeedKeepsMatchCoords();
    testDroppedShiftsMatches();
    testResizeReflowKeepsMatches();
    testAlternateSwitchClearsSearch();
    if (g_failures == 0)
        std::printf("test_terminal_search: all passed\n");
    return g_failures;
}
