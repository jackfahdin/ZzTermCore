# M15 native 行变 resize 语义对齐（缩行压历史 / 扩行回抽）设计

- 日期：2026-09-30
- 前置：M14 历史访问契约（2026-09-30-m14-history-view-design.md）；M13 spike 现象 5/7（resize 内容丢失拉大不恢复）根因=本缺口
- 分支：contour

## 1. 目的与范围

消除双后端行变 resize 语义不对称：native 后端 `ZzScreen::resize` 行数缩小时从尾部截断（光标所在的底部新内容直接丢弃、不入历史），行数增大时尾部补空——用户可见症状为「resize 截断且拉大不恢复」（M13 spike 现象 5/7）。contour 后端与 xterm 的行为是缩行压入历史、扩行从历史回抽。

范围内：

- native 行变语义对齐 contour 条件语义（缩行压历史、扩行回抽，光标贴底条件）
- ZzScrollback 接口新增 takeNewest（回抽原语）
- 契约文档与 API 文档的行变 parity 声明更新
- 双后端 compat 行变 parity 测试升级

范围外：

- ZzPty 易用性两项（rawMode 默认陷阱、nonBlocking 配置）——单主题原则，另立里程碑
- contour 侧 droppedLineCount 行变过冲（M14 §8 已立案的独立后续项，不动）
- spike widget 改动（免改，语义修复后行为自动正确）
- 列变 reflow 路径（M4 已落地，本里程碑不动）

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | 语义对齐粒度 | 完全对齐 contour 条件语义（shrinkLines/growLines，视口跟随光标） |
| 2 | 里程碑范围 | 只做行变语义对齐，单主题 |

## 3. 架构：回调对偶，Screen 自治

保持「Screen 不知道历史后端」分层（Screen.h:18-21 职责注释）。在现有 `scrollOutCallback_`（屏幕到历史，reflow 溢出已在用，Screen.cpp:102-108）的对偶位置新增 `historyPullCallback_`（历史到屏幕）：

```cpp
/// 历史回抽回调：索取最多 maxLines 行最新历史（旧到新顺序移交所有权）；
/// 无历史可取时返回空向量。由 backend 安装为 scrollback takeNewest。
using ZzHistoryPullCallback = std::function<std::vector<ZzLine>(std::size_t maxLines)>;
```

ZzNativeBackend 构造时与 scrollOutCallback 同点安装。行变逻辑完整收进 `ZzScreen::resize`，backend 的 resize 行变分支零新增判断；Alternate 缓冲无回调路径，自然退化为现状（尾部截断/补空），「Alternate 无历史」规则不变。压入/回抽按缓冲区分（Primary 缓冲有路径），不按当前激活态——与 reflowBuffer 的 mayScrollOut 同一原则，alt 期间主屏网格行变照样压/抽其历史。

## 4. 行变语义（对齐 contour Grid.cpp:784-828 / 736-772）

### 4.1 缩行（k 行）

1. 先裁光标下方行：`cutoff = min(k, 光标下方可裁行数)`，直接缩短、不入历史（对齐 contour 的 cutoffCount 路径；**「光标下方可裁行数」的精确条件——是否仅限空行——以实现时对照 fork Grid.cpp:784-828 为准，钉入 compat 测试**，本规格按 contour 实际语义对齐而非自定义）；
2. 剩余 `k' = k - cutoff > 0` 时，把顶部 k' 行经 `scrollOutCallback_` 压入历史（仅 Primary），光标行号上移 k'。contour 的 pushUp 以光标在末行为条件（Grid.cpp:818）；裁完光标下方可裁行后光标即贴底的推导在「可裁=全部下方行」口径下成立，若 fork 实证为「仅限空行」口径，则压入条件与光标位置的精确联动同样以 fork 为准钉入测试；
3. Alternate 缓冲：维持尾部截断，无压入。

关键收益：缩行后光标所在行与最近输出始终可见，顶部旧内容进入历史而非消失——缩行后向上滚动即可见，M13 现象 5/7 的「截断」面根治。

### 4.2 扩行（k 行）

1. 仅当 Primary 缓冲且光标在末行且 pull 回调存在时：经回调回抽 `m = min(k, 回调实取数)` 行注入屏幕顶部，光标行号下移 m；剩余 k - m 行底部补空；
2. 光标不在末行或无历史可取：全部底部补空（现状）。

「拉大不恢复」面根治：扩行把最新历史拉回屏幕顶部，视口跟随光标。

### 4.3 统一坐标不变性

推入/回抽下统一物理行坐标（历史区在前、屏幕区在后）对内容行保持不变：压入 n 行使 historyCount+1、屏幕行号-1；回抽 m 行使 historyCount-m、屏幕行号+m——内容行的统一坐标不变，M5 选区锚点与 M14 历史视图索引天然安全，无需额外平移。

## 5. ZzScrollback 新增 takeNewest

接口（include/ZzTerm/Scrollback.h）新增：

```cpp
/**
 * @brief 从最新端取走最多 n 行并删除（Core 内部使用；M15 行变回抽原语）。
 * @param n 最多取走行数。
 * @return 取走的行（旧到新顺序），不足 n 行时全部返回。
 * @note 返回引用规则同 append：调用后既有 lineAt 引用失效。
 * @note stats 语义：本操作不是容量裁剪，totalDropped 不变；totalAppended
 *       只增不改（绝对行号产生回退空洞，与 contour rotateBuffersRight 的
 *       stableBase 回退同构，选区锚点按不透明行号处理）。
 */
[[nodiscard]] virtual std::vector<ZzLine> takeNewest(std::size_t n) = 0;
```

ChunkedScrollback 实现：从尾块向头块取（尾块可不满 256 行，无 offset 概念，不碰 headOffset_ 定长寻址不变量——「除尾块外每块恰 256 行」不受影响），同步 totalLines_ 与 approxBytes_（按 append 同一公式逐行扣），返回向量逆序装配成旧到新。

## 6. 计数、契约与文档联动

- historyGeneration_：push 经 scrollOutCallback（M14 递增点已在回调内）；pull 在回调内 takeNewest 实取非空时同样递增（可见历史行数变化，契约「不得漏增」）。
- scrolledOutLines 口径不变：resize 期间 activeChanges_ 为 nullptr，回调聚合的 scrolledOutPending_ 在下次 feed 重置丢弃——API.md 钉住的「resize/reflow 溢出行不计入 scrolledOutLines」维持。
- HistoryView.h 注释与 docs/API.md：M14 §8 登记的「行变不对称补注释」后续项作废，改为声明「行变语义双后端对齐：缩行压历史、扩行回抽（光标贴底条件）；Alternate 无历史」。
- 既有钉住复核：`ZzTerminal::resize` 后选区/搜索 clamp（Terminal 层 cachedLogicalCount 失效路径）在新语义下仍成立（统一坐标不变性，§4.3）。

## 7. 实现与测试

### 7.1 实现面

- `include/ZzTerm/Screen.h` / `src/screen/Screen.cpp`：Buffer 行变新语义（§4）、historyPullCallback_ 安装口、顶部注入与光标平移
- `include/ZzTerm/Scrollback.h` / `src/history/ChunkedScrollback.cpp`：takeNewest（§5）
- `src/backend/native/ZzNativeBackend.cpp`：构造时装 pull 回调（takeNewest 实取非空 ++historyGeneration_）；resize 行变分支改走新语义（列变+行变同调时维持先列后行顺序）
- 既有调用点适配：`test_native_reflow` 用例 4、`test_terminal_core` 光标钳制用例按新语义重钉断言

### 7.2 测试

- 新 Screen 行变单测：裁光标下方 / pushUp / 混合、回抽 / 非末行 / 无历史、Alternate 缓冲不受影响（无压入无回抽）、光标平移与 clamp
- takeNewest 单测：基本取行顺序 / 跨块 / 取空与超取 / totalDropped 与 totalAppended 不变
- facade 级：ZzTerminal(native) 行变后 historyView().lineCount() 与 generation() 联动断言（M14 契约联动）
- compat 升级：test_backend_compat 用例 9（resize-keep）按条件语义重写为真 parity 断言；新增行变 parity 用例（双后端同脚本缩行压历史内容一致、扩行回抽一致）
- 基线：linux-gcc-debug / m2-off-check / m2-shared-check 全绿，fuzz 3/3，doxygen 零警告

### 7.3 验证

- spike 免改；里程碑完成后用户人工复验：拖矮窗口（缩行）最近输出保持可见、旧内容滚轮上滚可见；拉高窗口（扩行）历史拉回屏幕顶部
- 受影响既有用例（test_native_reflow 用例 4、test_terminal_core 光标钳制）的重钉必须附新语义推演，禁止盲改期望值

## 8. 对路线的输入

- M13 spike 现象 5/7（resize 内容丢失）随本里程碑根治，M13 记录 §4 现象 5 标注更新
- M14 §8 后续项「契约注释补行变不对称说明」作废（语义已对齐，无需免责声明）
- 删 ZzTermWidget 风险排序不变；特性对齐阶段 P1 队列不变（ZzPty 两项、IME）
