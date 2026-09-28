# M3a 设计：键盘输入编码 + output 通道 + 终端回传

- 日期：2026-09-20
- 分支：contour
- 前置：M2 已合并 master（EAW 宽度表 + DEC 私有模式）
- 架构依据：Architecture.md §9（输入语义事件统一入口、UI 不直接拼 escape sequence、IME commit text 才进 Core）、§19（M3 里程碑）

## 1. 背景与目标

M1 已交付完整的输入编码组件（`include/ZzTerm/Input.h` + `src/input/InputEncoder.cpp`，ZzKeyEvent/ZzMouseEvent 类型与 encodeKey/encodeMouse/encodePaste/encodeFocus 全部实现），但它是孤岛，存在 4 处集成断点：

1. facade 没有输入 API——ZzTerminal 上没有 send 系列方法，encoder 无任何单测；
2. 模式位没接线——驱动编码的 DEC 模式（?1 DECCKM、keypad ESC=/ESC>）在 native dispatch 中仍是安全忽略；
3. output 通道是空的——`ZzNativeBackend::setOutputHandler` 为 no-op（注释指明 M3 启用），编码字节没有出路；DA 响应/光标上报等回传缺失（vim/tmux 启动查询 DA，不答会异常）；
4. Contour 侧未接——适配层 output 通道 M1b 已通，但 send 路径与类型映射没有。

M3a 目标：打通键盘输入方向（前端按键 → 编码字节 → PTY）与终端回传（DA/DSR 应答），demo 中 vim 可交互。鼠标/粘贴/焦点归 M3b。

## 2. 范围

### 2.1 包含

- facade 输入 API：`ZzTerminal::sendText(std::string_view)`、`ZzTerminal::sendKey(const ZzKeyEvent&)`，返回 void；
- `ZzTerminalBackend` 新增 send 系列纯虚；native 后端持有 `ZzInputEncoder` 并实存 `outputHandler_`；
- native 模式接线：`?1h/l`（DECCKM）→ setApplicationCursorKeys；`ESC=`/`ESC>`（DECKPAM/DECPNM，dispatchEsc）→ setApplicationKeypad；
- native 回传：`CSI c`（DA1）→ `\x1b[?1;2c`；`CSI 5n` → `\x1b[0n`；`CSI 6n`（CPR）→ 真实光标位置（1 起始换算）；均经 output handler 发出；
- Contour 适配层：ZzKeyEvent → contour Key/KeyboardModifiers 映射（扩展 ZzContourConvert.h），委托 contour 自家 sendKeyEvent/sendCharEvent；
- ZzInputEncoder 单测补齐（M1 遗留零覆盖）：encodeText/encodeKey 全表 + 模式位影响；
- compat 新增输入方向对照：双后端 output 捕获比字节（可强对照项）/ b 类分别断言（DA 应答等实现相关项）；
- demo：ZzTermSmoke 接通 stdin → sendText/sendKey；verify_smoke.py 加按键回显端到端用例。

### 2.2 明确排除（用户已确认）

- 鼠标（?9/?1000/?1002/?1003/?1006、sendMouse）、bracketed paste（?2004、sendPaste）、焦点上报（?1004、sendFocus）→ M3b；
- kitty 键盘协议 → 远期；
- grapheme 聚簇（UAX #29）维持 M5；
- encoder 的 encodeMouse/encodePaste/encodeFocus 测试随 M3b 补（与对应模式接线同批）。

## 3. 现状盘点（已核实的落点）

- `ZzInputEncoder`（Input.h/InputEncoder.cpp，240 行）实现完整：方向键普通/application 两态、F1-F12、修饰键编码、encodeText 透传；模式同步 set 系列齐全；零单测；
- `ZzTerminalBackend::setOutputHandler` 接口 M1b 已存在；Contour 适配层已实现（接 contour output 钩子），native 为 no-op（ZzNativeBackend.cpp:84-87，注释指明 M3）；
- Contour 输入 API 齐全：sendKeyEvent/sendCharEvent（screen/Terminal.hpp:926-927），模式自管；类型转换层 ZzContourConvert.h（M1b）可扩展；
- native dispatchEsc（ZzNativeBackend.cpp:240+）当前对无 intermediate 的 ESC 序列已有分发结构，ESC=/ESC> 落入忽略分支；dispatchDecPrivate（M2）default 分支忽略 ?1；
- `ZzContourBackendAdapter` 的 output 通道 M1b 已验证（test_contour_adapter）。

## 4. 设计

### 4.1 数据流（统一出口）

- 前端在任一后端上只设一个 `setOutputHandler`，所有外发字节（输入编码 + 终端回传）都经它流向 PTY——两后端对称；
- facade：`sendText`/`sendKey` 返回 void，经 `ZzTerminalBackend` 新纯虚下发（backend 持有编码器，方案 A）；
- native：`ZzNativeBackend` 持有 `ZzInputEncoder encoder_` 与 `outputHandler_`；send 系列 → encoder → emit（emit = handler 已设则调用，未设则丢弃，文档钉住）；
- Contour：适配层映射类型后调 contour sendKeyEvent/sendCharEvent，字节经 contour output 钩子流出；ZzInputEncoder 是 native 内部组件，Contour 后端自己就是编码器——架构 §9 的统一入口由 facade 契约承载，注释钉住。

### 4.2 模式接线（native）

- `?1h/l`（DECCKM）：dispatchDecPrivate 新增 case 1 → `encoder_.setApplicationCursorKeys(set)`；标脏规则同 M2 既有（模式切换标脏）；
- `ESC=`（DECKPAM）/`ESC>`（DECPNM）：dispatchEsc 无 intermediate 分支新增 `=`/`>` final → `encoder_.setApplicationKeypad(true/false)`；
- Contour 侧模式由 contour 内部自管，无需映射。

### 4.3 回传（native output 通道启用）

- `setOutputHandler` 实存 handler（替换 no-op，原注释删除）；
- dispatchCsi 新增：
  - `case 'c'`（DA1，无参数）：emit `ESC[?1;2c`（VT102 级，xterm 兼容最小集）；带参数/intermediate 的 DA 变体安全忽略；
  - `case 'n'`（DSR）：参数 5 → emit `ESC[0n`；参数 6（CPR）→ emit `ESC[{行+1};{列+1}R`（真实光标位置，0 起始转 1 起始）；其余参数安全忽略；
  - 回传不标脏（屏幕无变化）；
- Contour 侧这些应答它自己发出（通道 M1b 已通），零工作量。

### 4.4 测试

- encoder 单测（新 tests/unit/test_input_encoder.cpp）：
  - encodeText 透传（含 UTF-8 多字节）；
  - encodeKey：方向键普通（CSI A/B/C/D）与 application（SS3 OA/OB/OC/OD）两态；Home/End/Insert/Delete/PgUp/PgDn；F1-F4（SS3）与 F5-F12（CSI ~）；修饰键编码（mod 值 = 1 + Shift1/Alt2/Ctrl4 组合，CSI 1;{mod}X 形式）；Enter/Tab/Backspace/Escape；Release 动作返回空串；
  - 模式位 set 后编码立即生效；
- native 集成测试（新 tests/unit/test_native_input.cpp，facade 公开 API）：
  - feed `?1h` 后 sendKey(Up) 编码从 CSI A 切为 SS3 OA，`?1l` 切回；
  - feed ESC= 后小键盘模式位生效（经编码行为间接断言）；
  - feed `CSI c` → handler 收到 `\x1b[?1;2c`；feed `CSI 5n` → `\x1b[0n`；写文本后 feed `CSI 6n` → CPR 行列与光标一致；
  - handler 未设置时 send/回传不崩；
- compat（test_backend_compat.cpp 新增用例）：
  - 输入方向强对照：双后端各挂 output 捕获，feed `?1h` 后 sendKey(Up)，比对发出字节（Contour application 模式同编码 SS3 OA）；`?1l` 后同；
  - CPR 强对照：双后端写相同文本后 feed `CSI 6n`，应答字节一致；
  - DA 应答：两后端应答串不同（Contour 有自己的 DA 串）→ b 类分别断言各自非空且以 CSI ? 开头，注释钉住研判；
- demo/冒烟：
  - ZzTermSmoke 接通 stdin → sendText/sendKey（方向键等功能键映射为 ZzKeyEvent）；
  - verify_smoke.py 新增：向 shell 发送按键序列，断言回显内容经 RenderView 可见（双后端）；
- 人工实测：demo 内 vim 交互（hjkl 移动、i 插入、:wq 退出），操作清单入报告。

### 4.5 错误处理

- sendText 非法 UTF-8：前端契约保证合法（Input.h 已注明），Core 不重复校验；
- output handler 未设置：send 与回传字节静默丢弃，不崩不抛；
- encoder 对未知/Release 动作返回空串（既有行为），emit 空串不调用 handler；
- CSI c/n 参数异常沿用既有"省略回退默认、未知安全忽略"约定。

### 4.6 验收（沿用既有 DoD）

- `ctest --preset linux-gcc-debug` ON 全绿（24 + 新增）；
- OFF（-DZZTERM_WITH_CONTOUR=OFF）构建与测试全绿；
- shared 构建全绿；
- `doxygen Doxyfile` 零警告（注意 docs markdown 与公开头注释陷阱）；
- demo 实测 vim 交互（人工或脚本化 PTY）。

## 5. 风险与对策

- **Contour 按键映射表不全**：contour Key 枚举与 ZzKeyEvent.Key 的覆盖差（如 F11/F12、Insert）→ 映射表在 ZzContourConvert.h 集中管理，未覆盖键显式忽略并在注释钉住；compat 用例选双方都有的键；
- **DA 应答串差异导致应用行为分支**：vim/tmux 按 DA 应答能力位启用特性，`\x1b[?1;2c`（VT102 级）能力声明低，应用降级为保守行为——可接受，M3b/后续按需抬级并在注释记录；
- **encoder 既有实现与 xterm 细节差**（如 F1-F4 修饰键编码形式）：单测以 xterm ctlseqs 为准编写；发现既有实现偏差时以 xterm 为准修正并在报告说明；
- **Contour application keypad 行为差**：contour 内部自管，若 compat 暴露分歧按既有规则回退 b 类钉住。

## 6. 里程碑外后续（记录不实施）

- M3b：鼠标/bracketed paste/焦点上报 + sendMouse/sendPaste/sendFocus + encoder 对应测试 + tmux 实测；
- M2 终审延后族（xterm 细节差异、上游 contour 两项）维持原裁定；
- DA 应答能力抬级（xterm 级 ?62;…）、kitty 键盘协议：按真实应用需求再评估。
