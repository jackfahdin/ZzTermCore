# M5b 设计：Search / Highlight（子串搜索 + Core 持有 match 列表）

- 日期：2026-09-21
- 分支：contour
- 前置：M5a 已合并 master（fd604f1）——ZzLogicalPos 坐标、ZzIPhysicalLineSource 双后端统一物理行源、ZzSelection 锚定机制（丢弃平移/reflow 保持/Alternate 清空）、选区提取单扫描（O(R+输出)）
- 架构依据：Architecture.md §13（Search 不得拼全部历史为大字符串，按 logical line/chunk；ZzIPhysicalLineSource 已注记为地基）、§504-505（Search Match 必须保持——本里程碑落地）、§894（Logical Position 模型 M5a 已落地，match 复用同坐标）、§19（M5 里程碑行）
- 用户批准决策：执行模型 A（Core 同步单次扫描 + benchmark 门控，异步归前端）；匹配语义 A（纯文本子串 + 大小写敏感选项）；保持机制 A（Core 持有 match 列表，与选区同机制）；高亮表达 A（Core 只给坐标查询，前端画，RenderView 不动）；实现方案一（无状态引擎纯函数 + Core 持有结果列表）

## 1. 背景与目标

M5a 交付了坐标/数据源/锚定三件套，M5b 在其上交付：子串搜索引擎（纯函数、单扫描、文本偏移↔格偏移位置回映）、Core 持有的 match 列表（与选区同套锚定维护）、facade 搜索 API 与 match 坐标查询。高亮渲染与搜索 UI 均为前端职责。

## 2. 旧实现调研结论（决策依据）

ZzTermWidget（Konsole 谱系）search 评估（M4 阶段四路调研）：

- 明确不学：同步阻塞搜索（大数据量卡死 GUI）、每帧全量重建过滤缓冲（复杂度与内存双高）；
- 值得移植：Decoder 的位置回映思想（文本偏移与格偏移互映，宽字符/cluster 换算的正确性全靠它）；
- 本里程碑对应：同步单扫描在 Core 库层无 GUI 线程可阻塞（异步由前端包线程）；引擎 O(R+匹配数)，无任何增量索引。

## 3. 范围

### 3.1 包含

- 公开类型 ZzLogicalRange（半开区间）与 ZzSearchOptions（caseSensitive，Types.h）；
- 搜索引擎 zzSearchLines（src/terminal/，纯函数，双后端共用）：单物理行扫描、按链组建逻辑行文本、位置回映表、子串匹配；
- ZzSearchState（src/terminal/）：pattern/options/matches 持有 + onLinesDropped 平移 + clampTo + clear；
- facade 搜索 API 四件（见 5.5），feed/resize 时与选区同一切面维护；
- 测试矩阵（单测/集成/compat/benchmark 门控）与文档更新（见 5.7/5.8）。

### 3.2 明确排除

- 正则、跨逻辑行匹配（pattern 含换行永不命中）→ 按真实需求再评估；
- 异步/分片/取消令牌 → 前端包线程；
- RenderView/CellView 高亮标志位、当前 match 游标（findNext/findPrev）→ 前端用 match 列表自行导航；
- feed 时增量搜索索引 → 旧实现反面教材，排除；
- M5a 终审延后 Minor M3（extendSelection 每次 O(R) 重算 logicalLineCount 的缓存化）→ 维持延后，真实高频场景出现再评估。

## 4. 现状盘点（已核实的落点）

- ZzIPhysicalLineSource（src/backend/ZzLineSource.h）：六方法统一物理行只读口，双后端各一实现（ZzNativeLineSource / ZzContourLineSource），Alternate 屏历史归零、丢弃计数（native totalDropped / contour stableFloor 双入口 noteFloor/reanchorFloor）；
- zzLogicalLineCount / zzExtractSelectionText（src/terminal/ZzSelectionText.h）：单扫描范式已验证（M5a 终审修复波 I1），搜索引擎按同范式实现；
- ZzSelection（src/terminal/ZzSelection.h）：onLinesDropped 物理计数平移 + 负值 clamp + 全丢清空的语义与测试矩阵（tests/unit/test_selection.cpp），ZzSearchState 平移逻辑参照实现；
- facade 锚点维护切面：Terminal.cpp noteSelectionAfterFeed（activeBufferChanged 清空 / 丢弃平移）与 resize 包裹（clampTo）——本里程碑扩展该两处同时维护 search state；
- 坐标提取规则（M5a 已钉）：宽字符 lead 占 2 格续格无文本、cluster 整串 1 格、空单元格输出空格、行尾空白修剪——搜索引擎组建文本时遵循同一规则（匹配结果坐标才与 selectedText 自洽）；
- 测试基建：tests/unit/test_selection_text.cpp 的 FakeSource 可直接复用；compat 仿 test_selection_compat.cpp 注册模式；benchmark 仿 tests/unit/test_perf_scrollback.cpp 门控 + JSON 落盘（tests/perf/records/）。

## 5. 设计

### 5.1 公开类型（Types.h）

```cpp
struct ZzLogicalRange { ZzLogicalPos start; ZzLogicalPos end; };  // 半开区间 [start, end)
struct ZzSearchOptions { bool caseSensitive = true; };
```

### 5.2 搜索引擎 zzSearchLines（纯函数）

- 签名：`std::vector<ZzLogicalRange> zzSearchLines(const ZzIPhysicalLineSource& src, std::string_view pattern, ZzSearchOptions options)`；
- 单扫描：顺物理行链逐条组建逻辑行文本（规则同 M5a 提取：宽字符整字/续格跳过、cluster 整串、空单元格输出空格、行尾空白修剪）；同时产出**位置回映表**（text offset → cell offset）与大小写折叠副本（不敏感模式；ASCII 折叠，Unicode 不折叠，注释钉住）；
- 匹配：逻辑行文本内子串查找；命中不重叠（命中后从 match 文本末尾继续找）；pattern 含换行符永不命中（不跨逻辑行，v1 钉死）；空 pattern 返回空列表；
- 映射：命中 text 区间经回映表转为 `ZzLogicalRange{{line, cellStart}, {line, cellEnd}}`（半开；宽字符完整覆盖——起点落 lead、终点过续格之后，不拆半字）。格粒度固有边界（T2 审查裁定）：pattern 为裸组合符等 cluster 字节串中段片段时，可命中但坐标归并到整格，此时"命中坐标提取文本等于 pattern"的自洽不变量不成立（提取结果为完整 cluster）——v1 接受，使用该不变量做断言时排除此类 pattern；
- 每时刻只持有一条逻辑行的文本+回映表，不拼全量大串（§13 合规）；复杂度 O(R + 匹配数)。

### 5.3 ZzSearchState（Core 持有 match 列表）

- 成员：pattern（string）、options、matches（vector<ZzLogicalRange>，按坐标升序）；
- onLinesDropped(delta)：所有 match 的 line 平移（物理计数近似语义，与 ZzSelection 同一口径）；两端 line 都平移为负的 match 移除，存活端负值 clamp 到 0；
- clampTo(lineCount)：line 越界的 match clamp/移除（语义同 ZzSelection::clampTo：起点越界移除该 match，终点 clamp）；
- clear：清空全部状态。

### 5.4 Match 保持（§504/505 落地）

- feed：不动（已有内容序号稳定；新内容不自动重搜，重搜时机归前端）；
- 丢弃：onLinesDropped（见 5.3）；
- reflow：逻辑行集合不变，match 天然保持（compat 钉住）；截断行上 col 越界在查询返回时 clamp 到行末（与选区提取同层处理）；
- Alternate 切换：清空 search state（与选区同一切面）；
- 内容漂移钉注：match 是搜索时刻的坐标快照，此后 feed 改写的同坐标内容不校验（同选区语义）；API.md 补注。

### 5.5 facade API

```cpp
// 执行搜索（替换旧搜索状态），返回匹配数；空 pattern 清空状态并返回 0。
std::size_t search(std::string_view pattern, ZzSearchOptions options = {});
// 清空搜索状态。
void clearSearch() noexcept;
// 当前 match 总数（无搜索状态为 0）。
std::size_t searchMatchCount() const noexcept;
// 查询第 index 个 match 的坐标区间（半开）；index 越界或无状态返回 false。
bool searchMatch(std::size_t index, ZzLogicalPos& start, ZzLogicalPos& end) const;
```

### 5.6 错误处理与边界

- 空 pattern → 返回 0 且状态清空；index 越界 → false；无搜索状态 → count 0 / 查询 false；
- 重复 search 替换旧状态（不叠加）；
- 搜索期间不修改任何缓冲（只读扫描）；调用方在 feed/resize 后重搜以覆盖新内容。

### 5.7 测试

- 引擎单测（tests/unit/test_search.cpp，FakeSource 复用 M5a 基建）：单/多命中、不重叠推进、大小写两态、宽字符回映（"x界y" 搜 "界" → {line,1}..{line,3}）、cluster 回映、软换行链内命中、跨逻辑行不命中、pattern 含换行不命中、行尾修剪与 selectedText 自洽（命中坐标提取出的文本等于 pattern）、空 pattern；
- 状态单测（同文件或独立）：onLinesDropped 平移/两端全丢移除/存活端 clamp、clampTo 越界移除与 clamp、clear；
- 集成（tests/unit/test_terminal_search.cpp）：search 后 feed 坐标不漂移、丢弃平移、resize reflow 保持、Alternate 清空、重复 search 替换；
- compat（tests/unit/test_search_compat.cpp，注册模式仿 test_selection_compat）：双后端同脚本 match 列表逐一相等（含宽字符与软换行链）；
- benchmark（tests/unit/test_perf_scrollback.cpp 扩充或独立文件）：10 万行历史单次搜索耗时门控（实测后定阈值，参照 M4 CI 余量惯例），JSON 落盘 tests/perf/records/ 入库；
- 回归门：ON/OFF/shared 全绿、doxygen 零警告。

### 5.8 验收（DoD）

- 三配置全绿（含新增用例）与 doxygen 零警告；
- compat 双后端 match 列表一致；benchmark 门控过且 JSON 入库；
- Architecture.md §19 M5 行收口（M5b 完成）、§504-505 Search Match 标注已落地；API.md 新增搜索小节（含内容漂移与坐标快照钉注）；v1/v2 Checklist Search 相关项勾选。

## 6. 风险与对策

- **位置回映错误**（宽字符/cluster 坐标错一格）：回映规则与 M5a 提取共用同一组建逻辑，单测用"命中坐标提取文本等于 pattern"做自洽断言双向钉死；
- **大文本扫描性能不达标**：单扫描 O(R)，benchmark 门控卡回归；阈值按实测加余量（M4 惯例）；
- **match 列表内存**：每 match 32B，极端 10 万匹配 3.2MB，可接受，不优化；
- **平移近似语义与选区不一致的风险**：ZzSearchState 平移逻辑逐行参照 ZzSelection 实现并共用注释口径，状态单测矩阵对齐 T1；
- **内容漂移造成前端高亮错位**：API.md 钉注"坐标快照"语义 + 建议前端 feed 后按需重搜。

## 7. 里程碑外后续（记录不实施）

- 正则与跨逻辑行匹配：按真实应用需求评估；
- 异步搜索封装示例（前端线程模型）：随首个真实前端落地再评估；
- M5a 终审延后 Minor 族（extendSelection O(R) 缓存、滚动区/DL/IL 序号漂移、contour floor 边角族）维持原裁定；
- M2/M3b/M4 终审延后族维持原裁定。
