# M3b（鼠标上报 + bracketed paste + 焦点上报）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 接通鼠标上报（?9/?1000/?1002/?1003/?1006）、bracketed paste（?2004）、焦点上报（?1004）的输入链路，encoder 三路编码补测试并修正 xterm 偏差，vim 鼠标端到端可用。

**架构：** 沿用 M3a 形态——facade send 系列经 ZzTerminalBackend 纯虚下发；native 由 ZzInputEncoder 编码经 emit 出口，DEC 模式分发同步 encoder 模式位；Contour 适配层类型映射委托 contour 自家 send API，统一 output handler 单一出口。

**技术栈：** C++20（Contour 侧 C++23）、CMake Presets（linux-gcc-debug）、CTest、pexpect/pyte（冒烟）。

**规格：** `docs/superpowers/specs/2026-09-20-m3b-mouse-paste-focus-design.md`（已批准）

**通用约定：**

- 构建/测试：`cmake --preset linux-gcc-debug`（CMake 变更后）、`cmake --build --preset linux-gcc-debug`、`ctest --preset linux-gcc-debug`（全量）、`ctest --preset linux-gcc-debug -R <测试名> --output-on-failure`（单个）。
- OFF：`cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure`。
- shared：`cmake -S . -B build/m2-shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check --output-on-failure`。
- 测试风格：tests/unit/*.cpp 自带 main()，ZZ_CHECK 宏计数失败（仿 tests/unit/test_backend_compat.cpp 开头），GLOB 自动收编，无需改 CMake。
- commit 规范：`type(scope): 中文描述`。
- doxygen 陷阱（公开头注释与 docs/）：行内 code span 内禁尖括号、内容禁以点开头、后禁紧跟顿号、禁 `#` 预处理词；正文与行内代码中禁 `\x` 反斜杠转义序列（代码围栏内不受影响）。
- 每个任务结束：全量 ctest 绿（+ 改动公开头/docs 时 doxygen 零警告）再 commit。
- third_party/contour 上游代码一律不改；Contour target 链接一律 PRIVATE。

---

### 任务 1：encoder 鼠标/粘贴/焦点单测 + Ctrl+非字母 C0 映射 + 无按钮移动码修正

**文件：**
- 修改：`src/input/InputEncoder.cpp`（Character 分支 Ctrl+非字母 C0 表；encodeMouse 无按钮移动码 35 修正）
- 测试：`tests/unit/test_input_encoder.cpp`（扩充：encodeMouse/encodePaste/encodeFocus/C0 表）

背景：encoder 三路编码 M1 实现、零测试。计划审查已发现两处与 xterm 的偏差：(a) Character 的 Ctrl 只处理字母（M3a 终审留项）；(b) encodeMouse 对 button==None 的移动事件给 code 32，xterm 无按钮移动为 32+3=35（ctlseqs：CB 低两位 3 表示释放/无按钮）。

- [ ] **步骤 1：扩充 encoder 单测（先红）**

`tests/unit/test_input_encoder.cpp` 匿名命名空间内追加（ZZ_CHECK/keyEvent 辅助沿用既有）：

```cpp
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
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U' ')) == "\x00");
    ZZ_CHECK(enc.encodeKey(keyEvent(ZzKeyEvent::Key::Character, ctrl, U'@')) == "\x00");
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
```

`main()` 中 `testModeBits();` 之后追加：

```cpp
    testMouseModeFiltering();
    testMouseClassicEncoding();
    testMouseMotionNoButton();
    testMouseSgrEncoding();
    testMouseClassicCoordinateLimit();
    testEncodePaste();
    testEncodeFocus();
    testCtrlNonLetterC0();
```

- [ ] **步骤 2：跑测试确认红**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_input_encoder --output-on-failure`
预期：FAIL——testMouseMotionNoButton（现有实现给码 32='@'）、testCtrlNonLetterC0（现有实现透传）失败；其余应全过。若 encodeMouse 既有实现还有其他与 xterm 的出入被新测试抓出，以 ctlseqs 为准修正并在报告说明。

- [ ] **步骤 3：修正 encodeMouse 无按钮移动码 + Ctrl 非字母 C0 表**

`src/input/InputEncoder.cpp`：

(a) encodeMouse 的按钮码 switch 中 `case ZzMouseButton::None: code = 0;` 改为：

```cpp
    case ZzMouseButton::None:
        // xterm：无按钮移动按按钮 3 编码（CB 低两位 3 + 移动位 32 = 35）。
        code = (event.action == ZzMouseAction::Move) ? 3 : 0;
        break;
```

（注意现有代码 `if (event.action == ZzMouseAction::Release) code = 3;` 在 switch 之后覆写，Release 路径不受影响。）

(b) Character 分支的 Ctrl 处理（M3a 版本为 `if (ctrl && letter) ... else appendUtf8`）改为：

```cpp
        if (ctrl && letter) {
            out.push_back(static_cast<char>(event.character & 0x1F));
        } else if (ctrl) {
            // xterm Ctrl+非字母 C0 映射：Space/@=NUL、[\]^_=0x1B-0x1F、?=DEL；
            // 未定义的非字母透传原字符。
            switch (event.character) {
            case U' ': case U'@': out.push_back('\x00'); break;
            case U'[':  out.push_back('\x1B'); break;
            case U'\\': out.push_back('\x1C'); break;
            case U']':  out.push_back('\x1D'); break;
            case U'^':  out.push_back('\x1E'); break;
            case U'_':  out.push_back('\x1F'); break;
            case U'?':  out.push_back('\x7F'); break;
            default:    appendUtf8(out, event.character); break;
            }
        } else {
            appendUtf8(out, event.character);
        }
```

删除 M3a 留下的"Ctrl+非字母的 C0 映射……未实现，归 M3b"注释（本任务交付）。

- [ ] **步骤 4：跑测试确认绿**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_input_encoder --output-on-failure`
预期：PASS。

- [ ] **步骤 5：全量 + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿（27 既有，本任务不新增测试文件）。

```bash
git add src/input/InputEncoder.cpp tests/unit/test_input_encoder.cpp
git commit -m "feat(input): encoder 鼠标/粘贴/焦点单测 + Ctrl 非字母 C0 表 + 无按钮移动码 35 修正"
```

---

### 任务 2：模式接线 + facade/接口 + native 实现 + Contour 适配 + native 集成测试

**文件：**
- 修改：`src/backend/ZzTerminalBackend.h`（sendMouse/sendPaste/sendFocus 纯虚）
- 修改：`include/ZzTerm/Terminal.h`（facade 三方法声明）
- 修改：`src/terminal/Terminal.cpp`（委托实现）
- 修改：`src/backend/native/ZzNativeBackend.h` / `.cpp`（三方法实现）
- 修改：`src/backend/native/NativeCsiDispatch.cpp`（dispatchDecPrivate 加 case 9/1000/1002/1003/1004/1006/2004 + 文件头注释）
- 修改：`src/backend/contour/ZzContourConvert.h`（zzMouseButton 映射）
- 修改：`src/backend/contour/ZzContourBackend.h` / `.cpp`（sendMouseEvent/sendPasteText/sendFocusEvent）
- 修改：`src/backend/contour/ZzContourBackendAdapter.cpp`（三方法委托 + flushReplies）
- 修改：`tests/unit/test_backend_interface.cpp`（FakeBackend 补 override）
- 测试：`tests/unit/test_native_mouse_input.cpp`（新）

- [ ] **步骤 1：写 native 集成测试 `tests/unit/test_native_mouse_input.cpp`（先红）**

```cpp
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
```

- [ ] **步骤 2：跑测试确认红**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_mouse_input --output-on-failure`
预期：编译失败（ZzTerminal 无 sendMouse/sendPaste/sendFocus 成员）。

- [ ] **步骤 3：接口 + facade + native 实现**

`src/backend/ZzTerminalBackend.h` 接口区（sendKey 之后）新增：

```cpp
    /// 编码并发出鼠标事件；当前上报模式下不该上报时静默丢弃。
    virtual void sendMouse(const ZzMouseEvent& event) = 0;
    /// 编码并发出粘贴文本（bracketed paste 2004 开启时包裹 200~/201~）。
    virtual void sendPaste(std::string_view utf8) = 0;
    /// 编码并发出焦点事件（focus reporting 1004 开启时 CSI I/O）。
    virtual void sendFocus(bool focused) = 0;
```

`include/ZzTerm/Terminal.h` 公开方法区（sendKey 声明之后）新增：

```cpp
    /**
     * @brief 发送鼠标事件（网格坐标，0 起始）。
     * @param event 鼠标语义事件（见 ZzTerm/Input.h）。
     * @note 编码依当前鼠标上报模式（?9/?1000/?1002/?1003）与编码格式（?1006），
     *       由 feed 接收的 DEC 序列联动；未开模式或未设 handler 时静默丢弃。
     */
    void sendMouse(const ZzMouseEvent& event);

    /**
     * @brief 发送粘贴文本。
     * @param utf8 已确认的合法 UTF-8 文本。
     * @note bracketed paste（?2004）开启时自动包裹 200~/201~；未设 handler 静默丢弃。
     */
    void sendPaste(std::string_view utf8);

    /**
     * @brief 发送焦点事件。
     * @param focused true = 获得焦点，false = 失去焦点。
     * @note focus reporting（?1004）开启时发 CSI I/O；未开或未设 handler 静默丢弃。
     */
    void sendFocus(bool focused);
```

`src/terminal/Terminal.cpp`：

```cpp
void ZzTerminal::sendMouse(const ZzMouseEvent& event)
{
    impl_->backend->sendMouse(event);
}

void ZzTerminal::sendPaste(std::string_view utf8)
{
    impl_->backend->sendPaste(utf8);
}

void ZzTerminal::sendFocus(bool focused)
{
    impl_->backend->sendFocus(focused);
}
```

`src/backend/native/ZzNativeBackend.h` 接口实现区新增：

```cpp
    void sendMouse(const ZzMouseEvent& event) override;
    void sendPaste(std::string_view utf8) override;
    void sendFocus(bool focused) override;
```

`src/backend/native/ZzNativeBackend.cpp`（sendKey 实现之后）：

```cpp
void ZzNativeBackend::sendMouse(const ZzMouseEvent& event)
{
    emit(encoder_.encodeMouse(event));
}

void ZzNativeBackend::sendPaste(std::string_view utf8)
{
    emit(encoder_.encodePaste(utf8));
}

void ZzNativeBackend::sendFocus(bool focused)
{
    emit(encoder_.encodeFocus(focused));
}
```

`src/backend/native/NativeCsiDispatch.cpp` dispatchDecPrivate 的 switch 中（case 25 之后）新增：

```cpp
        case 9: // X10 鼠标（仅按下）
            encoder_.setMouseReportMode(set ? ZzMouseReportMode::X10 : ZzMouseReportMode::None);
            break;
        case 1000: // Normal 鼠标（按下+释放）
            encoder_.setMouseReportMode(set ? ZzMouseReportMode::Normal : ZzMouseReportMode::None);
            break;
        case 1002: // Button-event 鼠标（+按下时拖动）
            encoder_.setMouseReportMode(set ? ZzMouseReportMode::ButtonEvent
                                            : ZzMouseReportMode::None);
            break;
        case 1003: // Any-event 鼠标（+任意移动）
            encoder_.setMouseReportMode(set ? ZzMouseReportMode::AnyEvent
                                            : ZzMouseReportMode::None);
            break;
        case 1004: // 焦点上报（CSI I/O）
            encoder_.setFocusReporting(set);
            break;
        case 1006: // SGR 1006 鼠标编码格式
            encoder_.setMouseSgrEncoding(set);
            break;
        case 2004: // bracketed paste
            encoder_.setBracketedPaste(set);
            break;
```

文件头注释更新：mouse/bracketed paste 从 M3 忽略清单移除（M3b 已交付）；剩余忽略项注明（其余 DEC 私有模式）。

- [ ] **步骤 4：Contour 适配层**

`src/backend/contour/ZzContourConvert.h` 输入方向节（zzKey 之后）新增：

```cpp
/// \brief ZzMouseButton → vtbackend::MouseButton。
/// 注意枚举顺序差：Zz 为 None/Left/Middle/Right，vtbackend 为 Left/Right/Middle；
/// None/Release 统一映射为 vtbackend::MouseButton::Release。
inline vtbackend::MouseButton zzMouseButton(ZzMouseButton button)
{
    switch (button) {
    case ZzMouseButton::Left:       return vtbackend::MouseButton::Left;
    case ZzMouseButton::Middle:     return vtbackend::MouseButton::Middle;
    case ZzMouseButton::Right:      return vtbackend::MouseButton::Right;
    case ZzMouseButton::WheelUp:    return vtbackend::MouseButton::WheelUp;
    case ZzMouseButton::WheelDown:  return vtbackend::MouseButton::WheelDown;
    case ZzMouseButton::WheelLeft:  return vtbackend::MouseButton::WheelLeft;
    case ZzMouseButton::WheelRight: return vtbackend::MouseButton::WheelRight;
    case ZzMouseButton::None:
    default:                        return vtbackend::MouseButton::Release;
    }
}
```

`src/backend/contour/ZzContourBackend.h` 公开方法区（sendText 之后）新增：

```cpp
    /// \brief 透传鼠标事件到 contour Terminal（网格坐标 → CellLocation；像素坐标缺省）。
    void sendMouseEvent(const ZzMouseEvent& event);
    /// \brief 透传粘贴文本（contour 按自身 bracketed 模式包裹）。
    void sendPasteText(std::string_view utf8);
    /// \brief 透传焦点事件。
    void sendFocusEvent(bool focused);
```

`src/backend/contour/ZzContourBackend.cpp` 实现（参照 sendKeyEvent 的既有结构）：

```cpp
void ZzContourBackend::sendMouseEvent(const ZzMouseEvent& event)
{
    const vtbackend::Modifiers mods { zzModifiers(event.modifiers) };
    const vtbackend::CellLocation pos { .line = vtbackend::LineOffset(event.row),
                                        .column = vtbackend::ColumnOffset(event.col) };
    switch (event.action) {
    case ZzMouseAction::Press:
        impl_->terminal->sendMousePressEvent(mods, zzMouseButton(event.button), pos,
                                             vtbackend::PixelCoordinate{}, false);
        break;
    case ZzMouseAction::Move:
        impl_->terminal->sendMouseMoveEvent(mods, pos, vtbackend::PixelCoordinate{}, false);
        break;
    case ZzMouseAction::Release:
        impl_->terminal->sendMouseReleaseEvent(mods, zzMouseButton(event.button),
                                               vtbackend::PixelCoordinate{}, false);
        break;
    }
}

void ZzContourBackend::sendPasteText(std::string_view utf8)
{
    impl_->terminal->sendPaste(utf8);
}

void ZzContourBackend::sendFocusEvent(bool focused)
{
    if (focused)
        impl_->terminal->sendFocusInEvent();
    else
        impl_->terminal->sendFocusOutEvent();
}
```

`src/backend/contour/ZzContourBackendAdapter.cpp` 类内（sendKey 之后）新增：

```cpp
    void sendMouse(const ZzMouseEvent& event) override
    {
        backend_->sendMouseEvent(event);
        backend_->flushReplies();
    }
    void sendPaste(std::string_view utf8) override
    {
        backend_->sendPasteText(utf8);
        backend_->flushReplies();
    }
    void sendFocus(bool focused) override
    {
        backend_->sendFocusEvent(focused);
        backend_->flushReplies();
    }
```

`tests/unit/test_backend_interface.cpp` 的 FakeBackend 补 override：

```cpp
    void sendMouse(const ZzMouseEvent& /*event*/) override {}
    void sendPaste(std::string_view /*utf8*/) override {}
    void sendFocus(bool /*focused*/) override {}
```

- [ ] **步骤 5：构建 + 聚焦测试确认绿**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "test_native_mouse_input|test_backend_interface" --output-on-failure`
预期：PASS。

- [ ] **步骤 6：OFF 构建验证**

运行：`cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure`
预期：全绿。

- [ ] **步骤 7：全量 + doxygen + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug && doxygen Doxyfile`
预期：全绿、零警告。

```bash
git add src/backend/ZzTerminalBackend.h include/ZzTerm/Terminal.h src/terminal/Terminal.cpp \
        src/backend/native/ZzNativeBackend.h src/backend/native/ZzNativeBackend.cpp \
        src/backend/native/NativeCsiDispatch.cpp \
        src/backend/contour/ZzContourConvert.h src/backend/contour/ZzContourBackend.h \
        src/backend/contour/ZzContourBackend.cpp src/backend/contour/ZzContourBackendAdapter.cpp \
        tests/unit/test_backend_interface.cpp tests/unit/test_native_mouse_input.cpp
git commit -m "feat(input): 鼠标/粘贴/焦点 send 链路——模式接线、facade 三方法、Contour 映射委托"
```

---

### 任务 3：compat 鼠标/粘贴/焦点强对照

**文件：**
- 测试：`tests/unit/test_backend_compat.cpp`（新用例 15/16/17 + main() 追加）

- [ ] **步骤 1：新增 compat 用例**

`tests/unit/test_backend_compat.cpp` 匿名命名空间内（testDeviceAttributes 之后）新增：

```cpp
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
```

`main()` 中 `testDeviceAttributes();` 之后追加：

```cpp
    testMouseSgrCompat();
    testBracketedPasteCompat();
    testFocusReportingCompat();
```

- [ ] **步骤 2：跑 compat 确认并处理分歧**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_backend_compat --output-on-failure`
预期与处置：
- 若全过：记录 Contour 实际编码到报告；
- 若红：先查 Contour 侧 output 是否经 flushReplies 上行（任务 2 步骤 4 已含）；再查 Contour 实际编码与 xterm 差异（SGR 释放是否 m 结尾、paste 包裹串、focus 序列）。确属上游语义差异的按 b 类规则分别断言并注释钉住（写清实测串、上游代码定位），不强行对齐。

- [ ] **步骤 3：全量 + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿。

```bash
git add tests/unit/test_backend_compat.cpp
git commit -m "test(compat): 鼠标 SGR/bracketed paste/焦点上报强对照"
```

---

### 任务 4：demo 鼠标解析 + 冒烟 vim 鼠标 + 四项验收 + API.md 同步

**文件：**
- 修改：`examples/ZzTermSmoke/main.cpp`（InputTranslator 加鼠标分支）
- 修改：`tests/interactive/verify_smoke.py`（vim 鼠标点击定位步骤）
- 修改：`docs/API.md`（sendMouse/sendPaste/sendFocus、鼠标模式、C0 表说明）

- [ ] **步骤 1：InputTranslator 扩展鼠标解析**

`examples/ZzTermSmoke/main.cpp` 的 InputTranslator：

1. 类内新增私有方法（tryEscape 之前）：

```cpp
    // 鼠标码（xterm 位布局）→ ZzMouseEvent → sendMouse。sgrRelease 仅 SGR 编码
    // 用（m 结尾）；经典编码释放由码 3 表达。坐标入参为协议 1 起始。
    void sendMouseFromCode(int code, int x, int y, bool sgrRelease)
    {
        ZzMouseEvent ev;
        ev.col = x - 1;
        ev.row = y - 1;
        if (code & 4)
            ev.modifiers = ev.modifiers | ZzKeyModifier::Shift;
        if (code & 8)
            ev.modifiers = ev.modifiers | ZzKeyModifier::Alt;
        if (code & 16)
            ev.modifiers = ev.modifiers | ZzKeyModifier::Ctrl;
        if (code & 64) {
            // 滚轮只有按下；横向滚轮（66/67）demo 忽略。
            if ((code & 1) != 0 || (code & 2) != 0)
                return;
            ev.action = ZzMouseAction::Press;
            ev.button = ZzMouseButton::WheelUp;
        } else if ((code & 3) == 3 || sgrRelease) {
            ev.action = ZzMouseAction::Release;
            ev.button = ZzMouseButton::None;
        } else if (code & 32) {
            ev.action = ZzMouseAction::Move;
            ev.button = ZzMouseButton::None;
        } else {
            ev.action = ZzMouseAction::Press;
            ev.button = (code & 3) == 0 ? ZzMouseButton::Left
                      : (code & 3) == 1 ? ZzMouseButton::Middle
                                        : ZzMouseButton::Right;
        }
        term_.sendMouse(ev);
    }
```

2. tryEscape 的 CSI 分支（`if (s[1] != '[')` 判断之后、参数扫描之前）插入两种鼠标形态：

```cpp
        if (s.size() >= 3 && s[2] == 'M') {
            // 经典 X10 鼠标：ESC [ M Cb Cx Cy（各 -32）
            if (s.size() < 6)
                return 0;
            const int code = static_cast<unsigned char>(s[3]) - 32;
            const int x = static_cast<unsigned char>(s[4]) - 32;
            const int y = static_cast<unsigned char>(s[5]) - 32;
            sendMouseFromCode(code, x, y, false);
            return 6;
        }
        if (s.size() >= 3 && s[2] == '<') {
            // SGR 1006 鼠标：ESC [ < code ; x ; y M/m
            std::size_t j = 3;
            while (j < s.size()
                   && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == ';'))
                ++j;
            if (j >= s.size())
                return 0; // 不完整
            if (s[j] != 'M' && s[j] != 'm')
                return j; // 非鼠标 < 序列：丢弃已扫描部分
            int code = 0, x = 0, y = 0;
            if (std::sscanf(std::string(s.substr(3, j - 3)).c_str(), "%d;%d;%d", &code, &x, &y)
                == 3)
                sendMouseFromCode(code, x, y, s[j] == 'm');
            return j + 1;
        }
```

（`<cstdio>` include 若无则补。）

- [ ] **步骤 2：构建 demo 验证编译**

运行：`cmake --build --preset linux-gcc-debug`
预期：编译通过。

- [ ] **步骤 3：verify_smoke.py 追加 vim 鼠标步骤**

在既有步骤尾部（exit 步骤之前，沿用编号顺延与 settle/check/screen_text/snapshot 辅助）追加：

```python
    # 步骤 N：鼠标（M3b）——vim mouse=a，合成 SGR 点击经 InputTranslator ->
    # sendMouse -> PTY -> vim；pyte 跟踪屏幕光标断言点击定位生效。
    child.sendline("vim -u NONE -c 'set mouse=a' mouse-m3b.txt")
    settle(child, stream)
    child.send("\x1b[<0;10;5M")  # 左键点击第 5 行第 10 列（SGR 1006，1 起始）
    settle(child, stream)
    cur = screen.cursor
    check(abs(cur.y - 4) <= 1 and abs(cur.x - 9) <= 1,
          "N.vim鼠标点击定位", f"cursor=({cur.x},{cur.y})")
    child.send(":q!\r")
    settle(child, stream)
```

注意：vim 对空缓冲区的点击会把光标放到点击处或最近文本位，容差 ±1；若实测 vim 版本行为差异（如点击落行尾/首），以实测调整断言并注释钉住。pyte 的 screen.cursor 跟踪渲染输出中的 CUP，与 Core 视图无关，是独立解释。

- [ ] **步骤 4：冒烟双后端全量运行**

运行：`ctest --preset linux-gcc-debug -R "SmokeInteractive" --output-on-failure`
预期：ZzTermSmokeInteractive_native 与 _contour 均 Passed（含新增鼠标步骤）。
若 Contour 失败而 native 成功：查 Contour 鼠标路径（zzMouseButton 映射/sendMousePressEvent 语义/flushReplies），确属上游差异按既有规则在脚本注释钉住（对照 compat 用例 15 结论）。

- [ ] **步骤 5：docs/API.md 同步**

在 API.md 输入方向章节补充：

- `sendMouse(const ZzMouseEvent&)` / `sendPaste(std::string_view)` / `sendFocus(bool)`：语义、模式联动（?9/?1000/?1002/?1003 互斥、?1006 编码格式、?2004、?1004）、未开模式/未设 handler 静默丢弃；
- 鼠标编码：经典（+32 三字节，坐标上限 223）与 SGR 1006（M/m 结尾）两格式；无按钮移动码 35；Contour 侧经 zzMouseButton 映射委托（枚举顺序差已处理）；
- Ctrl+非字母 C0 映射表（Space/@、[\]^_、?）已支持。

注意 doxygen markdown 陷阱（含禁 `\x` 反斜杠转义）。

- [ ] **步骤 6：四项验收**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure
cmake -S . -B build/m2-shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check --output-on-failure
doxygen Doxyfile
```

预期：ON 全绿（27 + 新增 1）、OFF 全绿、shared 全绿、doxygen 零警告。

- [ ] **步骤 7：vim 鼠标脚本化实测 + 人工清单**

冒烟步骤 3 的 vim 鼠标点击定位即脚本化实测（双后端）；报告附人工复测清单：

- tmux（本机未装）：开启鼠标后点击切换窗格、拖拽调整窗格大小、滚轮滚动；
- vim：mouse=a 下点击定位、鼠标拖拽可视选择、滚轮滚动；
- bracketed paste：bash 开启 enable-bracketed-paste（默认开）后粘贴多行文本不逐字执行；
- 焦点上报：开 ?1004h 的应用（如 vim 插件）切换窗口焦点。

- [ ] **步骤 8：Commit**

```bash
git add examples/ZzTermSmoke/main.cpp tests/interactive/verify_smoke.py docs/API.md
git commit -m "feat(demo): InputTranslator 鼠标解析（SGR 1006 + 经典 X10），冒烟加 vim 鼠标步骤，API.md 同步"
```

---

## 自检记录

- **规格覆盖度：** 规格 4.1（模式接线 ?9/?1000/?1002/?1003/?1006/?2004/?1004）→ 任务 2 步骤 3；规格 4.2（facade 三方法、native encoder 出口、Contour 映射委托）→ 任务 2 步骤 3/4；规格 4.3（Ctrl 非字母 C0 表、encodeMouse xterm 偏差修正）→ 任务 1 步骤 3；规格 4.4（encoder 单测、native 集成、compat 强对照、demo 鼠标解析、冒烟 vim 鼠标、tmux 人工清单）→ 任务 1/2/3/4 各自步骤；规格 4.5（错误处理）→ 任务 2 步骤 1（testNoHandlerSafe）+ 任务 4 步骤 1（非法序列丢弃）；规格 4.6（四项验收 + vim 实测）→ 任务 4 步骤 6/7。
- **类型一致性：** `sendMouse(const ZzMouseEvent&)`/`sendPaste(std::string_view)`/`sendFocus(bool)`（任务 2 定义于接口/facade/native/Contour/FakeBackend，任务 3/4 使用同一签名）；Contour 侧 `sendMouseEvent(const ZzMouseEvent&)`/`sendPasteText(std::string_view)`/`sendFocusEvent(bool)`（任务 2 定义，适配器委托）；`zzMouseButton`（任务 2 定义于 ZzContourConvert.h）；`sendMouseFromCode`（任务 4 demo 本地私有）；测试辅助 `mouseEvent` 在 test_native_mouse_input.cpp 与 test_backend_compat.cpp 各自文件内独立定义（签名一致，无跨文件依赖）。
- **占位符扫描：** 无待定/TODO；"以实测调整断言容差"（任务 4 步骤 3 vim 点击）与"分歧回退 b 类"（任务 3 步骤 2）均给出明确判断标准与默认动作。
