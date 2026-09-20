// ZzTerminal print 通路与 pending-wrap 行为测试。
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

static char32_t cpAt(const ZzTerminal& term, int row, int col)
{
    const ZzCellView cell = term.renderView().lineAt(row).cellAt(col);
    if (cell.text.empty()) return 0;
    // 解码首码位（本文件断言均为单码位文本）。
    const auto* p = reinterpret_cast<const unsigned char*>(cell.text.data());
    if (p[0] < 0x80) return p[0];
    if ((p[0] & 0xE0) == 0xC0) return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    if ((p[0] & 0xF0) == 0xE0)
        return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
}

static void testPrintAscii()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    const ZzTermChanges changes = [&term] {
        const std::string s = "hi";
        return term.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(s.data()), s.size()));
    }();
    ZZ_TEST_EXPECT(changes.screenDirty);
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'h');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'i');
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
}

static void testPrintUtf8()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "中文"); // M2 真实 EAW 宽度：均按宽格落格（各占 2 列）
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'中');
    ZZ_TEST_EXPECT(cpAt(term, 0, 2) == U'文');
    ZZ_TEST_EXPECT(term.cursor().position.col == 4);
}

static void testPendingWrap()
{
    ZzTerminal term(5, 3, ZzBackendKind::Native, 100);
    feedStr(term, "abcde");
    // 写满最后一列：光标停在最后一列，暂不换行（xterm pending-wrap）。
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 4);
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());

    feedStr(term, "f");
    // 下一个可打印字符到达才换行。
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'f');
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 1);
}

static void testPendingWrapClearedByCR()
{
    ZzTerminal term(5, 3, ZzBackendKind::Native, 100);
    feedStr(term, "abcde");
    feedStr(term, "\rX"); // CR 清除 pending-wrap：X 覆盖行首而非换行
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'X');
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(term.cursor().position.col == 1);
}

static void testNoWrapWhenAutoWrapOff()
{
    ZzTerminal term(5, 3, ZzBackendKind::Native, 100);
    term.screen().setAutoWrapMode(false); // DECAWM 关
    feedStr(term, "abcdefg");
    // 不换行：后续字符持续覆盖最后一列。
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 4);
    ZZ_TEST_EXPECT(cpAt(term, 0, 4) == U'g');
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());
}

static void testC0()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "ab\rcd");        // CR 回列首
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'c');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'd');

    feedStr(term, "\n");            // LF 下移一行
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);

    feedStr(term, "x\by");          // BS 左移后覆盖（y 覆盖 x 所在格）
    ZZ_TEST_EXPECT(cpAt(term, 1, 2) == U'y');
    ZZ_TEST_EXPECT(term.cursor().position.col == 3);

    feedStr(term, "\t");            // HT 到下一个 Tab Stop（默认每 8 列）
    ZZ_TEST_EXPECT(term.cursor().position.col == 8);

    const ZzTermChanges ch = term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("\a"), 1));
    ZZ_TEST_EXPECT(ch.bell);        // BEL 置响铃标志
}

static void testLfScrollsAtRegionBottom()
{
    ZzTerminal term(5, 2, ZzBackendKind::Native, 100);
    feedStr(term, "one\r\ntwo\r\n"); // 第二行使出滚动区下沿 -> 上滚
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U't'); // "two" 顶到第 0 行
    ZZ_TEST_EXPECT(term.scrollback().lineCount() == 1); // "one" 入历史
}

static void testOscTitle()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]2;my title\x07"); // OSC 2 ; title BEL
    ZZ_TEST_EXPECT(term.title() == "my title");
    feedStr(term, "\x1b]0;both\x1b\\");   // OSC 0，ST 终止
    ZZ_TEST_EXPECT(term.title() == "both");
}

static void testEscSaveRestore()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "abc");
    feedStr(term, "\x1b" "7");      // DECSC
    feedStr(term, "\r\nxyz");
    feedStr(term, "\x1b" "8");      // DECRC
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 3);
}

static void testEscIndNelRiHts()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b" "D");      // IND：下移一行
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    feedStr(term, "\x1b" "M");      // RI：上移一行
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    feedStr(term, "\x1b" "E");      // NEL：回列首并下移
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 0);
    feedStr(term, "\x1b" "H");      // HTS：当前列设 Tab Stop
    feedStr(term, "\t");
    // HTS 精确语义：col 0 设 stop 后，HT 从 col 0 跳到下一个默认 stop（col 8）。
    ZZ_TEST_EXPECT(term.cursor().position.col == 8);
}

static void testRiScrollsDownAtTop()
{
    ZzTerminal term(5, 2, ZzBackendKind::Native, 100);
    feedStr(term, "ab");
    feedStr(term, "\x1b" "M"); // 光标在滚动区上沿，RI 向下滚动
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'a');
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
}

static void testNelScrollsAndResetsColAtBottom()
{
    ZzTerminal term(5, 2, ZzBackendKind::Native, 100);
    feedStr(term, "ab\x1b" "Ecd"); // NEL 到末行，列非 0
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
    feedStr(term, "\x1b" "E"); // NEL 在下沿：上滚且 CR 无条件生效（ECMA-48 NEL = CR + IND）
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 0);
    ZZ_TEST_EXPECT(term.scrollback().lineCount() == 1); // 首行入历史
}

static void testRestoreCursorClampedAfterResize()
{
    ZzTerminal term(10, 6, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b[6;8H"); // CUP：row 5、col 7（0 起始）
    feedStr(term, "\x1b" "7");  // DECSC 保存光标
    term.resize(4, 4);          // 缩小网格
    feedStr(term, "\x1b" "8");  // DECRC 恢复：位置必须钳制到新网格内
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.cursor().position.col == 3);
}

int main()
{
    testPrintAscii();
    testPrintUtf8();
    testPendingWrap();
    testPendingWrapClearedByCR();
    testNoWrapWhenAutoWrapOff();
    testC0();
    testLfScrollsAtRegionBottom();
    testOscTitle();
    testEscSaveRestore();
    testEscIndNelRiHts();
    testRiScrollsDownAtTop();
    testNelScrollsAndResetsColAtBottom();
    testRestoreCursorClampedAfterResize();
    if (g_failures == 0)
        std::puts("test_terminal_core: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
