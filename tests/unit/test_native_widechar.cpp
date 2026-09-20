// native 引擎宽字符落格流（M2）：双格写入、光标 +2、行尾换行、
// 半格覆写清理、行尾 wrap-pending。仅公开 API（facade + RenderView）。
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

ZzCellView cell(const ZzTerminal& t, int row, int col)
{
    return t.renderView().lineAt(row).cellAt(col);
}

// 宽字符双格写入 + 光标前进 2 列 + 后续窄字符落在第 3 列。
void testWidePair()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "\xE4\xB8\xAD" "A"); // "中A"
    ZZ_CHECK(cell(t, 0, 0).text == "\xE4\xB8\xAD");
    ZZ_CHECK(cell(t, 0, 0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(cell(t, 0, 1).width == ZzCellWidth::WideContinuation);
    ZZ_CHECK(cell(t, 0, 1).text.empty());
    ZZ_CHECK(cell(t, 0, 2).text == "A");
    ZZ_CHECK(cell(t, 0, 2).width == ZzCellWidth::Narrow);
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 3 }));
}

// 宽字符在行尾最后一列放不下：留空、立即换行到新行行首落格。
void testWideAtRightEdge()
{
    ZzTerminal t(5, 4, ZzBackendKind::Native, 0);
    feed(t, "ABCD\xE4\xB8\xAD"); // "ABCD中"
    ZZ_CHECK(cell(t, 0, 3).text == "D");
    ZZ_CHECK(cell(t, 0, 4).text.empty());           // 行尾留空
    ZZ_CHECK(cell(t, 0, 4).width != ZzCellWidth::WideLead);
    ZZ_CHECK(cell(t, 1, 0).text == "\xE4\xB8\xAD"); // 宽字符落新行行首
    ZZ_CHECK(cell(t, 1, 0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(cell(t, 1, 1).width == ZzCellWidth::WideContinuation);
    ZZ_CHECK(t.cursor().position == (ZzPosition { 1, 2 }));
}

// 半格覆写：新字符落在宽字符的续格上，首格清为空格；落在首格上，续格清空。
void testOverwriteHalfWide()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "\xE4\xB8\xAD");
    feed(t, "\x1b[1;2H"); // 光标到行 0 列 1（续格位置，1 起始）
    feed(t, "X");
    ZZ_CHECK(cell(t, 0, 1).text == "X");
    ZZ_CHECK(cell(t, 0, 0).text.empty());                 // 首格被清理
    ZZ_CHECK(cell(t, 0, 0).width != ZzCellWidth::WideLead);

    ZzTerminal t2(10, 4, ZzBackendKind::Native, 0);
    feed(t2, "\xE4\xB8\xAD");
    feed(t2, "\x1b[1;1H"); // 回首格位置
    feed(t2, "Y");
    ZZ_CHECK(cell(t2, 0, 0).text == "Y");
    ZZ_CHECK(cell(t2, 0, 1).text.empty());                       // 续格被清理
    ZZ_CHECK(cell(t2, 0, 1).width != ZzCellWidth::WideContinuation);
}

// 宽字符占满行尾两格：光标停最后一列并置 wrap-pending，下一字符换行。
void testWideWrapPending()
{
    ZzTerminal t(4, 4, ZzBackendKind::Native, 0);
    feed(t, "AB\xE4\xB8\xAD"); // "AB中"（中占列 2-3）
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 3 }));
    feed(t, "Z");
    ZZ_CHECK(cell(t, 1, 0).text == "Z"); // Z 换行落新行行首
    ZZ_CHECK(t.renderView().lineAt(0).wrapped()); // 行 0 软换行标记
}

} // namespace

int main()
{
    testWidePair();
    testWideAtRightEdge();
    testOverwriteHalfWide();
    testWideWrapPending();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_widechar: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
