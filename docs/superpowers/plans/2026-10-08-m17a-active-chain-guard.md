# M17a 光标活动链 reflow 收链保护 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 扩列 reflow 收链时豁免光标所在折链（保持旧宽度拆分），使 readline 陈旧帧擦除命中自己的提示符碎片行而非无辜内容行。

**架构：** 纯函数层 `zzReflowLines` 增加光标链 Preserve 模式（豁免链旧布局原样保留、行存储扩宽到新列宽）；`ZzScreen::reflowBuffer` 在「Primary 且扩列且光标在折链上」时启用；contour 后端不变（差异化语义，compat 偏离登记）。

**技术栈：** C++20、ZzTermCore native 后端（src/screen/）、ctest 自研断言宏（ZZ_TEST_EXPECT / ZZ_CHECK）。

**规格：** `docs/superpowers/specs/2026-10-08-m17a-active-chain-guard-design.md`

**基线命令：**
- `cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`（57 例）
- `ctest --test-dir build/m2-off-check`（46 例）
- `ctest --test-dir build/m2-shared-check`（57 例）
- 单测直跑：`./build/linux-gcc-debug/tests/test_reflow` 等（含 FAIL 明细）

---

### 任务 1：zzReflowLines 光标链 Preserve 模式（纯函数层）

**文件：**
- 修改：`src/screen/Reflow.h:34-35`（签名 + 枚举）
- 修改：`src/screen/Reflow.cpp:108-136`（zzReflowLines 主循环加豁免分支）
- 测试：`tests/unit/test_reflow.cpp`（文件尾 main 前加 3 用例并注册）

- [ ] **步骤 1：编写失败的测试**

在 `tests/unit/test_reflow.cpp` 的 `main()` 之前追加（`makeLine`/`lineText` 帮助函数沿用文件既有定义）：

```cpp
// M17a-1：Preserve 扩列——光标链豁免收链，旧布局/旗标/光标行位保持
static void testPreserveCursorChainOnWiden()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(10, "ABCDEFGHIJ", true)); // 链 0 行 0
    lines.push_back(makeLine(10, "klm", false));       // 链 0 行 1（13 格）
    lines.push_back(makeLine(10, "xy", false));        // 链 1
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 11; // 片段 1 列 1（'l'）
    auto out = zzReflowLines(std::move(lines), 10, 20, &cur,
                             ZzReflowCursorChain::Preserve);
    ZZ_TEST_EXPECT(out.size() == 3); // 链豁免：2 行原样 + 链 1 一行
    ZZ_TEST_EXPECT(out[0].cellCount() == 20);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 10) == "ABCDEFGHIJ");
    ZZ_TEST_EXPECT(!out[1].wrapped());
    ZZ_TEST_EXPECT(lineText(out[1]).substr(0, 3) == "klm");
    ZZ_TEST_EXPECT(lineText(out[2]).substr(0, 2) == "xy");
    ZZ_TEST_EXPECT(cur.row == 1); // 行位 = 链起点 0 + 偏移 11 / 10
    ZZ_TEST_EXPECT(cur.col == 1); // 列 = 偏移 11 % 10
}

// M17a-2：Preserve 缩列不豁免——照常重切（与 Reflow 逐点一致）
static void testPreserveIgnoredOnShrink()
{
    auto buildLines = [] {
        std::vector<ZzLine> lines;
        lines.push_back(makeLine(10, "ABCDEFGHIJ", true));
        lines.push_back(makeLine(10, "klm", false));
        lines.push_back(makeLine(10, "xy", false));
        return lines;
    };
    ZzReflowCursor curA; curA.chainIndex = 0; curA.chainOffset = 11;
    ZzReflowCursor curB = curA;
    auto outPreserve = zzReflowLines(buildLines(), 10, 5, &curA,
                                     ZzReflowCursorChain::Preserve);
    auto outReflow = zzReflowLines(buildLines(), 10, 5, &curB,
                                   ZzReflowCursorChain::Reflow);
    ZZ_TEST_EXPECT(outPreserve.size() == 4); // "ABCDE"(w) "FGHIJ"(w) "klm" "xy"
    ZZ_TEST_EXPECT(outPreserve.size() == outReflow.size());
    for (std::size_t i = 0; i < outPreserve.size(); ++i) {
        ZZ_TEST_EXPECT(lineText(outPreserve[i]) == lineText(outReflow[i]));
        ZZ_TEST_EXPECT(outPreserve[i].wrapped() == outReflow[i].wrapped());
    }
    ZZ_TEST_EXPECT(curA.row == 2 && curA.col == 1);
    ZZ_TEST_EXPECT(curA.row == curB.row && curA.col == curB.col);
}

// M17a-3：Preserve 无光标跟踪——豁免无对象，等同 Reflow，不崩
static void testPreserveWithoutCursor()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(10, "ABCDEFGHIJ", true));
    lines.push_back(makeLine(10, "klm", false));
    auto out = zzReflowLines(std::move(lines), 10, 20, nullptr,
                             ZzReflowCursorChain::Preserve);
    ZZ_TEST_EXPECT(out.size() == 1); // 13 格合 1 行（与 Reflow 一致）
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 13) == "ABCDEFGHIJklm");
}
```

在 `main()` 中注册三行：

```cpp
    testPreserveCursorChainOnWiden();
    testPreserveIgnoredOnShrink();
    testPreserveWithoutCursor();
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug 2>&1 | grep -E 'error|ZzReflowCursorChain' | head -5`
预期：编译失败，报错含 `ZzReflowCursorChain` 未声明 / 实参过多。

- [ ] **步骤 3：实现 Preserve 模式**

`src/screen/Reflow.h`：在 `ZzReflowCursor` 结构体之后、函数声明之前加枚举，并改签名：

```cpp
/// @brief 光标链处置（M17a）：Reflow=现状重组；Preserve=扩列时豁免收链，
///        光标所在折链保持旧宽度拆分（行存储扩宽到新列宽，内容布局/旗标/
///        光标行位不动）——readline 陈旧帧擦除兼容。缩列方向忽略本标志。
enum class ZzReflowCursorChain { Reflow, Preserve };

/**
 * @brief 将物理行序列从旧列宽重组到新列宽（soft-wrap reflow）。
 * @param lines 物理行序列（按值传入，调用方可 move；函数不保留引用）。
 * @param oldCols 旧列宽（大于 0，不变量：所有行均为该宽度）。
 * @param newCols 新列宽（大于 0）。
 * @param cursor 可选光标跟踪（nullptr 表示不跟踪）。
 * @param cursorChain 光标链处置（默认 Reflow；Preserve 仅在 cursor 非空
 *        且 newCols > oldCols 时生效）。
 * @return 重组后的物理行序列（每行 newCols 列，wrapped 标记已重算；
 *         Preserve 豁免链的标记原样保留）。
 * @note 链按新列宽重切：内容超宽的多行化；硬行（未 wrapped 的单行链）即
 *       chainLen==1 普通链——缩列多行化、拉大沿 wrapped 链合并恢复
 *       （M16，与 Contour/xterm 对齐，取代 M4 的硬行截断语义）；
 *       链末尾的完全默认空白格被裁除（避免短行变窄产生幽灵行）；
 *       宽字符原子搬运不落边界（边界前移一格补默认空白）；
 *       cluster 格在新行重新 internCluster。
 */
std::vector<ZzLine> zzReflowLines(std::vector<ZzLine> lines, int oldCols, int newCols,
                                  ZzReflowCursor* cursor = nullptr,
                                  ZzReflowCursorChain cursorChain
                                  = ZzReflowCursorChain::Reflow);
```

`src/screen/Reflow.cpp`：zzReflowLines 主循环内、`zzReflowChain` 调用前加豁免分支：

```cpp
std::vector<ZzLine> zzReflowLines(std::vector<ZzLine> lines, int oldCols, int newCols,
                                  ZzReflowCursor* cursor, ZzReflowCursorChain cursorChain)
{
    std::vector<ZzLine> out;
    if (oldCols <= 0 || newCols <= 0 || oldCols == newCols) {
        // 列宽未变：原样返回（wrapped 标记不动）。
        if (cursor) {
            // 恒等映射：行 = 链起点 + 偏移 / newCols 的近似由调用方保证不触发本分支。
            cursor->row = 0;
            cursor->col = 0;
        }
        return lines;
    }

    std::size_t chainIndex = 0;
    std::size_t chainStart = 0;
    while (chainStart < lines.size()) {
        // 链尾：lines[j].wrapped() 为 true 则 j+1 同链。
        std::size_t chainEnd = chainStart + 1;
        while (chainEnd < lines.size() && lines[chainEnd - 1].wrapped())
            ++chainEnd;
        // M17a：扩列且光标在本链（多行链）时豁免收链——旧宽度布局原样
        // 保留（仅扩宽行存储），光标行位 = 链起点 + 偏移/旧宽、列 = 偏移%旧宽。
        if (cursorChain == ZzReflowCursorChain::Preserve && cursor
            && cursor->chainIndex == chainIndex && newCols > oldCols
            && chainEnd - chainStart > 1) {
            cursor->row = static_cast<int>(out.size()) + cursor->chainOffset / oldCols;
            cursor->col = cursor->chainOffset % oldCols;
            for (std::size_t i = chainStart; i < chainEnd; ++i) {
                ZzLine row = std::move(lines[i]);
                row.resize(newCols); // 扩宽，右侧补默认格；wrapped 旗标随 move 保留
                out.push_back(std::move(row));
            }
        } else {
            const bool trackThis = cursor && cursor->chainIndex == chainIndex;
            zzReflowChain(&lines[chainStart], chainEnd - chainStart, oldCols, newCols, out,
                          cursor, trackThis);
        }
        ++chainIndex;
        chainStart = chainEnd;
    }
    return out;
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_reflow`
预期：无 FAIL 输出，退出码 0（既有用例不受影响——新参数有默认值）。

- [ ] **步骤 5：Commit**

```bash
git add src/screen/Reflow.h src/screen/Reflow.cpp tests/unit/test_reflow.cpp
git commit -m "feat(reflow): zzReflowLines 光标链 Preserve 模式——扩列豁免收链（M17a 纯函数层）"
```

---

### 任务 2：ZzScreen::reflowBuffer 豁免接线

**文件：**
- 修改：`src/screen/Screen.cpp:142-164`（reflowBuffer 光标链坐标段）
- 测试：`tests/unit/test_screen_reflow_topfill.cpp`（加 3 用例并注册）

- [ ] **步骤 1：编写失败的测试**

在 `tests/unit/test_screen_reflow_topfill.cpp` 追加（`writeRow`/`rowText`/`makeTextLine` 沿用）：

```cpp
// M17a-4：光标在折链上扩列——豁免收链，布局/旗标/光标不动，无顶补需求
static void testPreserveCursorChainScreen()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa"); // 链首（10 格）
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");         // 链尾（链 12 格）
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{1, 1}); // 光标在链上（片段 1 列 1）
    std::size_t asked = 0;
    scr.setHistoryPullCallback([&](std::size_t maxLines) {
        asked = maxLines;
        return std::vector<ZzLine>{};
    });
    scr.reflow(20);
    ZZ_TEST_EXPECT(asked == 0); // 豁免链贡献 2 行，产出 4 行 == rows，无缺口
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 10) == "aaaaaaaaaa");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "bb");
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "s2");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 1);
}

// M17a-5：光标在折链上缩列——不豁免，照常重切溢出
static void testPreserveSkippedOnShrinkScreen()
{
    ZzScreen scr(20, 4);
    writeRow(scr, 0, "aaaaaaaaaaaaaaaaaaaa"); // 链首（20 格）
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");                   // 链尾（链 22 格）
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{1, 1});
    std::size_t spilled = 0;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) { spilled += lines.size(); });
    scr.reflow(10); // 链 22 格 -> 3 行，产出 5 行 > 4，溢出 1 行压历史
    ZZ_TEST_EXPECT(spilled == 1);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 10) == "aaaaaaaaaa");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 10) == "aaaaaaaaaa");
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "bb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "s1");
    // 光标：链偏移 21 -> 10 列下行 2 列 1，减溢出 1 -> 行 1 列 1
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 1);
}

// M17a-6：Alternate 缓冲不豁免（全屏应用自带重绘，无 readline 帧假设）
static void testPreserveSkippedOnAlternate()
{
    ZzScreen scr(10, 4);
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{1, 1}); // Alternate 光标在链上
    scr.reflow(20); // Alternate 缓冲：不豁免，链 12 格合 1 行
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s2");
    // Alternate 光标链跟踪走现状换算：链偏移 11 -> 20 列下行 0 列 11
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 11);
}
```

在 `main()` 中注册三行：

```cpp
    testPreserveCursorChainScreen();
    testPreserveSkippedOnShrinkScreen();
    testPreserveSkippedOnAlternate();
```

- [ ] **步骤 2：运行测试验证失败**

运行：`./build/linux-gcc-debug/tests/test_screen_reflow_topfill`
预期：M17a-4 失败（链被合并成 `aaaaaaaaaabb`，lineAt(0) 断言落空）；M17a-5/6 可能碰巧通过（现状路径）——以 M17a-4 的失败为准。

- [ ] **步骤 3：实现豁免接线**

`src/screen/Screen.cpp` reflowBuffer 中，把：

```cpp
    std::vector<ZzLine> out = zzReflowLines(buf.lines, cols_, newCols, &track);
```

改为：

```cpp
    // M17a：Primary 且扩列且光标在折链（链 >= 2 行）上时豁免收链——
    // readline 的 WINCH 重绘按旧布局帧发相对擦除，豁免让擦除命中
    // 提示符碎片行而非收链后的无辜内容行（规格 2026-10-08-m17a §3）。
    const bool preserveCursorChain =
        mayScrollOut && newCols > cols_
        && buf.lines[static_cast<std::size_t>(chainStartRow)].wrapped();
    std::vector<ZzLine> out =
        zzReflowLines(buf.lines, cols_, newCols, &track,
                      preserveCursorChain ? ZzReflowCursorChain::Preserve
                                          : ZzReflowCursorChain::Reflow);
```

（chainStartRow 的现有计算在上方第 144-162 行，豁免判定放在其之后。）

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_screen_reflow_topfill && ctest --preset linux-gcc-debug -R 'screen|reflow' 2>&1 | tail -3`
预期：无 FAIL；ctest 相关例全绿。

- [ ] **步骤 5：Commit**

```bash
git add src/screen/Screen.cpp tests/unit/test_screen_reflow_topfill.cpp
git commit -m "feat(screen): reflowBuffer 扩列收链豁免——Primary 光标在折链上时启用 Preserve（M17a）"
```

---

### 任务 3：facade 级事故复刻（readline 擦除端到端）

**文件：**
- 测试：`tests/unit/test_native_reflow_topfill.cpp`（加 1 用例并注册）

- [ ] **步骤 1：编写失败的测试**

在 `tests/unit/test_native_reflow_topfill.cpp` 追加（`feedStr`/`screenRowText` 沿用）：

```cpp
// 6. M17a 事故复刻（2026-10-08 用户实测，spike 留痕重放定位）：极窄拖拽
// 使提示符折链 -> 拉回时 readline 按旧帧发 \e[A\e[K 相对擦除。豁免后
// 擦除命中提示符碎片行，内容零损失、无空洞、光标与 bash 预期一致。
// 行数账（手工推演，与 M16c 探针同款方法）：
//   40x10 喂 L0..L11（各 \r\n）：滚动后历史 3（L0,L1,L2），
//   屏幕 [L3..L11, 提示符行]，光标 (19,9)；
//   resize(8,4)：提示符 19 格折 3 行，物理 12 行溢出 8 压历史
//   （历史 11 = L0..L10），屏幕 [L11, "prompt$ "(w), "echo abc"(w), "def"]，
//   光标 (3,3)；
//   resize(40,10)：豁免链保持 3 行，缺口 6 顶补（L5..L10），
//   屏幕 [L5..L11, 碎片×3]，光标 (3,9)，历史 5（L0..L4）；
//   重绘 "\r\e[K" + "\e[A\e[K"×2 + 重印：擦除命中 3 个碎片行，
//   提示符重印在 L11 下一行，光标 (19,7)，内容零损失。
static void testActiveChainGuardVsReadlineErase()
{
    ZzTerminal term(40, 10, ZzBackendKind::Native, 100);
    for (int i = 0; i < 12; ++i)
        feedStr(term, "L" + std::to_string(i) + "\r\n");
    feedStr(term, "prompt$ echo abcdef"); // 19 格，光标 (19,9)
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);

    ZZ_TEST_EXPECT(term.resize(8, 4)); // 极窄：提示符折 3 行
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 11);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "L11");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "prompt$ ");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "echo abc");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "def");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.cursor().position.col == 3);

    ZZ_TEST_EXPECT(term.resize(40, 10)); // 拉回：豁免收链
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5); // 顶补 6（L5..L10）

    // readline WINCH 重绘（陈旧帧高 3）：擦上行×2 + 重印。
    feedStr(term, "\r\033[K\033[A\033[K\033[A\033[K");
    feedStr(term, "prompt$ echo abcdef");
    ZZ_TEST_EXPECT(screenRowText(term, 6) == "L11"); // 修复前此处 L11 已被误擦
    ZZ_TEST_EXPECT(screenRowText(term, 7) == "prompt$ echo abcdef");
    ZZ_TEST_EXPECT(screenRowText(term, 8).empty()); // 被擦的是碎片行
    ZZ_TEST_EXPECT(screenRowText(term, 9).empty());
    ZZ_TEST_EXPECT(term.cursor().position.row == 7);
    ZZ_TEST_EXPECT(term.cursor().position.col == 19);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5); // L0..L4 完好留存
}
```

在 `main()` 中注册：

```cpp
    testActiveChainGuardVsReadlineErase();
```

- [ ] **步骤 2：运行测试验证失败**

运行：`./build/linux-gcc-debug/tests/test_native_reflow_topfill`
预期：FAIL——`screenRowText(term, 6) == "L11"` 落空（修复前 L11 被 `\e[K` 误擦，row 6 实为 "L9"）；history 断言也可能因顶补数不同（8 vs 6）失败。注意：本用例前面关于 resize(8,4) 的断言修复前后均应通过（缩列无豁免）。

- [ ] **步骤 3：若有断言值与推演不符，先核账再改**

运行：`./build/linux-gcc-debug/tests/test_native_reflow_topfill 2>&1 | head -10`
若失败点在 resize(8,4) 段（历史 11、屏幕四行文本、光标 (3,3)）——说明推演账有误，回到 gdb/printf 探针实测核账（方法见 M16c 计划附录：双后端探针逐点核对），修正推演值后重跑。本步骤不改动实现代码。

- [ ] **步骤 4：验证修复后通过**

运行：`./build/linux-gcc-debug/tests/test_native_reflow_topfill`
预期：无 FAIL（任务 1/2 已实现，本用例应直接转绿）。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_native_reflow_topfill.cpp
git commit -m "test(native): M17a 事故复刻——readline 陈旧帧擦除命中碎片行，内容零损失"
```

---

### 任务 4：compat 偏离登记与全量排查

**文件：**
- 测试：`tests/unit/test_backend_compat.cpp:504-513`（testResizeReflowSeamChain 的 60→80 回程段）

- [ ] **步骤 1：全量跑测试枚举受影响用例**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug 2>&1 | grep -E 'FAIL|Failed' | head -20`
预期：test_backend_compat 的 seam 用例失败（60→80 回程 native 豁免收链、contour 收链，逐点断言落空）。若出现其他失败用例，逐一确认其场景确属「扩列且光标在折链上」后按本任务同款方式登记；场景不属此形态的失败是回归，停下来回到任务 2 修实现。

- [ ] **步骤 2：改写 seam 用例回程段为偏离登记**

`tests/unit/test_backend_compat.cpp` testResizeReflowSeamChain 中，把：

```cpp
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
```

改为：

```cpp
    // M17a 偏离登记：拉大时光标在折链上——native 豁免收链（保持 60 列
    // 布局 2 行），contour 收链（80+10 布局 2 行）。两侧各自钉住，不再
    // 逐点等同（规格 2026-10-08-m17a §4）。
    d.native.resize(80, 24);
    d.contour.resize(80, 24);
    // native：链保持旧布局——行 0 为 60 个 a（wrapped），行 1 为 20 a+10 b。
    ZZ_CHECK(screenText(d.native, 0, 80) == std::string(60, 'a'));
    ZZ_CHECK(d.native.renderView().lineAt(0).wrapped());
    ZZ_CHECK(screenText(d.native, 1, 80) == std::string(20, 'a') + std::string(10, 'b'));
    // contour：收链——行 0 为 80 个 a（wrapped），行 1 为 10 a+10 b。
    ZZ_CHECK(screenText(d.contour, 0, 80) == std::string(80, 'a'));
    ZZ_CHECK(d.contour.renderView().lineAt(0).wrapped());
    ZZ_CHECK(screenText(d.contour, 1, 80) == std::string(10, 'a') + std::string(10, 'b'));
```

（`screenText` 为该文件既有按行文本帮助函数——若实际签名不同，以文件内其他用例的取行文本方式为准逐字替换；行 0/1 的具体行号以 seam 场景实际布局为准，改写前先直跑一次 `./build/linux-gcc-debug/tests/test_backend_compat` 看 FAIL 行给出的实际值核账。）

- [ ] **步骤 3：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_backend_compat`
预期：无 FAIL，退出码 0。

- [ ] **步骤 4：Commit**

```bash
git add tests/unit/test_backend_compat.cpp
git commit -m "test(compat): M17a 偏离登记——seam 回程 native 豁免收链 vs contour 收链各自钉住"
```

---

### 任务 5：文档、基线、留痕重放验证与收尾

**文件：**
- 修改：`docs/Scrollback-and-Reflow.md`（reflow 章节加「光标活动链保护」小节）
- 修改：`docs/API.md`（resize 语义段 + 版本节 M17a 条目）
- 修改：`include/ZzTerm/Screen.h`（reflow 注释同步）

- [ ] **步骤 1：docs/Scrollback-and-Reflow.md 加小节**

在 reflow 相关章节末尾（M16c 顶补小节之后）追加：

```markdown
### 光标活动链保护（M17a）

扩列 reflow 收链时，**Primary 缓冲中光标所在的折链豁免合并**，保持旧宽度
拆分（行存储扩宽到新列宽，内容布局、wrapped 旗标、光标行位均不动）。

动机：readline 的 WINCH 重绘按**旧布局帧**发相对擦除（`\e[A\e[K`×N）。
若 reflow 已把输入链收链为 1 行，擦除会命中收链后无辜的内容行，造成
永久性内容破坏（2026-10-08 用户实测，spike 留痕重放定位）。豁免后擦除
命中提示符自己的碎片行，内容零损失。contour/xterm/VTE 等 reflow 终端
均有此破坏（contour 已用留痕逐点实测确认），本语义为有意差异化。

细则：仅扩列方向（缩列拆分照常）、仅 Primary（Alternate 不豁免）；
豁免是瞬时态——应用重写该链（readline 重印提示符）后链消失，后续
reflow 无豁免对象；流式输出（cat）在链末片段旧列位继续追加，内容正确，
视觉折点待下次重写消除。
```

- [ ] **步骤 2：docs/API.md 与 Screen.h 同步**

docs/API.md：resize 语义段（grep -n "顶补\|M16c" docs/API.md 定位 M16c 条目附近）追加 M17a 行为说明；版本与 ABI 节追加：

```markdown
- M17a：ZzScreen/ZzTerminal 无签名变化；行为语义变化——扩列 reflow 时
  Primary 光标所在折链豁免收链（readline 重绘兼容）；与 contour 后端
  在该场景有意偏离（偏离登记见 compat 例 seam 回程段）。
```

Screen.h：reflow 方法的注释块补一句（对齐实现）：

```cpp
    // M17a：扩列时 Primary 光标所在折链豁免收链（readline 陈旧帧擦除兼容），
    // 详见 docs/Scrollback-and-Reflow.md「光标活动链保护」。
```

- [ ] **步骤 3：doxygen 与全部基线**

运行：`doxygen Doxyfile`（exit 0 零警告；注意 docs 陷阱：行内 code span 禁尖括号、禁裸反斜杠字母、禁游离 @词）
运行：`ctest --preset linux-gcc-debug`（57/57）、`ctest --test-dir build/m2-off-check`（46/46）、`ctest --test-dir build/m2-shared-check`（57/57）、`ctest --preset linux-clang-fuzz -R fuzz`（3/3）
预期：全绿。

- [ ] **步骤 4：留痕重放验证（人工步骤，记录结果）**

运行：`cd /tmp/zz-resize-repro && ZB=/home/zz/Jackfahdin/github/ZzTermCore/build/linux-gcc-debug && g++ -std=c++20 -g replay.cpp -I/home/zz/Jackfahdin/github/ZzTermCore/include -L$ZB -L$ZB/src/backend/contour -L$ZB/contour/vtbackend -L$ZB/contour/vtparser -L$ZB/contour/vtpty -L$ZB/contour/crispy -L$ZB/_deps/libunicode-build/src/libunicode -lZzTermCore -lZzTermContourBackend -lvtbackend -lvtparser -lvtpty -lcrispy-core -lunicode -lunicode_ucd -o replay && stdbuf -oL ./replay /tmp/spike-trace.bin`
预期：native 终态由「非空 71/75 欠填、cursor=(44,70)」变为「擦除命中碎片行、listing 尾行（test/toolchain/u-boot/vendor 等）完好」；contour 终态不变（差异化）。把两侧终态贴进进度账本。

- [ ] **步骤 5：提交、打 tag、推送、CI 确认、重建 spike**

```bash
git add docs/Scrollback-and-Reflow.md docs/API.md include/ZzTerm/Screen.h docs/superpowers/plans/2026-10-08-m17a-active-chain-guard.md
git commit -m "docs(m17a): 光标活动链保护文档同步与计划收尾"
git tag m17a
git push origin contour --tags
gh run list --branch contour --limit 7   # 确认 7 workflow 全绿
cmake --build /home/zz/Jackfahdin/github/ZzClawTerm/build/spike-debug  # 重建 spike 交付用户复验
```

---

## 自检

**规格覆盖度：**
- §3.1 豁免对象识别 → 任务 2 实现 + 用例 M17a-4
- §3.2 仅扩列/仅 Primary/旧布局保持/光标不动/账务联动 → 任务 1 纯函数 + 任务 2 接线 + 用例 M17a-1/2/4/5/6；账务联动（顶补少取 N-1）由任务 3 历史断言（5 vs 8）钉住
- §3.3 豁免后演进（重写自愈/cat 流式） → 任务 3 重绘段即为「重写自愈」实证；cat 流式演进经 M17a-4 语义覆盖（光标在链末片段继续追加走现有 putChar 路径，无新代码路径），不再单独立例（YAGNI）
- §3.4 替代方案排除 → 纯设计论证，规格已录，无代码任务
- §3.5 降级边界 → 纯声明，无代码任务
- §4 parity 偏离 → 任务 4
- §5 测试策略 1/2/3/4/5 → 任务 3 / 任务 2 对照用例（M17a-2/3 与既有 testWidenTopFill）/ 任务 3 重绘后断言 / 任务 4 / 任务 5 步骤 4
- §6 文档与发布 → 任务 5

**占位符扫描：** 任务 4 步骤 2 的括注是「先核账再逐字替换」的防错指引（行号/帮助函数名以实测为准），非占位符；其余步骤均含逐字代码。

**类型一致性：** `ZzReflowCursorChain::{Reflow,Preserve}` 在 Reflow.h 定义，Screen.cpp 经 `#include "Reflow.h"`（既有）可见；`row.resize(newCols)` 与 Screen.cpp:103 的既有 `line.resize(cols)` 同 API；测试帮助函数 `makeLine`/`lineText`/`writeRow`/`rowText`/`makeTextLine`/`feedStr`/`screenRowText`/`screenText`/`historyText`/`checkRowEqualAllowEmptyWidthDiff` 均沿用各测试文件既有定义。
