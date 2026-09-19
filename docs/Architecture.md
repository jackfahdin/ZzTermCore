# ZzTermCore 技术设计与开发规范

> 本文档为 v1.0（自研 VT 引擎路线）原始设计。现行主设计文档为 docs/Architecture-v2.md（v2.1，Contour 后端整合版）；本文档保留作历史参考与 ZzNativeBackend 语义契约来源，其中的 M0–M6 里程碑编号已废止，仅按 v1 语义解读。

> v1.0 · C++20 · CMakeLists.txt + CMakePresets.json · 2026-09-17

## 1. 项目定位

ZzTermCore 是独立、跨平台、自研的终端模拟器组件，不依赖 ZzClawTerm
进行开发、测试或功能验证。ZzClawTerm 未来仅是下游使用者之一。

项目交付：纯 C++20 Terminal Engine、Qt 6 QWidget
官方组件、可选关键词/正则高亮模块、Unix PTY/Windows ConPTY、独立
ZzTermDemo、Unit/Compatibility/Regression/Fuzz/Benchmark，以及持续更新的
API 文档和 VT/xterm Checklist。

## 2. 最高级架构约束

Core 必须平台无关，不得直接依赖 Qt、libssh2、OpenSSL、Windows API、POSIX
PTY、OpenGL/QRhi、网络或 GUI API。

``` text
bytes -> UTF-8/VT/xterm Parser -> Terminal State
      -> Cell/Line/Screen -> Scrollback -> RenderView

Frontend semantic events -> InputEncoder -> bytes
```

`ZzRenderView` 是 Core 与 Renderer 的稳定边界。UI、PTY、SSH、Highlight
均属于外层模块。

## 3. 仓库与模块

``` text
ZzTermCore/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
├── include/ZzTerm/
├── src/{parser,unicode,screen,history,input,terminal}/
├── highlight/
├── theme/                 # ZzTermTheme：配色主题/Palette/iTerm2 导入（见第 24 节）
├── widget/
├── pty/{unix,windows}/
├── examples/ZzTermSmoke/  # 控制台冒烟 Demo（调试工具）
├── examples/ZzTermDemo/
├── tests/{unit,compatibility,regression}/
├── fuzz/
├── benchmarks/
└── docs/
```

Targets：`ZzTermCore`、`ZzTermHighlight`、`ZzTermTheme`、`ZzTermWidget`、`ZzTermPty`、`ZzTermDemo`。依赖只能由外向内。

## 4. CMake、Presets、静态/动态库

要求 CMake \>= 3.19、C++20，维护标准 `CMakeLists.txt` 与
`CMakePresets.json`，推荐 Ninja。

必须同时支持静态库和动态库。优先遵循 CMake
`BUILD_SHARED_LIBS`，动态库使用统一 `ZZTERM_API`
导出宏；静态/动态构建的公开 API 语义必须一致。

至少维护：

-   linux-clang-debug / release
-   linux-gcc-debug
-   windows-msvc-debug / release
-   macos-clang-debug

可提供 static/shared variant。开发者不应依赖手工拼接大量 CMake 参数。

## 5. 开发环境与平台

主开发采用 Ubuntu + Clang + Ninja + CMake。Linux
PTY、bash/zsh、vim、tmux、htop、less、ncurses 是天然测试环境。

CI 早期覆盖 Ubuntu Clang、Ubuntu GCC、Windows MSVC；macOS 尽早加入 Apple
Clang。OpenHarmony 作为 portability target，Core 必须保持可移植，OHOS
UI/PTY 由适配层承担。

## 6. 核心数据模型

### Cell / Grapheme

禁止假设 `1 code point == 1 cell`。Cell 必须表达
Empty、Narrow、Wide、WideContinuation、grapheme 与显示属性。颜色覆盖
default、ANSI 16、bright、256、RGB TrueColor；属性预留
bold、faint、italic、underline
variants、blink、inverse、invisible、strikethrough、protected。持续监控
`sizeof(ZzCell)`。

### Line

区分 logical line、physical row、hard newline、soft wrap。公共 API
不暴露底层 vector。

### Screen

支持 Primary/Alternate Screen、Cursor、Saved Cursor、Scroll Region、Tab
Stops、Origin/Insert Mode、Dirty Tracking。Screen
只负责工作区，不知道历史后端类型。

### Scrollback

第一阶段采用 chunked RAM history；架构允许未来扩展 Hot RAM / Warm LZ4 /
Cold mmap-file。百万行是扩展目标。

## 7. VT/ANSI/xterm Parser

Parser 必须增量、可独立测试、可 Fuzz，支持任意 chunk 边界。状态至少包含
Ground、Escape、CSI Entry/Param/Intermediate、OSC String、DCS
Entry/Data。

Parser 只负责语法 dispatch，Terminal 负责语义。禁止巨型 switch 同时承担
parse、Screen
修改和绘图。未知、畸形、超长序列必须有限长缓存和安全恢复。具体功能以
`VT-Xterm-Checklist.md` 为实施基线。

## 8. Unicode/CJK

早期实现分片 UTF-8、非法 UTF-8、East Asian Width、combining
mark、variation selector、emoji/ZWJ 的可扩展 grapheme 表达以及 wide
continuation 一致性。

Copy/Search/Reflow 基于 logical line/grapheme。Unicode
数据版本必须可追踪；兼容问题必须留下 Regression Test。

## 9. Input Encoder

UI 不直接拼 escape sequence。统一以
`ZzKeyEvent`、`ZzMouseEvent`、Paste、Focus 等语义事件进入
`ZzInputEncoder`。

支持 Unicode text、方向键、Home/End、Insert/Delete、PgUp/PgDn、F
keys、modifier、application cursor/keypad、bracketed paste、xterm
mouse、focus reporting。IME composition 属于前端，commit text 才进入
Core。

## 10. RenderView 与 Qt Renderer

`ZzRenderView` 是稳定只读渲染 API，Renderer 不访问 Core 私有容器。

官方 `ZzTermWidget` 使用 Qt 6 QWidget + QPainter CPU Renderer，不强制
OpenGL/QRhi。要求 dirty row/cell 局部刷新、背景/文本 run
batching、cursor blink 局部刷新、DPI/font fallback 支持。Terminal
palette 与应用主题分离。

## 11. 关键词/正则高亮

高亮不是 VT Core 协议语义，独立为 `ZzTermHighlight`，包含
`ZzKeywordMatcher`、`ZzRegexMatcher`、`ZzDecoration`。依赖只能是
`ZzTermHighlight -> ZzTermCore`。

Decoration 使用稳定 logical range，不修改 Cell 原始 VT
属性。推荐绘制优先级：

`Terminal Style -> Keyword -> Search -> Selection -> Cursor`

## 12. PTY 与独立 Demo

项目不依赖 ZzClawTerm 验证。`ZzTermPty`：Linux/macOS 使用
forkpty/openpty，Windows 使用 ConPTY。

`ZzTermDemo` 是真正可交互的小型本地终端，可运行
bash/zsh/PowerShell、vim、tmux、htop、less。建议提供可开关 Terminal
Inspector，显示 size、cursor、screen、parser state、DEC
modes、history、dirty region、最近 VT sequence、paint timing
和内存统计。

## 13. Resize/Reflow、Search/Copy

严格区分 hard newline 与 soft wrap。列数变化时仅 soft-wrapped logical
line reflow，并正确映射 cursor、selection/search anchor 与 wide
grapheme。

Search 不得把全部历史拼成巨大字符串；按 logical line/chunk
扫描，未来可加增量索引。Copy 正确处理 soft wrap、hard newline、trailing
blanks、wide cell、grapheme。

## 14. 性能目标

常规目标：10 万行 scrollback 流畅滚动和可用搜索；大量输出期间 UI
保持响应；resize 无明显秒级冻结；shell/vim/tmux 无明显掉帧。

百万行是扩展目标。禁止永久采用 `historyLines × columns × sizeof(Cell)`
的朴素历史矩阵。所有优化由 benchmark/profiler 数据驱动。

## 15. 安全与健壮性

远端 bytes 全部视为不可信。限制 OSC/DCS payload、CSI
参数数量和数值范围；防整数溢出、超大 resize、非法 UTF-8、parser
死循环和无界内存增长。

建立 UTF-8 decoder、VT Parser、feed+resize 的 Fuzz target。

## 16. 测试体系

必须包含 Unit、Golden、Compatibility、Regression、Fuzz、Benchmark。

Compatibility 至少实际验证
bash/zsh、vim/neovim、nano、less、top/htop、tmux、git log、256
color、TrueColor、中文、IME、mouse、bracketed paste、OSC 8。

所有兼容 bug 必须执行：复现 -\> 最小测试 -\> Test FAIL -\> 修复 -\> Test
PASS。禁止只修代码不留测试。

## 17. API 文档实时同步

API 文档属于正式交付物，必须与源码实时同步。所有公开 API 使用标准
Doxygen 中文注释。

新增/删除公开类或函数、修改参数/返回值、ownership/lifetime、线程安全、错误行为、ABI
行为时，必须同步更新接口文档。CI 应生成 Doxygen 文档并检查 warning。

`docs/API.md` 维护高层 API 说明；详细接口以源码 Doxygen
自动生成结果为准。

``` cpp
/**
 * @brief 向终端模拟器输入原始字节流。
 *
 * 输入可在 UTF-8 字符或 VT 控制序列中间截断。
 * 解析器必须保存未完成状态，并在后续 feed() 中继续处理。
 *
 * @param data 输入的原始字节流。
 * @return 本次输入产生的终端状态变化。
 */
ZzTermChanges feed(std::span<const std::byte> data);
```

API 文档更新属于代码评审的 Definition of
Done，不允许"代码先合并，文档以后补"。

## 18. 中文注释规范

标识符使用英文，公开类型使用 `Zz`
前缀；注释和项目文档以中文为主；协议名称保留标准英文。

公共 API 必须有中文
Doxygen。复杂逻辑解释"为什么"，禁止无价值的逐行翻译式注释。协议实现应注明标准名称/编号。源文件统一
UTF-8。

## 19. 开发里程碑

-   M0：仓库、CMake/Presets、静态/动态构建、Core
    API、测试框架、Doxygen。
-   M1：UTF-8、基础 Parser/Screen/SGR，Demo 可运行 shell。
-   M2：alt screen、scroll
    region、input、IME、256/TrueColor，vim/nano/less 可用。
-   M3：tmux、mouse、bracketed paste、OSC、DEC modes。
-   M4：10 万行 scrollback、selection/copy/search/highlight、reflow
    第一版。
-   M5：Unicode edge cases、Fuzz、百万行实验、性能优化、macOS。
-   M6：API/ABI 收敛、静态/动态发布、兼容矩阵稳定、OpenHarmony
    适配验证准备。

## 20. Definition of Done

-   Core 在 MSVC/GCC/Clang 下以 C++20 独立构建；
-   静态/动态库均可构建、安装和链接；
-   Core 无 Qt/PTY/SSH 依赖；
-   ZzTermDemo 独立运行；
-   常见 TUI 通过兼容测试；
-   中文/IME/TrueColor/mouse/alternate screen 可用；
-   10 万行历史达到目标；
-   Fuzz/Regression/Benchmark 建立；
-   API Doxygen 与 `docs/API.md` 实时同步；
-   VT/xterm Checklist 与实际实现/测试状态一致。

## 21. 明确禁止

禁止 Core 引入 Qt；禁止 Parser 直接绘图；禁止 Screen 直接知道
file/buffer history；禁止一个 codepoint 永久等于一个
Cell；禁止每帧扫描全部历史；禁止 resize
无条件复制全部历史；禁止远端输入触发无界容器；禁止无测试的兼容性特判。

## 22. 首批任务

1.  CMakeLists + CMakePresets + static/shared + CI；
2.  Doxygen/API 文档流水线；
3.  UTF-8 decoder；
4.  VT parser state machine；
5.  Cell/Line/Screen invariants；
6.  Terminal semantic dispatch；
7.  QPainter renderer；
8.  Unix PTY + ZzTermDemo；
9.  InputEncoder + IME；
10. alternate screen/modes；
11. chunked scrollback；
12. selection/copy/search/highlight；
13. resize/reflow；
14. Windows ConPTY；
15. Fuzz/Benchmark/Compatibility suite。

第一次正式设计评审必须优先审查
Cell、Line、Screen、Scrollback、RenderView 和公开 API 生命周期模型。

## 23. 换行、字体缩放、DPI 与终端网格尺寸

本章节属于 ZzTermCore 的正式设计要求。实现时必须严格区分 Terminal Core 的字符网格语义与 GUI 层的像素、字体、DPI 语义。

### 23.1 设计原则

ZzTermCore Core 只处理终端字符网格，不感知：

-   字体名称；
-   字体大小；
-   DPI；
-   Device Pixel Ratio；
-   Widget 像素宽高；
-   应用 UI 缩放比例；
-   Retina / HiDPI；
-   操作系统显示缩放。

Core 对终端尺寸的唯一认知应为 columns × rows 的字符网格尺寸。该载体
已存在：`ZzSize`（include/ZzTerm/Types.h，`int cols` / `int rows`），
`ZzTerminal::resize(int cols, int rows)` 与 `ZzPty::resize(int cols, int rows)`
均以它为语义基准——本节不引入并行类型，实现时继续使用 `ZzSize`。

禁止向 Core 传递以下信息：

```text
pixelWidth
pixelHeight
fontSize
dpi
devicePixelRatio
uiScale
```

正确的职责关系为：

```text
Window / DPI / Font
        ↓
ZzTermWidget
        ↓
ZzFontMetrics
        ↓
计算 columns × rows
        ↓
   ┌────┴────┐
   ↓         ↓
ZzTermCore  ZzTermPty
 resize()    resize()
   ↓
Reflow
```

该设计必须保证未来 Qt、macOS Native UI、OpenHarmony UI 或其他 Renderer 均可使用相同 ZzTermCore。

---

### 23.2 Hard Newline 与 Soft Wrap

终端必须严格区分：

```text
Hard Newline
```

与：

```text
Soft Wrap
```

Hard Newline 表示应用实际产生的逻辑换行。

Soft Wrap 表示由于当前终端列数不足，由终端显示系统自动产生的物理换行。

例如终端宽度为 10 columns：

```text
1234567890ABCDE
```

可能显示为：

```text
1234567890
ABCDE
```

但逻辑内容仍然是：

```text
1234567890ABCDE
```

第二个 physical row 不得被视为包含真实 `\n`。

当终端宽度扩大后，应允许重新排版为：

```text
1234567890ABCDE
```

而真实输入：

```text
1234567890\nABCDE
```

必须始终保持两个 logical lines。

因此 Line 模型必须永久保留：

-   hard newline；
-   soft wrap；
-   logical line；
-   physical row；

之间的关系。

Copy、Search、Selection、Resize 和 Reflow 均必须基于该语义。

---

### 23.3 Auto Wrap 与 Wrap Pending

必须正确实现 DEC Auto Wrap Mode（DECAWM）。

特别注意终端右边界的 `wrap pending` 状态。

字符写入最后一列后，不应简单立即执行换行。终端可能进入 pending wrap 状态，并在后续 printable character 到达时执行自动换行。

必须建立专门测试覆盖：

-   最后一列写入；
-   最后一列后继续写入；
-   CR；
-   LF；
-   BS；
-   Cursor Movement；
-   Erase；
-   DECAWM 开/关；
-   CJK 双宽字符位于右边界；
-   combining character 位于右边界；
-   wide character continuation。

禁止使用简单：

```cpp
if (++column >= columns)
    newLine();
```

作为完整 Auto Wrap 实现。

---

### 23.4 Window Resize

用户调整窗口尺寸时：

```text
Widget Pixel Size
        ↓
扣除 margin / scrollbar / UI
        ↓
Terminal Viewport Pixel Size
        ↓
Cell Width / Cell Height
        ↓
columns × rows
        ↓
ZzTermCore::resize()
        +
ZzTermPty::resize()
```

Core 与 PTY 接收的均为字符网格尺寸，而不是像素尺寸。

PTY resize：

```text
Linux/macOS
    ↓
TIOCSWINSZ / PTY

Windows
    ↓
ConPTY Resize
```

窗口 resize 过程中必须避免高频重复执行昂贵的完整历史 Reflow。

允许 Widget 层进行 resize 合并、延迟或 debounce，但最终尺寸必须正确同步给 Core 和 PTY。

---

### 23.5 Resize Reflow

终端列数变化时必须支持 Reflow。

基本规则：

-   Hard Newline 不参与跨逻辑行合并；
-   Soft Wrap 可以重新排版；
-   Wide Grapheme 不允许被拆成非法 Cell；
-   Cursor 必须映射到新的 logical position；
-   Selection anchor 必须保持；
-   Search Match 必须保持；
-   当前 Scrollback View Position 应尽可能保持；
-   Alternate Screen 的 Reflow 行为必须单独定义并测试。

位置模型应优先使用：

```text
Logical Line ID
+
Logical Character/Grapheme Offset
```

而不是只保存：

```text
physical row + column
```

否则 Resize 后 Selection/Search/View Anchor 很容易失效。

---

### 23.6 Terminal Font Zoom

ZzTermWidget 必须支持独立的 Terminal Font Zoom。

建议默认快捷键：

```text
Ctrl + +
Ctrl + -
Ctrl + 0
Ctrl + MouseWheel
```

其中：

```text
Ctrl + +
```

增大终端字体。

```text
Ctrl + -
```

减小终端字体。

```text
Ctrl + 0
```

恢复默认字体大小。

Font Zoom 只影响 Terminal 内容，不应自动改变应用 Toolbar、Tab、Menu、Icon 等 UI 元素。

字体缩放流程必须是：

```text
Font Size Changed
        ↓
重新计算 Font Metrics
        ↓
重新计算 Cell Geometry
        ↓
重新计算 columns × rows
        ↓
ZzTermCore::resize()
        ↓
Reflow
        ↓
ZzTermPty::resize()
        ↓
重新 Rasterize / Paint
```

禁止简单放大已经绘制好的 Terminal Bitmap。

字体必须重新 rasterize，以保证 125%、150%、175%、200% 等比例以及 HiDPI/Retina 环境下保持清晰。

---

### 23.7 Font Zoom 范围

建议提供可配置字体缩放范围，例如：

```text
Minimum: 6 pt
Default: User Profile
Maximum: 72 pt
```

具体上下限允许后续调整，但必须防止：

-   0 或负数；
-   极端字体导致 columns/rows 为 0；
-   超大字体造成异常内存分配；
-   高频滚轮造成重复昂贵 Reflow。

Widget 应对连续 Font Zoom 请求进行合理处理。

---

### 23.8 Application/UI Scale

必须区分：

```text
Terminal Font Zoom
```

与：

```text
Application/UI Scale
```

Terminal Font Zoom 只影响终端字符。

Application/UI Scale 影响：

-   Toolbar；
-   Tab；
-   Icon；
-   Padding；
-   Scrollbar；
-   Dialog；
-   Terminal Widget；
-   其他 UI 元素。

Application/UI Scale 原则上交由 Qt High DPI 和操作系统 DPI 系统处理。

ZzTermCore Core 不得知道 Application Scale。

---

### 23.9 High DPI / Device Pixel Ratio

ZzTermWidget 必须正确支持：

-   Windows Display Scaling；
-   Linux HiDPI；
-   macOS Retina；
-   Qt Device Pixel Ratio；
-   多显示器不同 DPI。

Renderer 应使用 Qt logical coordinate system，并正确处理实际 rasterization。

禁止假设：

```text
1 logical pixel == 1 physical pixel
```

也禁止缓存永久依赖启动时 DPI 的 Font Metrics。

---

### 23.10 Runtime DPI Change

必须支持应用运行过程中 DPI 变化。

典型场景：

```text
Laptop Display @ 125%
        ↓
拖动窗口
        ↓
4K Display @ 200%
```

发生 DPI / Screen Change 后：

```text
DPI Changed
    ↓
Invalidate Font/Glyph Metrics
    ↓
Recalculate Cell Geometry
    ↓
Recalculate Grid Size
    ↓
Core Resize
    ↓
PTY Resize
    ↓
Repaint
```

不得要求用户重启应用才能正确显示。

---

### 23.11 Font Fallback

Renderer 必须支持字体 fallback。

典型情况：

```text
ASCII
    ↓
Primary Monospace Font

CJK
    ↓
CJK Fallback Font

Emoji
    ↓
Emoji Fallback Font
```

例如：

```text
A 中 😀
```

三个 grapheme 可能由三个不同字体实际绘制。

但字体 fallback 不允许破坏 Terminal Grid。

无论实际 Glyph 来源为何：

-   Narrow Cell 仍占一个 Cell；
-   Wide Cell 仍占两个 Cell；
-   Combining Mark 不额外占 Cell；
-   Glyph advance 不得反向改变 Terminal Cell Width。

Terminal Grid Geometry 必须由终端自己的 Cell Metrics 控制，而不是由每个 fallback glyph 的自然 advance 决定。

---

### 23.12 Font Metrics

建议 `ZzTermWidget` 内部建立独立：

```text
ZzFontMetrics
```

负责：

-   Cell Width；
-   Cell Height；
-   Baseline；
-   Ascent；
-   Descent；
-   Underline Position；
-   Strikeout Position；
-   DPI；
-   Font Fallback；
-   Glyph Metrics Cache。

Renderer 不应在每个 Cell 绘制时重新查询完整字体信息。

字体、DPI 或 fallback 配置改变时必须使相关缓存失效。

---

### 23.13 Line Spacing

允许 Terminal Profile 配置适量 Line Spacing。

Line Spacing 属于 Renderer/Grid Geometry，不属于 ZzTermCore。

改变 Line Spacing 后应重新计算：

```text
Cell Height
    ↓
Rows
    ↓
Core Resize
    ↓
PTY Resize
```

不得直接修改 Core 的 Line 数据结构。

---

### 23.14 Font Zoom 时保持用户位置

如果用户当前位于 Scrollback 历史区域：

```text
History Line ~80000
```

执行 Font Zoom 或 DPI Change 后，不应无条件跳回 Terminal Bottom。

应尽量保持当前视图对应的 logical anchor。

同样适用于：

-   Selection；
-   Search Match；
-   Hyperlink；
-   Keyword Highlight。

建议 Viewport 使用稳定 logical anchor，而不是只记录 physical scroll row。

---

### 23.15 Grid Size 边界

必须处理极端窗口尺寸。

例如窗口过小时：

```text
columns < 1
rows < 1
```

不得向 Core 或 PTY 传递非法尺寸。

建议最低有效 Terminal Grid：

```text
1 × 1
```

Widget 可以在无法显示有效 Cell 时暂时停止内容绘制，但不得造成除零、负尺寸或超大 unsigned 转换。

---

### 23.16 性能要求

以下操作不得导致不必要的全历史扫描：

-   Window Resize；
-   Font Zoom；
-   DPI Change；
-   Scroll；
-   Cursor Blink。

Reflow 可以是昂贵操作，因此必须建立独立 Benchmark。

至少测试：

```text
1,000 lines
10,000 lines
100,000 lines
1,000,000 lines
```

关注：

-   Reflow Time；
-   Peak Memory；
-   Allocation Count；
-   UI Blocking Time。

对于百万行历史，应允许采用分块、lazy reflow 或其他增量策略。

---

### 23.17 模块职责总结

| 功能                 | 所属模块                                 |
| ------------------ | ------------------------------------ |
| Hard Newline       | ZzTermCore                           |
| Soft Wrap          | ZzTermCore                           |
| DECAWM             | ZzTermCore                           |
| Wrap Pending       | ZzTermCore                           |
| Resize Reflow      | ZzTermCore                           |
| CJK/Wide 边界换行      | ZzTermCore                           |
| Font Zoom          | ZzTermWidget                         |
| Ctrl +/-/0         | ZzTermWidget                         |
| Ctrl + MouseWheel  | ZzTermWidget                         |
| Font Fallback      | ZzTermRenderer                       |
| High DPI           | ZzTermWidget / Qt                    |
| Runtime DPI Change | ZzTermWidget                         |
| Retina             | ZzTermWidget / Qt                    |
| Window Resize      | ZzTermWidget                         |
| PTY Resize         | ZzTermPty                            |
| View Anchor        | ZzTermWidget + Core Logical Position |
| Font/DPI 信息进入 Core | **禁止**                               |
| Pixel Size 进入 Core | **禁止**                               |

注：`ZzTermRenderer` 指 ZzTermWidget 内部的渲染子组件（ZzFontMetrics /
Glyph Cache 的所在地），不是第 3 节 target 清单里的独立模块。

里程碑归属：Wrap Pending / DECAWM / 宽字符右边界 → M2；Resize Reflow 与
Logical Position 模型 → M4；Font Zoom / DPI / Font Metrics / Fallback →
随 ZzTermWidget 迭代（M4 之后）。

以上职责边界属于架构约束，后续不得为了实现方便将字体、DPI 或像素概念引入 ZzTermCore。

## 24. Terminal Theme / Color System

终端配色主题属于 ZzTermCore 项目的正式能力，但不属于应用程序 UI Theme。

必须严格区分：

```text
Terminal Theme
```

与：

```text
Application UI Theme
```

Terminal Theme 负责终端内容区域的颜色语义与 Palette。

Application UI Theme 负责 Toolbar、Tab、Button、Dialog、Navigation、Menu 等客户端 UI。

ZzTermCore 不负责应用程序整体 Dark/Light Theme。

---

### 24.1 模块职责

新增独立模块：

```text
ZzTermTheme
├── ZzTermColor
├── ZzTermPalette
├── ZzTermTheme
├── ZzTermThemeLoader
└── ZzItermColorSchemeLoader
```

整体关系：

```text
                     ZzTermCore
                         ▲
                         │
                    ZzTermTheme
                         ▲
                         │
                   ZzTermWidget
                         ▲
                         │
                    ZzTermDemo
```

ZzTermCore 负责保存 Terminal Color Semantic。

ZzTermTheme 负责：

-   ANSI Palette；
-   Default Foreground；
-   Default Background；
-   Cursor Color；
-   Cursor Text Color；
-   Terminal Selection 默认配色；
-   Terminal Theme 加载；
-   iTerm2 Color Scheme 加载。

ZzTermWidget / Renderer 负责将：

```text
Color Semantic
       +
Terminal Theme
```

解析为最终绘制颜色。

---

### 24.2 Terminal Color Semantic

Core 不应将所有颜色立即转换为最终 RGB。

必须保留颜色来源和语义。

建议颜色模型：

```cpp
enum class ZzTermColorType : uint8_t
{
    DefaultForeground,
    DefaultBackground,
    Indexed,
    Rgb
};

struct ZzRgbColor
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct ZzTermColor
{
    ZzTermColorType type;

    union
    {
        uint8_t index;
        ZzRgbColor rgb;
    };
};
```

具体实现允许调整，但必须能够无损表达：

```text
Default Foreground
Default Background
ANSI Indexed Color
xterm 256 Color
RGB TrueColor
```

注：该语义模型已在 Core 落地——`ZzColor`（include/ZzTerm/Cell.h）即
Default/Indexed/Rgb 三态的 32 位紧凑编码；Cell 的 foreground/background
两个独立字段即承载 DefaultForeground/DefaultBackground 语义（SGR 39/49
已映射为 `ZzColor::Default()`，见 Sgr.cpp）。实现主题系统时沿用/扩展
`ZzColor` 与 `ZzRgbColor` 语义，不再引入并行的 `ZzTermColor` 类型。

禁止 Parser 在收到：

```text
ESC[31m
```

时直接保存某个固定 RGB，例如：

```text
#FF0000
```

Core 应保存：

```text
Indexed Color 1
```

最终 RGB 由当前 Terminal Theme 决定。

---

### 24.3 Default Foreground / Background

必须将：

```text
DefaultForeground
DefaultBackground
```

作为独立颜色语义保存。

例如：

```text
ESC[39m
```

表示：

```text
Default Foreground
```

而不是：

```text
White
```

同理：

```text
ESC[49m
```

表示：

```text
Default Background
```

而不是：

```text
Black
```

这样用户运行时切换 Terminal Theme 时：

```text
Dracula
   ↓
Solarized Dark
```

历史内容无需重新解析即可立即应用新主题。

---

### 24.4 ANSI 16 Color Palette

ANSI 0～15 色必须由 Terminal Theme 提供。

推荐结构：

```cpp
struct ZzTermPalette
{
    std::array<ZzRgbColor, 16> ansiColors;
};
```

映射：

```text
0  Black
1  Red
2  Green
3  Yellow
4  Blue
5  Magenta
6  Cyan
7  White

8  Bright Black
9  Bright Red
10 Bright Green
11 Bright Yellow
12 Bright Blue
13 Bright Magenta
14 Bright Cyan
15 Bright White
```

注意这些名称表示 ANSI color slot，而不是固定 RGB。

例如：

```text
ANSI Red
```

在不同 Terminal Theme 中允许对应不同 RGB。

---

### 24.5 xterm 256 Color

Indexed Color：

```text
0 ～ 255
```

必须支持完整 xterm 256-color 模型。

推荐解析规则：

```text
Indexed 0～15
      ↓
Terminal Theme ANSI Palette

Indexed 16～231
      ↓
xterm 6×6×6 Color Cube

Indexed 232～255
      ↓
xterm Grayscale Ramp
```

其中：

```text
0～15
```

必须允许 Terminal Theme 覆盖。

```text
16～255
```

默认按照标准 xterm 256-color Palette 解析。

禁止将所有 256 色在 Parser 阶段提前转换并永久写死为 RGB。

---

### 24.6 RGB TrueColor

必须支持：

```text
24-bit RGB TrueColor
```

例如：

```text
CSI 38;2;R;G;B m
CSI 48;2;R;G;B m
```

TrueColor 属于显式颜色，不经过 ANSI Theme Palette 替换。

例如：

```text
ESC[38;2;123;45;67m
```

必须保存为：

```text
RGB(123, 45, 67)
```

Theme 切换不得改变该颜色。

---

### 24.7 Underline Color

颜色模型应支持独立：

```text
Underline Color
```

例如现代 xterm 扩展：

```text
CSI 58;...m
CSI 59m
```

Cell Attributes 中：

```text
Foreground
Background
UnderlineColor
```

必须是独立语义。

Underline Color 也应支持：

```text
Default
Indexed
RGB
```

存储位置遵循 Cell.h 的既有决策：不内嵌进 `ZzCell`（避免增大
sizeof(ZzCell)），采用按行稀疏侧表，Cell 属性位的 12-15 预留位为扩展点。

---

### 24.8 ZzTermTheme

建议主题模型至少包含：

```cpp
struct ZzTermTheme
{
    std::string name;

    ZzRgbColor foreground;
    ZzRgbColor background;

    std::array<ZzRgbColor, 16> ansiColors;

    ZzRgbColor cursor;
    ZzRgbColor cursorText;

    ZzRgbColor selectionBackground;
    ZzRgbColor selectionForeground;
};
```

实际实现可以扩展，但不得让 ZzTermTheme 依赖 Qt 类型。

禁止：

```cpp
QColor
QString
QPalette
```

进入 ZzTermTheme 的基础数据模型。

建议全部使用：

```text
std::string
uint8_t
std::array
ZzRgbColor
```

保证未来可以直接用于：

```text
Qt
macOS Native
OpenHarmony
其他 Renderer
```

---

### 24.9 Terminal Theme 与 Decoration

必须区分：

```text
Terminal Theme
```

与：

```text
Decoration
```

Terminal Theme 负责：

```text
Default FG/BG
ANSI Palette
Cursor
Terminal Selection Default
```

Decoration 负责：

```text
Keyword Highlight
Regex Highlight
Search Match
Current Search Match
Hyperlink Hover
```

推荐最终绘制流程：

```text
Terminal Cell
     ↓
Resolve Terminal Color
     ↓
Terminal Theme
     ↓
Keyword / Regex Decoration
     ↓
Search Highlight
     ↓
Selection
     ↓
Hyperlink Hover
     ↓
Cursor
     ↓
Final Render Style
```

Decoration 不允许修改 Cell 中保存的原始 VT Attribute。

---

### 24.10 Application Theme

Application Theme 不属于 ZzTermCore。

例如：

```text
Toolbar
Tab
Navigation
Button
Menu
Dialog
Title Bar
Settings Page
Application Background
```

全部由最终客户端负责。

例如未来：

```text
ZzClawTerm Dark Theme
```

可以搭配：

```text
Dracula Terminal Theme
```

也可以搭配：

```text
Solarized Light Terminal Theme
```

两者必须完全独立。

---

### 24.11 iTerm2 Color Scheme

ZzTermTheme 必须正式支持导入：

```text
*.itermcolors
```

建议实现：

```text
ZzItermColorSchemeLoader
```

对外接口示例：

```cpp
ZzTermTheme theme =
    ZzItermColorSchemeLoader::load(path);
```

Loader 负责将 iTerm2 Color Scheme 转换为标准：

```text
ZzTermTheme
```

Widget 不直接解析 `.itermcolors`。

Renderer 同样不直接解析文件。

正确流程：

```text
.itermcolors
      ↓
ZzItermColorSchemeLoader
      ↓
ZzTermTheme
      ↓
ZzTermWidget
      ↓
Renderer
```

---

### 24.12 Theme Loader 与文件格式扩展

Theme Loader 应设计为可扩展结构。

第一阶段必须支持：

```text
Built-in Theme
iTerm2 .itermcolors
```

未来可以增加：

```text
ZzTerm JSON Theme
Windows Terminal Scheme
其他 Terminal Theme Format
```

但 Core 不得依赖具体 Theme 文件格式。

推荐结构：

```text
Theme File
    ↓
Format Loader
    ↓
ZzTermTheme
```

所有外部格式最终转换为统一 `ZzTermTheme`。

---

### 24.13 Built-in Themes

ZzTermDemo 应提供少量内置 Theme 用于验证。

至少建议：

```text
Default Dark
Default Light
```

其他 Theme 可以通过外部文件导入，不要求将大量第三方主题直接编译进项目。

内置 Theme 的主要目的不是提供完整主题商店，而是：

-   Regression Test；
-   Dark/Light 验证；
-   ANSI Palette 验证；
-   Runtime Theme Switching 验证。

---

### 24.14 Runtime Theme Switching

必须支持运行时切换 Terminal Theme。

例如：

```cpp
terminalWidget->setTerminalTheme(theme);
```

Theme 切换不得：

-   重建 Terminal；
-   清空 Scrollback；
-   重新连接 PTY；
-   重新解析历史字节流；
-   修改原始 Cell Attribute。

正确流程：

```text
Theme Changed
      ↓
Update Theme
      ↓
Invalidate Resolved Color Cache
      ↓
Repaint Visible Terminal
```

历史数据必须立即按照新 Theme 显示。

---

### 24.15 Theme 与 Scrollback

Scrollback 中不得永久保存已经经过 Theme 解析的最终 ANSI RGB。

例如：

```text
ESC[31mERROR
```

历史数据应保存：

```text
Foreground = Indexed(1)
```

而不是：

```text
Foreground = RGB(255, 0, 0)
```

否则切换 Theme 后历史颜色无法同步变化。

显式 TrueColor 除外。

---

### 24.16 Theme 与 Renderer Cache

Renderer 可以缓存颜色解析结果，但 Cache 必须与 Theme Generation 绑定。

例如：

```text
Theme Generation = 42
```

切换 Theme：

```text
Generation 42
      ↓
Generation 43
```

所有依赖 Theme 的：

```text
Brush Cache
Color Cache
Text Run Cache
Background Run Cache
```

必须失效或能够识别 Generation 变化。

禁止使用永久 RGB Cache 导致 Theme 切换后部分历史仍显示旧颜色。

---

### 24.17 Background 与透明度

第一阶段 Terminal Theme 以不透明 Background 为基线。

数据结构可以为未来：

```text
Opacity
Background Image
Blur
Acrylic
Transparency
```

保留扩展空间，但这些能力不应进入 Terminal Core。

透明度、背景图片、Blur 等属于 Widget/客户端视觉效果。

Core 只保存：

```text
DefaultBackground
```

语义。

---

### 24.18 Cursor Theme

Terminal Theme 应支持：

```text
Cursor Color
Cursor Text Color
```

Cursor Shape 不属于 Theme Color 本身。

Cursor Shape 属于 Terminal State / Renderer，例如：

```text
Block
Underline
Bar
```

因此：

```text
Cursor Shape
```

与：

```text
Cursor Color
```

必须解耦。

远端应用通过 VT/xterm sequence 修改 Cursor Shape 时，不应覆盖用户配置的 Cursor Color，除非协议明确要求颜色变化。

---

### 24.19 Selection Color

Selection 默认颜色可以由 ZzTermTheme 提供：

```text
Selection Background
Selection Foreground
```

但 Selection 状态属于 Widget。

即：

```text
ZzTermTheme
      ↓
Selection Colors

ZzTermWidget
      ↓
Selection Range / Interaction
```

Theme 不保存 Selection Range。

---

### 24.20 Theme 与 Keyword Highlight

Keyword Highlight 的颜色不得写入 Terminal Theme 的 ANSI Palette。

例如：

```text
ERROR
```

高亮成红色属于：

```text
ZzTermHighlight
```

而不是修改：

```text
ANSI Red
```

推荐：

```text
ZzKeywordRule
├── pattern
└── ZzDecorationStyle
```

这样 Terminal Theme 切换与 Keyword Rule 可以独立配置。

---

### 24.21 Theme 与 Search Highlight

Search Highlight 同样属于 Decoration。

建议至少区分：

```text
Search Match
Current Search Match
```

例如：

```text
普通匹配
    ↓
黄色背景

当前匹配
    ↓
橙色背景
```

具体颜色可以来自 Widget Profile 或 Decoration Theme，但不得污染 Terminal Cell Attribute。

---

### 24.22 颜色优先级

Renderer 必须明确颜色覆盖优先级。

建议：

```text
1. Terminal VT Attribute
2. Terminal Theme Resolution
3. Keyword / Regex Decoration
4. Search Match
5. Selection
6. Hyperlink Hover
7. Cursor
```

当多个 Decoration 同时作用于同一 Cell 时，必须有确定性规则。

禁止依赖：

```text
谁最后 paint 谁覆盖
```

这种隐式行为。

---

### 24.23 Theme API

建议提供类似：

```cpp
class ZzTermWidget
{
public:
    void setTerminalTheme(const ZzTermTheme &theme);

    [[nodiscard]]
    const ZzTermTheme &terminalTheme() const noexcept;
};
```

Theme Loader：

```cpp
class ZzItermColorSchemeLoader
{
public:
    static ZzTermTheme load(const std::filesystem::path &path);
};
```

实际错误处理方式根据项目统一 Error Policy 决定。

所有公开 API 必须按照项目规范提供中文 Doxygen 文档。

---

### 24.24 Theme 测试

必须建立独立 Theme Test。

至少覆盖：

```text
Default FG/BG
ANSI 0～15
Indexed 16～255
TrueColor
Underline Color
Cursor
Selection
Theme Switching
Scrollback Theme Switching
iTerm2 Import
Invalid Theme File
Missing Theme Fields
```

Golden Test 应验证：

```text
VT Input
   ↓
Color Semantic
   ↓
Theme
   ↓
Expected Resolved Color
```

特别必须验证：

```text
ANSI Indexed Color
```

会随着 Theme 改变，而：

```text
Explicit RGB TrueColor
```

不会随着 Theme 改变。

---

### 24.25 模块职责总结

| 功能                          | 所属模块                         |
| --------------------------- | ---------------------------- |
| VT Color Semantic           | ZzTermCore                   |
| Default FG/BG Semantic      | ZzTermCore                   |
| ANSI Indexed Color Semantic | ZzTermCore                   |
| RGB TrueColor Semantic      | ZzTermCore                   |
| ANSI 0～15 Palette           | ZzTermTheme                  |
| xterm 16～255 Palette        | ZzTermTheme / Color Resolver |
| Cursor Color                | ZzTermTheme                  |
| Selection Default Color     | ZzTermTheme                  |
| iTerm2 `.itermcolors`       | ZzTermTheme                  |
| Theme File Loader           | ZzTermTheme                  |
| Runtime Theme Switching     | ZzTermTheme + ZzTermWidget   |
| Theme Color Resolution      | Renderer / Theme Resolver    |
| Keyword Highlight           | ZzTermHighlight              |
| Regex Highlight             | ZzTermHighlight              |
| Search Highlight            | ZzTermWidget / Decoration    |
| Application Dark/Light      | 最终客户端                        |
| Toolbar/Tab/Button Theme    | 最终客户端                        |
| Background Blur/Image       | Widget / 最终客户端               |
| Qt 类型进入 Theme Core Model    | **禁止**                       |

里程碑归属：Core 侧颜色语义已随 M1 落地（`ZzColor` + SGR 全色域 + 39/49
Default 语义，均有测试）；`ZzTermTheme` 模块、Theme Loader、iTerm2 导入、
运行时切换与 Renderer 颜色解析 → 随 ZzTermWidget 迭代（M4 之后）。

核心原则：

> ZzTermCore 保存终端颜色的语义，ZzTermTheme 定义这些语义如何映射为实际颜色，ZzTermWidget 负责绘制，最终客户端负责应用程序 UI Theme。

该边界不得为了实现方便而破坏。
