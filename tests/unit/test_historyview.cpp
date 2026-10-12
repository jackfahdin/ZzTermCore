// ZzTerminal 历史视图（ZzHistoryView，M14）native 后端单测：
// 行数与 0=最旧顺序、容量裁剪与 droppedLineCount、wrapped 标记、
// generation 在滚出/reflow/Alternate 切换时递增、Alternate 历史归零。
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

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

// M19：构造注入用行（各测试文件自带脚手架是既有惯例，副本同 test_scrollback.cpp）。
static ZzLine makeLine(int cols, const std::string& text, bool wrapped)
{
    ZzLine line(cols);
    for (int i = 0; i < (int)text.size() && i < cols; ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint((char32_t)text[(std::size_t)i]);
        line.setCell(i, c);
    }
    line.setWrapped(wrapped);
    return line;
}

// 屏幕第 row 行全部格文本拼接（空白格 text 为空串自然跳过）。
static std::string screenLineText(const ZzTerminal& term, int row)
{
    const ZzLineView line = term.renderView().lineAt(row);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
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
    ZZ_TEST_EXPECT(term.historyView().primaryLineCount() == 5); // Primary 下同口径
    const std::uint64_t g0 = term.historyView().generation();
    feedStr(term, "\x1b[?1049h"); // 进 Alternate：历史不可见
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    // buffer 无关口径：Alternate 期间仍返回 Primary 历史真实行数
    ZZ_TEST_EXPECT(term.historyView().primaryLineCount() == 5);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    // Alternate 期间列变 reflow 照样重组 Primary 历史（不可见但存在）
    ZZ_TEST_EXPECT(term.resize(20, 4));
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().primaryLineCount() == 5); // 短行不折，行数不变
    const std::uint64_t g1 = term.historyView().generation();
    feedStr(term, "\x1b[?1049l"); // 回 Primary：历史恢复可见
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(term.historyView().primaryLineCount() == 5);
    ZZ_TEST_EXPECT(term.historyView().generation() > g1);
}

// M19.6 绝对行号稳定 + generation 递增 + 屏幕不动（E-2 减法口径）
static void testTerminalPrependHistoryAbsoluteStable()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 5); // 历史容量 5
    // 喂 10 行：3 行屏幕装不下滚出 8 行，容量 5 裁掉最旧 3 行（D=3，归还额度 3）
    for (int i = 0; i < 10; ++i)
        term.feed(std::as_bytes(std::span("line\r\n", 6)));
    const auto& hv = term.historyView();
    const std::size_t histBefore = hv.lineCount();
    const std::uint64_t droppedBefore = hv.droppedLineCount();
    ZZ_TEST_EXPECT(droppedBefore >= 2); // 前置：额度足够注入 2 行
    const std::uint64_t genBefore = hv.generation();
    const ZzCursorState cursorBefore = term.cursor();
    const std::string screenRow0Before = screenLineText(term, 0); // 记录屏幕首行文本
    const std::string histRow0Before = historyLineText(term, 0);  // 记录最旧历史行文本
    // 注入 2 行（归还真被裁掉的行）
    std::vector<ZzLine> older;
    older.push_back(makeLine(10, "older-one", false));
    older.push_back(makeLine(10, "older-two", false));
    ZZ_TEST_EXPECT(term.prependHistoryLines(std::move(older)) == true);
    ZZ_TEST_EXPECT(hv.lineCount() == histBefore + 2);
    ZZ_TEST_EXPECT(hv.droppedLineCount() == droppedBefore - 2); // 基线回退 N
    // 绝对行号逐行不变：注入前 idx i（绝对号 droppedBefore+i）== 
    // 注入后 idx i+2（绝对号 基线'+i+2）
    for (std::size_t i = 0; i < histBefore; ++i)
        ZZ_TEST_EXPECT(droppedBefore + i == hv.droppedLineCount() + (i + 2));
    // newest = 基线+lineCount 注入前后恒等（应用追加差分天然跳过注入行）
    ZZ_TEST_EXPECT(droppedBefore + histBefore == hv.droppedLineCount() + hv.lineCount());
    // 行身份：注入前 idx 0 的行顺移到 idx 2；注入行占最旧端
    ZZ_TEST_EXPECT(historyLineText(term, 2) == histRow0Before);
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "older-one");
    ZZ_TEST_EXPECT(hv.generation() > genBefore);
    // 屏幕与光标不动
    ZZ_TEST_EXPECT(term.cursor().position.row == cursorBefore.position.row);
    ZZ_TEST_EXPECT(term.cursor().position.col == cursorBefore.position.col);
    ZZ_TEST_EXPECT(screenLineText(term, 0) == screenRow0Before); // 屏幕首行文本与注入前一致
}

// M19.7 Alternate 拒绝
static void testTerminalPrependHistoryAlternateRejected()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    term.feed(std::as_bytes(std::span("\x1b[?1049h", 8)));
    std::vector<ZzLine> older;
    older.push_back(makeLine(10, "older", false));
    ZZ_TEST_EXPECT(term.prependHistoryLines(std::move(older)) == false);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // Alternate 恒 0
}

// M19.8 空输入 true 空操作
static void testTerminalPrependHistoryEmpty()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    ZZ_TEST_EXPECT(term.prependHistoryLines({}) == true);
}

// M19.10 选区/搜索锚点跨注入稳定（E-2 审查修复：注入使既有内容索引顺移
// +n，锚点须同步正移跟随内容；lastDropped 同步观测基线，否则下次 feed 的
// 裁剪 delta 会误食注入的基线回退）
static void testTerminalPrependSelectionAnchorStable()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 5); // 历史容量 5
    // 喂 10 行互异内容：滚出 L0..L7 共 8 行，容量 5 裁 L0..L2（D=3），
    // 历史持 L3..L7（逻辑行 0..4），屏幕 L8/L9/空行。
    for (int i = 0; i < 10; ++i)
        feedStr(term, "L" + std::to_string(i) + "\r\n");
    // 选中逻辑行 1（= L4）+ 搜索 L4：锚点落在同一内容行
    term.setSelection({1, 0}, {2, 0});
    const std::string selBefore = term.selectedText();
    ZZ_TEST_EXPECT(selBefore == "L4\n"); // 整行选区带跨逻辑行换行
    ZZ_TEST_EXPECT(term.search("L4") == 1);
    ZzLogicalPos ms, me;
    ZZ_TEST_EXPECT(term.searchMatch(0, ms, me));
    ZZ_TEST_EXPECT(ms.line == 1);
    // 注入 2 行（额度 3 足够）：既有内容索引顺移 +2
    std::vector<ZzLine> older;
    older.push_back(makeLine(10, "older-one", false));
    older.push_back(makeLine(10, "older-two", false));
    ZZ_TEST_EXPECT(term.prependHistoryLines(std::move(older)) == true);
    // 锚点跟随原内容：选区文本不变、match 顺移到逻辑行 3
    ZZ_TEST_EXPECT(term.selectedText() == "L4\n");
    ZZ_TEST_EXPECT(term.searchMatch(0, ms, me));
    ZZ_TEST_EXPECT(ms.line == 3);
    // 注入后再 feed 触发裁剪：滚出 L8 → 历史 8 行裁 3（o1/o2/L3），
    // 净平移 +2（注入）−3（裁剪）= −1，L4 落逻辑行 0
    feedStr(term, "L10\r\n");
    ZZ_TEST_EXPECT(term.selectedText() == "L4\n");
    ZZ_TEST_EXPECT(term.searchMatch(0, ms, me));
    ZZ_TEST_EXPECT(ms.line == 0);
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
    testTerminalPrependHistoryAbsoluteStable();
    testTerminalPrependHistoryAlternateRejected();
    testTerminalPrependHistoryEmpty();
    testTerminalPrependSelectionAnchorStable();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
