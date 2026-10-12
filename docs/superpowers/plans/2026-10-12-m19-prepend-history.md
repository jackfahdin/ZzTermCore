# M19 历史最旧端注入原语 实现计划

> **勘误指针（2026-10-12）：** 本计划多处写的「基线 = totalDropped + totalPrepended」加法口径在实现期被证实与「绝对行号稳定」目标矛盾，已按规格勘误 E-2 改为**减法口径**（基线 = totalDropped − totalPrepended + 归还额度不变量）；计划正文保留时点原样，以规格文件为准。另：prependOldest 返回 bool（额度不足拒绝），prepend 成功路径对选区/搜索锚点施加正移（onLinesPrepended）。

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 给 ZzTermCore 加「向 Primary 历史最旧端注入行」原语（ZzTerminal::prependHistoryLines），供应用侧历史读回桥使用；绝对行号体系对既有行零扰动。

**架构：** ZzScrollback 新增纯虚 prependOldest（ChunkedScrollback 头部插入，维持 phys=headOffset_+index 定长寻址不变量）→ ZzTerminalBackend 新增非纯虚默认不支持 → ZzNativeBackend 实现（Alternate 拒绝 + generation 递增）→ ZzTerminal 公共出口。绝对行号基线改为 totalDropped+totalPrepended。

**技术栈：** C++20，无新依赖；测试为自带 ZZ_TEST_EXPECT 轻量断言（参照 tests/unit/test_scrollback.cpp）。

**规格：** `docs/superpowers/specs/2026-10-12-m19-prepend-history-design.md`

**仓库/基线：** ZzTermCore，contour 分支 @ 427df20；测试基线：linux-gcc-debug 63/63（`ctest --test-dir build/linux-gcc-debug --output-on-failure`；构建 `cmake -S . -B build/linux-gcc-debug -DCMAKE_BUILD_TYPE=Debug && cmake --build build/linux-gcc-debug -j$(nproc)`）。另有 m2-off-check（52）、m2-shared-check（63）两个构建目录需同步跑。commit 格式 `type(scope): 中文描述`。doxygen 注释口径：不用反引号+前导 `::`（CI 1.9.8 误判显式链接）。

---

## 文件结构

| 文件 | 职责 |
| --- | --- |
| `include/ZzTerm/Scrollback.h` | ZzScrollback 新增纯虚 prependOldest；ZzScrollbackStats 新增 totalPrepended |
| `src/history/ChunkedScrollback.cpp` | prependOldest 头部插入实现 |
| `src/backend/ZzTerminalBackend.h` / `.cpp` | prependHistoryLines 非纯虚默认 false |
| `src/backend/native/ZzNativeBackend.{h,cpp}` | 覆写：Alternate 拒绝、转发 scrollback、generation 递增 |
| `src/backend/native/ZzNativeHistoryView.cpp`、`ZzNativeLineSource.cpp` | droppedLineCount 基线 = totalDropped + totalPrepended |
| `include/ZzTerm/Terminal.h`、`src/terminal/Terminal.cpp` | ZzTerminal::prependHistoryLines 公共出口 |
| `tests/unit/test_scrollback.cpp`、`tests/unit/test_terminal_*.cpp`（按既有分文件惯例选或新建 test_prepend_history.cpp） | 行为测试 |
| `docs/API.md` | 版本节 M19 条目 |

---

## 任务 1：prependOldest 原语 + Terminal 公共出口（单任务，TDD）

**文件：** 见上表（全部）。

- [ ] **步骤 1：编写失败的测试（scrollback 级）**

在 `tests/unit/test_scrollback.cpp` 追加（沿用该文件 makeLine/lineText/ZZ_TEST_EXPECT 脚手架；若行数膨胀明显，新建 test_prepend_history.cpp 并在 tests CMakeLists 注册——二选一，与既有文件组织惯例一致）：

```cpp
// M19.1 注入顺序与寻址：基本情形
static void testPrependOldestOrder()
{
    auto sb = zzCreateChunkedScrollback(100);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "c1", false));
    batch.push_back(makeLine(4, "c2", false));
    sb->append(std::move(batch));
    std::vector<ZzLine> older;
    older.push_back(makeLine(4, "o1", false));
    older.push_back(makeLine(4, "o2", false));
    older.push_back(makeLine(4, "o3", false));
    sb->prependOldest(std::move(older));
    ZZ_TEST_EXPECT(sb->lineCount() == 5);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "o1"); // 最旧端
    ZZ_TEST_EXPECT(lineText(sb->lineAt(2), 2) == "o3");
    ZZ_TEST_EXPECT(lineText(sb->lineAt(3), 2) == "c1"); // 原历史顺移
    ZZ_TEST_EXPECT(lineText(sb->lineAt(4), 2) == "c2");
    const ZzScrollbackStats st = sb->stats();
    ZZ_TEST_EXPECT(st.totalPrepended == 3);
    ZZ_TEST_EXPECT(st.totalAppended == 2);  // 注入不算 append
    ZZ_TEST_EXPECT(st.totalDropped == 0);
}

// M19.2 部分裁剪态 + 注入量超过头块空闲槽（跨块前插）
static void testPrependOldestAcrossChunks()
{
    auto sb = zzCreateChunkedScrollback(1000);
    // 先 append 260 行再裁出头块一部分：append 两批并用小容量造部分裁剪
    // （造 headOffset_ 非零的最简路径：append 300 行到容量 300，再 append
    //   10 行裁掉最旧 10 行 → headOffset_ = 10）
    std::vector<ZzLine> big;
    for (int i = 0; i < 300; ++i)
        big.push_back(makeLine(4, "a" + std::to_string(i % 10), false));
    sb->append(std::move(big));
    std::vector<ZzLine> more;
    for (int i = 0; i < 10; ++i)
        more.push_back(makeLine(4, "b" + std::to_string(i), false));
    sb->append(std::move(more)); // 触发裁剪，headOffset_ 变为非零
    const std::size_t before = sb->lineCount();
    const std::string firstBefore = lineText(sb->lineAt(0), 2);
    // 注入 300 行（远超 headOffset_ 空闲槽，必须前插新块）
    std::vector<ZzLine> older;
    for (int i = 0; i < 300; ++i)
        older.push_back(makeLine(4, "z" + std::to_string(i % 10), false));
    sb->prependOldest(std::move(older));
    ZZ_TEST_EXPECT(sb->lineCount() == before + 300);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "z0");
    ZZ_TEST_EXPECT(lineText(sb->lineAt(299), 2) == "z9");
    ZZ_TEST_EXPECT(lineText(sb->lineAt(300), 2) == firstBefore); // 原最旧行顺移后仍衔接
    ZZ_TEST_EXPECT(sb->stats().totalPrepended == 300);
}

// M19.3 注入不触发裁剪；后续 append 先裁注入行
static void testPrependOldestCapacityDeferred()
{
    auto sb = zzCreateChunkedScrollback(4);
    std::vector<ZzLine> batch;
    for (int i = 0; i < 4; ++i)
        batch.push_back(makeLine(4, "c" + std::to_string(i), false));
    sb->append(std::move(batch)); // 满容量 4
    std::vector<ZzLine> older;
    older.push_back(makeLine(4, "o1", false));
    older.push_back(makeLine(4, "o2", false));
    sb->prependOldest(std::move(older));
    ZZ_TEST_EXPECT(sb->lineCount() == 6); // 瞬时超容量，不裁
    std::vector<ZzLine> next;
    next.push_back(makeLine(4, "n1", false));
    next.push_back(makeLine(4, "n2", false));
    sb->append(std::move(next)); // append 触发裁剪回容量 4
    ZZ_TEST_EXPECT(sb->lineCount() == 4);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "c0"); // 注入行 o1/o2 先被裁
    ZZ_TEST_EXPECT(lineText(sb->lineAt(3), 2) == "n1");
    const ZzScrollbackStats st = sb->stats();
    ZZ_TEST_EXPECT(st.totalPrepended == 2);
    ZZ_TEST_EXPECT(st.totalDropped == 4); // 裁掉的 2 注入行 + 2 原行
}

// M19.4 零容量：即注即裁
static void testPrependOldestZeroCapacity()
{
    auto sb = zzCreateChunkedScrollback(0);
    std::vector<ZzLine> older;
    older.push_back(makeLine(4, "o1", false));
    sb->prependOldest(std::move(older));
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    ZZ_TEST_EXPECT(sb->stats().totalDropped == 1);
    ZZ_TEST_EXPECT(sb->stats().totalPrepended == 1);
}

// M19.5 注入后 reflow 顺序保持（变列重组覆盖注入行）
static void testPrependOldestReflow()
{
    auto sb = zzCreateChunkedScrollback(100);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "abcd", true));
    batch.push_back(makeLine(4, "efgh", false));
    sb->append(std::move(batch));
    std::vector<ZzLine> older;
    older.push_back(makeLine(4, "wxyz", true));
    older.push_back(makeLine(4, "1234", false));
    sb->prependOldest(std::move(older));
    sb->reflow(8);
    ZZ_TEST_EXPECT(sb->lineCount() == 2);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 8) == "wxyz1234");
    ZZ_TEST_EXPECT(lineText(sb->lineAt(1), 8) == "abcdefgh");
}
```

main 函数注册这些用例（沿用该文件既有 main 聚合方式）。

- [ ] **步骤 2：编写失败的测试（Terminal 级）**

新建或并入既有 terminal 测试文件（参照 tests/unit/ 里 M14/M16 的 Terminal 级测试组织）：

```cpp
// M19.6 绝对行号稳定 + generation 递增 + 屏幕不动
static void testTerminalPrependHistoryAbsoluteStable()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    // 喂 5 行产生 2 行历史（3 行屏幕装不下）
    for (int i = 0; i < 5; ++i)
        term.feed(std::as_bytes(std::span("line\r\n", 6)));
    const auto& hv = term.historyView();
    const std::size_t histBefore = hv.lineCount();
    const std::uint64_t droppedBefore = hv.droppedLineCount();
    const std::uint64_t genBefore = hv.generation();
    const ZzCursorState cursorBefore = term.cursor();
    const std::size_t screenRow0Hash = /* 记录屏幕首行文本 */;
    // 注入 2 行
    std::vector<ZzLine> older;
    older.push_back(makeLine(10, "older-one", false));
    older.push_back(makeLine(10, "older-two", false));
    ZZ_TEST_EXPECT(term.prependHistoryLines(std::move(older)) == true);
    ZZ_TEST_EXPECT(hv.lineCount() == histBefore + 2);
    ZZ_TEST_EXPECT(hv.droppedLineCount() == droppedBefore + 2); // 基线平移
    // 既有行绝对行号不变：注入前 idx 0 的绝对号 == 注入后 idx 2 的绝对号
    ZZ_TEST_EXPECT(droppedBefore + 0 == hv.droppedLineCount() - 2 + 2);
    ZZ_TEST_EXPECT(hv.generation() > genBefore);
    // 屏幕与光标不动
    ZZ_TEST_EXPECT(term.cursor().position.row == cursorBefore.position.row);
    ZZ_TEST_EXPECT(term.cursor().position.col == cursorBefore.position.col);
    /* 断言屏幕首行文本与注入前一致 */
}

// M19.7 Alternate 拒绝
static void testTerminalPrependHistoryAlternateRejected()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    term.feed(std::as_bytes(std::span("\x1b[?1049h", 8)));
    std::vector<ZzLine> older;
    older.push_back(makeLine(10, "older", false));
    ZZ_TEST_EXPECT(term.prependHistoryLines(std::move(older)) == false);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // Alternate 恒 0
}

// M19.8 空输入 true 空操作
static void testTerminalPrependHistoryEmpty()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    ZZ_TEST_EXPECT(term.prependHistoryLines({}) == true);
}
```

（makeLine 辅助若该文件没有就从 test_scrollback.cpp 抄一份静态副本——各测试文件自带脚手架是既有惯例。）

- [ ] **步骤 3：运行测试验证失败**

运行：`cmake --build build/linux-gcc-debug -j$(nproc) 2>&1 | tail -5`
预期：编译失败，`prependOldest`/`prependHistoryLines`/`totalPrepended` 不存在。

- [ ] **步骤 4：实现 ZzScrollback::prependOldest + stats 字段**

`include/ZzTerm/Scrollback.h`：

1. `ZzScrollbackStats` 追加（totalDropped 之后，附 doxygen 注释）：

```cpp
    std::uint64_t totalPrepended = 0; ///< 累计经 prependOldest 注入的行数（M19）。
```

行序约定注释区补一句：绝对行号基线 = totalDropped + totalPrepended（M19）。

2. `ZzScrollback` 追加纯虚（takeNewest 声明之后）：

```cpp
    /**
     * @brief 向最旧端注入一批行（M19 读回注入原语；旧到新顺序，以值移交所有权）。
     * @param lines 待注入行（行宽必须等于终端当前列宽，同 append 约定）。
     * @note 注入不触发容量裁剪（允许瞬时超容量；后续 append 的裁剪从最旧端
     *       先裁注入行）。capacity==0 时即注即裁（计入 totalDropped）。
     * @note 注入使 stats().totalPrepended 同步增加；HistoryView 的
     *       droppedLineCount 基线为 totalDropped+totalPrepended，故既有行
     *       绝对行号（基线+index）在注入前后不变。
     * @note totalAppended 不变（注入不是 append）；调用后既有 lineAt
     *       引用失效（同 append 失效规则）。
     */
    virtual void prependOldest(std::vector<ZzLine> lines) = 0;
```

3. `src/history/ChunkedScrollback.cpp` 实现（ZzChunkedScrollback 类内）：

```cpp
    void prependOldest(std::vector<ZzLine> lines) override
    {
        const std::size_t n = lines.size();
        if (n == 0)
            return;
        totalPrepended_ += n;
        if (capacity_ == 0) { // 即注即裁（等价 append 的零容量语义）
            totalDropped_ += n;
            return;
        }
        // 头部插入，维持 phys = headOffset_ + index 定长寻址不变量：
        // 空闲槽足够则回填头块；不足则前插整块再回填。
        if (n > headOffset_) {
            const std::size_t need = n - headOffset_;
            const std::size_t newChunks =
                (need + kChunkLines - 1) / kChunkLines;
            for (std::size_t i = 0; i < newChunks; ++i)
                chunks_.emplace_front();
            headOffset_ += newChunks * kChunkLines;
        }
        headOffset_ -= n;
        // 按寻址公式逐行写入 [headOffset_, headOffset_+n)
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t phys  = headOffset_ + i;
            const std::size_t chunk = phys / kChunkLines;
            const std::size_t inner =
                (phys % kChunkLines) - (chunk == 0 ? headOffset_ : 0);
            approxBytes_ += sizeof(ZzLine) +
                            static_cast<std::size_t>(lines[i].cellCount()) *
                                sizeof(ZzCell);
            chunks_[chunk][inner] = std::move(lines[i]);
        }
        totalLines_ += n;
        // 不调用 trimToCapacity：注入允许瞬时超容量（规格定案 3）
    }
```

类成员追加 `std::uint64_t totalPrepended_ = 0;`；`stats()` 填充新字段。

实现注意：写入用寻址公式与 lineAt 完全同构；新前插块是空 vector，`chunks_[chunk]` 需要 resize 到相应槽位（新块先 `resize(kChunkLines)` 或按 inner 扩容——实现时选其一并在注释说明；旧头块前段槽位已存在）。验证手段就是步骤 1 的跨块用例。

- [ ] **步骤 5：实现记账调整 + 后端 + Terminal 出口**

1. `src/backend/native/ZzNativeHistoryView.cpp` droppedLineCount（:39-42）与 `src/backend/native/ZzNativeLineSource.cpp` droppedLineCount（:45-48）：返回 `stats().totalDropped + stats().totalPrepended`，注释更新为「绝对行号基线（含 M19 注入计数）」。
2. `src/backend/ZzTerminalBackend.h` 追加（historyView() 声明之后）：

```cpp
    /// 向 Primary 历史最旧端注入行（M19）；默认不支持（返回 false）。
    [[nodiscard]] virtual bool prependHistoryLines(std::vector<ZzLine> lines);
```

`src/backend/ZzTerminalBackend.cpp` 追加默认实现：

```cpp
bool ZzTerminalBackend::prependHistoryLines(std::vector<ZzLine> lines)
{
    (void)lines;
    return false; // 默认不支持（contour 后端暂不提供）
}
```

（顶部 include 需要 `<vector>` 与 ZzTerm/Line.h——按该文件既有 include 惯例补。）

3. `src/backend/native/ZzNativeBackend.h/.cpp` 覆写：

```cpp
bool ZzNativeBackend::prependHistoryLines(std::vector<ZzLine> lines)
{
    if (screen_.activeBuffer() == ZzScreenBuffer::Alternate)
        return false; // Alternate 永不注入（与 prependPrimaryLines 同口径）
    if (lines.empty())
        return true; // 空输入空操作
    scrollback_->prependOldest(std::move(lines));
    ++historyGeneration_; // M19：注入代计数递增（同 append/reflow 口径）
    return true;
}
```

4. `include/ZzTerm/Terminal.h`（historyView() 声明附近）与 `src/terminal/Terminal.cpp`：

```cpp
    /**
     * @brief 向 Primary 历史最旧端注入一批行（M19；应用侧历史读回用）。
     * @param lines 待注入行（旧到新顺序，以值移交所有权；行宽须等于
     *        终端当前列宽）。
     * @return false 表示拒绝（Alternate 屏活动期间或后端不支持）；
     *         空输入为 true 空操作。
     * @note 屏幕区与光标完全不动（区别于 ZzScreen::prependPrimaryLines
     *       的接缝归还语义）；注入经 HistoryView generation() 递增对
     *       前端可见；绝对行号基线平移使既有行号稳定（规格
     *       2026-10-12-m19-prepend-history-design.md 定案 2）。
     */
    [[nodiscard]] bool prependHistoryLines(std::vector<ZzLine> lines);
```

Terminal.cpp 实现为转发 `backend_->prependHistoryLines(std::move(lines))`（成员名以该文件既有后端成员为准）。

- [ ] **步骤 6：运行测试验证通过**

```bash
cmake --build build/linux-gcc-debug -j$(nproc) && ctest --test-dir build/linux-gcc-debug --output-on-failure
```

预期：scrollback/terminal 新用例全过，全量 63/63 + 新增用例绿（ctest 条目数视新文件是否单列 +1）。

- [ ] **步骤 7：docs/API.md 版本节补 M19 条目**

按该文件既有里程碑条目格式追加：公共面新增
（ZzTerminal::prependHistoryLines / ZzScrollback::prependOldest /
ZzScrollbackStats.totalPrepended）+ droppedLineCount 口径变化说明
（基线 = totalDropped + totalPrepended）。

- [ ] **步骤 8：三构建目录全量验证 + doxygen**

```bash
ctest --test-dir build/linux-gcc-debug --output-on-failure
cmake --build build/m2-off-check -j$(nproc) && ctest --test-dir build/m2-off-check --output-on-failure
cmake --build build/m2-shared-check -j$(nproc) && ctest --test-dir build/m2-shared-check --output-on-failure
# doxygen 零警告（若仓库有 doxygen 目标/脚本按惯例跑）
```

预期：63/63、52/52、63/63（+新增条目），doxygen 无新警告。

- [ ] **步骤 9：Commit**

```bash
git add include src tests docs/API.md
git commit -m "feat(history): M19 历史最旧端注入原语（prependHistoryLines/prependOldest/totalPrepended）"
```

---

## 自检记录

- **规格覆盖**：定案 1（步骤 5-3/5-4 + 测试 M19.7/M19.8）、定案 2（步骤 4 stats + 步骤 5-1 + 测试 M19.6）、定案 3（步骤 4 + 测试 M19.3/M19.4）、定案 4（行宽约定注释 + 测试 M19.5）、定案 5（步骤 5-3 generation + 测试 M19.6）、定案 6（规格备注，桥侧职责不在本计划）。
- **命名一致**：prependOldest / prependHistoryLines / totalPrepended 全程一致。
- **风险**：ChunkedScrollback 头部插入是本计划唯一有算法复杂度的点，跨块用例（M19.2）是其判据；实现者对「新块空 vector 扩容写入」的具体手法可在注释口径内自决。
