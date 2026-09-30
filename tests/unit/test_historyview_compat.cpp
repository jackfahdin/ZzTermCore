// M14：双后端历史视图 compat（同一 VT 脚本历史文本逐行比对，native 为基准）
// + contour 侧 generation/alternate 行为钉住（facade 层，只碰公开头）。
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

static std::string historyLineText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

// 纯滚动脚本：30 行编号行 + 21 字符软换行长行 + CJK 行。
static void runScript(ZzTerminal& term)
{
    for (int i = 0; i < 30; ++i)
        feedStr(term, "line-" + std::to_string(i) + "\r\n");
    feedStr(term, "abcdefghijklmnopqrstu\r\n"); // 21 字符在 20 列软换行为 2 行
    feedStr(term, "\xe4\xb8\xad\xe6\x96\x87\xe8\xa1\x8c\r\n"); // 「中文行」
}

static void testHistoryTextParity()
{
    ZzTerminal native(20, 6, ZzBackendKind::Native, 100);
    ZzTerminal contour(20, 6, ZzBackendKind::Contour, 100);
    runScript(native);
    runScript(contour);
    const std::size_t n = native.historyView().lineCount();
    ZZ_TEST_EXPECT(n > 0);
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == n);
    const std::size_t common = n < contour.historyView().lineCount()
        ? n : contour.historyView().lineCount();
    for (std::size_t i = 0; i < common; ++i)
        ZZ_TEST_EXPECT(historyLineText(native, i) == historyLineText(contour, i));
}

static void testTrimParity()
{
    ZzTerminal native(20, 6, ZzBackendKind::Native, 10); // 容量 10 触发裁剪
    ZzTerminal contour(20, 6, ZzBackendKind::Contour, 10);
    runScript(native);
    runScript(contour);
    // 纯滚动脚本下双后端均只计真实容量裁剪（contour 经 noteFloor 口径）。
    ZZ_TEST_EXPECT(native.historyView().lineCount() == 10);
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 10);
    ZZ_TEST_EXPECT(native.historyView().droppedLineCount()
                   == contour.historyView().droppedLineCount());
    for (std::size_t i = 0; i < 10; ++i)
        ZZ_TEST_EXPECT(historyLineText(native, i) == historyLineText(contour, i));
}

static void testContourGenerationAndAlternate()
{
    ZzTerminal term(20, 6, ZzBackendKind::Contour, 100);
    const std::uint64_t g0 = term.historyView().generation();
    runScript(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() > 0);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0); // 滚出 append 递增
    const std::uint64_t g1 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(40, 6));
    ZZ_TEST_EXPECT(term.historyView().generation() > g1); // 列变 reflow 递增
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).cellCount() == 40); // 行宽不变量
    const std::uint64_t g2 = term.historyView().generation();
    feedStr(term, "\x1b[?1049h");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // Alternate 历史归零
    ZZ_TEST_EXPECT(term.historyView().generation() > g2);
    feedStr(term, "\x1b[?1049l");
    ZZ_TEST_EXPECT(term.historyView().lineCount() > 0); // 回 Primary 恢复
}

int main()
{
    testHistoryTextParity();
    testTrimParity();
    testContourGenerationAndAlternate();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
