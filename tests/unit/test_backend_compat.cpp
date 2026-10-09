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

// 比对 (row, col) 一格（严格）：文本/前景/背景/属性/宽度全部强比对。
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

// 比对 (row, col) 一格（放宽空单元格宽度类别），仅供 resize reflow 用例调用。
// 分歧钉住（b 类，M4 任务 5 resize reflow 对照实测）：空单元格宽度类别
// 两后端表示约定不同——native 报 ZzCellWidth::Empty（值 0），Contour 经
// zzWidth 对无文本格报 ZzCellWidth::Narrow（值 1）；文本/颜色/属性一致，
// 纯属空格的宽度类别表示差异，非 reflow 语义分歧。故两侧文本均为空时
// 放宽宽度比对；宽字符续格（WideContinuation，文本亦为空）仍强比对。
void checkCellEqualAllowEmptyWidthDiff(const ZzTerminal& a, const ZzTerminal& b, int row, int col,
                                       const char* what)
{
    const ZzCellView ca = a.renderView().lineAt(row).cellAt(col);
    const ZzCellView cb = b.renderView().lineAt(row).cellAt(col);
    const bool bothEmptyText = ca.text.empty() && cb.text.empty();
    const bool widthEqual = ca.width == cb.width
        || (bothEmptyText && ca.width != ZzCellWidth::WideContinuation
            && cb.width != ZzCellWidth::WideContinuation);
    if (ca.text != cb.text || ca.foreground != cb.foreground || ca.background != cb.background
        || ca.attributes != cb.attributes || !widthEqual) {
        ++g_failures;
        std::fprintf(stderr, "FAIL cell(%d,%d) %s: native{text=%s,w=%d} vs contour{text=%s,w=%d}\n",
                     row, col, what, ca.text.c_str(), (int)ca.width, cb.text.c_str(), (int)cb.width);
    }
}

// 比对整行前 n 格（放宽空单元格宽度类别；仅供 testResizeReflow /
// testResizeReflowCjk / testRowResizeParity 使用，其余用例一律走严格 checkRowEqual）。
void checkRowEqualAllowEmptyWidthDiff(const ZzTerminal& a, const ZzTerminal& b, int row, int n,
                                      const char* what)
{
    for (int col = 0; col < n; ++col)
        checkCellEqualAllowEmptyWidthDiff(a, b, row, col, what);
}

// 取历史行纯文本（M15 行变 parity 用例）。
std::string historyText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
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

// 5. CJK 宽字符（M2：两后端均为真实 UAX #11 宽度，恢复逐格强对照）。
// 样例码位取两后端 Unicode 数据中稳定为宽的常用 CJK 区间，规避版本漂移。
void testCjkWide()
{
    Dual d;
    d.feedBoth("\xE4\xB8\xAD" "A" "\xE4\xB8\x96"); // "中A世"
    checkRowEqual(d.native, d.contour, 0, 5, "cjk-wide");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
}

// 6. Alternate Screen（M2：两后端均实现 1049，内容恢复逐格强对照）。
// 光标断言为 b 类分别断言（两后端真实语义分歧，非转换层 bug，钉住不强行对齐）：
// xterm 语义 1049 = 1048 + 1047，1049l 恢复 1049h 保存的光标，native 对齐
// xterm 得 (0,4)；Contour 经 RenderBuffer 上报光标，实测自 1049h 起即不再上报
// （visible=false，position 为占位 (0,0)，?25h 亦不复现），属上游渲染光标
// 可见性条件差异（contour Terminal.cpp fillRenderBufferInternal 的页耦合判定）。
void testAltScreen()
{
    Dual d;
    d.feedBoth("MAIN\x1b[?1049h");
    ZZ_CHECK(d.native.isAlternateScreen());
    ZZ_CHECK(d.contour.isAlternateScreen());
    d.feedBoth("ALT");
    checkRowEqual(d.native, d.contour, 0, 3, "alt-write");
    d.feedBoth("\x1b[?1049l");
    ZZ_CHECK(!d.native.isAlternateScreen());
    ZZ_CHECK(!d.contour.isAlternateScreen());
    checkRowEqual(d.native, d.contour, 0, 4, "alt-restore");
    ZZ_CHECK(d.native.cursor().position == (ZzPosition { 0, 4 })); // native：xterm 语义恢复
    ZZ_CHECK(!d.contour.cursor().visible); // Contour：RenderBuffer 不上报光标（见函数头注释）
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

// 20. 行变 resize parity（M15）：缩行压历史与扩行回抽双后端一致。
// 屏幕比对走放宽空单元格宽度档：行变搬行后行尾空格命中本文件 :52 已钉住的
// b 类表示差异（native Empty vs contour Narrow，文本/颜色/属性一致）。
void testRowResizeParity()
{
    Dual d;
    for (int i = 0; i < 30; ++i)
        d.feedBoth("row-" + std::to_string(i) + "\r\n"); // 30 行进 24 行屏：双后端各 7 行历史
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());

    ZZ_CHECK(d.native.resize(80, 10) == d.contour.resize(80, 10)); // 缩 14：光标贴底 pushUp
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    ZZ_CHECK(d.native.historyView().lineCount() == 21); // 7 + 14
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        const std::string n = historyText(d.native, i);
        const std::string c = historyText(d.contour, i);
        ZZ_CHECK(n == c);
    }
    checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, 0, 9, "rowresize-shrink");

    ZZ_CHECK(d.native.resize(80, 24) == d.contour.resize(80, 24)); // 扩 14：光标贴底回抽
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    ZZ_CHECK(d.native.historyView().lineCount() == 7);
    checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, 0, 23, "rowresize-grow");
}

// 10. 光标可见性（DECTCEM ?25l/h；M2：两后端均上报真实值，恢复强对照）。
void testCursorVisibility()
{
    Dual d;
    d.feedBoth("AB\x1b[?25l");
    ZZ_CHECK(!d.native.cursor().visible);
    ZZ_CHECK(!d.contour.cursor().visible);
    d.feedBoth("\x1b[?25h");
    ZZ_CHECK(d.native.cursor().visible);
    ZZ_CHECK(d.contour.cursor().visible);
}

// 11. DECAWM ?7l：右边界覆写不换行（M2 新增强对照）。
void testAutoWrapMode()
{
    Dual d; // 80x24
    d.feedBoth("\x1b[?7l");
    std::string seq(80, 'X');
    seq += "YZ"; // 前 80 填满行 0；Y/Z 依次覆写最后一格
    d.feedBoth(seq);
    checkRowEqual(d.native, d.contour, 0, 80, "decawm-off");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
    ZZ_CHECK(d.native.renderView().lineAt(1).cellAt(0).text.empty());
    ZZ_CHECK(d.contour.renderView().lineAt(1).cellAt(0).text.empty());
}

// 12. 输入方向（M3a）：?1 application cursor 下 sendKey(Up) 两后端发出字节强对照。
void testInputApplicationCursor()
{
    Dual d;
    std::string nativeOut, contourOut;
    d.native.setOutputHandler([&](std::string_view b) { nativeOut.append(b); });
    d.contour.setOutputHandler([&](std::string_view b) { contourOut.append(b); });
    d.feedBoth("\x1b[?1h");
    ZzKeyEvent up;
    up.key = ZzKeyEvent::Key::Up;
    d.native.sendKey(up);
    d.contour.sendKey(up);
    ZZ_CHECK(nativeOut == "\x1bOA");
    ZZ_CHECK(nativeOut == contourOut);
    d.feedBoth("\x1b[?1l");
    nativeOut.clear();
    contourOut.clear();
    d.native.sendKey(up);
    d.contour.sendKey(up);
    ZZ_CHECK(nativeOut == "\x1b[A");
    ZZ_CHECK(nativeOut == contourOut);
    // Release 对称性：双后端均静默丢弃（xterm 默认不上报 Release），无新增字节。
    nativeOut.clear();
    contourOut.clear();
    up.action = ZzKeyEvent::Action::Release;
    d.native.sendKey(up);
    d.contour.sendKey(up);
    ZZ_CHECK(nativeOut.empty());
    ZZ_CHECK(contourOut.empty());
}

// 13. CPR（CSI 6n）：双后端写相同文本后应答强对照（1 起始）。
void testCursorPositionReport()
{
    Dual d;
    std::string nativeOut, contourOut;
    d.native.setOutputHandler([&](std::string_view b) { nativeOut.append(b); });
    d.contour.setOutputHandler([&](std::string_view b) { contourOut.append(b); });
    d.feedBoth("AB");
    d.feedBoth("\x1b[6n");
    ZZ_CHECK(nativeOut == "\x1b[1;3R");
    ZZ_CHECK(nativeOut == contourOut);
}

// 14. DA1 应答。差异研判（b 类，实现相关的应答串，非转换层 bug）：
// native 应答 VT102 级最小集（规格 4.3）；Contour 应答自有 DA 串（能力位不同，
// 应用据此启用特性，抬级归后续里程碑）。分别断言各自应答形态，注释钉住。
// Contour 实测应答为 ESC 开头 ?65;1;3;4;7;9;18;21;22;29;52;314c（2026-09-20 实测，
// 能力位高于 native 的 VT102 最小集）。
void testDeviceAttributes()
{
    Dual d;
    std::string nativeOut, contourOut;
    d.native.setOutputHandler([&](std::string_view b) { nativeOut.append(b); });
    d.contour.setOutputHandler([&](std::string_view b) { contourOut.append(b); });
    d.feedBoth("\x1b[c");
    ZZ_CHECK(nativeOut == "\x1b[?1;2c");
    ZZ_CHECK(!contourOut.empty());
    ZZ_CHECK(contourOut.starts_with("\x1b[?"));
}

ZzMouseEvent mouseEvent(ZzMouseAction action, ZzMouseButton button, int col, int row)
{
    ZzMouseEvent ev;
    ev.action = action;
    ev.button = button;
    ev.col = col;
    ev.row = row;
    return ev;
}

// 15. 鼠标 SGR 编码（M3b）：?1000h+?1006h 后 sendMouse 两后端发出字节强对照。
void testMouseSgrCompat()
{
    Dual d;
    std::string nativeOut, contourOut;
    d.native.setOutputHandler([&](std::string_view b) { nativeOut.append(b); });
    d.contour.setOutputHandler([&](std::string_view b) { contourOut.append(b); });
    d.feedBoth("\x1b[?1000h\x1b[?1006h");
    const ZzMouseEvent press = mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2);
    d.native.sendMouse(press);
    d.contour.sendMouse(press);
    ZZ_CHECK(nativeOut == "\x1b[<0;5;3M");
    ZZ_CHECK(nativeOut == contourOut);
    nativeOut.clear();
    contourOut.clear();
    const ZzMouseEvent release = mouseEvent(ZzMouseAction::Release, ZzMouseButton::Left, 4, 2);
    d.native.sendMouse(release);
    d.contour.sendMouse(release);
    ZZ_CHECK(nativeOut == "\x1b[<0;5;3m");
    ZZ_CHECK(nativeOut == contourOut);
}

// 16. bracketed paste（M3b）：?2004h 后 sendPaste 两后端包裹字节强对照。
void testBracketedPasteCompat()
{
    Dual d;
    std::string nativeOut, contourOut;
    d.native.setOutputHandler([&](std::string_view b) { nativeOut.append(b); });
    d.contour.setOutputHandler([&](std::string_view b) { contourOut.append(b); });
    d.feedBoth("\x1b[?2004h");
    d.native.sendPaste("hello");
    d.contour.sendPaste("hello");
    ZZ_CHECK(nativeOut == "\x1b[200~hello\x1b[201~");
    ZZ_CHECK(nativeOut == contourOut);
}

// 17. 焦点上报（M3b）：?1004h 后 sendFocus 两后端 CSI I/O 强对照。
void testFocusReportingCompat()
{
    Dual d;
    std::string nativeOut, contourOut;
    d.native.setOutputHandler([&](std::string_view b) { nativeOut.append(b); });
    d.contour.setOutputHandler([&](std::string_view b) { contourOut.append(b); });
    d.feedBoth("\x1b[?1004h");
    d.native.sendFocus(true);
    d.contour.sendFocus(true);
    ZZ_CHECK(nativeOut == "\x1b[I");
    ZZ_CHECK(nativeOut == contourOut);
    nativeOut.clear();
    contourOut.clear();
    d.native.sendFocus(false);
    d.contour.sendFocus(false);
    ZZ_CHECK(nativeOut == "\x1b[O");
    ZZ_CHECK(nativeOut == contourOut);
}

// 18. resize reflow：长行历史随列宽重组，两后端屏幕逐格比对。
void testResizeReflow()
{
    Dual d;
    std::string longLine(200, 'x');
    d.feedBoth(longLine + "\r\n");
    for (int i = 0; i < 30; ++i)
        d.feedBoth("filler\r\n");
    d.native.resize(40, 24);
    d.contour.resize(40, 24);
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 40, "reflow-40");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
    d.native.resize(80, 24);
    d.contour.resize(80, 24);
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 80, "reflow-80");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
}

// 19. CJK 长行 resize 后两后端逐格比对（宽字符边界规则对照）。
void testResizeReflowCjk()
{
    Dual d;
    std::string cjk;
    for (int i = 0; i < 45; ++i)
        cjk += "中文"; // 90 个宽字符共 180 列，80 列下折 3 行
    d.feedBoth(cjk + "\r\n");
    for (int i = 0; i < 30; ++i)
        d.feedBoth("filler\r\n");
    d.native.resize(37, 24); // 奇数列宽逼出宽字符边界钳制
    d.contour.resize(37, 24);
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 37, "reflow-cjk-37");
}

// 21. 硬行缩列 reflow parity（M16）：硬行缩列多行化、拉大接回，两后端
// 历史行数/历史文本/wrapped 标记/屏幕逐格一致——M4 钉住的硬行 b 类分歧消灭。
void testResizeReflowHardLine()
{
    Dual d;
    const std::string hard(60, 'h'); // 80 列下的硬行（60 字符 + 尾空白），未软折
    d.feedBoth(hard + "\r\n");
    for (int i = 0; i < 30; ++i)
        d.feedBoth("filler\r\n"); // 顶入历史

    d.native.resize(40, 24); // 缩列：硬行多行化（40 + 20 两行链）
    d.contour.resize(40, 24);
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        ZZ_CHECK(historyText(d.native, i) == historyText(d.contour, i));
        ZZ_CHECK(d.native.historyView().lineAt(i).wrapped()
                 == d.contour.historyView().lineAt(i).wrapped());
    }
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 40, "hardline-40");

    d.native.resize(80, 24); // 拉大：链接回，内容完整恢复
    d.contour.resize(80, 24);
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        ZZ_CHECK(historyText(d.native, i) == historyText(d.contour, i));
        ZZ_CHECK(d.native.historyView().lineAt(i).wrapped()
                 == d.contour.historyView().lineAt(i).wrapped());
    }
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 80, "hardline-80");
}

// 22. 跨缝链列变 parity（M16b）：链横跨历史/屏幕接缝，缩/拉两档后双后端
// 历史行数/文本/wrapped/屏幕逐格一致——native 归还机制 vs contour 统一流。
void testResizeReflowSeamChain()
{
    Dual d;
    const std::string head(80, 'a');
    const std::string tail(10, 'b');
    d.feedBoth(head + tail + "\r\n"); // 90 格链：80 列 autowrap 成 2 行，光标到行 2
    for (int i = 0; i < 22; ++i)
        d.feedBoth("filler\r\n");     // 恰好滚出 1 行：链头入历史、链尾留屏幕（跨缝）
    // 跨缝状态双后端钉住：历史末行 wrapped=true
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    ZZ_CHECK(d.native.historyView().lineCount() > 0);
    const std::size_t h0 = d.native.historyView().lineCount();
    ZZ_CHECK(d.native.historyView().lineAt(h0 - 1).wrapped());
    ZZ_CHECK(d.contour.historyView().lineAt(h0 - 1).wrapped());

    d.native.resize(60, 24); // 缩列：跨缝链接续保持（不劈开）
    d.contour.resize(60, 24);
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        ZZ_CHECK(historyText(d.native, i) == historyText(d.contour, i));
        ZZ_CHECK(d.native.historyView().lineAt(i).wrapped()
                 == d.contour.historyView().lineAt(i).wrapped());
    }
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 60, "seam-60");

    d.native.resize(80, 24); // 拉大：链接回
    d.contour.resize(80, 24);
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        ZZ_CHECK(historyText(d.native, i) == historyText(d.contour, i));
        ZZ_CHECK(d.native.historyView().lineAt(i).wrapped()
                 == d.contour.historyView().lineAt(i).wrapped());
    }
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 80, "seam-80");
}

// 23. 列变拉宽顶补 parity（M16c）：满屏折链拉宽，native 顶补与 contour
// 统一流尾部窗口净效果一致——历史行数/屏幕逐行文本/光标一致。
void testResizeReflowTopFill()
{
    ZzTerminal native(10, 4, ZzBackendKind::Native, 100);
    ZzTerminal contour(10, 4, ZzBackendKind::Contour, 100);
    for (auto* term : {&native, &contour}) {
        for (int i = 0; i < 6; ++i) {
            std::string s = "L" + std::to_string(i) + std::string(18, char('a' + i));
            term->feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(s.data()), s.size()));
            term->feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>("\r\n"), 2));
        }
        term->feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>("s1\r\ns2"), 6));
    }
    ZZ_CHECK(native.resize(20, 4));
    ZZ_CHECK(contour.resize(20, 4));
    ZZ_CHECK(native.historyView().lineCount() == contour.historyView().lineCount());
    ZZ_CHECK(native.historyView().lineCount() == 4);
    for (int r = 0; r < 4; ++r)
        checkRowEqualAllowEmptyWidthDiff(native, contour, r, 20, "topfill-20");
    ZZ_CHECK(native.cursor().position == contour.cursor().position);
}

// 24. 行列同变回归（M16d）：contour Grid::resize 先列后行，列向再扩宽横扫只
// 覆盖旧页高，行长暴露的窄存储备用行会被彩色写穿越界断言。adapter 拆成
// 先行后列两步规避；本用例钉住双后端在同变 + 彩色宽行 + 滚动下存活且内容完好。
void testResizeBothDimsColoredWide()
{
    ZzTerminal native(80, 24, ZzBackendKind::Native, 10000);
    ZzTerminal contour(80, 24, ZzBackendKind::Contour, 10000);
    const std::string line = std::string(40, 'a') + "\033[01;34m" + std::string(20, 'b')
                           + "\033[0m" + std::string(25, 'c') + "\r\n";
    for (auto* term : {&native, &contour}) {
        term->resize(79, 24);
        term->resize(271, 75); // 行列同增：崩溃现场手势
        for (int i = 0; i < 100; ++i)
            term->feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(line.data()), line.size()));
    }
    // 存活即主断言；内容完好性：屏幕 75 行应全满（100 行 85 格内容溢出滚动）。
    for (auto* term : {&native, &contour}) {
        ZZ_CHECK(term->renderView().size().cols == 271);
        ZZ_CHECK(term->renderView().size().rows == 75);
        ZZ_CHECK(term->historyView().lineCount() > 0);
        // 末行 \r\n 落在新空行（光标行）：0..73 满，74 空。
        for (int r = 0; r < 74; ++r) {
            const ZzLineView line = term->renderView().lineAt(r);
            std::string text;
            for (int c = 0; c < line.cellCount(); ++c)
                text += line.cellAt(c).text;
            ZZ_CHECK(!text.empty());
        }
    }
}

// 25. erase 斩链 parity 偏离登记（M17c）：整行擦除（EL 列 0 起）斩断折链——
// native 斩（M17c 语义：擦除行出链与前驱入链均置死，r0.wrapped 变 false）；
// contour 不斩（第三方冻结不改，r0.wrapped 保持 true）。b 类真实语义分歧，
// 分别断言钉住，不强行对齐。
void testEraseSeverDeviation()
{
    ZzTerminal native(20, 6, ZzBackendKind::Native, 100);
    ZzTerminal contour(20, 6, ZzBackendKind::Contour, 100);
    for (auto* term : {&native, &contour}) {
        const std::string chain(25, 'x'); // 20 列折链 r0(w)-r1，光标 (5,1)
        term->feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(chain.data()), chain.size()));
    }
    // 斩链前提：双后端 r0 均在链上（r0(w)-r1 同一折链）
    ZZ_CHECK(native.renderView().lineAt(0).wrapped());
    ZZ_CHECK(contour.renderView().lineAt(0).wrapped());
    for (auto* term : {&native, &contour}) {
        term->feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>("\r\x1b[K"), 4)); // 光标回列 0 整行擦 r1
    }
    ZZ_CHECK(!native.renderView().lineAt(0).wrapped());  // native：M17c 斩链
    ZZ_CHECK(contour.renderView().lineAt(0).wrapped());  // contour：不斩，登记偏离
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
    testAutoWrapMode();
    testInputApplicationCursor();
    testCursorPositionReport();
    testDeviceAttributes();
    testMouseSgrCompat();
    testBracketedPasteCompat();
    testFocusReportingCompat();
    testResizeReflow();
    testResizeReflowCjk();
    testResizeReflowHardLine();
    testResizeReflowSeamChain();
    testResizeReflowTopFill();
    testResizeBothDimsColoredWide();
    testRowResizeParity();
    testEraseSeverDeviation();
    if (g_failures != 0)
        std::fprintf(stderr, "test_backend_compat: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
