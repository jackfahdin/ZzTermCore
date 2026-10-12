# M19 历史最旧端注入原语设计（prependHistoryLines）

- 日期：2026-10-12
- 仓库：ZzTermCore（contour）
- 触发：ZzClawTerm Z2 任务 5（历史读回注入桥）前置门槛——E-1 勘误
  确认既有 `prependPrimaryLines` 语义为「屏幕顶部插入、溢出裁回
  历史最新端」（M16b 接缝归还专用），用于读回会顺序倒置；
  ZzScrollback 无最旧端注入公共 API。验收标准由 ZzClawTerm Z2
  计划任务 5 前置门槛给定（用户已批准）。

## 1. 目的与范围

给应用侧提供「向 Primary 历史**最旧端**注入行」的原语，供
ZzClawTerm 读回桥把 LogEngine 冷层历史注入显示层（滚动越顶
加载更早内容）。

范围内：ZzScrollback 新纯虚 `prependOldest` + ChunkedScrollback
实现 + 统计字段；ZzTerminalBackend 默认不支持（返回 false）+
ZzNativeBackend 实现；ZzTerminal 公共方法 `prependHistoryLines`；
HistoryView/LineSource 绝对行号基线记账调整；单测；API.md。

范围外：contour 后端的注入支持（默认 false）；注入行的样式
保留（调用方供行，样式由调用方决定）；ZzClawTerm 侧桥逻辑
（Z2 任务 5 自身）。

## 2. 语义定案

1. `ZzTerminal::prependHistoryLines(std::vector<ZzLine> lines)`
   ——向 Primary 历史最旧端注入；Alternate 屏活动期间拒绝并返回
   false；空输入为 true 空操作；成功返回 true。屏幕区与光标
   **完全不动**（区别于 prependPrimaryLines）。
2. **绝对行号稳定**：注入 N 行使 `ZzScrollbackStats` 新增
   `totalPrepended += N`；HistoryView/LineSource 的
   `droppedLineCount()` 改为返回 `totalDropped − totalPrepended`
   （绝对行号基线 = 累计裁掉 − 累计注入归还）。效果：既有行的
   绝对行号（基线+idx）在注入前后不变；注入行归还占用其被裁时
   的原号段——应用侧追加差分（newest = 基线+lineCount）在注入
   前后恒等，天然跳过注入行，不会重复归档；选区锚点跨注入稳定。
   **归还额度不变量**：注入数 n ≤ totalDropped − totalPrepended，
   否则拒绝（返回 false/空操作）——只能归还真被裁掉且尚未归还
   的行，杜绝基线下溢；真实读回流（引擎行号与显示绝对号 1:1）
   该条件恒成立。基线因此可回退，HistoryView「单调不减」的旧
   口径作废，以本条为准（contour 后端不支持注入，不受影响）。
   （勘误 E-2：本定案初稿误写为 totalDropped + totalPrepended，
   该公式使既有行绝对号 +2N 平移且追加差分重注入，数学上与
   「零扰动」目标矛盾；2026-10-12 实现期核实修正为减法口径。）
3. **容量**：注入本身不触发裁剪，允许瞬时超容量；下一次
   append 的容量裁剪从最旧端（=注入行）先裁——无害，LogEngine
   是持久权威。注入时 capacity==0 则视为即注即裁
   （totalDropped 直接 +N，等价 append 的零容量语义）。
4. **行宽不变量**：调用方以终端当前列宽供行（同 append 约定）；
   reflow 对注入行一视同仁（重组、斩链规则不变）。
5. **代计数**：注入使历史 generation 递增（前端据此刷新滚动
   范围），同 append/reflow 口径。
6. **clear（ED 3）**：清空全部历史（含注入行）；totalDropped/
   totalPrepended 均不复位（单调口径不变）。应用侧注入账本
   需自行随 clear 复位（Z2 任务 5 职责）。

## 3. 接口

```cpp
// include/ZzTerm/Scrollback.h（ZzScrollback 新增纯虚）
/// 向最旧端注入一批行（M19 读回注入原语；旧到新顺序，以值移交）。
/// 注入不触发容量裁剪（允许瞬时超容量，由后续 append 的裁trim
/// 先裁注入行）；capacity==0 时即注即裁（计入 totalDropped，不耗额度）。
/// 注入成功使 stats().totalPrepended 同步增加，绝对行号基线
/// （totalDropped-totalPrepended）回退 N、既有行绝对号不变。
/// @return false=归还额度不足（n > totalDropped-totalPrepended），
///         整体拒绝、不注入任何行；空输入为 true 空操作。
virtual bool prependOldest(std::vector<ZzLine> lines) = 0;

// ZzScrollbackStats 新增字段
std::uint64_t totalPrepended = 0; ///< 累计经 prependOldest 注入的行数（M19）

// include/ZzTerm/Terminal.h（ZzTerminal 新增公共方法，historyView() 附近）
/// 向 Primary 历史最旧端注入一批行（M19；应用侧历史读回用）。
/// @return false 表示拒绝（Alternate 屏活动期间 / 后端不支持）；
///         空输入为 true 空操作。
/// @note 屏幕区与光标不动；行须为当前列宽；注入经
///       HistoryView.generation() 递增对前端可见。
[[nodiscard]] bool prependHistoryLines(std::vector<ZzLine> lines);

// src/backend/ZzTerminalBackend.h（新增非纯虚，默认不支持）
[[nodiscard]] virtual bool prependHistoryLines(std::vector<ZzLine> lines);
// 默认实现：(void)lines; return false;
```

## 4. ChunkedScrollback 实现要点

头部插入维持「phys = headOffset_ + index」定长寻址不变量：

- `lines.size() <= headOffset_`：直接回填头块空闲槽位
  `[headOffset_-n, headOffset_)`，headOffset_ -= n；
- 否则：前插 `k = ceil((n - headOffset_) / kChunkLines)` 个空块，
  新 headOffset_ = k*kChunkLines + headOffset_ - n，按同一寻址
  公式逐行写入（跨新块尾部与旧头块前段）；
- totalLines_/approxBytes_ 记账 +=；totalPrepended_ += n；
  totalAppended 不变（注入不是 append）；
- 不调用 trimToCapacity（定案 3）。

## 5. 记账调整点

- `ZzNativeHistoryView::droppedLineCount()`（ZzNativeHistoryView.cpp:39-42）
  与 `ZzNativeLineSource::droppedLineCount()`（ZzNativeLineSource.cpp:45-48）：
  `stats().totalDropped` → `stats().totalDropped - stats().totalPrepended`。
- contour 侧 HistoryView/LineSource 不动（注入默认不支持，
  totalPrepended 恒 0 不经 stats——contour 不消费 ZzScrollback）。

## 6. 测试（TDD）

1. prependOldest 顺序与寻址：注入 3 行后 lineAt(0..2) 为注入行
   （旧到新），lineAt(3..) 为原历史；跨块边界（headOffset_ 部分
   裁剪态 + 注入 > 空闲槽）用例。
2. 绝对行号稳定：Terminal 级——feed 出历史，prependHistoryLines
   N 行，断言既有行 dropped+idx 不变（基线 −N + idx +N 逐行抵消）、
   droppedLineCount()−N（基线回退）、lineCount()+N；额度不足拒绝
   用例（n > 额度 → false，历史不变）。
3. 容量：注满容量后再注入不裁；随后 append 触发裁剪先裁注入行。
4. Alternate 拒绝：进 Alternate 后 prependHistoryLines 返回 false，
   历史不变。
5. 空输入 true 空操作；generation 递增。
6. reflow 后注入行重组正确（变列 resize 后注入行内容顺序保持）。

## 7. 文档

- docs/API.md 版本节补 M19 条目（公共面新增：
  ZzTerminal::prependHistoryLines、ZzScrollback::prependOldest、
  ZzScrollbackStats.totalPrepended；droppedLineCount 口径变化 =
  基线含注入计数）。
- doxygen 注释随代码同步（CI 1.9.8 口径：不用反引号+前导 ::）。
