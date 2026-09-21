# M5b Search / Highlight 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 交付子串搜索：zzSearchLines 引擎（纯函数、单扫描、位置回映）、Core 持有的 match 列表（ZzSearchState，与选区同套锚定维护）、facade 搜索四 API + match 坐标查询（前端画高亮）。

**架构：** 引擎与状态均为后端无关纯逻辑，放 src/terminal/，复用 M5a 的 ZzIPhysicalLineSource 双后端数据源；facade 在 M5a 已有的 feed/resize 锚点维护切面上同步维护 search state（丢弃平移、Alternate 清空、reflow clamp）。引擎按链组建逻辑行文本时遵循 M5a 提取同一规则，保证命中坐标与 selectedText 自洽。

**技术栈：** C++20、CMake（src/terminal 与 tests/unit 走 GLOB+CONFIGURE_DEPENDS 自动收编；compat 测试需手工剔除+条件注册）、自带 ZZ_TEST_EXPECT 风格宏测试、doxygen 零警告门。

**规格：** docs/superpowers/specs/2026-09-21-m5b-search-highlight-design.md（commit 29814b5）。

**命令约定（全计划通用）：**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
# OFF：cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
# shared：cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
# 文档：doxygen Doxyfile（必须在仓库根运行，exit 0 且零警告）
```

doxygen 陷阱（公开头与 docs 注释）：行内 code span 内禁尖括号、内容禁以点开头、后禁紧跟顿号、禁 `#` 预处理词、正文与行内代码禁反斜杠转义（写"换行符"不写转义序列）。代码围栏内不受限。

## 文件结构

| 文件 | 职责 | 任务 |
|---|---|---|
| include/ZzTerm/Types.h | 新增 ZzLogicalRange（半开区间）、ZzSearchOptions（caseSensitive） | T1 |
| src/terminal/ZzSearchState.h/.cpp | Core 持有的搜索状态：pattern/options/matches + 平移/clamp/clear | T1 |
| src/terminal/ZzUtf8Encode.h | 内部 UTF-8 编码助手（从 ZzSelectionText.cpp 抽出共享，DRY） | T2 |
| src/terminal/ZzSearch.h/.cpp | 引擎 zzSearchLines 纯函数：单扫描、位置回映、子串匹配 | T2 |
| include/ZzTerm/Terminal.h + src/terminal/Terminal.cpp | facade 四 API + feed/resize 维护切面扩展 | T3 |
| tests/unit/test_search_state.cpp | ZzSearchState 模型单测 | T1 |
| tests/unit/test_search.cpp | 引擎矩阵单测（FakeSource 仿 M5a 基建） | T2 |
| tests/unit/test_terminal_search.cpp | facade 集成：保持/平移/reflow/Alternate/替换 | T3 |
| tests/unit/test_search_compat.cpp | 双后端 match 列表逐一相等（剔除 GLOB+条件注册） | T4 |
| tests/unit/test_perf_search.cpp | 10 万行搜索 benchmark 门控 + JSON 落盘 | T4 |
| tests/perf/records/2026-09-21-m5b-search.json | benchmark 基线入库 | T4 |
| docs/Architecture.md、docs/API.md、docs/VT-Xterm-Checklist.md、docs/VT-Xterm-Checklist-v2.md | §19/§504-505/§13 收口、搜索小节、Search 章勾选 | T4 |

---

### 任务 1：ZzLogicalRange/ZzSearchOptions + ZzSearchState 搜索状态模型

**文件：**
- 修改：`include/ZzTerm/Types.h`（ZzLogicalPos 之后追加两个类型）
- 创建：`src/terminal/ZzSearchState.h`、`src/terminal/ZzSearchState.cpp`
- 测试：`tests/unit/test_search_state.cpp`
- 构建：`tests/CMakeLists.txt`（shared target_sources 块追加）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_search_state.cpp`：

```cpp
// ZzSearchState 搜索状态模型单测（M5b）：持有/查询、丢弃平移、clamp、clear。
#include "../../src/terminal/ZzSearchState.h"

#include <cstdio>
#include <string>
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

ZzSearchState makeState()
{
    ZzSearchState st;
    st.set("needle", ZzSearchOptions{},
           {ZzLogicalRange{{2, 1}, {2, 5}},
            ZzLogicalRange{{10, 0}, {10, 6}},
            ZzLogicalRange{{20, 3}, {20, 8}}});
    return st;
}

} // namespace

static void testTypesDefaults()
{
    ZzSearchOptions opt;
    ZZ_TEST_EXPECT(opt.caseSensitive);
    ZzLogicalRange a{{1, 2}, {3, 4}};
    ZzLogicalRange b{{1, 2}, {3, 4}};
    ZZ_TEST_EXPECT(a == b);
    b.end.col = 9;
    ZZ_TEST_EXPECT(!(a == b));
}

static void testSetAndQuery()
{
    ZzSearchState st = makeState();
    ZZ_TEST_EXPECT(st.matchCount() == 3);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.match(1, s, e));
    ZZ_TEST_EXPECT(s.line == 10 && s.col == 0 && e.line == 10 && e.col == 6);
    ZZ_TEST_EXPECT(!st.match(3, s, e)); // 越界
}

static void testClear()
{
    ZzSearchState st = makeState();
    st.clear();
    ZZ_TEST_EXPECT(st.matchCount() == 0);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(!st.match(0, s, e));
}

static void testOnLinesDroppedRemovesFullyDropped()
{
    ZzSearchState st = makeState();
    st.onLinesDropped(4); // {2,..} 两端 -2 全丢移除；{10,..}→6；{20,..}→16
    ZZ_TEST_EXPECT(st.matchCount() == 2);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.match(0, s, e));
    ZZ_TEST_EXPECT(s.line == 6 && e.line == 6);
    ZZ_TEST_EXPECT(st.match(1, s, e));
    ZZ_TEST_EXPECT(s.line == 16 && e.line == 16);
}

static void testOnLinesDroppedClampsSurvivingStart()
{
    ZzSearchState st;
    st.set("needle", ZzSearchOptions{}, {ZzLogicalRange{{2, 1}, {10, 5}}});
    st.onLinesDropped(4); // start -2 → clamp {0,0}；end 6 存活
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.matchCount() == 1);
    ZZ_TEST_EXPECT(st.match(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 0);
    ZZ_TEST_EXPECT(e.line == 6 && e.col == 5);
}

static void testClampToRemovesOutOfRangeStart()
{
    ZzSearchState st = makeState();
    st.clampTo(12); // {2,..} 存活；{10,..} 存活；{20,..} 起点越界移除
    ZZ_TEST_EXPECT(st.matchCount() == 2);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.match(1, s, e));
    ZZ_TEST_EXPECT(s.line == 10);
}

static void testClampToClampsEnd()
{
    ZzSearchState st;
    st.set("needle", ZzSearchOptions{}, {ZzLogicalRange{{2, 1}, {20, 5}}});
    st.clampTo(12); // start 2 存活；end 20 → clamp 到 11
    ZZ_TEST_EXPECT(st.matchCount() == 1);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(st.match(0, s, e));
    ZZ_TEST_EXPECT(e.line == 11);
}

int main()
{
    testTypesDefaults();
    testSetAndQuery();
    testClear();
    testOnLinesDroppedRemovesFullyDropped();
    testOnLinesDroppedClampsSurvivingStart();
    testClampToRemovesOutOfRangeStart();
    testClampToClampsEnd();
    if (g_failures == 0)
        std::printf("test_search_state: all passed\n");
    return g_failures;
}
```

tests/CMakeLists.txt 的 target_sources 块（test_native_linesource 块之后）追加：

```cmake
if(TARGET test_search_state)
    target_sources(test_search_state PRIVATE "${CMAKE_SOURCE_DIR}/src/terminal/ZzSearchState.cpp")
endif()
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug`
预期：编译失败，`ZzSearchState.h: No such file or directory`

- [ ] **步骤 3：实现类型与状态**

`include/ZzTerm/Types.h`：ZzLogicalPos 定义（:49-55 区域）之后追加（doxygen 中文注释，风格仿 ZzLogicalPos）：

```cpp
/**
 * @brief 逻辑行坐标区间（搜索 match，M5b）。
 *
 * 半开区间 [start, end)：start 为命中首格，end 为命中末格之后一格。
 * 坐标语义见 ZzLogicalPos；match 为搜索时刻的坐标快照，此后内容漂移不校验。
 */
struct ZzLogicalRange {
    ZzLogicalPos start; ///< 区间起点（含）
    ZzLogicalPos end;   ///< 区间终点（不含）
    friend constexpr bool operator==(ZzLogicalRange, ZzLogicalRange) noexcept = default;
};

/**
 * @brief 搜索选项（M5b）。
 */
struct ZzSearchOptions {
    bool caseSensitive = true; ///< true = 大小写敏感；false = ASCII 大小写折叠（Unicode 不折叠）
};
```

`src/terminal/ZzSearchState.h`：

```cpp
// ZzSearchState：Core 持有的搜索状态（M5b）。pattern/options/matches 快照 +
// 锚定维护（平移/clamp 与 ZzSelection 同一口径：物理计数平移逻辑序号，近似语义）。
#pragma once

#include <ZzTerm/Types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ZzSearchState {
public:
    void set(std::string pattern, ZzSearchOptions options, std::vector<ZzLogicalRange> matches);
    void clear() noexcept;
    [[nodiscard]] std::size_t matchCount() const noexcept;
    // 查询第 index 个 match（坐标升序）；越界返回 false（start/end 不写入）。
    [[nodiscard]] bool match(std::size_t index, ZzLogicalPos& start, ZzLogicalPos& end) const noexcept;
    // 历史头部丢弃 delta 个物理行后平移全部 match：两端全丢的移除，
    // 存活起点负值 clamp 到 {0,0}（语义同 ZzSelection::onLinesDropped）。
    void onLinesDropped(std::uint64_t delta) noexcept;
    // line clamp 到 [0, lineCount)：起点越界的 match 移除，终点 clamp
    //（语义同 ZzSelection::clampTo；col 由提取/查询层按行 clamp）。
    void clampTo(std::int64_t lineCount) noexcept;

private:
    std::string pattern_;
    ZzSearchOptions options_{};
    std::vector<ZzLogicalRange> matches_; // 坐标升序（引擎扫描序天然有序）
};
```

`src/terminal/ZzSearchState.cpp`：

```cpp
#include "ZzSearchState.h"

#include <utility>

void ZzSearchState::set(std::string pattern, ZzSearchOptions options, std::vector<ZzLogicalRange> matches)
{
    pattern_ = std::move(pattern);
    options_ = options;
    matches_ = std::move(matches);
}

void ZzSearchState::clear() noexcept
{
    pattern_.clear();
    options_ = {};
    matches_.clear();
}

std::size_t ZzSearchState::matchCount() const noexcept
{
    return matches_.size();
}

bool ZzSearchState::match(std::size_t index, ZzLogicalPos& start, ZzLogicalPos& end) const noexcept
{
    if (index >= matches_.size())
        return false;
    start = matches_[index].start;
    end = matches_[index].end;
    return true;
}

void ZzSearchState::onLinesDropped(std::uint64_t delta) noexcept
{
    const auto shift = static_cast<std::int64_t>(delta);
    std::size_t w = 0;
    for (std::size_t i = 0; i < matches_.size(); ++i) {
        ZzLogicalRange m = matches_[i];
        m.end.line -= shift;
        if (m.end.line < 0)
            continue; // 两端全丢：移除
        m.start.line -= shift;
        if (m.start.line < 0) {
            m.start.line = 0;
            m.start.col = 0;
        }
        matches_[w++] = m;
    }
    matches_.resize(w);
}

void ZzSearchState::clampTo(std::int64_t lineCount) noexcept
{
    std::size_t w = 0;
    for (std::size_t i = 0; i < matches_.size(); ++i) {
        ZzLogicalRange m = matches_[i];
        if (m.start.line >= lineCount)
            continue; // 起点越界：无可锚内容，移除
        if (m.end.line >= lineCount)
            m.end.line = lineCount - 1;
        matches_[w++] = m;
    }
    matches_.resize(w);
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_search_state`
预期：PASS，输出 `test_search_state: all passed`

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Types.h src/terminal/ZzSearchState.h src/terminal/ZzSearchState.cpp tests/unit/test_search_state.cpp tests/CMakeLists.txt
git commit -m "feat(search): ZzLogicalRange/ZzSearchOptions 与 ZzSearchState 状态模型（M5b T1）"
```

---

### 任务 2：搜索引擎 zzSearchLines（单扫描 + 位置回映）

**文件：**
- 创建：`src/terminal/ZzUtf8Encode.h`（从 ZzSelectionText.cpp 抽出的共享编码助手）
- 修改：`src/terminal/ZzSelectionText.cpp`（appendCodePoint 改为引用共享头，函数体删除）
- 创建：`src/terminal/ZzSearch.h`、`src/terminal/ZzSearch.cpp`
- 测试：`tests/unit/test_search.cpp`
- 构建：`tests/CMakeLists.txt`（shared target_sources 块追加）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_search.cpp`（FakeSource 与 makeLine 仿 tests/unit/test_selection_text.cpp 的既有基建）：

```cpp
// zzSearchLines 引擎矩阵（M5b）：子串多命中、大小写两态、宽字符/cluster
// 位置回映、软换行链内命中、跨逻辑行不命中、空 pattern、selectedText 自洽。
#include "../../src/backend/ZzLineSource.h"
#include "../../src/terminal/ZzSearch.h"
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

void expectMatch(const ZzLogicalRange& m, std::int64_t line, std::int32_t cs, std::int32_t ce)
{
    ZZ_TEST_EXPECT(m.start.line == line && m.start.col == cs);
    ZZ_TEST_EXPECT(m.end.line == line && m.end.col == ce);
}

// 命中坐标经 zzExtractSelectionText 提取必须等于 pattern（自洽不变量，大小写敏感）。
void expectSelfConsistent(const ZzIPhysicalLineSource& src, const std::vector<ZzLogicalRange>& matches,
                          std::string_view pattern)
{
    for (const ZzLogicalRange& m : matches) {
        const std::string text = zzExtractSelectionText(src, m.start, m.end);
        ZZ_TEST_EXPECT(text == pattern);
    }
}

} // namespace

static void testSingleMatch()
{
    FakeSource src(16, 0, {makeLine(16, "hello world", false)});
    const auto m = zzSearchLines(src, "world", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 6, 11);
    expectSelfConsistent(src, m, "world");
}

static void testMultipleMatchesNonOverlapping()
{
    FakeSource src(16, 0, {makeLine(16, "ababab", false)});
    const auto m = zzSearchLines(src, "abab", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1); // 命中后从 match 末尾继续：0-4 后 "ab" 不再命中
    expectMatch(m[0], 0, 0, 4);
}

static void testCaseInsensitiveAscii()
{
    FakeSource src(16, 0, {makeLine(16, "hello World", false)});
    ZzSearchOptions insensitive;
    insensitive.caseSensitive = false;
    const auto m = zzSearchLines(src, "world", insensitive);
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 6, 11);
    const auto none = zzSearchLines(src, "world", ZzSearchOptions{});
    ZZ_TEST_EXPECT(none.empty()); // 敏感模式不命中
}

static void testWideCharRemap()
{
    // 格序列：x 界(lead+续) y → "界" 命中应占格 1..3
    ZzLine line;
    line.resize(8);
    line.setCell(0, narrowCell('x'));
    ZzCell lead;
    lead.setWidth(ZzCellWidth::WideLead);
    lead.setCodePoint(U'界');
    line.setCell(1, lead);
    ZzCell cont;
    cont.setWidth(ZzCellWidth::WideContinuation);
    line.setCell(2, cont);
    line.setCell(3, narrowCell('y'));
    FakeSource src(8, 0, {std::move(line)});
    const auto m = zzSearchLines(src, "界", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 1, 3); // 宽字符完整覆盖，不拆半字
    expectSelfConsistent(src, m, "界");
    const auto my = zzSearchLines(src, "y", ZzSearchOptions{});
    ZZ_TEST_EXPECT(my.size() == 1);
    expectMatch(my[0], 0, 3, 4);
}

static void testClusterRemap()
{
    ZzLine line;
    line.resize(8);
    line.setCell(0, narrowCell('z'));
    ZzCell cluster;
    cluster.setWidth(ZzCellWidth::Narrow);
    cluster.setCluster(line.internCluster("a\u0301")); // a + 组合重音符，占 1 格
    line.setCell(1, cluster);
    FakeSource src(8, 0, {std::move(line)});
    const auto m = zzSearchLines(src, "a\u0301", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 1, 2);
    expectSelfConsistent(src, m, "a\u0301");
}

static void testMatchInsideSoftWrapChain()
{
    // "hello"+"world" 一条逻辑行；跨物理行边界的 "owo" 命中
    FakeSource src(5, 0, {makeLine(5, "hello", true), makeLine(5, "world", false)});
    const auto m = zzSearchLines(src, "owo", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 4, 7);
    expectSelfConsistent(src, m, "owo");
}

static void testNoCrossLogicalLineMatch()
{
    FakeSource src(8, 0, {makeLine(8, "ab", false), makeLine(8, "cd", false)});
    ZZ_TEST_EXPECT(zzSearchLines(src, "bc", ZzSearchOptions{}).empty());
}

static void testPatternWithNewlineNeverMatches()
{
    FakeSource src(8, 0, {makeLine(8, "ab", false), makeLine(8, "cd", false)});
    ZZ_TEST_EXPECT(zzSearchLines(src, "ab\ncd", ZzSearchOptions{}).empty());
}

static void testSeamChainMatch()
{
    // 历史末行 wrapped 续到屏幕首行（接缝链）：跨域命中
    FakeSource src(5, 1, {makeLine(5, "hello", true), makeLine(5, "world", false)});
    const auto m = zzSearchLines(src, "lowo", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 3, 7);
}

static void testEmptyPatternReturnsEmpty()
{
    FakeSource src(8, 0, {makeLine(8, "hello", false)});
    ZZ_TEST_EXPECT(zzSearchLines(src, "", ZzSearchOptions{}).empty());
}

static void testMatchesAcrossLogicalLinesSorted()
{
    FakeSource src(8, 0, {makeLine(8, "xax", false), makeLine(8, "yay", false)});
    const auto m = zzSearchLines(src, "a", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 2);
    expectMatch(m[0], 0, 1, 2);
    expectMatch(m[1], 1, 1, 2);
}

int main()
{
    testSingleMatch();
    testMultipleMatchesNonOverlapping();
    testCaseInsensitiveAscii();
    testWideCharRemap();
    testClusterRemap();
    testMatchInsideSoftWrapChain();
    testNoCrossLogicalLineMatch();
    testPatternWithNewlineNeverMatches();
    testSeamChainMatch();
    testEmptyPatternReturnsEmpty();
    testMatchesAcrossLogicalLinesSorted();
    if (g_failures == 0)
        std::printf("test_search: all passed\n");
    return g_failures;
}
```

tests/CMakeLists.txt 的 target_sources 块追加：

```cmake
if(TARGET test_search)
    target_sources(test_search PRIVATE
        "${CMAKE_SOURCE_DIR}/src/terminal/ZzSearch.cpp"
        "${CMAKE_SOURCE_DIR}/src/terminal/ZzSelectionText.cpp")
endif()
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：编译失败，`ZzSearch.h: No such file or directory`

- [ ] **步骤 3：抽出共享编码头并实现引擎**

`src/terminal/ZzUtf8Encode.h`（把 ZzSelectionText.cpp 的 appendCodePoint 逻辑移入；ZzSelectionText.cpp 删除其匿名 namespace 内的 appendCodePoint 定义并改为 include 本头调用 zzEncodeUtf8——行为不变，该文件测试 test_selection_text 必须继续全绿）：

```cpp
// zzEncodeUtf8：码点 → UTF-8 编码（src/terminal 内部共享，M5b 自
// ZzSelectionText.cpp 抽出；ZzSearch/ZzSelectionText 共用）。
#pragma once

#include <cstddef>
#include <string>

// 把 cp 编码进 buf（至多 4 字节），返回字节数。调用方保证 cp 为合法标量值。
inline int zzEncodeUtf8(char (&buf)[4], char32_t cp)
{
    if (cp < 0x80) {
        buf[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = static_cast<char>(0xC0 | (cp >> 6));
        buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = static_cast<char>(0xE0 | (cp >> 12));
        buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    buf[0] = static_cast<char>(0xF0 | (cp >> 18));
    buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

// 便捷封装：编码并追加到 out。
inline void zzAppendCodePoint(std::string& out, char32_t cp)
{
    char buf[4];
    const int n = zzEncodeUtf8(buf, cp);
    out.append(buf, static_cast<std::size_t>(n));
}
```

`src/terminal/ZzSearch.h`：

```cpp
// zzSearchLines：子串搜索引擎（M5b）。纯函数，双后端共用。
// 单次物理行扫描，按 wrapped 链逐条组建逻辑行文本与位置回映表，
// 每时刻只持有一条逻辑行的数据——不拼全量大串（Architecture §13）。
#pragma once

#include <ZzTerm/Types.h>

#include <string_view>
#include <vector>

class ZzIPhysicalLineSource;

// 在统一空间内搜索子串，返回全部命中（坐标升序，半开区间）。
// 规则（规格 5.2）：不跨逻辑行匹配（pattern 含换行符永不命中）；命中不重叠
// （命中后从 match 末尾继续）；空 pattern 返回空列表；caseSensitive=false 时
// 按 ASCII 大小写折叠（Unicode 不折叠，v1 钉死）。命中坐标经回映表换算，
// 宽字符完整覆盖不拆半字。
[[nodiscard]] std::vector<ZzLogicalRange> zzSearchLines(const ZzIPhysicalLineSource& src,
                                                        std::string_view pattern,
                                                        ZzSearchOptions options);
```

`src/terminal/ZzSearch.cpp`：

```cpp
#include "ZzSearch.h"

#include "../backend/ZzLineSource.h"
#include "ZzUtf8Encode.h"

#include <ZzTerm/Cell.h>

#include <string>

namespace {

char foldByte(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

std::string foldString(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
        out.push_back(foldByte(c));
    return out;
}

struct LineTextMap {
    std::string text;                      // 行尾空白已修剪
    std::string folded;                    // ASCII 折叠副本（仅不敏感模式构建）
    std::vector<std::int32_t> byteToCell;  // text.size()+1 项：字节位置 → 格偏移
};

// 组建一条逻辑行的纯文本与位置回映表（规则同 ZzSelectionText 提取：
// 宽字符整字续格跳过、cluster 整串、空单元格输出空格、行尾空白修剪）。
LineTextMap buildLineText(const ZzIPhysicalLineSource& src,
                          std::size_t firstRow, std::size_t rowCount, bool needFolded)
{
    const int cols = src.cols();
    LineTextMap m;
    std::int32_t cell = 0;
    auto emit = [&](std::string_view bytes, std::int32_t cellCount) {
        for (char c : bytes) {
            m.text.push_back(c);
            m.byteToCell.push_back(cell);
            if (needFolded)
                m.folded.push_back(foldByte(c));
        }
        cell += cellCount;
    };
    for (std::size_t r = 0; r < rowCount; ++r) {
        const ZzLine line = src.lineAt(firstRow + r);
        for (int c = 0; c < cols; ++c) {
            const ZzCell& zc = line.cellAt(c);
            switch (zc.width()) {
            case ZzCellWidth::WideContinuation:
                continue; // 续格无文本（lead 已取整字）
            case ZzCellWidth::Empty:
                emit(" ", 1); // 行内空白占位；行尾统一修剪
                continue;
            default:
                break;
            }
            if (zc.isCluster()) {
                emit(line.clusterText(zc.clusterIndex()), 1);
            } else if (zc.codePoint() != 0) {
                char buf[4];
                const int n = zzEncodeUtf8(buf, zc.codePoint());
                emit(std::string_view(buf, static_cast<std::size_t>(n)),
                     zc.width() == ZzCellWidth::WideLead ? 2 : 1);
            } else {
                emit(" ", 1);
            }
        }
    }
    // 行尾空白修剪（text/folded/回映表同步截断）
    while (!m.text.empty() && m.text.back() == ' ') {
        m.text.pop_back();
        m.byteToCell.pop_back();
        if (needFolded)
            m.folded.pop_back();
    }
    m.byteToCell.push_back(cell); // 末尾哨兵：text.size() → 行总长（格）
    return m;
}

} // namespace

std::vector<ZzLogicalRange> zzSearchLines(const ZzIPhysicalLineSource& src,
                                          std::string_view pattern,
                                          ZzSearchOptions options)
{
    std::vector<ZzLogicalRange> out;
    if (pattern.empty() || pattern.find('\n') != std::string_view::npos)
        return out; // 空 pattern；不跨逻辑行匹配（v1 钉死）

    std::string foldedPattern;
    if (!options.caseSensitive)
        foldedPattern = foldString(pattern);
    const std::string_view needle = options.caseSensitive ? pattern : std::string_view(foldedPattern);

    const std::size_t total = src.historyLineCount() + static_cast<std::size_t>(src.screenRowCount());
    std::int64_t logicalLine = 0;
    std::size_t row = 0;
    while (row < total) {
        std::size_t count = 1;
        while (row + count < total && src.lineWrapped(row + count - 1))
            ++count;
        LineTextMap m = buildLineText(src, row, count, !options.caseSensitive);
        const std::string& hay = options.caseSensitive ? m.text : m.folded;
        std::size_t pos = 0;
        while ((pos = hay.find(needle, pos)) != std::string::npos) {
            const auto cellStart = m.byteToCell[pos];
            const auto cellEnd = m.byteToCell[pos + needle.size()];
            out.push_back(ZzLogicalRange{{logicalLine, cellStart}, {logicalLine, cellEnd}});
            pos += needle.size(); // 命中不重叠：从 match 末尾继续
        }
        row += count;
        ++logicalLine;
    }
    return out;
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_search && ./build/linux-gcc-debug/tests/test_selection_text`
预期：两个都 PASS（test_selection_text 必须继续全绿——共享头抽取的行为不变验证）

- [ ] **步骤 5：Commit**

```bash
git add src/terminal/ZzUtf8Encode.h src/terminal/ZzSelectionText.cpp src/terminal/ZzSearch.h src/terminal/ZzSearch.cpp tests/unit/test_search.cpp tests/CMakeLists.txt
git commit -m "feat(search): zzSearchLines 引擎——单扫描+位置回映子串匹配（M5b T2）"
```

---

（任务 3-4 见下文续写）

### 任务 3：facade 搜索 API + 锚点维护切面扩展

**文件：**
- 修改：`include/ZzTerm/Terminal.h`（selectedText 之后新增四个搜索 API 声明）
- 修改：`src/terminal/Terminal.cpp`（Impl 持有 ZzSearchState；noteSelectionAfterFeed 与 resize 包裹扩展；API 实现）
- 测试：`tests/unit/test_terminal_search.cpp`（GLOB 自动收编，仅链 ZzTermCore）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_terminal_search.cpp`（全部走公开 facade API；feed 辅助与 test_terminal_selection.cpp 同款）：

```cpp
// facade 搜索集成测试（M5b）：search/match 查询、feed 坐标稳定、丢弃平移、
// reflow 保持、Alternate 清空、重复搜索替换、大小写选项。
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

static void testBasicSearch()
{
    ZzTerminal term(16, 3, ZzBackendKind::Native, 100);
    feed(term, "hello world");
    ZZ_TEST_EXPECT(term.search("world") == 1);
    ZZ_TEST_EXPECT(term.searchMatchCount() == 1);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 6 && e.line == 0 && e.col == 11);
    ZZ_TEST_EXPECT(!term.searchMatch(1, s, e));
    term.clearSearch();
    ZZ_TEST_EXPECT(term.searchMatchCount() == 0);
}

static void testCaseOption()
{
    ZzTerminal term(16, 3, ZzBackendKind::Native, 100);
    feed(term, "hello World");
    ZZ_TEST_EXPECT(term.search("world") == 0); // 默认敏感
    ZzSearchOptions insensitive;
    insensitive.caseSensitive = false;
    ZZ_TEST_EXPECT(term.search("world", insensitive) == 1);
}

static void testReSearchReplaces()
{
    ZzTerminal term(16, 3, ZzBackendKind::Native, 100);
    feed(term, "hello world");
    ZZ_TEST_EXPECT(term.search("hello") == 1);
    ZZ_TEST_EXPECT(term.search("world") == 1); // 替换旧状态
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.col == 6); // 是 world 不是 hello
    ZZ_TEST_EXPECT(term.search("") == 0); // 空 pattern 清空
    ZZ_TEST_EXPECT(term.searchMatchCount() == 0);
}

static void testFeedKeepsMatchCoords()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 100);
    feed(term, "aaaa\r\nbbbb\r\ncccc"); // aaaa/bbbb 入历史
    ZZ_TEST_EXPECT(term.search("aaaa") == 1);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 0 && e.col == 4);
    feed(term, "dddd\r\neeee\r\nffff\r\ngggg"); // 新内容不触发重搜，旧坐标稳定
    ZZ_TEST_EXPECT(term.searchMatchCount() == 1);
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && e.col == 4);
    // 坐标仍指向 "aaaa"：经选区提取自洽验证
    term.setSelection(s, e);
    ZZ_TEST_EXPECT(term.selectedText() == "aaaa");
}

static void testDroppedShiftsMatches()
{
    ZzTerminal term(10, 2, ZzBackendKind::Native, 4); // 历史容量 4
    feed(term, "aaaaaaaaaa\r\n"); // 逻辑行 0 = aaaaaaaaaa，硬终结
    ZZ_TEST_EXPECT(term.search("aaaaaaaaaa") == 1);
    for (int i = 0; i < 6; ++i)
        feed(term, "x" + std::to_string(i) + "\r\n"); // 挤出容量，逻辑行 0 被丢弃
    ZZ_TEST_EXPECT(term.searchMatchCount() == 0); // 两端全丢的 match 已移除
}

static void testResizeReflowKeepsMatches()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feed(term, "0123456789abcde"); // 软链 15 格
    ZZ_TEST_EXPECT(term.search("789ab") == 1);
    ZzLogicalPos s, e;
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 7 && e.col == 12);
    term.resize(5, 3); // 软链重切为 3 物理行，逻辑行集合不变
    ZZ_TEST_EXPECT(term.searchMatchCount() == 1);
    ZZ_TEST_EXPECT(term.searchMatch(0, s, e));
    ZZ_TEST_EXPECT(s.line == 0 && s.col == 7 && e.col == 12);
    term.setSelection(s, e);
    ZZ_TEST_EXPECT(term.selectedText() == "789ab");
}

static void testAlternateSwitchClearsSearch()
{
    ZzTerminal term(16, 3, ZzBackendKind::Native, 100);
    feed(term, "hello world");
    ZZ_TEST_EXPECT(term.search("world") == 1);
    feed(term, "\x1b[?1049h");
    ZZ_TEST_EXPECT(term.searchMatchCount() == 0);
}

int main()
{
    testBasicSearch();
    testCaseOption();
    testReSearchReplaces();
    testFeedKeepsMatchCoords();
    testDroppedShiftsMatches();
    testResizeReflowKeepsMatches();
    testAlternateSwitchClearsSearch();
    if (g_failures == 0)
        std::printf("test_terminal_search: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：编译失败，`ZzTerminal` 无 `search` 成员

- [ ] **步骤 3：facade API 与维护切面扩展**

`include/ZzTerm/Terminal.h`：在 selectedText 声明（:259 区域）之后、"Core 内部访问"注释块之前新增（doxygen 中文注释；注意陷阱：写"换行符"不写转义序列，行内 code span 禁尖括号与顿号紧跟）：

```cpp
    // ---- 搜索（M5b；高亮渲染由前端用 match 坐标自行绘制） ----

    /**
     * @brief 执行子串搜索（替换旧搜索状态）。
     * @param pattern 搜索子串；空串清空搜索状态并返回 0。
     * @param options 搜索选项（大小写敏感见 ZzSearchOptions）。
     * @return 匹配数。
     * @note 不跨逻辑行匹配：pattern 含换行符时永不命中；命中不重叠。
     *       match 为搜索时刻的坐标快照，此后 feed 改写的同坐标内容不校验；
     *       新内容不触发自动重搜，重搜时机由前端决定。
     */
    std::size_t search(std::string_view pattern, ZzSearchOptions options = {});

    /// @brief 清空搜索状态。
    void clearSearch() noexcept;

    /// @brief 当前搜索的 match 总数（无搜索状态为 0）。
    [[nodiscard]] std::size_t searchMatchCount() const noexcept;

    /**
     * @brief 查询第 index 个 match 的坐标区间（半开区间，坐标升序）。
     * @param index match 序号（0 起）。
     * @param start 输出：区间起点。
     * @param end 输出：区间终点。
     * @return false = index 越界或无搜索状态（start/end 不写入）。
     * @note 历史头部丢弃时 Core 自动平移 match（与选区同一近似口径）；
     *       reflow 后 match 保持；切换 Alternate 屏时搜索状态清空。
     */
    bool searchMatch(std::size_t index, ZzLogicalPos& start, ZzLogicalPos& end) const;
```

`src/terminal/Terminal.cpp`：

- include 区加 `"ZzSearch.h"`、`"ZzSearchState.h"`；
- `Impl` 在 `selection` 成员后加 `ZzSearchState searchState;`；`noteSelectionAfterFeed` 改为同时维护搜索状态（注释更新为"维护选区与搜索锚点"）：

```cpp
    // feed/resize 后维护选区与搜索锚点（规格 M5a 5.1 / M5b 5.4：
    // Alternate 切换清空；丢弃按物理计数平移，近似语义）
    void noteSelectionAfterFeed(const ZzTermChanges& changes)
    {
        const std::uint64_t dropped = backend->lineSource().droppedLineCount();
        if (changes.activeBufferChanged) {
            selection.clear();
            searchState.clear();
        } else if (dropped > lastDropped) {
            selection.onLinesDropped(dropped - lastDropped);
            searchState.onLinesDropped(dropped - lastDropped);
        }
        lastDropped = dropped;
    }
```

- resize 包裹：zzLogicalLineCount 提取为局部变量复用，两个 clampTo 都用它：

```cpp
bool ZzTerminal::resize(int cols, int rows)
{
    const bool changed = impl_->backend->resize(cols, rows);
    if (changed) {
        // 隐含前提：后端 resize 永不置 activeBufferChanged；若未来违反，
        // 语义仍安全（走 noteSelectionAfterFeed 的清空分支）。
        impl_->noteSelectionAfterFeed(ZzTermChanges{}); // resize 也可能丢弃（reflow 裁剪）
        const std::int64_t count = zzLogicalLineCount(impl_->backend->lineSource());
        impl_->selection.clampTo(count);
        impl_->searchState.clampTo(count);
    }
    return changed;
}
```

- 文件末尾新增四个 API 实现：

```cpp
std::size_t ZzTerminal::search(std::string_view pattern, ZzSearchOptions options)
{
    if (pattern.empty()) {
        impl_->searchState.clear();
        return 0;
    }
    auto matches = zzSearchLines(impl_->backend->lineSource(), pattern, options);
    impl_->searchState.set(std::string(pattern), options, std::move(matches));
    return impl_->searchState.matchCount();
}

void ZzTerminal::clearSearch() noexcept
{
    impl_->searchState.clear();
}

std::size_t ZzTerminal::searchMatchCount() const noexcept
{
    return impl_->searchState.matchCount();
}

bool ZzTerminal::searchMatch(std::size_t index, ZzLogicalPos& start, ZzLogicalPos& end) const
{
    return impl_->searchState.match(index, start, end);
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿（39 基线 + test_search_state + test_search + test_terminal_search = 42），含 `test_terminal_search: all passed`

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Terminal.h src/terminal/Terminal.cpp tests/unit/test_terminal_search.cpp
git commit -m "feat(search): facade 搜索 API 与锚点维护切面扩展（M5b T3）"
```

---

### 任务 4：compat + benchmark + 文档收口 + 全回归

**文件：**
- 测试：`tests/unit/test_search_compat.cpp`、`tests/unit/test_perf_search.cpp`
- 构建：`tests/CMakeLists.txt`（compat 剔除 GLOB + 条件注册）
- 记录：`tests/perf/records/2026-09-21-m5b-search.json`
- 文档：`docs/Architecture.md`、`docs/API.md`、`docs/VT-Xterm-Checklist.md`、`docs/VT-Xterm-Checklist-v2.md`

- [ ] **步骤 1：compat 测试并运行观察**

创建 `tests/unit/test_search_compat.cpp`（Dual 结构仿 test_selection_compat.cpp；比对 match 列表逐一相等）：

```cpp
// 双后端搜索 compat（M5b）：同一 VT 脚本喂 Native/Contour，search 的
// match 列表逐一相等（native 为基准）。分歧按 b 类分别断言 + 注释钉住。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <string>
#include <vector>

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

std::vector<ZzLogicalRange> allMatches(ZzTerminal& t)
{
    std::vector<ZzLogicalRange> out;
    ZzLogicalPos s, e;
    for (std::size_t i = 0; t.searchMatch(i, s, e); ++i)
        out.push_back(ZzLogicalRange{s, e});
    return out;
}

void checkSearchEqual(Dual& d, std::string_view pattern, ZzSearchOptions options, const char* what)
{
    const std::size_t a = d.native.search(pattern, options);
    const std::size_t b = d.contour.search(pattern, options);
    if (a != b)
        std::fprintf(stderr, "  count mismatch [%s]: native=%zu contour=%zu\n", what, a, b);
    ZZ_CHECK(a == b);
    ZZ_CHECK(allMatches(d.native) == allMatches(d.contour));
}

} // namespace

// 1. 屏幕区 ASCII
static void testScreenSearch()
{
    Dual d;
    d.feedBoth("hello world");
    checkSearchEqual(d, "world", ZzSearchOptions{}, "screen ascii");
}

// 2. 历史+屏幕统一空间多命中
static void testHistorySearch()
{
    Dual d;
    d.feedBoth("ab\r\nab\r\nab\r\nab\r\nab");
    checkSearchEqual(d, "ab", ZzSearchOptions{}, "history multi");
}

// 3. 软换行链内命中（跨物理行边界）
static void testSoftWrapChainSearch()
{
    Dual d;
    d.feedBoth("0123456789abcde");
    checkSearchEqual(d, "89ab", ZzSearchOptions{}, "softwrap chain");
}

// 4. 宽字符回映
static void testWideCharSearch()
{
    Dual d;
    d.feedBoth("ab界面cd界");
    checkSearchEqual(d, "界", ZzSearchOptions{}, "wide char");
}

// 5. 大小写不敏感
static void testCaseInsensitiveSearch()
{
    Dual d;
    d.feedBoth("hello World");
    ZzSearchOptions insensitive;
    insensitive.caseSensitive = false;
    checkSearchEqual(d, "world", insensitive, "case insensitive");
}

// 6. 零命中
static void testNoMatch()
{
    Dual d;
    d.feedBoth("hello");
    checkSearchEqual(d, "zzz", ZzSearchOptions{}, "no match");
}

int main()
{
    testScreenSearch();
    testHistorySearch();
    testSoftWrapChainSearch();
    testWideCharSearch();
    testCaseInsensitiveSearch();
    testNoMatch();
    if (g_failures == 0)
        std::printf("test_search_compat: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
```

tests/CMakeLists.txt：REMOVE_ITEM 列表追加 `"${CMAKE_CURRENT_SOURCE_DIR}/unit/test_search_compat.cpp"`；test_selection_compat 注册块后追加同形块：

```cmake
# M5b：双后端搜索 compat（同一脚本 match 列表逐一相等，native 为基准）。
# 只碰公开头，仅链接 ZzTermCore（同 test_backend_compat 的 shared 构建理由）。
if(TARGET ZzTermContourBackend)
    add_executable(test_search_compat unit/test_search_compat.cpp)
    target_link_libraries(test_search_compat PRIVATE ZzTermCore)
    add_test(NAME test_search_compat COMMAND test_search_compat)
endif()
```

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_search_compat`
预期：PASS。若出现双后端分歧：按 b 类惯例分别断言 + 注释钉住，并在 commit message 与 Architecture.md 记录。

- [ ] **步骤 2：benchmark 门控**

创建 `tests/unit/test_perf_search.cpp`（GLOB 自动收编、仅链 ZzTermCore、native facade 公开 API；JSON 落盘 cwd 仿 test_perf_scrollback.cpp）：

```cpp
// 搜索性能门控（M5b）：10 万行历史单次子串搜索耗时门控，JSON 落盘 cwd。
// 门控宽松（防回归绊线，非精确基准）；CI 机器慢 2-3 倍仍应通过。
#include <ZzTerm/Terminal.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

int main()
{
    ZzTerminal term(80, 24, ZzBackendKind::Native, 100000);
    // 构造 10 万行历史（每行约 30 字符，分块喂入）
    {
        std::string chunk;
        for (int i = 0; i < 100000; ++i) {
            chunk += "line " + std::to_string(i) + " payload text for search\r\n";
            if (chunk.size() >= 256 * 1024) {
                term.feed(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(chunk.data()), chunk.size()));
                chunk.clear();
            }
        }
        if (!chunk.empty())
            term.feed(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(chunk.data()), chunk.size()));
    }

    const auto t0 = std::chrono::steady_clock::now();
    const std::size_t matches = term.search("payload");
    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    ZZ_TEST_EXPECT(matches == 100000 || matches == 99976); // 每行一命中（历史容量裁剪 24 行屏幕占用）
    // 门控标定：实测后填写本机基线并注释（M4 惯例：500ms 保证 CI runner 余量）
    ZZ_TEST_EXPECT(ms < 500);
    std::printf("perf: search 100k lines %lldms, %zu matches\n", static_cast<long long>(ms), matches);

    std::ofstream js("zzterm-perf-search.json");
    js << "{\n"
       << "  \"date\": \"2026-09-21\",\n"
       << "  \"milestone\": \"m5b-search\",\n"
       << "  \"note\": \"-O0 debug, 100k lines x 30 cols, substring search\",\n"
       << "  \"searchMs\": " << ms << ",\n"
       << "  \"matches\": " << matches << "\n"
       << "}\n";
    if (g_failures == 0)
        std::printf("test_perf_search: all passed\n");
    return g_failures;
}
```

运行：`cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_perf_search`
预期：PASS；观察实测 ms。若实测超 150ms，把门控从 500 调到"实测×3 取整百"并在注释标定基线（M4 惯例）。然后把 JSON 拷贝入库：

```bash
cp build/linux-gcc-debug/tests/zzterm-perf-search.json tests/perf/records/2026-09-21-m5b-search.json
```

（matches 断言若因行数/容量语义与推演不符，以实测为准修正并注释——10 万行脚本在 24 行屏 + 10 万容量下的精确留存数。）

- [ ] **步骤 3：文档收口**

- `docs/Architecture.md`：§19 M5 里程碑行标注 M5b 完成（M5 整体收口）；§504-505 Search Match 保持条款标注 M5b 已落地（§504 的 Selection anchor 已标 M5a，本次补 Search Match）；§13 注记更新为 zzSearchLines 单扫描已落地。
- `docs/API.md`：在"选区与复制"小节后新增"搜索（M5b）"小节：四 API 用法、ZzSearchOptions、match 坐标快照语义与内容漂移钉注、前端职责（高亮绘制用 searchMatch 坐标、feed 后按需重搜）、三条钉注（不跨逻辑行、命中不重叠、ASCII 折叠）。
- `docs/VT-Xterm-Checklist.md` 与 `docs/VT-Xterm-Checklist-v2.md`：Search 相关项勾选 M5b 已覆盖部分（v2 的 Search Match Mapping after Reflow 现在勾；异步/正则等非目标项保持未勾）。

doxygen 陷阱复查（写"换行符"不写转义序列等）。

- [ ] **步骤 4：全回归门**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile  # 仓库根运行，exit 0 且零警告
```

预期：ON 44/44（42 + test_search_compat + test_perf_search）、OFF 33/33（compat 缺席为预期）、shared 44/44、doxygen 零警告。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_search_compat.cpp tests/unit/test_perf_search.cpp tests/CMakeLists.txt tests/perf/records/2026-09-21-m5b-search.json docs/Architecture.md docs/API.md docs/VT-Xterm-Checklist.md docs/VT-Xterm-Checklist-v2.md
git commit -m "test(search): 双后端搜索 compat、benchmark 门控与 M5b 文档收口（M5b T4）"
```

---

## 自检结论（计划编写后）

- **规格覆盖度**：§3.1 全项——公开类型（T1）、ZzSearchState（T1）、引擎含位置回映（T2）、facade 四 API 与维护切面（T3）、保持机制（T3 集成测试五件：feed 稳定/丢弃平移/reflow 保持/Alternate 清空/重复替换）、测试矩阵（T1-T4）、benchmark 门控与 JSON（T4）、文档（T4）。§5.4 内容漂移钉注 → T3 注释 + T4 API.md。
- **占位符扫描**：benchmark 门控阈值"实测后定"沿用 M4 既定程序（给了默认值 500ms 与调整规则）；matches 精确数给了实测修正指引——均为可执行指令而非占位符。无 TODO/待定。
- **类型一致性**：ZzLogicalRange/ZzSearchOptions（T1 定义，T2 引擎签名、T3 facade 签名、T4 compat 使用一致）；ZzSearchState 六方法（T1 定义/测试，T3 使用一致）；zzSearchLines 签名（T2 定义，T3 调用一致）；facade 四 API（T3 声明/实现/测试，T4 compat 使用一致）。
