# M16c 列变 reflow 历史顶补 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 列变 reflow 屏幕内容收缩时从最新历史顶补填满（内容贴底锚定），根治列行同变时 M15 回抽条件失效导致的历史滞留。

**架构：** 改动集中于 `ZzScreen::reflowBuffer` 的「产出不足 rows_」分支：底部补空前先经既有 HistoryPullCallback 索取缺口行插入顶部（等价 M16b prepend 语义，直接操作重组产出向量避免二次搬移），光标跟踪行随顶补数平移。backend 协调顺序不变（M16b 接缝归还 → 历史 reflow → 屏幕 reflow），顶补取到的已是新列宽行。

**技术栈：** C++20，CMake preset 体系，CTest；规格：`docs/superpowers/specs/2026-10-08-m16c-reflow-topfill-design.md`。

**实证基准（2026-10-08 双后端探针，本计划断言值的来源）：** contour 在满屏拉宽时顶补（history 10->4、内容贴底、光标守底行）；在不满屏列行同增时抽干历史（history 2->0、内容顶锚、底部补空）。native 当前两者均为底部补空 + 历史滞留。本计划所有 facade 断言值经探针实测核对。

---

## 文件结构

- 修改：`src/screen/Screen.cpp`（reflowBuffer 顶补分支，约 142-187 行区域）
- 修改：`include/ZzTerm/Screen.h`（reflow 与 HistoryPullCallback 注释更新）
- 创建：`tests/unit/test_screen_reflow_topfill.cpp`（screen 级 6 用例；GLOB 自动收编，无需改 CMake）
- 创建：`tests/unit/test_native_reflow_topfill.cpp`（facade 级 4 用例；GLOB 自动收编）
- 修改：`tests/unit/test_backend_compat.cpp`（追加用例 23 双后端对照）
- 修改：`docs/Scrollback-and-Reflow.md`（§3/§4 改写为顶补语义）
- 修改：`docs/API.md`（88-102 行区域 resize 语义段同步）

---

### 任务 1：ZzScreen 顶补原语 + screen 级测试（TDD）

**文件：**
- 创建：`tests/unit/test_screen_reflow_topfill.cpp`
- 修改：`src/screen/Screen.cpp:166-181`（行数平衡分支）
- 修改：`include/ZzTerm/Screen.h:96-106`（reflow 注释）

- [ ] **步骤 1：编写失败的测试**

新建 `tests/unit/test_screen_reflow_topfill.cpp`（助手函数与 test_screen_rowresize.cpp 同款：writeRow/rowText/makeTextLine/ZZ_TEST_EXPECT）：

```cpp
// ZzScreen::reflow 历史顶补测试（M16c）：列变重组内容收缩时从最新历史
// 顶补填满（内容贴底锚定），历史不足余量补空，Alternate 不顶补。
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

// 1. 拉宽顶补：缺口 1，顶补 1 行入顶部，内容贴底，光标随顶补平移
static void testWidenTopFill()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa"); // 满行，链首
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");         // 链尾：链 12 格
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{3, 2});
    std::size_t asked = 0;
    scr.setHistoryPullCallback([&](std::size_t maxLines) {
        asked = maxLines;
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(20, "h9")); // 20 列（新列宽）
        return pulled;
    });
    scr.reflow(20); // 链 12 格合 1 行：产出 3 行 < 4，缺口 1 顶补
    ZZ_TEST_EXPECT(asked == 1);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h9");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "s2");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3); // 链跟踪行 2 + 顶补 1
    ZZ_TEST_EXPECT(scr.cursor().position.col == 2);
}

// 2. 历史不足：缺口 2 实取 1，余量底部补空
static void testTopFillPartialHistory()
{
    ZzScreen scr(10, 5);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "aa"); // 链 1：12 个 a
    writeRow(scr, 2, "bbbbbbbbbb");
    scr.setLineWrapped(2, true);
    writeRow(scr, 3, "bb"); // 链 2：12 个 b
    writeRow(scr, 4, "s1");
    scr.setCursorPosition(ZzPosition{4, 1});
    std::size_t asked = 0;
    scr.setHistoryPullCallback([&](std::size_t maxLines) {
        asked = maxLines;
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(20, "h9")); // 只有 1 行可取
        return pulled;
    });
    scr.reflow(20); // 产出 3 行 < 5：缺口 2，顶补 1 + 底部补空 1
    ZZ_TEST_EXPECT(asked == 2);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h9");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 12) == "aaaaaaaaaaaa");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 12) == "bbbbbbbbbbbb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(4), 2) == "  "); // 余量补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3); // 链跟踪行 2 + 顶补 1
    ZZ_TEST_EXPECT(scr.cursor().position.col == 1);
}

// 3. 无回调退化：维持底部补空（M4 语义不回归）
static void testNoCallbackPadsBottom()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{3, 2});
    scr.reflow(20); // 无 HistoryPullCallback：底部补空
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "s1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "s2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "  ");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 2);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 2);
}

// 4. Alternate 不顶补：备用屏缺口仍底部补空（回调存在也不调）
static void testAlternateNoTopFill()
{
    ZzScreen scr(10, 4); // primary 4 空行：重组无缺口，不会触发回调
    bool pullCalled = false;
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    scr.setCursorPosition(ZzPosition{1, 2});
    scr.reflow(20); // alt 产出 3 行 < 4：mayScrollOut=false，不顶补
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "  ");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0); // 链内偏移 12 -> (0,12)
    ZZ_TEST_EXPECT(scr.cursor().position.col == 12);
}

// 5. 顶补行 wrapped 标志保留（链头留历史的合法跨缝态）
static void testTopFillPreservesWrapped()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{3, 2});
    scr.setHistoryPullCallback([](std::size_t) {
        std::vector<ZzLine> pulled;
        ZzLine line = makeTextLine(20, "h9");
        line.setWrapped(true); // 模拟链头留在历史的半链顶补
        pulled.push_back(std::move(line));
        return pulled;
    });
    scr.reflow(20);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "h9");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());
}

// 6. 光标在链内：链偏移跟踪 + 顶补平移复合
static void testTopFillCursorInsideChain()
{
    ZzScreen scr(10, 4);
    writeRow(scr, 0, "aaaaaaaaaa");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "bb");
    writeRow(scr, 2, "s1");
    writeRow(scr, 3, "s2");
    scr.setCursorPosition(ZzPosition{1, 1}); // 链内：偏移 10+1=11
    scr.setHistoryPullCallback([](std::size_t) {
        std::vector<ZzLine> pulled;
        pulled.push_back(makeTextLine(20, "h9"));
        return pulled;
    });
    scr.reflow(20);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 12) == "aaaaaaaaaabb");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1); // 链跟踪 (0,11) + 顶补 1
    ZZ_TEST_EXPECT(scr.cursor().position.col == 11);
}

int main()
{
    testWidenTopFill();
    testTopFillPartialHistory();
    testNoCallbackPadsBottom();
    testAlternateNoTopFill();
    testTopFillPreservesWrapped();
    testTopFillCursorInsideChain();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_screen_reflow_topfill
```

预期：用例 1/2/5/6 FAIL（当前底部补空，顶补断言不成立）；用例 3/4 PASS（退化路径）。

- [ ] **步骤 3：实现顶补分支**

修改 `src/screen/Screen.cpp` reflowBuffer 的行数平衡分支（当前 166-181 行），将不足分支改为：

```cpp
    // 行数平衡：溢出上移（仅 Primary）或丢弃（Alternate）；
    // 不足时先经 HistoryPullCallback 从最新历史顶补（M16c，仅 Primary 且
    // 装有回调——对齐 contour 统一流尾部窗口的净效果），余量底部补空。
    if (static_cast<int>(out.size()) > rows_) {
        const int overflow = static_cast<int>(out.size()) - rows_;
        if (mayScrollOut && scrollOutCallback_) {
            std::vector<ZzLine> spilled;
            spilled.reserve(static_cast<std::size_t>(overflow));
            for (int k = 0; k < overflow; ++k)
                spilled.push_back(std::move(out[static_cast<std::size_t>(k)]));
            scrollOutCallback_(std::move(spilled));
        }
        out.erase(out.begin(), out.begin() + overflow);
        track.row -= overflow;
    } else if (static_cast<int>(out.size()) < rows_) {
        int deficit = rows_ - static_cast<int>(out.size());
        if (mayScrollOut && historyPullCallback_) {
            std::vector<ZzLine> pulled =
                historyPullCallback_(static_cast<std::size_t>(deficit));
            if (!pulled.empty()) {
                const auto pulledCount = static_cast<int>(pulled.size());
                out.insert(out.begin(), std::make_move_iterator(pulled.begin()),
                           std::make_move_iterator(pulled.end()));
                track.row += pulledCount;
                deficit -= pulledCount;
            }
        }
        for (; deficit > 0; --deficit)
            out.push_back(ZzLine(newCols));
    }
```

注意：顶补插入的是重组产出向量 `out`（赋值 `buf.lines` 之前），等价
prepend 语义；顶补行宽度由调用方契约保证为新列宽（backend 历史先重组）。

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_screen_reflow_topfill
```

预期：6/6 PASS。同时跑相邻回归：

```bash
ctest --preset linux-gcc-debug -R 'test_screen_reflow|test_screen_rowresize|test_native_reflow|test_native_rowresize|test_backend_compat'
```

预期：全部 PASS（既有用例均无缺口场景，不受顶补影响）。

- [ ] **步骤 5：更新 Screen.h 注释**

`include/ZzTerm/Screen.h` 的 reflow 注释（96-106 行区域）改写要点：
重组导致行数超出时 Primary 顶部溢出经 ScrollOutCallback 上移不变；
行数不足时改为「先经 HistoryPullCallback 从最新历史顶补（M16c），
余量底部补空行」；Alternate 溢出丢弃/不足补空不变。
HistoryPullCallback 注释（61-67 行区域）补充：回调返回行宽度须与当前
网格列宽一致（行变回抽时列宽不变；列变顶补时历史已先完成重组）。

- [ ] **步骤 6：Commit**

```bash
git add tests/unit/test_screen_reflow_topfill.cpp src/screen/Screen.cpp include/ZzTerm/Screen.h
git commit -m "feat(screen): M16c 列变 reflow 历史顶补原语（内容贴底锚定）"
```

---

### 任务 2：facade 级钉住 + 双后端对照

**文件：**
- 创建：`tests/unit/test_native_reflow_topfill.cpp`
- 修改：`tests/unit/test_backend_compat.cpp`（追加用例 23 + main 注册）

- [ ] **步骤 1：编写 facade 钉住测试**

新建 `tests/unit/test_native_reflow_topfill.cpp`（feedStr/screenRowText/historyLineText 助手同 test_native_rowresize.cpp）。4 个用例的断言值均经 2026-10-08 双后端探针实测核对：

```cpp
// M16c facade 级顶补钉住：纯列变拉宽顶补（探针场景 A）、列行同变事故复刻
// （探针场景 B）、窄宽往返布局守恒、Alternate 期间主屏顶补按缓冲区分。
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
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

// 场景 A 初态构造（10x4）：6 条 20 格长行 + s1/s2，历史 10 行，
// 屏幕 [L5a(w), L5b, s1, s2]，光标 (2,3)。
static void feedScenarioA(ZzTerminal& term)
{
    for (int i = 0; i < 6; ++i) {
        std::string s = "L" + std::to_string(i) + std::string(18, char('a' + i));
        feedStr(term, s + "\r\n");
    }
    feedStr(term, "s1\r\ns2");
}

// 1. 纯列变拉宽顶补（探针场景 A，对齐 contour 实测布局）
static void testWidenTopFillFacade()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedScenarioA(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 10);

    const std::uint64_t g0 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(20, 4)); // 链 2->1 接回，缺口 1 顶补
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 4); // 5 链合 5 行再顶补 1
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "L4eeeeeeeeeeeeeeeeee");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "L5ffffffffffffffffff");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "s1");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "s2");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3); // 贴底（contour 同款）
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
}

// 2. 列行同变事故复刻（探针场景 B：2026-10-08 用户实测「最大化后仅剩
// 提示符、历史滞留」的 Core 级形态，M16c 后对齐 contour 抽干历史）
static void testCombinedResizeTopFill()
{
    ZzTerminal term(30, 10, ZzBackendKind::Native, 100);
    feedStr(term, "short1\r\nshort2\r\n");
    feedStr(term, std::string(45, 'w') + "\r\n"); // 45 格长行 30 列折 2 行
    feedStr(term, "tail\r\nprompt$ ");

    ZZ_TEST_EXPECT(term.resize(15, 5)); // 缩：溢出 2 行压历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);

    ZZ_TEST_EXPECT(term.resize(60, 20)); // 列行同增（事故手势）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // 顶补抽干，无滞留
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "short1");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "short2");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == std::string(45, 'w'));
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "tail");
    ZZ_TEST_EXPECT(screenRowText(term, 4) == "prompt$");
    ZZ_TEST_EXPECT(screenRowText(term, 5).empty()); // 余量底部补空
    ZZ_TEST_EXPECT(term.cursor().position.row == 4);
    ZZ_TEST_EXPECT(term.cursor().position.col == 8);
}

// 3. 窄宽往返布局守恒：满屏初态缩列再拉回，屏幕与历史精确还原
static void testNarrowWideRoundtrip()
{
    ZzTerminal term(20, 4, ZzBackendKind::Native, 100);
    feedScenarioA(term); // 20 列下每链 1 行：历史 [L0..L3]，屏幕 [L4,L5,s1,s2]
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 4);

    ZZ_TEST_EXPECT(term.resize(10, 4)); // 缩列：链重切，溢出 2 行压历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 10);
    ZZ_TEST_EXPECT(term.resize(20, 4)); // 拉回：顶补还原
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 4);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "L4eeeeeeeeeeeeeeeeee");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "L5ffffffffffffffffff");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == "s1");
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "s2");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
}

// 4. Alternate 期间主屏顶补按缓冲区分（与激活态无关），备用屏自身不顶补
static void testPrimaryTopFillDuringAlternate()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "f0\r\nf1\r\n");
    feedStr(term, std::string(20, 'c')); // 链占行 2/3（wrap-pending）
    feedStr(term, "\r\n");               // 滚出 f0 入历史
    feedStr(term, "x");                  // 屏幕 [f1, cA(w), cB, x]
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1);

    feedStr(term, "\x1b[?1049h"); // 进备用屏
    ZZ_TEST_EXPECT(term.isAlternateScreen());
    ZZ_TEST_EXPECT(term.resize(20, 4)); // 主屏重组收缩，顶补 f0（历史清空）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);

    feedStr(term, "\x1b[?1049l"); // 回主屏验证顶补结果
    ZZ_TEST_EXPECT(!term.isAlternateScreen());
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "f0");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "f1");
    ZZ_TEST_EXPECT(screenRowText(term, 2) == std::string(20, 'c'));
    ZZ_TEST_EXPECT(screenRowText(term, 3) == "x");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.cursor().position.col == 1);
}

int main()
{
    testWidenTopFillFacade();
    testCombinedResizeTopFill();
    testNarrowWideRoundtrip();
    testPrimaryTopFillDuringAlternate();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

- [ ] **步骤 2：运行验证通过（任务 1 实现已就位）**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_reflow_topfill
```

预期：4/4 PASS。若 FAIL，逐条核对断言值与探针实测是否一致，禁止改断言迁就实现——分歧须回查实现。

- [ ] **步骤 3：追加双后端对照顾例 23**

`tests/unit/test_backend_compat.cpp` 追加（放在 testResizeReflowSeamChain 之后、命名空间闭合之前；main 中注册）。不用 Dual（其尺寸固定 80x24），直接构造两台 10x4 终端：

```cpp
// 23. 列变拉宽顶补 parity（M16c）：满屏折链拉宽，native 顶补与 contour
// 统一流尾部窗口净效果一致——历史行数/屏幕逐行文本/光标一致。
void testResizeReflowTopFill()
{
    ZzTerminal native(10, 4, ZzBackendKind::Native, 100);
    ZzTerminal contour(10, 4, ZzBackendKind::Contour, 100);
    for (auto* term : {&native, &contour}) {
        for (int i = 0; i < 6; ++i) {
            std::string s = "L" + std::to_string(i) + std::string(18, char('a' + i));
            term->feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(s.data()), s.size()));
            term->feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>("\r\n"), 2));
        }
        term->feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>("s1\r\ns2"), 6));
    }
    ZZ_CHECK(native.resize(20, 4));
    ZZ_CHECK(contour.resize(20, 4));
    ZZ_CHECK(native.historyView().lineCount() == contour.historyView().lineCount());
    ZZ_CHECK(native.historyView().lineCount() == 4);
    for (int r = 0; r < 4; ++r)
        checkRowEqualAllowEmptyWidthDiff(native, contour, r, 20, "topfill-20");
    ZZ_CHECK(native.cursor().position == contour.cursor().position);
}
```

main 中 `testResizeReflowSeamChain();` 之后加 `testResizeReflowTopFill();`。

- [ ] **步骤 4：运行对照与全量回归**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：57/57（55 + 2 个新测试文件）。再跑 contour OFF 与 shared 基线：

```bash
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
```

预期：44/44、57/57。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_native_reflow_topfill.cpp tests/unit/test_backend_compat.cpp
git commit -m "test(native): M16c 顶补 facade 钉住与 contour 对照顾例 23"
```

---

### 任务 3：文档同步 + 里程碑收尾

**文件：**
- 修改：`docs/Scrollback-and-Reflow.md`
- 修改：`docs/API.md:88-102`（resize 语义段）
- 修改：`docs/superpowers/plans/2026-10-08-m16c-reflow-topfill.md`（本计划勘误补记）

- [ ] **步骤 1：Scrollback-and-Reflow.md 改写**

§3 重组规则要点追加一条：屏幕重组产出不足 rows 时先经
HistoryPullCallback 从最新历史顶补（M16c），余量底部补空。
§4 第 3 步「屏幕再按新列宽 reflow」改写为顶补语义（内容贴底锚定、
光标随顶补平移）；§7 语义边界更新（底部补空仅余量路径）。

- [ ] **步骤 2：API.md 同步**

88-102 行区域：resize 语义段补充「列变重组内容收缩时从最新历史顶补
填满屏幕（M16c，对齐 contour 统一流净效果）」。

- [ ] **步骤 3：计划勘误补记**

在本计划文末追加「实施勘误」节：记录实施中与计划/规格的偏差
（逐条：预期 vs 实际 vs 裁定），含规格 §3「复用 prependPrimaryLines
原语」细化为「reflowBuffer 产出向量顶部直插（等价 prepend 语义，
避免二次搬移）」这一点已知细化。无偏差则写「无」。

- [ ] **步骤 4：全基线回归 + 文档生成**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
```

预期：57/57、44/44、57/57、fuzz 3/3、doxygen exit 0 零警告。

- [ ] **步骤 5：Commit + tag + 推送 + CI 确认**

```bash
git add docs/Scrollback-and-Reflow.md docs/API.md docs/superpowers/plans/2026-10-08-m16c-reflow-topfill.md
git commit -m "docs(m16c): 顶补语义文档同步与计划收尾"
git tag m16c
git push origin contour --tags
gh run list --branch contour --limit 7
```

预期：7 个 workflow 全部绿。

- [ ] **步骤 6：spike 重建与用户复验交接**

重建 spike（上次事故的教训：Core 修复后必须重建 spike 才生效）：

```bash
cmake --build /home/zz/Jackfahdin/github/ZzClawTerm/build/spike-debug
```

向用户交接复验清单：`./build/spike-debug/zzcore_spike --local`，
制造不满屏内容后拖拽最大化，预期历史行回填屏幕（contour 同款布局），
不再出现「仅剩提示符 + 大片空白」。

---

## 自检记录

- 规格覆盖：§3 机制（任务 1）、§5 测试 1-7（任务 1 用例 1-6 + 既有
  testWidenPadsBottom 退化、任务 2 用例 1-4）、§6 文档（任务 3）。
- 断言值推演：任务 1 用例 1-6 与任务 2 用例 1-4 的行数账/光标账均已
  手工推演；facade 断言值经双后端探针实测核对（非纯推演）。
- 类型一致：HistoryPullCallback 签名与 Screen.h:67 一致；prepend 语义
  直插 out 向量的细化已在任务 1 步骤 3 注释与任务 3 步骤 3 勘误登记。

---

## 实施勘误（2026-10-08，任务 1/2 已落地：cfaccdd、f6b9724）

1. 规格 §3「复用 prependPrimaryLines 原语」——预期：直接调用 M16b 的
   `ZzScreen::prependPrimaryLines` 完成顶补；实际：细化为在
   `reflowBuffer` 的重组产出向量 `out` 顶部直插（`out.insert(out.begin(), ...)`
   于赋值 `buf.lines` 之前）。裁定：等价 prepend 语义且避免二次搬移，
   采纳；prependPrimaryLines 保持 M16b 接缝归还专用，未改动。
2. 任务 2 步骤 4 预期 OFF 基线 44/44——实际：OFF 基线因新测试文件经
   GLOB 收编增至 46/46。裁定：基线计数随测试收编自然增长，非偏差，
   以实测 46/46 为准。
3. 其余实施项与计划/规格一致，无偏差。
