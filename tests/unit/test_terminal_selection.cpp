// facade 选区集成测试（M5a）：setSelection/selectedText、reflow 锚点保持、
// 丢弃平移、Alternate 清除、selectionRange 查询。
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

static void testSelectScreenText()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "hello");
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 5});
    ZZ_TEST_EXPECT(term.hasSelection());
    ZZ_TEST_EXPECT(term.selectedText() == "hello");
    term.clearSelection();
    ZZ_TEST_EXPECT(!term.hasSelection());
    ZZ_TEST_EXPECT(term.selectedText().empty());
}

static void testSelectAcrossSoftWrap()
{
    ZzTerminal term(5, 3, ZzBackendKind::Native, 100);
    feed(term, "abcdefgh"); // 软换行：abcde / fgh
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 8});
    ZZ_TEST_EXPECT(term.selectedText() == "abcdefgh"); // 软换行不插换行
}

static void testSelectHistoryAndScreenSeam()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 100);
    feed(term, "aaaa\r\nbbbb\r\ncccc"); // 2 行屏：aaaa 滚入历史，屏幕 bbbb/cccc
    // 统一空间：历史 2 行 + 屏幕 2 行；选第 0 条逻辑行
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 4});
    ZZ_TEST_EXPECT(term.selectedText() == "aaaa");
}

static void testSelectionRangeQuery()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "hello");
    term.setSelection(ZzLogicalPos{0, 4}, ZzLogicalPos{0, 1}); // 反向
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.selectionRange(s, e));
    ZZ_TEST_EXPECT(s.col == 1 && e.col == 4);
    term.clearSelection();
    ZZ_TEST_EXPECT(!term.selectionRange(s, e));
}

static void testResizeReflowKeepsSelection()
{
    ZzTerminal term(5, 3, ZzBackendKind::Native, 100);
    feed(term, "abcdefgh");
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 8});
    const std::string before = term.selectedText();
    term.resize(10, 3); // 列变触发 reflow，逻辑行集合不变
    ZZ_TEST_EXPECT(term.selectedText() == before);
    // M16 恢复 M5a 原文断言 resize(4,3)（M5a 计划 :1908 记载的避让随硬行
    // 截断语义废弃而失效）：硬行缩列多行化后逻辑行集合不变，选区文本保持。
    term.resize(4, 3);
    ZZ_TEST_EXPECT(term.selectedText() == before);
}

static void testDroppedShiftsAnchor()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 4); // 历史容量 4
    feed(term, "aaaaaaaaaa"); // 第 0 行写满 10 列，处于 wrap-pending
    feed(term, "\r\n");       // 终结该逻辑行（消除 wrap-pending），光标到屏幕行 1
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 10});
    ZZ_TEST_EXPECT(term.selectedText() == "aaaaaaaaaa");
    std::string script;
    for (int i = 0; i < 6; ++i)
        script += "x" + std::to_string(i) + "\r\n"; // 每条在末行滚一行入历史
    feed(term, script);
    // 推演：6 次末行回车共滚入历史 6 行（aaaaaaaaaa, x0..x4），容量 4 丢弃
    // aaaaaaaaaa 与 x0；历史剩 x1..x4，屏幕剩 x5 与空行。选区两端都锚在被丢弃的
    // aaaaaaaaaa 上：内容全丢 → 选区清空（规格 5.5 全丢语义，ZzSelection::
    // onLinesDropped 的 e.line < 0 分支）。若 Terminal.cpp 的 onLinesDropped
    // 接线被删掉，锚点停在 {0,0}-{0,10}，selectedText 会读出历史新第 0 行
    // "x1"，本用例两条断言同时变红。
    ZZ_TEST_EXPECT(!term.hasSelection());
    ZZ_TEST_EXPECT(term.selectedText().empty());
}

static void testAlternateSwitchClearsSelection()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "hello");
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 5});
    ZZ_TEST_EXPECT(term.hasSelection());
    feed(term, "\x1b[?1049h"); // 进 Alternate
    ZZ_TEST_EXPECT(!term.hasSelection());
}

int main()
{
    testSelectScreenText();
    testSelectAcrossSoftWrap();
    testSelectHistoryAndScreenSeam();
    testSelectionRangeQuery();
    testResizeReflowKeepsSelection();
    testDroppedShiftsAnchor();
    testAlternateSwitchClearsSelection();
    if (g_failures == 0)
        std::printf("test_terminal_selection: all passed\n");
    return g_failures;
}
