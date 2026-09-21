// 双后端选区 compat（M5a）：同一 VT 脚本喂 Native/Contour，选区提取文本
// 逐字节一致（native 为基准）；含跨接缝逻辑行、宽字符、reflow 保持用例。
// 已知分歧按 b 类惯例分别断言 + 注释钉住。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <string>

static int g_failures = 0;
#define ZZ_CHECK(cond)                                                        \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

struct Dual {
    ZzTerminal native { 10, 3, ZzBackendKind::Native, 100 };
    ZzTerminal contour { 10, 3, ZzBackendKind::Contour, 100 };
    void feedBoth(std::string_view bytes)
    {
        native.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
        contour.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
    }
};

void checkSelectedTextEqual(Dual& d, ZzLogicalPos start, ZzLogicalPos end, const char* what)
{
    d.native.setSelection(start, end);
    d.contour.setSelection(start, end);
    const std::string a = d.native.selectedText();
    const std::string b = d.contour.selectedText();
    if (a != b)
        std::fprintf(stderr, "  mismatch [%s]: native=%zu bytes contour=%zu bytes\n",
                     what, a.size(), b.size());
    ZZ_CHECK(a == b);
    d.native.clearSelection();
    d.contour.clearSelection();
}

} // namespace

// 1. 屏幕区 ASCII 选区
static void testScreenSelection()
{
    Dual d;
    d.feedBoth("hello");
    checkSelectedTextEqual(d, {0, 0}, {0, 5}, "screen ascii");
}

// 2. 历史+屏幕统一空间选区（10x3 屏，5 行硬行脚本：2 行入历史）
static void testHistorySelection()
{
    Dual d;
    d.feedBoth("r0\r\nr1\r\nr2\r\nr3\r\nr4");
    checkSelectedTextEqual(d, {0, 0}, {0, 2}, "history line 0");
    checkSelectedTextEqual(d, {1, 0}, {2, 2}, "cross history/screen");
}

// 3. 软换行逻辑行选区（不插换行）
static void testSoftWrapSelection()
{
    Dual d;
    d.feedBoth("0123456789abcde"); // 10 列软换行两条物理行
    checkSelectedTextEqual(d, {0, 0}, {0, 15}, "softwrap joined");
}

// 4. 跨接缝逻辑行（历史末行软续到屏幕首行）：收口 M4 观察项①。
//    10x3 屏：首条逻辑行 16 格占两条物理行，脚本共 4 物理行 → 历史恰留
//    链首行（abcdefghij），链尾（klmnop）在屏幕首行，接缝断在链中间。
//    若两后端在接缝拼接上分歧，按 b 类分别断言并注释钉住。
static void testSeamLogicalLine()
{
    Dual d; // 10x3
    d.feedBoth("abcdefghijklmnop\r\nzz\r\nww"); // 4 物理行，历史 1 行 = 链首
    checkSelectedTextEqual(d, {0, 0}, {0, 16}, "seam stitched logical line");
}

// 5. 宽字符选区
static void testWideCharSelection()
{
    Dual d;
    d.feedBoth("ab界面cd");
    checkSelectedTextEqual(d, {0, 0}, {0, 6}, "wide chars");
}

// 6. resize reflow 后选区文本保持（双后端各自断言 resize 前后一致 + 互比）
static void testReflowKeepsSelectionText()
{
    Dual d;
    d.feedBoth("0123456789abcde");
    d.native.setSelection({0, 0}, {0, 15});
    d.contour.setSelection({0, 0}, {0, 15});
    const std::string nativeBefore = d.native.selectedText();
    const std::string contourBefore = d.contour.selectedText();
    d.native.resize(5, 3);
    d.contour.resize(5, 3);
    ZZ_CHECK(d.native.selectedText() == nativeBefore);
    ZZ_CHECK(d.contour.selectedText() == contourBefore);
    ZZ_CHECK(d.native.selectedText() == d.contour.selectedText());
}

int main()
{
    testScreenSelection();
    testHistorySelection();
    testSoftWrapSelection();
    testSeamLogicalLine();
    testWideCharSelection();
    testReflowKeepsSelectionText();
    if (g_failures == 0)
        std::printf("test_selection_compat: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
