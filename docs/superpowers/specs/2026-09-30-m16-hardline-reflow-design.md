# M16 reflow 硬行语义对齐（缩列多行化 / 拉大接回）设计

- 日期：2026-09-30
- 前置：M4 resize reflow（2026-09-20-m4-resize-reflow-design.md，本规格取代其硬行条款）；M15 行变语义对齐；M13 spike 现象的列向根因
- 分支：contour

## 1. 目的与范围

消除 native reflow 的硬行内容丢失：现行语义「硬行（未 wrapped 的单行链）截断/补空，永不多行化」（src/screen/Reflow.h:27）使缩列时硬行尾部内容永久丢失、拉大不可恢复（用户实测：多栏列表拖窄后断在词中间、拉回不恢复）。contour 与 xterm 的行为是缩列软折行、拉大接回，内容完整。

**取代声明**：M4 规格 :60 的「硬行不参与合并：缩窄截断、变宽补空白（与 Konsole/Contour reflow 语义对齐）」条款由本规格取代——调研证实该对齐主张是事实性错误（contour 的 shrinkColumns 把硬行折成 wrapped 链，fork Grid.cpp:1084/1039-1104），且 M4 规格 :90/:126 本就预留了「硬行处理为可钉住 b 类分歧」的退路。

范围内：

- zzReflowChain 删除硬行特判：硬行 = chainLen==1 普通链，缩列多行化、拉大合并恢复
- 受影响测试重钉与 M5a 避让测试恢复
- compat 新增硬行缩列对照用例（b 类分歧转正为对齐）
- 文档改写（Reflow.h、API.md、VT-Xterm-Checklist 两份）

范围外：

- **M17 不换行显示模式**（横向滚动条）——§7 只登记方向，不实现
- per-line Wrappable 标志（contour 的 setWrappable 概念）——不引入（定案：用户要的是显示层全局切换，非逐行钉死）
- 行尾完全默认空白格的 trim——维持既有行为（定案：与 contour/xterm 一致，见 §3）
- contour fork、ZzPty 两项、IME——均不动

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | per-line Wrappable 标志 | 不引入（YAGNI；不换行需求由显示层全局模式承载，见 §7） |
| 2 | 行尾空白 trim | 维持既有（链尾完全默认空白格裁除，与 contour/xterm 一致；文本内容完整） |
| 3 | 不换行显示模式 | 拆分 M17：显示层全局运行时开关，Core 模型与 winsize 语义不变（§7 登记） |
| 4 | M5a 避让测试 | 改回 resize(4,3) 原始断言（语义修正后它是最有力的回归证据） |

## 3. 语义定义

- **缩列**：硬行内容超新列宽时多行化——按新列宽重切为 wrapped 链（除末行外 wrapped=true），宽字符原子搬运不落边界（边界前移一格补默认空白，与软折行链同一规则），cluster 格在新行重新 internCluster。
- **拉大**：多行化产生的链沿 wrapped 标志合并重切，末行恢复 wrapped=false——硬行往返内容完整恢复。
- **既有有损点（维持）**：链尾完全默认空白格在重切时被裁除（trimEnd，M4 已接受，contour trimBlankRight 与 xterm 同样裁尾空白）。文本内容不受损。
- **副作用声明**：缩列多行化使物理行数增加，屏幕溢出经既有 scrollOutCallback 上移进历史（reflowBuffer 路径通用），历史超容量从最旧端裁的语义不变；历史与屏幕分域 reflow 的接缝断链固有边界（Scrollback.h:90-92）不变。

## 4. 实现面

核心改动仅 src/screen/Reflow.cpp 三处（调研已验证其余路径全部天然兼容）：

1. 删除 isHardLine 判定（:21）及其两个分支：截断限宽（:47-49，limit 不再对硬行取 min(trimEnd, newCols)）与边界宽字符整体丢弃（:71-76 的 isHardLine break）；
2. 光标跟踪（ZzReflowCursor）零改动——硬行多行化后偏移落在真实物理位置（替代现行 :107-111 的 clamp 兜底），语义更准；
3. zzReflowLines 链划分、ZzReflowStreamer 流式器、ZzScreen::reflowBuffer 溢出路径、ZzChunkedScrollback::reflow 流式、M15 takeNewest/HistoryPullCallback——零改动。

ZzLine 无双标志需求：硬行多行化后与既有软折行链同构（单 wrapped 标志），拉大合并重切自然恢复。

## 5. 测试

### 5.1 重钉（直接断言截断语义的 5 处 + 1 注释）

- tests/unit/test_reflow.cpp 用例 2（testNarrowTruncatesHardLine，:62-71）：80 列硬行→40 列，断言从「1 行截断」改为「2 行链、首行 wrapped、内容完整 80 字符」；用例 11（testHardLineWideCharAtBoundary，:216-239）：10→9 列边界宽字符从「整体丢弃」改为「前移落第 2 行、内容完整」
- tests/unit/test_scrollback.cpp 用例 3（:82-89）：tail 4→2 从截断 "ta" 改为 ta/il 两行链、行数 5→6；用例 8（:219-238）：8→4→8 往返从「不恢复」改为「恢复 5 字符」，期望生成器重写；用例 9（:294-305）：300 硬行 8→2 从截断改为多行化链
- tests/unit/test_native_reflow.cpp:45-47：注释更新 + 补整宽硬行的往返断言

### 5.2 恢复（语义修正的回归证据）

- tests/unit/test_terminal_selection.cpp:68-82（testResizeReflowKeepsSelection）：改回 resize(4,3) 原始断言（M5a 计划 :1908 记载的被迫避让随本里程碑失效）

### 5.3 compat 转正

- tests/unit/test_backend_compat.cpp 新增硬行缩列对照用例：双后端喂同一硬长行，缩列后逻辑行内容逐行一致、拉大恢复一致；M4 b 类分歧注释更新为该分歧已消灭

### 5.4 不受影响（调研已核实）

test_reflow 其余用例、test_screen_reflow 全部、compat 用例 18/19（长行是 feed 时已软折的链）、selection/search 系、historyview 系、rowresize 系、perf 门控、bench、fuzz harness。

### 5.5 基线

linux-gcc-debug / m2-off-check / m2-shared-check 全绿，fuzz 3/3，doxygen 零警告。

## 6. 文档改写

- src/screen/Reflow.h:27：语义注释重写为多行化语义（本规格 §3 同口径）
- docs/API.md:91：「硬行截断/补空」改述为多行化；:220-223 的 M5b「截断行 col 快照」条款删除（M16 后不存在截断行）并在版本节补 M16 条目（行为语义变化，无 API 签名变化）
- docs/VT-Xterm-Checklist.md:118：Hard-newline preservation 按 xterm 真实语义重新表述（硬行软折行、拉大接回）；VT-Xterm-Checklist-v2.md:181-182 含义同步更新
- M4/M5a/M8b 历史规格与计划不回写，本规格 §1 的取代声明即为记录

## 7. M17 方向登记（只记录，不实现）

用户的根本需求（从 ZzTermWidget 换引擎的原因）：**两种显示模式共存且内容完整**——

1. 自动换行模式：列比内容窄时软折行（M16 完成后的默认行为，与 contour/xterm 一致）；
2. 不换行模式：列比内容窄时内容保持原始宽度不动，横向滚动条查看右侧部分。

架构定案：**不换行是显示层模式，Core 终端模型与 winsize 语义不变**（网格宽度必须等于上报应用的尺寸，否则 vim/bash 行为全乱）。实现拆分：

- Core 侧（候选 M17a）：逻辑行只读视图契约——沿 wrapped 链拼接的单点实现（复用 src/terminal/ZzSelectionText 的拼链逻辑），供前端免自建拼链；
- 前端侧（先在 spike 演示层落地，后随特性对齐进 ZzClawTerm）：模式运行时切换 + 横向滚动条（虚拟行 = 逻辑行、横向偏移视口、两种模式共享同一份完整内容、切换零代价）。

前置关系：M16 的内容完整性是 M17 拼链显示的地基。不引入 per-line Wrappable 标志；行尾空白 trim 维持既有。

## 8. 对路线的输入

- M13 spike 现象 5/7 的列向根因随本里程碑根治（行向根因 M15 已根治），M13 记录 §4 现象 5 标注更新
- M4 b 类分歧（硬行处理）消灭；VT-Xterm 对齐面推进一条
- 特性对齐阶段补充一条用户核心诉求：不换行显示模式 + 横向滚动条（§7），优先级待与 P1 队列（ZzPty 两项、IME）一并排序
