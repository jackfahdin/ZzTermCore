# M21 设计：contour 后端历史两件落地 + 两件登记

> 日期：2026-10-19 · 分支：contour · 前置：contour pin 升级 6777ff05 → 01f68347（04edd8a，已落地）

## 背景与目标

Z3 收官登记（`docs/Scrollback-and-Reflow.md` 第 10 节）裁定：启用 contour 后端须一并落地
四件套——`primaryLineCount()`、`clearCount()`、`resizeScreenScrollout()` 覆写与
`prependHistoryLines()` 支持，缺一则滚动/读回语义残缺。

M21 摸底（2026-10-19）确认四件套可行性严重不均：

- contour `Terminal::primaryScreen()` 是 public，主屏历史 Alternate 期间保留——`primaryLineCount` 纯读路径可行；
- adapter `resize` 已捕获 `historyBefore`——`resizeScreenScrollout` 可做变体口径；
- contour ED3 清史（`Terminal::scrollbackBufferCleared()`）不转发 Events 监听器，且清的是
  currentScreen（Alternate 期间清备屏，与 xterm/native「恒清 Primary」分歧）——`clearCount` 无侦测手段；
- contour Grid 无最旧端注入 API，亦无 ZzLine→contour Line 反向转换——`prependHistoryLines` 不可行。

上游 master（01f68347）核对：两个缺口上游均未补（`scrollbackBufferCleared`/`rowsDropped`
仍为 Terminal 内部方法；Grid 仅有反向的 `dropRowsAbove`）。

**决议（用户裁定方案 A）**：两件落地 + 两件登记为上游能力缺口。零 patch 红线不动——
`docs/Architecture-v2.md` §5「原则上禁止长期维护 Contour 私有 patch」。

## 范围

只动 ZzTermCore。ZzClawTerm 继续写死 `ZzBackendKind::Native`
（`ZzCoreTerminalWidget.cpp:65`），不做切换开关，不动产品。

## 设计

### 1. `primaryLineCount()` 覆写（落地）

- `ZzContourBackend` 新增 `primaryHistoryLineCount()`：
  `terminal->primaryScreen().historyLineCount()`（Alternate 期间仍读主屏 Grid）。
- `ZzContourHistoryView` 覆写 `primaryLineCount()` 调它。
- 语义与 native 一致：buffer 无关口径，Alternate 期间不归零。

### 2. `resizeScreenScrollout()` 覆写（落地，contour 变体口径）

- 落点 `ZzContourBackendAdapter::resize`：已有 `historyBefore`，补 `historyAfter`，
  返回 `max(0, 主屏 historyAfter − historyBefore)`。
- 口径定义（写入 `docs/API.md` M21 条目）：**resize 前后主屏历史行数净增量，负值归零**。
  - 行变收缩迁移：与 native 净迁移口径等价；
  - 列变 reflow：contour 重建行身份，差值仅为近似——登记为已知偏离；
  - 扩行回抽为负：归零，调用方（桥）走收缩向兜底，行为安全（与默认回退 0 的既有调用语义一致）。

### 3. `clearCount()` / `prependHistoryLines()`（登记，不实现）

- 代码不动，维持默认回退（0 / false）。
- `docs/API.md` M21 条目登记缺口与解封条件：
  - `clearCount`：待 contour 上游把清史/rowsDropped 接进 Events（另需对齐 ED3 清 Primary 语义）；
  - `prependHistoryLines`：待 contour 上游 Grid 提供最旧端注入 API。
- `docs/Scrollback-and-Reflow.md` 第 10 节启用清单更新为「两落地两缺口」状态。
- 「是否向 contour 上游提 PR」记录为独立后续事项，不占 M21。

### 4. 测试

- 沿用 `test_historyview_compat.cpp` 手工双后端对拍范式（不引入 gtest 参数化）：
  - `primaryLineCount`：Alternate 期间 contour 与 native 一致返回主屏历史行数（参照
    `test_historyview.cpp::testAlternateZero` 补 contour 侧）；
  - `resizeScreenScrollout`：行变收缩迁移计数双后端对拍；列变场景只断言非负、不崩
    （口径本就不同，数值不对拍）；
- 全量 ctest：CONTOUR=ON 基线 63/63 + 新增；CONTOUR=OFF 52/52 不受影响。

### 5. 版本与 ABI

- 公共头无签名变化；覆写发生在后端内部，属行为补全。`docs/API.md` 按惯例加 M21 条目。
- 完成后打 tag `m21`。

## 明确不做

- 不改 `third_party/contour` 任何字节（零 patch 红线）；
- 不动 ZzClawTerm；不做后端运行时切换；
- 不改 `clearCount`/`prependHistoryLines` 代码；
- 不消费上游新增的 `rowsDropped`/`dropRowsAbove`（留作后续 contour 侧计数对齐的抓手，
  已在升级报告备注）。

## 风险

- 列变 reflow 下 `resizeScreenScrollout` 为近似值：桥侧若在列变后依赖精确计数对账，
  可能出现收缩向兜底路径走得比 native 多——可接受，行为安全方向（宁可多兜底不可错位）。
- 两件缺口使 contour 后端仍不可用于产品：这是方案 A 的既定代价，产品不受影响。
