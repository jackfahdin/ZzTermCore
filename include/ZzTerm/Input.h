#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "ZzTerm/Export.h"

/**
 * @file Input.h
 * @brief 输入语义事件与 ZzInputEncoder（UI 不直接拼 escape sequence）。
 *
 * 约束（Architecture.md 第 9 节）：
 * - 前端以 ZzKeyEvent / ZzMouseEvent / Paste / Focus 等语义事件进入
 *   ZzInputEncoder，由 Encoder 依据当前终端模式（application cursor/
 *   keypad、mouse reporting、bracketed paste、focus reporting）生成字节；
 * - IME composition 属于前端，只有 commit text 经由 encodeText 进入 Core；
 * - 模式位由 ZzTerminal 从 DEC/xterm mode 变化同步到 Encoder。
 */

/// @brief 键盘修饰键（位掩码，可组合）。
enum class ZzKeyModifier : std::uint8_t {
    None  = 0,
    Shift = 1 << 0,
    Alt   = 1 << 1,
    Ctrl  = 1 << 2,
    Super = 1 << 3 ///< Windows/Command 键。
};

/**
 * @brief 修饰键按位或（组合多个修饰键）。
 * @param a 左侧修饰键组合。
 * @param b 右侧修饰键组合。
 * @return a 与 b 的位或结果。
 */
[[nodiscard]] constexpr ZzKeyModifier operator|(ZzKeyModifier a, ZzKeyModifier b) noexcept
{
    return static_cast<ZzKeyModifier>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/**
 * @brief 修饰键按位与（交集检测用）。
 * @param a 左侧修饰键组合。
 * @param b 右侧修饰键组合。
 * @return a 与 b 的位与结果。
 */
[[nodiscard]] constexpr ZzKeyModifier operator&(ZzKeyModifier a, ZzKeyModifier b) noexcept
{
    return static_cast<ZzKeyModifier>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

/**
 * @brief 判断修饰键组合中是否含某修饰键。
 * @param set 修饰键组合。
 * @param m 待检测的修饰键。
 * @return true 表示 set 中包含 m。
 */
[[nodiscard]] constexpr bool zzHasModifier(ZzKeyModifier set, ZzKeyModifier m) noexcept
{
    return (set & m) != ZzKeyModifier::None;
}

/**
 * @brief 键盘语义事件。
 *
 * 普通文本输入（含 IME commit）优先走 ZzInputEncoder::encodeText；
 * 本事件面向“按键”语义：功能键、组合键及需要区分按下/释放的场景。
 */
struct ZzKeyEvent {
    /// @brief 按键动作。
    enum class Action : std::uint8_t {
        Press,   ///< 按下。
        Repeat,  ///< 长按重复（编码同 Press）。
        Release  ///< 释放（xterm 默认不上报；为 kitty 键盘协议等未来扩展预留）。
    };

    /// @brief 按键标识。
    enum class Key : std::uint8_t {
        Character,  ///< 可打印字符，码位见 character 字段。
        Enter,
        Tab,
        Backspace,
        Escape,
        Up, Down, Left, Right,
        Home, End,
        Insert, Delete,
        PageUp, PageDown,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
    };

    Key           key       = Key::Character;   ///< 按键。
    Action        action    = Action::Press;    ///< 动作。
    char32_t      character = 0;                ///< key == Character 时的 Unicode 码位。
    ZzKeyModifier modifiers = ZzKeyModifier::None; ///< 修饰键组合。
};

/// @brief 鼠标按钮。
enum class ZzMouseButton : std::uint8_t {
    None,       ///< 无按钮（纯移动）。
    Left,
    Middle,
    Right,
    WheelUp,
    WheelDown,
    WheelLeft,  ///< 横向滚轮（部分协议支持，预留）。
    WheelRight
};

/// @brief 鼠标动作。
enum class ZzMouseAction : std::uint8_t {
    Press,
    Release,
    Move
};

/// @brief 鼠标语义事件（网格坐标，0 起始）。
struct ZzMouseEvent {
    ZzMouseAction action    = ZzMouseAction::Move; ///< 动作（按下/释放/移动）。
    ZzMouseButton button    = ZzMouseButton::None; ///< 触发按钮；纯移动事件为 None。
    int           col       = 0; ///< 列号（0 起始，Encoder 负责换算为协议 1 起始）。
    int           row       = 0; ///< 行号（0 起始）。
    ZzKeyModifier modifiers = ZzKeyModifier::None; ///< 事件发生时的修饰键组合。
};

/// @brief 鼠标上报模式（枚举值为序号；注释标注对应 xterm DECSET 编号）。
enum class ZzMouseReportMode : std::uint8_t {
    None        = 0, ///< 不上报。
    X10         = 1, ///< X10（DECSET 9，仅按下）。
    Normal      = 2, ///< Normal（DECSET 1000，按下 + 释放）。
    ButtonEvent = 3, ///< Button-event（DECSET 1002，+ 按下时拖动）。
    AnyEvent    = 4  ///< Any-event（DECSET 1003，+ 任意移动）。
};

/**
 * @brief 输入编码器：语义事件 -> 终端字节流。
 *
 * ownership：值语义对象，调用方持有；返回的字节串由调用方拥有，
 * 通常直接写入 PTY/连接。
 *
 * 线程安全：非线程安全，与 Terminal 同线程使用；模式位由 Terminal
 * 同步（set* 系列方法）。
 */
class ZZTERM_API ZzInputEncoder {
public:
    ZzInputEncoder() = default;

    /**
     * @brief 编码普通文本（Unicode 输入、IME commit text）。
     * @param utf8 已确认的 UTF-8 文本（前端/IME 必须保证合法 UTF-8）。
     * @return 待发送字节；当前实现原样透传。
     */
    [[nodiscard]] std::string encodeText(std::string_view utf8) const;

    /**
     * @brief 编码按键事件。
     * @param event 键盘语义事件。
     * @return 待发送字节；事件不产生输出（如未知 Release）时返回空串。
     * @note application cursor/keypad 模式影响方向键等功能键编码。
     */
    [[nodiscard]] std::string encodeKey(const ZzKeyEvent& event) const;

    /**
     * @brief 编码鼠标事件。
     * @param event 鼠标语义事件。
     * @return 待发送字节；当前上报模式下不该上报时返回空串。
     * @note 坐标协议为 1 起始 + 32 偏移（X10/Normal）或 SGR 1006 格式，
     *       由本方法依据 setMouseSgrEncoding 选择。
     */
    [[nodiscard]] std::string encodeMouse(const ZzMouseEvent& event) const;

    /**
     * @brief 编码粘贴文本（bracketed paste 2004 开启时包裹 200~/201~）。
     * @param utf8 粘贴文本（合法 UTF-8）。
     * @return 待发送字节。
     */
    [[nodiscard]] std::string encodePaste(std::string_view utf8) const;

    /**
     * @brief 编码焦点事件（focus reporting 1004 开启时输出 CSI I/O）。
     * @param focused true = 获得焦点；false = 失去焦点。
     * @return 待发送字节；未开启 focus reporting 时返回空串。
     */
    [[nodiscard]] std::string encodeFocus(bool focused) const;

    // ---- 模式同步（由 ZzTerminal 调用） ----

    /// @brief Application Cursor Keys（DECCKM）。
    /// @param on true 开启 application 模式，方向键等编码切换为 SS3 序列。
    void setApplicationCursorKeys(bool on) noexcept;
    /// @brief Application Keypad（DECPAM/DECPNM）。
    /// @param on true 开启 application keypad 模式。
    void setApplicationKeypad(bool on) noexcept;
    /// @brief 鼠标上报模式。
    /// @param mode 目标上报模式（对应 xterm DECSET 编号见枚举注释）。
    void setMouseReportMode(ZzMouseReportMode mode) noexcept;
    /// @brief 鼠标编码格式：true = SGR 1006，false = 经典 X10/Normal 编码。
    /// @param on true 使用 SGR 1006 编码。
    void setMouseSgrEncoding(bool on) noexcept;
    /// @brief Bracketed Paste（2004）。
    /// @param on true 开启 bracketed paste 包裹。
    void setBracketedPaste(bool on) noexcept;
    /// @brief Focus Reporting（1004）。
    /// @param on true 开启焦点事件上报。
    void setFocusReporting(bool on) noexcept;

    /// @brief 是否处于 Application Cursor Keys 模式（DECCKM）。
    /// @return 当前模式状态。
    [[nodiscard]] bool applicationCursorKeys() const noexcept { return appCursorKeys_; }
    /// @brief 是否处于 Application Keypad 模式（DECPAM）。
    /// @return 当前模式状态。
    [[nodiscard]] bool applicationKeypad() const noexcept { return appKeypad_; }
    /// @brief 当前鼠标上报模式。
    /// @return 当前上报模式。
    [[nodiscard]] ZzMouseReportMode mouseReportMode() const noexcept { return mouseMode_; }
    /// @brief 当前鼠标编码格式。
    /// @return true 表示 SGR 1006，false 表示经典 X10/Normal 编码。
    [[nodiscard]] bool mouseSgrEncoding() const noexcept { return mouseSgr_; }
    /// @brief 是否开启 Bracketed Paste（2004）。
    /// @return 当前模式状态。
    [[nodiscard]] bool bracketedPaste() const noexcept { return bracketedPaste_; }
    /// @brief 是否开启 Focus Reporting（1004）。
    /// @return 当前模式状态。
    [[nodiscard]] bool focusReporting() const noexcept { return focusReporting_; }

private:
    bool              appCursorKeys_  = false;
    bool              appKeypad_      = false;
    ZzMouseReportMode mouseMode_      = ZzMouseReportMode::None;
    bool              mouseSgr_       = false;
    bool              bracketedPaste_ = false;
    bool              focusReporting_ = false;
};
