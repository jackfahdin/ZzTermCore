# M6 性能优化实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在"行为零变化"硬约束下清偿三件量化性能债：ZzIPhysicalLineSource 值快照改借用口、搜索 LineTextMap 逐行复用、facade logicalLineCount 缓存；搜索实测 503 → 目标 <250ms（-O0 同机）。

**架构：** 全部改动在内部层（src/backend 接口、src/terminal 实现、linesource 测试调用形式）；公开 API 与全部测试断言零变化。借用口语义：native copy-assign 复用容量、contour move-assign 零回归（cluster 侧表无公开清理口，规格 5.1 已裁定）。

**技术栈：** C++20、CMake（GLOB 收编，无新文件即无 CMake 改动）、benchmark 门控惯例（实测×3 向上取整）、doxygen 零警告门。

**规格：** docs/superpowers/specs/2026-09-21-m6-performance-design.md（commit 4de6ca6 + 裁定 d79e780）。

**第一原则（全计划最高优先级）：** 现有 44 个测试不改任何断言原样通过；仅 test_native_linesource / test_contour_linesource 两文件随接口迁移改**调用形式**（断言值不变）。任何需要改断言才能过的改动即行为变化——停下来报告 BLOCKED，不硬闯。

**命令约定（全计划通用）：**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
# OFF：cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
# shared：cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
# 文档：doxygen Doxyfile（必须在仓库根运行，exit 0 且零警告）
```

## 文件结构

| 文件 | 职责 | 任务 |
|---|---|---|
| src/backend/ZzLineSource.h | 接口：值返回 lineAt 替换为借用口 lineAt(row, out) | T1 |
| src/backend/native/ZzNativeLineSource.h/.cpp | native 借用口实现（copy-assign 复用容量） | T1 |
| src/backend/contour/ZzContourLineSource.h/.cpp | contour 借用口实现（move-assign 零回归） | T1 |
| src/terminal/ZzSelectionText.cpp | extractLogicalLine 调用方迁移（emplace_back+填充） | T1 |
| src/terminal/ZzSearch.cpp | buildLineText 行缓冲复用（T1）；LineTextMap 提升复用（T2） | T1/T2 |
| tests/unit/test_native_linesource.cpp、test_contour_linesource.cpp | 调用形式迁移（断言值不变） | T1 |
| src/terminal/Terminal.cpp | logicalLineCount 缓存 + 四个消费点迁移 | T3 |
| tests/unit/test_perf_search.cpp | 门控按新实测收紧 | T4 |
| tests/perf/records/2026-09-21-m6-perf.json | 新基线入库 | T4 |
| docs/Architecture.md | §13 区域性能注记更新 | T4 |

---

### 任务 1：lineAt 借用口迁移（接口 + 两后端 + 调用方 + 测试）

**文件：**
- 修改：`src/backend/ZzLineSource.h`
- 修改：`src/backend/native/ZzNativeLineSource.h`、`src/backend/native/ZzNativeLineSource.cpp`
- 修改：`src/backend/contour/ZzContourLineSource.h`、`src/backend/contour/ZzContourLineSource.cpp`
- 修改：`src/terminal/ZzSelectionText.cpp`（extractLogicalLine 快照循环）
- 修改：`src/terminal/ZzSearch.cpp`（buildLineText 行缓冲提出 r 循环）
- 测试：`tests/unit/test_native_linesource.cpp`、`tests/unit/test_contour_linesource.cpp`（调用形式迁移）

- [ ] **步骤 1：迁移两个 linesource 测试的调用形式（断言值不变）**

`tests/unit/test_native_linesource.cpp`：所有 `src.lineAt(N).cellAt(M)` 与 `src.lineAt(N).wrapped()` 形式改为先取行再断言。逐处对照（以下为代表性改写，文件内全部 lineAt 调用点同构处理）：

```cpp
// 旧：
ZZ_TEST_EXPECT(src.lineAt(0).cellAt(0).codePoint() == U'a');
// 新：
ZzLine line;
src.lineAt(0, line);
ZZ_TEST_EXPECT(line.cellAt(0).codePoint() == U'a');
```

注意：同一测试函数内多个 lineAt 调用可复用同一个 `ZzLine line;` 变量（每次调用完整覆写），但**不得**跨调用保留 cell 引用。`lineWrapped` 调用形式不变。`makeLine` 辅助与全部断言值不动。

`tests/unit/test_contour_linesource.cpp`：`lineText(src.lineAt(0), 2)` 形式改为：

```cpp
// 旧：
ZZ_TEST_EXPECT(lineText(src.lineAt(0), 2) == "r0");
// 新：
ZzLine line;
src.lineAt(0, line);
ZZ_TEST_EXPECT(lineText(line, 2) == "r0");
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：编译失败，`lineAt` 单参版本不存在（此时接口还是值返回版——测试先行红）

- [ ] **步骤 3：接口与两后端实现**

`src/backend/ZzLineSource.h`：值返回声明替换为借用口（注释同步更新）：

```cpp
    // 统一物理行借用口：把第 unifiedRow 行完整覆写到 out（含 cluster 侧表，
    // 两侧均经赋值语义整体替换，无残留）。out 进入时可为任意状态；
    // unifiedRow ∈ [0, historyLineCount()+screenRowCount())。
    // 复用 out 可消除逐行分配（native copy-assign 复用容量；contour
    // move-assign 与值返回开销持平——ZzLine cluster 侧表无公开清理口，
    // contour 容量复用待后续 ZzLine 增补清理口后启用，规格 M6 5.1）。
    virtual void lineAt(std::size_t unifiedRow, ZzLine& out) const = 0;
```

（头部文件注释中"值快照语义"段同步改写为借用口约定。）

`src/backend/native/ZzNativeLineSource.h`：声明替换为：

```cpp
    void lineAt(std::size_t unifiedRow, ZzLine& out) const override;
```

`src/backend/native/ZzNativeLineSource.cpp`：实现替换为：

```cpp
void ZzNativeLineSource::lineAt(std::size_t unifiedRow, ZzLine& out) const
{
    const std::size_t history = historyLineCount();
    if (unifiedRow < history)
        out = scrollback_.lineAt(unifiedRow); // copy-assign：复用 out 容量
    else
        out = screen_.lineAt(static_cast<int>(unifiedRow - history));
}
```

`src/backend/contour/ZzContourLineSource.h`：声明替换为：

```cpp
    void lineAt(std::size_t unifiedRow, ZzLine& out) const override;
```

`src/backend/contour/ZzContourLineSource.cpp`：实现替换为：

```cpp
void ZzContourLineSource::lineAt(std::size_t unifiedRow, ZzLine& out) const
{
    const auto history = historyLineCount();
    if (unifiedRow < history)
        out = backend_.historyLineSnapshot(static_cast<int>(unifiedRow));
    else
        out = backend_.screenLineSnapshot(static_cast<int>(unifiedRow - history));
    // move-assign：侧表整体替换无残留；分配开销与旧值返回持平（规格 M6 5.1 裁定）
}
```

- [ ] **步骤 4：调用方迁移**

`src/terminal/ZzSelectionText.cpp` 的 extractLogicalLine 快照循环（:46-49 区域）：

```cpp
    // 旧：
    // std::vector<ZzLine> snapshots;
    // snapshots.reserve(span.second);
    // for (std::size_t i = 0; i < span.second; ++i)
    //     snapshots.push_back(src.lineAt(span.first + i));
    // 新：
    std::vector<ZzLine> snapshots;
    snapshots.reserve(span.second);
    for (std::size_t i = 0; i < span.second; ++i) {
        snapshots.emplace_back();
        src.lineAt(span.first + i, snapshots.back());
    }
```

`src/terminal/ZzSearch.cpp` 的 buildLineText（:52-53 区域）：行缓冲提出 r 循环（链内复用；LineTextMap 提升是 T2 的事，本任务不动）：

```cpp
    // 旧：
    // for (std::size_t r = 0; r < rowCount; ++r) {
    //     const ZzLine line = src.lineAt(firstRow + r);
    // 新：
    ZzLine line;
    for (std::size_t r = 0; r < rowCount; ++r) {
        src.lineAt(firstRow + r, line);
```

注意 buildLineText 内 `line.clusterText(...)` 的调用点（:66）引用的就是该循环变量，迁移后语义不变（同行生命周期内查询）。

- [ ] **步骤 5：运行全部测试验证通过（行为零变化判据）**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：44/44 全绿，断言零改动（git diff 确认测试文件只有调用形式变化）

- [ ] **步骤 6：Commit**

```bash
git add src/backend/ZzLineSource.h src/backend/native/ZzNativeLineSource.h src/backend/native/ZzNativeLineSource.cpp src/backend/contour/ZzContourLineSource.h src/backend/contour/ZzContourLineSource.cpp src/terminal/ZzSelectionText.cpp src/terminal/ZzSearch.cpp tests/unit/test_native_linesource.cpp tests/unit/test_contour_linesource.cpp
git commit -m "refactor(linesource): lineAt 值快照迁移为借用口（M6 T1，行为零变化）"
```

---

### 任务 2：搜索 LineTextMap 提升复用 + 前后测量

**文件：**
- 修改：`src/terminal/ZzSearch.cpp`
- 测量：`tests/unit/test_perf_search.cpp`（不改代码，只跑数）

- [ ] **步骤 1：优化前测量并记录**

运行：`./build/linux-gcc-debug/tests/test_perf_search`
记录：searchMs 实测值（写进 T2 commit message 的"优化前"数字；T1 借用口可能已带来部分提升，如实记录）。

- [ ] **步骤 2：buildLineText 改 out 参数复用**

`src/terminal/ZzSearch.cpp`：

```cpp
// buildLineText 签名与函数头改为：
// 组建一条逻辑行的纯文本与位置回映表到 m（规则同 ZzSelectionText 提取）。
// m 由调用方持有跨行复用：进入时四表同步清空（vector clear 保容量，
// 分配从"每行 3-4 次"降为"全程常数次"，M6 债 1）。
void buildLineText(const ZzIPhysicalLineSource& src,
                   std::size_t firstRow, std::size_t rowCount, bool needFolded,
                   LineTextMap& m)
{
    const int cols = src.cols();
    m.text.clear();
    m.folded.clear();
    m.byteToCell.clear();
    m.byteToCellEnd.clear();
    std::int32_t cell = 0;
    // ……emit lambda 与逐格逻辑、哨兵/修剪逻辑全部不变……
    // （函数末尾的 return m; 删除）
}
```

`zzSearchLines` 主循环（:114 区域）改为持有复用：

```cpp
    // 旧：
    // LineTextMap m = buildLineText(src, row, count, !options.caseSensitive);
    // 新（LineTextMap m; 提到 while 循环外一次声明）：
    buildLineText(src, row, count, !options.caseSensitive, m);
```

- [ ] **步骤 3：行为零变化验证**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_search && ./build/linux-gcc-debug/tests/test_selection_text && ./build/linux-gcc-debug/tests/test_search_compat`
预期：三个全 PASS（断言零改动）

- [ ] **步骤 4：优化后测量并记录**

运行：`./build/linux-gcc-debug/tests/test_perf_search`（连跑三次取代表值）
记录：searchMs 实测值。对照目标 <250ms：达成/未达都如实记录（未达不硬扛，报告 DONE_WITH_CONCERNS 由控制者裁定）。

- [ ] **步骤 5：Commit**

```bash
git add src/terminal/ZzSearch.cpp
git commit -m "perf(search): LineTextMap 提升复用消除逐行分配（M6 T2，实测 <前>ms → <后>ms）"
```

（commit message 中的前后数字用步骤 1/4 的实测值替换。）

---

### 任务 3：facade logicalLineCount 缓存

**文件：**
- 修改：`src/terminal/Terminal.cpp`
- 测试：无新增（既有 44 测试行为零变化判据）

- [ ] **步骤 1：Impl 增加缓存与惰性访问器**

`src/terminal/Terminal.cpp` 的 Impl（:34-50 区域）：

```cpp
    std::unique_ptr<ZzTerminalBackend> backend;

    ZzSelection selection;
    ZzSearchState searchState;
    std::uint64_t lastDropped = 0; // lineSource().droppedLineCount() 的上次观测值
    std::int64_t cachedLogicalCount = -1; // 逻辑行计数缓存（-1 = 脏；M6 债 2）

    // 逻辑行计数（惰性重算：脏时 O(R) 扫描一次并缓存；feed/resize 置脏后
    // 任意多次 API 调用共享一次扫描）。
    [[nodiscard]] std::int64_t logicalCount()
    {
        if (cachedLogicalCount < 0)
            cachedLogicalCount = zzLogicalLineCount(backend->lineSource());
        return cachedLogicalCount;
    }
```

`noteSelectionAfterFeed` 末尾（lastDropped 更新后）追加置脏（feed 与 resize 都经过此切面，单点置脏）：

```cpp
        lastDropped = dropped;
        cachedLogicalCount = -1; // 内容已变：计数缓存置脏
```

- [ ] **步骤 2：四个消费点迁移**

- `setSelection`（:133）：`const std::int64_t count = zzLogicalLineCount(impl_->backend->lineSource());` → `const std::int64_t count = impl_->logicalCount();`
- `extendSelection`（:149）：同上替换；
- `resize`（:70 区域）：`const std::int64_t count = zzLogicalLineCount(impl_->backend->lineSource());` → `const std::int64_t count = impl_->logicalCount();`（注意 resize 先经 noteSelectionAfterFeed 置脏再取值，顺序保持）；
- `search`：若有计数调用同法替换（当前实现不经计数——zzSearchLines 自行扫描；无则不动，注释说明）。

- [ ] **步骤 3：行为零变化验证**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：44/44 全绿

- [ ] **步骤 4：Commit**

```bash
git add src/terminal/Terminal.cpp
git commit -m "perf(terminal): logicalLineCount 惰性缓存消除重复全扫描（M6 T3）"
```

---

### 任务 4：门控收紧 + 新基线 + 文档 + 全回归

**文件：**
- 修改：`tests/unit/test_perf_search.cpp`（门控与基线注释）
- 创建：`tests/perf/records/2026-09-21-m6-perf.json`
- 修改：`docs/Architecture.md`（§13 区域，:181-183 附近的 zzSearchLines 注记）

- [ ] **步骤 1：门控收紧**

`tests/unit/test_perf_search.cpp`：门控从 1600ms 收紧为"T2 步骤 4 实测值 ×3 向上取整到整百"（实测若 230ms → 门控 700ms），注释更新基线标定（注明 M6 优化前后数字与日期）。matches 断言不动。

- [ ] **步骤 2：新基线 JSON 入库**

```bash
./build/linux-gcc-debug/tests/test_perf_search
cp build/linux-gcc-debug/tests/zzterm-perf-search.json tests/perf/records/2026-09-21-m6-perf.json
```

保留 2026-09-21-m5b-search.json 作为历史基线不覆盖。

- [ ] **步骤 3：Architecture.md §13 区域注记更新**

docs/Architecture.md 的 zzSearchLines 单扫描注记（:181-183 区域）追加一句：M6 已落地数据源借用口与缓冲复用（行为零变化），搜索实测由 503ms 优化至实测值（-O0）；数据源值快照语义已由借用口取代（lineAt(row, out)）。

- [ ] **步骤 4：全回归门**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
```

预期：ON 44/44、OFF 35/35、shared 44/44、doxygen 零警告；test_perf_scrollback 原门控全过（不劣化证明）。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_perf_search.cpp tests/perf/records/2026-09-21-m6-perf.json docs/Architecture.md
git commit -m "perf(search): 门控随 M6 优化收紧并入库新基线（M6 T4）"
```

---

## 自检结论（计划编写后）

- **规格覆盖度**：§3.1 全项——债 3 借用口（T1）、债 1 LineTextMap 复用（T2）、债 2 计数缓存（T3）、benchmark 收紧与 JSON（T4）、不劣化验证（T4 步骤 4）、文档（T4 步骤 3）。§4 硬约束 → 每个任务都有"断言零改动全绿"步骤。§5.1 contour 裁定（move-assign 零回归）→ T1 步骤 3 实现与注释。
- **占位符扫描**：T2 commit message 的"前后数字"为测量纪律的既定程序（步骤 1/4 实测替换），T4 门控为"实测×3 向上取整"既定规则——均为可执行指令而非占位符。无 TODO/待定。
- **类型一致性**：借用口签名 `void lineAt(std::size_t, ZzLine&) const` 在接口/两后端/三调用方/两测试间一致；`logicalCount()` 与 `cachedLogicalCount` 命名在 T3 各处一致；buildLineText 新签名（out 参数末位）T2 定义与调用一致。
