# M17d 扩行回填（grow refill）+ ED3 清历史设计

- 日期：2026-10-09
- 前置：M17c 整行擦除斩链（2026-10-08-m17c-erase-chain-sever-design.md，tag m17c）
- 分支：contour

## 1. 目的与范围

消除 M17c 复验发现的「空白海」事故：窗口缩到极小再拉满后，屏幕内容
堆在顶部、底部大面积空白，历史区内容不被拉回，提示符悬在半空。

**事故链**（spike 留痕 `/tmp/spike-trace3.bin`（2026-10-09 13:43，
27 事件：feed 20 / resize 7）离线重放实证）：

1. 用户在长路径目录（62 字符提示符）下 `ll`，窗口从 271x75 缩到
   79x24 → 14x23（极小窗），再经 97x29 → 144x45 拉回 271x75；
2. 拉回 271x75 后屏幕只有 25 行非空堆在顶部，**50 行空白**横在提示符
   下方，历史区躺 74 行无人认领（重放终态：`非空=25/75 history=74
   cursor=(49,24)`，欠填报告 2 次）；
3. contour 参照后端跑同一剧本结果几乎相同（`非空=26/75 history=69`，
   欠填报告同为 2 次）——**非 M17c 回归，是既存语义缺口**，本次手势
   （缩至极小再拉满）将其彻底暴露。

**根因**（代码实证）：

1. **扩行回抽条件过苛**：`ZzScreen::resizeBuffer` 扩行分支仅在「光标
   恰好贴旧末行」（`cursor.row == oldRows - 1`）时才经
   HistoryPullCallback 回抽（Screen.cpp:89-90）。列变宽 reflow 把内容
   向上压实后光标停在内容末行（如 24/45），不再是缓冲末行，回抽拒发，
   底部直接补空；
2. **补空行永久化**：补进去的空行是真实行对象，后续 reflow 的逆差回抽
   判断（`out.size() < rows_`，Screen.cpp:189）把空行当内容计数，逆差
   永不成立，空白随每次拉大累积且不可逆（直到新输出把它顶走）。
3. 整个事故中历史计数的变化（78→76→74）全部是 reflow 合并窄窗折链
   所致，**回抽一次都没有真正发生**。

主流终端（Konsole、Windows Terminal、kitty）拉大时从历史回填把屏幕
填满、提示符沉底；这正是本项目「无论怎么缩拉内容都完整」的核心诉求。

范围内：

- native 后端扩行回填语义（Screen 扩行分支 + 折链对齐）
- ED 3（`ESC [ 3 J`）清滚动区接线（接口已存在，纯分发层补齐）
- Core 单元测试（事故复刻 + 回填规则单测）与 compat 偏离登记
- 文档同步（Architecture-v2 parity 登记、Screen.cpp M15 注释、
  Scrollback-and-Reflow.md、API.md）

范围外：

- contour 后端行为变更（第三方冻结；parity 偏离见 §6）
- 缩行语义（M15 裁光标下方/压历史不变）
- trace3 row 23 半截提示符残行（窄窗死代残骸，M17d 后复测再议）
- 光标停提示符中间：上游 readline 8.3 bug，终端侧不动（M17c §5 已登记）
- M17b 不换行显示模式与横向滚动条（后续里程碑，正交）
- spike 演示层改动

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | clear 与回填的关系 | 1A：顺带实现 ED3 清历史。`clear`（`ESC [ H` + `ESC [ 2 J` + `ESC [ 3 J`）后历史真正清空，拉大不回填，对齐 xterm/VTE/kitty |
| 2 | 实现路径 | 方案一：放宽扩行回抽条件 + ED3 清历史，改动集中于 Screen 扩行分支与 CSI 分发层 |
| 3 | 方案二（reflow 逆差分支把尾部空行视为可填充） | 剔除：YAGNI，且 ED2-only 清屏后纯拖宽就复活内容，语义危险 |
| 4 | 方案三（前端显示层虚拟回填） | 剔除：违背 RenderView 真实反映引擎状态的架构，选区/搜索会面对引擎与显示不一致 |

## 3. 核心机制：扩行回填

### 3.1 触发条件

`resizeBuffer` 扩行分支（rows > oldRows）回抽条件由

> 光标贴旧末行（`cursor.row == oldRows - 1`）

放宽为

> **Primary buffer 且光标下方所有行均为空行**（空行 = 整行空白 cell）。

旧条件是新条件的子集（贴末行时下方无行，全称量词真空成立），既有
行为天然兼容。

空行判定：ZzLine 暂无空白查询接口，计划阶段在 Screen 内部加行级空白
判定辅助（逐 cell 比对空白），不进公共 API。

### 3.2 回抽量与光标

满足触发条件时：经既有 HistoryPullCallback 回抽
`p = min(扩行数 k, 历史可用行数)` 行顶插屏幕顶部，
`cursor.row += p`（内容整体下沉，提示符沉底）；历史不足时余量底部
补空（现状语义不变）。

### 3.3 折链对齐

回抽须按折链边界对齐，**向下取整**（跨缝的整条链留在历史）：
设历史行数 H，初值 `n = min(k, H)`；当 `0 < n < H` 且历史第
`H - n - 1` 行（被取块上方的接缝行）`wrapped = true`（被取块从链
中段切开）时递减 `n`，直至接缝行 `wrapped = false`、`n == 0` 或
`n == H`（历史全取，无接缝问题）。

不向上多取的原因：顶插行数超过扩行数 k 时，resize 出口从缓冲区末尾
截断，光标下方空行不足吸纳超出量时会裁到活内容行，造成内容丢失。
向下取整的代价是至多（链长 - 1）行差额留在底部补空，后续 resize
或输出自然修复；链完整性优先。

对齐在 native 后端 HistoryPullCallback 接线层实现（Screen 只见回调
不见历史），对 M16c reflow 逆差顶补同生效——dangling 预防全域化。

保证不变量：被取块首行必为链头（顶插后屏幕首行永不 dangling）；
历史末行与屏幕首行之间既有的跨缝接续关系保持原样。与 M16b 跨缝摘除
同族但路径不同（M16b 作用于列变 reflow 之前，本规则作用于行变扩行
与 reflow 逆差回抽）。

### 3.4 不回抽的情形

- 光标下方存在非空行（光标在内容中间，如全屏应用/布局中）——一律
  不回抽，保护布局；
- Alternate buffer——维持 M15 尾部截断/补空，不动历史。

## 4. ED 3 清滚动区

xterm 标准语义：`ESC [ 3 J` 清空滚动区，**不动屏幕内容、不动光标**。

实现为纯接线（接口已就绪）：

- `NativeCsiDispatch` case 'J' 当前只受理 p ≤ 2（注释「ED 3 不在 M1
  范围，忽略」），增加 p == 3 分支调 `scrollback_->clear()`
  （ZzScrollback 纯虚已定义于 Scrollback.h，ChunkedScrollback 已实现：
  chunks/计数全复位）；
- `++historyGeneration_`（M14「不得漏增」同口径：HistoryView 可见
  内容变化必须计代）；
- 注释同步更新。

效果：`clear`（terminfo 序列含 ED3）后历史真正清空，拉大不回填，
clear 意图受尊重。

对 contour 是 parity 补齐而非偏离：contour 库原生支持 ED3 清滚动区。

## 5. 与既有机制的交互

- resize 顺序不变：先列变（M16b 跨缝摘除 + 历史 reflow + 屏幕
  reflow），后行变 resize（本机制在行变阶段）；
- M17a Preserve 仅作用于 reflow 阶段，M17c 斩链不动，与本机制正交；
- 回抽走既有 `historyPullCallback` → `takeNewest`，代计数递增已有
  （ZzNativeBackend.cpp:214-217），HistoryView/选区/搜索经代计数
  自洽；选区钉住在「扩行顶插」情形的坐标平移由计划阶段核查
  （M16b 钉住逻辑是否覆盖该路径）；
- **已知取舍（登记）**：应用只发 ED2 清屏（不带 ED3 的老式清屏）后
  拉大，历史内容会复活回填——kitty/Windows Terminal 行为相同，与
  主流一致，非缺陷。

## 6. parity 与偏离登记

- **扩行回填**：有意偏离 contour 核心（其 growLines 仅在光标贴末行
  时回抽），compat 登记 deviation（对齐 M17c 用例 25 先例）；
- **ED3 清滚动区**：parity 补齐，contour 后端天然正确，可加对照
  用例。

## 7. 测试

Core 单测（native 后端或 Screen 装回调层级）：

1. 光标贴旧末行扩行回抽——旧条件子集，回归保护；
2. 光标下方全空（非贴底）扩行回抽填满——新语义核心；
3. 历史不足：部分回抽 + 底部补空；
4. 光标下方有非空行 → 不回抽（布局保护）；
5. 折链对齐：历史末行是链中段时多取到链头，屏幕首行不 dangling；
6. ED3：清历史、屏幕与光标不动、代计数递增；clear 序列后扩行不
   回填；
7. Alternate 扩行不动历史。

compat：扩行回填 deviation 登记 + ED3 parity 对照。

**重放金标准**：`/tmp/spike-trace3.bin` 终态期望从「非空 25/75 +
欠填 2」变为「非空 ≥ 74/75、欠填报告清零、光标沉底附近」（历史 74
行足以覆盖 50 行缺口）。欠填不变量自此常驻重放器检查。重放器属 /tmp
诊断工具不入库，其不变量口径在计划中写明以便重建。

spike 复验手势：长路径目录 `ll` → 拖至极小窗 → 拉回最大化，预期屏幕
填满、提示符沉底。

fuzz 路径不受影响。

## 8. 文档同步

- Architecture-v2.md：parity 登记（扩行回填 deviation + ED3 补齐）；
- Screen.cpp:59-62 的 M15 注释（「光标贴末行才回抽」）更新为新语义；
- Scrollback-and-Reflow.md、API.md：ED3 语义与扩行回填行为记录。

## 勘误 E-1（2026-10-09 计划阶段）

§3.3 初版为「向上多取至链头」。计划阶段推演发现：顶插行数超过扩行数
k 时，resizeBuffer 出口 `lines.resize(rows)` 从缓冲区末尾截断，光标
下方空行不足吸纳超出量时会裁掉活内容行（内容丢失）。改为**向下取整
到链边界**，并明确对齐实现位置在 native 后端 HistoryPullCallback
接线层（对 M16c reflow 逆差顶补同生效）。
