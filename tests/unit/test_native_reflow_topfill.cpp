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
// M17a 语义变更：光标 (2,5) 在折链上 → 扩列豁免收链（规格
// 2026-10-08-m17a §3）。归还的链头 a(w) 使豁免链贡献 4 行 > rows=3，
// 溢出裁顶又压回历史——净效果为布局整体冻结：历史/屏幕/光标逐点不变，
// 不产生缺口、不顶补 h0。旧断言（收链成 abc 30 格 + 顶补 h0）按设计失效；
// 光标在链外的归还×顶补路径由用例 1 testWidenTopFillFacade 钉住。
static void testSeamChainTopFillCombo()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedStr(term, "h0\r\n");
    feedStr(term, std::string(10, 'a') + std::string(10, 'b')
                  + std::string(10, 'c') + std::string(5, 'd')); // 35 格链不换行
    // 跨缝初态：history=[h0, a(w)]，screen=[b(w), c(w), ddddd]，光标 (2,5)
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);
    ZZ_TEST_EXPECT(term.historyView().lineAt(1).wrapped());

    ZZ_TEST_EXPECT(term.resize(30, 3)); // M17a：光标在链上 → 豁免，布局冻结
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2); // 归还的 a(w) 压回历史
    ZZ_TEST_EXPECT(term.historyView().lineAt(1).wrapped());
    ZZ_TEST_EXPECT(screenRowText(term, 0) == std::string(10, 'b'));
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(screenRowText(term, 1) == std::string(10, 'c'));
    ZZ_TEST_EXPECT(term.renderView().lineAt(1).wrapped());
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "ddddd");
    ZZ_TEST_EXPECT(!term.renderView().lineAt(2).wrapped());
    ZZ_TEST_EXPECT(term.cursor().position.row == 2);
    ZZ_TEST_EXPECT(term.cursor().position.col == 5);
}

// 6. M17a 事故复刻（2026-10-08 用户实测，spike 留痕重放定位）：极窄拖拽
// 使提示符折链 -> 拉回时 readline 按旧帧发 \e[A\e[K 相对擦除。豁免后
// 擦除命中提示符碎片行，内容零损失、无空洞、光标与 bash 预期一致。
// 行数账（手工推演，与 M16c 探针同款方法）：
//   40x10 喂 L0..L11（各 \r\n）：滚动后历史 3（L0,L1,L2），
//   屏幕 [L3..L11, 提示符行]，光标 (19,9)；
//   resize(8,4)：提示符 19 格折 3 行，物理 12 行溢出 8 压历史
//   （历史 11 = L0..L10），屏幕 [L11, "prompt$ "(w), "echo abc"(w), "def"]，
//   光标 (3,3)；
//   resize(40,10)：豁免链保持 3 行，缺口 6 顶补（L5..L10），
//   屏幕 [L5..L11, 碎片×3]，光标 (3,9)，历史 5（L0..L4）；
//   重绘 "\r\e[K" + "\e[A\e[K"×2 + 重印：擦除命中 3 个碎片行，
//   提示符重印在 L11 下一行，光标 (19,7)，内容零损失。
static void testActiveChainGuardVsReadlineErase()
{
    ZzTerminal term(40, 10, ZzBackendKind::Native, 100);
    for (int i = 0; i < 12; ++i)
        feedStr(term, "L" + std::to_string(i) + "\r\n");
    feedStr(term, "prompt$ echo abcdef"); // 19 格，光标 (19,9)
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);

    ZZ_TEST_EXPECT(term.resize(8, 4)); // 极窄：提示符折 3 行
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 11);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "L11");
    // 物理内容 "prompt$ "（8 格，末格为分隔空格），screenRowText 裁行尾空格后为 "prompt$"
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "prompt$");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "echo abc");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "def");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.cursor().position.col == 3);

    ZZ_TEST_EXPECT(term.resize(40, 10)); // 拉回：豁免收链
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5); // 顶补 6（L5..L10）

    // readline WINCH 重绘（陈旧帧高 3）：擦上行×2 + 重印。
    feedStr(term, "\r\033[K\033[A\033[K\033[A\033[K");
    feedStr(term, "prompt$ echo abcdef");
    ZZ_TEST_EXPECT(screenRowText(term, 6) == "L11"); // 修复前此处 L11 已被误擦
    ZZ_TEST_EXPECT(screenRowText(term, 7) == "prompt$ echo abcdef");
    ZZ_TEST_EXPECT(screenRowText(term, 8).empty()); // 被擦的是碎片行
    ZZ_TEST_EXPECT(screenRowText(term, 9).empty());
    ZZ_TEST_EXPECT(term.cursor().position.row == 7);
    ZZ_TEST_EXPECT(term.cursor().position.col == 19);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5); // L0..L4 完好留存
}

int main()
{
    testWidenTopFillFacade();
    testCombinedResizeTopFill();
    testNarrowWideRoundtrip();
    testPrimaryTopFillDuringAlternate();
    testSeamChainTopFillCombo();
    testActiveChainGuardVsReadlineErase();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
