// ZzScreen 行变 resize 条件语义测试（M15）：缩行裁光标下方 / pushUp 压历史、
// 扩行回抽 / 非末行补空、无回调降级、Alternate 无历史路径。
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

// 1. 缩行先裁光标下方行（不入历史）
static void testShrinkCutsBelowCursor()
{
    ZzScreen scr(10, 6);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    for (int r = 0; r < 6; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{2, 1}); // 光标行 2，下方 3 行
    scr.resize(10, 4);                       // 缩 2：全部从光标下方裁（r4/r5）
    ZZ_TEST_EXPECT(spilled.empty());
    ZZ_TEST_EXPECT(scr.size().rows == 4);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "r3");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 2);
}

// 2. 缩行 pushUp：光标贴底时顶行压入历史
static void testShrinkPushUpToCallback()
{
    ZzScreen scr(10, 4);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    for (int r = 0; r < 4; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{3, 1}); // 光标贴底，下方 0 行
    scr.resize(10, 2);                       // 缩 2：pushUp 顶两行 r0/r1
    ZZ_TEST_EXPECT(spilled.size() == 2);
    ZZ_TEST_EXPECT(rowText(spilled[0], 2) == "r0");
    ZZ_TEST_EXPECT(rowText(spilled[1], 2) == "r1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "r3");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1); // 3 - 2 = 1
}

// 3. 缩行混合：先裁下方再 pushUp
static void testShrinkMixed()
{
    ZzScreen scr(10, 6);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    for (int r = 0; r < 6; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{4, 1}); // 下方 1 行
    scr.resize(10, 3);                       // 缩 3：裁下方 1（r5）+ pushUp 2（r0/r1）
    ZZ_TEST_EXPECT(spilled.size() == 2);
    ZZ_TEST_EXPECT(rowText(spilled[0], 2) == "r0");
    ZZ_TEST_EXPECT(rowText(spilled[1], 2) == "r1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "r4");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 2); // 4 - 2 = 2
}

// 4. 缩行无回调时 pushUp 丢弃（同 reflow 溢出「Primary 且回调存在才上移」语义）
static void testShrinkPushUpDroppedWithoutCallback()
{
    ZzScreen scr(10, 4); // 不装回调
    for (int r = 0; r < 4; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{3, 1});
    scr.resize(10, 2);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r2"); // 内容位移同 pushUp，行被丢弃
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
}

// 5. 扩行回抽：光标贴底 + pull 回调
static void testGrowPullsBack()
{
    ZzScreen scr(10, 2);
    writeRow(scr, 0, "r2");
    writeRow(scr, 1, "r3");
    scr.setCursorPosition(ZzPosition{1, 0}); // 贴底
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        std::vector<ZzLine> pulled;
        ZZ_TEST_EXPECT(maxLines == 2); // 索取数 = 扩行数
        pulled.push_back(makeTextLine(10, "r0")); // 旧到新
        pulled.push_back(makeTextLine(10, "r1"));
        return pulled;
    });
    scr.resize(10, 4); // 扩 2：回抽 2 行注入顶部
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "r1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "r2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "r3");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3); // 1 + 2 = 3
}

// 6. 扩行光标不在末行：不调回调、底部补空
static void testGrowNoPullWhenCursorNotLast()
{
    ZzScreen scr(10, 3);
    for (int r = 0; r < 3; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{0, 0}); // 不在末行
    bool pullCalled = false;
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.resize(10, 5);
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(4), 2) == "  "); // 底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0);
}

// 7. 扩行回调返回空（无历史可取）：底部补空
static void testGrowEmptyPull()
{
    ZzScreen scr(10, 2);
    writeRow(scr, 0, "r0");
    writeRow(scr, 1, "r1");
    scr.setCursorPosition(ZzPosition{1, 0}); // 贴底
    scr.setHistoryPullCallback([](std::size_t) { return std::vector<ZzLine>{}; });
    scr.resize(10, 4);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "  "); // 底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
}

// 8. Alternate 缓冲：缩行尾部截断不压历史、扩行底部补空不回抽
static void testAlternateNoHistoryPath()
{
    ZzScreen scr(10, 4);
    bool scrollOutCalled = false;
    bool pullCalled = false;
    scr.setScrollOutCallback([&](std::vector<ZzLine>) { scrollOutCalled = true; });
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    for (int r = 0; r < 4; ++r)
        writeRow(scr, r, "a" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{3, 0});
    scr.resize(10, 2); // alt 缩行：尾部截断（a2/a3 丢弃），不压历史
    ZZ_TEST_EXPECT(!scrollOutCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "a0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "a1");
    scr.resize(10, 4); // alt 扩行：底部补空，不回抽
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "a1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "  ");
}

int main()
{
    testShrinkCutsBelowCursor();
    testShrinkPushUpToCallback();
    testShrinkMixed();
    testShrinkPushUpDroppedWithoutCallback();
    testGrowPullsBack();
    testGrowNoPullWhenCursorNotLast();
    testGrowEmptyPull();
    testAlternateNoHistoryPath();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
