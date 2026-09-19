# ZzTermCore VT/xterm 功能实现 Checklist

> 本文档为 v1.0 checklist。现行 checklist 为 docs/VT-Xterm-Checklist-v2.md（v2.1）；本文档已勾选条目在 v2.1 中以 [PASS-Native] 标签迁移，保留作历史参考。

> 本文档同时承担规格追踪、开发进度与测试追踪。复杂项完成时应补充
> Spec、Unit Test、Integration Test、Notes。

## Parser

-   [x] Ground / Escape
-   [x] CSI Entry / Param / Intermediate
-   [x] OSC String
-   [x] DCS Entry / Data
-   [x] 任意 chunk 边界
-   [x] 超长序列限制
-   [x] 非法序列恢复
-   [ ] Parser Fuzz

## C0/C1

-   [x] BEL / BS / HT
-   [x] LF / VT / FF / CR
-   [x] ESC
-   [x] IND / NEL / RI / HTS

## Cursor / CSI

-   [x] CUU / CUD / CUF / CUB
-   [x] CNL / CPL
-   [x] CHA / VPA
-   [x] CUP / HVP
-   [x] Save / Restore Cursor

## Erase / Insert / Delete / Scroll

-   [x] ED 0/1/2/3（ED 3 清历史除外）
-   [x] EL 0/1/2
-   [x] ECH
-   [x] ICH / DCH
-   [x] IL / DL
-   [x] SU / SD

## SGR

-   [x] Reset
-   [x] Bold / Faint
-   [x] Italic
-   [ ] Underline variants
-   [x] Blink / Inverse / Invisible / Strikethrough
-   [x] ANSI 16 / Bright
-   [x] 256 colors
-   [x] RGB TrueColor
-   [x] Default FG/BG
-   [ ] Underline color

## DEC/xterm Modes

-   [ ] DECCKM
-   [ ] DECOM
-   [ ] DECAWM
-   [ ] Cursor visibility/style
-   [ ] Alternate Screen 47 / 1047
-   [ ] Save Cursor 1048
-   [ ] Alternate Screen 1049
-   [ ] Focus Reporting 1004
-   [ ] Bracketed Paste 2004

## Mouse

-   [ ] X10
-   [ ] Normal / Button-event / Any-event
-   [ ] SGR coordinates
-   [ ] Wheel
-   [ ] Modifiers

## OSC / DCS

-   [x] Window/Icon title
-   [ ] Palette/default colors
-   [ ] OSC 8 hyperlink
-   [ ] BEL/ST termination
-   [ ] OSC payload protection
-   [ ] DCS parser framework
-   [ ] DCS safe ignore/passthrough
-   [ ] DCS payload protection

## Unicode

-   [x] Split UTF-8 / Invalid UTF-8
-   [ ] CJK width
-   [ ] Combining marks
-   [ ] Variation selectors
-   [ ] Emoji / ZWJ
-   [ ] Wide continuation consistency
-   [ ] Grapheme-aware copy/search
-   [ ] Unicode version recorded

## Screen / History

-   [ ] Primary / Alternate Screen
-   [ ] Scroll margins / Tab stops
-   [ ] Insert/replace mode
-   [ ] Dirty tracking
-   [ ] Hard newline / Soft wrap
-   [ ] Chunked scrollback / History trim
-   [ ] 100k-line test
-   [ ] 1M-line benchmark

## Resize / Reflow

-   [ ] Grow/Shrink rows
-   [ ] Grow/Shrink columns
-   [ ] Soft-wrap reflow
-   [ ] Hard-newline preservation
-   [ ] Wide grapheme boundary
-   [ ] Cursor mapping
-   [ ] Selection/Search mapping
-   [ ] Alternate-screen behavior

## Input / IME

-   [ ] Unicode text
-   [ ] Arrows / Home / End
-   [ ] Insert/Delete / PgUp/PgDn
-   [ ] F1-F12
-   [ ] Modifiers
-   [ ] Application cursor/keypad
-   [ ] Bracketed paste
-   [ ] Focus reporting
-   [ ] Qt IME composition/commit

## Widget / Renderer

-   [ ] QPainter CPU renderer
-   [ ] Dirty-region repaint
-   [ ] Background/Text run batching
-   [ ] Cursor blink local repaint
-   [ ] DPI / Font fallback
-   [ ] Selection
-   [ ] Search highlight
-   [ ] Keyword/Regex decoration
-   [ ] Hyperlink hover

## PTY / Demo

-   [x] Linux PTY
-   [ ] macOS PTY
-   [ ] Windows ConPTY
-   [x] bash/zsh
-   [ ] PowerShell/cmd
-   [ ] Terminal Inspector
-   [ ] VT sequence monitor

## Compatibility

-   [ ] bash / zsh
-   [ ] vim / neovim
-   [ ] nano / less
-   [ ] top / htop
-   [ ] tmux
-   [ ] git log
-   [ ] 256 colors / TrueColor
-   [ ] 中文输出 / 中文 IME
-   [ ] Mouse / Bracketed Paste
-   [ ] OSC 8

## Build / Release / API

-   [x] CMakeLists.txt
-   [x] CMakePresets.json
-   [x] Static library
-   [x] Shared library
-   [ ] Linux Clang/GCC（GCC 已验证，Clang 待 CI）
-   [ ] Windows MSVC
-   [ ] macOS Apple Clang
-   [x] Doxygen 中文 API 注释
-   [x] docs/API.md
-   [ ] CI 自动生成 API 文档
-   [ ] API 文档与源码同步检查
-   [x] Install/Export CMake package
-   [ ] Version / ABI policy


## Wrap / Reflow

-   [x] Hard Newline
-   [x] Soft Wrap
-   [x] Logical Line / Physical Row
-   [ ] DECAWM Enable
-   [ ] DECAWM Disable
-   [x] Wrap Pending
-   [x] Last Column Printable Character
-   [x] CR while Wrap Pending
-   [ ] LF while Wrap Pending
-   [ ] BS while Wrap Pending
-   [x] Cursor Movement while Wrap Pending
-   [ ] CJK Wide Character at Right Boundary
-   [ ] Combining Character at Right Boundary
-   [ ] WideContinuation Consistency
-   [ ] Grow Columns Reflow
-   [ ] Shrink Columns Reflow
-   [ ] Grow Rows
-   [ ] Shrink Rows
-   [ ] Cursor Mapping after Reflow
-   [ ] Selection Mapping after Reflow
-   [ ] Search Match Mapping after Reflow
-   [ ] Scrollback View Anchor after Reflow
-   [ ] Alternate Screen Resize Behavior
-   [ ] 100k-line Reflow Benchmark
-   [ ] 1M-line Reflow Benchmark

## Terminal Font Zoom

-   [ ] Increase Font Size
-   [ ] Decrease Font Size
-   [ ] Reset Font Size
-   [ ] Ctrl + `+`
-   [ ] Ctrl + `-`
-   [ ] Ctrl + `0`
-   [ ] Ctrl + MouseWheel
-   [ ] Configurable Minimum Font Size
-   [ ] Configurable Maximum Font Size
-   [ ] Recalculate Font Metrics
-   [ ] Recalculate Cell Geometry
-   [ ] Recalculate Grid Size
-   [ ] Core Resize after Font Zoom
-   [ ] PTY Resize after Font Zoom
-   [ ] Preserve Scrollback View Anchor
-   [ ] Preserve Selection
-   [ ] Preserve Search Match
-   [ ] No Bitmap Scaling
-   [ ] Clear Glyph Cache after Font Change

## High DPI / Application Scale

-   [ ] Qt High DPI
-   [ ] Windows 125%
-   [ ] Windows 150%
-   [ ] Windows 175%
-   [ ] Windows 200%
-   [ ] Linux HiDPI
-   [ ] macOS Retina
-   [ ] Device Pixel Ratio != 1
-   [ ] Runtime DPI Change
-   [ ] Move Window Between Different-DPI Displays
-   [ ] Font Metrics Recalculation after DPI Change
-   [ ] Grid Recalculation after DPI Change
-   [ ] Core Resize after DPI Change
-   [ ] PTY Resize after DPI Change
-   [ ] No DPI Information inside ZzTermCore
-   [ ] No Pixel Geometry inside ZzTermCore

## Font / Glyph

-   [ ] Primary Monospace Font
-   [ ] CJK Font Fallback
-   [ ] Emoji Font Fallback
-   [ ] Combining Mark Rendering
-   [ ] Narrow Cell Geometry Stable
-   [ ] Wide Cell Geometry Stable
-   [ ] Fallback Glyph Does Not Change Grid Width
-   [ ] Baseline
-   [ ] Ascent / Descent
-   [ ] Underline Position
-   [ ] Strikeout Position
-   [ ] Configurable Line Spacing
-   [ ] Font Metrics Cache
-   [ ] Glyph Cache
-   [ ] Cache Invalidation after Font Change
-   [ ] Cache Invalidation after DPI Change

## Window / Grid Resize

-   [ ] Window Resize
-   [ ] Viewport Pixel Geometry Calculation
-   [ ] Margin Deduction
-   [ ] Scrollbar Deduction
-   [ ] Pixel Size → Columns/Rows
-   [ ] Minimum Grid 1×1
-   [ ] Rapid Resize Stability
-   [ ] Resize Event Coalescing
-   [x] Linux PTY `TIOCSWINSZ`
-   [ ] macOS PTY `TIOCSWINSZ`（M5）
-   [ ] Windows ConPTY Resize
-   [x] Core and PTY Grid Size Consistency


## Terminal Color / Theme

### Color Semantic

-   [x] Default Foreground
-   [x] Default Background
-   [x] Indexed Color
-   [x] RGB TrueColor
-   [ ] Underline Color
-   [x] ANSI Color Semantic 不提前转换为固定 RGB
-   [x] TrueColor 保留原始 RGB

### ANSI Palette

-   [ ] ANSI Color 0～7
-   [ ] Bright Color 8～15
-   [ ] Theme ANSI Palette
-   [ ] ANSI Palette Runtime Change
-   [ ] Historical ANSI Colors Follow Theme Change

### xterm 256 Color

-   [ ] Indexed 0～15 使用 Theme Palette
-   [ ] Indexed 16～231 Color Cube
-   [ ] Indexed 232～255 Grayscale
-   [x] 256-color Foreground
-   [x] 256-color Background
-   [ ] 256-color Underline

### TrueColor

-   [x] `38;2;R;G;B`
-   [x] `48;2;R;G;B`
-   [ ] Underline TrueColor
-   [ ] TrueColor 不受 Terminal Theme ANSI Palette 影响

### Default Color

-   [x] SGR 39 Default Foreground
-   [x] SGR 49 Default Background
-   [ ] Default Underline Color
-   [ ] Theme Change 更新 Default FG/BG
-   [ ] Scrollback Default Color 跟随 Theme Change

### ZzTermTheme

-   [ ] Theme Name
-   [ ] Foreground
-   [ ] Background
-   [ ] ANSI 16 Colors
-   [ ] Cursor Color
-   [ ] Cursor Text Color
-   [ ] Selection Background
-   [ ] Selection Foreground
-   [ ] Theme Model 无 Qt 类型
-   [ ] Theme Model 可独立于 Renderer 使用

### Built-in Theme

-   [ ] Default Dark
-   [ ] Default Light
-   [ ] ANSI Color Verification
-   [ ] Dark/Light Regression Test

### iTerm2 Color Scheme

-   [ ] `.itermcolors` Loader
-   [ ] Foreground Color
-   [ ] Background Color
-   [ ] ANSI 0～15
-   [ ] Cursor Color
-   [ ] Cursor Text Color
-   [ ] Selection Color
-   [ ] Missing Field Handling
-   [ ] Invalid File Handling
-   [ ] Import Regression Test

### Runtime Theme Switching

-   [ ] Runtime `setTerminalTheme()`
-   [ ] 不重建 ZzTermCore
-   [ ] 不清空 Scrollback
-   [ ] 不重新解析历史
-   [ ] 不重新创建 PTY
-   [ ] Visible Screen Immediate Repaint
-   [ ] Scrollback Immediate Theme Update
-   [ ] Theme Cache Invalidation
-   [ ] Repeated Theme Switching Stability

### Theme Cache

-   [ ] Theme Generation / Version
-   [ ] Color Cache Invalidation
-   [ ] Brush Cache Invalidation
-   [ ] Text Run Cache Invalidation
-   [ ] Background Run Cache Invalidation
-   [ ] 无旧 Theme 残留

### Cursor

-   [ ] Cursor Color
-   [ ] Cursor Text Color
-   [ ] Cursor Shape 与 Cursor Color 解耦
-   [ ] Block Cursor
-   [ ] Underline Cursor
-   [ ] Bar Cursor
-   [ ] Theme Change 更新 Cursor Color

### Selection / Decoration

-   [ ] Selection Background
-   [ ] Selection Foreground
-   [ ] Keyword Decoration
-   [ ] Regex Decoration
-   [ ] Search Match Decoration
-   [ ] Current Search Match Decoration
-   [ ] Hyperlink Hover Decoration
-   [ ] Decoration 不修改原始 VT Cell Attribute

### Render Priority

-   [ ] Terminal VT Attribute
-   [ ] Terminal Theme Resolution
-   [ ] Keyword / Regex
-   [ ] Search Match
-   [ ] Selection
-   [ ] Hyperlink Hover
-   [ ] Cursor
-   [ ] 多 Decoration 冲突规则确定且有测试

### Theme / Application Boundary

-   [ ] Terminal Theme 独立于 Application Theme
-   [ ] Application Dark/Light 不进入 ZzTermCore
-   [ ] Toolbar Theme 不进入 ZzTermCore
-   [ ] Tab Theme 不进入 ZzTermCore
-   [ ] Button Theme 不进入 ZzTermCore
-   [ ] Background Blur/Image 不进入 Core
-   [ ] Qt `QColor` 不进入 Theme Core Model
-   [ ] Qt `QPalette` 不进入 Theme Core Model

### Cross-platform

-   [ ] Linux Theme
-   [ ] Windows Theme
-   [ ] macOS Theme
-   [ ] Theme Model 不依赖 OS
-   [ ] OpenHarmony Renderer 可消费相同 ZzTermTheme


## 单项完成记录模板

``` text
Feature:
Status:
Spec:
Parser:
Core:
Unit Test:
Regression Test:
Integration Test:
Platforms:
Notes:
```

"Complete"
不仅代表代码存在，还必须有对应测试；涉及用户可见行为的功能应至少在
ZzTermDemo 中完成一次集成验证。
