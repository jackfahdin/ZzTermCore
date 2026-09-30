# M16 reflow 硬行语义对齐（缩列多行化 / 拉大接回）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 删除 zzReflowChain 的硬行截断特判，硬行缩列多行化、拉大合并恢复，内容往返完整，对齐 contour/xterm；重钉受影响测试并恢复 M5a 避让断言。

**架构：** 硬行即 chainLen==1 普通链——Reflow.cpp 仅删三处特判（isHardLine 判定、截断限宽、边界宽字符丢弃），链合并/重切/wrapped 重算/光标跟踪/流式器/溢出进历史全部天然兼容（调研已验证）。

**技术栈：** C++20、CMake（tests GLOB 自动收编）。

**规格：** docs/superpowers/specs/2026-09-30-m16-hardline-reflow-design.md（已审定）

**基线命令（ZzTermCore 仓根目录）：**
- `cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`（现状 55/55）
- `ctest --test-dir build/m2-off-check`（44/44）、`ctest --test-dir build/m2-shared-check`（55/55）
- `doxygen Doxyfile`（exit 0 零警告）

---

## 文件结构

**创建：** 无（全部为重钉/改写）。

**修改（任务 1）：**
- `src/screen/Reflow.cpp` — 删 isHardLine 三处特判（:21、:47-49、:71-76 中的硬行 break）
- `src/screen/Reflow.h`（:27 语义注释）— 多行化语义重写
- `tests/unit/test_reflow.cpp` — 用例 2（:62-71）、用例 11（:216-239）重钉 + main 注册名
- `tests/unit/test_scrollback.cpp` — 用例 3（:82-89）、用例 8（:219-238）、用例 9（:294-305）重钉
- `tests/unit/test_native_reflow.cpp`（:45-47 注释 + 补 A 多行化断言）

**修改（任务 2）：**
- `tests/unit/test_terminal_selection.cpp`（:68-82）— 恢复 resize(4,3) 原始断言
- `tests/unit/test_backend_compat.cpp`（用例 19 :427-441 后新增用例 21 + main :445-467 注册）
- `docs/API.md`（:91 语义句、:220-223 M5b 截断行快照条款、版本节 M16 条目）
- `docs/VT-Xterm-Checklist.md`（:118）、`docs/VT-Xterm-Checklist-v2.md`（:181-182）
- `docs/superpowers/specs/2026-09-29-m13-spike-record.md`（§4 现象 5 标注）

---

## 任务 1：内核语义翻转 + 单元级测试重钉

**文件：** 见上「修改（任务 1）」。

- [ ] **步骤 1：重钉 test_reflow.cpp 用例 2/11（先写新断言）**

用例 2（:62-71）整函数替换（函数改名，main 注册同步改）：

```cpp
// 2. 变窄重切：80 列 1 行硬行 -> 40 列 2 行链（M16：硬行多行化，内容不丢，往返恢复）
static void testNarrowHardLineMultiLines()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, std::string(80, 'a'), false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(!out[1].wrapped());
    ZZ_TEST_EXPECT(lineText(out[0]) == std::string(40, 'a'));
    ZZ_TEST_EXPECT(lineText(out[1]) == std::string(40, 'a'));
    // 拉大往返：沿 wrapped 链合并，恢复单行硬行。
    auto back = zzReflowLines(std::move(out), 40, 80);
    ZZ_TEST_EXPECT(back.size() == 1);
    ZZ_TEST_EXPECT(!back[0].wrapped());
    ZZ_TEST_EXPECT(lineText(back[0]) == std::string(80, 'a'));
}
```

main()（:378）注册行 `testNarrowTruncatesHardLine();` 改为 `testNarrowHardLineMultiLines();`

用例 11（:216-239）整函数替换（函数名不变）：

```cpp
// 11. 硬行宽字符落新列宽边界：前移落下行（M16：多行化），内容完整、无续格泄漏
static void testHardLineWideCharAtBoundary()
{
    // 10 列硬行：8 个窄字符 + 第 8 列 WideLead（占 8-9 两列），reflow 到 9 列。
    std::vector<ZzLine> lines;
    ZzLine l0(10);
    for (int i = 0; i < 8; ++i) {
        ZzCell c; c.setWidth(ZzCellWidth::Narrow); c.setCodePoint(U'a' + i);
        l0.setCell(i, c);
    }
    ZzCell lead; lead.setWidth(ZzCellWidth::WideLead); lead.setCodePoint(0x4E2D);
    ZzCell cont; cont.setWidth(ZzCellWidth::WideContinuation);
    l0.setCell(8, lead);
    l0.setCell(9, cont);
    lines.push_back(std::move(l0));
    auto out = zzReflowLines(std::move(lines), 10, 9);
    // M16：宽字符不落边界（9 列行末仅剩 1 列）→ 前移落第 2 行，两行成链。
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(!out[1].wrapped());
    ZZ_TEST_EXPECT(out[0].cellCount() == 9);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 8) == "abcdefgh");
    ZZ_TEST_EXPECT(out[0].cellAt(8).isEmpty()); // 边界补空白
    ZZ_TEST_EXPECT(out[1].cellAt(0).width() == ZzCellWidth::WideLead);
    ZZ_TEST_EXPECT(out[1].cellAt(0).codePoint() == 0x4E2D);
    ZZ_TEST_EXPECT(out[1].cellAt(1).width() == ZzCellWidth::WideContinuation);
    for (int i = 2; i < out[1].cellCount(); ++i)
        ZZ_TEST_EXPECT(out[1].cellAt(i).width() != ZzCellWidth::WideContinuation); // 无续格泄漏
}
```

- [ ] **步骤 2：重钉 test_scrollback.cpp 用例 3/8/9**

用例 3（:82-89 的注释与断言段）替换为：

```cpp
    // 链 abcd/efgh -> ab/cd/ef/gh（4 行）；tail -> ta/il（M16：硬行多行化 2 行链）
    ZZ_TEST_EXPECT(sb->lineCount() == 6);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "ab");
    ZZ_TEST_EXPECT(sb->lineAt(0).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(3), 2) == "gh");
    ZZ_TEST_EXPECT(!sb->lineAt(3).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(4), 2) == "ta");
    ZZ_TEST_EXPECT(sb->lineAt(4).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(5), 2) == "il");
    ZZ_TEST_EXPECT(!sb->lineAt(5).wrapped());
```

用例 8 的期望生成器（:219-238）整段替换为：

```cpp
    // 期望产出（M16 多行化）：8->4 时每条硬行（编号 5 字符有效内容）变 2 行链
    //（"L%04" wrapped + "d" 非 wrapped）；4->8 沿链合并恢复 "L%04d" 单行硬行。
    // 链流按 newCols 顺序切块（编号序列严格递增无缺无重），除链末行外 wrapped 全 true。
    auto expectedRows = [&](int newCols) {
        std::vector<std::pair<std::string, bool>> rows;
        for (int i = 0; i < 600; ++i) {
            if (i >= 250 && i <= 260) {
                if (i == 250)
                    for (std::size_t off = 0; off < chainStream.size();
                         off += (std::size_t)newCols)
                        rows.emplace_back(chainStream.substr(off, (std::size_t)newCols),
                                          off + (std::size_t)newCols < chainStream.size());
                continue;
            }
            std::snprintf(buf, sizeof(buf), "L%04d", i);
            const std::string t(buf); // "L%04d"，5 字符
            if (newCols == 4) {
                rows.emplace_back(t.substr(0, 4), true);  // "L%04" wrapped
                rows.emplace_back(t.substr(4, 1), false); // "d" 链末行
            } else {
                rows.emplace_back(t, false); // 往返恢复完整编号硬行
            }
        }
        return rows;
    };
```

用例 9 的期望生成段（:294-305）替换为：

```cpp
    // 期望产出（8->2，M16 多行化）：每条编号硬行（5 字符有效内容）变 3 行链
    //（"L%" wrapped、"04" wrapped、"d" 非 wrapped）；链流按 2 列顺序切块
    //（编号序列严格递增无缺无重），除链末行外 wrapped 全 true。
    std::vector<std::pair<std::string, bool>> rows;
    for (int i = 0; i < 300; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        const std::string t(buf);
        rows.emplace_back(t.substr(0, 2), true);
        rows.emplace_back(t.substr(2, 2), true);
        rows.emplace_back(t.substr(4, 1), false);
    }
    for (std::size_t off = 0; off < chainStream.size(); off += 2)
        rows.emplace_back(chainStream.substr(off, 2),
                          off + 2 < chainStream.size());
```

用例 9 容量核算（审查锚点）：300 硬行 x 3 + 链流 1199 行 = 2099 行 < 容量 4096，不触发裁剪，lineCount 全等断言成立。用例 8 容量同为 4096（:199 构造）：窄档 589 x 2 + 链 22 = 1200 行、宽档 589 + 链 11 = 600 行，均充足。

- [ ] **步骤 3：test_native_reflow.cpp 注释更新 + 补 A 断言**

:45-47 的注释与 foundChain 段之后（:56 `ZZ_TEST_EXPECT(foundChain);` 行后）插入：

```cpp
    // M16：A（20 列整宽硬行）缩列同样多行化为 2 行链且内容完整
    //（取代原「硬行永不多行化截断、不参与断言」注释）。
    bool foundA = false;
    for (std::size_t i = 0; i + 1 < term.scrollback().lineCount(); ++i) {
        const ZzLine& l0 = term.scrollback().lineAt(i);
        const ZzLine& l1 = term.scrollback().lineAt(i + 1);
        if (l0.wrapped() && !l1.wrapped() && l0.cellAt(0).codePoint() == U'a'
            && l1.cellAt(9).codePoint() == U'x')
            foundA = true;
    }
    ZZ_TEST_EXPECT(foundA); // A 多行化：首行 10 个 a wrapped，链末行 9 个 a + x
```

同时把 :45-47 的原注释（「A 变宽后恰为整宽硬行，按 zzReflowLines 设计"硬行永不多行化"截断，不参与重切断言」）删除（已被上方 M16 注释取代）。

- [ ] **步骤 4：运行三个测试验证红**

运行：
```bash
cmake --build --preset linux-gcc-debug --target test_reflow test_scrollback test_native_reflow
./build/linux-gcc-debug/tests/test_reflow; ./build/linux-gcc-debug/tests/test_scrollback; ./build/linux-gcc-debug/tests/test_native_reflow
```
预期：三者均 FAIL——test_reflow 用例 2（out.size()==2 实际为 1）与用例 11（out.size()==2 实际为 1）；test_scrollback 用例 3（lineCount==6 实际为 5）、用例 8/9（行数与 wrapped 不匹配）；test_native_reflow（foundA==false）。记录实际失败行确认形态符合。

- [ ] **步骤 5：删除 Reflow.cpp 硬行特判 + Reflow.h 注释重写**

`src/screen/Reflow.cpp` 三处。其一，删除 :21 的判定行：

```cpp
    const bool isHardLine = (chainLen == 1) && !chainLines[0].wrapped();
```

其二，:47-49 的 limit 计算整段删除，热循环条件改用 trimEnd——即把 :56 的 `for (std::size_t s = 0; s < limit; ++s)` 与 :62 的 `if (s + 1 < limit)` 中的 `limit` 都改为 `trimEnd`（limit 变量随之不再需要）。

其三，:71-76 的边界分支删除硬行特判，改为无条件前移换行：

```cpp
        if (outCol + w > newCols) {
            // 宽字符落边界：本行以默认空白收尾，提前换行。
            flushRow(true);
        }
```

`src/screen/Reflow.h` 的 zzReflowLines 注释（:26-30 @note 段）整段替换为：

```cpp
 * @return 重组后的物理行序列（每行 newCols 列，wrapped 标记已重算）。
 * @note 链按新列宽重切：内容超宽的多行化；硬行（未 wrapped 的单行链）即
 *       chainLen==1 普通链——缩列多行化、拉大沿 wrapped 链合并恢复
 *       （M16，与 Contour/xterm 对齐，取代 M4 的硬行截断语义）；
 *       链末尾的完全默认空白格被裁除（避免短行变窄产生幽灵行）；
 *       宽字符原子搬运不落边界（边界前移一格补默认空白）；
 *       cluster 格在新行重新 internCluster。
```

- [ ] **步骤 6：运行三个测试验证绿 + 全基线**

运行：
```bash
cmake --build --preset linux-gcc-debug
./build/linux-gcc-debug/tests/test_reflow && ./build/linux-gcc-debug/tests/test_scrollback && ./build/linux-gcc-debug/tests/test_native_reflow
ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
```
预期：三测试 exit 0；**56/56**（test_stream_equivalence 等同文件其余用例不受影响，调研已核实：用例 1/3/4/5/6/7/8/9/10/12 无硬行截断断言）、**44/44**、**56/56**（计数不变——本任务无新测试文件，只有重钉）；doxygen exit 0。注意：若有未预判的既有用例失败（如 perf 门控因缩列产出增多超时），先按推演核对是否为语义演进的必然结果，是则按新语义重钉并在报告附推演，不得回退内核。

- [ ] **步骤 7：Commit**

```bash
git add src/screen/Reflow.cpp src/screen/Reflow.h tests/unit/test_reflow.cpp \
    tests/unit/test_scrollback.cpp tests/unit/test_native_reflow.cpp
git commit -m "feat(reflow): M16 硬行语义对齐——缩列多行化拉大接回，删截断特判"
```

---

## 任务 2：facade 恢复 + compat 转正 + 文档收尾

**文件：** 见计划头部「修改（任务 2）」。

- [ ] **步骤 1：恢复 M5a 避让断言（test_terminal_selection.cpp:68-82）**

`testResizeReflowKeepsSelection` 整函数替换为：

```cpp
static void testResizeReflowKeepsSelection()
{
    ZzTerminal term(5, 3, ZzBackendKind::Native, 100);
    feed(term, "abcdefgh");
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 8});
    const std::string before = term.selectedText();
    term.resize(10, 3); // 列变触发 reflow，逻辑行集合不变
    ZZ_TEST_EXPECT(term.selectedText() == before);
    // M16 恢复 M5a 原文断言 resize(4,3)（M5a 计划 :1908 记载的避让随硬行
    // 截断语义废弃而失效）：硬行缩列多行化后逻辑行集合不变，选区文本保持。
    term.resize(4, 3);
    ZZ_TEST_EXPECT(term.selectedText() == before);
}
```

- [ ] **步骤 2：compat 新增硬行缩列对照用例**

`tests/unit/test_backend_compat.cpp` 在用例 19（testResizeReflowCjk，:427-441）后、匿名命名空间结束前新增：

```cpp
// 21. 硬行缩列 reflow parity（M16）：硬行缩列多行化、拉大接回，两后端
// 历史行数/历史文本/wrapped 标记/屏幕逐格一致——M4 钉住的硬行 b 类分歧消灭。
void testResizeReflowHardLine()
{
    Dual d;
    const std::string hard(60, 'h'); // 80 列下的硬行（60 字符 + 尾空白），未软折
    d.feedBoth(hard + "\r\n");
    for (int i = 0; i < 30; ++i)
        d.feedBoth("filler\r\n"); // 顶入历史

    d.native.resize(40, 24); // 缩列：硬行多行化（40 + 20 两行链）
    d.contour.resize(40, 24);
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        ZZ_CHECK(historyText(d.native, i) == historyText(d.contour, i));
        ZZ_CHECK(d.native.historyView().lineAt(i).wrapped()
                 == d.contour.historyView().lineAt(i).wrapped());
    }
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 40, "hardline-40");

    d.native.resize(80, 24); // 拉大：链接回，内容完整恢复
    d.contour.resize(80, 24);
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        ZZ_CHECK(historyText(d.native, i) == historyText(d.contour, i));
        ZZ_CHECK(d.native.historyView().lineAt(i).wrapped()
                 == d.contour.historyView().lineAt(i).wrapped());
    }
    for (int r = 0; r < 24; ++r)
        checkRowEqualAllowEmptyWidthDiff(d.native, d.contour, r, 80, "hardline-80");
}
```

main() 注册（`testResizeReflowCjk();` 行后）：`testResizeReflowHardLine();`

注意（审查锚点）：historyText 帮手为 M15 已加（本文件匿名命名空间内）。wrapped parity 依赖 contour 历史快照的 flag 归一化（ZzContourBackend.h:75-77 承诺按 ZzLine「续到下一行为 true」语义返回）——若 wrapped 断言失败而文本 parity 成立，先核对 ZzContourBackend.cpp 快照口的 flag 换算再定钉住方式，不得直接删断言。光标位置不做对照（contour shrinkColumns 的光标跟踪粗糙，fork Grid.cpp:1126 标 TODO；本用例聚焦内容 parity）。

- [ ] **步骤 3：运行验证**

运行：
```bash
cmake --build --preset linux-gcc-debug --target test_terminal_selection test_backend_compat
./build/linux-gcc-debug/tests/test_terminal_selection && ./build/linux-gcc-debug/tests/test_backend_compat
```
预期：exit 0（内核已在任务 1 翻转，本条为恢复+新增覆盖，直接绿）。若 resize(4,3) 断言失败，推演选区锚点在多行化下的逻辑行坐标（逻辑行集合不变、锚点无需平移），先查推演再查实现；不得改回 resize(8,3)。

- [ ] **步骤 4：文档改写（五处）**

其一，`docs/API.md:91` 所在语义句：

旧：`  不拆半，硬行截断/补空，光标按逻辑行链跟随内容；行变化（M15，双后端`
新：`  不拆半，硬行缩列多行化、拉大沿 wrapped 链合并恢复（M16，对齐 Contour/xterm，`
`  取代 M4 硬行截断），光标按逻辑行链跟随内容；行变化（M15，双后端`

其二，`docs/API.md` 的 M5b 截断行快照条款（:220-223 附近，以「5. 截断行 col 快照语义」开头的整段）整段删除，替换为一行：

```markdown
5. （M16 起本条废止：硬行缩列多行化后不存在截断行，match 的 col 均在行内。）
```

其三，`docs/API.md` 版本与 ABI 策略节（grep -n "版本与 ABI" docs/API.md 定位）追加 M16 条目：

```markdown
- M16：行为语义变化（reflow 硬行由截断改为多行化，zzReflowLines/两端 reflow
  路径对外可观察结果变化），无 API 签名变化；M5b 的「截断行 col 快照」
  条款随之废止。
```

其四，`docs/VT-Xterm-Checklist.md:118`：

旧：`-   [x] Hard-newline preservation（硬行永不多行化，M4）`
新：`-   [x] Hard-newline preservation（硬行缩列软折、拉大接回，M16 取代 M4 截断语义）`

其五，`docs/VT-Xterm-Checklist-v2.md:181-182` 的 `-   [ ] \[Contour\]\[Test\] Hard-newline preservation` 一行含义随 M16 更新（在该行末追加括注）：

旧：`-   [ ] \[Contour\]\[Test\] Hard-newline preservation`
新：`-   [ ] \[Contour\]\[Test\] Hard-newline preservation（语义以 M16 为准：硬行软折、拉大接回）`

- [ ] **步骤 5：M13 记录现象标注**

`docs/superpowers/specs/2026-09-29-m13-spike-record.md` §4 现象 5 的「M15 已根治」标注处，把该行末的标注扩为：

```markdown
**M15/M16 已根治**：行向截断不压历史由 M15 对齐 contour 根治（缩行压历史/扩行回抽）；列向硬行截断由 M16 对齐 contour/xterm 根治（缩列多行化/拉大接回，spec 2026-09-30-m16-hardline-reflow-design.md）
```

（操作方式：先 grep -n "M15 已根治" docs/superpowers/specs/2026-09-29-m13-spike-record.md 定位该行，整行替换。）

- [ ] **步骤 6：全基线 + doxygen + Commit + 推送 + CI**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
git add tests/unit/test_terminal_selection.cpp tests/unit/test_backend_compat.cpp \
    docs/API.md docs/VT-Xterm-Checklist.md docs/VT-Xterm-Checklist-v2.md \
    docs/superpowers/specs/2026-09-29-m13-spike-record.md
git commit -m "feat(reflow): M16 选区断言恢复+compat 硬行对照转正+文档收尾"
git push origin contour
gh run list --branch contour --limit 7
```
预期：**56/56**、**44/44**、**56/56**（计数不变，compat 为既有目标内新增用例）；doxygen exit 0；CI 7 个 workflow 全绿。

---

## 收尾（全部任务完成后）

- ZzTermCore 仓打 tag `m16` 并推送（`git tag m16 && git push origin m16`）。
- 人工复验移交（用户执行）：`cd /home/zz/Jackfahdin/github/ZzClawTerm && ./build/spike-debug/zzcore_spike --local`——① 跑出多栏 ls 后反复拖窄/拉宽窗口：内容应软折行且**不再截断**，拉宽后**接回原始长行**（对比 M15 时代的断词现象）；② M15 行向行为保持（拖矮压历史、拖高回抽、滚轮可见）；③ 行尾空白格不保证恢复（既有 trim 语义，与 contour/xterm 一致）。
