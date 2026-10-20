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
    // 纯行变 resize（R1）：缩行把屏幕顶行压入历史、扩行回抽——两者历史内容均变，
    // 代计数均须递增。扩行不触动 droppedLineCount（floor 不动），唯行数比较能检出，
    // 钉住 historyBefore 捕获点前移。
    const std::uint64_t gRow = term.historyView().generation();
    const std::size_t rowsBefore = term.historyView().lineCount();
    ZZ_TEST_EXPECT(term.resize(40, 3));
    ZZ_TEST_EXPECT(term.historyView().generation() > gRow);      // 缩行递增
    ZZ_TEST_EXPECT(term.historyView().lineCount() > rowsBefore); // 顶行压入历史
    const std::uint64_t gShrink = term.historyView().generation();
    const std::size_t rowsShrunk = term.historyView().lineCount();
    ZZ_TEST_EXPECT(term.resize(40, 6)); // 扩行回抽，视图仍可读不越界
    ZZ_TEST_EXPECT(term.historyView().generation() > gShrink);   // 扩行亦递增（R1 关键）
    ZZ_TEST_EXPECT(term.historyView().lineCount() < rowsShrunk); // 历史行回抽屏幕
    ZZ_TEST_EXPECT(term.historyView().lineCount() > 0);
    const std::uint64_t g2 = term.historyView().generation();
    feedStr(term, "\x1b[?1049h");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // Alternate 历史归零
    ZZ_TEST_EXPECT(term.historyView().generation() > g2);
    feedStr(term, "\x1b[?1049l");
    ZZ_TEST_EXPECT(term.historyView().lineCount() > 0); // 回 Primary 恢复
}

// M21：primaryLineCount buffer 无关口径双后端对拍
static void testPrimaryLineCountParity()
{
    ZzTerminal native(10, 4, ZzBackendKind::Native, 100);
    ZzTerminal contour(10, 4, ZzBackendKind::Contour, 100);
    for (int i = 0; i < 8; ++i) {
        feedStr(native, "x\r\n");
        feedStr(contour, "x\r\n");
    }
    ZZ_TEST_EXPECT(native.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(contour.historyView().primaryLineCount() == 5); // Primary 下同口径
    feedStr(native, "\x1b[?1049h");
    feedStr(contour, "\x1b[?1049h"); // 进 Alternate：可见历史归零
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 0);
    // buffer 无关口径：Alternate 期间仍返回 Primary 历史真实行数
    //（默认回退 lineCount() 会归 0，本断言驱动覆写落地）
    ZZ_TEST_EXPECT(contour.historyView().primaryLineCount() == 5);
    ZZ_TEST_EXPECT(native.historyView().primaryLineCount()
                   == contour.historyView().primaryLineCount());
    feedStr(native, "\x1b[?1049l");
    feedStr(contour, "\x1b[?1049l"); // 回 Primary：历史恢复可见
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(contour.historyView().primaryLineCount() == 5);
}

// M21：纯行变收缩的屏→史迁移计数与 native 对拍（列宽不变、无 reflow，两口径等价）
static void testResizeScrolloutRowShrinkParity()
{
    ZzTerminal native(40, 6, ZzBackendKind::Native, 100);
    ZzTerminal contour(40, 6, ZzBackendKind::Contour, 100);
    for (int i = 0; i < 8; ++i) {
        feedStr(native, "segment\r\n");
        feedStr(contour, "segment\r\n");
    }
    ZZ_TEST_EXPECT(native.historyView().lineCount() == 3);
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 3);
    ZZ_TEST_EXPECT(native.resize(40, 3));  // 行收缩：屏 3 行迁移入史
    ZZ_TEST_EXPECT(contour.resize(40, 3));
    ZZ_TEST_EXPECT(native.resizeScreenScrollout() == 3);
    ZZ_TEST_EXPECT(contour.resizeScreenScrollout() == 3); // 默认回退 0，本断言驱动覆写
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 6);
    // 扩行回抽为负 → 归零（调用方走收缩向兜底）
    ZZ_TEST_EXPECT(contour.resize(40, 6));
    ZZ_TEST_EXPECT(contour.resizeScreenScrollout() == 0);
}

// M21：列变 reflow 钉住变体口径本身（数值 = max(0, 主屏历史净增量)），不与 native 对拍
static void testResizeScrolloutColumnChangeVariant()
{
    ZzTerminal term(40, 6, ZzBackendKind::Contour, 100);
    for (int i = 0; i < 8; ++i)
        feedStr(term, "segment-abcdefgh0123456789-abcdefgh0123\r\n"); // 40 列恰好一行
    const std::size_t before = term.historyView().lineCount();
    ZZ_TEST_EXPECT(before == 3);
    ZZ_TEST_EXPECT(term.resize(24, 6)); // 列变触发 reflow
    const std::size_t after = term.historyView().lineCount();
    const std::size_t expected = after > before ? after - before : 0;
    ZZ_TEST_EXPECT(term.resizeScreenScrollout() == expected); // 钉住变体口径定义
    ZZ_TEST_EXPECT(term.historyView().lineCount() > 0);       // 历史可读不崩
}

int main()
{
    testHistoryTextParity();
    testTrimParity();
    testContourGenerationAndAlternate();
    testPrimaryLineCountParity();
    testResizeScrolloutRowShrinkParity();
    testResizeScrolloutColumnChangeVariant();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
