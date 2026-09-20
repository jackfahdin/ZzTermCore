// ZzScreen::reflow 网格原语测试（M4）。
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

// 1. 变窄重切 + 溢出行经 ScrollOutCallback 入历史（Primary）
static void testNarrowOverflowToCallback()
{
    ZzScreen scr(4, 2);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    writeRow(scr, 0, "abcd");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "efgh");
    scr.setCursorPosition(ZzPosition{1, 2});

    scr.reflow(2); // 4 列 -> 2 列：链 abcd/efgh -> ab/cd/ef/gh 共 4 行，溢出 2 行
    ZZ_TEST_EXPECT(spilled.size() == 2);
    ZZ_TEST_EXPECT(rowText(spilled[0], 2) == "ab");
    ZZ_TEST_EXPECT(rowText(spilled[1], 2) == "cd");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "ef");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "gh");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
    // 光标原 (1,2)：链偏移 4+2=6 -> 2 列下行 3 列 0，溢出 2 行后行 1 列 0
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 0);
}

// 2. 变宽合并 + 底部补空行
static void testWidenPadsBottom()
{
    ZzScreen scr(2, 4);
    writeRow(scr, 0, "ab");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "cd");
    writeRow(scr, 2, "xy");
    scr.reflow(4); // 链 ab/cd 合并为 abcd 一行
    ZZ_TEST_EXPECT(scr.size().rows == 4);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 4) == "abcd");
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 4) == "xy  ");
    ZZ_TEST_EXPECT(scr.lineAt(2).cellCount() == 4); // 补的空行
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 4) == "    ");
}

// 3. 备用屏重组但溢出不入历史
static void testAlternateReflowNoHistory()
{
    ZzScreen scr(4, 2);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    writeRow(scr, 0, "abcd");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "efgh");
    scr.reflow(2);
    ZZ_TEST_EXPECT(spilled.empty()); // 备用屏溢出丢弃，不进历史
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "ef");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "gh");
}

// 4. wrapPending 清除 + 滚动区复位 + tab stops 重建 + 全屏标脏
static void testSideEffects()
{
    ZzScreen scr(8, 3);
    scr.setWrapPending(true);
    scr.setScrollRegion(1, 2);
    scr.setTabStop(4);
    const std::uint64_t genBefore = scr.dirtyGeneration();
    scr.clearDirty();
    scr.reflow(4);
    ZZ_TEST_EXPECT(!scr.wrapPending());
    ZZ_TEST_EXPECT(scr.scrollRegionRows().startCol == 0);
    ZZ_TEST_EXPECT(scr.scrollRegionRows().endCol == 3); // 语义为 bottom+1（rows=3 复位全屏）
    ZZ_TEST_EXPECT(scr.nextTabStop(0) == 3); // tab stops 按新列宽重建为空，nextTabStop 返回最后一列
    ZZ_TEST_EXPECT(scr.dirtyGeneration() > genBefore);
    ZZ_TEST_EXPECT(scr.rowDirty(0) && scr.rowDirty(1) && scr.rowDirty(2));
}

// 5. 列宽未变与非法参数为空操作
static void testNoOp()
{
    ZzScreen scr(4, 2);
    writeRow(scr, 0, "abcd");
    scr.reflow(4); // 同宽：不动
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 4) == "abcd");
    scr.reflow(0); // 非法：不崩
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 4) == "abcd");
}

int main()
{
    testNarrowOverflowToCallback();
    testWidenPadsBottom();
    testAlternateReflowNoHistory();
    testSideEffects();
    testNoOp();
    if (g_failures == 0)
        std::printf("test_screen_reflow: all passed\n");
    return g_failures;
}
