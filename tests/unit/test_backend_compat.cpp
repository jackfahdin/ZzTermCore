// 双后端对照测试：同一 VT 序列喂 Native/Contour 两个 ZzTerminal，
// 经统一 ZzRenderView 逐格比对（native 为对照基准）。
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

// 同一字节序列喂两个后端，逐格比对统一视图。
struct Dual {
    ZzTerminal native { 80, 24, ZzBackendKind::Native, 1000 };
    ZzTerminal contour { 80, 24, ZzBackendKind::Contour, 1000 };
    void feedBoth(std::string_view bytes)
    {
        native.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
        contour.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
    }
};

// 比对 (row, col) 一格：文本/前景/背景/属性/宽度。
void checkCellEqual(const ZzTerminal& a, const ZzTerminal& b, int row, int col, const char* what)
{
    const ZzCellView ca = a.renderView().lineAt(row).cellAt(col);
    const ZzCellView cb = b.renderView().lineAt(row).cellAt(col);
    if (ca.text != cb.text || ca.foreground != cb.foreground || ca.background != cb.background
        || ca.attributes != cb.attributes || ca.width != cb.width) {
        ++g_failures;
        std::fprintf(stderr, "FAIL cell(%d,%d) %s: native{text=%s,w=%d} vs contour{text=%s,w=%d}\n",
                     row, col, what, ca.text.c_str(), (int)ca.width, cb.text.c_str(), (int)cb.width);
    }
}

// 比对整行前 n 格（宽度不同的宽字符场景逐格比对仍成立：两后端对续格都给 WideContinuation）。
void checkRowEqual(const ZzTerminal& a, const ZzTerminal& b, int row, int n, const char* what)
{
    for (int col = 0; col < n; ++col)
        checkCellEqual(a, b, row, col, what);
}

// 1. ASCII 文本 + 光标位置/可见性。
void testAscii()
{
    Dual d;
    d.feedBoth("Hello");
    checkRowEqual(d.native, d.contour, 0, 5, "ascii");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
    ZZ_CHECK(d.native.cursor().visible == d.contour.cursor().visible);
}

// 2. SGR 索引色：红前景 + 复位。
void testSgrIndexed()
{
    Dual d;
    d.feedBoth("\x1b[31mR\x1b[0mN");
    checkCellEqual(d.native, d.contour, 0, 0, "sgr-indexed");
    checkCellEqual(d.native, d.contour, 0, 1, "sgr-reset");
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(0).foreground == ZzColor::Indexed(1));
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(1).foreground == ZzColor::Default());
}

// 3. RGB TrueColor 前景/背景。
void testRgb()
{
    Dual d;
    d.feedBoth("\x1b[38;2;10;20;30m\x1b[48;2;1;2;3mX");
    checkCellEqual(d.native, d.contour, 0, 0, "rgb");
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(0).foreground == ZzColor::Rgb(10, 20, 30));
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(0).background == ZzColor::Rgb(1, 2, 3));
}

// 4. bold / italic / underline 样式位。
void testStyles()
{
    Dual d;
    d.feedBoth("\x1b[1mB\x1b[0m\x1b[3mI\x1b[0m\x1b[4mU");
    checkCellEqual(d.native, d.contour, 0, 0, "style-bold");
    checkCellEqual(d.native, d.contour, 0, 1, "style-italic");
    checkCellEqual(d.native, d.contour, 0, 2, "style-underline");
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(0).attributes.bold());
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(1).attributes.italic());
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(2).attributes.underline()
             == ZzUnderlineStyle::Single);
}

// 5. CJK 宽字符。差异研判（b 类，两后端真实语义分歧，非转换层 bug）：
// native 的 zzCellWidthOf 是 M1 占位实现、恒返回窄（UnicodeWidth.h 明确真实
// East Asian Width 区间表属 M2），故 "中" 按 Narrow 落格 0、"A" 落格 1；
// Contour vtbackend 有真实 UAX #11 宽度：WideLead / WideContinuation / "A"。
// 按规则 (b) 分别断言各自语义；M2 接入真实宽度表后本用例应恢复逐格对照。
void testCjkWide()
{
    Dual d;
    d.feedBoth("\xE4\xB8\xAD" "A"); // "中A"
    // native：M1 占位宽度表（恒窄）
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(0).text == "\xE4\xB8\xAD");
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(0).width == ZzCellWidth::Narrow);
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(1).text == "A");
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(1).width == ZzCellWidth::Narrow);
    // Contour：真实 UAX #11 宽度
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(0).text == "\xE4\xB8\xAD");
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(1).width == ZzCellWidth::WideContinuation);
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(1).text.empty());
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(2).text == "A");
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(2).width == ZzCellWidth::Narrow);
}

// 6. Alternate Screen。差异研判（b 类，两后端真实语义分歧，非转换层 bug）：
// native 的 dispatchCsi 明确忽略全部 DEC 私有 CSI（备用屏幕 1049/1047/1048 属
// M2，见 NativeCsiDispatch.cpp 文件头注释），故 isAlternateScreen 恒 false，
// "ALT" 直接续写在主屏行 0（"MAINALT"），1049l 亦为无操作；
// Contour 实现完整 1049 语义：切 alt 清屏写 ALT，退出时主屏 "MAIN" 恢复。
// 按规则 (b) 分别断言各自语义；M2 native 支持备用屏幕后应恢复逐格对照。
void testAltScreen()
{
    Dual d;
    d.feedBoth("MAIN\x1b[?1049h");
    ZZ_CHECK(d.contour.isAlternateScreen());  // Contour：1049h 生效
    ZZ_CHECK(!d.native.isAlternateScreen());  // native：DEC 私有 CSI 忽略（M2）
    d.feedBoth("ALT");
    // Contour：alt 屏行 0 写入 "ALT"
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(0).text == "A");
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(1).text == "L");
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(2).text == "T");
    // native：主屏行 0 续写 → "MAINALT"
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(0).text == "M");
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(4).text == "A");
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(6).text == "T");
    d.feedBoth("\x1b[?1049l");
    ZZ_CHECK(!d.contour.isAlternateScreen());
    ZZ_CHECK(!d.native.isAlternateScreen());
    // Contour：主屏 "MAIN" 恢复
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(0).text == "M");
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(3).text == "N");
    ZZ_CHECK(d.contour.renderView().lineAt(0).cellAt(4).text.empty());
    // native：无切换语义，行 0 保持 "MAINALT"
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(0).text == "M");
    ZZ_CHECK(d.native.renderView().lineAt(0).cellAt(6).text == "T");
}

// 7. changes 标志：bell 与 title。
void testChangesFlags()
{
    Dual d;
    const auto cn = d.native.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("\x07"), 1));
    const auto cc = d.contour.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("\x07"), 1));
    ZZ_CHECK(cn.bell);
    ZZ_CHECK(cc.bell);

    const auto tn = d.native.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("\x1b]0;X\x07"), 7));
    const auto tc = d.contour.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("\x1b]0;X\x07"), 7));
    ZZ_CHECK(tn.titleChanged);
    ZZ_CHECK(tc.titleChanged);
    ZZ_CHECK(d.native.title() == "X");
    ZZ_CHECK(d.contour.title() == "X");
}

// 8. Scrollback：双后端各 feed 30 行，累计滚出行数对照。
void testScrollback()
{
    Dual d;
    std::size_t nativeScrolled = 0;
    std::size_t contourScrolled = 0;
    for (int i = 0; i < 30; ++i) {
        const std::string line = "L" + std::to_string(i) + "\r\n";
        const auto cn = d.native.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(line.data()), line.size()));
        const auto cc = d.contour.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(line.data()), line.size()));
        if (cn.scrollbackChanged)
            nativeScrolled += cn.scrolledOutLines;
        if (cc.scrollbackChanged)
            contourScrolled += cc.scrolledOutLines;
    }
    // 两侧累计 scrollback 行数相等（若实测两侧滚动边界语义差 1，以实测为准统一断言容差并注释原因）。
    ZZ_CHECK(nativeScrolled == contourScrolled);
    ZZ_CHECK(nativeScrolled >= 6);
    ZZ_CHECK(d.native.size() == (ZzSize { 80, 24 }));
    ZZ_CHECK(d.contour.size() == (ZzSize { 80, 24 }));
}

// 9. resize：行首短文本在两侧 resize 后内容一致。
void testResize()
{
    Dual d;
    d.feedBoth("Keep");
    ZZ_CHECK(d.native.resize(100, 30));
    ZZ_CHECK(d.contour.resize(100, 30));
    ZZ_CHECK(d.native.size() == d.contour.size());
    ZZ_CHECK(d.native.size() == (ZzSize { 100, 30 }));
    checkRowEqual(d.native, d.contour, 0, 4, "resize-keep");
}

// 10. 光标可见性（DECTCEM ?25l/h）。差异研判（b 类，两后端真实语义分歧）：
// native 忽略 DEC 私有 CSI（DECTCEM 属 M2，同 testAltScreen 注释），visible
// 恒 true；Contour 经 RenderBuffer 上报真实可见性。分别断言各自语义。
void testCursorVisibility()
{
    Dual d;
    d.feedBoth("AB\x1b[?25l");
    ZZ_CHECK(!d.contour.cursor().visible); // Contour：?25l 生效
    ZZ_CHECK(d.native.cursor().visible);   // native：DECTCEM 未实现（M2）
    d.feedBoth("\x1b[?25h");
    ZZ_CHECK(d.contour.cursor().visible);
    ZZ_CHECK(d.native.cursor().visible);
}

} // namespace

int main()
{
    testAscii();
    testSgrIndexed();
    testRgb();
    testStyles();
    testCjkWide();
    testAltScreen();
    testChangesFlags();
    testScrollback();
    testResize();
    testCursorVisibility();
    if (g_failures != 0)
        std::fprintf(stderr, "test_backend_compat: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
