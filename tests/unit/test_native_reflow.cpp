// native 后端 resize reflow 端到端测试：ZzTerminal facade -> feed -> resize -> 断言（M4）。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void feed(ZzTerminal& term, const std::string& bytes)
{
    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                         bytes.size()));
}

// 1. 历史中的长行随变宽合并、变窄重切
static void testHistoryReflow()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    // 两条长行 + 30 行 filler 顶入历史：
    // A：20 字符（19 个 a + x），10 列折 2 行；
    // B：30 字符（29 个 b + y），10 列折 3 行。
    feed(term, "aaaaaaaaaaaaaaaaaaax\r\n");
    feed(term, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbby\r\n");
    for (int i = 0; i < 30; ++i)
        feed(term, "filler\r\n");
    ZZ_TEST_EXPECT(term.scrollback().lineCount() >= 5);

    term.resize(20, 3); // 变宽：A 的 2 行链合并为 1 行
    bool foundMerged = false;
    for (std::size_t i = 0; i < term.scrollback().lineCount(); ++i) {
        const ZzLine& l = term.scrollback().lineAt(i);
        if (l.cellCount() == 20 && !l.wrapped() && l.cellAt(19).codePoint() == U'x')
            foundMerged = true;
    }
    ZZ_TEST_EXPECT(foundMerged);

    term.resize(10, 3); // 变窄：B 的链重新折成多行（B 合并后仍是 2 行链，变窄可逆重切）。
    bool foundChain = false;
    for (std::size_t i = 0; i + 1 < term.scrollback().lineCount(); ++i) {
        const ZzLine& l0 = term.scrollback().lineAt(i);
        const ZzLine& l1 = term.scrollback().lineAt(i + 1);
        if (l0.wrapped() && !l1.wrapped() && l1.cellAt(0).codePoint() != 0
            && l0.cellAt(0).codePoint() == U'b' && l1.cellAt(0).codePoint() == U'b')
            foundChain = true;
    }
    ZZ_TEST_EXPECT(foundChain);
    // M16：A（20 列整宽硬行）缩列同样多行化为 2 行链且内容完整
    //（取代原「硬行永不多行化截断、不参与断言」注释）。
    bool foundA = false;
    for (std::size_t i = 0; i + 1 < term.scrollback().lineCount(); ++i) {
        const ZzLine& l0 = term.scrollback().lineAt(i);
        const ZzLine& l1 = term.scrollback().lineAt(i + 1);
        if (l0.wrapped() && !l1.wrapped() && l0.cellAt(0).codePoint() == U'a'
            && l1.cellAt(9).codePoint() == U'x')
            foundA = true;
    }
    ZZ_TEST_EXPECT(foundA); // A 多行化：首行 10 个 a wrapped，链末行 9 个 a + x
}

// 2. 屏幕区随 resize 重组且光标 clamp 在界内
static void testScreenReflowAndCursor()
{
    ZzTerminal term(8, 4, ZzBackendKind::Native, 100);
    feed(term, "abcdefg hijklm"); // 折行：abcdefg /hijklm
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text == "a");
    term.resize(4, 4);
    // 4 列下：abcd/efg /hijk/lm（光标 clamp 在界内）
    const ZzPosition cur = term.cursor().position;
    ZZ_TEST_EXPECT(cur.row >= 0 && cur.row < 4 && cur.col >= 0 && cur.col < 4);
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text.size() == 1);
}

// 3. CJK 长行 resize 后不拆半
static void testCjkNotSplit()
{
    ZzTerminal term(6, 4, ZzBackendKind::Native, 100);
    feed(term, "中文中文中"); // 5 个宽字符占 10 列，折 2 行
    term.resize(5, 4);
    // 5 列下每个宽字符 2 列：每行最多 2 个宽字符（第 5 列留白）
    bool noOrphanContinuation = true;
    for (int r = 0; r < 4; ++r) {
        const ZzLineView line = term.renderView().lineAt(r);
        for (int c = 0; c < 5; ++c) {
            const ZzCellView cv = line.cellAt(c);
            // 续格左侧必须是 lead：行首出现续格即拆半
            if (cv.width == ZzCellWidth::WideContinuation && c == 0)
                noOrphanContinuation = false;
        }
    }
    ZZ_TEST_EXPECT(noOrphanContinuation);
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text == "中");
}

// 4. 1049 备用屏与 resize 交错：进出后主屏内容仍在且已重组
static void testAltScreenInterleave()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feed(term, "primary-content-here\r\n"); // 20 字符折 2 行
    feed(term, "\x1b[?1049h");
    ZZ_TEST_EXPECT(term.isAlternateScreen());
    feed(term, "alt\r\n");
    term.resize(20, 4); // 备用屏期间 resize
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text == "a");
    feed(term, "\x1b[?1049l");
    ZZ_TEST_EXPECT(!term.isAlternateScreen());
    // 主屏长行已合并为 1 行
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text == "p");
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(19).text == "e");
}

// 5. resize 返回值语义不变
static void testResizeReturn()
{
    ZzTerminal term(8, 4, ZzBackendKind::Native, 100);
    ZZ_TEST_EXPECT(!term.resize(8, 4));   // 尺寸未变
    ZZ_TEST_EXPECT(term.resize(10, 4));   // 列变
    ZZ_TEST_EXPECT(term.resize(10, 6));   // 纯行变
    ZZ_TEST_EXPECT(!term.resize(0, 4));   // 非法
}

int main()
{
    testHistoryReflow();
    testScreenReflowAndCursor();
    testCjkNotSplit();
    testAltScreenInterleave();
    testResizeReturn();
    if (g_failures == 0)
        std::printf("test_native_reflow: all passed\n");
    return g_failures;
}
