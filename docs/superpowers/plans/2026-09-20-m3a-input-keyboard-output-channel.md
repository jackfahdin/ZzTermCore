# M3a（键盘输入编码 + output 通道 + 终端回传）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 打通键盘输入方向（前端按键 → 编码字节 → output 通道 → PTY）与终端回传（DA1/DSR 应答），双后端对称，demo 内 vim 可交互。

**架构：** facade 新增 sendText/sendKey，经 ZzTerminalBackend 新纯虚下发（backend 持有编码器）；native 后端持有 ZzInputEncoder 并实存 outputHandler_，DEC 模式分发同步 encoder 模式位；Contour 适配层做类型映射委托 contour 自家 send API，字节经 M1b 已通的 output 钩子上行。所有外发字节（输入编码 + 回传）经统一 output handler 出口。

**技术栈：** C++20（Contour 侧 C++23）、CMake Presets（linux-gcc-debug）、CTest、pexpect/pyte（冒烟）。

**规格：** `docs/superpowers/specs/2026-09-20-m3a-input-keyboard-output-channel-design.md`（已批准）

**通用约定：**

- 构建/测试：`cmake --preset linux-gcc-debug`（CMake 变更后）、`cmake --build --preset linux-gcc-debug`、`ctest --preset linux-gcc-debug`（全量）、`ctest --preset linux-gcc-debug -R <测试名> --output-on-failure`（单个）。
- OFF：`cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure`（复用既有目录）。
- 测试风格：tests/unit/*.cpp 自带 main()，ZZ_CHECK 宏计数失败（仿 tests/unit/test_backend_compat.cpp 开头），GLOB 自动收编，无需改 CMake。
- commit 规范：`type(scope): 中文描述`。
- doxygen 陷阱（公开头注释与 docs/）：行内 code span 内禁尖括号、内容禁以点开头、后禁紧跟顿号、禁 `#` 预处理词。
- 每个任务结束：全量 ctest 绿（+ 改动公开头/docs 时 doxygen 零警告）再 commit。
- Contour target 链接一律 PRIVATE（tests/CMakeLists.txt 既有注释约束）。

---

### 任务 1：ZzInputEncoder 单测补齐 + Character 修饰键编码修正

**文件：**
- 修改：`src/input/InputEncoder.cpp:63-66`（Character 分支补 Ctrl/Alt 修饰键编码）
- 测试：`tests/unit/test_input_encoder.cpp`（新）

背景：encoder 实现于 M1（240 行，零测试）。计划审查发现 Character 分支忽略修饰键——Ctrl+C 在真实 shell 中不可用，属必须修正的缺口（规格 5 风险节"encoder 既有实现与 xterm 细节差：以 xterm 为准修正并在报告说明"）。

- [ ] **步骤 1：写 encoder 单测 `tests/unit/test_input_encoder.cpp`（先红）**

```cpp
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
    if (g_failures != 0)
        std::fprintf(stderr, "test_input_encoder: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：跑测试确认红**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_input_encoder --output-on-failure`
预期：FAIL——testCharacterWithModifiers 的 Ctrl/Alt 断言失败（现有实现忽略修饰键）；其余应全过。若有其余失败，说明 M1 实现与 xterm 另有偏差，以 xterm ctlseqs 为准修正并在报告说明。

- [ ] **步骤 3：修正 Character 修饰键编码**

`src/input/InputEncoder.cpp` 的 `case Key::Character:`（63-66 行）替换为：

```cpp
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
```

- [ ] **步骤 4：跑测试确认绿**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_input_encoder --output-on-failure`
预期：PASS。

- [ ] **步骤 5：全量 + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿（24 + 1）。

```bash
git add src/input/InputEncoder.cpp tests/unit/test_input_encoder.cpp
git commit -m "feat(input): ZzInputEncoder 单测补齐 + Character 修饰键编码（Ctrl C0/Alt ESC 前缀）"
```

---

### 任务 2：send 链路贯通（接口 / facade / native / Contour 适配）+ native 集成测试

**文件：**
- 修改：`src/backend/ZzTerminalBackend.h`（send 系列纯虚 + Input.h include）
- 修改：`include/ZzTerm/Terminal.h`（facade sendText/sendKey 声明）
- 修改：`src/terminal/Terminal.cpp`（委托实现）
- 修改：`src/backend/native/ZzNativeBackend.h` / `.cpp`（encoder_/outputHandler_ 成员、emit、send 实现、setOutputHandler 实存、ESC=/ESC> 接线）
- 修改：`src/backend/native/NativeCsiDispatch.cpp`（dispatchDecPrivate 加 case 1 DECCKM）
- 修改：`src/backend/contour/ZzContourBackend.h` / `.cpp`（sendKeyEvent/sendText 透传）
- 修改：`src/backend/contour/ZzContourConvert.h`（ZzTerm → vtbackend 输入方向映射节）
- 修改：`src/backend/contour/ZzContourBackendAdapter.cpp`（send 委托 + flushReplies）
- 修改：`tests/unit/test_backend_interface.cpp`（FakeBackend 补 override）
- 测试：`tests/unit/test_native_input.cpp`（新）

- [ ] **步骤 1：写 native 集成测试 `tests/unit/test_native_input.cpp`（先红）**

```cpp
// native 输入链路集成测试（M3a）：facade send → encoder → output 通道；
// DECCKM(?1) 模式经 feed 同步到编码；handler 未设不崩。仅公开 API。
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

ZzKeyEvent keyEvent(ZzKeyEvent::Key key)
{
    ZzKeyEvent ev;
    ev.key = key;
    return ev;
}

// sendText/sendKey 经 output handler 发出；功能键编码正确。
void testSendThroughOutputHandler()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    t.sendText("hi");
    ZZ_CHECK(out == "hi");
    out.clear();
    t.sendKey(keyEvent(ZzKeyEvent::Key::Up));
    ZZ_CHECK(out == "\x1B[A");
    out.clear();
    t.sendKey(keyEvent(ZzKeyEvent::Key::F1));
    ZZ_CHECK(out == "\x1BOP");
}

// DECCKM：feed ?1h 后方向键切 SS3，?1l 切回（模式位经 CSI 同步到 encoder）。
void testDecckmSync()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "\x1B[?1h");
    t.sendKey(keyEvent(ZzKeyEvent::Key::Up));
    ZZ_CHECK(out == "\x1BOA");
    out.clear();
    feed(t, "\x1B[?1l");
    t.sendKey(keyEvent(ZzKeyEvent::Key::Up));
    ZZ_CHECK(out == "\x1B[A");
}

// handler 未设置：send 与回传静默丢弃，不崩。
void testNoHandlerSafe()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    t.sendText("x");
    t.sendKey(keyEvent(ZzKeyEvent::Key::Up));
    feed(t, "\x1B[?1h"); // 模式切换也不依赖 handler
    ZZ_CHECK(true);      // 到达此处即未崩
}

// handler 可替换：新 handler 生效，旧 handler 不再收到。
void testHandlerReplaceable()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string a, b;
    t.setOutputHandler([&](std::string_view s) { a.append(s); });
    t.sendText("1");
    t.setOutputHandler([&](std::string_view s) { b.append(s); });
    t.sendText("2");
    ZZ_CHECK(a == "1");
    ZZ_CHECK(b == "2");
}

} // namespace

int main()
{
    testSendThroughOutputHandler();
    testDecckmSync();
    testNoHandlerSafe();
    testHandlerReplaceable();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_input: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：跑测试确认红**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_input --output-on-failure`
预期：编译失败（ZzTerminal 无 sendText/sendKey 成员）。

- [ ] **步骤 3：接口 + facade + native 实现**

`src/backend/ZzTerminalBackend.h`：顶部 include 区加 `#include "ZzTerm/Input.h"` 与 `#include <string_view>`（若无）；接口区（setAmbiguousWidthMode 之后）新增：

```cpp
    /// 编码并发出普通文本输入（IME commit text）；经 output 通道。
    virtual void sendText(std::string_view utf8) = 0;
    /// 编码并发出按键事件；编码依据后端当前终端模式（application cursor 等）。
    virtual void sendKey(const ZzKeyEvent& event) = 0;
```

`include/ZzTerm/Terminal.h`：include 区加 `#include "ZzTerm/Input.h"`（若 ZzTermChanges 定义处未带入）；公开方法区（setAmbiguousWidthMode 声明之后）新增：

```cpp
    /**
     * @brief 发送普通文本输入（Unicode 输入、IME commit text）。
     * @param utf8 已确认的合法 UTF-8 文本（前端契约，Core 不重复校验）。
     * @note 编码字节经 setOutputHandler 的 output 通道发出；
     *       未设置 handler 时字节静默丢弃。
     */
    void sendText(std::string_view utf8);

    /**
     * @brief 发送按键事件（功能键、组合键；普通字符优先 sendText）。
     * @param event 键盘语义事件（见 ZzTerm/Input.h）。
     * @note 编码依据后端当前终端模式（application cursor/keypad 等），
     *       与 feed 接收的 DEC 模式序列联动；未设置 handler 时静默丢弃。
     */
    void sendKey(const ZzKeyEvent& event);
```

`src/terminal/Terminal.cpp`（参照既有 setOutputHandler 委托写法）：

```cpp
void ZzTerminal::sendText(std::string_view utf8)
{
    impl_->backend->sendText(utf8);
}

void ZzTerminal::sendKey(const ZzKeyEvent& event)
{
    impl_->backend->sendKey(event);
}
```

`src/backend/native/ZzNativeBackend.h`：

include 区加 `#include "ZzTerm/Input.h"`（InputEncoder 完整类型需要）。接口实现区新增：

```cpp
    void sendText(std::string_view utf8) override;
    void sendKey(const ZzKeyEvent& event) override;
```

私有方法区新增：

```cpp
    /// 经 outputHandler_ 发出字节；handler 未设或字节为空时丢弃。
    void emit(std::string_view bytes);
```

成员区（ambiguousWide_ 附近）新增：

```cpp
    ZzInputEncoder encoder_; ///< 输入编码器（模式位由 CSI/ESC 分发同步，M3a）。
    std::function<void(std::string_view)> outputHandler_; ///< output 通道（M3a 启用）。
```

`src/backend/native/ZzNativeBackend.cpp`：

原 no-op setOutputHandler（含"M3 输入编码后启用"注释，84-87 行）替换为：

```cpp
void ZzNativeBackend::setOutputHandler(std::function<void(std::string_view)> handler)
{
    outputHandler_ = std::move(handler);
}

void ZzNativeBackend::emit(std::string_view bytes)
{
    if (outputHandler_ && !bytes.empty())
        outputHandler_(bytes);
}

void ZzNativeBackend::sendText(std::string_view utf8)
{
    emit(encoder_.encodeText(utf8));
}

void ZzNativeBackend::sendKey(const ZzKeyEvent& event)
{
    emit(encoder_.encodeKey(event));
}
```

同文件 dispatchEsc 的 switch 中（case 'H' HTS 之后）新增：

```cpp
    case '=': // DECKPAM：application keypad（encoder 暂无 numpad 键消费方，
              // 状态同步为未来 keypad 编码保持正确）
        encoder_.setApplicationKeypad(true);
        break;
    case '>': // DECPNM：numeric keypad
        encoder_.setApplicationKeypad(false);
        break;
```

`src/backend/native/NativeCsiDispatch.cpp`：dispatchDecPrivate 的 switch 中（case 7 之前）新增：

```cpp
        case 1: // DECCKM：application cursor keys（同步到输入编码器）
            encoder_.setApplicationCursorKeys(set);
            break;
```

文件头注释中"其余 DEC 私有模式（mouse/bracketed paste 等）"的表述把 ?1 从忽略清单移除（M3a 已交付）。

- [ ] **步骤 4：Contour 适配层**

`src/backend/contour/ZzContourConvert.h`：文件头注释"vtbackend → ZzTerm 公开类型的转换共享头"更新为双向；文件末尾新增输入方向映射节：

```cpp
// ---- ZzTerm → vtbackend（输入方向，M3a）----

#include <vtbackend/input/InputGenerator.hpp>

#include <ZzTerm/Input.h>

#include <optional>

/// \brief ZzKeyModifier → vtbackend::Modifiers（Shift/Alt/Ctrl/Super 一一对应）。
inline vtbackend::Modifiers zzModifiers(ZzKeyModifier mods)
{
    vtbackend::Modifiers out;
    if (zzHasModifier(mods, ZzKeyModifier::Shift))
        out.enable(vtbackend::Modifier::Shift);
    if (zzHasModifier(mods, ZzKeyModifier::Alt))
        out.enable(vtbackend::Modifier::Alt);
    if (zzHasModifier(mods, ZzKeyModifier::Ctrl))
        out.enable(vtbackend::Modifier::Control);
    if (zzHasModifier(mods, ZzKeyModifier::Super))
        out.enable(vtbackend::Modifier::Super);
    return out;
}

/// \brief ZzKeyEvent::Key → vtbackend::Key；Character 与未覆盖键返回 nullopt
///（Character 由调用方走 sendCharEvent 路径）。
inline std::optional<vtbackend::Key> zzKey(ZzKeyEvent::Key key)
{
    using ZK = ZzKeyEvent::Key;
    using CK = vtbackend::Key;
    switch (key) {
    case ZK::Enter:     return CK::Enter;
    case ZK::Tab:       return CK::Tab;
    case ZK::Backspace: return CK::Backspace;
    case ZK::Escape:    return CK::Escape;
    case ZK::Up:        return CK::UpArrow;
    case ZK::Down:      return CK::DownArrow;
    case ZK::Left:      return CK::LeftArrow;
    case ZK::Right:     return CK::RightArrow;
    case ZK::Home:      return CK::Home;
    case ZK::End:       return CK::End;
    case ZK::Insert:    return CK::Insert;
    case ZK::Delete:    return CK::Delete;
    case ZK::PageUp:    return CK::PageUp;
    case ZK::PageDown:  return CK::PageDown;
    case ZK::F1:  return CK::F1;
    case ZK::F2:  return CK::F2;
    case ZK::F3:  return CK::F3;
    case ZK::F4:  return CK::F4;
    case ZK::F5:  return CK::F5;
    case ZK::F6:  return CK::F6;
    case ZK::F7:  return CK::F7;
    case ZK::F8:  return CK::F8;
    case ZK::F9:  return CK::F9;
    case ZK::F10: return CK::F10;
    case ZK::F11: return CK::F11;
    case ZK::F12: return CK::F12;
    case ZK::Character:
    default:            return std::nullopt;
    }
}
```

（crispy::Flags 的置位方法名以库内实际为准：enable / set / operator|= 之一，编译验证后定稿并在报告注明。）

`src/backend/contour/ZzContourBackend.h` 公开方法区（flushReplies 之后）新增：

```cpp
    /// \brief 透传按键事件到 contour Terminal（编码字节经 output 钩子上行）。
    /// Character 键走 sendCharEvent；未覆盖键忽略。
    void sendKeyEvent(const ZzKeyEvent& event);
    /// \brief 普通文本输入。encodeText 恒等语义，字节直接经 output 钩子上行
    ///（与 contour 的 KAM/闪烁重置无交互，注释钉住）。
    void sendText(std::string_view utf8);
```

（`ZzKeyEvent` 前向声明或 include ZzTerm/Input.h——该头公开安装、无 Contour 依赖，直接 include。）

`src/backend/contour/ZzContourBackend.cpp` 实现（Impl 持有 `std::unique_ptr<vtbackend::Terminal> terminal`）：

```cpp
void ZzContourBackend::sendKeyEvent(const ZzKeyEvent& event)
{
    const auto now = std::chrono::steady_clock::now();
    const vtbackend::KeyboardModifiers mods { zzModifiers(event.modifiers) };
    if (event.key == ZzKeyEvent::Key::Character) {
        if (event.character != 0)
            impl_->terminal->sendCharEvent(event.character, vtbackend::KeyIdentity{}, mods,
                                           vtbackend::KeyboardEventType::Press, now);
        return;
    }
    const std::optional<vtbackend::Key> key = zzKey(event.key);
    if (!key)
        return; // 未覆盖键：忽略（ZzContourConvert.h 注释钉住）
    impl_->terminal->sendKeyEvent(*key, mods, vtbackend::KeyboardEventType::Press, now);
}

void ZzContourBackend::sendText(std::string_view utf8)
{
    // encodeText 恒等：直接写入 transport（复用回传路径，保持单一出口）。
    impl_->terminal->writeToTransport(utf8); // 方法名以 contour 实际 API 为准，见下
}
```

注意：sendText 的上行通道需用 contour 的实际写出 API——若 `writeToTransport` 不存在/不合用，改为由适配器层直接调 outputHandler_（见步骤 5 备选），实现者以编译与行为验证为准并在报告说明选择。

`src/backend/contour/ZzContourBackendAdapter.cpp` 类内（setAmbiguousWidthMode 之后）新增：

```cpp
    void sendText(std::string_view utf8) override
    {
        backend_->sendText(utf8);
        backend_->flushReplies();
    }
    void sendKey(const ZzKeyEvent& event) override
    {
        backend_->sendKeyEvent(event);
        backend_->flushReplies(); // send 同步生成的编码字节立即上行
    }
```

备选（若步骤 4 的 sendText 无法经 contour 写出）：适配器直接 `if (outputHandler_) outputHandler_(utf8);` 并在注释钉住"encodeText 恒等，绕过 contour 直发"。

`tests/unit/test_backend_interface.cpp` 的 FakeBackend 补 override（参照其 setAmbiguousWidthMode 空实现）：

```cpp
    void sendText(std::string_view /*utf8*/) override {}
    void sendKey(const ZzKeyEvent& /*event*/) override {}
```

- [ ] **步骤 5：构建 + 聚焦测试确认绿**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "test_native_input|test_backend_interface" --output-on-failure`
预期：PASS。Contour 适配层编译错误（Flags 置位方法名、writeToTransport 等）按库内实际 API 修正并在报告记录。

- [ ] **步骤 6：OFF 构建验证（新纯虚在 OFF 路径链接完整）**

运行：`cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure`
预期：全绿。

- [ ] **步骤 7：全量 + doxygen + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug && doxygen Doxyfile`
预期：全绿、零警告。

```bash
git add src/backend/ZzTerminalBackend.h include/ZzTerm/Terminal.h src/terminal/Terminal.cpp \
        src/backend/native/ZzNativeBackend.h src/backend/native/ZzNativeBackend.cpp \
        src/backend/native/NativeCsiDispatch.cpp \
        src/backend/contour/ZzContourBackend.h src/backend/contour/ZzContourBackend.cpp \
        src/backend/contour/ZzContourConvert.h src/backend/contour/ZzContourBackendAdapter.cpp \
        tests/unit/test_backend_interface.cpp tests/unit/test_native_input.cpp
git commit -m "feat(input): send 链路贯通——facade sendText/sendKey、native encoder/output、Contour 映射委托、DECCKM/keypad 接线"
```

---

### 任务 3：native 回传 DA1 / DSR 5n / CPR 6n

**文件：**
- 修改：`src/backend/native/NativeCsiDispatch.cpp`（dispatchCsi 加 case 'c' / 'n'）
- 测试：`tests/unit/test_native_replies.cpp`（新）

- [ ] **步骤 1：写回传测试 `tests/unit/test_native_replies.cpp`（先红）**

```cpp
// native 终端回传（M3a）：DA1、DSR 5n、CPR 6n 经 output 通道应答。仅公开 API。
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

// DA1（CSI c）：应答 VT102 级最小集；回传不标脏。
void testDa1()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    const ZzTermChanges ch = feed(t, "\x1B[c");
    ZZ_CHECK(out == "\x1B[?1;2c");
    ZZ_CHECK(!ch.screenDirty); // 回传不标脏
}

// DSR 5n：就绪应答。
void testDsr5()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "\x1B[5n");
    ZZ_CHECK(out == "\x1B[0n");
}

// CPR 6n：应答真实光标位置（1 起始）；写入移动光标后应答跟随。
void testCpr()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "AB");
    feed(t, "\x1B[6n");
    ZZ_CHECK(out == "\x1B[1;3R"); // 行 1 列 3（0 起始 (0,2) 换算）
    out.clear();
    feed(t, "\x1B[3;5H");         // 光标到行 3 列 5（1 起始）
    feed(t, "\x1B[6n");
    ZZ_CHECK(out == "\x1B[3;5R");
}

// 未知 DSR 参数安全忽略（无应答、不崩）。
void testUnknownDsrIgnored()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "\x1B[7n");
    ZZ_CHECK(out.empty());
}

} // namespace

int main()
{
    testDa1();
    testDsr5();
    testCpr();
    testUnknownDsrIgnored();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_replies: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

注意：测试里 feed 返回 ZzTermChanges——上面 feed 辅助函数返回 void，testDa1 需要返回值。写文件时把辅助函数改为返回 ZzTermChanges：

```cpp
ZzTermChanges feed(ZzTerminal& t, std::string_view bytes)
{
    return t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                             bytes.size()));
}
```

- [ ] **步骤 2：跑测试确认红**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_replies --output-on-failure`
预期：FAIL（CSI c/n 当前落入 default 安全忽略，无应答）。

- [ ] **步骤 3：实现回传**

`src/backend/native/NativeCsiDispatch.cpp` 的 dispatchCsi switch 中（case 'm' 之前）新增（两个 case 直接 return，跳过函数末尾的 noteScreenDirty——回传不标脏）：

```cpp
    case 'c': // DA1：省略/0 参数应答 VT102 级最小集（xterm 兼容）；回传不标脏
        if (paramOr(seq, 0, 0) == 0)
            emit("\x1B[?1;2c");
        return;
    case 'n': { // DSR：5=就绪；6=CPR（真实光标位置，1 起始）；其余安全忽略
        const int p = paramOr(seq, 0, 0);
        if (p == 5) {
            emit("\x1B[0n");
        } else if (p == 6) {
            const std::string cpr = "\x1B[" + std::to_string(cur.row + 1) + ";"
                                  + std::to_string(cur.col + 1) + "R";
            emit(cpr);
        }
        return;
    }
```

（`<string>` include 若无则补；emit 为任务 2 引入的 ZzNativeBackend 成员。）

- [ ] **步骤 4：跑测试确认绿 + 全量 + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_replies --output-on-failure && ctest --preset linux-gcc-debug`
预期：聚焦 PASS、全量全绿。

```bash
git add src/backend/native/NativeCsiDispatch.cpp tests/unit/test_native_replies.cpp
git commit -m "feat(native): 终端回传 DA1/DSR 5n/CPR 6n 经 output 通道"
```

---

### 任务 4：compat 输入方向对照

**文件：**
- 测试：`tests/unit/test_backend_compat.cpp`（新用例 12/13/14 + main() 追加）

- [ ] **步骤 1：新增 compat 用例（先红——验证 Contour 侧实际行为）**

`tests/unit/test_backend_compat.cpp` 匿名命名空间内（testAutoWrapMode 之后）新增：

```cpp
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
```

`main()` 中 `testAutoWrapMode();` 之后追加：

```cpp
    testInputApplicationCursor();
    testCursorPositionReport();
    testDeviceAttributes();
```

- [ ] **步骤 2：跑 compat 确认并处理分歧**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_backend_compat --output-on-failure`
预期与处置：
- 若全过：记录 Contour 实际编码与应答串到报告；
- 若用例 12/13 红：先查 Contour 侧 output 是否经 flushReplies 上行（适配器 send 后必须 flush——任务 2 步骤 5 已含）；再查 Contour 实际编码（application 模式 SS3 形式、CPR 格式）与 xterm 差异。确属上游语义差异的，按 b 类规则分别断言并注释钉住（写清研判证据：实测串、上游代码定位），不强行对齐；
- 若用例 14 的应答前缀断言（CSI ? 开头）不成立：以 Contour 实际应答串修正断言并注释。

- [ ] **步骤 3：全量 + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿。

```bash
git add tests/unit/test_backend_compat.cpp
git commit -m "test(compat): 输入方向对照——DECCKM 编码/CPR 强对照、DA b 类钉住"
```

---

### 任务 5：demo stdin 接通 + 冒烟用例 + 四项验收 + API.md 同步

**文件：**
- 修改：`examples/ZzTermSmoke/main.cpp`（stdin 经 InputTranslator → sendText/sendKey；setOutputHandler → PTY）
- 修改：`tests/interactive/verify_smoke.py`（按键回显 + 方向键历史召回端到端用例）
- 修改：`docs/API.md`（sendText/sendKey、回传行为、统一 output 通道说明）

- [ ] **步骤 1：demo 接入输入链路**

`examples/ZzTermSmoke/main.cpp`：

1. 文件头部功能注释更新：原为"stdin 字节透传 -> PTY"，改为"stdin 经 Core 输入链路（sendText/sendKey）-> output 通道 -> PTY"。
2. 新增 demo 本地输入翻译器（放匿名命名空间，RawTerminal 类之后）：

```cpp
/// 输入翻译器（demo 本地）：stdin 字节流 → sendText / sendKey。
/// 功能键按 xterm 编码识别（CSI/SS3），其余字节（含 UTF-8 多字节）走 sendText。
/// 不完整转义序列在缓冲中等待后续字节；无法识别的序列丢弃。
class InputTranslator {
public:
    explicit InputTranslator(ZzTerminal& term) : term_(term) {}

    void feed(std::string_view data)
    {
        buf_ += data;
        std::size_t i = 0;
        while (i < buf_.size()) {
            if (buf_[i] == '\x1B') {
                const std::size_t eaten = tryEscape(std::string_view(buf_).substr(i));
                if (eaten == 0)
                    break; // 序列不完整：等更多字节
                i += eaten;
                continue;
            }
            // 普通文本段：累积到下一个 ESC 为止一次性 sendText
            const std::size_t next = buf_.find('\x1B', i);
            const std::size_t end = next == std::string::npos ? buf_.size() : next;
            term_.sendText(std::string_view(buf_).substr(i, end - i));
            i = end;
        }
        buf_.erase(0, i);
    }

private:
    void sendKey(ZzKeyEvent::Key k)
    {
        ZzKeyEvent ev;
        ev.key = k;
        term_.sendKey(ev);
    }

    // 返回消费字节数；0 = 序列不完整需等待。
    std::size_t tryEscape(std::string_view s)
    {
        if (s.size() < 2)
            return 0;
        if (s[1] == 'O') { // SS3：方向/Home/End/F1-F4
            if (s.size() < 3)
                return 0;
            switch (s[2]) {
            case 'A': sendKey(ZzKeyEvent::Key::Up); return 3;
            case 'B': sendKey(ZzKeyEvent::Key::Down); return 3;
            case 'C': sendKey(ZzKeyEvent::Key::Right); return 3;
            case 'D': sendKey(ZzKeyEvent::Key::Left); return 3;
            case 'H': sendKey(ZzKeyEvent::Key::Home); return 3;
            case 'F': sendKey(ZzKeyEvent::Key::End); return 3;
            case 'P': sendKey(ZzKeyEvent::Key::F1); return 3;
            case 'Q': sendKey(ZzKeyEvent::Key::F2); return 3;
            case 'R': sendKey(ZzKeyEvent::Key::F3); return 3;
            case 'S': sendKey(ZzKeyEvent::Key::F4); return 3;
            default: return 2; // 未知 SS3：丢弃 ESC O
            }
        }
        if (s[1] != '[') {
            sendKey(ZzKeyEvent::Key::Escape);
            return 1; // 裸 ESC
        }
        // CSI：ESC [ 参数 final
        std::size_t j = 2;
        while (j < s.size()
               && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == ';'))
            ++j;
        if (j >= s.size())
            return 0; // 不完整
        const char fin = s[j];
        switch (fin) {
        case 'A': sendKey(ZzKeyEvent::Key::Up); return j + 1;
        case 'B': sendKey(ZzKeyEvent::Key::Down); return j + 1;
        case 'C': sendKey(ZzKeyEvent::Key::Right); return j + 1;
        case 'D': sendKey(ZzKeyEvent::Key::Left); return j + 1;
        case 'H': sendKey(ZzKeyEvent::Key::Home); return j + 1;
        case 'F': sendKey(ZzKeyEvent::Key::End); return j + 1;
        case '~': {
            const std::string param(s.substr(2, j - 2));
            const int n = param.empty() ? 0 : std::atoi(param.c_str());
            switch (n) {
            case 2:  sendKey(ZzKeyEvent::Key::Insert); break;
            case 3:  sendKey(ZzKeyEvent::Key::Delete); break;
            case 5:  sendKey(ZzKeyEvent::Key::PageUp); break;
            case 6:  sendKey(ZzKeyEvent::Key::PageDown); break;
            case 15: sendKey(ZzKeyEvent::Key::F5); break;
            case 17: sendKey(ZzKeyEvent::Key::F6); break;
            case 18: sendKey(ZzKeyEvent::Key::F7); break;
            case 19: sendKey(ZzKeyEvent::Key::F8); break;
            case 20: sendKey(ZzKeyEvent::Key::F9); break;
            case 21: sendKey(ZzKeyEvent::Key::F10); break;
            case 23: sendKey(ZzKeyEvent::Key::F11); break;
            case 24: sendKey(ZzKeyEvent::Key::F12); break;
            default: break; // 未知 CSI ~：丢弃
            }
            return j + 1;
        }
        default: return j + 1; // 未知 CSI：丢弃
        }
    }

    ZzTerminal& term_;
    std::string  buf_;
};
```

（需补 include：`<cctype>`、`<cstdlib>`。）

3. Terminal 构造之后安装 output 通道（pty->write 签名以 main.cpp 既有调用为准）：

```cpp
    terminal.setOutputHandler([&](std::string_view bytes) {
        pty->write(bytes.data(), static_cast<std::ptrdiff_t>(bytes.size()));
    });
```

4. 主循环中 stdin 处理（原"stdin -> PTY 字节透传"段，约 :249-260）改为：

```cpp
        if (watchStdin && (fds[1].revents & POLLIN)) {
            const ssize_t n = ::read(STDIN_FILENO, ioBuf, sizeof(ioBuf));
            if (n <= 0) {
                watchStdin = false; // stdin EOF（管道场景）后停止监听
            } else {
                translator.feed(std::string_view(ioBuf, static_cast<std::size_t>(n)));
            }
        }
```

（`translator` 在 terminal 之后构造：`InputTranslator translator(terminal);`——若 demo 的 backend 选择分支产生多个 terminal 实例，按既有结构在最终持有处构造。EOF 行为保持既有语义，冒烟脚本依赖。）

- [ ] **步骤 2：构建 demo 并手工冒烟验证编译**

运行：`cmake --build --preset linux-gcc-debug`
预期：编译通过（ZzTermSmoke 目标）。

- [ ] **步骤 3：verify_smoke.py 追加输入方向用例**

在既有步骤之后（文件尾部 main 流程的合适位置，沿用 step()/settle() 辅助函数风格）追加两个步骤：

```python
    # 步骤 N：输入链路（M3a）——按键经 Core sendText -> PTY -> bash 回显。
    # stdin 字节不再透传，而是经 demo InputTranslator -> sendText/sendKey -> output 通道。
    step("输入链路：sendText 回显")
    child.send("echo M3A_INPUT_OK\r")
    settle(child, stream)
    stream.feed(child.read_nonblocking(size=65536, timeout=0.2) or b"")  # 按既有模式刷新
    text = screen_text(screen)
    check("M3A_INPUT_OK 回显可见", "M3A_INPUT_OK" in text)

    # 步骤 N+1：方向键经 sendKey + DECCKM——bash readline 启用 application
    # cursor（?1h），Up 编码为 SS3 OA 才能召回历史；断言召回的命令上屏。
    step("输入链路：sendKey 方向键召回历史")
    child.send("\x1b[A")       # Up：经 InputTranslator -> sendKey(Up)
    settle(child, stream)
    check("Up 召回 echo M3A_INPUT_OK", "echo M3A_INPUT_OK" in screen_text(screen))
    child.send(chr(3))         # Ctrl+C 放弃该行（经 sendText 透传控制字节）
    settle(child, stream)
```

注意：settle/stream 的实际调用模式以 verify_smoke.py 既有步骤为准（读文件对齐写法）；Ctrl+C 经 sendText 透传是既有行为（0x03 为普通控制字节文本），与 Character+Ctrl 编码路径无关。若 readline 未启用 application cursor（发送 CSI A 而非 SS3 OA 才能召回），说明 ?1h 同步未生效——按 RED 流程定位（先跑 test_native_input 聚焦）。

- [ ] **步骤 4：冒烟双后端全量运行**

运行：`ctest --preset linux-gcc-debug -R "SmokeInteractive" --output-on-failure`
预期：ZzTermSmokeInteractive_native 与 _contour 均 Passed（含新增输入步骤）。
若 Contour 后端方向键召回失败而 native 成功：查 Contour ?1 模式自管行为与 sendKeyEvent 映射，确属上游差异按既有规则在脚本注释钉住（对照 compat 用例 12 结论）。

- [ ] **步骤 5：docs/API.md 同步**

在 API.md 适当章节补充：

- 输入方向总述：前端经 sendText/sendKey 输入，所有外发字节（输入编码 + 终端回传）统一经 setOutputHandler 的 output 通道流出；未设置 handler 静默丢弃；
- `ZzTerminal::sendText(std::string_view)` / `sendKey(const ZzKeyEvent&)`：语义、模式联动（application cursor/keypad）、Contour 后端经类型映射委托其自家输入路径（ZzInputEncoder 为 native 内部组件）；
- 终端回传：DA1（CSI c → VT102 级最小集应答）、DSR 5n、CPR 6n，两后端均支持；DA 应答串实现相关（b 类分歧已钉住）。

注意 doxygen markdown 陷阱（行内 code 禁尖括号/禁以点开头/后禁紧跟顿号/禁 `#` 预处理词）。

- [ ] **步骤 6：四项验收**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure
cmake -S . -B build/m2-shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check --output-on-failure
doxygen Doxyfile
```

预期：ON 全绿（24 + 新增约 4 个测试）；OFF 全绿；shared 全绿；doxygen 零警告。

- [ ] **步骤 7：demo 实测（vim 交互）**

脚本化 PTY（pexpect，沿用 verify_smoke 模式）驱动 demo 内 vim：
- 启动 vim（经 sendText 输入 "vim t.txt\r"）；vim 启动查询 DA——native 应答 VT102 最小集 DA 串后 vim 应正常进界面（1049 备用屏）；
- 输入 i（插入）、打字、ESC（:wq 保存退出）；
- 断言：文件 t.txt 内容正确、退出后 shell 原屏恢复。
实测异常先对照 compat/单测定位后端归属，再按规格 5 风险节处理；Contour 侧 vim 异常记录现象并在报告钉住。附人工复测操作清单。

- [ ] **步骤 8：Commit**

```bash
git add examples/ZzTermSmoke/main.cpp tests/interactive/verify_smoke.py docs/API.md
git commit -m "feat(demo): stdin 经 Core 输入链路接通（InputTranslator + output 通道），冒烟加输入用例，API.md 同步"
```

---

## 自检记录

- **规格覆盖度：** 规格 4.1（facade send/backend 纯虚/native 持有 encoder 与 handler/Contour 映射委托）→ 任务 2；规格 4.2（?1 DECCKM、ESC=/ESC> 接线）→ 任务 2（ESC=/ESC> 无可观察编码路径，encoder getter 单测在任务 1，公开行为断言省略并在代码注释钉住）；规格 4.3（DA1/5n/6n 回传、setOutputHandler 实存、回传不标脏）→ 任务 3；规格 4.4（encoder 单测、native 集成、compat 输入对照、demo/冒烟、vim 实测）→ 任务 1/2/3/4/5 各自步骤；规格 4.5（错误处理）→ 任务 2 步骤 1（testNoHandlerSafe/testHandlerReplaceable）+ 任务 3 步骤 1（testUnknownDsrIgnored）；规格 4.6（验收四项 + demo 实测）→ 任务 5 步骤 6/7；规格 5 风险（Contour 键映射覆盖、DA 差异、encoder xterm 偏差）→ 任务 1 步骤 2 注、任务 2 步骤 4 注、任务 4 步骤 2 处置流程。
- **类型一致性：** `sendText(std::string_view)`/`sendKey(const ZzKeyEvent&)`（任务 2 定义于接口/facade/native/Contour/FakeBackend，任务 4/5 使用同一签名）；`emit(std::string_view)`（任务 2 定义，任务 3 使用）；`zzModifiers`/`zzKey`（任务 2 定义于 ZzContourConvert.h，ZzContourBackend.cpp 使用）；`InputTranslator`（任务 5 demo 本地，不跨文件）；测试辅助 `feed`/`keyEvent` 局部于各测试文件。
- **占位符扫描：** 无待定/TODO；三处"以库内实际 API 为准"（crispy::Flags 置位方法名、contour 写出 API、verify_smoke 既有步骤写法）均给出备选路径与报告要求，属对第三方代码的事实性不确定而非需求占位。
