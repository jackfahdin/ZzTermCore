// Ambiguous 宽度配置口（M2）：默认窄（xterm 兼容），可切 CJK 宽。仅公开 API。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
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

// 默认：Ambiguous 码位按窄（1 列）。
void testDefaultNarrow()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "\xC2\xB7"); // U+00B7 ·
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(0).width == ZzCellWidth::Narrow);
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 1 }));
}

// 开启宽模式：Ambiguous 码位按 2 列（双格写入）。
void testAmbiguousWide()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    t.setAmbiguousWidthMode(true);
    feed(t, "\xC2\xB7" "A"); // "·A"
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(1).width == ZzCellWidth::WideContinuation);
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(2).text == "A");
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 3 }));
}

// 配置不影响 W 类（始终宽）与 Na 类（始终窄）。
void testOtherClassesUnaffected()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    t.setAmbiguousWidthMode(true);
    feed(t, "\xE4\xB8\xAD" "B"); // "中B"
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(2).text == "B");
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(2).width == ZzCellWidth::Narrow);
}

} // namespace

int main()
{
    testDefaultNarrow();
    testAmbiguousWide();
    testOtherClassesUnaffected();
    if (g_failures != 0)
        std::fprintf(stderr, "test_ambiguous_width: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
