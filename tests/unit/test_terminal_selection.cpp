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
    // 简报原文为 resize(4, 3)：与 M4 钉死的「硬行永不多行化、缩列截断」
    // 语义冲突（长到 10 列后软链已合并为硬行，缩到 4 列按规格截断为 "abcd"，
    // test_native_reflow.cpp:46 钉住该行为）。改为缩到 8 列（不小于内容宽，
    // 不触发截断），仍覆盖「缩列 reflow 选区文本保持」。
    term.resize(8, 3);
    ZZ_TEST_EXPECT(term.selectedText() == before);
}

static void testDroppedShiftsAnchor()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 4); // 历史容量 4
    feed(term, "aaaaaaaaaa"); // 第 0 行内容
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 10});
    std::string script;
    for (int i = 0; i < 6; ++i)
        script += "x" + std::to_string(i) + "\r\n"; // 挤出历史容量
    feed(term, script);
    // 原选区内容已被丢弃：锚点 clamp 到 0 或选区清空，两种都合法——
    // 断言不为崩溃且文本不再是原始内容
    ZZ_TEST_EXPECT(term.selectedText() != "aaaaaaaaaa");
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
