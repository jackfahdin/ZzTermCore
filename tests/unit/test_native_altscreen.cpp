// native 备用屏幕（DECSET 1049/1047/1048，M2）。仅公开 API。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

void feed(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                      bytes.size()));
}

ZzCellView cell(const ZzTerminal& t, int row, int col)
{
    return t.renderView().lineAt(row).cellAt(col);
}

// 1049h：保存光标 + 切 alt + 清 alt；1049l：回主屏 + 恢复内容与光标。
void test1049RoundTrip()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "MAIN\x1b[?1049h");
    ZZ_CHECK(t.isAlternateScreen());
    ZZ_CHECK(cell(t, 0, 0).text.empty());        // alt 已清屏
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 0 }));
    feed(t, "ALT");
    ZZ_CHECK(cell(t, 0, 0).text == "A");
    feed(t, "\x1b[?1049l");
    ZZ_CHECK(!t.isAlternateScreen());
    ZZ_CHECK(cell(t, 0, 0).text == "M");         // 主屏恢复
    ZZ_CHECK(cell(t, 0, 3).text == "N");
    ZZ_CHECK(cell(t, 0, 4).text.empty());        // alt 的 ALT 不污染主屏
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 4 })); // 光标恢复
}

// 幂等：重复 1049h 不炸不叠加；主屏上 1049l 为空操作。
void test1049Idempotent()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "AB\x1b[?1049h\x1b[?1049h");
    ZZ_CHECK(t.isAlternateScreen());
    feed(t, "\x1b[?1049l\x1b[?1049l");
    ZZ_CHECK(!t.isAlternateScreen());
    ZZ_CHECK(cell(t, 0, 0).text == "A");
}

// 1047h/l：切 buffer + 进 alt 清屏；不保存/恢复光标（主屏光标原位保留）。
void test1047NoCursorSave()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "AB\x1b[?1047h");
    ZZ_CHECK(t.isAlternateScreen());
    feed(t, "XY\x1b[?1047l");
    ZZ_CHECK(!t.isAlternateScreen());
    ZZ_CHECK(cell(t, 0, 0).text == "A");
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 2 })); // 主屏光标原位
}

// 1048h/l：仅保存/恢复光标，不切 buffer。
void test1048CursorOnly()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "AB\x1b[?1048h\x1b[3;5H\x1b[?1048l");
    ZZ_CHECK(!t.isAlternateScreen());
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 2 }));
}

// alt 滚动永不进历史（ZzScreen 既有语义，经 1049 路径回归验证）。
void testAltScrollNoHistory()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 1000);
    feed(t, "\x1b[?1049h");
    std::size_t scrolled = 0;
    for (int i = 0; i < 20; ++i) {
        const std::string line = "L" + std::to_string(i) + "\r\n";
        const ZzTermChanges ch = t.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(line.data()), line.size()));
        scrolled += ch.scrolledOutLines;
    }
    ZZ_CHECK(scrolled == 0);
}

// 滚动区随 buffer 切换保存/恢复（间接验证，公开 API 无滚动区查询）：
// 主屏 6 行写满，设滚动区 [3,5]（1 起始，即 0 起始 2-4 行），1049 往返后，
// 光标到区底喂换行 → 区内滚动（行 0/1/5 不动，行 2 原内容被顶走）。
// 若滚动区未恢复（全屏），行 0 会被顶走——据此区分。
void testScrollRegionRestored()
{
    ZzTerminal t(10, 6, ZzBackendKind::Native, 0);
    feed(t, "AAAAAAAA\r\nBBBBBBBB\r\nCCCCCCCC\r\nDDDDDDDD\r\nEEEEEEEE\r\nFFFFFFFF");
    feed(t, "\x1b[3;5r");       // 滚动区行 2-4（0 起始）
    feed(t, "\x1b[?1049h\x1b[?1049l");
    feed(t, "\x1b[5;1H");       // 光标到区底行 4（1 起始第 5 行）
    feed(t, "\n");
    ZZ_CHECK(cell(t, 0, 0).text == "A"); // 区外行 0 不动
    ZZ_CHECK(cell(t, 1, 0).text == "B"); // 区外行 1 不动
    ZZ_CHECK(cell(t, 2, 0).text == "D"); // 区内上滚：原行 3 内容上移
    ZZ_CHECK(cell(t, 5, 0).text == "F"); // 区外行 5 不动
}

} // namespace

int main()
{
    test1049RoundTrip();
    test1049Idempotent();
    test1047NoCursorSave();
    test1048CursorOnly();
    testAltScrollNoHistory();
    testScrollRegionRestored();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_altscreen: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
