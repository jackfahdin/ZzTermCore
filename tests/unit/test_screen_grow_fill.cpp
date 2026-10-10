// ZzScreen 扩行填满测试（M17e）：既成空洞纳入回填预算——丢弃光标下方
// b 行既成空行，回抽请求量从 k 改为 k+b；历史不足时净效果与 M17d 等价。
// 回归搭档：test_screen_grow_refill.cpp / test_screen_rowresize.cpp 须保持绿。
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

// 1. 核心新语义：历史充足（p = k+b）→ 屏幕填满、底部零空行、光标沉底
static void testGrowFillBlankTailFull()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    writeRow(scr, 1, "c1");                  // 行 2/3 空（b=2）
    scr.setCursorPosition(ZzPosition{1, 0});
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        ZZ_TEST_EXPECT(maxLines == 4); // M17e：k(2) + b(2)
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(10, "h0"));
        pulled.push_back(makeTextLine(10, "h1"));
        pulled.push_back(makeTextLine(10, "h2"));
        pulled.push_back(makeTextLine(10, "h3"));
        return pulled;
    });
    scr.resize(10, 6); // 丢 2 空行、回抽 4 → 恰好填满
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "h1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "h2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "h3");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(4), 2) == "c0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(5), 2) == "c1"); // 底部零空行
    ZZ_TEST_EXPECT(scr.cursor().position.row == 5);    // 1 + 4 沉底
}

// 2. 历史部分充足（k < H < k+b）→ 严格改进账：底部空 k+b-H 行 < M17d 的 b 行
static void testGrowFillPartialHistory()
{
    ZzScreen scr(10, 5);
    writeRow(scr, 0, "c0");
    writeRow(scr, 1, "c1");                  // 行 2/3/4 空（b=3）
    scr.setCursorPosition(ZzPosition{1, 0});
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        ZZ_TEST_EXPECT(maxLines == 6); // k(3) + b(3)
        std::vector<ZzLine> pulled;
        for (int i = 0; i < 4; ++i)    // H=4：k < H < k+b
            pulled.push_back(makeTextLine(10, "h" + std::to_string(i)));
        return pulled;
    });
    scr.resize(10, 8);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "h3");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(4), 2) == "c0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(5), 2) == "c1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(6), 2) == "  "); // 底部空 2 = k+b-H
    ZZ_TEST_EXPECT(rowText(scr.lineAt(7), 2) == "  "); //（M17d 会空 b=3 行）
    ZZ_TEST_EXPECT(scr.cursor().position.row == 5);    // 1 + 4
}

// 3. 历史为空 → 丢弃+补空净效果与 M17d 等价（退化保护，clear 后形态）
static void testGrowFillEmptyHistoryEquivalent()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    scr.setCursorPosition(ZzPosition{0, 0}); // 下方 b=3 空
    bool called = false;
    scr.setHistoryPullCallback([&](std::size_t maxLines) {
        called = true;
        ZZ_TEST_EXPECT(maxLines == 6); // k(3) + b(3)
        return std::vector<ZzLine>{};
    });
    scr.resize(10, 7);
    ZZ_TEST_EXPECT(called);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "c0"); // 内容位置不变
    ZZ_TEST_EXPECT(rowText(scr.lineAt(6), 2) == "  "); // 底部空（同 M17d）
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0);
}

// 4. 空续行（wrapped=1 且 isEmpty）随空洞丢弃后，上方链头内容完整、
//    旗标无残留（规格 §3.4 链安全）
static void testGrowFillBlankWrappedContinuation()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    scr.setLineWrapped(1, true); // 行 1：空续行，属行 0 链尾
    scr.setCursorPosition(ZzPosition{0, 0}); // 下方 b=3 全空
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        ZZ_TEST_EXPECT(maxLines == 5); // k(2) + b(3)
        std::vector<ZzLine> pulled;
        for (int i = 0; i < 5; ++i)
            pulled.push_back(makeTextLine(10, "h" + std::to_string(i)));
        return pulled;
    });
    scr.resize(10, 6);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(5), 2) == "c0"); // 链头沉底、内容完整
    ZZ_TEST_EXPECT(!scr.lineAt(5).wrapped());          // 旗标无残留
    ZZ_TEST_EXPECT(scr.cursor().position.row == 5);    // 0 + 5
}

int main()
{
    testGrowFillBlankTailFull();
    testGrowFillPartialHistory();
    testGrowFillEmptyHistoryEquivalent();
    testGrowFillBlankWrappedContinuation();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
