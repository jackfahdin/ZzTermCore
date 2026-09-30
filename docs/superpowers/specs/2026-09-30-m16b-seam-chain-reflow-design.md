# M16b 列变 reflow 接缝链统一重组（接缝链归还屏幕）设计

- 日期：2026-09-30
- 前置：M16 硬行语义对齐（2026-09-30-m16-hardline-reflow-design.md）；M4 分域 reflow 固有边界（Scrollback.h:90-92，本规格收窄其适用范围）
- 分支：contour

## 1. 目的与范围

消除列变 reflow 的跨历史/屏幕接缝劈链：M16 后硬行缩列多行化正常，但往返中恰好横跨接缝的链（历史末行 wrapped=true + 屏幕首行续接）在变宽时被永久劈成两条独立链——历史 reflow 的 ZzReflowStreamer::finish 把 dangling 尾链当完整链终结（置 wrapped=false 并裁链尾空白），屏幕 reflow 划链从首行自立链头。contour 统一流重组无此问题，用户的核心诉求「任意缩列再拉大布局完整」在接缝处不成立。

**劈链机制（调研实证）**：劈链是两步接力——① ZzChunkedScrollback::reflow 经 ZzReflowStreamer::finish（src/screen/Reflow.cpp:161-167）终结 dangling 尾链；② ZzScreen::reflowBuffer 的链划分从 lines[0] 开始（zzReflowLines 分区循环）。跨缝逻辑行在架构内本是合法一等状态（选区 ZzSelectionText 的接缝规则，ZzLineSource.h:4-6），故本缺陷纯属 reflow 路径，不涉语义否认。

范围内：

- ZzNativeBackend 列变 reflow 前的接缝链检测与「摘除-归还」协调
- ZzScreen 新增 prependPrimaryLines 原语（顶部插行，仅 primary）
- 跨缝往返的 facade/compat/选区测试（当前全库零覆盖）
- M4 固有边界声明的收窄（文档更新）

范围外：

- **存量劈链修复**：已被现行 reflow 终结的链，链尾空白格已被裁剪（zzReflowChain trimEnd），逐格不可复原——本修复只对之后的 resize 生效（§6 向前兼容声明）
- 历史+屏幕全量统一 reflow（方案二，推翻 M4 分域架构）与不完整方案三（只改 finish 不终结）——均已评估否决
- 纯历史 API 的 dangling 终结语义（testReflowDanglingTailChain 钉住的行为）——本方案在 backend 层摘除，该 API 语义不变
- Alternate 缓冲（无历史，永不归还）
- M17 不换行显示模式

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | 方案选择 | 方案一：接缝链归还屏幕（prepend + 复用 reflowBuffer 单一重组路径） |
| 2 | 存量劈链 | 不修复（链尾空白已裁不可逐格复原），修复只对新 resize 生效 |
| 3 | Alternate | 永不归还（接缝链只属 primary，按缓冲区分与激活态无关） |

## 3. 核心机制：摘除-归还-复用单一重组路径

ZzNativeBackend::resize 的列变分支（src/backend/native/ZzNativeBackend.cpp:249-253）在历史 reflow（:250）之前插入接缝处理：

1. **检测**：`scrollback_->lineCount() > 0 && scrollback_->lineAt(lineCount-1).wrapped()`——历史末行 wrapped=true 即存在跨缝链（其续接在 primary 屏幕首链）；
2. **摘除**：沿 wrapped 标志自末行向回走 O(链长) 找链头，`scrollback_->takeNewest(链长)` 取出完整尾链（M15 原语，旧到新顺序，代计数经既有回调递增）；
3. **归还**：`screen_.prependPrimaryLines(std::move(尾链))`——尾链插到 primary 屏幕顶部，wrapped 链接与旧屏幕首行自然衔接；
4. **既有流程不变**：`scrollback_->reflow(cols)`（尾链不再 dangling，无终结劈链）→ `++historyGeneration_` → `screen_.reflow(cols)`——完整链接缝链经 reflowBuffer 统一重组，瞬时超 rows_ 的部分由既有溢出机制（Screen.cpp:151-161）推回历史，wrapped 标志保留，回到合法跨缝态（下次变大可再接）。

**contour 对齐论证（调研 §4）**：contour 统一流重组后「页面 = 重组流尾部 pageSize 行」（rotateBuffersLeft 收账）；本方案 prepend 使屏幕流变长、溢出裁头部，留下的恰是流尾 rows_ 行——语义精确对齐。contour 自身也持有「历史末行 wrapped dangling」瞬态，区别仅在其下次列变仍统一流重组故永远可接回；本方案使 native 获得同性质。

**组合 resize（行列同变）**：列 reflow 先于行变（:254-255），接缝摘除以插入点在列 reflow 前，行变回抽（takeNewest）拿到的历史不再是被终结的版本——组合场景一并修复。

## 4. ZzScreen::prependPrimaryLines 原语

```cpp
/**
 * @brief 把若干行插入 Primary 缓冲区顶部（Core 内部使用；M16b 接缝链归还）。
 * @param lines 待插入行（以值移交所有权，旧到新顺序）。
 * @note 插入后行数可瞬时超过 rows_——由随后的 reflow() 溢出分支裁回
 *       （reflowBuffer 出口恒 rows_）；cursor.position.row 随插入数平移；
 *       wrapPending 清除；dirty 状态由随后的 reflow 全屏标脏自洽。
 * @note 仅作用于 primary_（与活动缓冲无关）；Alternate 永不插入。
 */
void prependPrimaryLines(std::vector<ZzLine> lines);
```

实现要点（调研 §3 已验证）：

- 插入位置 `primary_.lines.begin()`；`primary_.cursor.position.row += 插入数`（reflowBuffer 的光标→链坐标换算依赖正确链头回找，不平移则 track 指错链）；
- dirtyRows/dirtyRanges 尺寸不动（瞬时超行对外不可见——lineAt/rowDirty/dirtyRange 均以 rows_ 界检；reflow 出口行数回 rows_ 后 markAllDirty 自洽）；
- 插入后不得插入任何写入路径调用（单线程约定下 prepend→reflow 是 backend resize 内的连续两步）；
- 瞬时超行时 Buffer 的对外查询以 rows_ 为界，安全。

## 5. 测试

### 5.1 新增（覆盖缺口——全库此前零跨缝列变用例）

- **Screen 原语单测**（tests/unit/test_screen_rowresize.cpp 或新文件）：prepend 插入与内容顺序、cursor.row 平移、瞬时超行后 reflow 出口恒 rows_、仅 primary（alternate 不受影响）
- **facade 跨缝往返**（tests/unit/test_native_reflow.cpp 或新文件）：
  - 确定性造缝：feed 一条硬长行 + filler 顶入历史使链恰好跨缝（历史末行 wrapped=true）→ 缩列 → 断言跨缝状态保持（历史末行 wrapped=true 且文本连续）→ 拉大 → 断言链接回（统一坐标下历史末行与屏幕首行文本合成原始长行、或整体回到一侧）、内容逐格完整；
  - 多轮往返：80→40→30→80 断言布局完整；
  - 光标跟踪：接缝链含光标所在链时 resize 后光标落在正确内容位置
- **compat 跨缝对照**（tests/unit/test_backend_compat.cpp 新增用例）：双后端同一跨缝脚本，缩/拉两档断言历史行数、历史文本逐行、wrapped 逐行（contour 快照口归一化语义）、屏幕逐格（既有 AllowEmptyWidthDiff 豁免档）一致——M16 用例 21 未覆盖跨缝场景，本用例补位
- **选区跨缝文本不变**（tests/unit/test_terminal_selection.cpp）：选区跨接缝链，80→40→80 后 selectedText 不变（补 ZzSelectionText 跨缝拼链在 reflow 下的回归）

### 5.2 既有回归（调研盘点预期零翻红）

test_native_reflow（缝处 filler 硬行无跨缝链）、compat 用例 18/19/21（历史末行均为硬行）、test_historyview 代计数断言（只要求递增）、test_screen_reflow、test_scrollback 全系（含 dangling 终结 API 语义，不动）、M15 行变全系、selection/search 系。

### 5.3 基线

linux-gcc-debug / m2-off-check / m2-shared-check 全绿，fuzz 3/3，doxygen 零警告。

## 6. 文档与声明

- **向前兼容声明**：存量劈链（修复前已被终结的跨缝链）不修复——其链尾空白格已被 trimEnd 裁剪，逐格不可复原；修复只对之后的 resize 生效。写入本规格即记录，运行时无迁移逻辑。
- **M4 固有边界收窄**：Scrollback.h:90-92 与 HistoryView.h 的「分域 reflow 接缝断链」免责声明更新为「接缝链经归还机制统一重组（M16b），仅纯历史 API 直调 reflow 时 dangling 尾链仍按完整链终结」；docs/API.md 与 Terminal.h resize 注释同步。
- docs/API.md 版本节补 M16b 条目（行为语义变化：跨缝链不再劈开；ZzScreen 新增 prependPrimaryLines 公共方法）。

## 7. 对路线的输入

- 用户核心诉求「任意缩列再拉大布局完整」随本里程碑 100% 成立（M15 行向 + M16 列向 + M16b 接缝）
- M13 记录 §4 现象 5 标注补记 M16b；demo 复验工具（/tmp/zz-m16-demo）可复用作修复后回归
- 删 ZzTermWidget 风险排序不变；M17（不换行显示模式）前置完整性地基全部就绪
