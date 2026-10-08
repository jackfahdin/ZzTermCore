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
      -> Cell/Line/Screen -> Scrollback -> RenderView + HistoryView

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
`ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines = 100000)`，
backend 显式选择终端引擎——`ZzBackendKind::Native`（自研引擎，兼容性
对照基准）或 `ZzBackendKind::Contour`（Contour vtbackend，需
ZZTERM_WITH_CONTOUR=ON 构建；OFF 构建传 Contour 抛 `std::logic_error`）。
parser/screen/scrollback 等引擎组件归各后端实现持有，`ZzTerminal`
只经后端接口委托，公开 API 不暴露任何后端类型。scrollbackMaxLines
默认 10 万行（M4 起，perf 门控保障），0 表示不保留历史。

- `feed` 返回 `ZzTermChanges` 变化摘要（屏幕/历史/活动缓冲区/标题/
  BEL/滚入行数），远端输入一律视为不可信：畸形/超长序列被安全丢弃
  并恢复，feed 不抛异常。
- `setOutputHandler` 设置统一 output 通道：前端输入编码字节
  （sendText/sendKey）与终端回传字节（DA 响应、DSR/CPR 应答等）均经
  此单一通道发出；未设置 handler 时字节静默丢弃。两后端均有效
  （M3a 起，native 回传已接入）。
- `sendText(std::string_view)` 发送普通文本输入（Unicode 输入、
  IME commit text；入参为已确认合法 UTF-8，Core 不重复校验）；
  `sendKey(const ZzKeyEvent&)` 发送按键语义事件（功能键、组合键；
  普通字符优先 sendText）。两者编码字节统一经 output 通道发出。
- `sendMouse(const ZzMouseEvent&)` 发送鼠标语义事件（网格坐标
  0 起始，编码时换算为协议 1 起始）；`sendPaste(std::string_view)`
  发送粘贴文本；`sendFocus(bool)` 发送焦点事件。三者编码字节同样
  统一经 output 通道发出；未开对应模式或未设 handler 时静默丢弃
  （M3b 起，两后端均支持）。
- 鼠标/粘贴/焦点模式联动（M3b，由 feed 接收的 DEC 序列驱动，前端
  无需自管）：鼠标上报模式 ?9/?1000/?1002/?1003 互斥（X10 仅按下、
  Normal 按下+释放、Button-event 加按下时拖动、Any-event 加任意
  移动），?1006 选择鼠标编码格式；?2004 bracketed paste 开启时
  sendPaste 自动包裹 200~/201~；?1004 focus reporting 开启时
  sendFocus 发 CSI I（获得焦点）或 CSI O（失去焦点）。
- 鼠标编码两格式：经典 X10/Normal（码值 +32 得三字节，坐标上限
  223）与 SGR 1006（CSI 以小于号引导，分号分隔按钮码、列、行，
  M 结尾为按下、m 结尾为释放）；无按钮移动上报码为 35。Contour
  侧经 `zzMouseButton` 映射委托其自家输入路径（两端枚举顺序差
  已在映射层处理）。
- 输入编码与终端模式联动：application cursor（DECCKM ?1）下
  方向键/Home/End 编码为 SS3（ESC O x），否则为 CSI（ESC [ x）；
  application keypad（ESC=/ESC>）模式位已同步到 native 编码器，
  小键盘键编码待 numpad 键类型引入后生效。模式由 feed
  接收的 DEC 序列驱动，前端无需自管同步。Contour 后端经类型映射
  委托其自家输入路径（ZzInputEncoder 为 native 内部组件，不属
  公开契约）。
- 终端回传（M3a，两后端均支持）：DA1（CSI c 设备属性查询）native
  应答 VT102 级最小集；DSR 5n（状态查询）应答就绪；CPR 6n（光标
  位置查询）上报当前光标行列。回传字节同样经 output 通道发出，
  且不影响 dirty 标记。DA 应答串为实现相关差异（Contour 应答其
  自家特征串），属已钉住的 b 类分歧，前端不得依赖具体应答内容。
- `setAmbiguousWidthMode(bool)` 设置 Ambiguous 宽度模式（UAX #11
  A 类别码位列宽）：true 按 2 列（CJK 环境）、false 按 1 列
  （xterm 默认，构造初值）。仅 native 后端生效，Contour 无对应
  配置项、调用为空操作（适配层注释钉住的已知分歧）；设置对其后
  的 feed 生效，已落格内容不 retroactive 重排。
- `resize(cols, rows)` 调整终端尺寸（M4 起支持真 reflow，M15 起行变
  按条件语义搬行）：列变化触发 soft-wrap reflow——屏幕区与 scrollback
  历史一起重组，logical line 合并后按新列宽重切（列变重组），宽字符
  不拆半，硬行缩列多行化、拉大沿 wrapped 链合并恢复（M16，对齐 Contour/xterm，
  取代 M4 硬行截断），光标按逻辑行链跟随内容；行变化（M15，双后端
  语义对齐 contour shrinkLines/growLines）：缩行先裁光标下方行（不入
  历史），不够裁时把 Primary 顶部行压入历史；扩行仅当光标贴末行时从
  最新历史回抽注入顶部，不足部分底部补空，光标不在末行时纯底部补空。
  Alternate Screen 不产生历史（备用屏无历史），Alternate 期间行变
  resize 时主屏网格仍按上述语义压历史/回抽。resize 后 RenderView、
  HistoryView 与既有 `ZzLineView` 句柄全部失效，前端需重新获取。
  尺寸未变或参数非法（非正）时返回 false 且为空操作。
  resize/reflow 经 ScrollOutCallback 溢出的行不计入 ZzTermChanges 的
  scrollbackChanged/scrolledOutLines（resize 无 changes 通道，前端
  resize 后重取视图）。跨历史/屏幕接缝的链在列变 resize 时经归还机制
  统一重组（M16b），缩列跨缝状态保持连续、拉大接回（对齐 contour 统一流）。
  列变重组内容收缩（重组产出不足行数）时先经 HistoryPullCallback 从
  最新历史顶补填满屏幕——内容贴底锚定、光标随顶补平移，历史不足
  余量底部补空，Alternate 永不顶补（M16c，对齐 contour 统一流净效果）。
  扩列 reflow 时 Primary 缓冲中光标所在的折链豁免收链、保持旧宽度
  拆分（M17a，readline WINCH 重绘按旧布局帧发相对擦除的兼容保护，
  详见 docs/Scrollback-and-Reflow.md「光标活动链保护」）；仅扩列方向、
  仅 Primary，与 contour 后端在该场景有意偏离。
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
  native 上报真实值。已知差异：native 进入 alt 后光标取 alt
  缓冲区自存位置（首次进入即原点 (0,0)），xterm 则保持主屏光标
  位置不动——真实应用进 alt 后均自行定位光标，此为规格 4.3 的
  有意简化。其余 DEC 私有模式（鼠标、bracketed paste 等）
  安全忽略，属 M3。

### 选区与复制（M5a）

选区基于 `ZzLogicalPos` 统一空间坐标（`ZzTerm/Types.h`）：`line` 为逻辑
行序号，0 = 当前最早一条有效逻辑行（历史区头部），屏幕区紧跟其后；
append 与滚动不改变已有内容的序号，历史头部丢弃时序号整体下移、Core
自动平移选区锚点（物理行计数近似并 clamp）；`col` 为逻辑行内单元格
偏移（0 起，按格不按字符），落在宽字符续格上时提取层归一到 lead 格。
坐标越界 clamp 到有效范围；切换 Alternate 屏时选区清空。

`ZzTerminal` 六条选区 API（双后端语义一致）：

- `setSelection(anchor, extent)` 设置选区（替换现有）；两端无序要求，
  内部规范化。
- `extendSelection(extent)` 拖动活动端（anchor 不变）；无选区时等价于
  `setSelection(extent, extent)`。
- `clearSelection()` 清空选区。
- `hasSelection()` 是否有非空选区（anchor != extent）。
- `selectionRange(start, end)` 查询规范化选区区间（半开区间
  [start, end)），空选区返回 false 且不写入出参；供前端绘制高亮。
- `selectedText()` 提取选区纯文本（UTF-8；空选区返回空串）。

提取规则四条（规格 5.4）：

1. 宽字符按格步进、续格跳过；边界落在半字上时归一（start 退到 lead、
   end 进到续格之后），不拆半字；
2. cluster 格取整串；
3. 软换行不插换行；跨逻辑行插单个换行符，末尾无换行；
4. 每条逻辑行尾部的空单元格与空格修剪；行内空单元格输出为一个空格。

前端职责：鼠标/触摸像素坐标换算为 `ZzLogicalPos` 由前端负责（Core 不
引入像素概念）；高亮绘制经 `selectionRange` 取区间后自行换算屏幕行；
feed/resize 后坐标可能已被平移或 clamp，需重新查询。双后端
`selectedText` 逐字节一致性由 test_selection_compat 钉住（native 为
基准，本里程碑未产生新 b 类分歧）。

已知限制：滚动区局部滚动、DL/IL 等销毁屏幕内容的操作发生时，后续逻辑行
序号上移而已有选区锚点不跟随（规格 5.1 的序号不变量只对全屏滚出入历史
成立）；v1 接受该语义。

### 搜索（M5b）

子串搜索基于 `ZzLogicalRange`（`ZzTerm/Types.h`）：半开区间
[start, end)，start 为命中首格，end 为命中末格之后一格，坐标语义同
`ZzLogicalPos` 统一空间（历史区头部为 0，屏幕区紧跟其后）。

`ZzTerminal` 四条搜索 API（双后端语义一致）：

- `search(pattern, options)` 执行子串搜索并替换旧搜索状态，返回匹配数；
  空 pattern 清空搜索状态并返回 0。`ZzSearchOptions` 目前仅
  `caseSensitive`（默认 true；false 时 ASCII 大小写折叠，Unicode
  不折叠）。
- `clearSearch()` 清空搜索状态。
- `searchMatchCount()` 当前搜索的 match 总数（无搜索状态为 0）。
- `searchMatch(index, start, end)` 查询第 index 个 match 的坐标区间
  （坐标升序）；index 越界或无搜索状态返回 false 且不写入出参。

match 坐标快照语义：match 是搜索时刻的坐标快照，此后 feed 改写的同坐标
内容不校验（内容漂移钉注，规格 5.4）；新内容不触发自动重搜。保持机制与
选区同口径：历史头部丢弃时 Core 按物理行计数近似平移 match 并 clamp，
列变 reflow 保持 match，切换 Alternate 屏时搜索状态清空。

前端职责：高亮绘制由前端经 `searchMatch` 取 match 坐标后自行换算屏幕行
绘制（Core 不引入装饰/渲染概念）；feed 后是否重搜由前端决定。

钉注四条：

1. 不跨逻辑行匹配：pattern 含换行符时永不命中；
2. 命中不重叠：同一逻辑行内 match 区间互不相交；
3. 大小写折叠仅 ASCII：`caseSensitive = false` 只折叠 ASCII 字母，
   Unicode 大小写不折叠；
4. 裸组合符 pattern 归并整格：pattern 为组合符等 cluster 字节串的中段
   片段时，命中坐标归并到所在整格，命中文本与 pattern 逐字节相等的
   自洽不变量不成立；v1 接受该语义（T2 审查裁定，规格 5.2）。
5. （M16 起本条废止：硬行缩列多行化后不存在截断行，match 的 col 均在行内。）

双后端 match 列表逐一相等由 test_search_compat 钉住（native 为基准，
本里程碑未产生新 b 类分歧）；10 万行搜索性能门控见
test_perf_search 与 tests/perf/records/2026-09-21-m5b-search.json。

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

以 `ZzKeyEvent`、`ZzMouseEvent`、Paste、Focus 等语义事件进入输入
链路，UI 不直接拼 escape sequence。输入方向公开面为 facade 的
`sendText` / `sendKey`（M3a）与 `sendMouse` / `sendPaste` /
`sendFocus`（M3b，语义与模式联动见 Terminal 节）；`ZzInputEncoder`
为 native 后端内部组件（`ZzTerm/Input.h` 定义事件类型），Contour
后端经类型映射（`ZzContourConvert.h`）委托其自家输入路径，两后端在
compat 用例覆盖的输入类上编码逐字节一致（键盘 compat 用例 12、鼠标/
粘贴/焦点用例 15-17 钉住；Release 携带 None 按钮的域外输入两后端
编码不同，见 compat 注释与后续跟踪）。当前覆盖方向键/Home/End/Insert/Delete/PageUp/PageDown/
F1-F12、Enter/Tab/Backspace/Escape 及修饰键组合；Ctrl+非字母 C0
映射（Space/@、方括号区间符号键、?）已支持；鼠标（经典与
SGR 1006 两编码格式）、bracketed paste、focus 上报已随 M3b 落地。

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
  stdout 全屏重绘的控制台冒烟 Demo；stdin 经 demo 本地 InputTranslator
  走 sendText/sendKey/sendMouse 输入链路（不再字节透传；鼠标识别
  经典 X10 与 SGR 1006 两种形态），子进程退出即以其退出码退出。

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

### HistoryView

M14 新增的后端无关历史行只读视图，与 RenderView 平行的第二只读边界：
RenderView 覆盖屏幕区，HistoryView 覆盖 scrollback 历史区。

- `ZzTerminal::historyView()` 返回 `const ZzHistoryView&`，借用 Terminal，
  不得比 Terminal 长寿。
- `ZzHistoryView` 为纯虚接口：`lineCount` / `lineAt` / `droppedLineCount` /
  `generation`。
- 坐标：index 属于 [0, lineCount())，0 = 最旧历史行；
  绝对行号 = droppedLineCount() + index（选区锚点平移/应用侧归档对齐用）。
- `lineAt(index)` 返回 ZzLineView 借用句柄：feed/resize/clear 或下一次
  lineAt 调用后失效（contour 为视图内部单行缓冲覆写，native 借 scrollback
  const 引用）。行宽恒等于终端当前列宽（resize 经 reflow 维持）。
- Alternate 屏 lineCount() 恒 0（Alternate 无历史），回 Primary 恢复。
- 变化侦测用 generation()（append/裁剪/reflow/clear/Alternate 切换递增，
  允许保守多增）；禁止每帧全扫历史，滚动查看按需取可见行。
- 线程：非线程安全，与 ZzTerminal 同线程。

## 版本与 ABI 策略

> 随实现补充（M6 里程碑收敛）。

- M14：新增公共类 `ZzHistoryView` 与 `ZzTerminal::historyView` 非虚方法，
  向后兼容的 minor 新增；内部 `ZzTerminalBackend` 加非纯虚默认实现，
  不影响公共 ABI。
- M15：`ZzScrollback` 新增 `takeNewest` 纯虚方法——该接口标注 Core
  内部使用但属公共头，实现类仅仓内 `ChunkedScrollback` 一个（外部若
  有自定义 `ZzScrollback` 实现需补实现，编译期可发现）；`ZzScreen`
  新增 `HistoryPullCallback` 类型别名与 `setHistoryPullCallback`
  方法（Core 内部协作口）；`ZzTerminal` 无签名变化，`resize` 行变
  行为语义变化（缩行压历史/扩行回抽，对齐 contour）。
- M16：行为语义变化（reflow 硬行由截断改为多行化，zzReflowLines/两端 reflow
  路径对外可观察结果变化），无 API 签名变化；M5b 的「截断行 col 快照」
  条款随之废止。
- M16b：行为语义变化（列变 reflow 的跨历史/屏幕接缝链不再劈开，经归还
  机制统一重组）；ZzScreen 新增 prependPrimaryLines 公共方法（Core 内部
  使用定位）；存量劈链不修复（链尾空白已被裁，只对新 resize 生效）。
- M17a：ZzScreen/ZzTerminal 无签名变化；行为语义变化——扩列 reflow 时
  Primary 光标所在折链豁免收链（readline 重绘兼容）；与 contour 后端
  在该场景有意偏离（偏离登记见 test_screen_reflow /
  test_native_reflow_topfill / test_screen_reflow_topfill 的 M17a 语义
  变更改写用例；compat seam 回程段光标不在折链上，parity 自然成立，
  见计划勘误 E-3）。另：wrapped 链各行尾部完全默认空白格恒为填充，
  reflow 与选区拼链逐行裁尾（M17a-4b），落被裁补白区的光标锚到链末行
  内容尾。
