// ZzScreen::reflow 历史顶补测试（M16c）：列变重组内容收缩时从最新历史
// 顶补填满（内容贴底锚定），历史不足余量补空，Alternate 不顶补。
#include <cstdio>
#include <string>
#include <vector>

#include "ZzTerm/Screen.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// 用 putCell 写 ASCII 串到指定行。
static void writeRow(ZzScreen& scr, int row, const std::string& text)
{
    for (int i = 0; i < (int)text.size(); ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint((char32_t)text[(std::size_t)i]);
        scr.putCell(ZzPosition{row, i}, c);
    }
}

static std::string rowText(const ZzLine& line, int n)
{
    std::string s;
    for (int i = 0; i < n && i < line.cellCount(); ++i) {
        const ZzCell& c = line.cellAt(i);
        s += (char)(c.isEmpty() ? ' ' : c.codePoint());
    }
    return s;
}

// 构造 cols 列、内容为 text 的行（供 pull 回调返回）。
static ZzLine makeTextLine(int cols, const std::string& text)
{
    ZzLine line(cols);
    for (int i = 0; i < (int)text.size(); ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint((char32_t)text[(std::size_t)i]);
        line.setCell(i, c);
    }
    return line;
}

// 1. 拉宽顶补：缺口 1，顶补 1 行入顶部，内容贴底，光标随顶补平移
static void testWidenTopFill()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa"); // 满行，链首
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");         // 链尾：链 12 格
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{3, 2});
    std::size_t asked = 0;
    scr.setHistoryPullCallback([&](std::size_t maxLines) {
        asked = maxLines;
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(20, "h9")); // 20 列（新列宽）
        return pulled;
    });
    scr.reflow(20); // 链 12 格合 1 行：产出 3 行 < 4，缺口 1 顶补
    ZZ_TEST_EXPECT(asked == 1);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h9");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "s2");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3); // 链跟踪行 2 + 顶补 1
    ZZ_TEST_EXPECT(scr.cursor().position.col == 2);
}

// 2. 历史不足：缺口 2 实取 1，余量底部补空
static void testTopFillPartialHistory()
{
    ZzScreen scr(10, 5);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "aa"); // 链 1：12 个 a
    writeRow(scr, 2, "bbbbbbbbbb");
    scr.setLineWrapped(2, true);
    writeRow(scr, 3, "bb"); // 链 2：12 个 b
    writeRow(scr, 4, "s1");
    scr.setCursorPosition(ZzPosition{4, 1});
    std::size_t asked = 0;
    scr.setHistoryPullCallback([&](std::size_t maxLines) {
        asked = maxLines;
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(20, "h9")); // 只有 1 行可取
        return pulled;
    });
    scr.reflow(20); // 产出 3 行 < 5：缺口 2，顶补 1 + 底部补空 1
    ZZ_TEST_EXPECT(asked == 2);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h9");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 12) == "aaaaaaaaaaaa");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 12) == "bbbbbbbbbbbb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(4), 2) == "  "); // 余量补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3); // 链跟踪行 2 + 顶补 1
    ZZ_TEST_EXPECT(scr.cursor().position.col == 1);
}

// 3. 无回调退化：维持底部补空（M4 语义不回归）
static void testNoCallbackPadsBottom()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{3, 2});
    scr.reflow(20); // 无 HistoryPullCallback：底部补空
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "  ");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 2);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 2);
}

// 4. Alternate 不顶补：备用屏缺口仍底部补空（回调存在也不调）
static void testAlternateNoTopFill()
{
    ZzScreen scr(10, 4); // primary 4 空行：重组无缺口，不会触发回调
    bool pullCalled = false;
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    scr.setCursorPosition(ZzPosition{1, 2});
    scr.reflow(20); // alt 产出 3 行 < 4：mayScrollOut=false，不顶补
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "  ");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0); // 链内偏移 12 -> (0,12)
    ZZ_TEST_EXPECT(scr.cursor().position.col == 12);
}

// 5. 顶补行 wrapped 标志保留（链头留历史的合法跨缝态）
static void testTopFillPreservesWrapped()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{3, 2});
    scr.setHistoryPullCallback([](std::size_t) {
        std::vector<ZzLine> pulled;
        ZzLine line = makeTextLine(20, "h9");
        line.setWrapped(true); // 模拟链头留在历史的半链顶补
        pulled.push_back(std::move(line));
        return pulled;
    });
    scr.reflow(20);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h9");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());
}

// 6. 光标在链内：链偏移跟踪 + 顶补平移复合
static void testTopFillCursorInsideChain()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{1, 1}); // 链内：偏移 10+1=11
    scr.setHistoryPullCallback([](std::size_t) {
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(20, "h9"));
        return pulled;
    });
    scr.reflow(20);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1); // 链跟踪 (0,11) + 顶补 1
    ZZ_TEST_EXPECT(scr.cursor().position.col == 11);
}

// M17a-4：光标在折链上扩列——豁免收链，布局/旗标/光标不动，无顶补需求
static void testPreserveCursorChainScreen()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa"); // 链首（10 格）
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");         // 链尾（链 12 格）
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{1, 1}); // 光标在链上（片段 1 列 1）
    std::size_t asked = 0;
    scr.setHistoryPullCallback([&](std::size_t maxLines) {
        asked = maxLines;
        return std::vector<ZzLine>{};
    });
    scr.reflow(20);
    ZZ_TEST_EXPECT(asked == 0); // 豁免链贡献 2 行，产出 4 行 == rows，无缺口
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 10) == "aaaaaaaaaa");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "bb");
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "s2");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 1);
}

// M17a-5：光标在折链上缩列——不豁免，照常重切溢出
static void testPreserveSkippedOnShrinkScreen()
{
    ZzScreen scr(20, 4);
    writeRow(scr, 0, "aaaaaaaaaaaaaaaaaaaa"); // 链首（20 格）
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");                   // 链尾（链 22 格）
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{1, 1});
    std::size_t spilled = 0;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) { spilled += lines.size(); });
    scr.reflow(10); // 链 22 格 -> 3 行，产出 5 行 > 4，溢出 1 行压历史
    ZZ_TEST_EXPECT(spilled == 1);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 10) == "aaaaaaaaaa");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());
    // 勘误 E-1：溢出从顶部删 1 行（链首 a10 压历史），剩余 [a10(w), bb, s1, s2]
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "bb");
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "s2");
    // 光标：链偏移 21 -> 10 列下行 2 列 1，减溢出 1 -> 行 1 列 1
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 1);
}

// M17a-6：Alternate 缓冲不豁免（全屏应用自带重绘，无 readline 帧假设）
static void testPreserveSkippedOnAlternate()
{
    ZzScreen scr(10, 4);
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{1, 1}); // Alternate 光标在链上
    scr.reflow(20); // Alternate 缓冲：不豁免，链 12 格合 1 行
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s2");
    // Alternate 光标链跟踪走现状换算：链偏移 11 -> 20 列下行 0 列 11
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 11);
}

int main()
{
    testWidenTopFill();
    testTopFillPartialHistory();
    testNoCallbackPadsBottom();
    testAlternateNoTopFill();
    testTopFillPreservesWrapped();
    testTopFillCursorInsideChain();
    testPreserveCursorChainScreen();
    testPreserveSkippedOnShrinkScreen();
    testPreserveSkippedOnAlternate();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
