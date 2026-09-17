# ZzTermCore API 说明

> 本文档维护高层 API 说明（模块边界、生命周期、线程与所有权约定）；
> 详细接口以源码 Doxygen 注释自动生成的文档为准（`doxygen Doxyfile`，
> 输出 `docs/api-html`）。
>
> 规范依据：`docs/Architecture.md` 第 17、18 节。公开 API 的新增/修改
> 必须同步更新本文档，属于代码评审的 Definition of Done。

## 总览

``` text
bytes -> UTF-8/VT/xterm Parser -> Terminal State
      -> Cell/Line/Screen -> Scrollback -> RenderView

Frontend semantic events -> InputEncoder -> bytes
```

Core 平台无关：不依赖 Qt、Windows API、POSIX PTY、OpenGL、网络等任何
平台头文件。所有公开类型使用 `Zz` 前缀，动态库导出统一使用
`ZZTERM_API` 宏（`ZzTerm/zzterm_export.h`，静态构建时为空宏）。

## 线程与所有权约定

> 随实现补充。

## 错误行为约定

> 随实现补充。

## 模块

### Terminal（语义调度）

Parser 只负责语法 dispatch，Terminal 负责语义。

> 随实现补充。

### Parser（UTF-8 / VT / xterm）

增量解析，支持任意 chunk 边界；状态至少包含 Ground、Escape、
CSI Entry/Param/Intermediate、OSC String、DCS Entry/Data。

> 随实现补充。

### Unicode

分片 UTF-8、East Asian Width、combining mark、variation selector、
emoji/ZWJ grapheme。禁止假设 1 code point == 1 cell。

> 随实现补充。

### Screen / Cell / Line

Primary/Alternate Screen、Cursor、Scroll Region、Tab Stops、
Dirty Tracking；区分 logical line 与 physical row、hard newline 与
soft wrap；公共 API 不暴露底层容器。

> 随实现补充。

### History / Scrollback

第一阶段 chunked RAM history；架构预留 Hot RAM / Warm LZ4 /
Cold mmap-file 扩展。Screen 不知道历史后端类型。

> 随实现补充。

### Input Encoder

以 `ZzKeyEvent`、`ZzMouseEvent`、Paste、Focus 等语义事件进入
`ZzInputEncoder`，UI 不直接拼 escape sequence。

> 随实现补充。

### RenderView

Core 与 Renderer 的稳定只读边界，Renderer 不访问 Core 私有容器。

> 随实现补充。

## 版本与 ABI 策略

> 随实现补充（M6 里程碑收敛）。
