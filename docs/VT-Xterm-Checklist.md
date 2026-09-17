# ZzTermCore VT/xterm 功能实现 Checklist

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
