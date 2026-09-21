# ZzTermCore 技术设计与开发规范

> v2.1 · 2026-09-19\
> Zz Public API：C++20 设计基线；ZzNativeBackend：C++20；Contour
> private backend：当前按 C++23 构建\
> CMakeLists.txt + CMakePresets.json

本文档整合 v1.0（`docs/Architecture.md`）与 v2.0，自本版起为现行主设计
文档。v1.0 保留作历史参考与 native backend 语义契约来源。

## 1. 项目定位

ZzTermCore 是独立、跨平台的终端核心组件，不依赖 ZzClawTerm
进行开发、测试或功能验证。ZzClawTerm 仅是下游使用者之一。

v2.1 采用双后端路线：主路线转向 Contour 后端，第一代默认 Terminal
Engine 采用 Contour 的 `vtbackend` / `vtparser`，由 `ZzContourBackend`
私有封装；v1 自研引擎保留为一等后端 `ZzNativeBackend`。

核心原则：

    Zz owns the API.
    Contour owns VT/xterm semantics.
    ZzNativeBackend owns the dependency-free reference implementation.
    ZzTermWidget owns Qt rendering.
    ZzSshCore owns SSH transport.

`ZzNativeBackend`（C++20）已实现
UTF-8/parser/screen/scrollback/input encoder/Unix
PTY，测试全绿。其价值：无 Contour 依赖的最小构建；兼容性对照基准（同一
兼容性测试套件可在双后端运行）；OpenHarmony/受限平台后备；保护已有 v1
投资。

Contour 永远是 ZzTermCore 的 private implementation detail，禁止
ZzClawTerm 直接依赖。双后端共存于同一 Public API/Backend 边界之后，
Public API 不暴露任一后端类型。

## 2. PoC 验证结论

`ZzContourPoc` 已在 Windows/MSVC 与 Ubuntu/GCC 实际通过：

-   ASCII、UTF-8/CJK；
-   ANSI、24-bit TrueColor；
-   CJK wide-cell；
-   OSC Window Title event；
-   Primary/Alternate Screen 切换与恢复；
-   Resize 80x24 -\> 100x30；
-   Terminal output -\> external transport adapter；
-   Scrollback。

简单 10,000 行输入：Windows Release 约 15 ms，Ubuntu Release 约 10
ms。该数据仅证明没有明显基础性能阻塞，不作为正式性能指标。

PoC 还证明最小集成不需要 Contour GUI、Contour
Renderer、`net`、OpenSSL、libssh2 或 Contour 官方 vtpty 实现。

## 3. 总体架构

    ZzClawTerm
    ├── ZzSshCore -> libssh2
    ├── ZzTermWidget -> QWidget/QPainter
    └── ZzTermCore
        ├── Public C++ API
        ├── RenderView / Search / Highlight
        ├── ZzContourBackend (默认)
        │   └── Contour
        │       ├── vtbackend
        │       ├── vtparser
        │       ├── crispy
        │       └── libunicode
        └── ZzNativeBackend (一等后端)
            └── v1 自研引擎
                ├── UTF-8 / VT Parser
                ├── Screen / Scrollback
                └── Input Encoder

数据方向：

    Remote/local bytes
        -> ZzTermCore::feed()
        -> Backend (ZzContourBackend / ZzNativeBackend)
        -> Terminal State
        -> ZzRenderView
        -> Renderer

输入方向：

    Frontend semantic events
        -> ZzTermCore Input API
        -> Backend input encoding
        -> ZzExternalTransportAdapter / native encoder output
        -> ZzSshCore / ZzTermPty / other transport

Public API 不得暴露 Qt、libssh2、OpenSSL、平台 API、网络 API、任何
Contour 类型或任何 native backend 内部类型。

## 4. 仓库结构

### 现状

    ZzTermCore/
    ├── CMakeLists.txt
    ├── CMakePresets.json
    ├── cmake/ZzTermCoreConfig.cmake.in
    ├── include/ZzTerm/          # 12 个公开头文件
    ├── src/{history,input,parser,screen,terminal,unicode}/
    ├── pty/unix/
    ├── tests/unit/              # 10 个单元测试
    ├── tests/interactive/       # verify_smoke.py
    ├── examples/ZzTermSmoke/
    └── docs/

Targets：`ZzTermCore`、`ZzTermPty`、`ZzTermSmoke` + 10 个单测 +
`ZzTermSmokeEcho`、`ZzTermSmokeInteractive`。全局 C++20，12/12
测试通过。

### 目标结构

在现状基础上新增：

    ZzTermCore/
    ├── cmake/ContourBackend.cmake
    ├── src/backend/contour/
    │   ├── ZzContourBackend.*
    │   ├── ZzContourEvents.*
    │   └── ZzExternalTransportAdapter.*
    ├── highlight/
    ├── theme/
    ├── widget/
    ├── tests/contour/
    ├── fuzz/
    ├── benchmarks/
    └── third_party/contour/     # Git submodule，固定 commit

native 引擎代码迁移到 `src/backend/native/` 为可选规划项，不强制物理
迁移。

Contour 作为 Git submodule 固定到经过验证的 commit，并放在 ZzTermCore
内部，而不是 ZzClawTerm。

## 5. Contour 构建与升级

禁止无约束地 `add_subdirectory(third_party/contour)` 整个顶层工程。PoC
已发现顶层 Version.cmake 路径假设、第三方依赖配置以及 `net -> OpenSSL`
等非核心依赖。

正式工程只构建所需的最小 Contour Terminal 组件。

升级流程：

    更新 Contour commit
        -> Windows/MSVC smoke tests
        -> Linux GCC/Clang smoke tests
        -> Regression/Compatibility tests
        -> 全部 PASS
        -> 更新 ZzTermCore submodule pointer

原则上禁止长期维护 Contour 私有 patch。

## 6. C++、CMake 与发布

要求 CMake \>= 3.19，维护 `CMakeLists.txt` 与 `CMakePresets.json`。

Zz Public API、Zz 自有源码与 `ZzNativeBackend` 继续以 C++20
兼容性作为设计基线。当前 Contour private backend 按 C++23
构建。不得让 Contour 的语言标准、类型或 ABI 泄漏到公开接口。

如果最终单一 target 必须整体启用 C++23，应明确区分"构建要求"和"Public
API 设计基线"。

Windows/MSVC 编译 Contour 时启用 UTF-8 源码解析，例如 `/utf-8`。

必须同时支持 static/shared，使用统一 `ZZTERM_API` 导出宏。至少维护：

-   linux-clang-debug/release
-   linux-gcc-debug/release
-   windows-msvc-debug/release
-   macos-clang-debug/release

## 7. Public API 与 Backend abstraction

Public API 由 Zz 定义。推荐使用 PImpl 隐藏具体后端：

    class ZZTERM_API ZzTerminal
    {
    public:
        ZzTermChanges feed(std::span<const std::byte> data);
        void resize(int columns, int rows);
        ZzRenderView renderView() const;
    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

Backend 边界同时承载 `ZzContourBackend` 与
`ZzNativeBackend`，至少覆盖 feed、resize、screen/render access、terminal
modes、cursor/state、input/output、events、history、dirty/update
notification。

禁止公开 `vtbackend::Terminal`、`vtbackend::Cell` 等类型，同样禁止公开
native backend 内部类型。后端选择通过工厂/配置完成，不通过公开类型。

## 8. Screen / Cell / Grapheme / RenderView

不再要求每帧把后端 Grid 全量复制成第二套 Zz
Grid。优先建立轻量、只读、生命周期明确的 `ZzRenderView` / `ZzCellView` /
logical-range abstraction。

要求：

-   不每帧复制整个 Grid；
-   Public API 不暴露 Contour 或 native 内部类型；
-   支持 dirty row/cell/region；
-   Search/Selection/Highlight 使用稳定 logical range；
-   支持
    Empty/Narrow/Wide/WideContinuation、grapheme、ANSI/256/TrueColor
    和常见文本属性。

## 9. 核心数据模型语义契约

以下语义契约继承自 v1.0，对 `ZzRenderView` / `ZzCellView` 与
native backend 仍然有效；Contour 后端的 adapter 必须映射到同一语义。

禁止假设 `1 code point == 1 cell`。Cell 必须表达
Empty、Narrow、Wide、WideContinuation、grapheme 与显示属性。颜色覆盖
default、ANSI 16、bright、256、RGB TrueColor；属性预留
bold、faint、italic、underline
variants、blink、inverse、invisible、strikethrough、protected。

必须区分 logical line、physical row、hard newline、soft
wrap。公共 API 不暴露底层容器。

## 10. VT/xterm 与 Unicode 职责

Contour 后端下，VT Parser、Terminal State、Primary/Alternate
Screen、Scroll Region、绝大多数 DEC/xterm 模式以及 Unicode/grapheme
基础能力由 Contour 提供；native 后端下由 v1 自研引擎提供同等语义。

ZzTermCore 负责：

1.  Adapter 与 Public API 映射；
2.  Compatibility/Regression Test（同一套件可在双后端运行）；
3.  缺失能力的受控扩展；
4.  保持 Backend 可替换。

`VT-Xterm-Checklist-v2.md` 使用
`[Contour]`、`[Zz-Adapter]`、`[Zz-Native]`、`[Test]`、`[Future]`、`[Rule]`
分类，并以 `[PASS-Native]`、`[PASS-PoC]` 记录已有验证基线。

M7b 落地注记：native 聚簇生产已落地——UAX #29 自研 segmenter
（UCD 16.0.0 钉版生成表 + 单一求值核 GB1-GB999，官方 golden 1093 用例
零失败）经 putChar 无状态回望续接集成（快路径三点短路，ASCII 热路径
零附加成本）；宽度/画笔/VS16 位移/软换行边界语义以 contour 探针裁定表
（docs/superpowers/specs/2026-09-21-m7b-parity-probe.md）为准；双后端
parity 经 test_cluster_compat 门控（b 类分歧 I-1/I-2/I-6/I-7 分别断言
钉住）。

## 11. Input、IME 与 Transport

UI 不直接拼 escape sequence。统一通过
`ZzKeyEvent`、`ZzMouseEvent`、Paste、Focus、Unicode commit text 进入
Core。

Input Encoder 能力清单（继承自 v1，作为验收标准保留）：

-   Unicode text；
-   方向键、Home/End、Insert/Delete、PgUp/PgDn；
-   F1-F12、modifier 组合；
-   application cursor / application keypad；
-   xterm mouse、focus reporting；
-   bracketed paste。

Contour 路线下由 Contour terminal-mode aware encoding
提供，adapter 负责事件映射；native 路线下由 v1 `ZzInputEncoder`
提供。双后端必须通过同一清单验收。

SSH 始终属于 `ZzSshCore -> libssh2`；Local shell 属于
`ZzTermPty -> forkpty/openpty/ConPTY`。未来 Serial、ADB shell 等
transport 不得要求修改任何 backend。

IME composition 属于前端，commit text 才进入 Core。

## 12. Qt Renderer、缩放与 DPI

`ZzRenderView` 是 Core 与 Renderer 的稳定边界。官方 `ZzTermWidget` 使用
Qt 6 QWidget + QPainter CPU Renderer，不使用 Contour Renderer，不强制
OpenGL/QRhi。

要求 dirty-region repaint、background/text run batching、cursor blink
局部刷新、DPI/font fallback。

终端字体缩放属于 Widget/Renderer；字体变化后重新计算 Cell geometry，并向
Core 发出新的 rows/columns resize。Core 不保存 QFont、point size、DPI 或
device pixel ratio。

应用 UI 缩放属于 ZzClawTerm/Qt。必须验证 Windows
100%/125%/150%/200%、多显示器 DPI 切换、font fallback、宽字符对齐，以及
font zoom + resize/reflow。

## 13. Terminal Theme

Terminal palette 与应用主题严格分离。

保留 `ZzTermTheme`/palette model，负责 default FG/BG、ANSI
16、bright、cursor、selection 和 decoration colors；TrueColor
属于终端内容，不被主题强行覆盖。

支持 iTerm2 Color Schemes。Contour 颜色状态通过 Adapter 映射为 Zz
颜色语义。

## 14. 关键词/正则高亮

高亮不是 VT 协议语义，继续由 `ZzTermHighlight` 自研：

-   `ZzKeywordMatcher`
-   `ZzRegexMatcher`
-   `ZzDecoration`

依赖只能 `ZzTermHighlight -> ZzTermCore`。Decoration 使用 stable logical
range，不修改原始 VT Cell 属性。

绘制优先级：

    Terminal Style -> Keyword -> Search -> Selection -> Cursor

## 15. PTY 与独立 Demo

`ZzTermPty`：Linux/macOS 使用 forkpty/openpty，Windows 使用
ConPTY。Unix PTY 已在 v1 实现并验证。

`ZzTermDemo` 必须独立于 ZzClawTerm，可运行
bash/zsh/PowerShell/cmd、vim/neovim、tmux、htop、less。

Terminal Inspector 建议显示 size、cursor、screen、DEC/xterm
modes、history、dirty region、最近 VT sequence、paint timing、内存统计和
当前 backend 类型/版本（Contour 或 native）。

## 16. Resize/Reflow、Search/Copy

Resize/Reflow 基础语义优先使用 Contour，并通过 compatibility/regression
tests 验证；native 后端按 v1 语义契约实现同一行为。

Search/Copy/Selection 基于 logical line/grapheme abstraction。Search
不得把全部历史拼成巨大字符串；按 logical line/chunk
扫描，未来可加增量索引。

必须正确处理 hard newline、soft wrap、wide
grapheme、cursor/selection/search anchor 映射。

## 17. 性能目标

常规目标：10 万行 scrollback 流畅滚动和可用搜索；大量输出期间 UI
保持响应；resize 无明显秒级冻结；shell/vim/tmux 无明显掉帧。

百万行是扩展目标。建立 10k/100k/1M benchmark，记录 feed
throughput、history access、reflow、search、RSS/peak memory 和 renderer
repaint cost。所有优化由 benchmark/profiler 数据驱动。

## 18. 安全与健壮性

远端 bytes 全部视为不可信。即使 parser 来自 Contour，Zz integration
仍必须对超长 OSC/DCS、异常 resize、非法
UTF-8、资源上限和事件桥接做测试；防整数溢出、parser
死循环和无界内存增长。

Fuzz target：

-   UTF-8 decoder；
-   VT Parser（Contour 路线下聚焦 adapter 边界与集成面；native
    路线下直接 fuzz 自研 parser）；
-   feed + resize 组合。

## 19. 测试体系与强制流程

测试体系：Unit、Golden、Compatibility、Regression、Fuzz、Benchmark。

Compatibility 至少覆盖
bash/zsh、vim/neovim、nano/less、top/htop、tmux、git
log、256/TrueColor、中文、IME、mouse、bracketed paste、OSC
8。同一兼容性测试套件必须可在 Contour 与 native 双后端运行，native
后端作为兼容性对照基准。

所有兼容性 bug 必须执行强制流程：

    复现 -> 最小测试 -> Test FAIL -> 修复 -> Test PASS

禁止只修代码不留测试。

## 20. API 文档实时同步

API 文档属于正式交付物，必须与源码实时同步。

所有公开 API 使用标准中文 Doxygen 注释；`docs/API.md`
维护高层 API 说明，详细接口以源码 Doxygen 自动生成结果为准。

以下变更必须同步更新接口文档：新增/删除公开类或函数、修改参数/返回值、
ownership/lifetime、线程安全、错误行为、ABI 行为。

CI 应生成 Doxygen 文档并检查 warning。API 文档更新属于代码评审的
Definition of Done，不允许"代码先合并，文档以后补"。

## 21. 中文注释规范

标识符使用英文，公开类型使用 `Zz` 前缀；注释和项目文档以中文为主；协议
名称保留标准英文。

复杂逻辑注释解释"为什么"，禁止无价值的逐行翻译式注释；协议实现应注明
标准名称/编号。源文件统一 UTF-8。

## 22. 开发环境与 CI

主开发采用 Ubuntu + Clang/GCC + Ninja + CMake。Linux
PTY、bash/zsh、vim、tmux、htop、less、ncurses 是天然测试环境。

CI 覆盖 Ubuntu Clang、Ubuntu GCC、Windows MSVC；macOS Apple Clang
尽早加入。

OpenHarmony 作为 portability target：Core 必须保持可移植，OHOS UI/PTY
由适配层承担；native backend 作为无 Contour 依赖的受限平台后备。

## 23. 里程碑

-   M0：Contour submodule 固定 commit、最小 Contour
    构建（cmake/ContourBackend.cmake）、Backend 边界与 Public
    API/PImpl。（v1 M0 的
    CMake/Presets/static/shared/测试框架/Doxygen 已完成。）
-   M1：ZzContourBackend / ZzContourEvents /
    ZzExternalTransportAdapter /
    ZzRenderView·ZzCellView；PoC 用例迁移为正式 smoke/regression
    tests；现有 native 测试套件保持全绿。
-   M2：ZzTermDemo + PTY/ConPTY + QWidget/QPainter Renderer 接入
    Contour 后端。
-   M3：Input/IME/mouse/bracketed paste/OSC/DEC modes compatibility。
-   M4：10 万行 scrollback、selection/copy/search/highlight、reflow、
    theme、font zoom。
-   M5：Unicode edge cases、Fuzz、百万行、性能优化、macOS。
-   M6：API/ABI 收敛、static/shared 发布、Contour upgrade
    policy、OpenHarmony portability。

注意：v1 文档的 M0–M6 编号自此废止；历史代码/提交注释中的 M
编号一律按 v1 语义（自研引擎路线）解读，不再与新编号对应。

## 24. Definition of Done

-   Windows/MSVC、Linux GCC/Clang 构建和测试稳定，macOS 纳入 CI；
-   static/shared 可构建、安装和链接；
-   Public API 无 Qt/PTY/SSH/Contour/native 后端类型泄漏；
-   最小 backend 不引入 Contour GUI/Renderer/net/OpenSSL/libssh2；
-   ZzTermDemo 独立运行；
-   常见 TUI 兼容测试通过；
-   中文/IME/TrueColor/mouse/alternate screen 可用；
-   10 万行历史达到目标；
-   Fuzz/Regression/Benchmark 建立；
-   API 文档与 Checklist 实时同步。

## 25. 明确禁止

禁止 ZzClawTerm 直接依赖 Contour；禁止 Public API 暴露 Contour；禁止
Public API 暴露任一后端类型；禁止使用 Contour
Renderer；禁止无约束构建整个 Contour 顶层工程；禁止每帧全量复制
Grid；禁止无测试的兼容性特判；禁止为了短期方便让 SSH/Qt/平台 API 渗入
Core；禁止删除 native backend 测试套件以让 Contour 集成"变绿"。

## 26. 首批任务

按整合版里程碑重排；v1 已完成项（构建体系、UTF-8/parser/screen、
PTY、测试体系）无需重做：

1.  固定 Contour submodule commit，建立最小 `ContourBackend.cmake`；
2.  建立 ZzTerminal Public API/PImpl 与双 Backend boundary；
3.  实现 ZzContourBackend / ZzContourEvents /
    ZzExternalTransportAdapter；
4.  设计低复制 RenderView/CellView 与 dirty/update
    API（对齐 v1 语义契约）；
5.  将 PoC 双平台用例迁移为正式 smoke/regression tests，并确认现有
    native 测试套件保持全绿；
6.  Doxygen/API 流水线接入 CI；
7.  Unix PTY（已有）+ ZzTermDemo 接入 Contour 后端；
8.  QPainter renderer + DPI/font fallback + font zoom；
9.  Input/IME/mouse/paste（按 §11 能力清单验收）；
10. Selection/Copy/Search/Highlight；
11. Resize/Reflow compatibility（双后端对照）；
12. Terminal Theme/iTerm2 palette；
13. Windows ConPTY；
14. 100k/1M benchmark、Fuzz、Compatibility；
15. macOS 与 OpenHarmony portability。

第一次 v2.1 设计评审优先审查 Public API/PImpl、双 Backend
边界、Contour 生命周期、ExternalTransport、RenderView/CellView、dirty
tracking、线程模型和 Contour 升级策略。
