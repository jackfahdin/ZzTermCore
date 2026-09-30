# M16b 列变 reflow 接缝链统一重组（接缝链归还屏幕）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 列变 reflow 前检测跨历史/屏幕接缝链（历史末行 wrapped=true），摘除历史尾链归还屏幕顶部统一重组，消除接缝劈链，任意缩列往返布局完整对齐 contour。

**架构：** ZzScreen 新增 prependPrimaryLines 原语（顶部插行、瞬时超行由 reflowBuffer 溢出裁回）；ZzNativeBackend::resize 列变分支在历史 reflow 前插入「检测→takeNewest 摘除→prepend 归还」协调；复用既有单一重组路径与溢出机制，零新重组算法。

**技术栈：** C++20、CMake（tests GLOB 自动收编）。

**规格：** docs/superpowers/specs/2026-09-30-m16b-seam-chain-reflow-design.md（已审定）

**基线命令（ZzTermCore 仓根目录）：**
- `cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`（现状 55/55）
- `ctest --test-dir build/m2-off-check`（44/44）、`ctest --test-dir build/m2-shared-check`（55/55）
- `doxygen Doxyfile`（exit 0 零警告）
- 本里程碑只改既有测试文件不新增测试目标，三套计数全程不变。

---

## 文件结构

**创建：** 无（全部为修改既有文件）。

**修改（任务 1）：**
- `include/ZzTerm/Screen.h`（reflow 声明后）— prependPrimaryLines 声明
- `src/screen/Screen.cpp`（reflow 实现附近）— prependPrimaryLines 实现
- `tests/unit/test_screen_reflow.cpp` — 新增 2 个原语用例 + main 注册

**修改（任务 2）：**
- `src/backend/native/ZzNativeBackend.cpp`（resize :249-253）— 接缝链检测/摘除/归还
- `tests/unit/test_native_reflow.cpp` — 跨缝往返 facade 用例 + main 注册
- `tests/unit/test_backend_compat.cpp` — 跨缝 compat 用例 22 + main 注册
- `tests/unit/test_terminal_selection.cpp` — 选区跨缝文本不变用例 + main 注册
- `include/ZzTerm/Scrollback.h`（固有边界注释 :90-92）、`include/ZzTerm/HistoryView.h`（文件头分域注释）、`include/ZzTerm/Terminal.h`（resize 注释）、`docs/API.md`（resize 段 + 版本节）、`docs/superpowers/specs/2026-09-29-m13-spike-record.md`（§4 现象 5）

---

## 任务 1：ZzScreen::prependPrimaryLines 原语

**文件：** 见上「修改（任务 1）」。

- [ ] **步骤 1：编写失败的测试（test_screen_reflow.cpp 追加）**

文件 include 区若无 vector/string 则保持现状（已有）。在文件尾部（main 前）新增帮手与两用例：

```cpp
// 构造 cols 列、内容为 text、wrapped 置链标记的行（M16b prepend 用例用）。
static ZzLine makeChainLine(int cols, const std::string& text, bool wrapped)
{
    ZzLine line(cols);
    for (int i = 0; i < (int)text.size() && i < cols; ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint((char32_t)text[(std::size_t)i]);
        line.setCell(i, c);
    }
    line.setWrapped(wrapped);
    return line;
}

// 6. prependPrimaryLines：顶部插行 + 光标平移 + reflow 统一重组溢出裁回（M16b）
static void testPrependPrimaryLines()
{
    ZzScreen scr(4, 2);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    writeRow(scr, 0, "ef");
    writeRow(scr, 1, "gh");
    scr.setCursorPosition(ZzPosition{1, 0});
    std::vector<ZzLine> head;
    head.push_back(makeChainLine(4, "ab", true)); // 跨缝链头，wrapped 续接旧首行
    head.push_back(makeChainLine(4, "cd", true));
    scr.prependPrimaryLines(std::move(head));      // 瞬时 4 行（超 rows_）

    scr.reflow(2); // 链 ab/cd/ef（6 格）-> ab/cd/ef 3 行 + gh 1 行 = 4 行，溢出 2 行
    ZZ_TEST_EXPECT(spilled.size() == 2);
    ZZ_TEST_EXPECT(rowText(spilled[0], 2) == "ab");
    ZZ_TEST_EXPECT(spilled[0].wrapped());          // 链被裁在缝上但链接连续（合法跨缝态）
    ZZ_TEST_EXPECT(rowText(spilled[1], 2) == "cd");
    ZZ_TEST_EXPECT(spilled[1].wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "ef");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());       // 屏幕首行续接 gh
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "gh");
    ZZ_TEST_EXPECT(scr.size().rows == 2);          // reflow 出口恒 rows_
    ZZ_TEST_EXPECT(scr.cursor().position.row >= 0 && scr.cursor().position.row < 2);

    // 空向量空操作。
    const auto gen = scr.dirtyGeneration();
    scr.prependPrimaryLines({});
    ZZ_TEST_EXPECT(scr.dirtyGeneration() == gen);
}

// 7. prepend 的跨缝链经 reflow 统一重组：变大接回（M16b 核心场景 Screen 级）
static void testPrependSeamChainRejoins()
{
    ZzScreen scr(4, 3);
    writeRow(scr, 0, "il"); // 模拟缩列后屏幕首行（跨缝链尾）
    writeRow(scr, 1, "xy");
    std::vector<ZzLine> head;
    head.push_back(makeChainLine(4, "ta", true)); // 历史尾链（摘除归还），wrapped 续接
    scr.prependPrimaryLines(std::move(head));
    scr.reflow(8); // 变大：链 ta/il 接回为 "tail" 一行
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 4) == "tail");
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "xy");
}

// 8. prepend 只作用 primary：Alternate 缓冲不受影响（M16b 按缓冲区分）
static void testPrependPrimaryOnly()
{
    ZzScreen scr(4, 2);
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    writeRow(scr, 0, "aa");
    scr.setActiveBuffer(ZzScreenBuffer::Primary);
    writeRow(scr, 0, "pp");
    std::vector<ZzLine> head;
    head.push_back(makeChainLine(4, "hh", false));
    scr.prependPrimaryLines(std::move(head)); // alt 非活动期间也作用 primary（规格 §2 定案 3）
    scr.reflow(4); // 注意：同宽 reflow 为空操作，改用列变验证——此处仅验证 prepend 本身
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "aa"); // alternate 内容不变
    ZZ_TEST_EXPECT(scr.size().rows == 2);
}
```

main() 注册三行（既有注册块末尾、`if (g_failures...` 前）：

```cpp
    testPrependPrimaryLines();
    testPrependSeamChainRejoins();
    testPrependPrimaryOnly();
```

注意（审查锚点）：testPrependPrimaryOnly 中的 `scr.reflow(4)` 是同宽空操作（reflow 入口早退），仅用于验证 prepend 本身不破坏尺寸与 alternate 内容，保留该行并保留注释说明，不得删除后另写列变（列变归任务 2 facade 覆盖）。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_screen_reflow 2>&1 | tail -5`
预期：编译失败，报错含 `prependPrimaryLines` 不是 ZzScreen 的成员。

- [ ] **步骤 3：Screen.h 声明**

`include/ZzTerm/Screen.h` 在 `reflow` 声明（:93 区域）后插入：

```cpp
    /**
     * @brief 把若干行插入 Primary 缓冲区顶部（Core 内部使用；M16b 接缝链归还）。
     * @param lines 待插入行（以值移交所有权，旧到新顺序）。
     * @note 插入后行数可瞬时超过 rows_——由随后的 reflow() 溢出分支裁回
     *       （reflowBuffer 出口恒 rows_）；cursor.position.row 随插入数平移；
     *       wrapPending 清除；dirty 状态由随后的 reflow 全屏标脏自洽。
     * @note 仅作用于 primary_（与当前活动缓冲无关）；Alternate 永不插入。
     *       插入后到 reflow() 之间不得穿插写入路径调用（单线程约定）。
     */
    void prependPrimaryLines(std::vector<ZzLine> lines);
```

- [ ] **步骤 4：Screen.cpp 实现**

`src/screen/Screen.cpp` 在 `ZzScreen::reflow` 实现（:112-124）后插入：

```cpp
void ZzScreen::prependPrimaryLines(std::vector<ZzLine> lines)
{
    if (lines.empty())
        return;
    const auto count = static_cast<int>(lines.size());
    primary_.lines.insert(primary_.lines.begin(),
                          std::make_move_iterator(lines.begin()),
                          std::make_move_iterator(lines.end()));
    // reflowBuffer 的光标→链坐标换算从 cursorRow 回找链头，必须随插入平移。
    primary_.cursor.position.row += count;
    primary_.wrapPending = false;
    // 行数瞬时超 rows_：由随后的 reflow() 溢出分支裁回（出口恒 rows_）；
    // dirtyRows/dirtyRanges 尺寸不动——对外查询以 rows_ 为界，reflow 后
    // markAllDirty 自洽（M16b 调研 §3 验证）。
}
```

实现要点（审查锚点）：`<iterator>` 已在 M15 加入本文件 include 区，无需重复；本原语不做列宽调整（归还的行与屏幕同宽，宽度不变量由调用方保证）。

- [ ] **步骤 5：构建并运行测试**

运行：
```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_screen_reflow
./build/linux-gcc-debug/tests/test_screen_reflow
```
预期：编译通过，退出码 0。若 testPrependPrimaryLines 的溢出数或 wrapped 断言失败，先按 reflowBuffer 溢出语义（out.size() > rows_ → 顶部 overflow 行经回调上移）核对自己的推演。

- [ ] **步骤 6：全量回归 + Commit**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
doxygen Doxyfile
git add include/ZzTerm/Screen.h src/screen/Screen.cpp tests/unit/test_screen_reflow.cpp
git commit -m "feat(screen): M16b prependPrimaryLines 顶部插行原语"
```
预期：55/55（无新测试目标）；doxygen exit 0。

---

## 任务 2：backend 协调 + facade/compat/selection 测试 + 文档收尾

**文件：** 见计划头部「修改（任务 2）」。

- [ ] **步骤 1：编写失败的 facade 测试（test_native_reflow.cpp 追加）**

文件尾部（main 前）新增用例与帮手（feed/rowText 帮手复用本文件既有）：

```cpp
// 屏幕第 row 行文本（去尾空白断言用前缀比对）。
static std::string screenRowText(const ZzTerminal& term, int row)
{
    const ZzLineView line = term.renderView().lineAt(row);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

// 历史第 index 行文本（test_native_reflow 现无同名帮手，随本用例新增）。
static std::string historyText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

// 6. 跨缝链列变往返（M16b）：缩列跨缝状态保持连续、拉大接回布局完整
static void testSeamChainReflowRoundtrip()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "abcdefghij"); // 写满行 0（wrap-pending）
    feed(term, "kl\r\n");     // 链 abcdefghijkl：行 0 wrapped + 行 1
    feed(term, "mn\r\n");
    feed(term, "op\r\n");     // 滚出链头：历史 [abcdefghij(wrapped)]，屏幕 kl/mn/op
    // 跨缝状态钉住：历史末行 wrapped=true（续接在屏幕首行）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1);
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).wrapped());

    // 缩列：链接续保持——归还-重组后溢出裁回，历史尾仍 wrapped=true（合法跨缝态）
    ZZ_TEST_EXPECT(term.resize(5, 3));
    const std::size_t h1 = term.historyView().lineCount();
    ZZ_TEST_EXPECT(h1 == 2); // 链 12 格 -> 5 列：abcde/fghij 溢出进历史，kl 留屏幕
    ZZ_TEST_EXPECT(term.historyView().lineAt(h1 - 1).wrapped());
    ZZ_TEST_EXPECT(historyText(term, 0) == "abcde");
    ZZ_TEST_EXPECT(historyText(term, 1) == "fghij");
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "kl");

    // 拉大：链接回——归还后统一重组为完整链回到屏幕首行
    ZZ_TEST_EXPECT(term.resize(10, 3));
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "abcdefghijkl");
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());
}

// 7. 多轮往返布局完整（M16b）：10->5->3->10 后内容逐格恢复
static void testSeamChainMultiRoundtrip()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "abcdefghij");
    feed(term, "kl\r\n");
    feed(term, "mn\r\n");
    feed(term, "op\r\n");
    ZZ_TEST_EXPECT(term.resize(5, 3));
    ZZ_TEST_EXPECT(term.resize(3, 3));
    ZZ_TEST_EXPECT(term.resize(10, 3));
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "abcdefghijkl");
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(term.cursor().position.row >= 0 && term.cursor().position.row < 3);
}
```

main() 注册两行：`testSeamChainReflowRoundtrip();` 与 `testSeamChainMultiRoundtrip();`

注意（审查锚点）：用例 6 的缩列推演——10x3 屏链 12 格（abcdefghij+kl）+ mn + op，resize(5,3)：归还后屏幕 [abcdefghij(w), kl, mn, op] 4 行，reflow(5)：链->abcde/fghij/kl 3 行 + mn + op = 5 行，溢出 2（abcde/fghij 经回调入历史），屏幕 kl/mn/op，历史 2 行尾 wrapped=true。resize(10,3)：摘除 2 行归还，屏幕 [abcde(w), fghij(w), kl, mn, op] 5 行，reflow(10)：链->abcdefghijkl 1 行 + mn + op = 3 行 = rows_ 无溢出，历史归零。用例 7 的 3 列中间态不做中间断言（只钉终态）。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_native_reflow 2>&1 | tail -3 && ./build/linux-gcc-debug/tests/test_native_reflow`
预期：编译通过（prependPrimaryLines 任务 1 已落地）但用例 6 失败——现行行为下 resize(5,3) 后历史 dangling 尾链被终结：h1==2 但 lineAt(1).wrapped()==false，且 resize(10,3) 后链不接回（screenRowText(0)=="kl" 或类似）。记录实际失败形态。

- [ ] **步骤 3：ZzNativeBackend 接缝协调**

`src/backend/native/ZzNativeBackend.cpp` 的 resize 列变分支（:249-253）整段替换为：

```cpp
    if (cols != old.cols) {
        // M16b：跨缝链接续保护——历史末行 wrapped=true 即尾链跨缝（续接在
        // primary 屏幕首链），先摘除归还屏幕统一重组，避免历史 reflow 把
        // dangling 尾链终结劈链（spec 2026-09-30-m16b-seam-chain-reflow-design.md）。
        if (const std::size_t histLines = scrollback_->lineCount();
            histLines > 0 && scrollback_->lineAt(histLines - 1).wrapped()) {
            std::size_t head = histLines - 1;
            while (head > 0 && scrollback_->lineAt(head - 1).wrapped())
                --head;
            screen_.prependPrimaryLines(scrollback_->takeNewest(histLines - head));
        }
        scrollback_->reflow(cols);
        ++historyGeneration_; // M14：历史 reflow 代计数递增（屏幕回流 append 经回调另计）
        screen_.reflow(cols);
    }
```

- [ ] **步骤 4：构建并运行 facade 测试**

运行：
```bash
cmake --build --preset linux-gcc-debug --target test_native_reflow
./build/linux-gcc-debug/tests/test_native_reflow
```
预期：全部通过，退出码 0。用例 6 失败时先核对步骤 1 的推演锚点，不得改断言迁就实现。

- [ ] **步骤 5：compat 跨缝对照用例**

`tests/unit/test_backend_compat.cpp` 在用例 21（testResizeReflowHardLine）后新增：

```cpp
// 22. 跨缝链列变 parity（M16b）：链横跨历史/屏幕接缝，缩/拉两档后双后端
// 历史行数/文本/wrapped/屏幕逐格一致——native 归还机制 vs contour 统一流。
void testResizeReflowSeamChain()
{
    Dual d;
    const std::string head(80, 'a');
    const std::string tail(10, 'b');
    d.feedBoth(head + tail + "\r\n"); // 90 格链：80 列 autowrap 成 2 行
    for (int i = 0; i < 23; ++i)
        d.feedBoth("filler\r\n");     // 恰好滚出 1 行：链头入历史、链尾留屏幕（跨缝）
    // 跨缝状态双后端钉住：历史末行 wrapped=true
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    ZZ_CHECK(d.native.historyView().lineCount() > 0);
    const std::size_t h0 = d.native.historyView().lineCount();
    ZZ_CHECK(d.native.historyView().lineAt(h0 - 1).wrapped());
    ZZ_CHECK(d.contour.historyView().lineAt(h0 - 1).wrapped());

    d.native.resize(60, 24); // 缩列：跨缝链接续保持（不劈开）
    d.contour.resize(60, 24);
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        ZZ_CHECK(historyText(d.native, i) == historyText(d.contour, i));
        ZZ_CHECK(d.native.historyView().lineAt(i).wrapped()
                 == d.contour.historyView().lineAt(i).wrapped());
    }
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 60, "seam-60");

    d.native.resize(80, 24); // 拉大：链接回
    d.contour.resize(80, 24);
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        ZZ_CHECK(historyText(d.native, i) == historyText(d.contour, i));
        ZZ_CHECK(d.native.historyView().lineAt(i).wrapped()
                 == d.contour.historyView().lineAt(i).wrapped());
    }
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 80, "seam-80");
}
```

main() 注册（`testResizeReflowHardLine();` 行后）：`testResizeReflowSeamChain();`

注意（审查锚点）：23 行 filler 的滚出推演——链占屏行 0/1、光标行 1；每行 filler 推进 1 行，第 22 行光标到行 23（不滚），第 23 行滚 1 行（链头入历史），链尾（10 个 b）留屏幕行 0。contour 侧接缝分布在 resize 后若与 native 有行位差异但链完整，先按 contour「页面=重组流尾部 rows_ 行」（fork Grid.cpp rotateBuffersLeft 收账）核对推演，不得直接删断言。

- [ ] **步骤 6：选区跨缝文本不变用例**

`tests/unit/test_terminal_selection.cpp` 在 testDroppedShiftsAnchor 前（或文件用例区合适位置）新增：

```cpp
static void testSeamChainSelectionSurvivesReflow()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "abcdefghij"); // 写满行 0（wrap-pending）
    feed(term, "kl\r\n");     // 链 abcdefghijkl
    feed(term, "mn\r\n");
    feed(term, "op\r\n");     // 滚出链头：跨缝（历史尾 wrapped=true）
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 12});
    const std::string before = term.selectedText(); // 跨缝拼链提取（ZzSelectionText 接缝规则）
    ZZ_TEST_EXPECT(before == "abcdefghijkl");
    term.resize(5, 3);  // 缩列：跨缝链接续保持
    ZZ_TEST_EXPECT(term.selectedText() == before);
    term.resize(10, 3); // 拉大：接回
    ZZ_TEST_EXPECT(term.selectedText() == before);
}
```

main() 注册一行：`testSeamChainSelectionSurvivesReflow();`

- [ ] **步骤 7：运行验证**

运行：
```bash
cmake --build --preset linux-gcc-debug --target test_native_reflow test_backend_compat test_terminal_selection
./build/linux-gcc-debug/tests/test_native_reflow && ./build/linux-gcc-debug/tests/test_backend_compat && ./build/linux-gcc-debug/tests/test_terminal_selection
```
预期：exit 0（compat 仅在 contour ON 构建）。任一失败先按对应锚点推演复核。

- [ ] **步骤 8：文档收尾（五处）**

其一，`include/ZzTerm/Scrollback.h` 的固有边界注释（:90-92）整段替换为：

```cpp
 * @note 分域重组的接缝处理：历史与屏幕分别重组；横跨两域的逻辑行在
 *       列变 resize 时经 backend 归还机制统一重组（M16b），不再被拆成
 *       两条独立链；仅纯历史 API 直调 reflow 时 dangling 尾链仍按
 *       完整链终结（ZzTerminal resize 路径不受影响）。
```

其二，`include/ZzTerm/HistoryView.h` 文件头的分域 reflow 免责段（M14 加入，grep -n "分域 reflow" include/ZzTerm/HistoryView.h 定位）整段替换为：

```cpp
/// 接缝处理（M16b）：历史与屏幕分域重组，但横跨两域的逻辑行在列变
/// resize 时经 backend 归还机制统一重组，不再拆链；历史末行 wrapped=true
/// 即与屏幕首行续接为一条逻辑行（与 ZzLineSource 接缝规则一致）。
```

其三，`include/ZzTerm/Terminal.h` resize 注释的列变化句（M16 版「列变化：屏幕区与 scrollback 历史一起重组（先历史后屏幕，光标跟随内容）」）句末追加：

```cpp
 *       ；跨历史/屏幕接缝的链经归还机制统一重组（M16b），缩列跨缝
 *       状态保持连续、拉大接回
```

其四，`docs/API.md`：grep -n "硬行缩列多行化" docs/API.md 定位 resize 语义段，在该段末追加一句「跨历史/屏幕接缝的链在列变 resize 时经归还机制统一重组（M16b），缩列跨缝状态保持连续、拉大接回（对齐 contour 统一流）」；版本节（grep -n "M16：" docs/API.md 定位）在 M16 条目后追加：

```markdown
- M16b：行为语义变化（列变 reflow 的跨历史/屏幕接缝链不再劈开，经归还
  机制统一重组）；ZzScreen 新增 prependPrimaryLines 公共方法（Core 内部
  使用定位）；存量劈链不修复（链尾空白已被裁，只对新 resize 生效）。
```

其五，`docs/superpowers/specs/2026-09-29-m13-spike-record.md` §4 现象 5 的「M15/M16 已根治」标注句末追加「；接缝劈链由 M16b 根治（归还机制统一重组，spec 2026-09-30-m16b-seam-chain-reflow-design.md）」。

- [ ] **步骤 9：全基线 + doxygen + Commit + 推送 + CI**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
git add src/backend/native/ZzNativeBackend.cpp tests/unit/test_native_reflow.cpp \
    tests/unit/test_backend_compat.cpp tests/unit/test_terminal_selection.cpp \
    include/ZzTerm/Scrollback.h include/ZzTerm/HistoryView.h include/ZzTerm/Terminal.h \
    docs/API.md docs/superpowers/specs/2026-09-29-m13-spike-record.md
git commit -m "feat(native): M16b 接缝链归还协调与跨缝往返/compat/选区钉住+文档"
git push origin contour
gh run list --branch contour --limit 7
```
预期：55/55、44/44、55/55（无新测试目标）；doxygen exit 0；CI 7 个 workflow 全绿。

---

## 收尾（全部任务完成后）

- ZzTermCore 仓打 tag `m16b` 并推送（`git tag m16b && git push origin m16b`）。
- demo 工具回归（M16 时建的 /tmp/zz-m16-demo）：重跑 `cd /tmp/zz-m16-demo && ./build/m16demo -platform offscreen`——A/C 全缓冲非空行序列应严格相等（M16 时发现的跨缝劈链行对应接回），退出码 0；三张 PNG 对照目验。
- 人工复验移交（用户执行）：spike 多栏 ls 反复拖窄拉宽，含接缝行的布局应完整恢复。

## 实施勘误（SDD 审查中修正的计划数据缺陷，代码以仓库为准）

1. 任务 1 步骤 1 测试 setup：makeChainLine 造 wrapped 非尾行只有 2 格内容——zzReflowChain 只裁链尾空白、链中间空白格占列，溢出/接回期望物理上必败。实施修正为满列 setup（test 6：head "abcd"(w) + 屏幕首行 "efgh" 置 wrapped；test 7：scr(2,3) + 满列 head "ta"），全部期望值逐项保留（4537537，任务 1 审查核实）。
2. 任务 2 步骤 1 facade 用例 6/7：feed("op\r\n") 会在末行多滚一行把链尾 kl 也顶入历史使缝消失，修正为 feed("op")（ed480a1，任务 2 审查推演核实）。
3. 任务 2 步骤 1 facade 用例 6/7：链 12 格在 10 列下必为 2 行链，「接回单行」需列宽 ≥ 12，拉大从 resize(10,3) 修正为 resize(12,3)（ed480a1）。
4. 任务 2 步骤 5 compat 用例 22：80x24 下 23 行 filler 会滚 2 行（链尾也入历史），filler 数修正为 22（ed480a1）。
5. demo 断言勘误（任务 2 审查裁定）：demo 的 A/C 严格相等断言对 bash SIGWINCH 重绘写入的提示符尾部空格过苛（应用侧内容、非 Core reflow 语义），放宽为去尾空白行序列比对；对照实验证实该差异与 M16b 无关。
