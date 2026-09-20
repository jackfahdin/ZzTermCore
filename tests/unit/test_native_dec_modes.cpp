// native DECAWM(?7) 与 DECTCEM(?25)（M2）。仅公开 API。
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

// DECTCEM：?25l 隐藏、?25h 恢复；默认可见。
void testCursorVisibility()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    ZZ_CHECK(t.cursor().visible);
    feed(t, "AB\x1b[?25l");
    ZZ_CHECK(!t.cursor().visible);
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 2 })); // 可见性不影响位置
    feed(t, "\x1b[?25h");
    ZZ_CHECK(t.cursor().visible);
}

// DECAWM 关闭：行尾覆写不换行；重新开启后恢复换行。
void testAutoWrapToggle()
{
    ZzTerminal t(4, 4, ZzBackendKind::Native, 0);
    feed(t, "\x1b[?7l");
    feed(t, "ABCDE"); // ABCD 填满，E 覆写最后一格
    ZZ_CHECK(cell(t, 0, 0).text == "A");
    ZZ_CHECK(cell(t, 0, 3).text == "E"); // D 被覆写
    ZZ_CHECK(cell(t, 1, 0).text.empty()); // 未换行
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 3 }));
    feed(t, "\x1b[?7h");
    feed(t, "F"); // 换行落新行行首
    ZZ_CHECK(cell(t, 1, 0).text == "F");
    ZZ_CHECK(t.cursor().position == (ZzPosition { 1, 1 }));
}

// 复合序列：ESC[?25;7l 一次关两个模式。
void testCombinedModes()
{
    ZzTerminal t(4, 4, ZzBackendKind::Native, 0);
    feed(t, "\x1b[?25;7l");
    ZZ_CHECK(!t.cursor().visible);
    feed(t, "ABCDE");
    ZZ_CHECK(cell(t, 1, 0).text.empty()); // DECAWM 也已关
}

} // namespace

int main()
{
    testCursorVisibility();
    testAutoWrapToggle();
    testCombinedModes();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_dec_modes: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
