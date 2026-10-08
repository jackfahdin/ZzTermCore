# Scrollback 与 Reflow：resize 内容保全的实现

本文档说明 ZzTermCore 如何在终端窗口尺寸变化（resize）时保证内容完整：
缩列时软折行、拉宽后原样恢复，行数变化时历史与屏幕之间的行双向流动。
语义基准为 Contour 与 xterm；设计决议见文末里程碑索引对应的规格文档。

## 1. 设计目标

-   无论列如何缩窄再拉大，最终内容必须完整（用户可验证的端到端语义）。
-   软换行（soft wrap）是显示折行，不是内容断行：缩列时一条逻辑行折成
    多条物理行，拉宽时沿 wrapped 链重新接回一条。
-   不改变已写入的字符内容本身；行尾完全默认的空白格在重组时裁除
    （与 Contour/xterm 一致，避免短行变窄产生幽灵行）。
-   resize 是终端模型行为，与前端渲染解耦：Core 负责网格与历史的重组，
    前端经只读视图契约（ZzRenderView / ZzHistoryView）感知结果。

## 2. 核心概念：物理行、逻辑行与 wrapped 链

数据模型（见 `include/ZzTerm/Line.h` 头部注释）：

-   physical row：屏幕上的一行，即 ZzLine，固定 cols 个单元格。
-   logical line：逻辑行，由若干连续物理行组成——除最后一行外每个组成
    行的 wrapped 标记为 true（软换行续接下一行），最后一行 wrapped 为
    false 表示以硬换行结束。
-   ZzLine 自身不感知归属，链关系由容器（ZzScreen / ZzScrollback）按
    上述规则推导。Copy、Search、Reflow 一律基于逻辑行。

wrapped 链是全部 resize 语义的支点：重组算法沿链把物理行序列合并回
逻辑行，再按新列宽重切，wrapped 标记随之重算。

## 3. 单点重组算法

`src/screen/Reflow.cpp`（内部头 `Reflow.h`，不安装）提供两个共享入口，
ZzScreen 与 ZzChunkedScrollback 两端共用同一算法核，杜绝语义漂移：

-   `zzReflowLines`：全量重组。输入物理行序列（按值移交）、旧列宽、新
    列宽，输出重组后的物理行序列；可选 ZzReflowCursor 光标跟踪——输入
    光标所在的逻辑行链序号与链内流偏移，输出重组后的物理行列坐标。
    屏幕路径使用本入口（需要光标跟踪）。
-   `ZzReflowStreamer`（M8b）：流式变体，逐批喂入、跨批只携带未完成链，
    峰值内存为 O(链长) 而非 O(全历史)。语义与 zzReflowLines 逐字节一致。
    历史路径使用本入口（不需要光标跟踪）。

重组规则要点：

-   链按新列宽重切：内容超宽的逻辑行多行化，变宽后沿 wrapped 链接回。
-   硬行不做特判（M16 语义翻转）：未 wrapped 的单行链即 chainLen 为 1
    的普通链，缩列同样多行化、拉大同样接回。这取代了 M4 的"硬行截断"
    语义——后者是事实性错误的对齐主张，已被 M16 规格正式取代。
-   链末尾的完全默认空白格裁除。
-   宽字符（占两列的单元格）原子搬运，不落在行边界上（边界前移一格并
    补默认空白）。
-   grapheme cluster 格在新行重新 internCluster。

## 4. 列变 reflow 的协调（ZzNativeBackend::resize）

列变化时 `ZzNativeBackend::resize`（`src/backend/native/ZzNativeBackend.cpp`）
按固定顺序协调历史与屏幕两个域：

1.  跨缝链接续保护（M16b）：若历史末行 wrapped 为 true，说明一条逻辑行
    链横跨历史与屏幕的接缝（续接部分在 Primary 屏幕首链）。backend 沿
    链头回找，经 `ZzScrollback::takeNewest` 把整条历史侧尾链摘除，再经
    `ZzScreen::prependPrimaryLines` 归还屏幕顶部，使整条链在屏幕域统一
    重组——避免历史 reflow 把跨缝链按 dangling 尾链终结而劈成两条。
2.  `scrollback_` 先按新列宽 reflow（流式路径）。
3.  `screen_` 再按新列宽 reflow（全量路径，含光标跟踪）。屏幕重组溢出
    的行以新宽度经 ScrollOutCallback 回流到已重组的历史，宽度不变量
    自洽。

历史代计数（M14）随之递增：历史 reflow 计一次，屏幕回流 append 经
ScrollOutCallback 另计，保证 ZzHistoryView 持有方能感知失效。

Alternate 缓冲区无历史：重组溢出行直接丢弃，不参与上述往返。

## 5. 行变 resize 的条件语义（M15）

行数变化不走 reflow，由 `ZzScreen::resize` 按条件语义搬行（对齐 Contour
Grid 的 shrinkLines/growLines）：

-   缩行：先裁光标下方的行（直接丢弃，不入历史）；不够裁时把 Primary
    顶部行经 ScrollOutCallback 压入历史（无回调则丢弃，同 reflow 溢出
    语义）；光标随内容平移。
-   扩行：仅当光标贴末行时，经 HistoryPullCallback 从最新历史回抽行
    注入屏幕顶部；不足部分底部补空行。光标不贴末行时纯底部补空，
    不动历史。
-   Alternate 缓冲区无回调路径：尾部截断或补空。

两个回调在 backend 构造时接线（ZzNativeBackend.cpp）：

-   ScrollOutCallback：累计滚出行数、append 入 scrollback、历史代计数
    递增。
-   HistoryPullCallback：经 `ZzScrollback::takeNewest` 取最新行（旧到新
    顺序、以值移交所有权），实取非空时历史代计数递增。

takeNewest 的统计语义：不是容量裁剪，totalDropped 不变；totalAppended
只增不改，绝对行号因此产生回退空洞（与 Contour rotateBuffersRight 的
stableBase 回退同构），选区锚点按不透明行号处理。

## 6. 前端的感知路径

resize 后前端不需要知道重组细节：

-   渲染：`ZzRenderView` 永远反映当前屏幕网格（含重组后的新行布局与
    重算后的 wrapped 标记），全屏标脏由 reflow 自洽。
-   历史：`ZzHistoryView` 的行数与代计数（generation）在 reflow、
    append、takeNewest 后更新；前端持有快照时应以代计数判断失效。
-   跨缝逻辑行在读取侧天然成立：ZzLineSource 接缝规则与
    ZzSelectionText 的跨缝拼链即按 wrapped 链跨域拼接，与 resize 重组
    共用同一链模型。

## 7. 语义边界与已知限制

-   行尾空白不保留：链末尾完全默认空白格在重组时裁除（与 Contour/xterm
    一致）。
-   存量劈链不修复：M16b 之前的会话中已被劈开的跨缝链不做追溯修复。
-   分域 reflow 的 dangling 尾链终结语义仍存在于纯历史 API 直调路径
    （有测试钉住）；经 ZzTerminal resize 的正常路径由 M16b 归还机制
    接管，不受影响。
-   "列比内容窄时不换行、横向滚动查看"属于显示层职责（逻辑行只读视图
    + 前端横向视口），不在终端模型内；该方向已登记为 M17 候选，终端
    模型的网格宽度始终等于上报给应用的尺寸，语义不变。

## 8. 里程碑与规格索引

-   M4：列变 soft-wrap reflow 奠基（分域重组、先历史后屏幕）。
-   M8b：流式 reflow 器（历史路径峰值内存优化）。
-   M15：行变条件语义（裁光标下方、压历史、回抽）——
    `superpowers/specs/2026-09-30-m15-native-row-resize-design.md`。
-   M16：硬行语义翻转（硬行即普通链，取代 M4 硬行截断）——
    `superpowers/specs/2026-09-30-m16-hardline-reflow-design.md`。
-   M16b：跨缝链归还统一重组——
    `superpowers/specs/2026-09-30-m16b-seam-chain-reflow-design.md`。

Contour 基准对照：third_party/contour 的 Grid.cpp——growColumns /
shrinkColumns（统一流重组，列变基准）、shrinkLines / growLines（行变
基准）。
