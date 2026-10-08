# M16c 列变 reflow 历史顶补（屏幕内容贴底锚定）设计

- 日期：2026-10-08
- 前置：M15 行变条件语义（2026-09-30-m15-native-row-resize-design.md）；M16 硬行 reflow（2026-09-30-m16-hardline-reflow-design.md）；M16b 接缝链归还（2026-09-30-m16b-seam-chain-reflow-design.md）
- 分支：contour

## 1. 目的与范围

消除列变 reflow 屏幕内容收缩时「底部补空」导致的双重症状：

1. **纯列变拉宽**：折行链接回、屏幕物理行收缩，底部补空——历史滞留不回填，
   屏幕下方大片空白。contour 统一流模型（页=流尾 pageSize 行）与 xterm
   在此场景会把历史行顶补到屏幕上方、内容贴底；
2. **列行同变（本次事故，用户实测）**：一次 resize 调用内列变先行
   （ZzNativeBackend::resize 的列变分支先于行变分支），列变 reflow 底部
   补空把光标抬离底行，随后的 M15 扩行回抽条件「光标贴末行」
   （对齐 contour growLines，Grid.cpp:741）被前置破坏——扩行不回抽，
   历史永远滞留。用户实测现象：拖拽最大化后屏幕仅剩顶部数行内容与
   提示符，其余全空，历史完好（滚动可见）。

**复现证据（2026-10-08 三层递进诊断）**：Core 净 resize 四种序列全过；
widget 装配层（offscreen 合成输入拖拽）全过；全链路（真 bash + PTY
SIGWINCH + 长行折链 + 快速多步拖拽）复现——拉到 274x74 时屏幕仅剩
顶部 4 行内容与提示符、history=5 滞留，与用户截图一致。根因因此定位在
Core 分域模型的语义缺口，非 spike 装配层。

范围内：

- ZzScreen::reflow 重组出口的历史顶补（Primary 缓冲，经 HistoryPullCallback）
- 顶补后的光标平移与余量底部补空
- Core 单元测试（含本次事故的 Core 级复刻）
- 文档同步（Scrollback-and-Reflow.md、Screen.h reflow 注释）

范围外：

- 纯行扩且内容不满屏的回抽放宽（保持 contour growLines 条件，§2 定案 2A）
- Alternate 缓冲（无历史，维持底部补空）
- 存量会话状态追溯修复（同 M16b §1 声明）
- M17 不换行显示模式（显示层职责，与本规格正交）

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | 修复语义 | 列变 reflow 后内容不足 rows 时从最新历史顶补填满，内容贴底锚定——对齐 contour 统一流净效果，连带治愈 combined resize（1A） |
| 2 | 纯行扩不满屏 | 保持 contour 条件（光标贴末行才回抽），不放宽（2A） |
| 3 | 验证手段 | Core 单元测试为主，事故场景在 Core 层转正（3A），不动 spike 仓 |

## 3. 核心机制：reflow 出口历史顶补

ZzScreen::reflow 的 reflowBuffer 重组出口（现行为：产出不足 rows_ 时底部
补空行）插入顶补分支，仅 Primary 且 historyPullCallback_ 非空时生效：

1. **缺口计算**：重组产出的内容行数 n（不含补空行），缺口
   spare = rows_ - n；spare 为 0 时无顶补（现状路径不变）；
2. **顶补索取**：pulled = historyPullCallback_(spare)——经既有回调从
   最新历史取行（ZzScrollback::takeNewest，旧到新顺序、以值移交所有权）；
   实取非空时历史代计数经 backend 既有回调接线递增（M14 契约自洽）；
3. **顶部插入**：pulled 插入 Primary 顶部（复用 M16b prependPrimaryLines
   原语；本场景插入后行数不超过 rows_，不触发其溢出声明分支，余量由
   第 5 步补足），内容随之贴底；
4. **光标平移**：cursor.position.row += pulled 行数（链跟踪已在重组期
   完成，此处仅随插入数下移），clamp 看护不变；
5. **余量补空**：历史不足时 spare - pulled 部分仍底部补空。

**宽度不变量**：backend 协调顺序不变（M16b 接缝归还 → scrollback reflow
→ screen reflow），顶补取到的历史行已是新列宽，与屏幕行同宽。

**链边界**：顶补按行截取，可能把某条逻辑行链的头部留在历史——跨缝链是
架构内既有合法状态（ZzLineSource 接缝规则、ZzSelectionText 跨缝拼链），
选区/搜索/后续 reflow（M16b 归还）均已能处理。

**contour 对齐论证**：contour 列变重组后「页面 = 重组流尾部 pageSize 行」
（rotateBuffers 收账），内容贴底、历史自动入页——顶补使分域模型获得
同等净效果。contour 的 growLines 贴底条件（Grid.cpp:741）因统一流顶补
而天然成立；本修复后 native 的 M15 回抽条件在列行同变路径同等成立。

## 4. 语义边界与统计约定

- 顶补不是容量裁剪：totalDropped 不变；totalAppended 只增不改，绝对行号
  产生回退空洞（M15 takeNewest 同款语义，选区锚点按不透明行号处理）；
- 顶补仅 Primary；Alternate 永不顶补（无回调路径）；
- wrapPending 清除、全屏标脏由 reflow 既有行为自洽；
- 纯行扩且光标不贴末行时不回抽（contour growLines 同款条件）；
- 存量会话中已底部补空的状态不追溯修复，仅对新 resize 生效。

## 5. 测试计划（全部 Core 仓内）

新增单元测试（screen/backend 两层）：

1. **拉宽顶补**：有历史 + 屏幕含折链，列变拉宽后历史行顶补入屏、内容
   贴底、光标行号随顶补数下移、历史行数与代计数账正确；
2. **列行同变（事故复刻）**：不满屏 + 折链初态，单次 resize 列行同增，
   断言扩行回抽触发、屏幕填满、历史无滞留（M15 条件经顶补治愈）；
3. **窄↔宽往返**：缩列溢出压历史 → 拉宽顶补接回，往返后屏幕满、
   历史账守恒；
4. **历史不足**：顶补索取大于可用历史，实取部分顶补、余量底部补空；
5. **Alternate 不顶补**：备用屏列变收缩仍底部补空；
6. **跨缝链组合**：M16b 接缝归还与顶补同路径（接缝链归还后重组收缩
   再顶补），链完整性与行数账正确；
7. **无回调退化**：screen 单测层无 HistoryPullCallback 时维持底部补空
   （现有测试不回归）。

回归基线：linux-gcc-debug 55/55、m2-off-check 44/44（contour OFF）、
m2-shared-check 55/55、fuzz 3/3、doxygen 零告警。

## 6. 文档同步

- `docs/Scrollback-and-Reflow.md` 第 4 节：底部补空改写为顶补语义
  （含纯行扩边界声明不变）；
- `include/ZzTerm/Screen.h` 的 reflow 注释：重组溢出/补空段更新为
  顶补语义；
- `docs/API.md` 若涉 resize 行为描述则同步。
