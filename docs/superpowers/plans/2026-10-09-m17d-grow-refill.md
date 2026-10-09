# M17d 扩行回填 + ED3 清历史实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 窗口扩行时若会话活在底部（光标下方全空行），从滚动历史回抽填满屏幕、提示符沉底；并接线 ED 3 清滚动区使 `clear` 后拉大不复活内容。

**架构：** 改动集中在两处：`ZzScreen::resizeBuffer` 扩行分支的回抽触发条件放宽（Screen 层），native 后端 HistoryPullCallback 接线的折链对齐与 CSI 分发层 ED3 接线（backend 层）。contour 第三方不改，扩行回填登记 compat 偏离，ED3 为 parity 补齐。

**技术栈：** C++20，CMake presets（linux-gcc-debug 等），CTest；诊断重放器在 /tmp（不入库）。

**规格：** `docs/superpowers/specs/2026-10-09-m17d-grow-refill-design.md`（含勘误 E-1：折链对齐向下取整，实现按勘误后语义）。

- 基线：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`（58 例）；
  `cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check`（47）；
  `cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check`（58）；
  fuzz `ctest --preset linux-clang-fuzz -R fuzz`（3）；`doxygen Doxyfile` 零警告。

**纪律：** 测试数据必须手工推演行数账；实测与推演不符时先核账（找出推演错误
或实现 bug），确属计划笔误才在本文末尾登记勘误（格式 `## 勘误 E-N`）。
新建测试文件由 GLOB CONFIGURE_DEPENDS 收编，**需先重新 configure**
（`cmake --preset linux-gcc-debug`）再按 target 构建。

**选区/搜索影响核查结论（计划阶段已核）：** 选区用绝对逻辑行号（M5a），
行在历史与屏幕之间移动不改变其绝对坐标，扩行回填不产生选区漂移；
`ZzTerminal::resize` 切面已有 `noteSelectionAfterFeed`（Terminal.cpp:86）
兜底裁剪丢弃场景。本里程碑无选区新增处理，既有 test_selection_* /
test_search_* 保持绿即佐证。

---

## 文件结构

- 修改 `src/screen/Screen.cpp`：resizeBuffer 扩行分支条件放宽 + 文件内空行判定辅助；M15 注释（:59-62）更新。
- 修改 `include/ZzTerm/Screen.h`：resize 文档注释（:95-99）同步新语义。
- 新建 `tests/unit/test_screen_grow_refill.cpp`：Screen 层扩行回填单测（3 用例）。
- 修改 `src/backend/native/ZzNativeBackend.cpp`：HistoryPullCallback 接线（:213-218）加折链对齐（向下取整）。
- 修改 `src/backend/native/NativeCsiDispatch.cpp`：case 'J'（:107-112）受理 ED 3。
- 新建 `tests/unit/test_native_grow_refill.cpp`：facade 级回填/链对齐/ED3 单测（5 用例）。
- 修改 `tests/unit/test_backend_compat.cpp`：用例 26（扩行回填偏离登记）、用例 27（ED3 parity）。
- 文档：`docs/Architecture-v2.md`、`docs/Scrollback-and-Reflow.md`、`docs/API.md`。
- 诊断工具：重建 `/tmp/zz-resize-repro/replay`（源码随机构建命令在任务 4；不入库）。

---

### 任务 1：Screen 扩行回填条件放宽

**文件：**
- 修改：`src/screen/Screen.cpp`（resizeBuffer 扩行分支 :87-99、M15 注释 :59-62、新增文件内辅助）
- 修改：`include/ZzTerm/Screen.h:95-99`（resize 注释）
- 测试：`tests/unit/test_screen_grow_refill.cpp`（新建）

- [ ] **步骤 1：编写失败的测试**

新建 `tests/unit/test_screen_grow_refill.cpp`：

```cpp
// ZzScreen 扩行回填测试（M17d）：回抽条件从「光标贴末行」放宽为
//「光标下方全空行」（会话活在底部）；部分回填/布局保护。
// 回归搭档：test_screen_rowresize.cpp（M15 八用例，含贴底回抽/Alternate）须保持绿。
#include <cstdio>
#include <string>
#include <vector>

#include "ZzTerm/Screen.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// 用 putCell 写 ASCII 串到指定行。
static void writeRow(ZzScreen& scr, int row, const std::string& text)
{
    for (int i = 0; i < (int)text.size(); ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint((char32_t)text[(std::size_t)i]);
        scr.putCell(ZzPosition{row, i}, c);
    }
}

static std::string rowText(const ZzLine& line, int n)
{
    std::string s;
    for (int i = 0; i < n && i < line.cellCount(); ++i) {
        const ZzCell& c = line.cellAt(i);
        s += (char)(c.isEmpty() ? ' ' : c.codePoint());
    }
    return s;
}

// 构造 cols 列、内容为 text 的行（供 pull 回调返回）。
static ZzLine makeTextLine(int cols, const std::string& text)
{
    ZzLine line(cols);
    for (int i = 0; i < (int)text.size(); ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint((char32_t)text[(std::size_t)i]);
        line.setCell(i, c);
    }
    return line;
}

// 1. 核心新语义：光标不在末行、下方全空 → 回抽注入顶部
static void testGrowRefillBelowCursorBlank()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    writeRow(scr, 1, "c1");                  // 行 2/3 空
    scr.setCursorPosition(ZzPosition{1, 0}); // 光标行 1（非末行 3），下方全空
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        ZZ_TEST_EXPECT(maxLines == 2); // 索取数 = 扩行数
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(10, "h0"));
        pulled.push_back(makeTextLine(10, "h1"));
        return pulled;
    });
    scr.resize(10, 6); // 扩 2：回抽 2 行注入顶部（旧语义光标不贴底不回抽）
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "h1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "c0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "c1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(5), 2) == "  "); // 底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3);  // 1 + 2
}

// 2. 光标在空行上也可回抽 + 历史不足时部分回填、余量补空
static void testGrowRefillCursorOnBlankRowPartial()
{
    ZzScreen scr(10, 5);
    writeRow(scr, 0, "c0");
    writeRow(scr, 1, "c1");                  // 行 2/3/4 空
    scr.setCursorPosition(ZzPosition{2, 0}); // 光标在空行 2 上，下方行 3/4 空
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        ZZ_TEST_EXPECT(maxLines == 2);
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(10, "m0"));
        return pulled; // 历史只剩 1 行：部分回填
    });
    scr.resize(10, 7); // 扩 2，回抽 1
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "m0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "c0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "c1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(6), 2) == "  "); // 余量底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3);  // 2 + 1
}

// 3. 布局保护：光标下方有非空行 → 不回抽（新旧语义一致，钉住）
static void testGrowNoRefillWhenContentBelow()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "c0");
    writeRow(scr, 2, "c2");                  // 行 1/3 空，行 2 有内容
    scr.setCursorPosition(ZzPosition{0, 0}); // 下方行 2 非空 → 不回抽
    bool pullCalled = false;
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.resize(10, 6);
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "c0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "c2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(5), 2) == "  "); // 底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0);
}

int main()
{
    testGrowRefillBelowCursorBlank();
    testGrowRefillCursorOnBlankRowPartial();
    testGrowNoRefillWhenContentBelow();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_screen_grow_refill && ./build/linux-gcc-debug/tests/test_screen_grow_refill`
预期：编译通过（纯既有 API），运行 FAIL——用例 1/2 断言失败（旧语义不回抽，
`lineAt(0)` 仍是 `"c0"`）；用例 3 新旧语义下均通过（钉住用例）。

- [ ] **步骤 3：实现——空行辅助 + 扩行分支条件放宽 + 注释同步**

其一，`src/screen/Screen.cpp` 在 `resizeBuffer` 定义（:63）之前插入文件内辅助：

```cpp
// M17d：空行判定——整行 cell 均 isEmpty（覆盖默认格/擦除格/宽字符续格；
// 显式写入的空格 cell 非 isEmpty，视为内容，保守不回抽）。
static bool zzRowIsBlank(const ZzLine& line) noexcept
{
    for (int c = 0; c < line.cellCount(); ++c)
        if (!line.cellAt(c).isEmpty())
            return false;
    return true;
}
```

其二，扩行分支（:87-99）：

```cpp
    } else if (rows > oldRows) {
        const int k = rows - oldRows;
        if (mayUseHistory && historyPullCallback_
            && buf.cursor.position.row == oldRows - 1) { // 光标贴末行才回抽
```

改为：

```cpp
    } else if (rows > oldRows) {
        const int k = rows - oldRows;
        // M17d：回抽条件放宽——光标下方全空行即「会话活在底部」
        //（旧条件「光标贴末行」是其子集：末行下方无行，全称真空成立）。
        bool liveAtBottom = true;
        for (int r = buf.cursor.position.row + 1; r < oldRows; ++r) {
            if (!zzRowIsBlank(buf.lines[static_cast<std::size_t>(r)])) {
                liveAtBottom = false;
                break;
            }
        }
        if (mayUseHistory && historyPullCallback_ && liveAtBottom) {
```

（分支体内回抽/顶插/`cursor.row +=` 逻辑与底部补空不变。）

其三，`src/screen/Screen.cpp:59-62` 的 M15 注释第四行「扩行：光标贴末行时经
HistoryPullCallback 回抽注入顶部，不足底部补空。」改为：

```cpp
// 扩行：M17d 起 Primary 且光标下方全空行（会话活在底部）时经
// HistoryPullCallback 回抽注入顶部（偏离 contour 仅贴末行回抽，compat
// 用例 26 登记），不足底部补空。
```

其四，`include/ZzTerm/Screen.h:95-99` resize 的 `@note` 末段「扩行仅当光标
贴末行时经 HistoryPullCallback 从最新历史回抽注入顶部，不足部分底部补空。」
改为：

```
     *       扩行回填（M17d）：Primary 且光标下方全空行（会话活在底部）时经
     *       HistoryPullCallback 从最新历史回抽注入顶部、光标随内容下沉
     *       （有意偏离 contour 仅贴末行回抽，compat 用例 26 登记），不足
     *       部分底部补空。
```

（注释内不写裸反斜杠序列与尖括号——doxygen 纪律。）

- [ ] **步骤 4：运行测试验证通过 + 回归**

运行：`cmake --build --preset linux-gcc-debug --target test_screen_grow_refill && ./build/linux-gcc-debug/tests/test_screen_grow_refill`
预期：PASS（3 用例）。
随后：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：58+1=59 例全绿（特别注意 test_screen_rowresize 八用例、
test_terminal_csi、test_screen_reflow 无回归；若旧断言钉住了贴末行语义，
先核账再按新语义修正断言并在 commit 信息中说明）。

- [ ] **步骤 5：Commit**

```bash
git add src/screen/Screen.cpp include/ZzTerm/Screen.h tests/unit/test_screen_grow_refill.cpp
git commit -m "feat(screen): M17d 扩行回填——光标下方全空即回抽历史注入顶部（任务 1）"
```

---

### 任务 2：后端回抽折链对齐 + ED3 清滚动区接线

**文件：**
- 修改：`src/backend/native/ZzNativeBackend.cpp:213-218`（HistoryPullCallback 接线加链对齐）
- 修改：`src/backend/native/NativeCsiDispatch.cpp:107-112`（case 'J' 受理 ED 3）
- 测试：`tests/unit/test_native_grow_refill.cpp`（新建）

- [ ] **步骤 1：编写失败的测试**

新建 `tests/unit/test_native_grow_refill.cpp`：

```cpp
// M17d facade 级测试：扩行回填端到端、回抽折链对齐（向下取整）、
// ED 3 清滚动区、clear 序列后扩行不复活。
// 回归搭档：test_native_rowresize / test_native_reflow_topfill 等须保持绿。
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#include "ZzTerm/Terminal.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void feedStr(ZzTerminal& term, const std::string& s)
{
    term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size()));
}

static std::string screenRowText(const ZzTerminal& term, int row)
{
    const ZzLineView line = term.renderView().lineAt(row);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

static std::string historyLineText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

// 喂 6 行（a..f 各带 CRLF）进 10 列终端：rows=3 时历史 a..d、屏幕 e/f、
// 光标空行 2；rows=4 时历史 a/b/c、屏幕 d/e/f、光标空行 3。
static void feedSixLines(ZzTerminal& term)
{
    for (char c = 'a'; c <= 'f'; ++c) {
        std::string s;
        s += c;
        s += "\r\n";
        feedStr(term, s);
    }
}

// 1. 扩行回填端到端（贴底路径，回归）：缩后扩回历史注入顶部
static void testGrowRefillEndToEnd()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedSixLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 4); // a,b,c,d

    const std::uint64_t g0 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(10, 5)); // 扩 2：光标贴底，回抽 c,d
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2); // 剩 a,b
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "c");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "d");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "e");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "f");
    ZZ_TEST_EXPECT(term.cursor().position.row == 4); // 2 + 2 沉底
}

// 2. 新语义区分器：光标被 CUP 抬离末行、下方全空 → 回抽（旧语义不回抽）
static void testGrowRefillCursorAboveBlankTail()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedSixLines(term);                       // 历史 a,b,c；屏幕 d,e,f
    feedStr(term, "\x1b[3;1H");               // CUP：光标行 2（f 行），行 3 空
    ZZ_TEST_EXPECT(term.resize(10, 6));       // 扩 2：回抽 b,c 顶插
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1); // 剩 a
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "b");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "c");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "d");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "e");
    ZZ_TEST_EXPECT(screenRowText(term, 4) == "f");
    ZZ_TEST_EXPECT(term.cursor().position.row == 4); // 2 + 2
}

// 3. 折链对齐：扩 2 但历史第 2 行深处是链中段 → 向下取整只取 1 行，
//    屏幕首行不 dangling（跨缝整链留在历史）
static void testGrowRefillChainAligned()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxx"); // 15 x：行 0 十 x（wrapped）折行 1 五 x
    feedStr(term, "\r\n");
    feedStr(term, "b\r\n");
    feedStr(term, "c\r\n"); // 滚出 x 头入历史（wrapped 跨缝）
    feedStr(term, "d\r\n"); // 滚出 x 尾入历史（链在历史闭合）
    feedStr(term, "e\r\n"); // 滚出 b
    // 历史 [x*10(w), x*5, b]；屏幕 c,d,e + 光标空行 3
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);
    ZZ_TEST_EXPECT(term.resize(10, 6)); // 扩 2，对齐后只取 1（b）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2); // 整链留历史
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "xxxxxxxxxx");
    ZZ_TEST_EXPECT(historyLineText(term, 1) == "xxxxx");
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "b"); // 非对齐实现会取到 "xxxxx"
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "c");
    ZZ_TEST_EXPECT(term.cursor().position.row == 4); // 3 + 1
}

int main()
{
    testGrowRefillEndToEnd();
    testGrowRefillCursorAboveBlankTail();
    testGrowRefillChainAligned();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

（用例 4/5 属 ED3，在步骤 6 以独立红绿循环追加，保证每个 commit 全绿。）

行数账推演（任务 2 用例 3）：10x4 喂 15 x 得行 0 十 x（wrapped）+ 行 1 五 x，
光标 (5,1)；`\r\n` 光标行 2；`b\r\n` 后光标行 3；`c\r\n` 底部滚动把 x 头
压入历史（跨缝 dangling，M16b 场景）；`d\r\n` 把 x 尾压入历史（链闭合）；
`e\r\n` 把 b 压入历史。历史 = [x*10(w), x*5, b] 共 3 行，屏幕 c/d/e + 空行。
扩 2：对齐初值 n=2，接缝行 lineAt(3-2-1)=lineAt(0) 即 x 头 wrapped=true →
n=1；lineAt(3-1-1)=lineAt(1) 即 x 尾 wrapped=false → 定 n=1，取 [b]。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_native_grow_refill && ./build/linux-gcc-debug/tests/test_native_grow_refill`
预期：编译通过，运行 FAIL——用例 2（旧语义光标不贴底不回抽，行 0 仍是 d）、
用例 3（无对齐，行 0 取到 "xxxxx"）失败；用例 1 新旧均过（贴底回归钉住）。

- [ ] **步骤 3：实现——HistoryPullCallback 折链对齐**

`src/backend/native/ZzNativeBackend.cpp:213-218` 的接线 lambda：

```cpp
    screen_.setHistoryPullCallback([this](std::size_t maxLines) {
        auto pulled = scrollback_->takeNewest(maxLines);
        if (!pulled.empty())
            ++historyGeneration_;
        return pulled;
    });
```

改为：

```cpp
    screen_.setHistoryPullCallback([this](std::size_t maxLines) {
        // M17d 折链对齐（向下取整）：被取块首行必须是链头——接缝行
        //（被取块上方一行）wrapped=true 说明从链中段切开，递减索取数
        // 把跨缝整链留在历史。不向上多取：顶插超过扩行数会在 resize
        // 出口从末尾截断，可能裁到活内容（规格 §3.3 勘误 E-1）。
        // 对 M16c reflow 逆差顶补同生效（dangling 预防全域化）。
        const std::size_t count = scrollback_->lineCount();
        std::size_t n = std::min(maxLines, count);
        while (n > 0 && n < count
               && scrollback_->lineAt(count - n - 1).wrapped())
            --n;
        auto pulled = scrollback_->takeNewest(n);
        if (!pulled.empty())
            ++historyGeneration_;
        return pulled;
    });
```

（`<algorithm>` 的 std::min 在该文件已有同等用法，若无 include 则补。）

- [ ] **步骤 4：运行用例 1-3 验证**

运行：`cmake --build --preset linux-gcc-debug --target test_native_grow_refill && ./build/linux-gcc-debug/tests/test_native_grow_refill`
预期：PASS（3 用例）。
随后回归观察（对齐共享回调的副作用排查）：
`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "topfill|rowresize|reflow|compat"`
预期：全绿。若 test_native_reflow_topfill / test_screen_reflow_topfill /
test_backend_compat 用例 23（testResizeReflowTopFill）断言变化，先核账：
旧 native 若曾在链中段切割（contour 的 LogicalLines 天然链对齐从不切割），
新行为是向 contour 靠拢，修正断言并在 commit 信息说明；否则修实现。

- [ ] **步骤 5：Commit（链对齐单独一笔，全绿）**

```bash
git add src/backend/native/ZzNativeBackend.cpp tests/unit/test_native_grow_refill.cpp
git commit -m "feat(native): M17d 回抽折链对齐——向下取整到链边界防 dangling（任务 2a）"
```

- [ ] **步骤 6：编写 ED3 失败的测试**

`tests/unit/test_native_grow_refill.cpp` 在 `testGrowRefillChainAligned`
之后追加，并在 main() 注册：

```cpp
// 4. ED 3 清滚动区：历史清空、代计数递增、屏幕与光标不动；空历史不计代
static void testEd3ClearsHistory()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedSixLines(term); // 历史 a,b,c
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);

    const std::uint64_t g0 = term.historyView().generation();
    feedStr(term, "\x1b[3J");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "d"); // 屏幕不动
    ZZ_TEST_EXPECT(term.cursor().position.row == 3); // 光标不动

    const std::uint64_t g1 = term.historyView().generation();
    feedStr(term, "\x1b[3J"); // 空历史守卫：不重复计代
    ZZ_TEST_EXPECT(term.historyView().generation() == g1);
}

// 5. clear 序列（H + ED2 + ED3）后扩行不复活内容
static void testClearSequenceNoRefill()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedSixLines(term);
    feedStr(term, "\x1b[H\x1b[2J\x1b[3J"); // clear：清屏 + 清历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.resize(10, 6)); // 扩 2：光标下方全空但历史已空
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(screenRowText(term, 0).empty()); // 不复活
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
}
```

main() 注册追加：

```cpp
    testEd3ClearsHistory();
    testClearSequenceNoRefill();
```

运行：`cmake --build --preset linux-gcc-debug --target test_native_grow_refill && ./build/linux-gcc-debug/tests/test_native_grow_refill`
预期：FAIL——用例 4/5 失败（ED3 当前被分发层忽略，历史仍 3 行、clear
后扩行复活 2 行）。

- [ ] **步骤 7：实现——ED 3 接线**

`src/backend/native/NativeCsiDispatch.cpp:107-112`：

```cpp
    case 'J': { // ED 0/1/2；ED 3（清历史）不在 M1 范围，忽略
        const int p = paramOr(seq, 0, 0);
        if (p <= 2)
            screen_.eraseInDisplay(static_cast<ZzEraseMode>(p), eraseFill());
        break;
    }
```

改为：

```cpp
    case 'J': { // ED 0/1/2 清屏；ED 3 清滚动区（M17d，xterm 语义：不动屏幕/光标）
        const int p = paramOr(seq, 0, 0);
        if (p <= 2)
            screen_.eraseInDisplay(static_cast<ZzEraseMode>(p), eraseFill());
        else if (p == 3 && scrollback_->lineCount() > 0) {
            scrollback_->clear();
            ++historyGeneration_; // M14 同口径：可见历史变化必计代；空历史守卫防空转
        }
        break;
    }
```

ED3 无需斩链处理：历史整体清空后无跨缝链残留，屏幕未动。

- [ ] **步骤 8：运行测试验证通过 + 全量回归**

运行：`cmake --build --preset linux-gcc-debug --target test_native_grow_refill && ./build/linux-gcc-debug/tests/test_native_grow_refill`
预期：PASS（5 用例）。
随后：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：59+1=60 例全绿。

- [ ] **步骤 9：Commit**

```bash
git add src/backend/native/NativeCsiDispatch.cpp tests/unit/test_native_grow_refill.cpp
git commit -m "feat(native): M17d ED 3 清滚动区接线——clear 后扩行不复活（任务 2b）"
```

---

### 任务 3：compat 登记 + 文档同步

**文件：**
- 修改：`tests/unit/test_backend_compat.cpp`（用例 26/27，main 注册）
- 修改：`docs/Architecture-v2.md`、`docs/Scrollback-and-Reflow.md`、`docs/API.md`

- [ ] **步骤 1：compat 用例 26/27**

`tests/unit/test_backend_compat.cpp` 在匿名命名空间内（`testEraseSeverDeviation`
之后、`} // namespace` 之前）追加：

```cpp
// 行文本提取（空格 cell 的 text 为空串，天然去尾）。
std::string rowTextOf(const ZzTerminal& term, int row)
{
    const ZzLineView line = term.renderView().lineAt(row);
    std::string out;
    for (int c = 0; c < line.cellCount(); ++c)
        out += line.cellAt(c).text;
    return out;
}

// 26. 扩行回填 parity 偏离登记（M17d）：行数增加且光标下方全空时，native
// 从历史回抽填满、光标沉底（M17d 语义）；contour 核心仅光标贴旧末行回抽，
// CUP 抬离末行后不回抽、底部补空（第三方冻结不改）。b 类真实语义分歧，
// 分别断言钉住，不强行对齐。
void testGrowRefillDeviation()
{
    ZzTerminal native(10, 4, ZzBackendKind::Native, 100);
    ZzTerminal contour(10, 4, ZzBackendKind::Contour, 100);
    for (auto* term : {&native, &contour}) {
        for (char c = 'a'; c <= 'f'; ++c) { // 历史 a,b,c；屏幕 d,e,f；光标空行 3
            std::string s;
            s += c;
            s += "\r\n";
            term->feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(s.data()), s.size()));
        }
        term->feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>("\x1b[3;1H"), 6)); // CUP 光标行 2
        term->resize(10, 6);
    }
    // native：回抽 b,c 顶插——屏幕 b,c,d,e,f,空；光标行 4；历史剩 a
    ZZ_CHECK(native.historyView().lineCount() == 1);
    ZZ_CHECK(rowTextOf(native, 0) == "b");
    ZZ_CHECK(rowTextOf(native, 1) == "c");
    ZZ_CHECK(native.cursor().position.row == 4);
    // contour：不回抽——屏幕 d,e,f 在顶、底部补空；光标行 2；历史仍 3
    ZZ_CHECK(contour.historyView().lineCount() == 3);
    ZZ_CHECK(rowTextOf(contour, 0) == "d");
    ZZ_CHECK(contour.cursor().position.row == 2);
}

// 27. ED 3 清滚动区 parity（M17d）：ESC [ 3 J 清空历史、屏幕与光标不动。
// contour 原生支持 ED3（xterm 标准扩展），预期 parity；若实测分歧按 b 类
// 登记偏离并分别断言钉住（同步修正 Architecture-v2 登记措辞）。
void testEd3ClearScrollbackParity()
{
    ZzTerminal native(10, 4, ZzBackendKind::Native, 100);
    ZzTerminal contour(10, 4, ZzBackendKind::Contour, 100);
    for (auto* term : {&native, &contour}) {
        for (char c = 'a'; c <= 'f'; ++c) { // 历史 a,b,c；屏幕 d,e,f；光标行 3
            std::string s;
            s += c;
            s += "\r\n";
            term->feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(s.data()), s.size()));
        }
        term->feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>("\x1b[3J"), 4));
    }
    ZZ_CHECK(native.historyView().lineCount() == 0);
    ZZ_CHECK(contour.historyView().lineCount() == 0);
    ZZ_CHECK(rowTextOf(native, 0) == "d"); // 屏幕不动
    ZZ_CHECK(rowTextOf(contour, 0) == "d");
    ZZ_CHECK(native.cursor().position.row == 3); // 光标不动
    ZZ_CHECK(contour.cursor().position.row == 3);
}
```

main() 中 `testEraseSeverDeviation();` 之后注册：

```cpp
    testGrowRefillDeviation();
    testEd3ClearScrollbackParity();
```

核账预案：用例 26 的 contour 侧断言依据「M15 对齐 contour shrinkLines/
growLines（贴末行才回抽）+ trace3 重放实测 contour 欠填同现」推演。若实测
contour 也回填（其条件比推演宽），则用例改写为 parity 钉住（双后端同断言），
Architecture-v2 相应改记「无偏离」；若 contour 行为介于两者之间，找出精确
条件后按 b 类分别钉住。

- [ ] **步骤 2：运行 compat 验证**

运行：`cmake --build --preset linux-gcc-debug --target test_backend_compat && ./build/linux-gcc-debug/tests/test_backend_compat`
预期：PASS（27 用例，含既有）。不符先核账（见步骤 1 预案）。

- [ ] **步骤 3：Commit（compat）**

```bash
git add tests/unit/test_backend_compat.cpp
git commit -m "test(compat): M17d 扩行回填偏离登记与 ED3 parity 对照（任务 3）"
```

- [ ] **步骤 4：文档同步**

其一，`docs/Architecture-v2.md`：`grep -n "M17c" docs/Architecture-v2.md` 定位
parity/偏离登记节，仿 M17c 条目格式追加 M17d 条目：

- 扩行回填（resizeBuffer 扩行分支）：native 为「光标下方全空行即回抽」，
  contour 为「仅光标贴旧末行回抽」——有意偏离，compat 用例 26 钉住；
  动机：用户核心诉求「任意缩拉后内容填满屏幕、提示符沉底」（trace3
  留痕实证 contour 同现欠填）。
- 回抽折链对齐（HistoryPullCallback 接线层向下取整）：dangling 预防
  全域化，对 M16c 顶补同生效；contour 的 LogicalLines 天然链对齐，
  此为向 contour 靠拢的加固，非偏离。
- ED 3 清滚动区：M17d 起支持（此前分发层忽略），xterm 标准语义
  （不动屏幕/光标），compat 用例 27 parity。

其二，`docs/Scrollback-and-Reflow.md`：文末追加「扩行回填（M17d）」小节，
要点照规格 §3/§4/§5：触发条件（Primary + 光标下方全空行）、回抽量与光标
下沉、折链对齐向下取整（含不向上多取的理由）、ED3 清滚动区、已知取舍
（ED2-only 清屏后扩行复活与 kitty/Windows Terminal 一致）。

其三，`docs/API.md`：`grep -n "M17c" docs/API.md` 定位版本节，追加 M17d
条目：

- 行为语义变化：`ZzScreen::resize` 扩行回填条件放宽（注释同步）；
  CSI ED 3 清滚动区（此前忽略）；
- 无新公共接口（`ZzScrollback::clear()` 为既有接口首次接线）；
- ABI 无变化。

- [ ] **步骤 5：Commit（文档）**

```bash
git add docs/Architecture-v2.md docs/Scrollback-and-Reflow.md docs/API.md
git commit -m "docs(m17d): 扩行回填与 ED3 文档同步——parity 登记与语义记录（任务 3）"
```

---

### 任务 4：全量回归 + 重放金标准 + tag/CI + spike 交付

**文件：**
- 诊断工具：`/tmp/zz-resize-repro/replay`（重建，不入库）
- git tag `m17d`

- [ ] **步骤 1：全量回归**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
```

预期：60（58+2 新测试文件）/ 49（47+2）/ 60（58+2）/ 3 全绿，doxygen
零警告。m2-off-check 无 contour（compat 等 8 个 contour 用例文件不编入），
两个新测试文件均为 native/screen 层，均计入。

- [ ] **步骤 2：重放金标准验证**

重建重放器并跑 trace3（欠填不变量口径：resize 后「屏幕非空行数 < 行数
且历史 > 0」即欠填；重放器源码 `/tmp/zz-resize-repro/replay.cpp` 若随
/tmp 丢失，按此口径与 `docs/superpowers/plans/2026-10-08-m17c-erase-chain-sever.md`
任务 5 的双后端结构重建）：

```bash
cd /tmp/zz-resize-repro && ZB=/home/zz/Jackfahdin/github/ZzTermCore/build/linux-gcc-debug && \
g++ -std=c++20 -g replay.cpp -I/home/zz/Jackfahdin/github/ZzTermCore/include \
  -L$ZB -L$ZB/src/backend/contour -L$ZB/contour/vtbackend -L$ZB/contour/vtparser \
  -L$ZB/contour/vtpty -L$ZB/contour/crispy -L$ZB/_deps/libunicode-build/src/libunicode \
  -lZzTermCore -lZzTermContourBackend -lvtbackend -lvtparser -lvtpty -lcrispy-core \
  -lunicode -lunicode_ucd -o replay
stdbuf -oL ./replay /tmp/spike-trace3.bin
```

预期：native 欠填报告从修复前的 2 降为 **0**；终态非空 ≥ 74/75、
cursor.row ≥ 70（提示符沉底附近，列 49 为 readline 8.3 上游 bug 所致，
不动）；contour 终态不变（非空 26/75、欠填 2——偏离已登记）。
若 `/tmp/spike-trace3.bin` 已随 /tmp 清理丢失：以步骤 4 的 spike 手势
复验替代，并在终审记录中注明。

再跑 M17c 留痕回归（若文件存在）：

```bash
stdbuf -oL ./replay /tmp/spike-trace.bin
```

预期：仍无 14 列碎片行、欠填报告 = 0；终态非空行数与光标行号可能因
回填增多而上升（更多历史被拉回屏幕），属 M17d 预期效果，不作钉死断言。

- [ ] **步骤 3：tag 与推送**

```bash
git tag m17d
git push origin contour --tags
gh run list --branch contour --limit 7
```

预期：7 个 workflow 全绿（等末次提交触发的 run 全部完成再确认）。

- [ ] **步骤 4：spike 重建交付用户复验**

```bash
cmake --build /home/zz/Jackfahdin/github/ZzClawTerm/build/spike-debug
```

告知用户复验路径：`cd /home/zz/Jackfahdin/github/ZzClawTerm &&
./build/spike-debug/zzcore_spike --local`，重点手势：长路径目录
（`~/tiplus7100/rk3399_8.1_pudu_core_secureboot`）下 `ll` → 拖至极小窗
→ 拉回最大化。预期：屏幕被历史回填填满、提示符沉底，不再出现底部
空白海；滚动区上方 1-3 条窄窗残骸短行属 M17c 方案 A 预定形态；光标
停提示符中间是 readline 8.3 上游 bug，回车自愈，不用报。
