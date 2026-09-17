# ZzTermCore 技术设计与开发规范

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
├── widget/
├── pty/{unix,windows}/
├── examples/ZzTermDemo/
├── tests/{unit,compatibility,regression}/
├── fuzz/
├── benchmarks/
└── docs/
```

Targets：`ZzTermCore`、`ZzTermHighlight`、`ZzTermWidget`、`ZzTermPty`、`ZzTermDemo`。依赖只能由外向内。

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
