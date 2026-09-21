# M5a Selection / Copy 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 交付后端无关的逻辑行坐标选区/复制：ZzLogicalPos 坐标模型、Core 持有选区状态、统一物理行只读数据源（双后端各一实现）、共享的逻辑行拼接与纯文本提取、facade 选区 API。

**架构：** 选区状态（ZzSelection）与文本提取（zzExtractSelectionText）为后端无关纯逻辑，放 src/terminal/；后端经新增内部接口 ZzIPhysicalLineSource（历史区+屏幕区统一物理行快照）向 Core 供数据，native 组合 ZzScrollback+ZzScreen，contour 经适配层新增历史只读口（Grid 负偏移 lineAt + stableRangeFloor 丢弃探测）。facade 在 feed/resize 两个唯一公共切面维护锚点（丢弃平移、Alternate 清除、reflow clamp）。

**技术栈：** C++20、CMake（GLOB+CONFIGURE_DEPENDS 收编 src/terminal 与 tests/unit；native/contour 子目录为显式列表需手工加文件）、自带 ZZ_TEST_EXPECT 风格宏测试、doxygen 零警告门。

**规格：** docs/superpowers/specs/2026-09-21-m5a-selection-copy-design.md（commit 7e8a056）。

**对规格的两处精化（实现层细节，不改变批准语义）：**

1. 规格 5.2 的 ZzILogicalLineSource 定为**物理行粒度**接口 ZzIPhysicalLineSource；逻辑行拼接（wrapped 链 + 历史/屏幕接缝）由 src/terminal 共享代码完成——双后端共用一套拼接逻辑，避免两端各写一份产生分歧；
2. 丢弃平移按**物理行**计数（规格原文即 stats().totalDropped，为物理计数；contour 侧 stableRangeFloor 前移同为物理计数，两端天然一致）。多物理行逻辑行被丢弃时锚点偏移为近似语义，代码注释钉住，真实应用反馈后再评估精确化。

**命令约定（全计划通用）：**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
# OFF 配置：cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
# shared 配置：cmake --preset 见 build/m2-shared-check 既有目录（cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check）
# 文档：doxygen Doxyfile（exit 0 且零警告）
```

注意：tests/CMakeLists.txt 与 src 两级 GLOB 均为 CONFIGURE_DEPENDS，新增 .cpp 后重新 build 即自动收编；但**显式列表三处必须手工加文件**（src/backend/native/CMakeLists.txt、src/backend/contour/CMakeLists.txt、tests/CMakeLists.txt 的剔除/条件注册块），且 OFF/shared 两个既有构建目录新增测试后需各自重新 configure。

## 文件结构

| 文件 | 职责 | 任务 |
|---|---|---|
| include/ZzTerm/Types.h | 新增公开值类型 ZzLogicalPos（坐标类聚合地，遵循现有惯例） | T1 |
| src/terminal/ZzSelection.h/.cpp | 选区模型：anchor/extent、规范化半开区间、丢弃平移、clamp、判空；不依赖后端 | T1 |
| src/backend/ZzLineSource.h | 内部接口 ZzIPhysicalLineSource（header-only，无实现） | T2 |
| src/terminal/ZzSelectionText.h/.cpp | 逻辑行拼接 + zzExtractSelectionText 四规则提取（纯函数，双后端共用） | T2 |
| src/backend/native/ZzNativeLineSource.h/.cpp | native 数据源：组合 ZzScrollback+ZzScreen，Alternate 时历史返回 0 | T3 |
| src/backend/contour/ZzContourBackend.h/.cpp | 新增历史/屏幕行快照与 wrapped、stableFloor 只读口（不动 third_party） | T4 |
| src/backend/contour/ZzContourLineSource.h/.cpp | contour 数据源：包装新读口，stableFloor 前移累计为丢弃计数 | T4 |
| src/backend/ZzTerminalBackend.h | 新增纯虚 lineSource() | T5 |
| src/backend/native/ZzNativeBackend.h/.cpp | 持有 ZzNativeLineSource 成员并实现 lineSource() | T5 |
| src/backend/contour/ZzContourBackendAdapter.cpp | 持有 ZzContourLineSource、feed/resize 后 noteFloor、实现 lineSource() | T5 |
| include/ZzTerm/Terminal.h + src/terminal/Terminal.cpp | facade 六个选区 API + Impl 选区成员 + feed/resize 锚点维护包裹 | T5 |
| tests/unit/test_selection.cpp | ZzSelection 模型单测 | T1 |
| tests/unit/test_selection_text.cpp | 提取四规则 + 接缝拼接单测（fake source） | T2 |
| tests/unit/test_native_linesource.cpp | native 数据源单测（公开 API 构造 ZzScreen/ZzScrollback） | T3 |
| tests/unit/test_contour_linesource.cpp | contour 数据源单测（剔除 GLOB + 条件注册，链接 ZzTermContourBackend） | T4 |
| tests/unit/test_terminal_selection.cpp | facade 集成：锚点维护、reflow 保持、丢弃平移、Alternate 清除 | T5 |
| tests/unit/test_selection_compat.cpp | 双后端选区提取逐字节一致（剔除 GLOB + 条件注册，仅链 ZzTermCore） | T6 |
| docs/Architecture.md、docs/API.md、docs/VT-Xterm-Checklist.md | §894/§19/§13/§504 状态落定、API 文档、Selection 章 | T6 |

---

### 任务 1：ZzLogicalPos + ZzSelection 选区模型

**文件：**
- 修改：`include/ZzTerm/Types.h`（新增 ZzLogicalPos，放坐标类型区）
- 创建：`src/terminal/ZzSelection.h`、`src/terminal/ZzSelection.cpp`
- 测试：`tests/unit/test_selection.cpp`
- 构建：`tests/CMakeLists.txt`（shared 构建 target_sources 先例块）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_selection.cpp`：

```cpp
// ZzSelection 选区模型单测（M5a）：规范化区间、丢弃平移、clamp、判空。
#include "../../src/terminal/ZzSelection.h"

#include <cstdio>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void testEmptyByDefault()
{
    ZzSelection sel;
    ZZ_TEST_EXPECT(sel.empty());
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(!sel.range(s, e));
}

static void testRangeNormalizesOrder()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{10, 5}, ZzLogicalPos{3, 2}); // anchor 在后
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 3 && s.col == 2);
    ZZ_TEST_EXPECT(e.line == 10 && e.col == 5);
    ZZ_TEST_EXPECT(!sel.empty());
}

static void testRangeSameLine()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{7, 20}, ZzLogicalPos{7, 4});
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 7 && s.col == 4);
    ZZ_TEST_EXPECT(e.line == 7 && e.col == 20);
}

static void testExtendKeepsAnchor()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{1, 0}, ZzLogicalPos{2, 0});
    sel.extend(ZzLogicalPos{5, 3});
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 1 && s.col == 0);
    ZZ_TEST_EXPECT(e.line == 5 && e.col == 3);
}

static void testClear()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{1, 0}, ZzLogicalPos{2, 0});
    sel.clear();
    ZZ_TEST_EXPECT(sel.empty());
}

static void testZeroLengthSelectionIsEmpty()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{4, 9}, ZzLogicalPos{4, 9});
    ZZ_TEST_EXPECT(sel.empty());
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(!sel.range(s, e));
}

static void testOnLinesDroppedShiftsAnchors()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{10, 2}, ZzLogicalPos{20, 3});
    sel.onLinesDropped(6);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 4 && e.line == 14);
}

static void testOnLinesDroppedClampsAtZero()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{2, 5}, ZzLogicalPos{20, 3});
    sel.onLinesDropped(6); // start 平移为负 → clamp 到 0 行 0 列
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 0);
    ZZ_TEST_EXPECT(e.line == 14 && e.col == 3);
}

static void testOnLinesDroppedBeyondExtentClears()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{2, 5}, ZzLogicalPos{4, 3});
    sel.onLinesDropped(6); // 整个选区内容已丢弃 → 选区清空
    ZZ_TEST_EXPECT(sel.empty());
}

static void testClampToLineCount()
{
    ZzSelection sel;
    sel.set(ZzLogicalPos{0, 0}, ZzLogicalPos{99, 5});
    sel.clampTo(12);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(sel.range(s, e));
    ZZ_TEST_EXPECT(e.line == 11);
}

int main()
{
    testEmptyByDefault();
    testRangeNormalizesOrder();
    testRangeSameLine();
    testExtendKeepsAnchor();
    testClear();
    testZeroLengthSelectionIsEmpty();
    testOnLinesDroppedShiftsAnchors();
    testOnLinesDroppedClampsAtZero();
    testOnLinesDroppedBeyondExtentClears();
    testClampToLineCount();
    if (g_failures == 0)
        std::printf("test_selection: all passed\n");
    return g_failures;
}
```

在 `tests/CMakeLists.txt` 的 test_reflow target_sources 先例块（:35-40）后追加同形块：

```cmake
# test_selection / test_selection_text 直接调用 src/terminal 内部实现（无
# ZZTERM_API 导出）：shared 构建下须把实现文件编进测试可执行文件。
if(TARGET test_selection)
    target_sources(test_selection PRIVATE "${CMAKE_SOURCE_DIR}/src/terminal/ZzSelection.cpp")
endif()
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_selection`
预期：编译失败，`ZzSelection.h: No such file or directory`

- [ ] **步骤 3：实现 ZzLogicalPos 与 ZzSelection**

`include/ZzTerm/Types.h` 坐标类型区新增（确认文件已含 `<cstdint>`；doxygen 中文注释，注意 doxygen 陷阱：行内 code span 内禁尖括号、内容禁以点开头、禁 `#` 词、禁反斜杠转义）：

```cpp
/**
 * @brief 逻辑行坐标（选区/复制，M5a）。
 *
 * line：统一空间逻辑行序号。0 = 当前最早一条有效逻辑行（历史区头部），
 *       屏幕区紧跟其后；append 与滚动不改变已有内容的序号；历史头部丢弃时
 *       序号整体下移，Core 自动平移选区锚点。
 * col：逻辑行内单元格偏移（0 起，按格不按字符）；落在宽字符续格上时
 *      提取层归一到 lead 格。
 */
struct ZzLogicalPos {
    std::int64_t line = 0; ///< 逻辑行序号（统一空间）
    std::int32_t col = 0;  ///< 逻辑行内单元格偏移
    friend bool operator==(const ZzLogicalPos&, const ZzLogicalPos&) = default;
};
```

`src/terminal/ZzSelection.h`（内部头，无 ZZTERM_API；头文件保护风格仿 src/backend/ZzTerminalBackend.h）：

```cpp
// ZzSelection：选区模型（M5a）。纯值逻辑，不依赖任何后端类型。
// 半开区间语义：range() 返回 [start, end)；anchor == extent 为空选区。
#pragma once

#include <ZzTerm/Types.h>

#include <cstdint>

class ZzSelection {
public:
    void set(ZzLogicalPos anchor, ZzLogicalPos extent) noexcept;
    void extend(ZzLogicalPos extent) noexcept;
    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept;
    // 规范化区间 [start, end)；空选区返回 false（start/end 不动）。
    [[nodiscard]] bool range(ZzLogicalPos& start, ZzLogicalPos& end) const noexcept;
    // 历史头部丢弃 delta 个物理行后平移锚点（近似语义：物理计数平移逻辑
    // 序号，规格 5.1；计划头部精化 2）。start 平移为负 clamp 到 {0,0}；
    // 两端都平移为负（内容全部丢弃）时选区清空。
    void onLinesDropped(std::uint64_t delta) noexcept;
    // line clamp 到 [0, lineCount)；空选区为空操作。col 不在此 clamp
    //（逻辑行长度由提取层按行 clamp）。
    void clampTo(std::int64_t lineCount) noexcept;

private:
    ZzLogicalPos anchor_{};
    ZzLogicalPos extent_{};
};
```

`src/terminal/ZzSelection.cpp`：

```cpp
#include "ZzSelection.h"

namespace {
// 词典序比较：先 line 后 col
bool posLess(ZzLogicalPos a, ZzLogicalPos b) noexcept
{
    return a.line < b.line || (a.line == b.line && a.col < b.col);
}
} // namespace

void ZzSelection::set(ZzLogicalPos anchor, ZzLogicalPos extent) noexcept
{
    anchor_ = anchor;
    extent_ = extent;
}

void ZzSelection::extend(ZzLogicalPos extent) noexcept
{
    extent_ = extent;
}

void ZzSelection::clear() noexcept
{
    anchor_ = {};
    extent_ = {};
}

bool ZzSelection::empty() const noexcept
{
    return anchor_ == extent_;
}

bool ZzSelection::range(ZzLogicalPos& start, ZzLogicalPos& end) const noexcept
{
    if (empty())
        return false;
    if (posLess(extent_, anchor_)) {
        start = extent_;
        end = anchor_;
    } else {
        start = anchor_;
        end = extent_;
    }
    return true;
}

void ZzSelection::onLinesDropped(std::uint64_t delta) noexcept
{
    if (empty())
        return;
    const auto shift = static_cast<std::int64_t>(delta);
    ZzLogicalPos s, e;
    // 先规范化再平移：start 负值 clamp {0,0}；end 也负 → 内容全丢 → 清空
    (void) range(s, e);
    e.line -= shift;
    if (e.line < 0) {
        clear();
        return;
    }
    s.line -= shift;
    if (s.line < 0) {
        s.line = 0;
        s.col = 0;
    }
    anchor_ = s;
    extent_ = e;
}

void ZzSelection::clampTo(std::int64_t lineCount) noexcept
{
    if (empty())
        return;
    ZzLogicalPos s, e;
    (void) range(s, e);
    if (s.line >= lineCount) {
        clear(); // 起点已越出有效范围：无内容可选
        return;
    }
    if (e.line >= lineCount)
        e.line = lineCount - 1;
    anchor_ = s;
    extent_ = e;
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_selection`
预期：PASS，输出 `test_selection: all passed`

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Types.h src/terminal/ZzSelection.h src/terminal/ZzSelection.cpp tests/unit/test_selection.cpp tests/CMakeLists.txt
git commit -m "feat(selection): ZzLogicalPos 坐标与 ZzSelection 选区模型（M5a T1）"
```

---

### 任务 2：ZzIPhysicalLineSource 接口 + 拼接/提取纯函数

**文件：**
- 创建：`src/backend/ZzLineSource.h`（header-only 接口）
- 创建：`src/terminal/ZzSelectionText.h`、`src/terminal/ZzSelectionText.cpp`
- 测试：`tests/unit/test_selection_text.cpp`
- 构建：`tests/CMakeLists.txt`（T1 的 target_sources 块追加 test_selection_text）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_selection_text.cpp`。fake source 与辅助（完整矩阵：ASCII 软换行拼接、接缝续接、宽字符边界归一、cluster 整串、行尾空白修剪、跨逻辑行插 `\n`、空选区、越界 clamp）：

```cpp
// zzExtractSelectionText 提取矩阵（M5a）：四规则 + 接缝续接 + clamp。
#include "../../src/backend/ZzLineSource.h"
#include "../../src/terminal/ZzSelectionText.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

ZzCell narrowCell(char c)
{
    ZzCell cell;
    cell.setWidth(ZzCellWidth::Narrow);
    cell.setCodePoint(static_cast<char32_t>(c));
    return cell;
}

// 用 ASCII 文本构造一行；cols 定宽，尾部为空单元格（isEmpty）。
ZzLine makeLine(int cols, std::string_view text, bool wrapped)
{
    ZzLine line;
    line.resize(cols);
    int col = 0;
    for (char c : text)
        line.setCell(col++, narrowCell(c));
    line.setWrapped(wrapped);
    return line;
}

// fake 数据源：rows 前半截为历史（historyRows 条），其余为屏幕。
class FakeSource final : public ZzIPhysicalLineSource {
public:
    FakeSource(int cols, int historyRows, std::vector<ZzLine> rows)
        : cols_(cols), historyRows_(historyRows), rows_(std::move(rows))
    {
    }
    std::size_t historyLineCount() const override { return static_cast<std::size_t>(historyRows_); }
    int screenRowCount() const override { return static_cast<int>(rows_.size()) - historyRows_; }
    int cols() const override { return cols_; }
    ZzLine lineAt(std::size_t unifiedRow) const override { return rows_.at(unifiedRow); }
    bool lineWrapped(std::size_t unifiedRow) const override { return rows_.at(unifiedRow).wrapped(); }
    std::uint64_t droppedLineCount() const override { return 0; }

private:
    int cols_;
    int historyRows_;
    std::vector<ZzLine> rows_;
};

} // namespace

static void testPlainSingleLogicalLine()
{
    FakeSource src(10, 0, {makeLine(10, "hello", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 5}) == "hello");
}

static void testSoftWrapChainJoinsWithoutNewline()
{
    // "hello" + "world" 一条逻辑行（首行 wrapped），共 10 格
    FakeSource src(5, 0, {makeLine(5, "hello", true), makeLine(5, "world", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 10}) == "helloworld");
}

static void testCrossLogicalLinesGetNewline()
{
    FakeSource src(10, 0, {makeLine(10, "ab", false), makeLine(10, "cd", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {1, 2}) == "ab\ncd");
}

static void testNoTrailingNewline()
{
    FakeSource src(10, 0, {makeLine(10, "ab", false)});
    const std::string text = zzExtractSelectionText(src, {0, 0}, {0, 2});
    ZZ_TEST_EXPECT(text == "ab");
}

static void testTrailingBlankTrimmed()
{
    // "ab" 后 8 个空单元格；选到行尾，输出不含尾部空白
    FakeSource src(10, 0, {makeLine(10, "ab", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 10}) == "ab");
}

static void testInteriorBlankKeptAsSpace()
{
    // "a" + 空单元格 + "b"：行内空白保留为一个空格
    ZzLine line;
    line.resize(5);
    line.setCell(0, narrowCell('a'));
    line.setCell(2, narrowCell('b'));
    FakeSource src(5, 0, {std::move(line)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 3}) == "a b");
}

static void testSeamStitchHistoryToScreen()
{
    // 历史末行 wrapped（续到屏幕首行）：跨域按一条逻辑行拼接，不插换行
    FakeSource src(5, 1, {makeLine(5, "hello", true), makeLine(5, "world", false)});
    ZZ_TEST_EXPECT(zzLogicalLineCount(src) == 1);
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 10}) == "helloworld");
}

static void testNoSeamStitchWhenHardBoundary()
{
    FakeSource src(5, 1, {makeLine(5, "hello", false), makeLine(5, "world", false)});
    ZZ_TEST_EXPECT(zzLogicalLineCount(src) == 2);
}

static void testWideCharBoundaryNormalization()
{
    // 格序列：'x' + 宽字符"界"(lead+续格) + 'y'
    ZzLine line;
    line.resize(6);
    line.setCell(0, narrowCell('x'));
    ZzCell lead;
    lead.setWidth(ZzCellWidth::WideLead);
    lead.setCodePoint(U'界');
    line.setCell(1, lead);
    ZzCell cont;
    cont.setWidth(ZzCellWidth::WideContinuation);
    line.setCell(2, cont);
    line.setCell(3, narrowCell('y'));
    FakeSource src(6, 0, {std::move(line)});
    // start 落在续格 → 归一到 lead，含整字
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 2}, {0, 4})
                   == zzExtractSelectionText(src, {0, 1}, {0, 4}));
    // end 落在续格（半开区间切在半字中间）→ 前扩含整字
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 2})
                   == zzExtractSelectionText(src, {0, 0}, {0, 3}));
}

static void testClusterTakenAsWhole()
{
    ZzLine line;
    line.resize(4);
    ZzCell cluster;
    cluster.setWidth(ZzCellWidth::Narrow);
    cluster.setCluster(line.internCluster("a\u0301")); // a + 组合重音符
    line.setCell(0, cluster);
    line.setCell(1, narrowCell('b'));
    FakeSource src(4, 0, {std::move(line)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 2}) == "a\u0301b");
}

static void testEmptySelectionReturnsEmpty()
{
    FakeSource src(10, 0, {makeLine(10, "hello", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 3}, {0, 3}).empty());
}

static void testOutOfRangeClamped()
{
    FakeSource src(10, 0, {makeLine(10, "hello", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 999}) == "hello");
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {99, 0}) == "hello");
}

int main()
{
    testPlainSingleLogicalLine();
    testSoftWrapChainJoinsWithoutNewline();
    testCrossLogicalLinesGetNewline();
    testNoTrailingNewline();
    testTrailingBlankTrimmed();
    testInteriorBlankKeptAsSpace();
    testSeamStitchHistoryToScreen();
    testNoSeamStitchWhenHardBoundary();
    testWideCharBoundaryNormalization();
    testClusterTakenAsWhole();
    testEmptySelectionReturnsEmpty();
    testOutOfRangeClamped();
    if (g_failures == 0)
        std::printf("test_selection_text: all passed\n");
    return g_failures;
}
```

tests/CMakeLists.txt 的 T1 target_sources 块内追加：

```cmake
if(TARGET test_selection_text)
    target_sources(test_selection_text PRIVATE "${CMAKE_SOURCE_DIR}/src/terminal/ZzSelectionText.cpp")
endif()
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug`（configure 重跑由 CONFIGURE_DEPENDS 触发）
预期：编译失败，`ZzLineSource.h / ZzSelectionText.h: No such file or directory`

- [ ] **步骤 3：实现接口与提取**

`src/backend/ZzLineSource.h`（header-only；注释钉住统一坐标约定与接缝规则）：

```cpp
// ZzIPhysicalLineSource：统一物理行只读数据源（M5a，内部接口）。
// 统一坐标：历史区物理行 [0, historyLineCount()) 在前，屏幕区物理行紧跟其后。
// ZzLine.wrapped() 语义：本行内容续到下一物理行（软换行链，最后一行为 false）。
// 接缝规则（规格 5.3，收口 M4 观察项①）：历史末行 wrapped()==true 即与屏幕
// 首行续接为一条逻辑行；逻辑行拼接与文本提取由 src/terminal/ZzSelectionText
// 统一实现，双后端共用。
// 值快照语义：lineAt 按值返回，调用方无惧后端缓冲失效；Alternate 屏时
// historyLineCount() 须返回 0（Alternate 无历史，规格 5.1）。
#pragma once

#include <ZzTerm/Line.h>

#include <cstddef>
#include <cstdint>

class ZzIPhysicalLineSource {
public:
    virtual ~ZzIPhysicalLineSource() = default;

    [[nodiscard]] virtual std::size_t historyLineCount() const = 0;
    [[nodiscard]] virtual int screenRowCount() const = 0;
    [[nodiscard]] virtual int cols() const = 0;
    // 统一物理行快照；unifiedRow ∈ [0, historyLineCount()+screenRowCount())
    [[nodiscard]] virtual ZzLine lineAt(std::size_t unifiedRow) const = 0;
    // 轻量 wrapped 查询（不得触发整行拷贝；供逻辑行链扫描使用）
    [[nodiscard]] virtual bool lineWrapped(std::size_t unifiedRow) const = 0;
    // 历史头部累计丢弃物理行数（单调不减；供选区锚点平移）
    [[nodiscard]] virtual std::uint64_t droppedLineCount() const = 0;
};
```

`src/terminal/ZzSelectionText.h`：

```cpp
// 逻辑行拼接与选区文本提取（M5a）。纯函数，双后端共用。
#pragma once

#include <ZzTerm/Types.h>

#include <cstdint>
#include <string>

class ZzIPhysicalLineSource;

// 统一空间逻辑行总数（按 wrapped 链分组）。
[[nodiscard]] std::int64_t zzLogicalLineCount(const ZzIPhysicalLineSource& src);

// 提取半开区间 [start, end) 的纯文本。规则（规格 5.4）：
//   1. 宽字符按格步进，续格跳过；边界落在半字上时归一（start 退到 lead、
//      end 进到续格之后），不拆半字；
//   2. cluster 格取整串；
//   3. 软换行不插换行；跨逻辑行插单个 '\n'，末尾无换行；
//   4. 每条逻辑行尾部的空单元格与空格修剪；行内空单元格输出为一个空格。
// 坐标越界 clamp；start == end 返回空串。
[[nodiscard]] std::string zzExtractSelectionText(const ZzIPhysicalLineSource& src,
                                                 ZzLogicalPos start,
                                                 ZzLogicalPos end);
```

`src/terminal/ZzSelectionText.cpp`：

```cpp
#include "ZzSelectionText.h"

#include "../backend/ZzLineSource.h"

#include <ZzTerm/Cell.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

void appendCodePoint(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::size_t totalRows(const ZzIPhysicalLineSource& src)
{
    return src.historyLineCount() + static_cast<std::size_t>(src.screenRowCount());
}

// 逻辑行 lineIndex 的物理行区间 [first, first+count)；越界返回 {total, 0}。
std::pair<std::size_t, std::size_t> logicalSpan(const ZzIPhysicalLineSource& src,
                                                std::int64_t lineIndex)
{
    const std::size_t total = totalRows(src);
    std::int64_t current = 0;
    std::size_t row = 0;
    while (row < total) {
        if (current == lineIndex) {
            std::size_t count = 1;
            while (row + count < total && src.lineWrapped(row + count - 1))
                ++count;
            return {row, count};
        }
        while (row + 1 < total && src.lineWrapped(row))
            ++row;
        ++row;
        ++current;
    }
    return {total, 0};
}

// 提取一条逻辑行 [colStart, colEnd) 半开列区间的文本（colEnd < 0 表示到行末）。
// 宽字符边界归一在此完成。行尾空白（空单元格与 U+0020）修剪。
std::string extractLogicalLine(const ZzIPhysicalLineSource& src,
                               std::pair<std::size_t, std::size_t> span,
                               std::int64_t colStart, std::int64_t colEnd)
{
    const int cols = src.cols();
    std::vector<const ZzLine*> lines; // 指向下方 snapshots 的存活期
    std::vector<ZzLine> snapshots;
    snapshots.reserve(span.second);
    for (std::size_t i = 0; i < span.second; ++i)
        snapshots.push_back(src.lineAt(span.first + i));
    for (const ZzLine& l : snapshots)
        lines.push_back(&l);

    const std::int64_t lineLen = static_cast<std::int64_t>(cols) * static_cast<std::int64_t>(span.second);
    std::int64_t begin = std::clamp<std::int64_t>(colStart, 0, lineLen);
    std::int64_t end = colEnd < 0 ? lineLen : std::clamp<std::int64_t>(colEnd, begin, lineLen);
    if (begin >= end)
        return {};

    auto cellAt = [&](std::int64_t offset) -> const ZzCell& {
        return lines[static_cast<std::size_t>(offset / cols)]
            ->cellAt(static_cast<int>(offset % cols));
    };
    // 边界归一：start 落续格退到 lead；end 落续格进到其后（不拆半字）
    if (begin > 0 && cellAt(begin).width() == ZzCellWidth::WideContinuation)
        --begin;
    if (end < lineLen && cellAt(end).width() == ZzCellWidth::WideContinuation)
        ++end;

    std::string out;
    for (std::int64_t i = begin; i < end; ++i) {
        const ZzCell& cell = cellAt(i);
        switch (cell.width()) {
        case ZzCellWidth::WideContinuation:
            continue; // 续格不输出（lead 已取整字）
        case ZzCellWidth::Empty:
            out.push_back(' '); // 行内空白占位；行尾统一修剪
            continue;
        default:
            break;
        }
        if (cell.isCluster()) {
            const ZzLine& owner = *lines[static_cast<std::size_t>(i / cols)];
            out += owner.clusterText(cell.clusterIndex());
        } else if (cell.codePoint() != 0) {
            appendCodePoint(out, cell.codePoint());
        } else {
            out.push_back(' ');
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

} // namespace

std::int64_t zzLogicalLineCount(const ZzIPhysicalLineSource& src)
{
    const std::size_t total = totalRows(src);
    std::int64_t count = 0;
    std::size_t row = 0;
    while (row < total) {
        ++count;
        while (row + 1 < total && src.lineWrapped(row))
            ++row;
        ++row;
    }
    return count;
}

std::string zzExtractSelectionText(const ZzIPhysicalLineSource& src,
                                   ZzLogicalPos start, ZzLogicalPos end)
{
    if (start.line == end.line && start.col == end.col)
        return {};
    // 调用方（ZzSelection::range）已保证 start <= end；防御性交换
    if (end.line < start.line || (end.line == start.line && end.col < start.col))
        std::swap(start, end);
    const std::int64_t logicalCount = zzLogicalLineCount(src);
    if (logicalCount == 0 || start.line >= logicalCount)
        return {};
    start.line = std::max<std::int64_t>(start.line, 0);
    end.line = std::min<std::int64_t>(end.line, logicalCount - 1);

    std::string out;
    for (std::int64_t line = start.line; line <= end.line; ++line) {
        if (line != start.line)
            out.push_back('\n');
        const auto span = logicalSpan(src, line);
        const std::int64_t colStart = line == start.line ? start.col : 0;
        const std::int64_t colEnd = line == end.line ? end.col : -1;
        out += extractLogicalLine(src, span, colStart, colEnd);
    }
    return out;
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_selection_text`
预期：PASS，输出 `test_selection_text: all passed`

- [ ] **步骤 5：Commit**

```bash
git add src/backend/ZzLineSource.h src/terminal/ZzSelectionText.h src/terminal/ZzSelectionText.cpp tests/unit/test_selection_text.cpp tests/CMakeLists.txt
git commit -m "feat(selection): 统一物理行数据源接口与选区文本提取（M5a T2）"
```

---

（任务 3-6 见下文续写）

### 任务 3：native 数据源 ZzNativeLineSource

**文件：**
- 创建：`src/backend/native/ZzNativeLineSource.h`、`src/backend/native/ZzNativeLineSource.cpp`
- 修改：`src/backend/native/CMakeLists.txt`（显式列表加新文件）
- 测试：`tests/unit/test_native_linesource.cpp`
- 构建：`tests/CMakeLists.txt`（shared target_sources 块追加）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_native_linesource.cpp`（只用公开 API 构造 ZzScreen/ZzScrollback，无需碰 native 后端内部符号）：

```cpp
// ZzNativeLineSource 单测（M5a）：历史+屏幕统一坐标、Alternate 历史归零、
// 丢弃计数透传（stats().totalDropped）。
#include "../../src/backend/native/ZzNativeLineSource.h"

#include <ZzTerm/Screen.h>
#include <ZzTerm/Scrollback.h>

#include <cstdio>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

ZzLine makeLine(int cols, char c, bool wrapped)
{
    ZzLine line;
    line.resize(cols);
    ZzCell cell;
    cell.setWidth(ZzCellWidth::Narrow);
    cell.setCodePoint(static_cast<char32_t>(c));
    line.setCell(0, cell);
    line.setWrapped(wrapped);
    return line;
}

} // namespace

static void testUnifiedRowMapping()
{
    ZzScreen screen(10, 3);
    auto scrollback = zzCreateChunkedScrollback(100);
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(10, 'a', false));
    lines.push_back(makeLine(10, 'b', true));
    scrollback->append(std::move(lines));

    ZzNativeLineSource src(screen, *scrollback);
    ZZ_TEST_EXPECT(src.historyLineCount() == 2);
    ZZ_TEST_EXPECT(src.screenRowCount() == 3);
    ZZ_TEST_EXPECT(src.cols() == 10);
    // 统一坐标：0/1 为历史（0 最旧），2 起为屏幕（空白行）
    ZZ_TEST_EXPECT(src.lineAt(0).cellAt(0).codePoint() == U'a');
    ZZ_TEST_EXPECT(src.lineAt(1).cellAt(0).codePoint() == U'b');
    ZZ_TEST_EXPECT(src.lineAt(1).wrapped());
    ZZ_TEST_EXPECT(src.lineWrapped(1) && !src.lineWrapped(0));
    ZZ_TEST_EXPECT(src.lineAt(2).cellAt(0).isEmpty()); // 屏幕空白行
}

static void testDroppedCountPassthrough()
{
    ZzScreen screen(10, 3);
    auto scrollback = zzCreateChunkedScrollback(2); // 容量 2，触发裁剪
    ZzNativeLineSource src(screen, *scrollback);
    std::vector<ZzLine> batch;
    for (int i = 0; i < 5; ++i)
        batch.push_back(makeLine(10, static_cast<char>('a' + i), false));
    scrollback->append(std::move(batch));
    ZZ_TEST_EXPECT(src.historyLineCount() == 2);
    ZZ_TEST_EXPECT(src.droppedLineCount() == 3);
    ZZ_TEST_EXPECT(src.lineAt(0).cellAt(0).codePoint() == U'd'); // 最旧留存
}

static void testAlternateScreenHidesHistory()
{
    ZzScreen screen(10, 3);
    auto scrollback = zzCreateChunkedScrollback(100);
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(10, 'a', false));
    scrollback->append(std::move(lines));
    ZzNativeLineSource src(screen, *scrollback);
    ZZ_TEST_EXPECT(src.historyLineCount() == 1);
    screen.setActiveBuffer(ZzScreenBuffer::Alternate);
    ZZ_TEST_EXPECT(src.historyLineCount() == 0); // Alternate 无历史
    ZZ_TEST_EXPECT(src.screenRowCount() == 3);
}

int main()
{
    testUnifiedRowMapping();
    testDroppedCountPassthrough();
    testAlternateScreenHidesHistory();
    if (g_failures == 0)
        std::printf("test_native_linesource: all passed\n");
    return g_failures;
}
```

tests/CMakeLists.txt 的 target_sources 块追加：

```cmake
if(TARGET test_native_linesource)
    target_sources(test_native_linesource PRIVATE
        "${CMAKE_SOURCE_DIR}/src/backend/native/ZzNativeLineSource.cpp")
endif()
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：编译失败，`ZzNativeLineSource.h: No such file or directory`

- [ ] **步骤 3：实现 ZzNativeLineSource**

`src/backend/native/ZzNativeLineSource.h`：

```cpp
// ZzNativeLineSource：native 后端的统一物理行数据源（M5a）。
// 组合 ZzScrollback（历史）+ ZzScreen（屏幕）；Alternate 屏时历史归零。
#pragma once

#include "../ZzLineSource.h"

class ZzScreen;
class ZzScrollback;

class ZzNativeLineSource final : public ZzIPhysicalLineSource {
public:
    ZzNativeLineSource(const ZzScreen& screen, const ZzScrollback& scrollback) noexcept;

    [[nodiscard]] std::size_t historyLineCount() const override;
    [[nodiscard]] int screenRowCount() const override;
    [[nodiscard]] int cols() const override;
    [[nodiscard]] ZzLine lineAt(std::size_t unifiedRow) const override;
    [[nodiscard]] bool lineWrapped(std::size_t unifiedRow) const override;
    [[nodiscard]] std::uint64_t droppedLineCount() const override;

private:
    const ZzScreen& screen_;
    const ZzScrollback& scrollback_;
};
```

`src/backend/native/ZzNativeLineSource.cpp`：

```cpp
#include "ZzNativeLineSource.h"

#include <ZzTerm/Screen.h>
#include <ZzTerm/Scrollback.h>

ZzNativeLineSource::ZzNativeLineSource(const ZzScreen& screen, const ZzScrollback& scrollback) noexcept
    : screen_(screen), scrollback_(scrollback)
{
}

std::size_t ZzNativeLineSource::historyLineCount() const
{
    if (screen_.activeBuffer() == ZzScreenBuffer::Alternate)
        return 0; // Alternate 无历史（规格 5.1）
    return scrollback_.lineCount();
}

int ZzNativeLineSource::screenRowCount() const
{
    return screen_.size().rows;
}

int ZzNativeLineSource::cols() const
{
    return screen_.size().cols;
}

ZzLine ZzNativeLineSource::lineAt(std::size_t unifiedRow) const
{
    const std::size_t history = historyLineCount();
    if (unifiedRow < history)
        return scrollback_.lineAt(unifiedRow);
    return screen_.lineAt(static_cast<int>(unifiedRow - history));
}

bool ZzNativeLineSource::lineWrapped(std::size_t unifiedRow) const
{
    const std::size_t history = historyLineCount();
    if (unifiedRow < history)
        return scrollback_.lineAt(unifiedRow).wrapped();
    return screen_.lineAt(static_cast<int>(unifiedRow - history)).wrapped();
}

std::uint64_t ZzNativeLineSource::droppedLineCount() const
{
    return scrollback_.stats().totalDropped;
}
```

`src/backend/native/CMakeLists.txt` 的 target_sources 列表加入 `ZzNativeLineSource.cpp`（保持字母/既有排序风格）。

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_native_linesource`
预期：PASS，输出 `test_native_linesource: all passed`

- [ ] **步骤 5：Commit**

```bash
git add src/backend/native/ZzNativeLineSource.h src/backend/native/ZzNativeLineSource.cpp src/backend/native/CMakeLists.txt tests/unit/test_native_linesource.cpp tests/CMakeLists.txt
git commit -m "feat(selection): native 统一物理行数据源（M5a T3）"
```

---

### 任务 4：Contour 历史只读口 + ZzContourLineSource

**文件：**
- 修改：`src/backend/contour/ZzContourBackend.h`、`src/backend/contour/ZzContourBackend.cpp`（新增只读口，不动 third_party）
- 创建：`src/backend/contour/ZzContourLineSource.h`、`src/backend/contour/ZzContourLineSource.cpp`
- 修改：`src/backend/contour/CMakeLists.txt`（显式列表加新文件）
- 测试：`tests/unit/test_contour_linesource.cpp`
- 构建：`tests/CMakeLists.txt`（剔除 GLOB + 条件注册，仿 test_contour_backend 块）

- [ ] **步骤 0：核实规格 5.6 计划阶段核实项（Contour reflow × stable id）**

阅读 `third_party/contour/src/vtbackend/grid/Grid.cpp` 的 reflow 路径（rotateBuffersLeft 与 _stableBase/_stableFloor 记账，:1050-1130 区域），回答：列变化 resize 后 ①stableRangeFloor 是否只随容量裁剪前移（reflow 本身不制造虚假前移）；②历史行内容在 reflow 后仍可按负偏移读取。结论写进 ZzContourLineSource.cpp 文件头注释；若 ①不成立（reflow 会扰动 floor），在 noteFloor 注释中钉住并按 compat 实测语义调整（分歧按 b 类惯例）。

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_contour_linesource.cpp`（构造模式仿 test_contour_backend.cpp 的 ZzContourEvents stub 与 ZzContourBackend 构造；断言历史快照内容、wrapped 标记、容量溢出后 droppedLineCount）：

```cpp
// ZzContourLineSource 单测（M5a）：历史行快照（格+wrapped）、屏幕行快照、
// stableFloor 前移累计为丢弃计数。仅 Contour 后端启用时构建。
#include "../../src/backend/contour/ZzContourLineSource.h"
#include "../../src/backend/contour/ZzContourBackend.h"
#include "../../src/backend/contour/ZzContourEvents.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

// 事件 stub：照 test_contour_backend.cpp 的既有写法（全部默认空实现即可）。
struct NullEvents : ZzContourEvents {
    void onTitleChanged(std::string_view) override {}
    void onBell() override {}
    void onScreenDirty() override {}
    void onActiveBufferChanged() override {}
    void onWriteToTransport(std::string_view) override {}
};

std::string lineText(const ZzLine& line, int n)
{
    std::string out;
    for (int i = 0; i < n; ++i) {
        const ZzCell& c = line.cellAt(i);
        if (c.isEmpty())
            break;
        out.push_back(static_cast<char>(c.codePoint()));
    }
    return out;
}

} // namespace

static void testHistorySnapshot()
{
    NullEvents events;
    ZzContourBackend backend(10, 2, events, 100); // 10 列 2 行小屏
    // 造 3 条硬行历史 + 屏幕可见行：每行 "r0\r\n" 等
    backend.feed("r0\r\nr1\r\nr2\r\nr3");
    ZzContourLineSource src(backend);
    ZZ_TEST_EXPECT(src.historyLineCount() == 2);      // 4 行内容 2 行屏 → 2 行历史
    ZZ_TEST_EXPECT(lineText(src.lineAt(0), 2) == "r0"); // 0 = 最旧
    ZZ_TEST_EXPECT(!src.lineWrapped(0));                // 硬行
    ZZ_TEST_EXPECT(src.screenRowCount() == 2);
    ZZ_TEST_EXPECT(lineText(src.lineAt(src.historyLineCount()), 2) == "r2"); // 屏幕首行
}

static void testSoftWrapChainWrappedFlag()
{
    NullEvents events;
    ZzContourBackend backend(5, 2, events, 100);
    backend.feed("abcdefgh"); // 5 列：abcd? 实际 "abcde" 满宽续行 + "fgh"
    ZzContourLineSource src(backend);
    // 屏幕首行（统一坐标的 historyLineCount() 行）wrapped=true（续到下一行）
    const auto screenFirst = src.historyLineCount();
    ZZ_TEST_EXPECT(src.lineWrapped(screenFirst));
    ZZ_TEST_EXPECT(!src.lineWrapped(screenFirst + 1));
    ZZ_TEST_EXPECT(lineText(src.lineAt(screenFirst), 5) == "abcde");
}

static void testDroppedCountAccumulates()
{
    NullEvents events;
    ZzContourBackend backend(10, 2, events, 4); // 历史容量 4 行
    ZzContourLineSource src(backend);
    std::string script;
    for (int i = 0; i < 10; ++i)
        script += "line" + std::to_string(i) + "\r\n";
    backend.feed(script);
    src.noteFloor(); // 适配层在 feed 后调用；测试直调验证语义
    ZZ_TEST_EXPECT(src.historyLineCount() == 4);
    ZZ_TEST_EXPECT(src.droppedLineCount() > 0); // 容量溢出产生丢弃
    // 幂等：无新 feed 时再次 noteFloor 不增加
    const auto dropped = src.droppedLineCount();
    src.noteFloor();
    ZZ_TEST_EXPECT(src.droppedLineCount() == dropped);
}

int main()
{
    testHistorySnapshot();
    testSoftWrapChainWrappedFlag();
    testDroppedCountAccumulates();
    if (g_failures == 0)
        std::printf("test_contour_linesource: all passed\n");
    return g_failures;
}
```

注意：ZzContourEvents 的方法名以 src/backend/contour/ZzContourEvents.h 实际声明为准（若 stub 与头不符，按头修正）；feed 行为细节（满宽续行的确切断点）若与断言不符，以实测为准调整断言并在注释说明。

tests/CMakeLists.txt：REMOVE_ITEM 列表（:11-15）追加一行 `"${CMAKE_CURRENT_SOURCE_DIR}/unit/test_contour_linesource.cpp"`，并在 test_contour_backend 注册块（:50-55）后追加同形块：

```cmake
# M5a：ZzContourLineSource 数据源测试，仅 Contour 后端启用时构建。
if(TARGET ZzTermContourBackend)
    add_executable(test_contour_linesource unit/test_contour_linesource.cpp)
    target_link_libraries(test_contour_linesource PRIVATE ZzTermContourBackend vtpty) # Contour target 一律 PRIVATE
    target_compile_features(test_contour_linesource PRIVATE cxx_std_23)
    add_test(NAME test_contour_linesource COMMAND test_contour_linesource)
endif()
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：编译失败，`ZzContourLineSource.h: No such file or directory`

- [ ] **步骤 3：实现 Contour 只读口与数据源**

`src/backend/contour/ZzContourBackend.h`：顶部 include 区加 `#include <ZzTerm/Line.h>`；在 `historyLineCount()`/`lineWrapped()` 声明（:70-72）之后新增：

```cpp
    /// \brief 历史第 i 行内容快照（i ∈ [0, historyLineCount())，0 = 最旧）。
    /// 返回 ZzLine 值快照（含 wrapped 标记，ZzLine 语义：续到下一行为 true）。
    [[nodiscard]] ZzLine historyLineSnapshot(int historyIndex) const;
    /// \brief 主屏/当前屏第 row 行内容快照（同上）。
    [[nodiscard]] ZzLine screenLineSnapshot(int row) const;
    /// \brief 历史第 i 行是否续到下一行（等价快照内 wrapped，轻量路径）。
    [[nodiscard]] bool historyLineWrapped(int historyIndex) const;
    /// \brief Grid stable id 下限（容量裁剪丢弃探测；单调，zero-history 分支可回退）。
    [[nodiscard]] std::int64_t stableFloor() const;
```

`src/backend/contour/ZzContourBackend.cpp` 实现（方法链 `impl_->terminal->currentScreen().grid()`，全部只读、不持锁——与现有 historyLineCount/RenderView 读路径同一约定；**禁用 Screen::at**（blank 行会物化分配），走 `lineAt` + `isBlank` 守卫 + `storage()` const 直读，blank 行填默认格并照 ZzContourRenderView.cpp:52-55 用 fillAttrs 还原背景；格转换复用 ZzContourConvert.h 的 zzWidth/zzColor/zzAttributes）：

```cpp
// 文件内匿名 namespace 辅助：
namespace {

// 把 contour Line（SoA）转为 ZzLine 快照。nextLineWrapped = 本行是否续到下一行。
ZzLine zzSnapshotContourLine(const vtbackend::Line& line, bool nextLineWrapped)
{
    const int cols = line.size().value;
    ZzLine out;
    out.resize(cols);
    if (!line.isBlank()) {
        auto const& soa = line.storage();
        for (int col = 0; col < cols; ++col) {
            const auto offset = vtbackend::ColumnOffset(col);
            ZzCell cell;
            // 宽度/续格判定与 ZzContourRenderView 一致：
            // WideCharContinuation flag → WideContinuation；width==2 → WideLead；否则 Narrow
            const bool continuation =
                soa.sgr[col].flags.contains(vtbackend::CellFlag::WideCharContinuation);
            if (continuation)
                cell.setWidth(ZzCellWidth::WideContinuation);
            else if (line.cellWidthAt(offset) == 2)
                cell.setWidth(ZzCellWidth::WideLead);
            else
                cell.setWidth(ZzCellWidth::Narrow);
            cell.setForeground(zzColor(soa.sgr[col].foreground));
            cell.setBackground(zzColor(soa.sgr[col].background));
            cell.setAttributes(zzAttributes(soa.sgr[col].flags));
            // 文本：cluster（多码点）整串 intern 到快照行侧表；单码点直接 setCodePoint
            const auto clusterSize = soa.clusterSize[col]; // 字段名以 LineSoA 实际为准
            if (!continuation) {
                if (clusterSize > 1) {
                    std::string text; // 主码点 + clusterPool 追加段
                    text = vtbackend::ConstCellProxy(line, offset).toUtf8(); // 或等价 const 路径
                    cell.setCluster(out.internCluster(text));
                } else {
                    cell.setCodePoint(soa.codepoints[col]);
                }
            }
            out.setCell(col, cell);
        }
    } else {
        // blank 行：默认格 + fillAttrs 背景（照 ZzContourRenderView.cpp:52-55）
        ZzCell cell;
        cell.setBackground(zzColor(line.storage().fillAttrs.backgroundColor));
        cell.setForeground(zzColor(line.storage().fillAttrs.foregroundColor));
        cell.setAttributes(zzAttributes(line.storage().fillAttrs.flags));
        for (int col = 0; col < cols; ++col)
            out.setCell(col, cell);
    }
    out.setWrapped(nextLineWrapped);
    return out;
}

} // namespace

ZzLine ZzContourBackend::historyLineSnapshot(int historyIndex) const
{
    auto const& screen = impl_->terminal->currentScreen();
    const auto offset = vtbackend::LineOffset(historyIndex - historyLineCount());
    return zzSnapshotContourLine(screen.grid().lineAt(offset), screen.isLineWrapped(offset + 1));
}

ZzLine ZzContourBackend::screenLineSnapshot(int row) const
{
    auto const& screen = impl_->terminal->currentScreen();
    const auto offset = vtbackend::LineOffset(row);
    const bool next = row + 1 < size().second && screen.isLineWrapped(offset + 1);
    return zzSnapshotContourLine(screen.grid().lineAt(offset), next);
}

bool ZzContourBackend::historyLineWrapped(int historyIndex) const
{
    auto const& screen = impl_->terminal->currentScreen();
    return screen.isLineWrapped(vtbackend::LineOffset(historyIndex - historyLineCount() + 1));
}

std::int64_t ZzContourBackend::stableFloor() const
{
    return impl_->terminal->currentScreen().grid().stableRangeFloor();
}
```

实现注意（步骤 0 的调研结论落到代码）：SoA 字段名（`soa.codepoints/sgr/clusterSize/fillAttrs` 的具体成员名与 CellProxy 构造路径）以 `third_party/contour/src/vtbackend/grid/Line.hpp`/`CellProxy.hpp` 实际声明为准调整；若 `ConstCellProxy` 构造不便，cluster 文本可经 `line.toUtf8(begin, end, ...)` 单格切片获取。

`src/backend/contour/ZzContourLineSource.h`：

```cpp
// ZzContourLineSource：contour 后端的统一物理行数据源（M5a）。
// 历史读取经 ZzContourBackend 新增只读口；丢弃计数由适配层在每次
// feed/resize 后调 noteFloor() 累计（stableFloor 前移量；zero-history 分支
// 回退不计）。Alternate 屏时历史归零。
#pragma once

#include "../ZzLineSource.h"

class ZzContourBackend;

class ZzContourLineSource final : public ZzIPhysicalLineSource {
public:
    explicit ZzContourLineSource(const ZzContourBackend& backend) noexcept;

    [[nodiscard]] std::size_t historyLineCount() const override;
    [[nodiscard]] int screenRowCount() const override;
    [[nodiscard]] int cols() const override;
    [[nodiscard]] ZzLine lineAt(std::size_t unifiedRow) const override;
    [[nodiscard]] bool lineWrapped(std::size_t unifiedRow) const override;
    [[nodiscard]] std::uint64_t droppedLineCount() const override;

    // 适配层在每次 feed/resize 完成后调用：累计 stableFloor 前移量。
    void noteFloor() noexcept;

private:
    const ZzContourBackend& backend_;
    std::int64_t lastFloor_ = 0;
    std::uint64_t droppedAccum_ = 0;
};
```

`src/backend/contour/ZzContourLineSource.cpp`：

```cpp
#include "ZzContourLineSource.h"

#include "ZzContourBackend.h"

ZzContourLineSource::ZzContourLineSource(const ZzContourBackend& backend) noexcept
    : backend_(backend)
{
    lastFloor_ = backend_.stableFloor();
}

std::size_t ZzContourLineSource::historyLineCount() const
{
    if (backend_.isAlternateScreen())
        return 0; // Alternate 无历史（规格 5.1）
    return static_cast<std::size_t>(backend_.historyLineCount());
}

int ZzContourLineSource::screenRowCount() const
{
    return backend_.size().second;
}

int ZzContourLineSource::cols() const
{
    return backend_.size().first;
}

ZzLine ZzContourLineSource::lineAt(std::size_t unifiedRow) const
{
    const auto history = historyLineCount();
    if (unifiedRow < history)
        return backend_.historyLineSnapshot(static_cast<int>(unifiedRow));
    return backend_.screenLineSnapshot(static_cast<int>(unifiedRow - history));
}

bool ZzContourLineSource::lineWrapped(std::size_t unifiedRow) const
{
    const auto history = historyLineCount();
    if (unifiedRow < history)
        return backend_.historyLineWrapped(static_cast<int>(unifiedRow));
    return backend_.lineWrapped(static_cast<int>(unifiedRow - history));
}

std::uint64_t ZzContourLineSource::droppedLineCount() const
{
    return droppedAccum_;
}

void ZzContourLineSource::noteFloor() noexcept
{
    const std::int64_t floor = backend_.stableFloor();
    if (floor > lastFloor_)
        droppedAccum_ += static_cast<std::uint64_t>(floor - lastFloor_);
    lastFloor_ = floor; // 回退（zero-history 分支）直接对齐，不计负丢弃
}
```

`src/backend/contour/CMakeLists.txt` 的 add_library 列表加入 `ZzContourLineSource.cpp`。

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_contour_linesource`
预期：PASS，输出 `test_contour_linesource: all passed`

- [ ] **步骤 5：Commit**

```bash
git add src/backend/contour/ZzContourBackend.h src/backend/contour/ZzContourBackend.cpp src/backend/contour/ZzContourLineSource.h src/backend/contour/ZzContourLineSource.cpp src/backend/contour/CMakeLists.txt tests/unit/test_contour_linesource.cpp tests/CMakeLists.txt
git commit -m "feat(selection): contour 历史只读口与统一物理行数据源（M5a T4）"
```

---

### 任务 5：后端接口接线 + facade 选区 API

**文件：**
- 修改：`src/backend/ZzTerminalBackend.h`（新增纯虚 lineSource）
- 修改：`src/backend/native/ZzNativeBackend.h`、`src/backend/native/ZzNativeBackend.cpp`（成员 + override）
- 修改：`src/backend/contour/ZzContourBackendAdapter.cpp`（成员 + noteFloor 调用 + override）
- 修改：`include/ZzTerm/Terminal.h`（六个选区 API 声明，doxygen 中文注释）
- 修改：`src/terminal/Terminal.cpp`（Impl 成员 + feed/resize 包裹 + API 实现）
- 测试：`tests/unit/test_terminal_selection.cpp`（GLOB 自动收编，仅链 ZzTermCore）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_terminal_selection.cpp`（全部走公开 facade API）：

```cpp
// facade 选区集成测试（M5a）：setSelection/selectedText、reflow 锚点保持、
// 丢弃平移、Alternate 清除、selectionRange 查询。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

void feed(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                      bytes.size()));
}

} // namespace

static void testSelectScreenText()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "hello");
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 5});
    ZZ_TEST_EXPECT(term.hasSelection());
    ZZ_TEST_EXPECT(term.selectedText() == "hello");
    term.clearSelection();
    ZZ_TEST_EXPECT(!term.hasSelection());
    ZZ_TEST_EXPECT(term.selectedText().empty());
}

static void testSelectAcrossSoftWrap()
{
    ZzTerminal term(5, 3, ZzBackendKind::Native, 100);
    feed(term, "abcdefgh"); // 软换行：abcde / fgh
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 8});
    ZZ_TEST_EXPECT(term.selectedText() == "abcdefgh"); // 软换行不插换行
}

static void testSelectHistoryAndScreenSeam()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 100);
    feed(term, "aaaa\r\nbbbb\r\ncccc"); // 2 行屏：aaaa 滚入历史，屏幕 bbbb/cccc
    // 统一空间：历史 2 行 + 屏幕 2 行；选第 0 条逻辑行
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 4});
    ZZ_TEST_EXPECT(term.selectedText() == "aaaa");
}

static void testSelectionRangeQuery()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "hello");
    term.setSelection(ZzLogicalPos{0, 4}, ZzLogicalPos{0, 1}); // 反向
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.selectionRange(s, e));
    ZZ_TEST_EXPECT(s.col == 1 && e.col == 4);
    term.clearSelection();
    ZZ_TEST_EXPECT(!term.selectionRange(s, e));
}

static void testResizeReflowKeepsSelection()
{
    ZzTerminal term(5, 3, ZzBackendKind::Native, 100);
    feed(term, "abcdefgh");
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 8});
    const std::string before = term.selectedText();
    term.resize(10, 3); // 列变触发 reflow，逻辑行集合不变
    ZZ_TEST_EXPECT(term.selectedText() == before);
    term.resize(4, 3);
    ZZ_TEST_EXPECT(term.selectedText() == before);
}

static void testDroppedShiftsAnchor()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 4); // 历史容量 4
    feed(term, "aaaaaaaaaa"); // 第 0 行内容
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 10});
    std::string script;
    for (int i = 0; i < 6; ++i)
        script += "x" + std::to_string(i) + "\r\n"; // 挤出历史容量
    feed(term, script);
    // 原选区内容已被丢弃：锚点 clamp 到 0 或选区清空，两种都合法——
    // 断言不为崩溃且文本不再是原始内容
    ZZ_TEST_EXPECT(term.selectedText() != "aaaaaaaaaa");
}

static void testAlternateSwitchClearsSelection()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "hello");
    term.setSelection(ZzLogicalPos{0, 0}, ZzLogicalPos{0, 5});
    ZZ_TEST_EXPECT(term.hasSelection());
    feed(term, "\x1b[?1049h"); // 进 Alternate
    ZZ_TEST_EXPECT(!term.hasSelection());
}

int main()
{
    testSelectScreenText();
    testSelectAcrossSoftWrap();
    testSelectHistoryAndScreenSeam();
    testSelectionRangeQuery();
    testResizeReflowKeepsSelection();
    testDroppedShiftsAnchor();
    testAlternateSwitchClearsSelection();
    if (g_failures == 0)
        std::printf("test_terminal_selection: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：编译失败，`ZzTerminal` 无 `setSelection` 成员

- [ ] **步骤 3：后端接口与各后端接线**

`src/backend/ZzTerminalBackend.h`：include 加 `"ZzLineSource.h"`；接口末尾（sendFocus 后）加：

```cpp
    /// 统一物理行只读数据源（M5a 选区/复制；借用语义同 renderView）。
    [[nodiscard]] virtual const ZzIPhysicalLineSource& lineSource() const noexcept = 0;
```

`src/backend/native/ZzNativeBackend.h`：include `"ZzNativeLineSource.h"`；成员区在 `scrollback_`（:62）之后加 `ZzNativeLineSource lineSource_;`（声明顺序须在 screen_/scrollback_ 之后——成员按声明序构造）；公开区加 override：

```cpp
[[nodiscard]] const ZzIPhysicalLineSource& lineSource() const noexcept override { return lineSource_; }
```

`src/backend/native/ZzNativeBackend.cpp`：构造函数初始化列表在 scrollback_ 之后加 `lineSource_(screen_, *scrollback_)`（注意 scrollback_ 须在初始化列表中先于 lineSource_ 完成赋值）。

`src/backend/contour/ZzContourBackendAdapter.cpp`：匿名 namespace 的 Adapter 类加成员 `ZzContourLineSource lineSource_;`（初始化列表以 `*backend_` 构造——注意成员声明顺序 backend_ 在前）；`feed`（:22-48）在 `backend_->feed` 返回后、`resize` 完成后各调一次 `lineSource_.noteFloor();`；加 override：

```cpp
[[nodiscard]] const ZzIPhysicalLineSource& lineSource() const noexcept override { return lineSource_; }
```

（ZzContourLineSource 构造需要 backend_ 已存在：若 Adapter 当前用 make_unique 在 ctor 体内构造 backend_，则把 lineSource_ 改为 `std::unique_ptr<ZzContourLineSource>` 在 backend_ 构造后跟进，或在初始化列表用 backend_.get() 解引用——按现有代码实际结构选择，注释说明顺序约束。）

- [ ] **步骤 4：facade API 与锚点维护**

`include/ZzTerm/Terminal.h`：在 sendFocus（:218）之后、"Core 内部访问"注释块（:220）之前新增（doxygen 中文注释，注意陷阱：行内 code span 禁尖括号、禁 `#` 词、禁反斜杠转义）：

```cpp
    // ---- 选区与复制（M5a） ----

    /**
     * @brief 设置选区（替换现有选区）。
     * @param anchor 锚点（选区固定端）。
     * @param extent 活动端。anchor 与 extent 无序要求，内部规范化。
     * @note 坐标越界 clamp 到有效范围；鼠标/触摸换算由前端负责。
     *       历史头部丢弃时 Core 自动平移锚点（物理行计数近似，语义见
     *       docs/Architecture.md 选区条款）；切换 Alternate 屏时选区清空。
     */
    void setSelection(ZzLogicalPos anchor, ZzLogicalPos extent);

    /**
     * @brief 拖动选区活动端（anchor 不变）。
     * @param extent 新活动端。无选区时等价于 setSelection(extent, extent)。
     */
    void extendSelection(ZzLogicalPos extent);

    /// @brief 清空选区。
    void clearSelection() noexcept;

    /// @brief 是否有非空选区（anchor != extent）。
    [[nodiscard]] bool hasSelection() const noexcept;

    /**
     * @brief 查询规范化选区区间（半开区间 [start, end)）。
     * @param start 输出：区间起点。
     * @param end 输出：区间终点。
     * @return false = 空选区（start/end 不写入）。
     * @note 供前端绘制高亮使用；feed/resize 后坐标可能已被平移/clamp。
     */
    bool selectionRange(ZzLogicalPos& start, ZzLogicalPos& end) const;

    /**
     * @brief 提取选区纯文本。
     * @return UTF-8 文本；空选区返回空串。规则：宽字符整取、cluster 整串、
     *         软换行不插换行、跨逻辑行插单个换行、行尾空白修剪。
     */
    [[nodiscard]] std::string selectedText() const;
```

（Terminal.h 顶部 include 区确认 `<string>` 已在；ZzLogicalPos 来自 Types.h，Terminal.h 已 include Types.h。）

`src/terminal/Terminal.cpp`：

- include 区加 `"ZzSelection.h"`、`"ZzSelectionText.h"`；
- `Impl`（:12-30）在 `backend` 成员后加：

```cpp
    ZzSelection selection;
    std::uint64_t lastDropped = 0; // lineSource().droppedLineCount() 的上次观测值

    // feed/resize 后维护选区锚点（规格 5.1：Alternate 切换清空；丢弃平移）
    void noteSelectionAfterFeed(const ZzTermChanges& changes)
    {
        const std::uint64_t dropped = backend->lineSource().droppedLineCount();
        if (changes.activeBufferChanged) {
            selection.clear();
        } else if (dropped > lastDropped) {
            selection.onLinesDropped(dropped - lastDropped);
        }
        lastDropped = dropped;
    }
```

- `feed`（:36）与 `resize`（:37）改为包裹：

```cpp
ZzTermChanges ZzTerminal::feed(std::span<const std::byte> data)
{
    ZzTermChanges changes = impl_->backend->feed(data);
    impl_->noteSelectionAfterFeed(changes);
    return changes;
}

bool ZzTerminal::resize(int cols, int rows)
{
    const bool changed = impl_->backend->resize(cols, rows);
    if (changed) {
        impl_->noteSelectionAfterFeed(ZzTermChanges{}); // resize 也可能丢弃（reflow 裁剪）
        impl_->selection.clampTo(zzLogicalLineCount(impl_->backend->lineSource()));
    }
    return changed;
}
```

- 文件末尾新增六个 API 实现：

```cpp
void ZzTerminal::setSelection(ZzLogicalPos anchor, ZzLogicalPos extent)
{
    const std::int64_t count = zzLogicalLineCount(impl_->backend->lineSource());
    if (count == 0) {
        impl_->selection.clear();
        return;
    }
    auto clampLine = [count](ZzLogicalPos& p) {
        p.line = std::clamp<std::int64_t>(p.line, 0, count - 1);
        p.col = std::max<std::int32_t>(p.col, 0);
    };
    clampLine(anchor);
    clampLine(extent);
    impl_->selection.set(anchor, extent);
}

void ZzTerminal::extendSelection(ZzLogicalPos extent)
{
    const std::int64_t count = zzLogicalLineCount(impl_->backend->lineSource());
    if (count == 0) {
        impl_->selection.clear();
        return;
    }
    extent.line = std::clamp<std::int64_t>(extent.line, 0, count - 1);
    extent.col = std::max<std::int32_t>(extent.col, 0);
    // 空选区时 extend 等价于放置一个零长选区（ZzSelection::extend 只动 extent_，
    // anchor_ 保持默认原点——与 "无选区时等价 setSelection(extent, extent)" 的
    // 声明语义不同，这里直接走 set 保证直觉一致）。
    if (impl_->selection.empty())
        impl_->selection.set(extent, extent);
    else
        impl_->selection.extend(extent);
}

void ZzTerminal::clearSelection() noexcept
{
    impl_->selection.clear();
}

bool ZzTerminal::hasSelection() const noexcept
{
    return !impl_->selection.empty();
}

bool ZzTerminal::selectionRange(ZzLogicalPos& start, ZzLogicalPos& end) const
{
    return impl_->selection.range(start, end);
}

std::string ZzTerminal::selectedText() const
{
    ZzLogicalPos start, end;
    if (!impl_->selection.range(start, end))
        return {};
    return zzExtractSelectionText(impl_->backend->lineSource(), start, end);
}
```

注意：`extendSelection` 的 anchor 保持依赖 ZzSelection::extend 语义（只动 extent_），实现时核对与单测 testExtendKeepsAnchor 一致；`<algorithm>` include 补齐。

- [ ] **步骤 5：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿（33 + 新增若干），含 `test_terminal_selection: all passed`

- [ ] **步骤 6：Commit**

```bash
git add src/backend/ZzTerminalBackend.h src/backend/native/ZzNativeBackend.h src/backend/native/ZzNativeBackend.cpp src/backend/contour/ZzContourBackendAdapter.cpp include/ZzTerm/Terminal.h src/terminal/Terminal.cpp tests/unit/test_terminal_selection.cpp
git commit -m "feat(selection): 后端 lineSource 接线与 facade 选区 API（M5a T5）"
```

---

### 任务 6：双后端 compat + 文档落定 + 全回归

**文件：**
- 测试：`tests/unit/test_selection_compat.cpp`
- 构建：`tests/CMakeLists.txt`（剔除 GLOB + 条件注册，仿 test_backend_compat 块，仅链 ZzTermCore）
- 文档：`docs/Architecture.md`、`docs/API.md`、`docs/VT-Xterm-Checklist.md`

- [ ] **步骤 1：编写 compat 测试并运行观察**

创建 `tests/unit/test_selection_compat.cpp`（Dual 结构与 feedBoth 仿 test_backend_compat.cpp:22-50；断言 selectedText 逐字节一致，而非逐格——选区语义比对）：

```cpp
// 双后端选区 compat（M5a）：同一 VT 脚本喂 Native/Contour，选区提取文本
// 逐字节一致（native 为基准）；含跨接缝逻辑行、宽字符、reflow 保持用例。
// 已知分歧按 b 类惯例分别断言 + 注释钉住。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <string>

static int g_failures = 0;
#define ZZ_CHECK(cond)                                                        \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

struct Dual {
    ZzTerminal native { 10, 3, ZzBackendKind::Native, 100 };
    ZzTerminal contour { 10, 3, ZzBackendKind::Contour, 100 };
    void feedBoth(std::string_view bytes)
    {
        native.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
        contour.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
    }
};

void checkSelectedTextEqual(Dual& d, ZzLogicalPos start, ZzLogicalPos end, const char* what)
{
    d.native.setSelection(start, end);
    d.contour.setSelection(start, end);
    const std::string a = d.native.selectedText();
    const std::string b = d.contour.selectedText();
    if (a != b)
        std::fprintf(stderr, "  mismatch [%s]: native=%zu bytes contour=%zu bytes\n",
                     what, a.size(), b.size());
    ZZ_CHECK(a == b);
    d.native.clearSelection();
    d.contour.clearSelection();
}

} // namespace

// 1. 屏幕区 ASCII 选区
static void testScreenSelection()
{
    Dual d;
    d.feedBoth("hello");
    checkSelectedTextEqual(d, {0, 0}, {0, 5}, "screen ascii");
}

// 2. 历史+屏幕统一空间选区（10x3 屏，5 行硬行脚本：2 行入历史）
static void testHistorySelection()
{
    Dual d;
    d.feedBoth("r0\r\nr1\r\nr2\r\nr3\r\nr4");
    checkSelectedTextEqual(d, {0, 0}, {0, 2}, "history line 0");
    checkSelectedTextEqual(d, {1, 0}, {2, 2}, "cross history/screen");
}

// 3. 软换行逻辑行选区（不插换行）
static void testSoftWrapSelection()
{
    Dual d;
    d.feedBoth("0123456789abcde"); // 10 列软换行两条物理行
    checkSelectedTextEqual(d, {0, 0}, {0, 15}, "softwrap joined");
}

// 4. 跨接缝逻辑行（历史末行软续到屏幕首行）：收口 M4 观察项①。
//    10x3 屏：首条逻辑行 16 格占两条物理行，脚本共 4 物理行 → 历史恰留
//    链首行（abcdefghij），链尾（klmnop）在屏幕首行，接缝断在链中间。
//    若两后端在接缝拼接上分歧，按 b 类分别断言并注释钉住。
static void testSeamLogicalLine()
{
    Dual d; // 10x3
    d.feedBoth("abcdefghijklmnop\r\nzz\r\nww"); // 4 物理行，历史 1 行 = 链首
    checkSelectedTextEqual(d, {0, 0}, {0, 16}, "seam stitched logical line");
}

// 5. 宽字符选区
static void testWideCharSelection()
{
    Dual d;
    d.feedBoth("ab界面cd");
    checkSelectedTextEqual(d, {0, 0}, {0, 6}, "wide chars");
}

// 6. resize reflow 后选区文本保持（双后端各自断言 resize 前后一致 + 互比）
static void testReflowKeepsSelectionText()
{
    Dual d;
    d.feedBoth("0123456789abcde");
    d.native.setSelection({0, 0}, {0, 15});
    d.contour.setSelection({0, 0}, {0, 15});
    const std::string nativeBefore = d.native.selectedText();
    const std::string contourBefore = d.contour.selectedText();
    d.native.resize(5, 3);
    d.contour.resize(5, 3);
    ZZ_CHECK(d.native.selectedText() == nativeBefore);
    ZZ_CHECK(d.contour.selectedText() == contourBefore);
    ZZ_CHECK(d.native.selectedText() == d.contour.selectedText());
}

int main()
{
    testScreenSelection();
    testHistorySelection();
    testSoftWrapSelection();
    testSeamLogicalLine();
    testWideCharSelection();
    testReflowKeepsSelectionText();
    if (g_failures == 0)
        std::printf("test_selection_compat: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
```

tests/CMakeLists.txt：REMOVE_ITEM 列表追加 `"${CMAKE_CURRENT_SOURCE_DIR}/unit/test_selection_compat.cpp"`；test_backend_compat 注册块（:68-72）后追加同形块：

```cmake
# M5a：双后端选区提取 compat（同一脚本 selectedText 逐字节一致，native 为基准）。
# 只碰公开头，仅链接 ZzTermCore（同 test_backend_compat 的 shared 构建理由）。
if(TARGET ZzTermContourBackend)
    add_executable(test_selection_compat unit/test_selection_compat.cpp)
    target_link_libraries(test_selection_compat PRIVATE ZzTermCore)
    add_test(NAME test_selection_compat COMMAND test_selection_compat)
endif()
```

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_selection_compat`
预期：PASS。若接缝（用例 4）或 reflow（用例 6）出现双后端分歧：按 b 类惯例把该用例改为分别断言各自语义 + 注释钉住分歧原因，并在 commit message 与 Architecture.md 中记录。

- [ ] **步骤 2：文档落定**

- `docs/Architecture.md`：
  - §894 附近"Logical Position 模型 → 随 selection/search（M5）"更新为：M5a 已落地 ZzLogicalPos（逻辑行序号 + 行内格偏移），定义与锚定不变量（reflow 保持、丢弃平移近似语义、Alternate 清空）；
  - §19 M5 里程碑行：标注 M5a（selection/copy）已完成，M5b（search/highlight）待建；
  - §13（search 不得拼大字符串）：补注 M5a 的统一物理行数据源（ZzIPhysicalLineSource）即为 M5b 按 logical line/chunk 扫描的地基；
  - §504-505：Selection anchor 保持条款标注已在 M5a 落地（语义：丢弃平移 clamp、reflow 保持）；Search Match 保持仍为 M5b；
  - 新增或更新选区相关条款时同步记录本里程碑 compat 钉住的 b 类分歧（若步骤 1 产生）。
- `docs/API.md`：新增"选区与复制"小节：六个 facade API 用法、ZzLogicalPos 坐标约定、提取规则四条、前端职责（鼠标换算、高亮绘制用 selectionRange）。
- `docs/VT-Xterm-Checklist.md`：仿 Wrap/Reflow 章格式补 Selection 章：setSelection/extendSelection/clearSelection/hasSelection/selectionRange/selectedText 六项 + 双后端 compat 状态。

doxygen 注释陷阱复查（本计划所有公开头改动）：行内 code span 内禁尖括号、内容禁以点开头、后禁紧跟顿号、禁 `#` 预处理词、禁反斜杠转义。

- [ ] **步骤 3：全回归门**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check  # 新增测试文件若未收编，先重新 configure
doxygen Doxyfile  # exit 0 且零警告
```

预期：ON 全绿（含 test_selection / test_selection_text / test_native_linesource / test_contour_linesource / test_terminal_selection / test_selection_compat）、OFF 全绿（contour 相关测试缺席为预期）、shared 全绿、doxygen 零警告。

- [ ] **步骤 4：Commit**

```bash
git add tests/unit/test_selection_compat.cpp tests/CMakeLists.txt docs/Architecture.md docs/API.md docs/VT-Xterm-Checklist.md
git commit -m "test(selection): 双后端选区 compat 与 M5a 文档落定（M5a T6）"
```

---

## 自检结论（计划编写后）

## 执行修正记录（SDD 过程中沉淀，规格语义不变）

- T2：testOutOfRangeClamped 与实现自相矛盾，以测试为准修复（终点越界=选到内容末尾）；规格 5.5 已补裁定细目（ab5e9a6）。
- T4：步骤 0 核实项结论①不成立——Contour 列变 reflow 把 stableFloor 顶到旧 base（行身份重建副产）；落地为 noteFloor/reanchorFloor 双入口，规格 5.6 退化路径兑现。
- T5：testResizeReflowKeepsSelection 原文 resize(4,3) 断言与 M4 硬行截断语义冲突，改为 resize(8,3)（不触发截断的最大缩列幅度，双向 reflow 覆盖保持）。
- T5 顺带修复：native 1049 切换补 activeBufferChanged 置位（前序遗漏，native 此前从不置位）；test_backend_interface FakeBackend 补 lineSource() override。
- T1 遗留待 T6 修复：Types.h:52 ZzLogicalPos::operator== 缺 doxygen 注释（阻塞 doxygen 全量验证）。

- **规格覆盖度**：§3.1 全项有对应任务——ZzLogicalPos/锚定不变量（T1/T5）、统一视图（T2 接口 + T3/T4 实现；接缝规则 T2 单测 + T6 compat）、ZzSelection（T1）、提取四规则（T2）、facade API（T5）、Contour 历史只读口与 stableFloor（T4）、M4 观察项①收口（T2 testSeamStitchHistoryToScreen + T6 用例 4）、测试矩阵（T1-T6）、文档（T6）。§5.6 计划阶段核实项 → T4 步骤 0。
- **占位符扫描**：T4 步骤 3 的 SoA 字段名与 T4 步骤 1 的 events stub 标注了"以实际声明为准"——这是上游 API 适配的固有不确定性（third_party 代码本地已改造），每个标注点都给出了确切的核实文件路径（Line.hpp/CellProxy.hpp/ZzContourEvents.h），不构成占位符。
- **类型一致性**：ZzIPhysicalLineSource 六方法在 T2 定义、T3/T4 实现、T5 消费一致；ZzSelection 公开方法（set/extend/clear/empty/range/onLinesDropped/clampTo）T1 定义、T5 使用一致；facade 六 API（setSelection/extendSelection/clearSelection/hasSelection/selectionRange/selectedText）T5 声明/实现/测试一致；ZzContourBackend 新口（historyLineSnapshot/screenLineSnapshot/historyLineWrapped/stableFloor）T4 声明、实现、ZzContourLineSource 消费一致。
