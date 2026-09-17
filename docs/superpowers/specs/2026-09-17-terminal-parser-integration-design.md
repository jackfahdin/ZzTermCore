# Terminal 接入 Parser（M1 Core 链路）设计规格

> 2026-09-17 · 状态：已批准 · 对应里程碑：M1（Core 部分）

## 1. 目标与范围

打通真实数据链路：`bytes -> ZzVtParser -> ZzUtf8Decoder -> Terminal 语义 -> Screen`，
替换 `Terminal::feed` 的 M0 占位逐字节实现。

**纳入：**

- C0 全套语义（BEL/BS/HT/LF/VT/FF/CR）
- 基础 CSI：光标（CUU/CUD/CUF/CUB/CNL/CPL/CHA/VPA/CUP/HVP）、擦除
  （ED 0-2 / EL 0-2 / ECH）、插删（ICH/DCH/IL/DL）、滚动（SU/SD）、
  Save/Restore Cursor（CSI s/u）
- SGR：基本属性（0-9、22-29）与颜色（30-39、48、49、90-97、100-107，
  含 256 色与 RGB TrueColor 扩展参数）
- pending-wrap 语义修正（xterm 行尾行为）
- OSC 0/1/2 窗口/图标标题
- ESC 序列：7/8（DECSC/DECRC）、D/E/M/H（IND/NEL/RI/HTS）
- DCS：安全忽略（Parser 已保证有界）

**不纳入：**

- PTY、Qt Widget、ZzTermDemo（后续迭代）
- DEC 特殊图形字符集（ESC ( 0 等，G0-G3/SI/SO 状态机，随 M2 字符集设计）
- East Asian Width 真实区间表（见第 5 节，M2 接入）
- reflow、selection/search（M4）
- Golden 快照测试（Screen 输出稳定后，约 M2 前后引入）

## 2. 架构

`ZzTerminal` 不直接继承 `ZzParserSink`，改用嵌套私有类：

``` text
ZzTerminal
├── std::unique_ptr<ZzVtParser> parser_ // 构造/析构定义在 .cpp，前置声明即可
├── struct Sink : ZzParserSink  // 嵌套私有类，定义在 .cpp，持有 ZzTerminal&
├── ZzUtf8Decoder utf8_         // print 通道：onPrint(byte) -> utf8_.feed -> putChar(char32_t)
└── 画笔状态 pen_               // ZzCellAttributes + 前景/背景 ZzColor
```

- `Terminal.h` 只需前置声明，公开头不新增依赖；Sink 作为嵌套类天然
  可访问 Terminal 私有成员，无转发层。
- Terminal.h 中"暂不持有 parser 成员"的注释随之移除。
- Parser 只做语法 dispatch；模式解释、画笔、历史入栈、Alternate Screen
  语义全部集中在 Terminal（Architecture.md 第 2/7 节）。

### 文件划分

- `src/terminal/Terminal.cpp`：feed 骨架替换、Sink 定义、print/C0/OSC/ESC 语义
- `src/terminal/CsiDispatch.cpp`：CSI 语义（同一类的成员函数分文件实现）
- `src/terminal/Sgr.cpp`：SGR 到画笔的映射

## 3. 语义细则

### pending-wrap（xterm 行尾语义）

`ZzScreen` 增加 wrap-pending 状态：

- 最后一列写入后，光标停在该列并置标志；
- 下一个可打印字符到达且 DECAWM 开时：标 wrapped、换行（滚动区下沿
  则滚动）、清标志，然后写入；
- CR/LF/光标定位/擦除会清除标志；
- Save/Restore Cursor 连带保存恢复该标志。

### SGR 映射

- 参数缺省（kOmitted）或空参数按 0（reset）处理；多参数顺序生效；
- 38/48 扩展：`;5;n` 为 256 色，`;2;r;g;b` 为 RGB；参数不足时静默忽略；
- `4` 统一映射为单下划线（`4:0-4:5` 子参数语法待 Parser 支持 ':' 后扩展）；
- SGR 只改画笔，不修改已有 Cell；print 落格时套用当前画笔。

### CSI 参数约定

- kOmitted 或 0 按规范默认值解释（如 CUP 缺省为 1）；
- 数值钳到网格范围；非法 final byte 静默忽略；
- DCS hook/put/unhook 全部空实现。

### 错误处理

远端输入一律不可信（Architecture.md 第 15 节）：不抛异常、不产生
未定义行为、不因畸形输入进入死循环。Parser 层负责长度/数值上限，
Terminal 层负责语义层面的钳位与忽略。

## 4. 测试策略

- `tests/unit/test_terminal_core.cpp`：C0 全套、pending-wrap（行尾写满后
  再写、CR 清除标志、DECAWM 关闭时不换行）、OSC 0/1/2 标题、
  ESC 7/8/D/E/M/H
- `tests/unit/test_terminal_csi.cpp`：光标各族、ED/EL/ECH、ICH/DCH/IL/DL、
  SU/SD、参数缺省与钳位
- `tests/unit/test_terminal_sgr.cpp`：属性开/关、16/bright/256/RGB、
  复合 SGR、reset
- 端到端场景：模拟彩色 ls 输出、vim 式清屏重绘，断言 Screen 文本 +
  属性 + 光标终态；抽 2-3 个场景做全切分点两段喂入终态一致性验证

## 5. 字符宽度接入点（决策：4a）

定义自由函数 `zzCellWidthOf(char32_t)`（声明于 `include/ZzTerm/UnicodeWidth.h`），
当前实现恒返回 narrow（1）。putChar 经由此函数决定宽度，M2 用真实
East Asian Width 区间表替换实现时不动 Terminal 逻辑。本次中文等宽字符
会按窄格渲染（已知偏差，M2 修正）。

## 6. Definition of Done

- 上述测试全部通过（GCC 静态/动态两种构建）
- `docs/VT-Xterm-Checklist.md` 对应项勾选（C0/C1、Cursor/CSI、
  Erase/Insert/Delete/Scroll、SGR 大部分、OSC 标题项）
- `Terminal.h` 的 M0 占位注释移除，公开 API 注释与实际行为一致
- `docs/API.md` 同步 Terminal 语义说明
- `doxygen Doxyfile` 零 warning
