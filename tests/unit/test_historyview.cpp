// ZzTerminal 历史视图（ZzHistoryView，M14）native 后端单测：
// 行数与 0=最旧顺序、容量裁剪与 droppedLineCount、wrapped 标记、
// generation 在滚出/reflow/Alternate 切换时递增、Alternate 历史归零。
#include <cstdint>
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

// 历史第 index 行全部格文本拼接（空白格 text 为空串自然跳过）。
static std::string historyLineText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

// 喂 8 行（a..h 各带 \r\n）进 10x4 终端：滚出 a..e 共 5 行进历史。
static void feedEightLines(ZzTerminal& term)
{
    for (char c = 'a'; c <= 'h'; ++c) {
        std::string s;
        s += c;
        s += "\r\n";
        feedStr(term, s);
    }
}

static void testEmptyInitial()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().droppedLineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().generation() == 0);
}

static void testScrollOutOrder()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(term.historyView().droppedLineCount() == 0); // 容量未满不裁
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "a");            // 0 = 最旧
    ZZ_TEST_EXPECT(historyLineText(term, 4) == "e");
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).cellCount() == 10); // 行宽=当前列宽
    ZZ_TEST_EXPECT(!term.historyView().lineAt(0).wrapped());
}

static void testCapacityTrimAndDropped()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 3); // 历史容量 3
    feedEightLines(term);                             // 滚出 a..e 共 5 行
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);
    ZZ_TEST_EXPECT(term.historyView().droppedLineCount() == 2); // a、b 被裁
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "c");            // 最旧留存
    // 绝对行号换算：droppedLineCount() + index（c 是第 3 条滚出行，0 起序号 2）。
    ZZ_TEST_EXPECT(term.historyView().droppedLineCount() + 0 == 2);
}

static void testWrappedFlag()
{
    ZzTerminal term(5, 2, ZzBackendKind::Native, 100);
    feedStr(term, "abcdefghij\r\n"); // 5 列软换行：「abcde」(wrapped) 滚入历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1);
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "abcde");
    feedStr(term, "klmno\r\n"); // 「fghij」(非 wrapped) 滚入历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);
    ZZ_TEST_EXPECT(historyLineText(term, 1) == "fghij");
    ZZ_TEST_EXPECT(!term.historyView().lineAt(1).wrapped());
}

static void testGenerationBumpsOnScrollOut()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    const std::uint64_t g0 = term.historyView().generation();
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
}

static void testGenerationBumpsOnReflow()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    const std::uint64_t g0 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(20, 4)); // 列变化触发历史 reflow
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "a");            // 内容保留
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).cellCount() == 20); // 行宽不变量
}

static void testAlternateZero()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    const std::uint64_t g0 = term.historyView().generation();
    feedStr(term, "\x1b[?1049h"); // 进 Alternate：历史不可见
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    const std::uint64_t g1 = term.historyView().generation();
    feedStr(term, "\x1b[?1049l"); // 回 Primary：历史恢复可见
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(term.historyView().generation() > g1);
}

int main()
{
    testEmptyInitial();
    testScrollOutOrder();
    testCapacityTrimAndDropped();
    testWrappedFlag();
    testGenerationBumpsOnScrollOut();
    testGenerationBumpsOnReflow();
    testAlternateZero();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
