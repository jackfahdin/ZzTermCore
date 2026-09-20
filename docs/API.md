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

`ZzTerminal` 是双后端顶层外观（Facade，M1b 起）：构造签名
`ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines = 10000)`，
backend 显式选择终端引擎——`ZzBackendKind::Native`（自研引擎，兼容性
对照基准）或 `ZzBackendKind::Contour`（Contour vtbackend，需
ZZTERM_WITH_CONTOUR=ON 构建；OFF 构建传 Contour 抛 `std::logic_error`）。
parser/screen/scrollback 等引擎组件归各后端实现持有，`ZzTerminal`
只经后端接口委托，公开 API 不暴露任何后端类型。

- `feed` 返回 `ZzTermChanges` 变化摘要（屏幕/历史/活动缓冲区/标题/
  BEL/滚入行数），远端输入一律视为不可信：畸形/超长序列被安全丢弃
  并恢复，feed 不抛异常。
- `setOutputHandler` 设置终端回传字节通道（DA 响应、光标上报等）；
  Contour 后端有效，native 暂不回传。
- `setAmbiguousWidthMode(bool)` 设置 Ambiguous 宽度模式（UAX #11
  A 类别码位列宽）：true 按 2 列（CJK 环境）、false 按 1 列
  （xterm 默认，构造初值）。仅 native 后端生效，Contour 无对应
  配置项、调用为空操作（适配层注释钉住的已知分歧）；设置对其后
  的 feed 生效，已落格内容不 retroactive 重排。
- `screen()` 与 `scrollback()` 为 Core 内部协作口（可变访问工作区/
  历史后端），仅 Native 后端可用、不带 noexcept，Contour 后端调用
  抛 `std::logic_error`。
- `ZzTermChanges::scrolledOutLines` 存在已知后端语义差：native 按
  实际滚出行计数、与容量无关；Contour 以历史行数差值近似，scrollback
  饱和后停止上报（以 Doxygen 注释为准）。

Native 引擎链路：原始字节流先经 `ZzVtParser` 做增量语法解析（可在
UTF-8 字符或 VT 序列中间截断，状态跨 feed 保留），print 通路再经
`ZzUtf8Decoder` 增量解码为码位，CSI/ESC/OSC/C0 事件由引擎语义层消费
（模式解释、画笔状态、历史入栈等）。

Native 引擎已落地语义：

- print 采用 pending-wrap 语义：字符写满行尾后不立即换行，置位
  wrap-pending；下一个可打印字符到达时才执行换行（滚屏在此时发生），
  保证行尾字符不被提前挤出且 autowrap 行为与 xterm 一致。M2 起
  wrap-pending 对齐 xterm 无条件置位模型：写满右边距即置位、与
  DECAWM 开关无关，消费时按当时的 DECAWM 状态决定是否换行
  （DECAWM 关闭时后续字符覆写最后一格）。
- 宽字符落格（M2）：print 通路按 `zzCellWidthOf` 取码位列宽，
  W/F 类别占双格（首格 WideLead、续格 WideContinuation，续格不
  单独渲染）；行尾仅剩一格放不下宽字符时先换行再落格；覆写宽
  字符半格时清理另一侧配对格，不残留半格。
- SGR 画笔模型：引擎持有当前画笔（属性位 + 前景/背景色），
  SGR 序列只改画笔，print 时把画笔快照写入单元格；支持 Reset、
  Bold/Faint/Italic/Blink/Inverse/Invisible/Strikethrough、
  ANSI 16/Bright、256 色、RGB TrueColor、Default FG/BG。
- OSC 0/1/2 设置窗口/图标标题（UTF-8），经 `title()` 读取，
  变化在 `ZzTermChanges::titleChanged` 中上报；DCS 载荷安全忽略。
- C0/C1 控制（BEL/BS/HT/LF/VT/FF/CR、IND/NEL/RI/HTS）、CSI 光标移动
  （CUU/CUD/CUF/CUB、CNL/CPL、CHA/VPA、CUP/HVP、Save/Restore）、
  擦除/插删/滚动（ED/EL/ECH/ICH/DCH/IL/DL/SU/SD）与 DECSTBM
  （CSI r 滚动区）均已接入；
  逐项覆盖见 `docs/VT-Xterm-Checklist.md`。
- DEC 私有模式（M2）：1049/1047/1048 备用屏幕切换（1049 取 xterm
  语义，即 1048 保存光标加 1047 切屏，退出时恢复主屏内容、光标
  与滚动区）、DECAWM `?7`（自动换行）、DECTCEM `?25`（光标可见性）
  已接入 native；`isAlternateScreen()` 与 `cursor().visible` 对
  native 上报真实值。其余 DEC 私有模式（鼠标、bracketed paste 等）
  安全忽略，属 M3。

### Parser（UTF-8 / VT / xterm）

增量解析，支持任意 chunk 边界；状态至少包含 Ground、Escape、
CSI Entry/Param/Intermediate、OSC String、DCS Entry/Data。

> 随实现补充。

### Unicode

分片 UTF-8、East Asian Width、combining mark、variation selector、
emoji/ZWJ grapheme。禁止假设 1 code point == 1 cell。

- `zzCellWidthOf(char32_t cp, bool ambiguousWide = false)`
  （`ZzTerm/UnicodeWidth.h`）返回单码位单元格宽度（1 或 2 列，
  UAX #11）：W/F 类别 2 列；Ambiguous 由 `ambiguousWide` 决定
  （true 2 列、默认 false 1 列，xterm 兼容）；其余码位（含未
  列出、combining、控制区间）1 列，越界码位防御性返回 1。
  实现为 `constexpr` 二分查找紧凑区间表。
- 宽度数据内嵌为 303 个 EAW 区间（`ZzTerm/detail/UnicodeWidthData.inc`），
  由 `scripts/gen_unicode_width.py` 从 Unicode 官方 16.0.0 钉版
  数据生成，不依赖系统 wcwidth(3)；更新须重跑脚本而非手改。
- grapheme 聚簇（combining/VS/emoji/ZWJ，UAX #29）在本接口之上
  分层实现，属 M3；当前每码位独立落格。

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

Core 与 Renderer 的稳定只读边界（M1b 重写为后端无关抽象契约），
Renderer 不访问 Core 私有容器。

- `ZzRenderView` 为纯虚接口：`size` / `isAlternateScreen` / `lineAt` /
  `cursor` / `dirtyGeneration` / `rowDirty` / `dirtyRange`，经
  `ZzTerminal::renderView` 获取，由各后端提供实现。
- `ZzLineView` 为类型擦除的行只读句柄：值语义、行状态内联存储
  （无堆分配），拷贝廉价；`cellAt` 产出值语义单格视图 `ZzCellView`
  （UTF-8 文本、前景/背景色、属性位、单元格宽度）。
- 借用寿命：视图借用 Terminal，不得比 Terminal 长寿；帧内使用、
  跨帧重新读取查询结果，feed/resize 后既有视图与 `ZzLineView`
  句柄全部失效；与 feed 同线程使用，非线程安全。
- Dirty 粒度：native 为行级 dirty，`dirtyRange` 精确到行内格区间；
  粗粒度后端（Contour）有脏时 `rowDirty` 恒 true、`dirtyRange`
  恒全行，契约对两种实现均成立。

## 版本与 ABI 策略

> 随实现补充（M6 里程碑收敛）。
