// native 鼠标/粘贴/焦点输入链路（M3b）：模式接线后经 facade send + output 捕获。
// 仅公开 API。
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

ZzMouseEvent mouseEvent(ZzMouseAction action, ZzMouseButton button, int col, int row)
{
    ZzMouseEvent ev;
    ev.action = action;
    ev.button = button;
    ev.col = col;
    ev.row = row;
    return ev;
}

// ?1000h+?1006h 后 sendMouse 发 SGR 编码；未开模式静默丢弃。
void testMouseModes()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    t.sendMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2));
    ZZ_CHECK(out.empty()); // 模式未开
    feed(t, "\x1B[?1000h\x1B[?1006h");
    t.sendMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2));
    ZZ_CHECK(out == "\x1B[<0;5;3M");
    out.clear();
    t.sendMouse(mouseEvent(ZzMouseAction::Release, ZzMouseButton::Left, 4, 2));
    ZZ_CHECK(out == "\x1B[<0;5;3m");
    out.clear();
    feed(t, "\x1B[?1006l"); // 回经典编码
    t.sendMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2));
    ZZ_CHECK(out == "\x1B[M \x25\x23");
    out.clear();
    feed(t, "\x1B[?1000l"); // 关模式
    t.sendMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2));
    ZZ_CHECK(out.empty());
}

// 鼠标模式互斥：?1000h 后 ?1002h → ButtonEvent（无按钮移动不上报）。
void testMouseModeExclusive()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "\x1B[?1003h"); // AnyEvent
    t.sendMouse(mouseEvent(ZzMouseAction::Move, ZzMouseButton::None, 1, 1));
    ZZ_CHECK(!out.empty());
    out.clear();
    feed(t, "\x1B[?1002h"); // 切 ButtonEvent（覆盖 AnyEvent）
    t.sendMouse(mouseEvent(ZzMouseAction::Move, ZzMouseButton::None, 1, 1));
    ZZ_CHECK(out.empty());
}

// ?2004h 后 sendPaste 包裹 200~/201~；关闭后透传。
void testBracketedPaste()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    t.sendPaste("abc");
    ZZ_CHECK(out == "abc");
    out.clear();
    feed(t, "\x1B[?2004h");
    t.sendPaste("abc");
    ZZ_CHECK(out == "\x1B[200~abc\x1B[201~");
    out.clear();
    feed(t, "\x1B[?2004l");
    t.sendPaste("abc");
    ZZ_CHECK(out == "abc");
}

// ?1004h 后 sendFocus 发 CSI I/O；未开静默。
void testFocusReporting()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    t.sendFocus(true);
    ZZ_CHECK(out.empty());
    feed(t, "\x1B[?1004h");
    t.sendFocus(true);
    ZZ_CHECK(out == "\x1B[I");
    out.clear();
    t.sendFocus(false);
    ZZ_CHECK(out == "\x1B[O");
    out.clear();
    feed(t, "\x1B[?1004l");
    t.sendFocus(true);
    ZZ_CHECK(out.empty());
}

// handler 未设置：三 send 静默不崩。
void testNoHandlerSafe()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "\x1B[?1000h\x1B[?2004h\x1B[?1004h");
    t.sendMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 1, 1));
    t.sendPaste("x");
    t.sendFocus(true);
    ZZ_CHECK(true);
}

} // namespace

int main()
{
    testMouseModes();
    testMouseModeExclusive();
    testBracketedPaste();
    testFocusReporting();
    testNoHandlerSafe();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_mouse_input: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
