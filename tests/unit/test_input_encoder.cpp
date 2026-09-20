// ZzInputEncoder 单测（M3a 补齐 M1 零覆盖）：encodeText/encodeKey 全表 +
// 模式位影响 + Character 修饰键编码。编码期望值以 xterm ctlseqs 为准。
#include <ZzTerm/Input.h>

#include <cstdio>
#include <string>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

ZzKeyEvent keyEvent(ZzKeyEvent::Key key, ZzKeyModifier mods = ZzKeyModifier::None,
                    char32_t ch = 0)
{
    ZzKeyEvent ev;
    ev.key = key;
    ev.character = ch;
    ev.modifiers = mods;
    return ev;
}

// 普通文本透传（含 UTF-8 多字节）。
void testEncodeText()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeText("hello") == "hello");
    ZZ_CHECK(enc.encodeText("\xE4\xB8\xAD\xE6\x96\x87") == "\xE4\xB8\xAD\xE6\x96\x87"); // UTF-8 原样透传
    ZZ_CHECK(enc.encodeText("") == "");
}

// 方向键：普通 CSI / application SS3 / 修饰键 CSI 1;mod X。
void testArrows()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Up)) == "\x1B[A");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Down)) == "\x1B[B");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Right)) == "\x1B[C");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Left)) == "\x1B[D");
    enc.setApplicationCursorKeys(true);
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Up)) == "\x1BOA");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Down)) == "\x1BOB");
    // 修饰键优先于 application 模式（xterm：带修饰恒 CSI 1;mod X）
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Up, ZzKeyModifier::Shift)) == "\x1B[1;2A");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Up, ZzKeyModifier::Ctrl)) == "\x1B[1;5A");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Up,
                                    ZzKeyModifier::Shift | ZzKeyModifier::Ctrl)) == "\x1B[1;6A");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Up, ZzKeyModifier::Alt)) == "\x1B[1;3A");
}

// Home/End：与方向键同规则（H/F final）。
void testHomeEnd()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Home)) == "\x1B[H");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::End)) == "\x1B[F");
    enc.setApplicationCursorKeys(true);
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Home)) == "\x1BOH");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::End, ZzKeyModifier::Ctrl)) == "\x1B[1;5F");
}

// Insert/Delete/PgUp/PgDn/F5-F12：CSI num [;mod] ~。
void testTildeKeys()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Insert)) == "\x1B[2~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Delete)) == "\x1B[3~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::PageUp)) == "\x1B[5~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::PageDown)) == "\x1B[6~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F5)) == "\x1B[15~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F6)) == "\x1B[17~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F7)) == "\x1B[18~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F8)) == "\x1B[19~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F9)) == "\x1B[20~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F10)) == "\x1B[21~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F11)) == "\x1B[23~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F12)) == "\x1B[24~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Delete, ZzKeyModifier::Shift)) == "\x1B[3;2~");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F5, ZzKeyModifier::Ctrl)) == "\x1B[15;5~");
}

// F1-F4：SS3 P/Q/R/S；带修饰键退化为 CSI 1;mod P/Q/R/S。
void testF1ToF4()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F1)) == "\x1BOP");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F2)) == "\x1BOQ");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F3)) == "\x1BOR");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F4)) == "\x1BOS");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F1, ZzKeyModifier::Shift)) == "\x1B[1;2P");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::F4, ZzKeyModifier::Alt)) == "\x1B[1;3S");
}

// Enter/Tab/Backspace/Escape 单字节；Release 动作返回空串。
void testSingleByteKeys()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Enter)) == "\r");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Tab)) == "\t");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Backspace)) == "\x7F");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Escape)) == "\x1B");
    ZzKeyEvent rel = keyEvent(ZzKeyEvent::Key::Up);
    rel.action = ZzKeyEvent::Action::Release;
    ZZ_CHECK(enc.encodeKey(rel).empty());
    ZzKeyEvent rep = keyEvent(ZzKeyEvent::Key::Up);
    rep.action = ZzKeyEvent::Action::Repeat;
    ZZ_CHECK(enc.encodeKey(rep) == "\x1B[A"); // Repeat 编码同 Press
}

// Character：无修饰透传 UTF-8；Ctrl+字母 → C0 控制字节；Alt → ESC 前缀。
void testCharacterWithModifiers()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ZzKeyModifier::None, U'a')) == "a");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ZzKeyModifier::None, U'中'))
             == "\xE4\xB8\xAD");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ZzKeyModifier::Ctrl, U'c'))
             == "\x03"); // Ctrl+C
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ZzKeyModifier::Ctrl, U'A'))
             == "\x01"); // 大小写同控制字节
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ZzKeyModifier::Alt, U'x'))
             == "\x1Bx");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character,
                                    ZzKeyModifier::Alt | ZzKeyModifier::Ctrl, U'c')) == "\x1B\x03");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ZzKeyModifier::None, 0)).empty());
}

// 模式位 setter/getter（keypad 暂无编码消费方，仅验证状态同步）。
void testModeBits()
{
    ZzInputEncoder enc;
    ZZ_CHECK(!enc.applicationCursorKeys());
    enc.setApplicationCursorKeys(true);
    ZZ_CHECK(enc.applicationCursorKeys());
    enc.setApplicationKeypad(true);
    ZZ_CHECK(enc.applicationKeypad());
    ZZ_CHECK(!enc.bracketedPaste());
    ZZ_CHECK(enc.mouseReportMode() == ZzMouseReportMode::None);
    ZZ_CHECK(!enc.mouseSgrEncoding());
    ZZ_CHECK(!enc.focusReporting());
}

ZzMouseEvent mouseEvent(ZzMouseAction action, ZzMouseButton button, int col, int row,
                        ZzKeyModifier mods = ZzKeyModifier::None)
{
    ZzMouseEvent ev;
    ev.action = action;
    ev.button = button;
    ev.col = col;
    ev.row = row;
    ev.modifiers = mods;
    return ev;
}

// encodeMouse 模式过滤：None 全禁；X10 仅按下；Normal 无移动；
// ButtonEvent 仅按下时拖动；AnyEvent 任意移动。
void testMouseModeFiltering()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2)).empty());
    enc.setMouseReportMode(ZzMouseReportMode::X10);
    ZZ_CHECK(!enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2)).empty());
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Release, ZzMouseButton::Left, 4, 2)).empty());
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Move, ZzMouseButton::None, 4, 2)).empty());
    enc.setMouseReportMode(ZzMouseReportMode::Normal);
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Move, ZzMouseButton::Left, 4, 2)).empty());
    enc.setMouseReportMode(ZzMouseReportMode::ButtonEvent);
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Move, ZzMouseButton::None, 4, 2)).empty());
    ZZ_CHECK(!enc.encodeMouse(mouseEvent(ZzMouseAction::Move, ZzMouseButton::Left, 4, 2)).empty());
    enc.setMouseReportMode(ZzMouseReportMode::AnyEvent);
    ZZ_CHECK(!enc.encodeMouse(mouseEvent(ZzMouseAction::Move, ZzMouseButton::None, 4, 2)).empty());
}

// 经典编码：CSI M Cb Cx Cy（各 +32）；坐标 1 起始；释放码 3；修饰位 4/8/16。
void testMouseClassicEncoding()
{
    ZzInputEncoder enc;
    enc.setMouseReportMode(ZzMouseReportMode::Normal);
    // 左键按下 (col 4, row 2) → x=5 y=3：CSI M <空格> % #
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2))
             == "\x1B[M \x25\x23");
    // 释放 → 按钮码 3（'#'=35）
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Release, ZzMouseButton::Left, 4, 2))
             == "\x1B[M#\x25\x23");
    // 中键/右键码 1/2
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Middle, 0, 0))
             == "\x1B[M!\x21\x21");
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Right, 0, 0))
             == "\x1B[M\"\x21\x21");
    // 滚轮码 64/65
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::WheelUp, 0, 0))
             == "\x1B[M`\x21\x21");
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::WheelDown, 0, 0))
             == "\x1B[Ma\x21\x21");
    // Shift 修饰 +4、Ctrl 修饰 +16
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 0, 0,
                                        ZzKeyModifier::Shift)) == "\x1B[M$\x21\x21");
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 0, 0,
                                        ZzKeyModifier::Ctrl)) == "\x1B[M0\x21\x21");
}

// 无按钮移动（AnyEvent）：xterm 码 35（32+3），Cb = 32+35 = 67 = 'C'。
void testMouseMotionNoButton()
{
    ZzInputEncoder enc;
    enc.setMouseReportMode(ZzMouseReportMode::AnyEvent);
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Move, ZzMouseButton::None, 4, 2))
             == "\x1B[MC\x25\x23");
}

// SGR 1006 编码：CSI < code ; x ; y M/m；释放 m 结尾。
void testMouseSgrEncoding()
{
    ZzInputEncoder enc;
    enc.setMouseReportMode(ZzMouseReportMode::Normal);
    enc.setMouseSgrEncoding(true);
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 4, 2))
             == "\x1B[<0;5;3M");
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Release, ZzMouseButton::Left, 4, 2))
             == "\x1B[<0;5;3m");
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::WheelDown, 0, 0,
                                        ZzKeyModifier::Shift)) == "\x1B[<69;1;1M");
    // 经典编码坐标上限 223 不适用 SGR
    ZZ_CHECK(!enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 300, 2)).empty());
}

// 经典编码坐标超 223 丢弃。
void testMouseClassicCoordinateLimit()
{
    ZzInputEncoder enc;
    enc.setMouseReportMode(ZzMouseReportMode::Normal);
    ZZ_CHECK(enc.encodeMouse(mouseEvent(ZzMouseAction::Press, ZzMouseButton::Left, 300, 2)).empty());
}

// encodePaste：2004 开包裹 200~/201~，关透传。
void testEncodePaste()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodePaste("abc") == "abc");
    enc.setBracketedPaste(true);
    ZZ_CHECK(enc.encodePaste("abc") == "\x1B[200~abc\x1B[201~");
    ZZ_CHECK(enc.encodePaste("a\rb") == "\x1B[200~a\rb\x1B[201~"); // 内容原样（含控制字节）
}

// encodeFocus：1004 开 CSI I/O，关空串。
void testEncodeFocus()
{
    ZzInputEncoder enc;
    ZZ_CHECK(enc.encodeFocus(true).empty());
    ZZ_CHECK(enc.encodeFocus(false).empty());
    enc.setFocusReporting(true);
    ZZ_CHECK(enc.encodeFocus(true) == "\x1B[I");
    ZZ_CHECK(enc.encodeFocus(false) == "\x1B[O");
}

// Ctrl+非字母 C0 映射（xterm：Ctrl+Space/@=NUL，[\]^_ 对应 0x1B-0x1F，Ctrl+?=DEL）。
void testCtrlNonLetterC0()
{
    ZzInputEncoder enc;
    const auto ctrl = ZzKeyModifier::Ctrl;
    // 期望串含 NUL，需显式长度构造（"==" 对 const char* 按 strlen 截断）。
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U' ')) == std::string(1, '\x00'));
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U'@')) == std::string(1, '\x00'));
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U'[')) == "\x1B");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U'\\')) == "\x1C");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U']')) == "\x1D");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U'^')) == "\x1E");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U'_')) == "\x1F");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U'?')) == "\x7F");
    // 未定义的非字母（如 Ctrl+1）透传原字符
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U'1')) == "1");
    // Alt+Ctrl+[ → ESC 前缀 + C0
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character,
                                    ZzKeyModifier::Alt | ZzKeyModifier::Ctrl, U'[')) == "\x1B\x1B");
}

} // namespace

int main()
{
    testEncodeText();
    testArrows();
    testHomeEnd();
    testTildeKeys();
    testF1ToF4();
    testSingleByteKeys();
    testCharacterWithModifiers();
    testModeBits();
    testMouseModeFiltering();
    testMouseClassicEncoding();
    testMouseMotionNoButton();
    testMouseSgrEncoding();
    testMouseClassicCoordinateLimit();
    testEncodePaste();
    testEncodeFocus();
    testCtrlNonLetterC0();
    if (g_failures != 0)
        std::fprintf(stderr, "test_input_encoder: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
