# M10 搜索优化（行级 ASCII 快路）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** facade 轨 1M 档全量 search 双画像搜进 5s 门（当前 ascii 5550.92ms / mixed 9691.16ms，-O0），search 公开 API 与行为零变化。

**架构：** 仅改 src/terminal/ZzSearch.cpp：buildLineText 加"全行窄格 ASCII/Empty"预检与批量提取快路（text/folded 紧凑填充、回映恒等免建表），zzSearchLines 对快路行直接换算命中位置；任一 cell 不满足回退原慢路。

**技术栈：** C++20、ctest、M8 bench 双 harness、tests/perf/records 归档惯例。

**规格：** docs/superpowers/specs/2026-09-29-m10-search-fastpath-design.md（门控口径、回退路径、排除项以规格为准）

**基线命令（本机 Linux，全程不得回归）：**
- ON：`ctest --preset linux-gcc-debug`（50/50）
- OFF：`ctest --test-dir build/m2-off-check`（40/40）
- shared：`ctest --test-dir build/m2-shared-check`（50/50）
- fuzz：`ctest --preset linux-clang-fuzz -R fuzz`（2/2；configure 重建需 `-D CMAKE_CXX_COMPILER=clang++-20`）
- doxygen：`doxygen Doxyfile`（exit 0 零警告）

---

### 任务 1：buildLineText 快路实现

**文件：**
- 修改：`src/terminal/ZzSearch.cpp`（LineTextMap 结构约 :27-32、buildLineText 约 :38-96、zzSearchLines 命中换算约 :124-131）

- [ ] **步骤 1：LineTextMap 加快路标记**

```cpp
struct LineTextMap {
    std::string text;                        // 行尾空白已修剪
    std::string folded;                      // ASCII 折叠副本（仅不敏感模式构建）
    std::vector<std::int32_t> byteToCell;    // text.size()+1 项：字节位置 → 格偏移
    std::vector<std::int32_t> byteToCellEnd; // text.size() 项：字节位置 → 所在单元末格之后一格
    bool fastIdentity = false;               // 快路：回映恒等（i -> i / i+1），两表未构建
};
```

- [ ] **步骤 2：buildLineText 加预检与快路**

在 buildLineText 函数体开头（m 四表清空之后、慢路 emit lambda 之前）插入预检与快路；快路命中即 return，慢路代码原样保留为回退。预检规则：逐 cell 检查——宽度 Empty 放行；宽度 Narrow 且非聚簇且码点不超过 127 放行；其余（宽格、续格、聚簇、码点超 127）置 fastPath=false 并短路。宽格续格出现时其 lead 必含宽码点，必然落入不放行分支，无漏判。

```cpp
void buildLineText(const ZzIPhysicalLineSource& src,
                   std::size_t firstRow, std::size_t rowCount, bool needFolded,
                   LineTextMap& m)
{
    const int cols = src.cols();
    m.text.clear();
    m.folded.clear();
    m.byteToCell.clear();
    m.byteToCellEnd.clear();
    m.fastIdentity = false;

    // 预检：全物理行均为"窄格非聚簇码点不超过 127，或 Empty"时走快路
    //（M10：ascii 画像全部行、mixed 画像约三分之二行命中，批量提取免建回映表）。
    ZzLine line;
    bool      fastPath = true;
    for (std::size_t r = 0; fastPath && r < rowCount; ++r) {
        src.lineAt(firstRow + r, line);
        for (int c = 0; c < cols; ++c) {
            const ZzCell& zc = line.cellAt(c);
            if (zc.width() == ZzCellWidth::Empty)
                continue;
            if (zc.width() != ZzCellWidth::Narrow || zc.isCluster() || zc.codePoint() > 127) {
                fastPath = false;
                break;
            }
        }
    }
    if (fastPath) {
        // 快路：text/folded 紧凑填充（Empty 与码点 0 的窄格输出空格，与慢路同规则）。
        const std::size_t totalCells = static_cast<std::size_t>(cols) * rowCount;
        m.text.resize(totalCells);
        if (needFolded)
            m.folded.resize(totalCells);
        std::size_t w = 0;
        for (std::size_t r = 0; r < rowCount; ++r) {
            src.lineAt(firstRow + r, line);
            for (int c = 0; c < cols; ++c, ++w) {
                const ZzCell& zc = line.cellAt(c);
                const char ch = (zc.width() == ZzCellWidth::Empty || zc.codePoint() == 0)
                                    ? ' '
                                    : static_cast<char>(zc.codePoint());
                m.text[w] = ch;
                if (needFolded)
                    m.folded[w] = foldByte(ch);
            }
        }
        // 行尾空白修剪（与慢路同规则；回映恒等无需维护表）。
        while (!m.text.empty() && m.text.back() == ' ') {
            m.text.pop_back();
            if (needFolded)
                m.folded.pop_back();
        }
        m.fastIdentity = true;
        return;
    }

    std::int32_t cell = 0;
    auto emit = [&](std::string_view bytes, std::int32_t cellCount) {
    // ……以下慢路代码与原实现逐字一致（emit lambda、逐 cell 循环、哨兵与修剪），
    // 唯一改动是删除原函数开头重复的 ZzLine line; 声明（已上移到预检前）。
```

实现注意：原慢路里的 `ZzLine line;` 声明上移到了预检之前（预检与快路也要用），慢路段不得重复声明；其余慢路语句逐字不动。

- [ ] **步骤 3：zzSearchLines 命中换算加快路分支**

现状（约 :124-131）：

```cpp
        while ((pos = hay.find(needle, pos)) != std::string::npos) {
            const auto cellStart = m.byteToCell[pos];
            // 末字节所在单元的末格之后一格：命中尾落在单元字节中段时
            // 同样归并整格（规格 5.2），不产零宽区间；match 末尾恰好
            // 对齐单元边界时等价于 byteToCell[pos + needle.size()]。
            const auto cellEnd = m.byteToCellEnd[pos + needle.size() - 1];
            out.push_back(ZzLogicalRange{{logicalLine, cellStart}, {logicalLine, cellEnd}});
            pos += needle.size(); // 命中不重叠：从 match 末尾继续
        }
```

改为：

```cpp
        while ((pos = hay.find(needle, pos)) != std::string::npos) {
            std::int32_t cellStart;
            std::int32_t cellEnd;
            if (m.fastIdentity) {
                // 快路恒等回映：字节位置即格偏移；命中尾末格之后一格即 pos+needle.size()，
                // 与慢路 byteToCellEnd 末项语义一致（慢路注释的等价关系在此成为定义）。
                cellStart = static_cast<std::int32_t>(pos);
                cellEnd   = static_cast<std::int32_t>(pos + needle.size());
            } else {
                // 末字节所在单元的末格之后一格：命中尾落在单元字节中段时
                // 同样归并整格（规格 5.2），不产零宽区间；match 末尾恰好
                // 对齐单元边界时等价于 byteToCell[pos + needle.size()]。
                cellStart = m.byteToCell[pos];
                cellEnd   = m.byteToCellEnd[pos + needle.size() - 1];
            }
            out.push_back(ZzLogicalRange{{logicalLine, cellStart}, {logicalLine, cellEnd}});
            pos += needle.size(); // 命中不重叠：从 match 末尾继续
        }
```

- [ ] **步骤 4：构建 + 全量单测（行为零变化的回归保障）**

```bash
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

预期：50/50。搜索语义用例（大小写折叠、宽格、聚簇、行尾修剪、命中不重叠）全在既有套件内，任何红说明快路与慢路不等价，停下修复。另抽查搜索相关测试单独跑一遍：`ctest --preset linux-gcc-debug -R "search|Search" --output-on-failure`。

- [ ] **步骤 5：本机基线回归**

```bash
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
```

预期：40/40、50/50、2/2、exit 0 零警告。

- [ ] **步骤 6：快路生效粗测（正式复测在任务 2，此处只验证快路真的走了）**

```bash
cmake --build build/linux-gcc-debug --target zz_bench_feed
./build/linux-gcc-debug/tests/zz_bench_feed --profile=ascii --tier=100k
```

预期：exit 0 且 JSON 的 search_ms 较 M8b 同档（557.97ms）显著下降（量级预期一半以下）；若几乎不变说明快路未命中预检，停下排查预检条件，不得带病进任务 2。

- [ ] **步骤 7：Commit**

```bash
git add src/terminal/ZzSearch.cpp
git commit -m "perf(search): M10 buildLineText 行级 ASCII 快路（预检批量提取 + 恒等回映免建表，慢路原样回退）"
```

---

### 任务 2：bench 矩阵复测 + 入库 + probe 续表 + 门控判定

**文件：**
- 创建（复测产物重命名入库）：`tests/perf/records/2026-09-29-m10-*.json`（九份）
- 修改：`docs/superpowers/specs/2026-09-22-m8-million-line-probe.md`（文末续 M10 节）

- [ ] **步骤 1：全矩阵复测**

```bash
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug -R zz_bench
cmake --build build/linux-gcc-debug --target bench-long
```

预期：短跑三份 + 长跑六份全部 exit 0；产物在 build/linux-gcc-debug/tests/ 下（文件名规则与 M8b 相同，zzterm-bench-*.json）。

- [ ] **步骤 2：产物清点与门控预判**

清点九份 JSON：search_matches==lines 自洽；feed-ascii-1m 与 feed-mixed-1m 的 search_ms 对照 5000ms 门预判。若 mixed 仍超门：按规格 §4 回退路径——停止本任务后续步骤，状态报 DONE_WITH_CONCERNS 并附九份数据摘录，由主代理回报用户裁定增量索引；ascii 已达标部分的数据仍可入库（如实记录）。

- [ ] **步骤 3：重命名入库**

```bash
cd tests/perf/records
# 按既有惯例重命名为 2026-09-29-m10-{scrollback,feed-ascii,feed-mixed}-{10k,100k,1m}.json
git add tests/perf/records/
git commit -m "test(perf): M10 搜索快路复测矩阵入库（九份，门控判定见 probe 续表）"
```

- [ ] **步骤 4：probe 文档续 M10 节**

在 docs/superpowers/specs/2026-09-22-m8-million-line-probe.md 文末追加（数值以实测填充，禁留占位；doxygen 陷阱：行内 code span 禁尖括号/点开头/井号预处理词/反斜杠/双冒号）：

```markdown
## 7. M10 搜索快路复测对照（2026-09-29，contour 分支）

- 变更：buildLineText 行级 ASCII 快路（src/terminal/ZzSearch.cpp，单一 commit）
- preset 与机器：同文件头（linux-gcc-debug -O0，同机）
- 数据源：tests/perf/records/2026-09-29-m10-*.json（九份；JSON 内 milestone 字段为 harness 硬编码 M8，以文件名前缀区分波次）
- 对照基准：§6 M8b 复测（tests/perf/records/2026-09-28-m8b-*.json 九份）

### 7.1 门控项前后对照（1m 档 search ms）

〈表：ascii/mixed 两画像 M8b -> M10，降幅百分比〉

### 7.2 门控判定

〈逐项：search 双画像对 5s 门的判定；若双达标——M8 规格 §4 门控五项（RSS 两口径、append、search、reflow、feed 记录项）至此全部达标/记录落账的收尾声明；若 mixed 未达标——如实记录并注明已按 M10 规格 §4 移交用户裁定增量索引〉

### 7.3 全矩阵对照

〈九行表：档位 x 画像，search ms 前 -> 后；附 append/feed/reflow 顺带变化简注（预期不变，若有显著波动如实记录）〉
```

```bash
doxygen Doxyfile  # exit 0 零警告后
git add docs/superpowers/specs/2026-09-22-m8-million-line-probe.md
git commit -m "docs(probe): M10 搜索快路复测对照与门控判定（search 5s 门达标落账）"
```

（若 mixed 未达标，commit message 改为如实表述，不得写"达标落账"。）

- [ ] **步骤 5：push 并确认 CI 六 workflow 绿**

```bash
git push origin contour
gh run list --branch contour --limit 6
```

预期：六 workflow 全 success（搜索为双后端共用路径，macOS 双 job 同步回归）。红则诊断修复复推。

---

### 任务 3：慢路批量化 + 回映惰性化（方案 A，规格 §7 修正后插入；任务 2 在本任务完成后续跑）

背景：任务 2 首次执行命中规格 §4 回退分支——行级快路对 mixed 画像命中率 0%（负载生成器每逻辑行含 CJK），mixed 1M 10191.0ms 仍超门。用户裁定方案 A 先行（规格 §7 修正记录）。本任务把行级双路径改为单一路径：段批量 + 惰性回映。

**文件：**
- 修改：`src/terminal/ZzSearch.cpp`（LineTextMap、buildLineText、zzSearchLines，即任务 1 刚改过的三处）

- [ ] **步骤 1：LineTextMap 精简**

删除 byteToCell / byteToCellEnd / fastIdentity 三个成员，只留 text 与 folded：

```cpp
struct LineTextMap {
    std::string text;   // 行尾空白已修剪
    std::string folded; // ASCII 折叠副本（仅不敏感模式构建）
};
```

- [ ] **步骤 2：buildLineText 单一路径化（段批量）**

移除任务 1 加入的行级预检与快路分支（含 fastIdentity 置位与提前 return）。慢路主体改造：逐 cell 循环内，当前 cell 满足段条件（宽度 Empty，或宽度 Narrow 且非聚簇且码点不超过 127）时进入**段批量内循环**——连续消费满足条件的 cell，text/folded 直接下标写入（先按 cols*rowCount 上限 resize，结束截断），Empty 与码点 0 窄格写空格；遇到不满足的 cell 落回原 emit 分支（宽格/聚簇/多字节码点逐字处理）。行尾修剪沿用原规则（text/folded 同步 pop）。两路格步进规则与原慢路一致（宽格 lead 计 2 格、续格跳过、聚簇整串 1 格）——但注意：格偏移信息不再入表，buildLineText 只产文本；**不再维护任何回映数据**。

实现要点：emit lambda 不再需要 byteToCell/byteToCellEnd 的 push（只写 text/folded）；ZzLine line 声明位置维持任务 1 现状；函数注释更新（说明段批量与惰性回映的新约定）。

- [ ] **步骤 3：命中换算惰性化**

zzSearchLines 的命中构造改为调用新 helper（匿名命名空间内）：

```cpp
// 惰性回映（M10 方案 A）：命中后遍历该逻辑行的 cells 同步累计字节数，
// 定位 pos 所在单元的格偏移（cellStart）与末字节所在单元末格之后一格（cellEnd）。
// 格步进规则与文本构建完全一致：续格跳过（无字节）、聚簇整串 1 格、
// WideLead 2 格、其余 1 格；Empty 与码点 0 窄格贡献 1 字节空格。
void zzCellRangeForMatch(const ZzIPhysicalLineSource& src,
                         std::size_t firstRow, std::size_t rowCount,
                         std::size_t pos, std::size_t needleLen,
                         std::int32_t& cellStart, std::int32_t& cellEnd);
```

语义必须与任务 1 前两表逐点一致：cellStart = pos 字节所在单元的起始格偏移；cellEnd = pos+needleLen-1 字节所在单元的"末格之后一格"（宽格 lead 为 +2、其余 +1、聚簇 +1）；多字节码点的中段字节归属其所在单元（即同一 cellStart）。命中区间不得为零宽。

zzSearchLines 命中循环改为：

```cpp
        while ((pos = hay.find(needle, pos)) != std::string::npos) {
            std::int32_t cellStart = 0;
            std::int32_t cellEnd   = 0;
            zzCellRangeForMatch(src, row, count, pos, needle.size(), cellStart, cellEnd);
            out.push_back(ZzLogicalRange{{logicalLine, cellStart}, {logicalLine, cellEnd}});
            pos += needle.size(); // 命中不重叠：从 match 末尾继续
        }
```

（原快路/慢路双分支换算整体删除。）

- [ ] **步骤 4：全量单测 + 基线回归**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
ctest --preset linux-gcc-debug -R "search|Search" --output-on-failure
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
```

预期：50/50、40/40、50/50、2/2、exit 0 零警告。搜索语义用例全绿即惰性换算与原两表等价的回归证据；任何红停下修复。

- [ ] **步骤 5：双画像粗测（防呆）**

```bash
./build/linux-gcc-debug/tests/zz_bench_feed --profile=ascii --tier=100k
./build/linux-gcc-debug/tests/zz_bench_feed --profile=mixed --tier=100k
```

预期与判据：mixed 100k 的 search_ms 较 M8b 基准（970.51ms）**显著下降**（段批量与免建表对 mixed 全行生效；若几乎不变说明段批量未命中，停下排查）；ascii 100k 较任务 1 的 337.8ms 不明显退化（段批量对纯 ASCII 行应等价覆盖行级快路收益，且省掉预检第二遍扫描）。两档 search_matches==lines 自洽。

- [ ] **步骤 6：Commit**

```bash
git add src/terminal/ZzSearch.cpp
git commit -m "perf(search): M10 方案 A 慢路批量化 + 命中回映惰性化（单一路径替代行级双路径，规格 §7 修正）"
```

完成后回到任务 2 从步骤 1 续跑（复测矩阵 -> 门控判定 -> 入库 -> probe 续表 -> CI）。

---

## 自检结论

- 规格覆盖：§2 改动点 1/2 = 任务 1 步骤 2/3，改动点 3（零公开头零 CMake）= 任务 1 文件面；§3 门控与复测入库 = 任务 2 步骤 1-4，CI 绿 = 任务 2 步骤 5；§4 回退路径 = 任务 2 步骤 2 分支（已触发一次，产出任务 3）；§7 修正记录（方案 A）= 任务 3；§6 记录 = 任务 2 步骤 4，finishing 不在计划内（收尾另行）。
- 占位符：任务 2 步骤 4 的 〈〉 段为实测填充指令，非实现缺口。
- 类型一致性：任务 3 将 LineTextMap 精简为 text/folded 两成员（任务 1 引入的 fastIdentity 与两表随之移除），zzCellRangeForMatch 的参数与语义在步骤 3 定义、zzSearchLines 在同步骤消费，一致。
