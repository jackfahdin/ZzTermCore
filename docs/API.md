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
`ZZTERM_API` 宏（`ZzTerm/Export.h`，静态构建时为空宏）。

## 线程与所有权约定

> 随实现补充。

## 错误行为约定

> 随实现补充。

## 模块

### Terminal（语义调度）

Parser 只负责语法 dispatch，Terminal 负责语义。

`ZzTerminal::feed` 的真实链路：原始字节流先经 `ZzVtParser` 做增量
语法解析（可在 UTF-8 字符或 VT 序列中间截断，状态跨 feed 保留），
print 通路再经 `ZzUtf8Decoder` 增量解码为码位，CSI/ESC/OSC/C0 事件
由 Terminal 的语义层消费（模式解释、画笔状态、历史入栈等）。
远端输入一律视为不可信：畸形/超长序列被安全丢弃并恢复，feed 不抛异常。

已落地语义：

- print 采用 pending-wrap 语义：字符写满行尾后不立即换行，置位
  wrap-pending；下一个可打印字符到达时才执行换行（滚屏在此时发生），
  保证行尾字符不被提前挤出且 autowrap 行为与 xterm 一致。
- SGR 画笔模型：Terminal 持有当前画笔（属性位 + 前景/背景色），
  SGR 序列只改画笔，print 时把画笔快照写入单元格；支持 Reset、
  Bold/Faint/Italic/Blink/Inverse/Invisible/Strikethrough、
  ANSI 16/Bright、256 色、RGB TrueColor、Default FG/BG。
- OSC 0/1/2 设置窗口/图标标题（UTF-8），经 `title()` 读取，
  变化在 `ZzTermChanges::titleChanged` 中上报；DCS 载荷安全忽略。
- C0/C1 控制（BEL/BS/HT/LF/VT/FF/CR、IND/NEL/RI/HTS）、CSI 光标移动
  （CUU/CUD/CUF/CUB、CNL/CPL、CHA/VPA、CUP/HVP、Save/Restore）、
  擦除/插删/滚动（ED/EL/ECH/ICH/DCH/IL/DL/SU/SD）均已接入；
  逐项覆盖见 `docs/VT-Xterm-Checklist.md`。

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

### PTY（ZzTermPty，Unix）

- 头文件 `ZzPty.h`（target `ZzTermPty`，纯 OS 封装，不依赖 ZzTermCore；
  仅 Unix 构建，macOS 头文件差异 M5 处理，Windows ConPTY 里程碑靠后）。
- `ZzPty::spawn(ZzPtyConfig)` 失败返回 nullptr，errno 保留（含子进程 exec
  失败）；`masterFd()` 供 poll/select/QSocketNotifier 事件驱动。
- `read` 返回 0 表示 EOF（Linux EIO 归一）；`writeAll` 循环写完或出错；
  `resize` 即 TIOCSWINSZ；`tryWait` 非阻塞收集退出码（信号杀死为
  128 + 信号号）。析构 SIGHUP（必要时 SIGKILL）子进程并回收僵尸。
- 调试工具 `ZzTermSmoke`（`examples/ZzTermSmoke`）：PTY -> ZzTerminal ->
  stdout 全屏重绘的控制台冒烟 Demo，子进程退出即以其退出码退出。

### RenderView

Core 与 Renderer 的稳定只读边界，Renderer 不访问 Core 私有容器。

> 随实现补充。

## 版本与 ABI 策略

> 随实现补充（M6 里程碑收敛）。
