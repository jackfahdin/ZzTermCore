# ZzTermCore VT/xterm 功能实现 Checklist

> v2.1 · 2026-09-19\
> 整合自 v1（docs/VT-Xterm-Checklist.md，保留作历史参考）与 v2（Contour
> 后端路线）。\
> 责任标签：`[Contour]` Contour 提供；`[Zz-Adapter]` Zz
> 需适配；`[Zz-Native]` Zz 自研；`[Test]` 必须验证；`[Future]`
> 后续扩展；`[Rule]` 架构规则。\
> 验证状态标签：`[PASS-Native]` 自研（native）后端已完成代码+测试+集成验证（v1
> 勾选迁移而来）；`[PASS-PoC]` 双平台 Headless PoC
> 已验证（不等于完整兼容性验收）。\
> 勾选规则：`[x]` 仅当条目达到完整验收才勾选——涉及终端引擎语义的条目必须在固定
> commit 的 Contour 后端上重新证明（此前仅有
> [PASS-Native]/[PASS-PoC] 的保持未勾）；与后端无关且当前已真实完成的条目（构建体系、文档、PTY
> 等）可勾选并标注 [PASS-Native] 表明验证发生在 v1 栈；Zz-Native
> 条目（Widget/Theme/高亮等）经测试+Demo 集成验证即可勾选。PoC
> 已验证的非引擎语义条目可勾选并标注 [PASS-PoC]；“已完成 PoC
> 基线”节整节保留作历史记录。

## 已完成 PoC 基线

-   [x] \[Contour\]\[Zz-Adapter\]\[PASS-PoC\] Headless vtbackend
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] ASCII
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] UTF-8 / CJK
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] ANSI cell/content
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] ANSI red attribute
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] RGB TrueColor
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] CJK wide-cell width
-   [x] \[Contour\]\[Zz-Adapter\]\[PASS-PoC\] OSC Window Title event
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] Alternate Screen
    enter/leave/restore
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] Resize
-   [x] \[Contour\]\[Zz-Adapter\]\[PASS-PoC\] Terminal output -\>
    external transport
-   [x] \[Contour\]\[Test\]\[PASS-PoC\] Scrollback
-   [x] \[Test\]\[PASS-PoC\] Windows/MSVC Release
-   [x] \[Test\]\[PASS-PoC\] Ubuntu/GCC Release

PoC 简单 10,000 行输入：Windows 约 15 ms，Ubuntu 约 10
ms；不作为正式性能指标。

## Build / Backend Boundary

-   [x] \[Zz-Adapter\] Contour 固定 commit 的 Git submodule（third_party/contour @ 6777ff05）
-   [x] \[Zz-Adapter\] 最小 Contour build，不构建顶层完整工程（cmake/ContourBackend.cmake：仅 crispy/vtpty/vtparser/vtbackend）
-   [x] \[Zz-Adapter\] vtbackend / vtparser / crispy / libunicode
-   [ ] \[Zz-Adapter\] Windows `/utf-8`
-   [ ] \[Zz-Adapter\] Zz Public API 不暴露 Contour 类型
-   [ ] \[Zz-Adapter\] Contour targets 全部 PRIVATE（当前唯一消费方 test_contour_smoke 为 PRIVATE 链接；接入 Core 在 M1）
-   [ ] \[Zz-Adapter\] ZzExternalTransportAdapter
-   [ ] \[Zz-Adapter\] ZzContourEvents
-   [ ] \[Zz-Adapter\] ZzRenderView / ZzCellView
-   [ ] \[Test\] Contour upgrade smoke/regression gate
-   [ ] \[Test\] Linux Clang（另见 Build / Release / API 节）
-   [ ] \[Test\] macOS Apple Clang（另见 Build / Release / API 节）

## Parser / C0 / C1

以下能力优先由 Contour 提供，Zz 不重新实现 parser；逐项建立
compatibility/regression tests。

-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Ground / Escape
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] CSI Entry / Param /
    Intermediate
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] OSC String
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] DCS Entry / Data
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] 任意 chunk 边界
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] 超长序列限制
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] 非法序列恢复
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] BEL / BS / HT
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] LF / VT / FF / CR
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] ESC
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] IND / NEL / RI / HTS
-   [ ] \[Test\] Parser/Feed fuzz 与资源上限

## Cursor / CSI / Editing

-   [ ] \[Contour\]\[Test\]\[PASS-Native\] CUU / CUD / CUF / CUB
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] CNL / CPL
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] CHA / VPA
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] CUP / HVP
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Save / Restore Cursor
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] ED 0/1/2/3（ED 3
    清历史除外）
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] EL 0/1/2
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] ECH
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] ICH / DCH
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] IL / DL
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] SU / SD
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Scroll margins / Tab stops
-   [ ] \[Contour\]\[Test\] Insert/replace mode

## SGR / Color

-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Reset
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Bold / Faint
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Italic
-   [ ] \[Contour\]\[Test\] Underline variants
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Blink / Inverse / Invisible /
    Strikethrough
-   [ ] \[Contour\]\[Test\]\[PASS-PoC\]\[PASS-Native\] ANSI basic color
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] ANSI 16 / Bright 全量
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] 256 colors
-   [ ] \[Contour\]\[Test\]\[PASS-PoC\]\[PASS-Native\] RGB TrueColor
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Default FG/BG
-   [ ] \[Contour\]\[Test\] Underline color
-   [ ] \[Zz-Adapter\] Contour Color -\> ZzColor mapping

## DEC / xterm Modes

-   [ ] \[Contour\]\[Test\] DECCKM
-   [ ] \[Contour\]\[Test\] DECOM
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] DECAWM
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Cursor visibility（DECTCEM
    ?25）
-   [ ] \[Contour\]\[Test\] Cursor style（DECSCUSR）
-   [ ] \[Contour\]\[Test\] Alternate Screen 47
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Alternate Screen 1047
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Save Cursor 1048
-   [ ] \[Contour\]\[Test\]\[PASS-PoC\]\[PASS-Native\] Alternate Screen
    1049 基础行为
-   [ ] \[Contour\]\[Test\] Focus Reporting 1004
-   [ ] \[Contour\]\[Test\] Bracketed Paste 2004

## Mouse

-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] X10
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Normal / Button-event /
    Any-event
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] SGR coordinates
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Wheel
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Modifiers

## OSC / DCS

-   [ ] \[Contour\]\[Zz-Adapter\]\[PASS-PoC\]\[PASS-Native\] Window
    title event
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Icon title
-   [ ] \[Contour\]\[Test\] Palette/default colors
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] OSC 8 hyperlink
-   [ ] \[Contour\]\[Test\] BEL/ST termination
-   [ ] \[Contour\]\[Test\] OSC payload protection
-   [ ] \[Contour\]\[Test\] DCS framework / safe ignore / passthrough
-   [ ] \[Test\] DCS payload/resource protection

## Unicode / Grapheme

-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Split UTF-8 / Invalid UTF-8
-   [ ] \[Contour\]\[Test\]\[PASS-PoC\] CJK basic output
-   [ ] \[Contour\]\[Test\]\[PASS-PoC\]\[PASS-Native\] CJK wide-cell
    width
-   [x] \[Contour\]\[Test\] Combining marks（M7b test_cluster_compat）
-   [x] \[Contour\]\[Test\] Variation selectors（M7b：VS16 位移 / VS15 / 尾列抑制）
-   [x] \[Contour\]\[Test\] Emoji / ZWJ（M7b：GB11 / 区旗 / keycap / 肤色）-   [ ] \[Contour\]\[Test\]\[PASS-Native\] Wide continuation
    consistency
-   [x] \[Zz-Native\]\[Test\] Grapheme-aware copy/search/highlight（M7b 手工实证：聚簇复制与搜索命中）
-   [ ] \[Zz-Adapter\] Unicode/libunicode version recorded

## Screen / History

-   [ ] \[Contour\]\[Test\]\[PASS-PoC\]\[PASS-Native\] Primary /
    Alternate Screen
-   [ ] \[Contour\]\[Test\]\[PASS-PoC\] Basic scrollback
-   [ ] \[Contour\]\[Zz-Adapter\] Dirty tracking / screenUpdated mapping
-   [ ] \[Contour\]\[Test\] Hard newline / Soft wrap
-   [ ] \[Contour\]\[Test\] History trim / configured limits
-   [ ] \[Test\] 100k-line benchmark
-   [ ] \[Future\]\[Test\] 1M-line benchmark
-   [ ] \[Test\] RSS / peak memory
-   [ ] \[Zz-Adapter\] Screen/Cell 低复制访问
-   [ ] \[Zz-Adapter\] View lifetime/invalidation contract

## Resize / Reflow

本节为粗粒度验收项；细粒度验收条目见下文 Wrap / Reflow 章，两处状态以
Wrap / Reflow 章为准。

-   [ ] \[Contour\]\[Test\]\[PASS-PoC\] Grow rows/columns 基础 resize
-   [ ] \[Contour\]\[Test\] Shrink rows/columns
-   [ ] \[Contour\]\[Test\] Soft-wrap reflow
-   [ ] \[Contour\]\[Test\] Hard-newline preservation
-   [ ] \[Contour\]\[Test\] Wide grapheme boundary
-   [ ] \[Contour\]\[Test\] Cursor mapping
-   [x] \[Zz-Native\]\[Test\] Selection/Search anchor mapping（M5a
    已交付 Selection 侧、M5b 已交付 Search 侧：丢弃平移 clamp、
    全丢清空、Alternate 切换清空）
-   [ ] \[Contour\]\[Test\] Alternate-screen resize behavior

## Wrap / Reflow

-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Hard Newline
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Soft Wrap
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Logical Line /
    Physical Row
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] DECAWM Enable
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] DECAWM Disable
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Wrap Pending
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Last Column
    Printable Character
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] CR while Wrap
    Pending
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] LF while Wrap Pending
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] BS while Wrap Pending
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Cursor Movement
    while Wrap Pending
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] CJK Wide
    Character at Right Boundary
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Combining Character at Right
    Boundary
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\]
    WideContinuation Consistency
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Grow Columns Reflow
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Shrink Columns Reflow
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Grow Rows
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Shrink Rows
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Cursor Mapping after Reflow
-   [x] \[Contour\]\[Zz-Native\]\[Test\] Selection Mapping after
    Reflow（M5a 已交付：列变 reflow 保持选区文本，
    testResizeReflowKeepsSelection 与双后端 compat 钉住）
-   [x] \[Contour\]\[Zz-Native\]\[Test\] Search Match Mapping after
    Reflow（M5b 已交付：列变 reflow 保持 match，集成测试钉住）
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Scrollback View Anchor after
    Reflow
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Alternate Screen Resize
    Behavior
-   [ ] \[Test\] 100k-line Reflow Benchmark
-   [ ] \[Test\] 1M-line Reflow Benchmark

## Input / IME / Transport

-   [x] \[Zz-Adapter\]\[PASS-PoC\] Terminal output -\> external
    transport
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Unicode text input
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Arrows / Home / End
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Insert/Delete / PgUp/PgDn
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] F1-F12
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Modifiers
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Application cursor/keypad
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Bracketed paste
-   [ ] \[Contour\]\[Zz-Adapter\]\[Test\] Focus reporting
-   [ ] \[Zz-Native\]\[Test\] Qt IME composition/commit bridge
-   [ ] \[Zz-Adapter\] ZzSshCore/libssh2 独立
-   [ ] \[Zz-Adapter\] ZzTermPty 独立
-   [ ] \[Future\] Serial / ADB shell transport

## Widget / Renderer / Scaling

-   [ ] \[Zz-Native\] QWidget + QPainter CPU renderer
-   [ ] \[Zz-Native\] Dirty-region repaint
-   [ ] \[Zz-Native\] Background/Text run batching
-   [ ] \[Zz-Native\] Cursor blink local repaint
-   [ ] \[Zz-Native\] DPI / Font fallback
-   [ ] \[Zz-Native\] Terminal font zoom + reset
-   [ ] \[Test\] Windows 100/125/150/200% DPI
-   [ ] \[Test\] Multi-monitor DPI change
-   [ ] \[Test\] Font fallback + CJK alignment
-   [ ] \[Test\] Font zoom + resize/reflow
-   [ ] \[Zz-Native\] Selection
-   [ ] \[Zz-Native\] Hyperlink hover
-   [ ] \[Rule\] 不使用 Contour Renderer / OpenGL / QRhi

## Terminal Font Zoom

-   [ ] \[Zz-Native\] Increase Font Size
-   [ ] \[Zz-Native\] Decrease Font Size
-   [ ] \[Zz-Native\] Reset Font Size
-   [ ] \[Zz-Native\] Ctrl + `+`
-   [ ] \[Zz-Native\] Ctrl + `-`
-   [ ] \[Zz-Native\] Ctrl + `0`
-   [ ] \[Zz-Native\] Ctrl + MouseWheel
-   [ ] \[Zz-Native\] Configurable Minimum Font Size
-   [ ] \[Zz-Native\] Configurable Maximum Font Size
-   [ ] \[Zz-Native\]\[Test\] Recalculate Font Metrics
-   [ ] \[Zz-Native\]\[Test\] Recalculate Cell Geometry
-   [ ] \[Zz-Native\]\[Test\] Recalculate Grid Size
-   [ ] \[Zz-Native\]\[Test\] Core Resize after Font Zoom
-   [ ] \[Zz-Native\]\[Test\] PTY Resize after Font Zoom
-   [ ] \[Zz-Native\]\[Test\] Preserve Scrollback View Anchor
-   [ ] \[Zz-Native\]\[Test\] Preserve Selection
-   [ ] \[Zz-Native\]\[Test\] Preserve Search Match
-   [ ] \[Zz-Native\]\[Test\] No Bitmap Scaling
-   [ ] \[Zz-Native\] Clear Glyph Cache after Font Change

## High DPI / Application Scale

-   [ ] \[Zz-Native\] Qt High DPI
-   [ ] \[Test\] Windows 125%
-   [ ] \[Test\] Windows 150%
-   [ ] \[Test\] Windows 175%
-   [ ] \[Test\] Windows 200%
-   [ ] \[Test\] Linux HiDPI
-   [ ] \[Test\] macOS Retina
-   [ ] \[Test\] Device Pixel Ratio != 1
-   [ ] \[Test\] Runtime DPI Change
-   [ ] \[Test\] Move Window Between Different-DPI Displays
-   [ ] \[Zz-Native\]\[Test\] Font Metrics Recalculation after DPI
    Change
-   [ ] \[Zz-Native\]\[Test\] Grid Recalculation after DPI Change
-   [ ] \[Zz-Native\]\[Test\] Core Resize after DPI Change
-   [ ] \[Zz-Native\]\[Test\] PTY Resize after DPI Change
-   [ ] \[Rule\] No DPI Information inside ZzTermCore
-   [ ] \[Rule\] No Pixel Geometry inside ZzTermCore

## Font / Glyph

-   [ ] \[Zz-Native\] Primary Monospace Font
-   [ ] \[Zz-Native\] CJK Font Fallback
-   [ ] \[Zz-Native\] Emoji Font Fallback
-   [ ] \[Zz-Native\]\[Test\] Combining Mark Rendering
-   [ ] \[Zz-Native\]\[Test\] Narrow Cell Geometry Stable
-   [ ] \[Zz-Native\]\[Test\] Wide Cell Geometry Stable
-   [ ] \[Zz-Native\]\[Test\] Fallback Glyph Does Not Change Grid Width
-   [ ] \[Zz-Native\] Baseline
-   [ ] \[Zz-Native\] Ascent / Descent
-   [ ] \[Zz-Native\] Underline Position
-   [ ] \[Zz-Native\] Strikeout Position
-   [ ] \[Zz-Native\] Configurable Line Spacing
-   [ ] \[Zz-Native\] Font Metrics Cache
-   [ ] \[Zz-Native\] Glyph Cache
-   [ ] \[Zz-Native\]\[Test\] Cache Invalidation after Font Change
-   [ ] \[Zz-Native\]\[Test\] Cache Invalidation after DPI Change

## Window / Grid Resize

-   [ ] \[Zz-Native\] Window Resize
-   [ ] \[Zz-Native\] Viewport Pixel Geometry Calculation
-   [ ] \[Zz-Native\] Margin Deduction
-   [ ] \[Zz-Native\] Scrollbar Deduction
-   [ ] \[Zz-Native\] Pixel Size → Columns/Rows
-   [ ] \[Zz-Native\]\[Test\] Minimum Grid 1×1
-   [ ] \[Zz-Native\]\[Test\] Rapid Resize Stability
-   [ ] \[Zz-Native\] Resize Event Coalescing
-   [x] \[Zz-Native\]\[PASS-Native\] Linux PTY `TIOCSWINSZ`
-   [ ] \[Zz-Native\] macOS PTY `TIOCSWINSZ`（M5）
-   [ ] \[Zz-Native\] Windows ConPTY Resize
-   [x] \[Zz-Native\]\[Test\]\[PASS-Native\] Core and PTY Grid Size
    Consistency

## Terminal Color / Theme

v2 “Theme / Search / Highlight” 节的 ZzTermTheme / terminal
palette、应用主题与终端主题分离、iTerm2 Color Schemes
三条并入本章对应子节，不重复出现。

### Color Semantic

-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Default
    Foreground
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Default
    Background
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] Indexed Color
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] RGB TrueColor
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Underline Color
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] ANSI Color
    Semantic 不提前转换为固定 RGB
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] TrueColor
    保留原始 RGB

### ANSI Palette

-   [ ] \[Contour\]\[Zz-Native\]\[Test\] ANSI Color 0～7
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Bright Color 8～15
-   [ ] \[Zz-Native\]\[Test\] Theme ANSI Palette
-   [ ] \[Zz-Native\]\[Test\] ANSI Palette Runtime Change
-   [ ] \[Zz-Native\]\[Test\] Historical ANSI Colors Follow Theme
    Change

### xterm 256 Color

-   [ ] \[Zz-Native\]\[Test\] Indexed 0～15 使用 Theme Palette
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Indexed 16～231 Color Cube
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] Indexed 232～255 Grayscale
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] 256-color
    Foreground
-   [ ] \[Contour\]\[Zz-Native\]\[Test\]\[PASS-Native\] 256-color
    Background
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] 256-color Underline

### TrueColor

-   [ ] \[Contour\]\[Test\]\[PASS-Native\] `38;2;R;G;B`
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] `48;2;R;G;B`
-   [ ] \[Contour\]\[Test\] Underline TrueColor
-   [ ] \[Contour\]\[Zz-Native\]\[Test\] TrueColor 不受 Terminal Theme
    ANSI Palette 影响

### Default Color

-   [ ] \[Contour\]\[Test\]\[PASS-Native\] SGR 39 Default Foreground
-   [ ] \[Contour\]\[Test\]\[PASS-Native\] SGR 49 Default Background
-   [ ] \[Contour\]\[Test\] Default Underline Color
-   [ ] \[Zz-Native\]\[Test\] Theme Change 更新 Default FG/BG
-   [ ] \[Zz-Native\]\[Test\] Scrollback Default Color 跟随 Theme
    Change

### ZzTermTheme

-   [ ] \[Zz-Native\] Theme Name
-   [ ] \[Zz-Native\] Foreground
-   [ ] \[Zz-Native\] Background
-   [ ] \[Zz-Native\] ANSI 16 Colors
-   [ ] \[Zz-Native\] Cursor Color
-   [ ] \[Zz-Native\] Cursor Text Color
-   [ ] \[Zz-Native\] Selection Background
-   [ ] \[Zz-Native\] Selection Foreground
-   [ ] \[Zz-Native\]\[Rule\] Theme Model 无 Qt 类型
-   [ ] \[Zz-Native\]\[Rule\] Theme Model 可独立于 Renderer 使用

### Built-in Theme

-   [ ] \[Zz-Native\] Default Dark
-   [ ] \[Zz-Native\] Default Light
-   [ ] \[Zz-Native\]\[Test\] ANSI Color Verification
-   [ ] \[Zz-Native\]\[Test\] Dark/Light Regression Test

### iTerm2 Color Scheme

-   [ ] \[Zz-Native\] `.itermcolors` Loader
-   [ ] \[Zz-Native\] Foreground Color
-   [ ] \[Zz-Native\] Background Color
-   [ ] \[Zz-Native\] ANSI 0～15
-   [ ] \[Zz-Native\] Cursor Color
-   [ ] \[Zz-Native\] Cursor Text Color
-   [ ] \[Zz-Native\] Selection Color
-   [ ] \[Zz-Native\]\[Test\] Missing Field Handling
-   [ ] \[Zz-Native\]\[Test\] Invalid File Handling
-   [ ] \[Zz-Native\]\[Test\] Import Regression Test

### Runtime Theme Switching

-   [ ] \[Zz-Native\] Runtime `setTerminalTheme()`
-   [ ] \[Zz-Native\]\[Test\] 不重建 ZzTermCore
-   [ ] \[Zz-Native\]\[Test\] 不清空 Scrollback
-   [ ] \[Zz-Native\]\[Test\] 不重新解析历史
-   [ ] \[Zz-Native\]\[Test\] 不重新创建 PTY
-   [ ] \[Zz-Native\]\[Test\] Visible Screen Immediate Repaint
-   [ ] \[Zz-Native\]\[Test\] Scrollback Immediate Theme Update
-   [ ] \[Zz-Native\]\[Test\] Theme Cache Invalidation
-   [ ] \[Zz-Native\]\[Test\] Repeated Theme Switching Stability

### Theme Cache

-   [ ] \[Zz-Native\] Theme Generation / Version
-   [ ] \[Zz-Native\]\[Test\] Color Cache Invalidation
-   [ ] \[Zz-Native\]\[Test\] Brush Cache Invalidation
-   [ ] \[Zz-Native\]\[Test\] Text Run Cache Invalidation
-   [ ] \[Zz-Native\]\[Test\] Background Run Cache Invalidation
-   [ ] \[Zz-Native\]\[Test\] 无旧 Theme 残留

### Cursor

-   [ ] \[Zz-Native\] Cursor Color
-   [ ] \[Zz-Native\] Cursor Text Color
-   [ ] \[Zz-Native\]\[Rule\] Cursor Shape 与 Cursor Color 解耦
-   [ ] \[Zz-Native\] Block Cursor
-   [ ] \[Zz-Native\] Underline Cursor
-   [ ] \[Zz-Native\] Bar Cursor
-   [ ] \[Zz-Native\]\[Test\] Theme Change 更新 Cursor Color

### Selection / Decoration

-   [ ] \[Zz-Native\] Selection Background
-   [ ] \[Zz-Native\] Selection Foreground
-   [ ] \[Zz-Native\] Keyword Decoration
-   [ ] \[Zz-Native\] Regex Decoration
-   [ ] \[Zz-Native\] Search Match Decoration
-   [ ] \[Zz-Native\] Current Search Match Decoration
-   [ ] \[Zz-Native\] Hyperlink Hover Decoration
-   [ ] \[Zz-Native\]\[Rule\] Decoration 不修改原始 VT Cell Attribute

### Render Priority

-   [ ] \[Zz-Native\] Terminal VT Attribute
-   [ ] \[Zz-Native\] Terminal Theme Resolution
-   [ ] \[Zz-Native\] Keyword / Regex
-   [ ] \[Zz-Native\] Search Match
-   [ ] \[Zz-Native\] Selection
-   [ ] \[Zz-Native\] Hyperlink Hover
-   [ ] \[Zz-Native\] Cursor
-   [ ] \[Zz-Native\]\[Test\] 多 Decoration 冲突规则确定且有测试

### Theme / Application Boundary

-   [ ] \[Rule\] Terminal Theme 独立于 Application Theme
-   [ ] \[Rule\] Application Dark/Light 不进入 ZzTermCore
-   [ ] \[Rule\] Toolbar Theme 不进入 ZzTermCore
-   [ ] \[Rule\] Tab Theme 不进入 ZzTermCore
-   [ ] \[Rule\] Button Theme 不进入 ZzTermCore
-   [ ] \[Rule\] Background Blur/Image 不进入 Core
-   [ ] \[Rule\] Qt `QColor` 不进入 Theme Core Model
-   [ ] \[Rule\] Qt `QPalette` 不进入 Theme Core Model

### Cross-platform

-   [ ] \[Test\] Linux Theme
-   [ ] \[Test\] Windows Theme
-   [ ] \[Test\] macOS Theme
-   [ ] \[Rule\] Theme Model 不依赖 OS
-   [ ] \[Future\] OpenHarmony Renderer 可消费相同 ZzTermTheme

## Search / Highlight

-   [ ] \[Zz-Native\] Search
-   [ ] \[Zz-Native\] Search highlight
-   [ ] \[Zz-Native\] Keyword matcher
-   [ ] \[Zz-Native\] Regex matcher
-   [ ] \[Zz-Native\] ZzDecoration logical range
-   [ ] \[Test\] Terminal Style -\> Keyword -\> Search -\> Selection
    -\> Cursor

## PTY / Demo

-   [x] \[Zz-Native\]\[PASS-Native\] Linux PTY
-   [ ] \[Zz-Native\] macOS PTY
-   [ ] \[Zz-Native\] Windows ConPTY
-   [ ] \[Zz-Native\] ZzTermDemo
-   [ ] \[Test\]\[PASS-Native\] bash/zsh（v1 勾选含 bash/zsh，实际仅
    bash 实测，zsh 未实测；需在新栈重验）
-   [ ] \[Test\] PowerShell/cmd
-   [ ] \[Zz-Native\] Terminal Inspector
-   [ ] \[Zz-Native\] VT sequence monitor

## Compatibility

-   [ ] \[Test\]\[PASS-Native\] bash / zsh（v1 勾选含
    bash/zsh，实际仅 bash 实测，zsh 未实测；需在新栈重验）
-   [ ] \[Test\] vim / neovim
-   [ ] \[Test\] nano / less
-   [ ] \[Test\] top / htop
-   [ ] \[Test\] tmux
-   [ ] \[Test\] git log
-   [ ] \[Test\] 256 colors / TrueColor
-   [ ] \[Test\] 中文输出 / 中文 IME
-   [ ] \[Test\] Mouse / Bracketed Paste
-   [ ] \[Test\] OSC 8

## Build / Release / API

-   [x] \[Zz-Native\]\[PASS-Native\] CMakeLists.txt
-   [x] \[Zz-Native\]\[PASS-Native\] CMakePresets.json
-   [x] \[Zz-Native\]\[PASS-Native\] Static library
-   [x] \[Zz-Native\]\[PASS-Native\] Shared library
-   [x] \[Test\]\[PASS-PoC\] Windows MSVC backend
-   [ ] \[Test\] Linux GCC/Clang（GCC 已验证 \[PASS-PoC\]，Clang 待 CI）
-   [ ] \[Test\] macOS Apple Clang
-   [x] \[Zz-Native\]\[PASS-Native\] Doxygen 中文 API 注释
-   [x] \[Zz-Native\]\[PASS-Native\] docs/API.md
-   [ ] \[Zz-Native\] CI 自动生成 API 文档
-   [ ] \[Zz-Native\] API 文档与源码同步检查
-   [x] \[Zz-Native\]\[PASS-Native\] Install/Export CMake package
-   [ ] \[Zz-Native\] Version / ABI policy
-   [ ] \[Zz-Native\] Contour commit/version 记录
-   [ ] \[Zz-Native\] Contour upgrade gate

## 单项完成记录模板

    Feature:
    Category: Contour / Zz-Adapter / Zz-Native
    Status:
    Contour Commit:
    Spec:
    Adapter:
    Unit Test:
    Regression Test:
    Integration Test:
    Platforms:
    Performance:
    Notes:

"Complete" 不仅代表代码存在。涉及 Contour capability
的项目必须证明当前固定 commit 的行为满足 ZzTermCore
要求；涉及用户可见行为的功能至少在 ZzTermDemo
中完成一次集成验证。涉及双后端的条目，"Complete" 要求默认（Contour）后端验收；[PASS-Native]
单独不构成勾选条件。
