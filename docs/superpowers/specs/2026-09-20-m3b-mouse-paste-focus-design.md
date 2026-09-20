# M3b 设计：鼠标上报 + bracketed paste + 焦点上报

- 日期：2026-09-20
- 分支：contour
- 前置：M3a 已合并 master（键盘输入 + output 通道 + DA/DSR 回传）
- 架构依据：Architecture.md §9（输入语义事件统一入口）、§19（M3：tmux、mouse、bracketed paste、OSC、DEC modes）

## 1. 背景与目标

M3a 打通了输入方向主干（send 链路、统一 output 通道、回传、模式接线框架）。ZzInputEncoder 的鼠标/粘贴/焦点三路编码（encodeMouse/encodePaste/encodeFocus，M1 实现）随模式 setter 一同就位但仍零测试、零接线。M3b 完成 M3 剩余输入项，达成 vim 鼠标可用、粘贴不逐字回显、焦点上报可追踪。

## 2. 范围

### 2.1 包含

- native 模式接线（dispatchDecPrivate）：?9/?1000/?1002/?1003（鼠标上报 X10/Normal/ButtonEvent/AnyEvent，h 互斥设置、l 回 None）、?1006（SGR 编码开关）、?2004（bracketed paste）、?1004（焦点上报）；复用 M3a 的 emit/output 通道；
- facade API：`ZzTerminal::sendMouse(const ZzMouseEvent&)`、`sendPaste(std::string_view)`、`sendFocus(bool focused)`，经 ZzTerminalBackend 新纯虚下发；
- Contour 适配：ZzMouseButton → vtbackend::MouseButton 映射（注意顺序差：我方 None/Left/Middle/Right，contour Left/Right/Middle/…）、ZzMouseAction → sendMousePress/Move/ReleaseEvent、sendPaste、sendFocusIn/OutEvent；Modifiers 复用 M3a 的 zzModifiers；PixelCoordinate 默认、uiHandledHint=false；
- encoder 三路单测补齐（过滤规则、经典 +32 偏移、SGR 1006 格式、释放码、修饰位、坐标超 223 丢弃、包裹/透传、CSI I/O）；发现与 xterm 偏差以 xterm 为准修正并报告；
- Ctrl+非字母 C0 映射（M3a 终审留项）：xterm 对 Ctrl+[@\]^_ 等的 C0 编码补全 + 测试；
- compat 强对照：?1000h+?1006h 后 sendMouse 字节、?2004h 后 sendPaste、?1004h 后 sendFocus；Contour 分歧按 b 类钉住；
- demo：InputTranslator 扩展解析宿主鼠标序列（SGR 1006 与经典 X10 两种）→ sendMouse；冒烟用 pexpect 合成点击序列 + vim mouse=a，pyte 跟踪光标断言点击定位生效；
- tmux 实测：本机无 tmux，列人工操作清单，不作为 CI 验收。

### 2.2 明确排除

- OSC 扩展（8 超链接、52 剪贴板等）→ 后续里程碑单独评估；
- kitty 键盘/鼠标协议、像素级鼠标（1016）→ 远期；
- grapheme 聚簇维持 M5；resize reflow 维持 M4；
- tmux 环境安装与 CI 化。

## 3. 现状盘点（已核实的落点）

- `ZzInputEncoder`：encodeMouse（InputEncoder.cpp:156-216，X10 仅按下/Normal 无移动/ButtonEvent 按下拖动/AnyEvent 任意移动过滤，经典 CSI M +32 三字节与 SGR 1006 双编码）、encodePaste（:218-226）、encodeFocus（:228-233）已实现，模式 setter 齐全，零测试；
- M3a 交付物直接复用：facade send 形态、backend 纯虚模式、native emit/encoder_、dispatchDecPrivate 模式接线框架、ZzContourConvert.h 输入方向节（zzModifiers/zzKey）、适配器 send 后 flushReplies；
- Contour 输入 API：sendMousePress/Move/ReleaseEvent（Terminal.hpp:932-944）、sendPaste（:947）、sendFocusIn/OutEvent（:945-946）；vtbackend::MouseButton 枚举顺序与我方不同（映射表处理）；
- demo InputTranslator（M3a）已有转义序列状态机，扩展一个鼠标分支即可；
- 本机环境：vim 可用、tmux 缺失（验收方案随之调整，见 4.4）。

## 4. 设计

### 4.1 模式接线（native dispatchDecPrivate）

- ?9h/l → X10 / None；?1000h/l → Normal / None；?1002h/l → ButtonEvent / None；?1003h/l → AnyEvent / None；h 为互斥设置（设新模式覆盖旧模式），l 回 None；
- ?1006h/l → encoder_.setMouseSgrEncoding；
- ?2004h/l → encoder_.setBracketedPaste；
- ?1004h/l → encoder_.setFocusReporting；
- 标脏规则同 M2/M3a 既有（模式切换标脏）。

### 4.2 facade 与后端

- facade：`sendMouse`/`sendPaste`/`sendFocus` 返回 void，注释钉住"未设置 output handler 静默丢弃""编码依当前终端模式"；
- native：encoder_.encodeMouse/encodePaste/encodeFocus → emit；
- Contour：ZzContourBackend 新增 sendMouseEvent/sendPasteText/sendFocusEvent（命名以实现为准），适配器委托 + flushReplies；按钮映射表集中在 ZzContourConvert.h 输入方向节。

### 4.3 encoder 修正（如发现偏差）

- Ctrl+非字母 C0 映射：xterm 对 Ctrl+2（NUL）、Ctrl+3-7（ESC/FS/GS/RS/US 即 [\]^_ 键位）、Ctrl+8（DEL）补全；Character 分支在字母规则之前按 xterm 表处理，注释记录依据；
- encodeMouse 既有实现若与 xterm 有出入（修饰位布局、释放码、AnyEvent 移动码 35 等），以 xterm ctlseqs 为准修正。

### 4.4 测试

- encoder 单测（test_input_encoder.cpp 扩充）：encodeMouse 四模式过滤、经典编码 +32/坐标 1 起始/释放码 3/修饰位/超 223 丢弃、SGR 1006 M/m 结尾、encodePaste 开关两态、encodeFocus 开关两态、Ctrl+非字母 C0 表；
- native 集成（test_native_input.cpp 扩充或新文件）：模式接线后 sendMouse/sendPaste/sendFocus 编码切换、互斥设置覆盖、handler 未设不崩；
- compat（test_backend_compat.cpp 新用例）：?1000h+?1006h 后 sendMouse(Press,Left,4,2) 强对照、?2004h 后 sendPaste 强对照、?1004h 后 sendFocus(true/false) 强对照；Contour 分歧回退 b 类并注释钉住；
- demo/冒烟：
  - InputTranslator 新增鼠标分支：SGR 1006（CSI < code;x;y M/m）与经典 X10（CSI M cb cx cy，-32 偏移）两种解析 → ZzMouseEvent → sendMouse；
  - verify_smoke.py 新增：vim 以 mouse=a 启动，pexpect 合成 SGR 点击序列喂 demo stdin，pyte 跟踪屏幕光标位置断言点击定位生效（双后端）；
- 人工清单：tmux 鼠标（窗格切换/调整大小）、vim 可视模式鼠标选择。

### 4.5 错误处理

- 鼠标坐标越界（负/超屏）：编码层按协议处理（经典编码超 223 丢弃，既有）；facade 不做额外钳制（前端契约）；
- handler 未设置静默丢弃（M3a 既有语义）；
- 非法鼠标序列（demo 解析侧）：丢弃不崩。

### 4.6 验收（沿用既有 DoD）

- ON 全绿（27 + 新增）、OFF 全绿、shared 全绿、doxygen 零警告；
- 冒烟双后端全过（含 vim 鼠标点击定位步骤）；
- vim 脚本化实测：mouse=a 下点击移动光标、滚轮滚动。

## 5. 风险与对策

- **Contour 鼠标编码细节差**（修饰位/释放码/移动码）：compat 强对照兜底，分歧回退 b 类钉住；
- **vim mouse=a 的 SGR 1006 依赖**：vim 默认请求 ?1006h+?1002h，若实测 vim 只用经典编码，冒烟断言以实际为准并注释；
- **按钮枚举顺序差映射错**：映射表集中在 ZzContourConvert.h，左/中/右/滚轮逐值单测（经 compat sendMouse 样例覆盖）；
- **pyte 光标跟踪精度**：vim 光标经 1049 备用屏渲染，pyte 独立解释与 Core 视图可能有时序差——settle 后取值， flaky 时改断言 vim 状态行行列号。

## 6. 里程碑外后续（记录不实施）

- M4：10 万行 scrollback、selection/copy/search/highlight、resize reflow；
- M5：grapheme 聚簇（UAX #29）、Fuzz、性能、macOS；
- OSC 8/52、DA 能力抬级、kitty 协议：按真实应用需求再评估；
- M2 终审延后族（xterm 细节差异、上游 contour 两项）维持原裁定。
