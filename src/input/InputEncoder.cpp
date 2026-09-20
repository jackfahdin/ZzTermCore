#include "ZzTerm/Input.h"

// ZzInputEncoder 实现骨架（M0）。
//
// 覆盖：普通文本透传、方向键/Home/End/Insert/Delete/PgUp/PgDn/F1-F12
// （含 application cursor 模式与 xterm 修饰键参数）、SGR(1006) 与经典
// 鼠标编码、bracketed paste、focus reporting。
//
// 注：文件内的 appendUtf8 是局部 UTF-8 编码小工具，仅服务按键字符输出；
// 正式的 UTF-8/Unicode 数据由 unicode 模块提供，届时可替换为模块 API。

namespace {

/// @brief 将单个码位编码为 UTF-8 追加到 out（局部工具，见文件头注释）。
void appendUtf8(std::string& out, char32_t cp)
{
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// @brief xterm 修饰键参数：Shift=2, Alt=3, Alt+Shift=4, Ctrl=5, ...（1 + 位值和）。
int modifierParam(ZzKeyModifier mods)
{
    int v = 0;
    if (zzHasModifier(mods, ZzKeyModifier::Shift)) v += 1;
    if (zzHasModifier(mods, ZzKeyModifier::Alt))   v += 2;
    if (zzHasModifier(mods, ZzKeyModifier::Ctrl))  v += 4;
    return v + 1; // 无修饰键时为 1，调用方据此决定是否带参数。
}

} // namespace

std::string ZzInputEncoder::encodeText(std::string_view utf8) const
{
    return std::string(utf8);
}

std::string ZzInputEncoder::encodeKey(const ZzKeyEvent& event) const
{
    // xterm 默认只上报按下/重复；Release 为未来键盘协议预留。
    if (event.action == ZzKeyEvent::Action::Release)
        return {};

    const int modParam = modifierParam(event.modifiers);
    const bool hasMods = modParam != 1;
    std::string out;

    using Key = ZzKeyEvent::Key;
    switch (event.key) {
    case Key::Character: {
        if (event.character == 0)
            return out;
        // Ctrl+字母 → C0 控制字节（xterm：Ctrl+C = 0x03，大小写同值）；
        // Alt → ESC 前缀（Meta 语义）。其余修饰组合按无修饰透传。
        const bool ctrl = zzHasModifier(event.modifiers, ZzKeyModifier::Ctrl);
        const bool alt = zzHasModifier(event.modifiers, ZzKeyModifier::Alt);
        const bool letter = (event.character >= U'a' && event.character <= U'z')
                         || (event.character >= U'A' && event.character <= U'Z');
        if (alt)
            out.push_back('\x1B');
        if (ctrl && letter)
            out.push_back(static_cast<char>(event.character & 0x1F));
        else
            appendUtf8(out, event.character);
        return out;
    }
    case Key::Enter:     return "\r";
    case Key::Tab:       return "\t";
    case Key::Backspace: return "\x7F";
    case Key::Escape:    return "\x1B";

    case Key::Up: case Key::Down: case Key::Left: case Key::Right: {
        const char finalChar = event.key == Key::Up ? 'A'
                             : event.key == Key::Down ? 'B'
                             : event.key == Key::Right ? 'C' : 'D';
        if (hasMods) {
            out = "\x1B[1;";
            out += static_cast<char>('0' + modParam);
            out.push_back(finalChar);
        } else if (appCursorKeys_) {
            out = "\x1BO";
            out.push_back(finalChar); // SS3 形式（DECCKM）。
        } else {
            out = "\x1B[";
            out.push_back(finalChar);
        }
        return out;
    }

    case Key::Home: case Key::End: {
        const char finalChar = event.key == Key::Home ? 'H' : 'F';
        if (hasMods) {
            out = "\x1B[1;";
            out += static_cast<char>('0' + modParam);
            out.push_back(finalChar);
        } else if (appCursorKeys_) {
            out = "\x1BO";
            out.push_back(finalChar);
        } else {
            out = "\x1B[";
            out.push_back(finalChar);
        }
        return out;
    }

    case Key::Insert: case Key::Delete:
    case Key::PageUp: case Key::PageDown:
    case Key::F5: case Key::F6: case Key::F7: case Key::F8:
    case Key::F9: case Key::F10: case Key::F11: case Key::F12: {
        // CSI <num> [;mod] ~
        int num = 0;
        switch (event.key) {
        case Key::Insert:   num = 2;  break;
        case Key::Delete:   num = 3;  break;
        case Key::PageUp:   num = 5;  break;
        case Key::PageDown: num = 6;  break;
        case Key::F5:       num = 15; break;
        case Key::F6:       num = 17; break;
        case Key::F7:       num = 18; break;
        case Key::F8:       num = 19; break;
        case Key::F9:       num = 20; break;
        case Key::F10:      num = 21; break;
        case Key::F11:      num = 23; break;
        case Key::F12:      num = 24; break;
        default: break;
        }
        out = "\x1B[";
        out += std::to_string(num);
        if (hasMods) {
            out.push_back(';');
            out += std::to_string(modParam);
        }
        out.push_back('~');
        return out;
    }

    case Key::F1: case Key::F2: case Key::F3: case Key::F4: {
        // SS3 形式（ESC O P/Q/R/S）；带修饰键时退化为 CSI 1;mod P/Q/R/S。
        const char finalChar = event.key == Key::F1 ? 'P'
                             : event.key == Key::F2 ? 'Q'
                             : event.key == Key::F3 ? 'R' : 'S';
        if (hasMods) {
            out = "\x1B[1;";
            out += static_cast<char>('0' + modParam);
            out.push_back(finalChar);
        } else {
            out = "\x1BO";
            out.push_back(finalChar);
        }
        return out;
    }
    }
    return {};
}

std::string ZzInputEncoder::encodeMouse(const ZzMouseEvent& event) const
{
    if (mouseMode_ == ZzMouseReportMode::None)
        return {};

    // X10 只上报按下。
    if (mouseMode_ == ZzMouseReportMode::X10 && event.action != ZzMouseAction::Press)
        return {};
    // Normal 不上报移动；ButtonEvent 只上报按下时拖动。
    if (event.action == ZzMouseAction::Move) {
        if (mouseMode_ == ZzMouseReportMode::Normal)
            return {};
        if (mouseMode_ == ZzMouseReportMode::ButtonEvent &&
            event.button == ZzMouseButton::None)
            return {};
    }

    // 按钮码（xterm 协议位布局）。
    int code = 0;
    switch (event.button) {
    case ZzMouseButton::Left:       code = 0; break;
    case ZzMouseButton::Middle:     code = 1; break;
    case ZzMouseButton::Right:      code = 2; break;
    case ZzMouseButton::WheelUp:    code = 64; break;
    case ZzMouseButton::WheelDown:  code = 65; break;
    case ZzMouseButton::WheelLeft:  code = 66; break;
    case ZzMouseButton::WheelRight: code = 67; break;
    case ZzMouseButton::None:       code = 0; break;
    }
    if (event.action == ZzMouseAction::Release)
        code = 3; // 经典编码释放为按钮 3；SGR 编码用 'm' 结尾。
    if (event.action == ZzMouseAction::Move)
        code |= 32;
    if (zzHasModifier(event.modifiers, ZzKeyModifier::Shift)) code |= 4;
    if (zzHasModifier(event.modifiers, ZzKeyModifier::Alt))   code |= 8;
    if (zzHasModifier(event.modifiers, ZzKeyModifier::Ctrl))  code |= 16;

    const int x = event.col + 1; // 协议坐标 1 起始。
    const int y = event.row + 1;

    std::string out;
    if (mouseSgr_) {
        // SGR 1006：CSI < code ; x ; y M/m
        out = "\x1B[<";
        out += std::to_string(code);
        out.push_back(';');
        out += std::to_string(x);
        out.push_back(';');
        out += std::to_string(y);
        out.push_back(event.action == ZzMouseAction::Release ? 'm' : 'M');
    } else {
        // 经典编码：CSI M Cb Cx Cy（各 +32）；超出 223 的坐标不支持（协议限制）。
        if (x > 223 || y > 223)
            return {};
        out = "\x1B[M";
        out.push_back(static_cast<char>(32 + code));
        out.push_back(static_cast<char>(32 + x));
        out.push_back(static_cast<char>(32 + y));
    }
    return out;
}

std::string ZzInputEncoder::encodePaste(std::string_view utf8) const
{
    if (!bracketedPaste_)
        return std::string(utf8);
    std::string out = "\x1B[200~";
    out += utf8;
    out += "\x1B[201~";
    return out;
}

std::string ZzInputEncoder::encodeFocus(bool focused) const
{
    if (!focusReporting_)
        return {};
    return focused ? "\x1B[I" : "\x1B[O";
}

void ZzInputEncoder::setApplicationCursorKeys(bool on) noexcept { appCursorKeys_ = on; }
void ZzInputEncoder::setApplicationKeypad(bool on) noexcept { appKeypad_ = on; }
void ZzInputEncoder::setMouseReportMode(ZzMouseReportMode mode) noexcept { mouseMode_ = mode; }
void ZzInputEncoder::setMouseSgrEncoding(bool on) noexcept { mouseSgr_ = on; }
void ZzInputEncoder::setBracketedPaste(bool on) noexcept { bracketedPaste_ = on; }
void ZzInputEncoder::setFocusReporting(bool on) noexcept { focusReporting_ = on; }
