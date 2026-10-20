# OSC 8 可点击超链接 — 设计规格

- 日期：2026-10-20
- 状态：已批准（brainstorming 流程完成，方案一选定）
- 涉及仓库：ZzTermCore（contour 分支，引擎层主体）、ZzClawTerm（master，前端）
- 里程碑：M22a（引擎+契约）、M22b（前端）
- Checklist 登记位：`docs/VT-Xterm-Checklist-v2.md:140`

## 1. 需求定稿（澄清结论）

| 项 | 结论 |
|---|---|
| 鼠标交互 | Ctrl+悬停显示下划线+手型光标；悬停 tooltip 显示完整 URI；Ctrl+左键点击打开 |
| 设置开关 | 要，ZzAppSettings 加 `hyperlinkEnabled`，默认启用 |
| `id=` 跨行关联 | 做，悬停任一段，全段一起高亮 |
| 复制选区带链接 | 不带，纯文本复制（YAGNI） |
| 双后端 | 都做：native 为主体实现，contour adapter 纯增量提取 |

## 2. 协议解析（native 引擎）

- OSC 8 格式：`ESC ] 8 ; params ; URI ST`。URI 为空串 = 关闭当前链接（序列成对出现）。
- 入口：`ZzNativeBackend::dispatchOsc`（`src/backend/native/ZzNativeBackend.cpp:586-597`）加 `"8"` 分支。OSC 通道现成：`src/parser/VtParser.cpp:343-375` OscString 状态 → sink `onOscDispatch(payload)`（`include/ZzTerm/Parser.h:194-201`）。
- params 解析：`key=value` 以 `:` 分隔，只认 `id`，其余键忽略。参数解析参考 contour `Screen.cpp:4604`。
- 解析结果写入后端当前超链接状态 `{uri, id}`，后续 print 落格继承该状态，直到下一个 OSC 8 改变或关闭。
- URI 校验：引擎层只拒绝含控制字符的 URI；scheme 白名单在前端打开时校验（引擎不解释 URI 语义）。

## 3. 存储模型（native 引擎，方案一：贴 contour 模型）

红线：`sizeof(ZzCell)==12` static_assert 钉死，不内联任何新字段到 cell；M8b 已裁定 hyperlink 走 `ZzCell::text_` bit26 存在位 + 稀疏侧表（`include/ZzTerm/Cell.h:317,332-333`）。

- `ZzScreen` 持有 `ZzHyperlinkPool`：LRU 池，容量 1024 条（对齐 contour 上游 `Hyperlink.hpp`）。条目 `{uri, id}`；`intern(uri, id) -> uint16_t hyperlinkId`，0 保留为"无链接"。
- `ZzLine` 加稀疏侧表 `std::unique_ptr<uint16_t[]> hyperlinkIds_`，仅当本行出现链接时按行宽分配。
- 落格：在 `internClusterAt` 同款位置（`src/screen/Screen.cpp:274-280`）写侧表 hyperlinkId 并对 cell 置 bit26；宽字符续格同样置位。
- 清空路径（EL/ED/DECALN 等）：cell 重置自然清 bit26；侧表不主动清，渲染以 bit26 为准。
- cluster interning（`include/ZzTerm/Line.h:110-116`）为现成范式，池实现照抄其 intern/引用语义。

## 4. `id=` 跨行关联

- 池 intern 命中规则：`(uri, id)` 二元组相同才复用 hyperlinkId。因此同 `(uri,id)` 的所有段天然同 hyperlinkId。
- 关联键 = hyperlinkId 本身。前端悬停某段时，对可见行内所有相同 hyperlinkId 的 cell 一起高亮。

## 5. Reflow（M8a 软折叠）适配

`src/screen/Reflow.cpp` 三处，照抄 cluster 搬运范式（`:80-83`）：

1. 行合并/拆分时 hyperlinkId 侧表随格搬运、重新拼接。
2. `zzIsPlainBlank`（`:9-13`）裁尾判据：bit26 置位的 cell 算内容，不裁尾。
3. 宽字符续格（`:87-91`）带 hyperlink 标记。

池是终端级，hyperlinkId 在 reflow 后稳定不变。

## 6. 渲染契约（ABI minor 新增）

- `ZzCellView` 尾部追加 `hyperlinkId` 字段（M18 先例，minor 兼容）。
- `ZzRenderView` 新增带默认实现的虚方法：
  - `hyperlinkUri(hyperlinkId)` → URI 字符串，默认空 = 无链接；
  - （tooltip 直接用 `hyperlinkUri` 的返回值，不另设方法。）
- 需改 5 处 view 实现：native `ZzNativeRenderView` / `ZzNativeHistoryView`、contour `ZzContourRenderView` / `ZzContourHistoryView`、`ZzUnwrapView`（透传）。

## 7. contour adapter（纯提取，零 patch 红线）

- `zzSnapshotContourLine`（`src/backend/contour/ZzContourBackend.cpp:61-112`）：从 `cell.hyperlink()`（上游 `CellProxy.hpp:109`）取 HyperlinkId，填入 `ZzCellView.hyperlinkId`。
- `ZzContourRenderView.cpp:75-98` 实现 `hyperlinkUri`：查上游 HyperlinkStorage（全局 LRU 1024 条）；LRU 已淘汰的 id 返回空 = 无链接。
- 关联键复用上游 HyperlinkId，`id=` 关联天然成立。
- 不改 vendored contour 任何字节。

## 8. 前端 ZzClawTerm（`src/terminal/ZzCoreTerminalWidget.cpp`）

- 开启 `mouseTracking`。
- `mouseMoveEvent`（现 `:800-827`，当前无 hover 逻辑）：Ctrl 按住时经 `logicalPosAtPixel`（`:763-785`）命中 `hyperlinkId != 0` 的 cell → `setCursor(Qt::PointingHandCursor)`，记录 hover 状态（hyperlinkId + 段范围）触发重绘，并 `QToolTip::showText` 显示完整 URI。
- 绘制（paintEvent `:224` 起，paintWrapped `:236-309` / paintUnwrapped `:311-376` 两遍制）：
  - 链接 cell 在 Ctrl 按住时画下划线（当前前端完全不渲染 attributes，下划线为新插入逻辑）；
  - hover 时对同 hyperlinkId 的所有段画高亮叠加层，仿 `paintSelectionOnWrappedRow`（`:382-415`）。
- `mousePressEvent`（`:787-798`）：Ctrl+左键命中链接 → scheme 白名单（http/https/ftp/ssh/mailto/file）→ `QDesktopServices::openUrl`（先例 `ZzAppShell.cpp:464`）；不命中走原选区逻辑，左键选区行为不受影响。
- 设置开关 `hyperlinkEnabled`（ZzAppSettings，模板参照 `x11ServerEnabled`）：关闭时前端完全跳过 hyperlink 查询；引擎照常解析，零额外成本。
- SSH 会话同样生效（链接由远端程序输出，前端只负责打开本地浏览器）。

## 9. 测试策略（TDD）

引擎层（ZzTermCore，既有 gtest 范式）：

1. OSC 8 解析：成对开关、params 变体（id、未知键忽略、多键）、空 URI 关闭、控制字符拒绝。
2. 池：intern 命中/未命中、LRU 淘汰、(uri,id) 二元组区分。
3. 落格：bit26 置位、宽字符续格置位、EL/ED 清除。
4. Reflow：软折叠行 resize 后链接保留且 id 稳定、bit26 行不裁尾。
5. `id=` 关联：同 id 不同段同 hyperlinkId，不同 id 同 uri 不同 hyperlinkId。
6. 契约快照：5 处 view 实现的 hyperlinkId/hyperlinkUri 输出。
7. contour adapter：含链接的快照提取、LRU 淘汰后降级为空。

前端：手测进 `ZzClawTerm/docs/acceptance/`（新增 v0.5 节：Ctrl 悬停下划线+tooltip、Ctrl+点击打开、id 跨行高亮、开关关闭后无响应、SSH 会话链接）。UI 自动化不做，沿袭现状。

## 10. ABI 与里程碑

- 全程 minor 新增：`ZzCellView` 尾部字段、RenderView 默认实现虚方法、无签名变更，无需 ABI break。
- **M22a**：native 解析/存储/reflow + RenderView 契约 + contour adapter，单测全绿。
- **M22b**：ZzCoreTerminalWidget 交互 + tooltip + 开关 + 验收清单更新。
- 落地后勾掉 `docs/VT-Xterm-Checklist-v2.md:140` 登记位。

## 11. 非目标

- 复制选区携带链接（HTML 剪贴板格式）。
- 引擎层 scheme 白名单。
- attributes 全面渲染（bold/italic 等）——本里程碑只加链接下划线。
- 上游 contour PR（M21 已登记的独立后续事项）。
