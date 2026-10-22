// ZzScreen 扩行回填测试（M17d）：回抽条件从「光标贴末行」放宽为
//「光标下方全空行」（会话活在底部）；部分回填/布局保护。
// M17e 起请求量含既成空洞，见 test_screen_grow_fill.cpp。
// 回归搭档：test_screen_rowresize.cpp（M15 八用例，含贴底回抽/Alternate）须保持绿。
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

// 1. 核心新语义：光标不在末行、下方全空 → 回抽注入顶部
static void testGrowRefillBelowCursorBlank()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    writeRow(scr, 1, "c1");                  // 行 2/3 空
    scr.setCursorPosition(ZzPosition{1, 0}); // 光标行 1（非末行 3），下方全空
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        ZZ_TEST_EXPECT(maxLines == 4); // M17e：索取数 = 扩行数 k + 既成空洞 b
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(10, "h0"));
        pulled.push_back(makeTextLine(10, "h1"));
        return pulled; // 只返回 2 行：部分回填
    });
    scr.resize(10, 6); // 扩 2：回抽 2 行注入顶部（旧语义光标不贴底不回抽）
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "h1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "c0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "c1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(5), 2) == "  "); // 底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3);  // 1 + 2
}

// 2. 光标在空行上也可回抽 + 历史不足时部分回填、余量补空
static void testGrowRefillCursorOnBlankRowPartial()
{
    ZzScreen scr(10, 5);
    writeRow(scr, 0, "c0");
    writeRow(scr, 1, "c1");                  // 行 2/3/4 空
    scr.setCursorPosition(ZzPosition{2, 0}); // 光标在空行 2 上，下方行 3/4 空
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        ZZ_TEST_EXPECT(maxLines == 4);
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(10, "m0"));
        return pulled; // 历史只剩 1 行：部分回填
    });
    scr.resize(10, 7); // 扩 2，回抽 1
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "m0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "c0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "c1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(6), 2) == "  "); // 余量底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3);  // 2 + 1
}

// 3. 布局保护：光标下方有非空行 → 不回抽（新旧语义一致，钉住）
static void testGrowNoRefillWhenContentBelow()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    writeRow(scr, 2, "c2");                  // 行 1/3 空，行 2 有内容
    scr.setCursorPosition(ZzPosition{0, 0}); // 下方行 2 非空 → 不回抽
    bool pullCalled = false;
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.resize(10, 6);
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "c0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "c2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(5), 2) == "  "); // 底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0);
}

// 4. 默认样式空格行 = 空行（2026-10-22 定因：readline 以字面空格清行，
// 旧判定把清行空格当内容否决回抽、提示符永久悬空）：光标下方整行默认
// 样式 U+0020 → 仍触发回抽。
static void testGrowRefillWhenPlainSpacesBelow()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    writeRow(scr, 2, "    ");                // 行 2 整行默认样式空格（清行残留）
    scr.setCursorPosition(ZzPosition{1, 0}); // 下方行 2 空格、行 3 空
    bool pullCalled = false;
    std::size_t asked = 0;
    scr.setHistoryPullCallback([&](std::size_t n) {
        pullCalled = true;
        asked = n;
        std::vector<ZzLine> out;
        out.push_back(makeTextLine(10, "m0"));
        out.push_back(makeTextLine(10, "m1"));
        return out;
    });
    scr.resize(10, 6); // 扩 2：空格行计入既成空洞（k→k+b）
    ZZ_TEST_EXPECT(pullCalled);
    ZZ_TEST_EXPECT(asked == 4); // k(2) + b(2)
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1 + 2);
}

// 5. 布局保护收紧后的边界：带颜色的空格行仍是内容 → 不回抽（钉住）。
static void testGrowNoRefillWhenStyledSpacesBelow()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    for (int i = 0; i < 4; ++i) { // 行 2 整行红底空格（色块 = 布局内容）
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint(U' ');
        c.setBackground(ZzColor::Indexed(1));
        scr.putCell(ZzPosition{2, i}, c);
    }
    scr.setCursorPosition(ZzPosition{0, 0});
    bool pullCalled = false;
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.resize(10, 6);
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0);
}

int main()
{
    testGrowRefillBelowCursorBlank();
    testGrowRefillCursorOnBlankRowPartial();
    testGrowNoRefillWhenContentBelow();
    testGrowRefillWhenPlainSpacesBelow();
    testGrowNoRefillWhenStyledSpacesBelow();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
