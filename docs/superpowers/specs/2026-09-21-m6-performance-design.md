# M6 设计：性能优化（三件量化债）

- 日期：2026-09-21
- 分支：contour
- 前置：M5a/M5b 已合并 master（eb6b3c9）——selection/copy/search/highlight 全交付，双后端 compat 零分歧，benchmark 基线齐全
- 路线图依据：Architecture.md §19 M5 行（性能优化随后续里程碑推进）、Architecture-v2.md §23 M5 行（性能优化）；v1 语义里程碑编号顺延为 M6
- 用户批准决策：范围组合 A（三件量化债全做 + 门控收紧 + 全回归，每件独立任务独立测量）；验收口径 A（搜索 503ms → 目标 <250ms（-O0 同机），未达按实测重新裁定）；接口容忍 A（允许改内部接口 ZzIPhysicalLineSource，公开 API 与行为零变化）；实现方案一（三件定向优化，各自独立可测量）

## 1. 背景与目标

M4/M5 的 benchmark 与终审台账量化出三笔性能债，本里程碑定向清偿：搜索热路径的重复堆分配、逻辑行计数的重复 O(R) 扫描、数据源 lineAt 的值快照拷贝。全部在"行为零变化"硬约束下进行——优化纯粹是资源使用效率问题，任何语义变化都视为事故。

## 2. 三笔债的量化证据（决策依据）

1. **搜索分配**：zzSearchLines 每条逻辑行新建 LineTextMap（text + byteToCell + byteToCellEnd，不敏感模式另有 folded），每物理行另有 lineAt 值快照的 cells vector 重分配；10 万行搜索实测 503ms（-O0，tests/perf/records/2026-09-21-m5b-search.json），T2 审查与 M5b 终审均标记此因；
2. **重复计数**：facade 的 setSelection/extendSelection/search/resize clamp 各自调用 zzLogicalLineCount（O(R) 全扫描），extendSelection 在鼠标拖动场景为高频路径（M5a 终审 Minor M3）；
3. **快照拷贝**：ZzIPhysicalLineSource::lineAt 按值返回 ZzLine——native 侧本可复用调用方缓冲（scrollback/screen 返回 const 引用，拷贝仅为满足值语义），contour 侧 SoA 转换也可复用调用方缓冲避免反复分配；搜索与提取两条热路径都按行支付。

## 3. 范围

### 3.1 包含

- 债 1：zzSearchLines 的 LineTextMap 由调用方持有、逐行 clear 复用容量；
- 债 3：ZzIPhysicalLineSource 值返回 lineAt 替换为借用口 lineAt(row, ZzLine& out)，两后端与全部调用方同任务迁移；
- 债 2：facade Impl 缓存 logicalLineCount（-1=脏，惰性重算），四个计数消费点改走缓存；
- benchmark：test_perf_search 门控按新实测收紧（×3 向上取整惯例），新 JSON 基线入库；test_perf_scrollback 原门控验证不劣化；
- 全回归（ON/OFF/shared/doxygen）与文档更新（见 5.6/5.7）。

### 3.2 明确排除

- 扫描会话（ZzScanSession）类统一遍历重构 → 方案二已排除，过度工程；
- profiling 基建（Tracy/perf 集成）→ 三笔债已有充分测量，无必要；
- UTF-8 编码三处存量重复统一（ZzNativeRenderView/ZzContourConvert/InputEncoder）→ 非性能项，后续里程碑候选；
- append/reflow/RenderView 路径优化 → 既有门控内，不动；
- 全部既有终审延后族（M2-M5b）→ 维持原裁定；
- 公开 API 任何形式的变化 → 禁止。

## 4. 行为零变化硬约束（本里程碑第一原则）

- 现有全部测试（44 个）不改任何断言原样通过；仅两个 linesource 测试随接口迁移改**调用形式**（值返回→out 参数），断言值不变；
- compat 语义、坐标语义、锚定语义、提取/搜索文本逐字节不变；
- 任何需要改测试断言才能过的"优化"即行为变化：停下来裁定，不硬闯；
- 验收目标（搜索 <250ms）未达时按实测重新裁定，不以行为妥协换数字。

## 5. 设计

### 5.1 债 3：lineAt 借用口（先行任务，其余两件建立其上）

- 接口变更（src/backend/ZzLineSource.h）：`virtual ZzLine lineAt(std::size_t) const = 0` 替换为 `virtual void lineAt(std::size_t unifiedRow, ZzLine& out) const = 0`；内部接口不留双口并存；
- out 约定：进入时可为任意状态，实现方负责完整覆写（含 cluster 侧表——native copy-assign 天然覆写；contour 填充前须清侧表，ZzLine 侧表清理语义在计划阶段核实后钉进代码注释）；
- native：out = scrollback_.lineAt(i) / out = screen_.lineAt(r)（vector copy-assign 复用 out 已有容量，消灭反复 malloc）；
- contour：照现状逻辑填入 out（resize 在列数不变时不重分配；blank 行 fillAttrs 防御等现状逻辑不变）；
- 调用方迁移：extractLogicalLine（ZzSelectionText.cpp）snapshots 循环复用同一 ZzLine 缓冲改为按行填充——注意该函数当前按值收 snapshots 且 cluster 文本经 owner 行 clusterText 查询，缓冲复用时须保证"cluster 查询与格遍历同一行生命周期内完成"（现逻辑已是逐行处理，天然满足）；buildLineText（ZzSearch.cpp）同形；两个 linesource 测试改调用形式。

### 5.2 债 1：搜索分配复用（ZzSearch.cpp）

- LineTextMap 提出逻辑行循环，由 zzSearchLines 持有；每行处理前 clear（text/folded/byteToCell/byteToCellEnd 四表同步清，vector clear 保容量）；
- folded 仍仅不敏感模式填充；哨兵与行尾修剪逻辑逐行不变（T2 修复的语义保持）；
- 预期：分配从"每行 3-4 次"降为"全程常数次"；与 5.1 借用口叠加达成 <250ms 目标。

### 5.3 债 2：logicalLineCount 缓存（src/terminal/Terminal.cpp）

- Impl 增加 `std::int64_t cachedLogicalCount = -1`（-1 = 脏）与 `std::int64_t logicalCount()` 惰性访问器（脏时重算 zzLogicalLineCount 并缓存）；
- 置脏点：feed 与 resize 的既有维护切面（noteSelectionAfterFeed 内统一置脏）；
- 消费点迁移：setSelection、extendSelection、search、resize 的 clamp 四处改调 logicalCount()；
- 每次 feed 批量最多一次 O(R) 重算，API 高频调用零扫描。

### 5.4 性能验证

- 测量纪律：同机同 preset（linux-gcc-debug，-O0），优化前后各跑一次 test_perf_search，对比数字写进 commit message；
- 门控：test_perf_search 按新实测 ×3 向上取整收紧，注释标定新基线；JSON 覆盖 tests/perf/records/2026-09-21-m5b-search.json 或新档（计划阶段定，新旧基线并置备注）；
- 不劣化验证：test_perf_scrollback 原门控（append/reflow/lineAt/内存四件）全过。

### 5.5 测试策略

- 硬约束为纲（§4）；linesource 两测试改调用形式不改断言值；
- 搜索/提取/集成/compat 全套原样绿即等价性证明（自洽不变量用例对此类重构尤其有效）；
- 无新增行为，不新增功能测试；借用口正确性由既有矩阵（含宽字符/cluster/接缝/blank 行）覆盖。

### 5.6 验收（DoD）

- 44 测试断言零改动全绿（linesource 两测试仅调用形式变化）；ON/OFF/shared/doxygen 全过；
- 搜索实测达成 <250ms 或按实测重新裁定记录；门控收紧且 JSON 入库；
- test_perf_scrollback 不劣化；
- Architecture.md §13 区域性能注记更新（缓冲复用落地）；规格/计划入库。

### 5.7 任务划分

- T1：lineAt 借用口迁移（接口 + 两后端 + 调用方 + 两 linesource 测试）；
- T2：搜索分配复用（LineTextMap 提升 + benchmark 前后对比重测）；
- T3：logicalLineCount 缓存；
- T4：门控收紧 + JSON 基线 + 文档 + 全回归门。

## 6. 风险与对策

- **借用口引入 use-after-clear 类生命周期 bug**（缓冲复用后 cluster 文本查询跨行失效）：调用点逐行处理语义不变，且 cluster 查询严格在同行生命周期内（5.1 已钉）；既有矩阵（cluster/宽字符/接缝用例）兜底；
- **contour 侧 ZzLine 侧表残留**（复用缓冲时旧 cluster 串污染新行）：out 约定强制完整覆写，代码注释钉住，linesource 测试含 cluster 断言；
- **缓存脏标志漏置**（新路径绕过 noteSelectionAfterFeed）：置脏集中在既有切面，feed/resize 是内容变更唯二入口（M5a 架构事实）；若漏置，测试表现为坐标类断言失败，44 测试兜底；
- **优化后行为漂移未发现**：硬约束 + 自洽不变量 + compat 三层网；
- **目标不达**：2A 已定按实测重新裁定，不硬扛。

## 7. 里程碑外后续（记录不实施）

- 扫描会话类统一遍历：真实 profiling 证明需要时再评估；
- UTF-8 编码三处存量重复统一、noteSelectionAfterFeed 改名 noteAnchorsAfterFeed：后续顺手项；
- 路线图后续组：Unicode edge cases + Fuzz（下一里程碑组）、百万行实验、macOS。
