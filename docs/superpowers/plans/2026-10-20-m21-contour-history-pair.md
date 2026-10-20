# M21 contour 后端历史两件落地 + 两件登记 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** contour 后端落地 `primaryLineCount()` 与 `resizeScreenScrollout()` 两个历史 API 覆写（后者为 contour 变体口径），`clearCount()`/`prependHistoryLines()` 登记为上游能力缺口，规格 `docs/superpowers/specs/2026-10-19-m21-contour-history-pair-design.md`。

**架构：** 全部改动在 ZzTermCore contour 分支，落点三处——`ZzContourBackend`（新增主屏历史读通道）、`ZzContourHistoryView`（覆写 primaryLineCount）、`ZzContourBackendAdapter`（resize 净迁移计数 + 覆写）。零 patch 红线：不改 `third_party/contour` 任何字节。

**技术栈：** C++20（主库）/ C++23（contour 后端）、contour vtbackend（pin 01f68347）、自研轻量测试框架（`ZZ_TEST_EXPECT` 宏，非 gtest）。

**通用约定：**
- 构建：`cmake --build build/linux-gcc-debug -j$(nproc)`（该目录 ZZTERM_WITH_CONTOUR=ON）
- 测试：`ctest --test-dir build/linux-gcc-debug --output-on-failure`，基线 **63/63**；CONTOUR=OFF 路径（build/m1b-off-check）52/52 任务 3 复核
- commit：`type(scope): 中文描述`，直接 commit 到 contour 分支；push 与 tag 在任务 3 统一执行
- 行号以 contour HEAD=7c2d614 为准；漂移按符号名定位
- contour 双后端测试范式：同用例手工构造两个 `ZzTerminal`（native 基准 + contour 对拍），见 `tests/unit/test_historyview_compat.cpp` 既有 `testHistoryTextParity`

---

## 任务 1：`primaryLineCount()` contour 覆写

**文件：**
- 修改：`src/backend/contour/ZzContourBackend.h`（:71 `historyLineCount()` 声明后加新方法）
- 修改：`src/backend/contour/ZzContourBackend.cpp`（:234 `historyLineCount()` 实现后加实现）
- 修改：`src/backend/contour/ZzContourHistoryView.h` / `.cpp`（覆写）
- 测试：`tests/unit/test_historyview_compat.cpp`（新增用例 + main 注册）

**背景：** contour `Terminal::primaryScreen()` 是 public（`third_party/contour/src/vtbackend/screen/Terminal.hpp:2382-2383`），Alternate 期间主屏 Grid 及其历史保留。默认回退 `HistoryView.h:48-51` 返回 `lineCount()`，Alternate 期间 contour 视图归 0——与 native 的 buffer 无关口径分歧。

- [ ] **步骤 1：编写失败的测试**

`tests/unit/test_historyview_compat.cpp` 追加（放在 `testContourGenerationAndAlternate` 之后）：

```cpp
// M21：primaryLineCount buffer 无关口径双后端对拍
static void testPrimaryLineCountParity()
{
    ZzTerminal native(10, 4, ZzBackendKind::Native, 100);
    ZzTerminal contour(10, 4, ZzBackendKind::Contour, 100);
    for (int i = 0; i < 8; ++i) {
        feedStr(native, "x\r\n");
        feedStr(contour, "x\r\n");
    }
    ZZ_TEST_EXPECT(native.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(contour.historyView().primaryLineCount() == 5); // Primary 下同口径
    feedStr(native, "\x1b[?1049h");
    feedStr(contour, "\x1b[?1049h"); // 进 Alternate：可见历史归零
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 0);
    // buffer 无关口径：Alternate 期间仍返回 Primary 历史真实行数
    //（默认回退 lineCount() 会归 0，本断言驱动覆写落地）
    ZZ_TEST_EXPECT(contour.historyView().primaryLineCount() == 5);
    ZZ_TEST_EXPECT(native.historyView().primaryLineCount()
                   == contour.historyView().primaryLineCount());
    feedStr(native, "\x1b[?1049l");
    feedStr(contour, "\x1b[?1049l"); // 回 Primary：历史恢复可见
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(contour.historyView().primaryLineCount() == 5);
}
```

`main()` 中注册调用 `testPrimaryLineCountParity();`（位置：既有用例调用序列尾部，`g_failures` 汇总之前）。

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build build/linux-gcc-debug -j$(nproc) --target test_historyview_compat
./build/linux-gcc-debug/tests/test_historyview_compat
```

预期：FAIL——`contour.historyView().primaryLineCount() == 5`（Alternate 期间实际返回 0，默认回退行为）。

- [ ] **步骤 3：实现**

`src/backend/contour/ZzContourBackend.h`，在 `historyLineCount()` 声明（:71）后加：

```cpp
    /// \brief 主屏（Primary）历史行数（buffer 无关口径，Alternate 期间仍读主屏）。
    [[nodiscard]] int primaryHistoryLineCount() const;
```

`src/backend/contour/ZzContourBackend.cpp`，在 `historyLineCount()` 实现后加：

```cpp
int ZzContourBackend::primaryHistoryLineCount() const
{
    return impl_->terminal->primaryScreen().historyLineCount().value;
}
```

`src/backend/contour/ZzContourHistoryView.h`，在 `lineCount()` 覆写声明后加：

```cpp
    [[nodiscard]] std::size_t primaryLineCount() const noexcept override;
```

`src/backend/contour/ZzContourHistoryView.cpp`，在 `lineCount()` 实现后加：

```cpp
std::size_t ZzContourHistoryView::primaryLineCount() const noexcept
{
    return static_cast<std::size_t>(backend_->primaryHistoryLineCount());
}
```

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build build/linux-gcc-debug -j$(nproc) --target test_historyview_compat
./build/linux-gcc-debug/tests/test_historyview_compat && echo PASS
```

预期：PASS（exit 0）。若 Alternate 期间行数不是 5，先查上游 `?1049h` 光标携带语义演进（04edd8a 登记处）是否影响了历史保留前提，不要放松断言。

- [ ] **步骤 5：全量回归 + Commit**

```bash
cmake --build build/linux-gcc-debug -j$(nproc)
ctest --test-dir build/linux-gcc-debug --output-on-failure   # 63/63+新增 全绿
git add src/backend/contour/ZzContourBackend.h src/backend/contour/ZzContourBackend.cpp \
        src/backend/contour/ZzContourHistoryView.h src/backend/contour/ZzContourHistoryView.cpp \
        tests/unit/test_historyview_compat.cpp
git commit -m "feat(backend): M21 contour 覆写 primaryLineCount——Alternate 期间走主屏历史，buffer 无关口径对齐 native"
```

---

## 任务 2：`resizeScreenScrollout()` contour 覆写（变体口径）

**文件：**
- 修改：`src/backend/contour/ZzContourBackendAdapter.cpp`（adapter 类内：成员 + resize() 计数 + 覆写）
- 测试：`tests/unit/test_historyview_compat.cpp`（新增两用例 + main 注册）

**背景：** 基类默认回退 0（`src/backend/ZzTerminalBackend.h:60`）；native 语义是「resize 期间毛入史 − 接缝回拉」净迁移（`ZzNativeBackend.cpp:318-322`）。contour 列变 reflow 重建行身份，只能做「resize 前后主屏历史行数净增量、负值归零」的变体口径（规格 §2）。adapter `resize()` 已有 `historyBefore` 捕获（`ZzContourBackendAdapter.cpp:72`），但那是 currentScreen 口径，本任务用任务 1 的 `primaryHistoryLineCount()` 主屏口径。

- [ ] **步骤 1：编写失败的测试**

`tests/unit/test_historyview_compat.cpp` 追加：

```cpp
// M21：纯行变收缩的屏→史迁移计数与 native 对拍（列宽不变、无 reflow，两口径等价）
static void testResizeScrolloutRowShrinkParity()
{
    ZzTerminal native(40, 6, ZzBackendKind::Native, 100);
    ZzTerminal contour(40, 6, ZzBackendKind::Contour, 100);
    for (int i = 0; i < 8; ++i) {
        feedStr(native, "segment\r\n");
        feedStr(contour, "segment\r\n");
    }
    ZZ_TEST_EXPECT(native.historyView().lineCount() == 3);
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 3);
    ZZ_TEST_EXPECT(native.resize(40, 3));  // 行收缩：屏 3 行迁移入史
    ZZ_TEST_EXPECT(contour.resize(40, 3));
    ZZ_TEST_EXPECT(native.resizeScreenScrollout() == 3);
    ZZ_TEST_EXPECT(contour.resizeScreenScrollout() == 3); // 默认回退 0，本断言驱动覆写
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 6);
    // 扩行回抽为负 → 归零（调用方走收缩向兜底）
    ZZ_TEST_EXPECT(contour.resize(40, 6));
    ZZ_TEST_EXPECT(contour.resizeScreenScrollout() == 0);
}

// M21：列变 reflow 钉住变体口径本身（数值 = max(0, 主屏历史净增量)），不与 native 对拍
static void testResizeScrolloutColumnChangeVariant()
{
    ZzTerminal term(40, 6, ZzBackendKind::Contour, 100);
    for (int i = 0; i < 8; ++i)
        feedStr(term, "segment-abcdefgh0123456789-abcdefgh0123\r\n"); // 40 列恰好一行
    const std::size_t before = term.historyView().lineCount();
    ZZ_TEST_EXPECT(before == 3);
    ZZ_TEST_EXPECT(term.resize(24, 6)); // 列变触发 reflow
    const std::size_t after = term.historyView().lineCount();
    const std::size_t expected = after > before ? after - before : 0;
    ZZ_TEST_EXPECT(term.resizeScreenScrollout() == expected); // 钉住变体口径定义
    ZZ_TEST_EXPECT(term.historyView().lineCount() > 0);       // 历史可读不崩
}
```

`main()` 注册两例。

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build build/linux-gcc-debug -j$(nproc) --target test_historyview_compat
./build/linux-gcc-debug/tests/test_historyview_compat
```

预期：FAIL——`contour.resizeScreenScrollout() == 3`（默认回退 0）。

若 `native.resizeScreenScrollout() == 3` 或 `lineCount() == 3` 前置断言失败，说明 40 列下 "segment\r\n" 行布局与假设不符，先打印实际值修正前置（行数推导：8 行输出、屏 6 行，滚出 3 行含末行光标推进，与 `test_historyview.cpp::test_resizeScreenScrolloutCountsMigratedLines` 同构），口径断言结构不变。

- [ ] **步骤 3：实现**

`src/backend/contour/ZzContourBackendAdapter.cpp`，adapter 类内三处：

成员区（`lastDropped_` 声明附近）加：

```cpp
    std::size_t resizeScrollout_ = 0; // M21：上一次 resize 主屏历史净增量（负值归零）
```

`resize(int cols, int rows)` 内，`const int historyBefore = backend_->historyLineCount();` 之后加捕获，`backend_->resize(cols, rows);` 之后加计算：

```cpp
        const int primaryHistBefore = backend_->primaryHistoryLineCount();
        backend_->resize(cols, rows);
        // M21：resize 屏→史迁移计数（contour 变体口径：主屏历史行数净增量，
        // 负值归零；列变 reflow 重建行身份时为近似值，偏离登记见 API.md M21）
        const int primaryHistAfter = backend_->primaryHistoryLineCount();
        resizeScrollout_ = primaryHistAfter > primaryHistBefore
            ? static_cast<std::size_t>(primaryHistAfter - primaryHistBefore)
            : 0;
```

覆写（放在 `isAlternateScreen()` 覆写附近）：

```cpp
    std::size_t resizeScreenScrollout() const noexcept override { return resizeScrollout_; }
```

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build build/linux-gcc-debug -j$(nproc) --target test_historyview_compat
./build/linux-gcc-debug/tests/test_historyview_compat && echo PASS
```

预期：PASS。若 `testResizeScrolloutRowShrinkParity` 中 contour 侧行变收缩计数与 native 不等（contour 行收缩迁移策略差异），打印双端实际值；确认是 contour Grid 行变语义差异（而非实现错误）后，把该用例改为分别钉住双端实际值并注释偏离原因——口径断言结构不变。

- [ ] **步骤 5：全量回归 + Commit**

```bash
cmake --build build/linux-gcc-debug -j$(nproc)
ctest --test-dir build/linux-gcc-debug --output-on-failure
git add src/backend/contour/ZzContourBackendAdapter.cpp tests/unit/test_historyview_compat.cpp
git commit -m "feat(backend): M21 contour 覆写 resizeScreenScrollout——主屏历史净增量变体口径，负值归零"
```

---

## 任务 3：两件登记 + 文档收口

**文件：**
- 修改：`docs/API.md`（版本与 ABI 策略节，M20 条目后加 M21 条目）
- 修改：`docs/Scrollback-and-Reflow.md`（第 10 节启用清单状态更新）

- [ ] **步骤 1：`docs/API.md` 加 M21 条目**

M20 条目（:469-485）之后追加：

```markdown
- M21：contour 后端历史能力两件落地、两件登记（行为补全，公共头无签名
  变化）。落地：`ZzContourHistoryView::primaryLineCount()` 覆写（Alternate
  期间读主屏 Grid，buffer 无关口径对齐 native）；
  `ZzContourBackendAdapter::resizeScreenScrollout()` 覆写——contour 变体
  口径为「resize 前后主屏历史行数净增量，负值归零」：纯行变收缩时与
  native 净迁移口径等价；列变 reflow 时 contour 重建行身份，数值为近似
  （已知偏离，不钉双端一致）；扩行回抽为负归零，调用方走收缩向兜底。
  登记（contour 上游能力缺口，维持默认回退）：`clearCount()`——contour
  ED3 清史不转发 Events 监听器且清的是 currentScreen（与 xterm/native
  「恒清 Primary」分歧），解封条件为上游提供清史事件并 Align 语义；
  `prependHistoryLines()`——contour Grid 无最旧端注入 API，解封条件为
  上游提供注入原语。规格：docs/superpowers/specs/2026-10-19-m21-contour-history-pair-design.md。
```

- [ ] **步骤 2：`docs/Scrollback-and-Reflow.md` 第 10 节状态更新**

第 10 节「contour 后端启用清单（Z3 收官登记）」末尾追加：

```markdown
M21 状态更新（2026-10-20）：`primaryLineCount()` 与
`resizeScreenScrollout()` 已落地（后者为变体口径，见 API.md M21）；
`clearCount()` 与 `prependHistoryLines()` 确认为 contour 上游能力缺口
（ED3 无事件钩子 + Grid 无最旧端注入 API），维持登记，解封条件见
API.md M21 条目。上游 PR 评估为独立后续事项。
```

- [ ] **步骤 3：双路径全量回归 + Commit + push**

```bash
cmake --build build/linux-gcc-debug -j$(nproc)
ctest --test-dir build/linux-gcc-debug --output-on-failure          # CONTOUR=ON 全绿
cmake --build build/m1b-off-check -j$(nproc)                        # CONTOUR=OFF 路径复核
ctest --test-dir build/m1b-off-check --output-on-failure            # 52/52 不受影响
git add docs/API.md docs/Scrollback-and-Reflow.md
git commit -m "docs(m21): contour 历史两件落地两件登记——API.md M21 条目 + 启用清单状态更新"
git push origin contour   # 连同任务 1/2 的提交一并推送
```

- [ ] **步骤 4：tag**

CI 绿（用户确认）后：

```bash
git tag m21 && git push origin m21
```

---

## 自检记录

- **规格覆盖**：规格 §1 → 任务 1；§2 → 任务 2；§3（两件登记 + 启用清单更新 + 上游 PR 记录）→ 任务 3；§4 测试（compat 范式、Alternate、行变对拍、列变非负不崩）→ 任务 1/2 步骤 1；§5（API.md 条目、无签名变化、tag m21）→ 任务 3。
- **占位符扫描**：无 TODO/待定；两处「若前置断言失败」分支给出了具体排查依据与修正边界（断言结构不变），属显式披露的实测校准，非占位。
- **类型一致性**：`primaryHistoryLineCount()`（任务 1 定义于 ZzContourBackend，任务 2 消费于 adapter）；`resizeScrollout_`（任务 2 定义并消费）；`resizeScreenScrollout()` 返回 `std::size_t`（与 `ZzTerminalBackend.h:60` 一致）；测试宏 `ZZ_TEST_EXPECT`、`feedStr`、`historyLineText` 沿用 compat 文件既有定义。
- **冲突消解**：三任务串行；任务 1/2 同改 `test_historyview_compat.cpp`（先 1 后 2），任务 2 依赖任务 1 的 `primaryHistoryLineCount()`。
