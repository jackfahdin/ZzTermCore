// ZzCell / ZzColor / ZzCellAttributes 数据模型不变量测试。
// 约定：每个 tests/unit/*.cpp 含 main()，失败返回非零。

#include <cstdio>

#include "ZzTerm/Cell.h"

static int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

int main()
{
    // 布局不变量（Cell.h 文件头记录了当前尺寸基线）。
    static_assert(sizeof(ZzColor) == 4);
    static_assert(sizeof(ZzCellAttributes) == 2);
    static_assert(sizeof(ZzCell) == 16);
    static_assert(alignof(ZzCell) == 4);

    // 颜色模型：default / ANSI 16 / bright / 256 / RGB TrueColor。
    ZZ_TEST_EXPECT(ZzColor::Default().isDefault());
    ZZ_TEST_EXPECT(ZzColor::Default().kind() == ZzColor::Kind::Default);

    constexpr ZzColor ansiRed = ZzColor::Indexed(1);    // ANSI 16
    constexpr ZzColor brightRed = ZzColor::Indexed(9);  // bright
    constexpr ZzColor ext = ZzColor::Indexed(200);      // 256 色
    ZZ_TEST_EXPECT(ansiRed.kind() == ZzColor::Kind::Indexed && ansiRed.index() == 1);
    ZZ_TEST_EXPECT(brightRed.index() == 9);
    ZZ_TEST_EXPECT(ext.index() == 200);

    constexpr ZzColor rgb = ZzColor::Rgb(0x12, 0x34, 0x56);
    ZZ_TEST_EXPECT(rgb.kind() == ZzColor::Kind::Rgb);
    ZZ_TEST_EXPECT(rgb.red() == 0x12 && rgb.green() == 0x34 && rgb.blue() == 0x56);
    ZZ_TEST_EXPECT(ZzColor::Rgb(0, 0, 0) != ZzColor::Default()); // 黑色不是默认色

    // 属性位。
    ZzCellAttributes attrs;
    ZZ_TEST_EXPECT(attrs.raw() == 0);
    attrs.setBold(true);
    attrs.setUnderline(ZzUnderlineStyle::Curly);
    attrs.setBlink(ZzBlinkStyle::Rapid);
    attrs.setInverse(true);
    attrs.setStrikethrough(true);
    attrs.setProtected(true);
    attrs.setInvisible(true);
    attrs.setFaint(true);
    attrs.setItalic(true);
    ZZ_TEST_EXPECT(attrs.bold() && attrs.faint() && attrs.italic());
    ZZ_TEST_EXPECT(attrs.underline() == ZzUnderlineStyle::Curly);
    ZZ_TEST_EXPECT(attrs.blink() == ZzBlinkStyle::Rapid);
    ZZ_TEST_EXPECT(attrs.inverse() && attrs.invisible() && attrs.strikethrough());
    ZZ_TEST_EXPECT(attrs.isProtected());
    attrs.setUnderline(ZzUnderlineStyle::Double);
    ZZ_TEST_EXPECT(attrs.bold()); // 改下划线不影响其他位
    attrs.reset();
    ZZ_TEST_EXPECT(attrs.raw() == 0);

    // Cell 文本载荷：空 / 单码位 / cluster 引用 / 宽度类别。
    ZzCell cell;
    ZZ_TEST_EXPECT(cell.isEmpty());
    ZZ_TEST_EXPECT(cell.width() == ZzCellWidth::Empty);

    cell.setCodePoint(U'A');
    cell.setWidth(ZzCellWidth::Narrow);
    ZZ_TEST_EXPECT(!cell.isEmpty() && !cell.isCluster());
    ZZ_TEST_EXPECT(cell.codePoint() == U'A');

    cell.setCodePoint(0x10FFFF); // 最大合法码位不丢位
    ZZ_TEST_EXPECT(cell.codePoint() == 0x10FFFF);

    cell.setWidth(ZzCellWidth::WideLead);
    cell.setCodePoint(0x4E2D); // '中'
    ZZ_TEST_EXPECT(cell.width() == ZzCellWidth::WideLead && cell.codePoint() == 0x4E2D);

    cell.setCluster(42);
    ZZ_TEST_EXPECT(cell.isCluster() && cell.clusterIndex() == 42);

    cell.reset();
    ZZ_TEST_EXPECT(cell.isEmpty());
    ZZ_TEST_EXPECT(cell.foreground().isDefault() && cell.background().isDefault());

    if (g_failures == 0) {
        std::fprintf(stderr, "test_cell: PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_cell: %d failure(s)\n", g_failures);
    return 1;
}
