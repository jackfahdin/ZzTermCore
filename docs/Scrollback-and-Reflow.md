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
-   逐行裁尾（M17a-4b）：wrapped 链各行尾部的完全默认空白格恒为填充
    而非内容（autowrap 只在行满触发、reflow 拆分各行恒满），重组时逐行
    裁除后顺接成链流——链末行裁尾即旧 trimEnd 语义；码位 0x20 的真空格
    不受影响。
-   宽字符（占两列的单元格）原子搬运，不落在行边界上（边界前移一格并
    补默认空白）。
-   grapheme cluster 格在新行重新 internCluster。
-   屏幕重组产出不足 rows 时先经 HistoryPullCallback 从最新历史顶补
    （M16c：仅 Primary 且装有回调时触发，对齐 Contour 统一流尾部窗口
    的净效果），历史不足余量底部补空；Alternate 永不顶补。

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
    自洽；重组产出不足 rows 时先经 HistoryPullCallback 从已重组的最新
    历史顶补插入屏幕顶部（M16c：内容贴底锚定、光标随顶补数平移，
    顶补行已是新列宽——历史先完成重组），历史不足余量底部补空行。

历史代计数（M14）随之递增：历史 reflow 计一次，屏幕回流 append 经
ScrollOutCallback 另计，保证 ZzHistoryView 持有方能感知失效。

Alternate 缓冲区无历史：重组溢出行直接丢弃、产出不足纯底部补空
（永不顶补），不参与上述往返。

### 光标活动链保护（M17a）

扩列 reflow 收链时，**Primary 缓冲中光标所在的折链豁免合并**，保持旧宽度
拆分（行存储扩宽到新列宽，内容布局、wrapped 旗标、光标行位均不动）。

动机：readline 的 WINCH 重绘按**旧布局帧**发相对擦除（ESC [ A、ESC [ K
连发 N 次）。若 reflow 已把输入链收链为 1 行，擦除会命中收链后无辜的
内容行，造成永久性内容破坏（2026-10-08 用户实测，spike 留痕重放定位）。
豁免后擦除命中提示符自己的碎片行，内容零损失。contour/xterm/VTE 等
reflow 终端均有此破坏（contour 已用留痕逐点实测确认），本语义为有意
差异化。

细则：仅扩列方向（缩列拆分照常）、仅 Primary（Alternate 不豁免）；
豁免是瞬时态——应用重写该链（readline 重印提示符）后链消失，后续
reflow 无豁免对象；流式输出（cat）在链末片段旧列位继续追加，内容正确，
视觉折点待下次重写消除。

### 整行擦除斩链（M17c）

整行擦除的语义是**内容死亡**：被擦行从折链上摘除——本行 wrapped 置
false（斩断出链，不再续接下一行），前驱行 wrapped 置 false（斩断入链，
前驱不再续接到本行）。前驱已滚入历史区时（擦屏幕首行），ZzScreen 经
SeverSeamLinkCallback 通知持有方，由 ZzScrollback::severNewestWrapped
跨界斩断历史末行链标（仅 Primary；Alternate 无历史不触发）。

触发集合（首版）：

-   EL 整行覆盖：ESC [ 0 K 且擦除起点为列 0（典型形态 CR + ESC [ K）、
    ESC [ 2 K（整行擦除，任意光标列）、ESC [ 1 K 且光标在末列；
-   ED 覆盖到的整行：ESC [ 0 J 下方全部整行（光标行仅当光标在列 0 时
    算整行覆盖）、ESC [ 1 J 上方全部整行（光标行仅当光标在末列时算）、
    ESC [ 2 J 全屏整行。ESC [ 3 J 清滚动区（M17d 起接线清空历史，
    见 §9），不触及屏幕行链标，不在触发集合。

不斩链：行尾/行首部分擦除（视为对活内容的编辑，链标不动）、覆盖写
（无擦除直接改写单元格）、ECH / DL / IL（首版豁免，记录在案）。

动机：M17a 复验事故——窄窗期间 bash 每次 WINCH 重绘都擦除并重写
提示符行，多代提示符尸体因擦除不动链标而粘连成一条僵尸折链，
Preserve 把整条僵尸链豁免收链，拉回后死代残尸永久留屏（spike 留痕
实证）。斩链后 Preserve 只保护活代：bash 帧擦除精确命中活代行数，
屏幕零残片、内容零损失；死代残骸按普通规则收链成短行留在滚动区
（方案 A 形态），选区拼链跨代自然断开。

降级边界：斩链只动 wrapped 旗标，不删行、不动内容；即使误斩（exotic
应用对活链整行擦除再续写），后果仅为该逻辑行在 reflow/选区拼链中按
硬行处理，不产生内容破坏。与 contour 后端在「erase 触及 wrapped 行」
场景有意偏离（contour 保持链标，偏离登记见 API.md 版本节）。

### 已知外部问题：readline 8.3 光标错位（终端侧不处理）

M17a 复验中用户同时报告「光标停在提示符中间」，诊断为上游 readline
8.3 已知 bug，与终端渲染无关（无终端 PTY 实验逐字节复刻现场，字节流
纯由 bash 算出）：

-   触发条件：终端变宽时提示符从折行变为单行，且提示符含 2 段以上
    隐形字符（颜色码）；readline 把光标放偏左，错位量 = 隐形段字节数；
-   上游状态：bug-readline 2026-08-10 报告（bash 5.3.9 实测复现），
    维护者确认 devel 分支已修复（commit 1e9f5e10b2），已发布补丁
    （8.3-p003 及之前）未带该修复；
-   自愈方式：按回车换新提示符即恢复；C-l 无效，打字会视觉覆盖提示符
    尾部（逻辑无损）；根治等发行版更新 bash/readline；
-   终端侧结论：xterm/konsole/contour 对同字节流渲染结果一致，无忠实
    修复手段，Core 与 spike 均不动。

## 5. 行变 resize 的条件语义（M15）

行数变化不走 reflow，由 `ZzScreen::resize` 按条件语义搬行（对齐 Contour
Grid 的 shrinkLines/growLines）：

-   缩行：先裁光标下方的行（直接丢弃，不入历史）；不够裁时把 Primary
    顶部行经 ScrollOutCallback 压入历史（无回调则丢弃，同 reflow 溢出
    语义）；光标随内容平移。
-   扩行（M17d 起）：Primary 且光标下方所有行为空行（会话活在底部）
    时，经 HistoryPullCallback 从最新历史回抽行注入屏幕顶部；不足
    部分底部补空行。光标下方有非空行时纯底部补空，不动历史。详见
    §9「扩行回填（M17d）」。
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

-   行尾空白不保留：wrapped 链各行尾部完全默认空白格在重组时逐行裁除
    （M17a-4b 逐行裁尾，与 Contour/xterm 的链尾裁除语义兼容——填充格
    均非内容）。
-   列变 reflow 的底部补空仅余量路径（M16c）：屏幕重组产出不足时优先
    从最新历史顶补，历史耗尽后的余量才底部补空；无 HistoryPullCallback
    的路径（Alternate、纯 ZzScreen 直调未装回调）退化为纯底部补空，
    M4 语义不变。
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
-   M16c：列变 reflow 历史顶补（屏幕内容贴底锚定、光标随顶补平移）——
    `superpowers/specs/2026-10-08-m16c-reflow-topfill-design.md`。
-   M17a：光标活动链保护（扩列 reflow 时 Primary 光标所在折链豁免收链，
    readline 陈旧帧擦除兼容；wrapped 行尾部空白格逐行裁尾）——
    `superpowers/specs/2026-10-08-m17a-active-chain-guard-design.md`。
-   M17c：整行擦除斩链（EL/ED 整行覆盖斩断 wrapped 链标，跨界斩链经
    SeverSeamLinkCallback 与 severNewestWrapped；readline 8.3 光标错位
    登记为已知外部问题）——
    `superpowers/specs/2026-10-08-m17c-erase-chain-sever-design.md`。
-   M17d：扩行回填（扩行回抽条件放宽为 Primary 且光标下方全空行即
    回抽，折链对齐向下取整；ED 3 清滚动区接线）——
    `superpowers/specs/2026-10-09-m17d-grow-refill-design.md`。

Contour 基准对照：third_party/contour 的 Grid.cpp——growColumns /
shrinkColumns（统一流重组，列变基准）、shrinkLines / growLines（行变
基准）。

## 9. 扩行回填（M17d）

M17c 复验暴露「空白海」事故：窗口缩至极小再拉满后内容堆顶、底部大面积
空白、提示符悬空（spike trace3 留痕离线重放实证，contour 参照后端跑同一
剧本同现欠填——既存语义缺口，非 M17c 回归）。M17d 放宽扩行回抽条件并
补齐 ED3 清历史。

-   触发条件：Primary 缓冲且**光标下方所有行均为空行**（空行 = 整行
    空白 cell）时回抽。旧条件「光标贴旧末行」是新条件的子集（贴末行
    时下方无行，全称量词真空成立），既有行为天然兼容；光标下方存在
    非空行（光标在内容中间，如全屏应用/布局中）一律不回抽，保护布局；
    Alternate 维持 M15 尾部截断/补空，不动历史。
-   回抽量与光标：满足触发条件时经既有 HistoryPullCallback 回抽
    `p = min(扩行数 k, 历史可用行数)` 行顶插屏幕顶部，
    `cursor.row += p`（内容整体下沉，提示符沉底）；历史不足时余量
    底部补空（M15 语义不变）。
-   折链对齐向下取整：回抽按折链边界对齐——被取块上方的接缝行
    wrapped 时递减取量，直至接缝行非 wrapped、取量归零或历史全取
    （跨缝的整条链留在历史）。不向上多取的理由：顶插行数超过扩行数
    k 时，resize 出口从缓冲区末尾截断，光标下方空行不足吸纳超出量时
    会裁掉活内容行（内容丢失）；向下取整的代价是至多（链长 - 1）行
    差额留在底部补空，后续 resize 或输出自然修复，链完整性优先。
    对齐在 native 后端 HistoryPullCallback 接线层实现（Screen 只见
    回调不见历史），对 M16c reflow 逆差顶补同生效——dangling 预防
    全域化；contour 的 LogicalLines 天然链对齐，此为向 contour 靠拢
    的加固，非偏离。
-   ED 3 清滚动区：`ESC [ 3 J` 清空历史，xterm 标准语义——**不动
    屏幕内容、不动光标**（此前分发层忽略；`ZzScrollback::clear()`
    既有接口首次接线，历史代计数递增）。`clear`（terminfo 序列含
    ED3）后历史真正清空，拉大不回填，clear 意图受尊重。
-   已知取舍：应用只发 ED2 清屏（不带 ED3 的老式清屏）后拉大，历史
    内容会复活回填——kitty/Windows Terminal 行为相同，与主流一致，
    非缺陷。

与 contour 后端的 parity：扩行回填为有意偏离（contour growLines 仅
光标贴末行回抽，偏离登记见 tests/unit/test_backend_compat.cpp 用例
26 testGrowRefillDeviation）；ED3 为 parity 补齐（contour 原生支持，
对照见用例 27 testEd3ClearScrollbackParity）。
