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

// M17c 事故复刻：readline 两代重绘死链不粘——erase 斩链后残片收链、
// 活代被 bash 帧擦除精确命中，内容零损失。
// 行数账（20x6）：
//   A) "C0\r\nC1\r\nC2\r\n" + 45 字符提示符 → r0-2=C0..C2，
//      链 [r3(w),r4(w),r5]（20+20+5），光标 (5,5)；
//   B) bash 二代重绘：\r\e[K\r + 同 45 字符 → \e[K 斩 r4.wrapped（M17c），
//      写入折行触发 2 次滚动（C0、C1 入历史），屏：
//      r0=C2, r1=gen1r0(w), r2=gen1r1(斩尾), r3=gen2r0(w), r4=gen2r1(w),
//      r5=gen2r2，光标 (5,5)；历史 [C0,C1]；
//   C) resize(40,6)：gen1 [r1,r2] 收链为 40 宽 1 行；gen2 是光标链，
//      M17a 豁免保持 20 宽 3 行；产出 5 行缺 1 → 顶补 C1，历史 [C0]；
//   D) bash 拉回重绘：\r\e[K + \e[A\e[K×2（帧=3 行，精确命中 gen2 三行）
//      + 45 字符按 40 宽重印 → r3(40 字符) + r4("PROMP")，光标 (5,4)。
//   无 M17c 时：B 不斩链 → gen1/gen2 粘成 5 行僵尸链 → C 整链豁免 →
//   D 只擦 3 行 → r1/r2 残留 20 列碎片（本测试的否定断言点）。
static void testEraseSeverVsZombieChain()
{
    ZzTerminal term(20, 6, ZzBackendKind::Native, 100);
    const std::string prompt =
        "0123456789abcdefghij0123456789abcdefghijPROMP"; // 45 字符
    feedStr(term, "C0\r\nC1\r\nC2\r\n");
    feedStr(term, prompt);                       // 链 r3-r5，光标 (5,5)
    feedStr(term, "\r\x1b[K\r");                 // bash 二代重绘：整行擦 r5（斩链点）
    feedStr(term, prompt);                       // 重写 → 2 次滚动，C0/C1 入历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);
    ZZ_TEST_EXPECT(term.resize(40, 6));          // 拉回（M17a 豁免活代）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1); // 顶补取走 C1
    feedStr(term, "\r\x1b[K\x1b[A\x1b[K\x1b[A\x1b[K"); // bash 帧擦除 3 行
    feedStr(term, prompt);                       // 40 宽重印
    // 内容零损失 + 无 20 列碎片：r2 是收链后的 40 列残骸（方案 A 形态）
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "C1");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "C2");
    ZZ_TEST_EXPECT(screenRowText(term, 2) ==
                   "0123456789abcdefghij0123456789abcdefghij"); // gen1 收链残骸
    ZZ_TEST_EXPECT(screenRowText(term, 3) ==
                   "0123456789abcdefghij0123456789abcdefghij"); // 活提示符
    ZZ_TEST_EXPECT(screenRowText(term, 4) == "PROMP");
    ZZ_TEST_EXPECT(screenRowText(term, 5).empty());
    ZZ_TEST_EXPECT(term.cursor().position.row == 4);
    ZZ_TEST_EXPECT(term.cursor().position.col == 5);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1); // C0 完好
}

// M17c 跨界斩链接线实测（Terminal 级：Screen 回调 → backend →
// scrollback_->severNewestWrapped 全链路）。
// 行数账（20x6）：feed "L0\r\n"×6 → 历史 [L0]，光标 (0,5)；
// 再连续写 130 个 'a'（无 \r\n）：每 20 字符折行触发 1 次滚屏，共 6 次——
// L1..L5 依次入历史，第 6 次滚出的是折行首段 seg0（wrapped=true），
// 历史 [L0..L5, seg0(w)] 共 7 行，seg0 续接屏幕 r0（seg1）构成接缝链；
// 屏幕 r0-r4=seg1..seg5(w)，r5=seg6（10 字符），光标 (10,5)。
// \e[H 光标回 row 0 → \e[K 整行擦除 r0 → 跨界斩：历史末行 seg0 链标死。
static void testEraseSeverAcrossSeam()
{
    ZzTerminal term(20, 6, ZzBackendKind::Native, 100);
    feedStr(term, "L0\r\nL1\r\nL2\r\nL3\r\nL4\r\nL5\r\n");
    feedStr(term, std::string(130, 'a'));
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 7);
    ZZ_TEST_EXPECT(term.historyView().lineAt(6).wrapped()); // 接缝链前提成立
    const auto g0 = term.historyView().generation();
    feedStr(term, "\x1b[H");   // CUP：光标到 (0,0)
    feedStr(term, "\x1b[K");   // EL 列 0 整行擦除 r0 → 跨界斩链
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());  // r0 出链斩
    ZZ_TEST_EXPECT(!term.historyView().lineAt(6).wrapped()); // 历史末行入链斩
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 7);     // 行数不动
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);    // 旗标变化计代
}

// M17c 终审 F3：接缝回调代计数守卫——历史末行本就无链标时整行擦屏幕
// 首行触发回调，但 severNewestWrapped 是空操作，不得计代（HistoryView 不
// 无谓失效）。
// 行数账（20x6）：feed "L0\r\n"×7 + "L7" → 历史 [L0,L1]（L1
// wrapped=false），屏幕 r0=L2..r5=L7，光标 (2,5)；\e[H 光标回 (0,0)；
// \e[K 整行擦 r0 → 接缝回调触发，但历史末行无链标 → 空操作不计代。
static void testEraseSeverSeamNoLinkNoGeneration()
{
    ZzTerminal term(20, 6, ZzBackendKind::Native, 100);
    for (int i = 0; i < 7; ++i)
        feedStr(term, "L" + std::to_string(i) + "\r\n");
    feedStr(term, "L7");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);
    ZZ_TEST_EXPECT(!term.historyView().lineAt(1).wrapped()); // 前提：无链标
    const std::uint64_t g0 = term.historyView().generation();
    feedStr(term, "\x1b[H");   // CUP：光标到 (0,0)
    feedStr(term, "\x1b[K");   // EL 列 0 整行擦除 r0 → 接缝回调触发
    ZZ_TEST_EXPECT(!term.historyView().lineAt(1).wrapped()); // 不变
    ZZ_TEST_EXPECT(term.historyView().generation() == g0);   // 无链标不计代
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);
}

int main()
{
    testWidenTopFillFacade();
    testCombinedResizeTopFill();
    testNarrowWideRoundtrip();
    testPrimaryTopFillDuringAlternate();
    testSeamChainTopFillCombo();
    testActiveChainGuardVsReadlineErase();
    testEraseSeverVsZombieChain();
    testEraseSeverAcrossSeam();
    testEraseSeverSeamNoLinkNoGeneration();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
