// M16c facade 级顶补钉住：纯列变拉宽顶补（探针场景 A）、列行同变事故复刻
// （探针场景 B）、窄宽往返布局守恒、Alternate 期间主屏顶补按缓冲区分、
// 接缝归还 × 顶补同路径组合（规格 §5 测试项 6，终审 I-1 补测）。
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
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// 场景 A 初态构造（10x4）：6 条 20 格长行 + s1/s2，历史 10 行，
// 屏幕 [L5a(w), L5b, s1, s2]，光标 (2,3)。
static void feedScenarioA(ZzTerminal& term)
{
    for (int i = 0; i < 6; ++i) {
        std::string s = "L" + std::to_string(i) + std::string(18, char('a' + i));
        feedStr(term, s + "\r\n");
    }
    feedStr(term, "s1\r\ns2");
}

// 1. 纯列变拉宽顶补（探针场景 A，对齐 contour 实测布局）
static void testWidenTopFillFacade()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedScenarioA(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 10);

    const std::uint64_t g0 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(20, 4)); // 链 2->1 接回，缺口 1 顶补
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 4); // 5 链合 5 行再顶补 1
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "L4eeeeeeeeeeeeeeeeee");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "L5ffffffffffffffffff");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "s1");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "s2");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3); // 贴底（contour 同款）
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
}

// 2. 列行同变事故复刻（探针场景 B：2026-10-08 用户实测「最大化后仅剩
// 提示符、历史滞留」的 Core 级形态，M16c 后对齐 contour 抽干历史）
static void testCombinedResizeTopFill()
{
    ZzTerminal term(30, 10, ZzBackendKind::Native, 100);
    feedStr(term, "short1\r\nshort2\r\n");
    feedStr(term, std::string(45, 'w') + "\r\n"); // 45 格长行 30 列折 2 行
    feedStr(term, "tail\r\nprompt$ ");

    ZZ_TEST_EXPECT(term.resize(15, 5)); // 缩：溢出 2 行压历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);

    ZZ_TEST_EXPECT(term.resize(60, 20)); // 列行同增（事故手势）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // 顶补抽干，无滞留
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "short1");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "short2");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == std::string(45, 'w'));
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "tail");
    ZZ_TEST_EXPECT(screenRowText(term, 4) == "prompt$");
    ZZ_TEST_EXPECT(screenRowText(term, 5).empty()); // 余量底部补空
    ZZ_TEST_EXPECT(term.cursor().position.row == 4);
    ZZ_TEST_EXPECT(term.cursor().position.col == 8);
}

// 3. 窄宽往返布局守恒：满屏初态缩列再拉回，屏幕与历史精确还原
static void testNarrowWideRoundtrip()
{
    ZzTerminal term(20, 4, ZzBackendKind::Native, 100);
    feedScenarioA(term); // 20 列下每链 1 行：历史 [L0..L3]，屏幕 [L4,L5,s1,s2]
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 4);

    ZZ_TEST_EXPECT(term.resize(10, 4)); // 缩列：链重切，溢出 2 行压历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 10);
    ZZ_TEST_EXPECT(term.resize(20, 4)); // 拉回：顶补还原
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 4);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "L4eeeeeeeeeeeeeeeeee");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "L5ffffffffffffffffff");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "s1");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "s2");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
}

// 4. Alternate 期间主屏顶补按缓冲区分（与激活态无关），备用屏自身不顶补
static void testPrimaryTopFillDuringAlternate()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "f0\r\nf1\r\n");
    feedStr(term, std::string(20, 'c')); // 链占行 2/3（wrap-pending）
    feedStr(term, "\r\n");               // 滚出 f0 入历史
    feedStr(term, "x");                  // 屏幕 [f1, cA(w), cB, x]
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1);

    feedStr(term, "\x1b[?1049h"); // 进备用屏
    ZZ_TEST_EXPECT(term.isAlternateScreen());
    ZZ_TEST_EXPECT(term.resize(20, 4)); // 主屏重组收缩，顶补 f0（历史清空）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);

    feedStr(term, "\x1b[?1049l"); // 回主屏验证顶补结果
    ZZ_TEST_EXPECT(!term.isAlternateScreen());
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "f0");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "f1");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == std::string(20, 'c'));
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "x");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.cursor().position.col == 1);
}

// 5. 接缝归还 × 顶补同路径组合（规格 §5 测试项 6，终审 I-1 补测）：
// resize 时点存在跨缝链（历史末行 wrapped=true 续接屏幕首链），单次拉宽
// 先经 M16b 归还接回链，重组收缩产生缺口再顶补历史。
static void testSeamChainTopFillCombo()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedStr(term, "h0\r\n");
    feedStr(term, std::string(10, 'a') + std::string(10, 'b')
                  + std::string(10, 'c') + std::string(5, 'd')); // 35 格链不换行
    // 跨缝初态：history=[h0, a(w)]，screen=[b(w), c(w), ddddd]，光标 (5,2)
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);
    ZZ_TEST_EXPECT(term.historyView().lineAt(1).wrapped());

    ZZ_TEST_EXPECT(term.resize(30, 3)); // 归还 a(w) 接回，链 35 格折 2 行，缺口 1 顶补 h0
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "h0");
    ZZ_TEST_EXPECT(screenRowText(term, 1)
                   == std::string(10, 'a') + std::string(10, 'b') + std::string(10, 'c'));
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "ddddd");
    ZZ_TEST_EXPECT(term.renderView().lineAt(1).wrapped());
    ZZ_TEST_EXPECT(!term.renderView().lineAt(2).wrapped());
    ZZ_TEST_EXPECT(term.cursor().position.row == 2);
    ZZ_TEST_EXPECT(term.cursor().position.col == 5);
}

int main()
{
    testWidenTopFillFacade();
    testCombinedResizeTopFill();
    testNarrowWideRoundtrip();
    testPrimaryTopFillDuringAlternate();
    testSeamChainTopFillCombo();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
