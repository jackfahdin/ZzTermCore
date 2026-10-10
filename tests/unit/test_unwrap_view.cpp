// ZzUnwrapView 拼接行视图测试（M17b）：拼接正确性、裁尾口径、跨缝链、
// EAW 宽字符、坐标映射往返、maxCellCount、Alternate、resize 稳定。
// 规格 docs/superpowers/specs/2026-10-10-m17b-unwrap-view-design.md。
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#include "ZzTerm/Terminal.h"
#include "ZzTerm/UnwrapView.h"

static int g_failures = 0;
// variadic：坐标断言含花括号初始化（ZzStitchedPos{0, 3}）的逗号，
// 单参数宏无法承载；#__VA_ARGS__ 保持原样字符串化。
#define ZZ_TEST_EXPECT(...)                                                   \
    do {                                                                      \
        if (!(__VA_ARGS__)) {                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void feedStr(ZzTerminal& term, const std::string& s)
{
    term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size()));
}

// 拼接行全文（cellAt 逐格拼接；续格 text 为空自然跳过）
static std::string stitchedText(const ZzUnwrapView& view, std::size_t index)
{
    const ZzStitchedLineView line = view.lineAt(index);
    std::string out;
    for (int c = 0; c < line.cellCount(); ++c)
        out += line.cellAt(c).text;
    return out;
}

// 1. 无折链：拼接行 = 物理行（空白行裁尾为 0 格但占位）
static void testNoWrapIdentity()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "a\r\nb\r\n"); // 屏幕 a,b,空,空；光标行 2
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(view.lineCount() == 4);
    ZZ_TEST_EXPECT(stitchedText(view, 0) == "a");
    ZZ_TEST_EXPECT(stitchedText(view, 1) == "b");
    ZZ_TEST_EXPECT(view.lineAt(2).cellCount() == 0); // 空拼接行裁尾为 0
    ZZ_TEST_EXPECT(view.lineAt(2).sourceLineCount() == 1);
    ZZ_TEST_EXPECT(view.maxCellCount() == 1);
}

// 2. 多行折链拼接：25 x 在 10 列下折 3 行 → 1 条拼接行
static void testStitchChain()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxxxxxxxxxxxx"); // 25 x：行0/1 满(w)，行2 五 x
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(view.lineCount() == 2); // 链 + 空行 3
    const ZzStitchedLineView line = view.lineAt(0);
    ZZ_TEST_EXPECT(line.cellCount() == 25);
    ZZ_TEST_EXPECT(line.sourceLine() == 0);
    ZZ_TEST_EXPECT(line.sourceLineCount() == 3);
    ZZ_TEST_EXPECT(stitchedText(view, 0) == std::string(25, 'x'));
    ZZ_TEST_EXPECT(view.maxCellCount() == 25);
    feedStr(term, "\r\nb"); // 另起短行：多链混合 maxCellCount 仍为链长
    ZZ_TEST_EXPECT(view.lineCount() == 2); // [x 链, b]
    ZZ_TEST_EXPECT(view.maxCellCount() == 25);
}

// 3. 跨历史-屏幕缝的链照常拼接，sourceLine 指历史区链头
static void testStitchAcrossHistorySeam()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxx"); // 15 x：行0 十 x(w)，行1 五 x
    feedStr(term, "\r\n");
    feedStr(term, "b\r\n"); // 滚出 x 头入历史（跨缝 wrapped）
    // 统一空间：历史 [x*10(w)]；屏幕 [x*5, b, 空]
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(view.lineCount() == 3); // [x 链, b, 空]
    ZZ_TEST_EXPECT(stitchedText(view, 0) == std::string(15, 'x'));
    ZZ_TEST_EXPECT(view.lineAt(0).sourceLine() == 0);       // 链头在历史
    ZZ_TEST_EXPECT(view.lineAt(0).sourceLineCount() == 2);
    ZZ_TEST_EXPECT(stitchedText(view, 1) == "b");
}

// 4. EAW 宽字符跨缝：行尾放不下的宽字符整体折下行，拼接后两格完整
static void testStitchWideCharSeam()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "123456789\xE4\xB8\xAD"); // 9 数字 + 「中」：中折到行 1
    const ZzUnwrapView& view = term.unwrapView();
    const ZzStitchedLineView line = view.lineAt(0);
    // 行 0 裁尾去掉填充格（9 格有效），行 1「中」2 格 → 共 11
    ZZ_TEST_EXPECT(line.cellCount() == 11);
    ZZ_TEST_EXPECT(line.cellAt(9).text == "\xE4\xB8\xAD"); // 跨缝寻址
    ZZ_TEST_EXPECT(line.cellAt(10).text.empty());          // 宽字符续格
    ZZ_TEST_EXPECT(stitchedText(view, 0) == "123456789\xE4\xB8\xAD");
}

// 5. 坐标映射：toStitched/fromStitched 逐点取值 + 往返一致 + 钳位
static void testCoordinateMapping()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxxxxxxxxxxxx"); // 链：行0/1/2
    feedStr(term, "\r\nb");                     // 行 3：b
    const ZzUnwrapView& view = term.unwrapView();
    // toStitched：物理 → 拼接
    ZZ_TEST_EXPECT(view.toStitched(ZzLogicalPos{0, 3}) == ZzStitchedPos{0, 3});
    ZZ_TEST_EXPECT(view.toStitched(ZzLogicalPos{1, 4}) == ZzStitchedPos{0, 14});
    ZZ_TEST_EXPECT(view.toStitched(ZzLogicalPos{2, 4}) == ZzStitchedPos{0, 24});
    ZZ_TEST_EXPECT(view.toStitched(ZzLogicalPos{3, 0}) == ZzStitchedPos{1, 0});
    // fromStitched：拼接 → 物理
    ZZ_TEST_EXPECT(view.fromStitched(0, 14) == ZzLogicalPos{1, 4});
    ZZ_TEST_EXPECT(view.fromStitched(0, 24) == ZzLogicalPos{2, 4});
    ZZ_TEST_EXPECT(view.fromStitched(1, 0) == ZzLogicalPos{3, 0});
    // 钳位：col 超出行尾 → 链末行最后有效格
    ZZ_TEST_EXPECT(view.fromStitched(0, 100) == ZzLogicalPos{2, 4});
    // 往返一致
    ZZ_TEST_EXPECT(view.fromStitched(view.toStitched(ZzLogicalPos{1, 4}).line,
                                     view.toStitched(ZzLogicalPos{1, 4}).col)
                   == ZzLogicalPos{1, 4});
    // 反向往返：toStitched∘fromStitched = id
    ZZ_TEST_EXPECT(view.toStitched(view.fromStitched(0, 14)) == ZzStitchedPos{0, 14});
}

// 5b. feed 驱动索引失效重建：首查 sourceLine 直接命中（重建路径），
// 再喂折链后重查须反映最新链合并
static void testFeedInvalidation()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "a\r\n"); // 行0: a；光标行 1
    const ZzUnwrapView& view = term.unwrapView();
    // 全新视图首查即 sourceLine（索引未建，须经 ensureFresh 重建）
    ZZ_TEST_EXPECT(view.lineAt(0).sourceLine() == 0);
    ZZ_TEST_EXPECT(view.lineCount() == 4); // [a, 空, 空, 空]
    feedStr(term, "xxxxxxxxxxxxxxxxxxxxxxxxx"); // 25 x：行1/2 满(w)，行3 五 x
    // 双代计数已变，重查触发重建：[a, x 链(3 物理行合并)] = 2 拼接行
    ZZ_TEST_EXPECT(view.lineCount() == 2);
    ZZ_TEST_EXPECT(stitchedText(view, 0) == "a");
    ZZ_TEST_EXPECT(stitchedText(view, 1) == std::string(25, 'x'));
    ZZ_TEST_EXPECT(view.lineAt(1).sourceLine() == 1);
    ZZ_TEST_EXPECT(view.lineAt(1).sourceLineCount() == 3);
}

// 6. 空缓冲：maxCellCount=0，拼接行 = 物理空行
static void testEmptyBuffer()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(view.lineCount() == 4);
    ZZ_TEST_EXPECT(view.maxCellCount() == 0);
    ZZ_TEST_EXPECT(view.lineAt(0).cellCount() == 0);
}

// 7. Alternate 屏：链在屏幕内照常拼接，无历史
static void testAlternateScreen()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b[?1049h");             // 进 Alternate
    feedStr(term, "xxxxxxxxxxxxxxx");          // 15 x：行0(w)，行1 五 x
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(view.lineCount() == 2); // [x 链, 空]
    ZZ_TEST_EXPECT(view.lineAt(0).cellCount() == 15);
    ZZ_TEST_EXPECT(view.lineAt(0).sourceLineCount() == 2);
}

// 8. resize/reflow 后拼接行内容稳定（M16 联动回归）
static void testStitchStableAcrossResize()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxxxxxxxxxxxx"); // 25 x 链
    ZZ_TEST_EXPECT(stitchedText(term.unwrapView(), 0) == std::string(25, 'x'));
    term.resize(16, 4); // 扩列：16+9 两行链
    ZZ_TEST_EXPECT(stitchedText(term.unwrapView(), 0) == std::string(25, 'x'));
    term.resize(6, 4);  // 缩列：链跨历史-屏幕缝（6*5=5 行 > 4 行屏）
    ZZ_TEST_EXPECT(stitchedText(term.unwrapView(), 0) == std::string(25, 'x'));
    term.resize(10, 4); // 还原
    ZZ_TEST_EXPECT(stitchedText(term.unwrapView(), 0) == std::string(25, 'x'));
}

int main()
{
    testNoWrapIdentity();
    testStitchChain();
    testStitchAcrossHistorySeam();
    testStitchWideCharSeam();
    testCoordinateMapping();
    testFeedInvalidation();
    testEmptyBuffer();
    testAlternateScreen();
    testStitchStableAcrossResize();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
