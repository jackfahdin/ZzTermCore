// M17d facade 级测试：扩行回填端到端、回抽折链对齐（向下取整）、
// ED 3 清滚动区、clear 序列后扩行不复活。
// 回归搭档：test_native_rowresize / test_native_reflow_topfill 等须保持绿。
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

static std::string screenRowText(const ZzTerminal& term, int row)
{
    const ZzLineView line = term.renderView().lineAt(row);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

static std::string historyLineText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

// 喂 6 行（a..f 各带 CRLF）进 10 列终端：rows=3 时历史 a..d、屏幕 e/f、
// 光标空行 2；rows=4 时历史 a/b/c、屏幕 d/e/f、光标空行 3。
static void feedSixLines(ZzTerminal& term)
{
    for (char c = 'a'; c <= 'f'; ++c) {
        std::string s;
        s += c;
        s += "\r\n";
        feedStr(term, s);
    }
}

// 1. 扩行回填端到端（贴底路径，回归）：缩后扩回历史注入顶部
static void testGrowRefillEndToEnd()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedSixLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 4); // a,b,c,d

    const std::uint64_t g0 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(10, 5)); // 扩 2：光标贴底，回抽 c,d
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2); // 剩 a,b
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "c");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "d");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "e");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "f");
    ZZ_TEST_EXPECT(term.cursor().position.row == 4); // 2 + 2 沉底
}

// 2. 新语义区分器：光标被 CUP 抬离末行、下方全空 → 回抽（旧语义不回抽）
static void testGrowRefillCursorAboveBlankTail()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedSixLines(term);                       // 历史 a,b,c；屏幕 d,e,f
    feedStr(term, "\x1b[3;1H");               // CUP：光标行 2（f 行），行 3 空
    ZZ_TEST_EXPECT(term.resize(10, 6));       // 扩 2：回抽 b,c 顶插
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1); // 剩 a
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "b");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "c");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "d");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "e");
    ZZ_TEST_EXPECT(screenRowText(term, 4) == "f");
    ZZ_TEST_EXPECT(term.cursor().position.row == 4); // 2 + 2
}

// 3. 折链对齐：扩 2 但历史第 2 行深处是链中段 → 向下取整只取 1 行，
//    屏幕首行不 dangling（跨缝整链留在历史）
static void testGrowRefillChainAligned()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxx"); // 15 x：行 0 十 x（wrapped）折行 1 五 x
    feedStr(term, "\r\n");
    feedStr(term, "b\r\n");
    feedStr(term, "c\r\n"); // 滚出 x 头入历史（wrapped 跨缝）
    feedStr(term, "d\r\n"); // 滚出 x 尾入历史（链在历史闭合）
    feedStr(term, "e\r\n"); // 滚出 b
    // 历史 [x*10(w), x*5, b]；屏幕 c,d,e + 光标空行 3
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);
    ZZ_TEST_EXPECT(term.resize(10, 6)); // 扩 2，对齐后只取 1（b）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2); // 整链留历史
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "xxxxxxxxxx");
    ZZ_TEST_EXPECT(historyLineText(term, 1) == "xxxxx");
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "b"); // 非对齐实现会取到 "xxxxx"
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "c");
    ZZ_TEST_EXPECT(term.cursor().position.row == 4); // 3 + 1
}

// 4. ED 3 清滚动区：历史清空、代计数递增、屏幕与光标不动；空历史不计代
static void testEd3ClearsHistory()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedSixLines(term); // 历史 a,b,c
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);

    const std::uint64_t g0 = term.historyView().generation();
    feedStr(term, "\x1b[3J");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "d"); // 屏幕不动
    ZZ_TEST_EXPECT(term.cursor().position.row == 3); // 光标不动

    const std::uint64_t g1 = term.historyView().generation();
    feedStr(term, "\x1b[3J"); // 空历史守卫：不重复计代
    ZZ_TEST_EXPECT(term.historyView().generation() == g1);
}

// 5. clear 序列（H + ED2 + ED3）后扩行不复活内容
static void testClearSequenceNoRefill()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedSixLines(term);
    feedStr(term, "\x1b[H\x1b[2J\x1b[3J"); // clear：清屏 + 清历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.resize(10, 6)); // 扩 2：光标下方全空但历史已空
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(screenRowText(term, 0).empty()); // 不复活
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
}

int main()
{
    testGrowRefillEndToEnd();
    testGrowRefillCursorAboveBlankTail();
    testGrowRefillChainAligned();
    testEd3ClearsHistory();
    testClearSequenceNoRefill();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
