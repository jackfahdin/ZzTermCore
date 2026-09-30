// M15：native 行变 resize 的 facade 级行为——缩行压历史/扩行回抽还原、
// 裁光标下方不动历史、非末行不回抽、Alternate 期间主屏按缓冲区分、
// M14 historyView 契约联动（lineCount/generation）。
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

// 喂 8 行（a..h 各带 CRLF）进 10x4 终端：滚出 a..e 共 5 行进历史，
// 屏幕 f/g/h（行 0..2），光标行 3（空行）。
static void feedEightLines(ZzTerminal& term)
{
    for (char c = 'a'; c <= 'h'; ++c) {
        std::string s;
        s += c;
        s += "\r\n";
        feedStr(term, s);
    }
}

// 1. 缩行压历史 + 扩行回抽还原（「resize 截断拉大不恢复」根治钉住）
static void testShrinkPushAndGrowPull()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);

    ZZ_TEST_EXPECT(term.resize(10, 3)); // 缩 1：光标贴底，f 压入历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 6);
    ZZ_TEST_EXPECT(historyLineText(term, 5) == "f");
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "g");
    ZZ_TEST_EXPECT(term.cursor().position.row == 2);

    const std::uint64_t g0 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(10, 4)); // 扩 1：光标贴底（行 2=末行）回抽 f
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "f");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0); // 回抽代计数递增
}

// 2. 缩行裁光标下方：历史不变
static void testShrinkCutsBelowCursorKeepsHistory()
{
    ZzTerminal term(10, 6, ZzBackendKind::Native, 100);
    feedStr(term, "top\r\n");
    feedStr(term, "mid\r\n"); // 光标行 2，下方 3 行空行
    ZZ_TEST_EXPECT(term.resize(10, 4)); // 缩 2：全部从光标下方裁
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "mid");
    ZZ_TEST_EXPECT(term.cursor().position.row == 2);
}

// 3. 光标不在末行时扩行不回抽（底部补空）
static void testGrowNoPullWhenCursorNotLast()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    feedStr(term, "\x1b[1;1H"); // CUP 回行 0（不在末行）
    ZZ_TEST_EXPECT(term.resize(10, 6)); // 扩 2：不回抽
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "f");
}

// 4. Alternate 期间 resize：主屏按缓冲区分照样压历史（规格 §3）
static void testAlternateResizePrimaryStillPushes()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term); // 主屏历史 5（a..e），屏幕 f/g/h，光标行 3
    feedStr(term, "\x1b[?1049h");
    feedStr(term, "alt\r\n");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // Alternate 恒 0（M14 契约）
    ZZ_TEST_EXPECT(term.resize(10, 2)); // alt 期间缩 2：主屏网格 pushUp f/g
    feedStr(term, "\x1b[?1049l");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 7); // 5 + f/g 两行
    ZZ_TEST_EXPECT(historyLineText(term, 6) == "g");
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "h"); // 主屏余 h 与光标行
}

int main()
{
    testShrinkPushAndGrowPull();
    testShrinkCutsBelowCursorKeepsHistory();
    testGrowNoPullWhenCursorNotLast();
    testAlternateResizePrimaryStillPushes();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
