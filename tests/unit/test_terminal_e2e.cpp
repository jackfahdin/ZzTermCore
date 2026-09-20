// ZzTerminal 端到端场景与 chunk 切分一致性测试。
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

/// 屏幕文本快照（非 ASCII 以 '?' 占位；仅用于断言行内容）。
static std::string screenText(const ZzTerminal& term)
{
    std::string out;
    for (int row = 0; row < term.size().rows; ++row) {
        const ZzLineView line = term.renderView().lineAt(row);
        for (int col = 0; col < line.cellCount(); ++col) {
            const ZzCellView cell = line.cellAt(col);
            if (cell.text.empty())
                out.push_back(' ');
            else if (cell.text.size() == 1)
                out.push_back(cell.text[0]);
            else
                out.push_back('?');
        }
        out.push_back('\n');
    }
    return out;
}

static void testColoredLs()
{
    // 模拟 ls --color：蓝色加粗目录名 + 普通文件名。
    ZzTerminal term(20, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b[1;34msrc\x1b[0m/  README.md\r\n");
    ZZ_TEST_EXPECT(screenText(term).substr(0, 20) == "src/  README.md     ");
    const ZzCellView dir = term.renderView().lineAt(0).cellAt(0);
    ZZ_TEST_EXPECT(dir.attributes.bold());
    ZZ_TEST_EXPECT(dir.foreground == ZzColor::Indexed(4));
    const ZzCellView file = term.renderView().lineAt(0).cellAt(7);
    ZZ_TEST_EXPECT(file.attributes == ZzCellAttributes{});
    ZZ_TEST_EXPECT(file.foreground.isDefault());
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 0);
}

static void testVimStyleRedraw()
{
    // 模拟全屏程序：清屏 + 光标归位 + 逐行重绘。
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedStr(term, "junk\r\njunk\r\njunk");
    feedStr(term, "\x1b[2J\x1b[H");
    feedStr(term, "~\r\n~\r\n~");
    ZZ_TEST_EXPECT(screenText(term) == "~         \n~         \n~         \n");
}

static void testSplitInvariance()
{
    // 混合场景在任意切分点分两段喂入，终态必须与一次性喂入一致。
    const std::string scenarios[] = {
        "hello \x1b[1;31mworld\x1b[0m!\r\nnext \x1b]2;t\x07line",
        "\xe4\xb8\xad\xe6\x96\x87\x1b[2;5Hz", // UTF-8 + CUP
        "abc\x1b[K\x1b[1;3H\x1b[7mQ\x1b[0m",
    };
    for (const std::string& input : scenarios) {
        ZzTerminal reference(20, 5, ZzBackendKind::Native, 100);
        feedStr(reference, input);
        const std::string want = screenText(reference);

        for (std::size_t cut = 0; cut <= input.size(); ++cut) {
            ZzTerminal term(20, 5, ZzBackendKind::Native, 100);
            feedStr(term, input.substr(0, cut));
            feedStr(term, input.substr(cut));
            ZZ_TEST_EXPECT(screenText(term) == want);
            ZZ_TEST_EXPECT(term.cursor() == reference.cursor());
            ZZ_TEST_EXPECT(term.title() == reference.title());
        }
    }
}

static void testScrollThenWriteRemainsVisible()
{
    // 底部行写入 + LF 滚动后，新写入的行必须可见。
    // 回归场景：滚动移位留下被 move 掏空的行，clear 不恢复列数，
    // 后续写入被 ZzLine::setCell 的边界检查静默丢弃（屏幕"冻结"）。
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b[4;1H"); // 光标到底部行
    feedStr(term, "A\r\nB\r\nC\r\nD");
    ZZ_TEST_EXPECT(screenText(term) == "A         \nB         \nC         \nD         \n");
}

static void testScrollDownThenWriteRemainsVisible()
{
    // 反方向：IL（scrollRegionDown）移位同样产生掏空行，腾出的空行必须可写。
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "A\r\nB\r\nC\r\nD"); // 写满 4 行（无滚动）
    feedStr(term, "\x1b[1;1H\x1b[L"); // 光标回 row 0，插入一行（D 滚出底部）
    feedStr(term, "X");
    ZZ_TEST_EXPECT(screenText(term) == "X         \nA         \nB         \nC         \n");
}

int main()
{
    testColoredLs();
    testVimStyleRedraw();
    testSplitInvariance();
    testScrollThenWriteRemainsVisible();
    testScrollDownThenWriteRemainsVisible();
    if (g_failures == 0)
        std::puts("test_terminal_e2e: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
