# M17b 拼接行视图（不换行显示模式）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** Core 新增只读拼接行视图 `ZzUnwrapView`（折链拼回完整行 + 双向坐标换算），spike 演示层落地不换行显示模式（横向滚动 + 光标跟随 + 选区复制完整长行）。

**架构：** 纯视图层——`ZzUnwrapView` 以后端无关方式消费既有 `ZzRenderView` + `ZzHistoryView`，惰性拼接索引随双代计数失效重建；引擎写入/reflow/resize/搜索/选区零改动。演示层（ZzClawTerm/spike）用 `unwrapView()` 渲染 + QScrollBar 横向滚动，选区经 `fromStitched` 回映射喂引擎。

**技术栈：** C++20、CMake preset、ctest、Qt6 Widgets/Test（spike）。

---

## 文件结构

ZzTermCore（/home/zz/Jackfahdin/github/ZzTermCore，分支 contour）：

- 修改：`include/ZzTerm/Types.h`（新增 `ZzStitchedPos`）
- 创建：`include/ZzTerm/UnwrapView.h`（公共头：`ZzUnwrapView` / `ZzStitchedLineView`）
- 创建：`src/terminal/ZzUnwrapView.cpp`（实现，GLOB 自动收编）
- 修改：`include/ZzTerm/Terminal.h`（unwrapView 声明）、`src/terminal/Terminal.cpp`（Impl 成员 + 访问器）
- 创建：`tests/unit/test_unwrap_view.cpp`（8 用例，GLOB 收编）

ZzClawTerm（/home/zz/Jackfahdin/github/ZzClawTerm，就地分支提交）：

- 修改：`spike/ZzCoreViewWidget.h` / `spike/ZzCoreViewWidget.cpp`（不换行渲染 + 横向滚动 + 切换 + 选区）
- 修改：`spike/main.cpp`（横向 QScrollBar 装配，runLocal/runSsh 两处）
- 修改：`spike/tests/tst_spikerender.cpp`（不换行模式 QTest）

## 基线

- `ctest --preset linux-gcc-debug`（61 例）；m2-off-check（50）；m2-shared-check（61）；fuzz 3；doxygen 零警告
- 新增 test_unwrap_view 后各 +1（62/51/62）
- 新公共头/源文件由 GLOB + CONFIGURE_DEPENDS 收编，但各 build 目录须先重新 configure：
  `cmake --preset linux-gcc-debug`；m2 两目录手动
  `cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF` /
  `cmake -S . -B build/m2-shared-check -G Ninja -DBUILD_SHARED_LIBS=ON`

## 关键口径（规格勘误后，测试断言的依据）

- **裁尾拼接**：物理行接入链流前裁掉尾部无效格（视图层判据
  `text.empty()` 且 `width != WideContinuation`），与 Reflow.cpp:21-26
  逐行裁尾同口径。拼接行 cellCount = 各物理行有效段累加。
- **链旗标**：`wrapped()==true` 表示本行末尾软换行续接下一物理行
  （Line.h:105）。链 = 从链头起连续 wrapped 行 + 首个非 wrapped 行。
- **统一空间**：索引 u < historyCount 为历史行（historyView().lineAt(u)），
  否则屏幕行 u - historyCount（renderView().lineAt(...)）。
- **空拼接行**：裁尾后 cellCount=0，仍占一条拼接行。
- **fromStitched 钳位**：line 钳到 [0, lineCount-1]，col 钳到
  [0, cellCount-1]（空行钳到 (startLine, 0)）；toStitched 的
  pos.line 越界时钳到首/末拼接行。
- **noexcept 诚实**：会触发惰性重建的方法（lineCount/maxCellCount/
  toStitched/fromStitched/cellAt/cellCount）不标 noexcept（重建分配
  内存）；lineAt/sourceLine/sourceLineCount 标 noexcept。

---

### 任务 1：Core 拼接行视图（ZzTermCore）

**文件：**
- 修改：`include/ZzTerm/Types.h`（ZzLogicalRange 之后加 ZzStitchedPos）
- 创建：`include/ZzTerm/UnwrapView.h`
- 创建：`src/terminal/ZzUnwrapView.cpp`
- 修改：`include/ZzTerm/Terminal.h:147` 附近（unwrapView 声明）、`src/terminal/Terminal.cpp`（Impl 成员 + 访问器）
- 测试：`tests/unit/test_unwrap_view.cpp`

- [ ] **步骤 1：Types.h 加 ZzStitchedPos**

`include/ZzTerm/Types.h` 在 ZzLogicalRange 定义之后插入：

```cpp
/**
 * @brief 拼接坐标（M17b 拼接行视图）：拼接行索引 + 行内拼接列。
 *
 * 拼接行 = 折链（wrapped 链）拼回的完整行；坐标空间与 ZzLogicalPos
 * 的物理行空间经 ZzUnwrapView::toStitched/fromStitched 双向换算。
 */
struct ZzStitchedPos {
    std::int64_t line = 0; ///< 拼接行序号（0 起）
    std::int32_t col  = 0; ///< 拼接行内单元格偏移

    /// @brief 相等比较（拼接行序号与拼接列均相等）。
    friend constexpr bool operator==(ZzStitchedPos, ZzStitchedPos) noexcept = default;
};
```

- [ ] **步骤 2：编写失败的测试**

创建 `tests/unit/test_unwrap_view.cpp`（完整文件）：

```cpp
// ZzUnwrapView 拼接行视图测试（M17b）：拼接正确性、裁尾口径、跨缝链、
// EAW 宽字符、坐标映射往返、maxCellCount、Alternate、resize 稳定。
// 规格 docs/superpowers/specs/2026-10-10-m17b-unwrap-view-design.md。
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#include "ZzTerm/Terminal.h"
#include "ZzTerm/UnwrapView.h"

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

// 拼接行全文（cellAt 逐格拼接；续格 text 为空自然跳过）
static std::string stitchedText(const ZzUnwrapView& view, std::size_t index)
{
    const ZzStitchedLineView line = view.lineAt(index);
    std::string out;
    for (int c = 0; c < line.cellCount(); ++c)
        out += line.cellAt(c).text;
    return out;
}

// 1. 无折链：拼接行 = 物理行（空白行裁尾为 0 格但占位）
static void testNoWrapIdentity()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "a\r\nb\r\n"); // 屏幕 a,b,空,空；光标行 2
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(view.lineCount() == 4);
    ZZ_TEST_EXPECT(stitchedText(view, 0) == "a");
    ZZ_TEST_EXPECT(stitchedText(view, 1) == "b");
    ZZ_TEST_EXPECT(view.lineAt(2).cellCount() == 0); // 空拼接行裁尾为 0
    ZZ_TEST_EXPECT(view.lineAt(2).sourceLineCount() == 1);
    ZZ_TEST_EXPECT(view.maxCellCount() == 1);
}

// 2. 多行折链拼接：25 x 在 10 列下折 3 行 → 1 条拼接行
static void testStitchChain()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxxxxxxxxxxxx"); // 25 x：行0/1 满(w)，行2 五 x
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(view.lineCount() == 2); // 链 + 空行 3
    const ZzStitchedLineView line = view.lineAt(0);
    ZZ_TEST_EXPECT(line.cellCount() == 25);
    ZZ_TEST_EXPECT(line.sourceLine() == 0);
    ZZ_TEST_EXPECT(line.sourceLineCount() == 3);
    ZZ_TEST_EXPECT(stitchedText(view, 0) == std::string(25, 'x'));
    ZZ_TEST_EXPECT(view.maxCellCount() == 25);
}

// 3. 跨历史-屏幕缝的链照常拼接，sourceLine 指历史区链头
static void testStitchAcrossHistorySeam()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxx"); // 15 x：行0 十 x(w)，行1 五 x
    feedStr(term, "\r\n");
    feedStr(term, "b\r\n"); // 滚出 x 头入历史（跨缝 wrapped）
    // 统一空间：历史 [x*10(w)]；屏幕 [x*5, b, 空]
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(view.lineCount() == 3); // [x 链, b, 空]
    ZZ_TEST_EXPECT(stitchedText(view, 0) == std::string(15, 'x'));
    ZZ_TEST_EXPECT(view.lineAt(0).sourceLine() == 0);       // 链头在历史
    ZZ_TEST_EXPECT(view.lineAt(0).sourceLineCount() == 2);
    ZZ_TEST_EXPECT(stitchedText(view, 1) == "b");
}

// 4. EAW 宽字符跨缝：行尾放不下的宽字符整体折下行，拼接后两格完整
static void testStitchWideCharSeam()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "123456789\xE4\xB8\xAD"); // 9 数字 + 「中」：中折到行 1
    const ZzUnwrapView& view = term.unwrapView();
    const ZzStitchedLineView line = view.lineAt(0);
    // 行 0 裁尾去掉填充格（9 格有效），行 1「中」2 格 → 共 11
    ZZ_TEST_EXPECT(line.cellCount() == 11);
    ZZ_TEST_EXPECT(line.cellAt(9).text == "\xE4\xB8\xAD"); // 跨缝寻址
    ZZ_TEST_EXPECT(line.cellAt(10).text.empty());          // 宽字符续格
    ZZ_TEST_EXPECT(stitchedText(view, 0) == "123456789\xE4\xB8\xAD");
}

// 5. 坐标映射：toStitched/fromStitched 逐点取值 + 往返一致 + 钳位
static void testCoordinateMapping()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxxxxxxxxxxxx"); // 链：行0/1/2
    feedStr(term, "\r\nb");                     // 行 3：b
    const ZzUnwrapView& view = term.unwrapView();
    // toStitched：物理 → 拼接
    ZZ_TEST_EXPECT(view.toStitched(ZzLogicalPos{0, 3}) == ZzStitchedPos{0, 3});
    ZZ_TEST_EXPECT(view.toStitched(ZzLogicalPos{1, 4}) == ZzStitchedPos{0, 14});
    ZZ_TEST_EXPECT(view.toStitched(ZzLogicalPos{2, 4}) == ZzStitchedPos{0, 24});
    ZZ_TEST_EXPECT(view.toStitched(ZzLogicalPos{3, 0}) == ZzStitchedPos{1, 0});
    // fromStitched：拼接 → 物理
    ZZ_TEST_EXPECT(view.fromStitched(0, 14) == ZzLogicalPos{1, 4});
    ZZ_TEST_EXPECT(view.fromStitched(0, 24) == ZzLogicalPos{2, 4});
    ZZ_TEST_EXPECT(view.fromStitched(1, 0) == ZzLogicalPos{3, 0});
    // 钳位：col 超出行尾 → 链末行最后有效格
    ZZ_TEST_EXPECT(view.fromStitched(0, 100) == ZzLogicalPos{2, 4});
    // 往返一致
    ZZ_TEST_EXPECT(view.fromStitched(view.toStitched(ZzLogicalPos{1, 4}).line,
                                     view.toStitched(ZzLogicalPos{1, 4}).col)
                   == ZzLogicalPos{1, 4});
}

// 6. 空缓冲：maxCellCount=0，拼接行 = 物理空行
static void testEmptyBuffer()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(view.lineCount() == 4);
    ZZ_TEST_EXPECT(view.maxCellCount() == 0);
    ZZ_TEST_EXPECT(view.lineAt(0).cellCount() == 0);
}

// 7. Alternate 屏：链在屏幕内照常拼接，无历史
static void testAlternateScreen()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b[?1049h");             // 进 Alternate
    feedStr(term, "xxxxxxxxxxxxxxx");          // 15 x：行0(w)，行1 五 x
    const ZzUnwrapView& view = term.unwrapView();
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(view.lineCount() == 3); // [x 链, 空, 空]
    ZZ_TEST_EXPECT(view.lineAt(0).cellCount() == 15);
    ZZ_TEST_EXPECT(view.lineAt(0).sourceLineCount() == 2);
}

// 8. resize/reflow 后拼接行内容稳定（M16 联动回归）
static void testStitchStableAcrossResize()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedStr(term, "xxxxxxxxxxxxxxxxxxxxxxxxx"); // 25 x 链
    ZZ_TEST_EXPECT(stitchedText(term.unwrapView(), 0) == std::string(25, 'x'));
    term.resize(16, 4); // 扩列：16+9 两行链
    ZZ_TEST_EXPECT(stitchedText(term.unwrapView(), 0) == std::string(25, 'x'));
    term.resize(6, 4);  // 缩列：链跨历史-屏幕缝（6*5=5 行 > 4 行屏）
    ZZ_TEST_EXPECT(stitchedText(term.unwrapView(), 0) == std::string(25, 'x'));
    term.resize(10, 4); // 还原
    ZZ_TEST_EXPECT(stitchedText(term.unwrapView(), 0) == std::string(25, 'x'));
}

int main()
{
    testNoWrapIdentity();
    testStitchChain();
    testStitchAcrossHistorySeam();
    testStitchWideCharSeam();
    testCoordinateMapping();
    testEmptyBuffer();
    testAlternateScreen();
    testStitchStableAcrossResize();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

- [ ] **步骤 3：重新 configure + 构建确认编译失败**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug 2>&1 | tail -5
```

预期：编译失败——`ZzTerm/UnwrapView.h` 不存在、`unwrapView` 未定义。

- [ ] **步骤 4：创建公共头 include/ZzTerm/UnwrapView.h**

```cpp
#pragma once

/// \file
/// \brief 拼接行视图（M17b）：把折链（wrapped 链）拼回完整行的只读视图。
/// 纯视图层——引擎存储/reflow/resize/搜索/选区语义不变。视图借用其来源
/// RenderView/HistoryView（经 ZzTerminal::unwrapView 获取），须与 feed 同
/// 线程使用；索引惰性重建，双代计数变化后的首次查询自动跟随最新内容。
/// 非线程安全。

#include <ZzTerm/Export.h>
#include <ZzTerm/HistoryView.h>
#include <ZzTerm/RenderView.h>
#include <ZzTerm/Types.h>

#include <cstddef>
#include <cstdint>
#include <memory>

class ZzUnwrapView;

/// \brief 拼接行只读句柄（M17b）：值语义，借用属主 ZzUnwrapView（须比句柄长寿）。
class ZZTERM_API ZzStitchedLineView final {
public:
    /**
     * @brief 拼接行有效全长（链内各物理行裁尾后有效段累加）。
     * @return 有效格数（空拼接行为 0）。
     */
    [[nodiscard]] int cellCount() const;
    /**
     * @brief 取拼接列 col 的单格视图（跨链寻址）。
     * @param col 拼接列（0 起；调用方保证 col < cellCount()）。
     * @return 该格的值语义单格视图。
     */
    [[nodiscard]] ZzCellView cellAt(int col) const;
    /**
     * @brief 链头统一行号（历史+屏幕统一空间，同 ZzLogicalPos::line）。
     * @return 统一行号（链头可能在历史区）。
     */
    [[nodiscard]] std::int64_t sourceLine() const noexcept;
    /**
     * @brief 链行数。
     * @return 链内物理行数（1 = 无折）。
     */
    [[nodiscard]] int sourceLineCount() const noexcept;

private:
    friend class ZzUnwrapView;
    ZzStitchedLineView(const ZzUnwrapView* owner, std::size_t index) noexcept
        : owner_(owner), index_(index) {}
    const ZzUnwrapView* owner_ = nullptr; ///< 属主视图（借用）
    std::size_t         index_ = 0;       ///< 拼接行索引
};

/// \brief 拼接行视图（M17b）：历史+屏幕统一空间按折链拼接的只读视图。
/// 经 ZzTerminal::unwrapView 获取；亦可由两个来源视图直接构造（测试/工具）。
class ZZTERM_API ZzUnwrapView final {
public:
    /**
     * @brief 以来源渲染视图与历史视图构造（ZzTerminal 内部接线使用）。
     * @param renderView 屏幕渲染视图（借用，须比本视图长寿）。
     * @param historyView 历史视图（借用，须比本视图长寿）。
     */
    ZzUnwrapView(const ZzRenderView& renderView, const ZzHistoryView& historyView);
    ~ZzUnwrapView();
    ZzUnwrapView(const ZzUnwrapView&) = delete;
    ZzUnwrapView& operator=(const ZzUnwrapView&) = delete;

    /**
     * @brief 拼接行数。
     * @return 拼接行条数（<= 统一空间物理行数；无折链时相等）。
     */
    [[nodiscard]] std::size_t lineCount() const;
    /**
     * @brief 第 index 条拼接行句柄。
     * @param index 拼接行索引（0 起；调用方保证 index < lineCount()）。
     * @return 拼接行只读句柄。
     */
    [[nodiscard]] ZzStitchedLineView lineAt(std::size_t index) const noexcept;
    /**
     * @brief 全部拼接行的最大有效全长（横向滚动 range 原料）。
     * @return 最大有效格数（空缓冲为 0）。
     */
    [[nodiscard]] int maxCellCount() const;
    /**
     * @brief 引擎坐标 → 拼接坐标。
     * @param pos 统一空间物理坐标（同 ZzLogicalPos 语义）。
     * @return 拼接行索引 + 拼接列；pos.line 越界时钳到首/末拼接行。
     */
    [[nodiscard]] ZzStitchedPos toStitched(ZzLogicalPos pos) const;
    /**
     * @brief 拼接坐标 → 引擎坐标。
     * @param line 拼接行索引（钳到 [0, lineCount-1]）。
     * @param col 拼接列（钳到 [0, cellCount-1]；空行钳到链头格 0）。
     * @return 统一空间物理坐标。
     */
    [[nodiscard]] ZzLogicalPos fromStitched(std::int64_t line, int col) const;

private:
    friend class ZzStitchedLineView;
    class Impl;
    std::unique_ptr<Impl> impl_;

    // 供 ZzStitchedLineView 回调（detail，非公共契约）。
    [[nodiscard]] int stitchedCellCount(std::size_t index) const;
    [[nodiscard]] ZzCellView stitchedCellAt(std::size_t index, int col) const;
    [[nodiscard]] std::int64_t stitchedSourceLine(std::size_t index) const noexcept;
    [[nodiscard]] int stitchedSourceLineCount(std::size_t index) const noexcept;
};
```

- [ ] **步骤 5：实现 src/terminal/ZzUnwrapView.cpp**

```cpp
#include <ZzTerm/UnwrapView.h>

#include <algorithm>
#include <utility>
#include <vector>

// M17b：拼接索引——统一空间（历史+屏幕）按 wrapped 链分段的派生缓存。
// 双代计数（RenderView.dirtyGeneration + HistoryView.generation）任一
// 变化即失效，下次查询重建。重建 O(物理行数 × 列数)（裁尾逐格扫），
// M6 量级下可接受；不行再优化（YAGNI）。
class ZzUnwrapView::Impl {
public:
    Impl(const ZzRenderView& rv, const ZzHistoryView& hv) noexcept : rv_(rv), hv_(hv) {}

    struct Entry {
        std::int64_t startLine = 0; // 链头统一行号
        int          rows  = 0;     // 链行数
        int          cells = 0;     // 有效全长（裁尾累加）
    };

    // 统一行号 u 的行句柄：u < hist 为历史行，否则屏幕行。
    [[nodiscard]] ZzLineView unifiedLine(std::int64_t u) const
    {
        const auto hist = static_cast<std::int64_t>(hv_.lineCount());
        if (u < hist)
            return hv_.lineAt(static_cast<std::size_t>(u));
        return rv_.lineAt(static_cast<int>(u - hist));
    }

    // 物理行有效格数：裁掉尾部无效格（text 空且非宽字符续格）——
    // 与 reflow 链流逐行裁尾同口径（Reflow.cpp：行尾默认空白恒为填充，
    // 码位 0x20 真空格 text 非空不受影响）。
    static int effectiveCells(const ZzLineView& line)
    {
        int n = line.cellCount();
        while (n > 0) {
            const ZzCellView c = line.cellAt(n - 1);
            if (!c.text.empty() || c.width == ZzCellWidth::WideContinuation)
                break;
            --n;
        }
        return n;
    }

    void ensureFresh()
    {
        const std::uint64_t rg = rv_.dirtyGeneration();
        const std::uint64_t hg = hv_.generation();
        if (rg == seenRenderGen_ && hg == seenHistoryGen_)
            return;
        entries_.clear();
        maxCells_ = 0;
        const auto hist = static_cast<std::int64_t>(hv_.lineCount());
        const std::int64_t total = hist + rv_.size().rows;
        std::int64_t u = 0;
        while (u < total) {
            Entry e;
            e.startLine = u;
            std::int64_t v = u;
            for (;;) {
                const ZzLineView line = unifiedLine(v);
                e.cells += effectiveCells(line);
                ++e.rows;
                if (!line.wrapped() || v + 1 >= total)
                    break;
                ++v;
            }
            maxCells_ = std::max(maxCells_, e.cells);
            entries_.push_back(e);
            u = v + 1;
        }
        seenRenderGen_ = rg;
        seenHistoryGen_ = hg;
    }

    [[nodiscard]] const std::vector<Entry>& entries()
    {
        ensureFresh();
        return entries_;
    }
    [[nodiscard]] int maxCellCount()
    {
        ensureFresh();
        return maxCells_;
    }

    // 拼接行 index 的拼接列 col → 单格（跨链寻址；调用方保证界内）
    [[nodiscard]] ZzCellView cellAt(std::size_t index, int col) const
    {
        const Entry& e = entries_[index];
        std::int64_t u = e.startLine;
        for (;;) {
            const ZzLineView line = unifiedLine(u);
            const int eff = effectiveCells(line);
            if (col < eff)
                return line.cellAt(col);
            col -= eff;
            ++u;
        }
    }

    [[nodiscard]] ZzStitchedPos toStitched(ZzLogicalPos pos)
    {
        ensureFresh();
        if (entries_.empty())
            return {0, 0};
        if (pos.line < 0)
            return {0, 0};
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            if (pos.line >= e.startLine && pos.line < e.startLine + e.rows) {
                int col = 0;
                for (std::int64_t u = e.startLine; u < pos.line; ++u)
                    col += effectiveCells(unifiedLine(u));
                col += std::min<int>(pos.col, effectiveCells(unifiedLine(pos.line)));
                return {static_cast<std::int64_t>(i), col};
            }
        }
        // 越界（pos.line 超末行）：钳到末拼接行行尾
        const std::size_t last = entries_.size() - 1;
        return {static_cast<std::int64_t>(last),
                entries_[last].cells > 0 ? entries_[last].cells - 1 : 0};
    }

    [[nodiscard]] ZzLogicalPos fromStitched(std::int64_t line, int col)
    {
        ensureFresh();
        if (entries_.empty())
            return {0, 0};
        line = std::clamp<std::int64_t>(line, 0,
                                        static_cast<std::int64_t>(entries_.size()) - 1);
        const Entry& e = entries_[static_cast<std::size_t>(line)];
        col = std::clamp(col, 0, e.cells > 0 ? e.cells - 1 : 0);
        int acc = 0;
        for (std::int64_t u = e.startLine; u < e.startLine + e.rows; ++u) {
            const int eff = effectiveCells(unifiedLine(u));
            if (col < acc + eff || u == e.startLine + e.rows - 1)
                return {u, eff > 0 ? std::min(col - acc, eff - 1) : 0};
            acc += eff;
        }
        return {e.startLine, 0}; // 不可达（上行末行兜底已返回）
    }

private:
    const ZzRenderView&  rv_;
    const ZzHistoryView& hv_;
    std::vector<Entry>   entries_;
    int                  maxCells_ = 0;
    std::uint64_t        seenRenderGen_ = 0; // 初值 0 与真代计数首值错开：
    std::uint64_t        seenHistoryGen_ = 0; // 首次 ensureFresh 恒重建
                                             //（首查前 entries_ 为空也安全）
};

ZzUnwrapView::ZzUnwrapView(const ZzRenderView& renderView, const ZzHistoryView& historyView)
    : impl_(std::make_unique<Impl>(renderView, historyView))
{
}
ZzUnwrapView::~ZzUnwrapView() = default;

std::size_t ZzUnwrapView::lineCount() const { return impl_->entries().size(); }

ZzStitchedLineView ZzUnwrapView::lineAt(std::size_t index) const noexcept
{
    return ZzStitchedLineView(this, index);
}

int ZzUnwrapView::maxCellCount() const { return impl_->maxCellCount(); }

ZzStitchedPos ZzUnwrapView::toStitched(ZzLogicalPos pos) const { return impl_->toStitched(pos); }

ZzLogicalPos ZzUnwrapView::fromStitched(std::int64_t line, int col) const
{
    return impl_->fromStitched(line, col);
}

int ZzUnwrapView::stitchedCellCount(std::size_t index) const
{
    return impl_->entries()[index].cells;
}

ZzCellView ZzUnwrapView::stitchedCellAt(std::size_t index, int col) const
{
    impl_->entries(); // 确保索引新鲜
    return impl_->cellAt(index, col);
}

std::int64_t ZzUnwrapView::stitchedSourceLine(std::size_t index) const noexcept
{
    return impl_->entries()[index].startLine;
}

int ZzUnwrapView::stitchedSourceLineCount(std::size_t index) const noexcept
{
    return impl_->entries()[index].rows;
}

int ZzStitchedLineView::cellCount() const { return owner_->stitchedCellCount(index_); }
ZzCellView ZzStitchedLineView::cellAt(int col) const { return owner_->stitchedCellAt(index_, col); }
std::int64_t ZzStitchedLineView::sourceLine() const noexcept
{
    return owner_->stitchedSourceLine(index_);
}
int ZzStitchedLineView::sourceLineCount() const noexcept
{
    return owner_->stitchedSourceLineCount(index_);
}
```

注意一个诚实的 noexcept 瑕疵（实现者须处理）：`stitchedSourceLine/
stitchedSourceLineCount` 标了 noexcept 但经 entries() 触发重建（可能
分配）。裁定：这两个方法改为**不触发重建**——直接读
`impl_->entries()` 的既有缓存（句柄本就要求先经 lineAt/lineCount
建立索引，调用顺序契约同 ZzLineView「句柄失效」口径）。实现为：
Impl 加 `entriesCached() const noexcept { return entries_; }` 访问器，
两个 noexcept 方法走它。

- [ ] **步骤 6：Terminal 接线**

`include/ZzTerm/Terminal.h`：
- 顶部前向声明区（ZzRenderView/ZzHistoryView 声明附近）加
  `class ZzUnwrapView;`
- historyView() 声明（:147）之后加：

```cpp
    /**
     * @brief 获取拼接行只读视图（M17b；与 renderView/historyView 平行的
     * 第三只读边界：折链拼回完整行 + 双向坐标换算）。
     *
     * 视图借用 Terminal，不得比 Terminal 长寿；索引惰性重建，feed/resize
     * 后查询自动跟随最新内容。非线程安全（同 renderView）。
     *
     * @return 拼接行视图常量引用。
     */
    [[nodiscard]] const ZzUnwrapView& unwrapView() const noexcept;
```

`src/terminal/Terminal.cpp`：
- 顶部 include 加 `#include <ZzTerm/UnwrapView.h>`
- Impl 成员区（backend 声明之后）加
  `std::unique_ptr<ZzUnwrapView> unwrapView;`
- Impl 构造函数 switch 之后加：

```cpp
        unwrapView = std::make_unique<ZzUnwrapView>(backend->renderView(),
                                                    backend->historyView());
```

- 文件尾部（historyView 访问器 :94 之后）加：

```cpp
const ZzUnwrapView& ZzTerminal::unwrapView() const noexcept { return *impl_->unwrapView; }
```

- [ ] **步骤 7：构建 + 测试通过**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R unwrap --output-on-failure
```

预期：test_unwrap_view 8 用例全绿。若有断言失败：先核账（手工推演
该用例行数/列数），确认是测试取值错还是实现错，改错的一方并在
报告里记录推演。

- [ ] **步骤 8：全量回归 + doxygen（新公共头过 doxygen）**

```bash
ctest --preset linux-gcc-debug --output-on-failure   # 62
doxygen Doxyfile                                     # exit 0 零警告
```

- [ ] **步骤 9：Commit**

```bash
git add include/ZzTerm/Types.h include/ZzTerm/UnwrapView.h include/ZzTerm/Terminal.h src/terminal/ZzUnwrapView.cpp src/terminal/Terminal.cpp tests/unit/test_unwrap_view.cpp
git commit -m "feat(view): M17b 拼接行视图 ZzUnwrapView——折链拼回完整行 + 双向坐标换算"
```

### 任务 2：spike 不换行显示模式（ZzClawTerm）

**文件：**
- 修改：`/home/zz/Jackfahdin/github/ZzClawTerm/spike/ZzCoreViewWidget.h`
- 修改：`/home/zz/Jackfahdin/github/ZzClawTerm/spike/ZzCoreViewWidget.cpp`
- 修改：`/home/zz/Jackfahdin/github/ZzClawTerm/spike/main.cpp`（两处滚动条装配）
- 修改：`/home/zz/Jackfahdin/github/ZzClawTerm/spike/tests/tst_spikerender.cpp`

- [ ] **步骤 1：ZzCoreViewWidget.h 接口扩展**

public 区（scrollByWheelStepsForTest 声明之后）加：

```cpp
    /**
     * @brief 设置不换行显示模式（M17b）：ON = 拼接行渲染 + 横向滚动。
     * @param on true 进不换行模式；false 回物理行渲染（现状）。
     */
    void setUnwrapMode(bool on);

    /**
     * @brief 当前是否不换行显示模式。
     * @return 是则为 true。
     */
    [[nodiscard]] bool unwrapMode() const noexcept { return unwrapMode_; }

    /**
     * @brief 设置横向滚动偏移（列数，0 = 左端；仅不换行模式有效）。
     * @param offset 目标偏移，自动夹取到 [0, maxHScrollOffset()]。
     */
    void setHScrollOffset(int offset);

    /**
     * @brief 当前横向滚动偏移。
     * @return 列偏移（0 = 左端）。
     */
    [[nodiscard]] int hScrollOffset() const noexcept { return hScrollOffset_; }

    /**
     * @brief 最大横向滚动偏移 = max(0, maxCellCount - 可见列数)。
     * @return 列数上限。
     */
    [[nodiscard]] int maxHScrollOffset() const noexcept;

    /**
     * @brief 测试钩子：不换行模式下可见第 row 行的横向窗口文本。
     * @param row 可见行号（0 起）。
     * @return 该拼接行 [hOffset, hOffset+cols) 窗口内文本拼接。
     */
    [[nodiscard]] QString visibleStitchedTextForTest(int row) const;

    /**
     * @brief 测试钩子：不换行模式下可见拼接行数（含历史滚动的统一拼接空间）。
     * @return 当前视口可见的拼接行条数。
     */
    [[nodiscard]] int visibleStitchedCountForTest() const;
```

signals 区加：

```cpp
    /**
     * @brief 横向可滚动范围变化（宿主据此更新横向滚动条上限）。
     * @param maximum 新的最大横向偏移。
     */
    void hScrollRangeChanged(int maximum);

    /**
     * @brief 横向滚动偏移变化（宿主据此同步横向滚动条位置）。
     * @param offset 新偏移（0 = 左端）。
     */
    void hScrollOffsetChanged(int offset);
```

protected 区加：

```cpp
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
```

private 区加成员与辅助：

```cpp
    /**
     * @brief 不换行模式下的统一拼接空间行数（历史段 + 屏幕段拼接后）。
     * @return 拼接行总数。
     */
    [[nodiscard]] std::size_t stitchedTotalLines() const;

    /**
     * @brief 不换行模式下完全落在历史区的拼接行数（跨缝链归屏幕侧）。
     * @return 历史拼接行数（纵向滚动上限）。
     */
    [[nodiscard]] int stitchedHistoryCount() const;

    /**
     * @brief 显示像素坐标 → 引擎统一坐标（两模式分派）。
     * @param pos 像素坐标。
     * @return 统一空间 ZzLogicalPos。
     */
    [[nodiscard]] ZzLogicalPos logicalPosAtPixel(QPoint pos) const;

    /**
     * @brief 不换行模式：光标拼接列出横向视口时贴边跟随。
     */
    void followCursorHorizontally();

    bool unwrapMode_ = false;   ///< 不换行显示模式（M17b）。
    int  hScrollOffset_ = 0;    ///< 横向滚动偏移（列，0 = 左端）。
    bool selecting_ = false;    ///< 鼠标拖拽选区进行中。
```

- [ ] **步骤 2：ZzCoreViewWidget.cpp 实现（渲染 + 滚动 + 切换）**

文件头 include 加 `#include <ZzTerm/UnwrapView.h>` 与
`#include <QMouseEvent>`。

paintEvent 改为两模式分派（物理分支逐字保留现有代码，新增加下分支）：

```cpp
void ZzCoreViewWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().base());

    if (unwrapMode_) {
        paintUnwrapped(painter);
    } else {
        // ……既有物理行渲染循环与光标绘制逐字保留（抽成 paintWrapped(painter)）……
    }
}
```

新方法 paintUnwrapped（拼接口径：一个拼接行占一个显示行，列窗口
[hScrollOffset_, hScrollOffset_+cols)，纵向滚动在拼接空间）：

```cpp
void ZzCoreViewWidget::paintUnwrapped(QPainter& painter)
{
    const ZzUnwrapView& view = term_.unwrapView();
    const ZzSize        grid = term_.size();
    // 拼接空间纵向视口：底部对齐，scrollOffset_ 为离底拼接行数。
    const std::size_t total = stitchedTotalLines();
    // 可见第 row 行的拼接索引 = total - scrollOffset_ - grid.rows + row
    //（视口高 grid.rows，钉底时末行 = total-1）。
    for (int row = 0; row < grid.rows; ++row) {
        const long long idx = (long long)total - scrollOffset_ - grid.rows + row;
        if (idx < 0 || idx >= (long long)total)
            continue; // 顶部留白（拼接行不足一屏）
        const ZzStitchedLineView line = view.lineAt((std::size_t)idx);
        const int y = row * cellHeight_ + cellAscent_;
        for (int c = hScrollOffset_;
             c < line.cellCount() && c - hScrollOffset_ < grid.cols; ++c) {
            const ZzCellView cell = line.cellAt(c);
            if (cell.text.empty())
                continue;
            painter.drawText((c - hScrollOffset_) * cellWidth_, y,
                             QString::fromStdString(cell.text));
        }
        // 选区反色高亮（如有）：本拼接行与选区重叠段
        paintSelectionOnStitchedRow(painter, (std::size_t)idx, row, line);
    }

    // 光标：物理坐标 → 拼接坐标 → 显示坐标（含横向跟随后的窗口）
    const ZzCursorState cursor = term_.cursor();
    if (scrollOffset_ == 0 && cursor.visible) {
        const auto hist = (std::int64_t)term_.historyView().lineCount();
        const ZzStitchedPos sp =
            view.toStitched(ZzLogicalPos{hist + cursor.position.row,
                                         cursor.position.col});
        // 显示行 = 拼接索引 - 视口顶索引
        const long long viewTop = (long long)total - scrollOffset_ - grid.rows;
        const long long row = (long long)sp.line - viewTop;
        const int col = (int)sp.col - hScrollOffset_;
        if (row >= 0 && row < grid.rows && col >= 0 && col < grid.cols) {
            const QRect cursorRect(col * cellWidth_, (int)row * cellHeight_,
                                   cellWidth_, cellHeight_);
            painter.fillRect(cursorRect, palette().text());
        }
    }
}
```

说明（实现者按此落实，变量命名可对齐文件风格）：
- 视口顶拼接索引 `viewTop = total - scrollOffset_ - grid.rows`（钉底时
  末行恰为 total-1；scrollOffset_>0 时向上滚入历史拼接行）。
- 显示行 row 对应拼接索引 viewTop + row；viewTop 为负时顶部留白。
- 选区高亮 paintSelectionOnStitchedRow：term_.selectionRange(start,end)
  有选区时，把本拼接行 [sp.line 区间] 与 [start,end) 求交（经
  toStitched 换算两端），交集列段扣除 hScrollOffset_ 后画反色矩形。
  物理分支同样加高亮（paintSelectionOnWrappedRow：直接按统一坐标
  行比较）——spike 此前无选区，两模式都要最小视觉反馈。

setUnwrapMode / 横向滚动 / 光标跟随：

```cpp
void ZzCoreViewWidget::setUnwrapMode(bool on)
{
    if (unwrapMode_ == on)
        return;
    unwrapMode_ = on;
    if (!on)
        setHScrollOffset(0); // 回物理模式横向归零
    emit hScrollRangeChanged(maxHScrollOffset());
    emit scrollRangeChanged(maxScrollOffset()); // 纵向口径随模式切换
    update();
}

int ZzCoreViewWidget::maxHScrollOffset() const noexcept
{
    if (!unwrapMode_)
        return 0;
    return qMax(0, term_.unwrapView().maxCellCount() - term_.size().cols);
}

void ZzCoreViewWidget::setHScrollOffset(int offset)
{
    offset = qBound(0, offset, maxHScrollOffset());
    if (offset == hScrollOffset_)
        return;
    hScrollOffset_ = offset;
    emit hScrollOffsetChanged(hScrollOffset_);
    update();
}

void ZzCoreViewWidget::followCursorHorizontally()
{
    if (!unwrapMode_)
        return;
    const ZzCursorState cursor = term_.cursor();
    if (!cursor.visible)
        return;
    const auto hist = (std::int64_t)term_.historyView().lineCount();
    const ZzStitchedPos sp = term_.unwrapView().toStitched(
        ZzLogicalPos{hist + cursor.position.row, cursor.position.col});
    const int cols = term_.size().cols;
    if ((int)sp.col < hScrollOffset_)
        setHScrollOffset((int)sp.col);
    else if ((int)sp.col >= hScrollOffset_ + cols)
        setHScrollOffset((int)sp.col - cols + 1); // 贴右边
}
```

纵向滚动口径切换：maxScrollOffset() 改为

```cpp
int ZzCoreViewWidget::maxScrollOffset() const noexcept
{
    if (unwrapMode_)
        return stitchedHistoryCount(); // 拼接空间：完全落在历史区的拼接行数
    return static_cast<int>(term_.historyView().lineCount());
}

int ZzCoreViewWidget::stitchedHistoryCount() const
{
    const ZzUnwrapView& view = term_.unwrapView();
    const auto hist = (std::int64_t)term_.historyView().lineCount();
    int count = 0;
    const std::size_t n = view.lineCount();
    for (std::size_t i = 0; i < n; ++i) {
        const ZzStitchedLineView line = view.lineAt(i);
        if (line.sourceLine() + line.sourceLineCount() <= hist)
            ++count; // 完全在历史区；跨缝链归屏幕侧
    }
    return count;
}

std::size_t ZzCoreViewWidget::stitchedTotalLines() const
{
    return term_.unwrapView().lineCount();
}
```

feedForTest / resizeEvent 尾部各加一行 `followCursorHorizontally();`
（在 setScrollOffset 夹取之后、update 之前），并在两处补
`emit hScrollRangeChanged(...)` 当 maxHScrollOffset() 变化时（仿
scrollRangeChanged 的 before/after 比较写法）。

keyPressEvent 开头加切换键（在 `if (scrollOffset_ != 0)` 之前）：

```cpp
    if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)
        && event->key() == Qt::Key_U) {
        setUnwrapMode(!unwrapMode_); // M17b：Ctrl+Shift+U 切换不换行显示
        event->accept();
        return;
    }
```

visibleStitchedTextForTest / visibleStitchedCountForTest：

```cpp
QString ZzCoreViewWidget::visibleStitchedTextForTest(int row) const
{
    const ZzUnwrapView& view = term_.unwrapView();
    const ZzSize        grid = term_.size();
    const std::size_t   total = view.lineCount();
    const long long idx = (long long)total - scrollOffset_ - grid.rows + row;
    if (idx < 0 || idx >= (long long)total)
        return QString();
    const ZzStitchedLineView line = view.lineAt((std::size_t)idx);
    QString out;
    for (int c = hScrollOffset_;
         c < line.cellCount() && c - hScrollOffset_ < grid.cols; ++c)
        out += QString::fromStdString(line.cellAt(c).text);
    return out;
}

int ZzCoreViewWidget::visibleStitchedCountForTest() const
{
    const ZzSize grid = term_.size();
    const long long total = (long long)term_.unwrapView().lineCount();
    return (int)qMax<long long>(0, qMin<long long>(grid.rows, total - scrollOffset_));
}
```

- [ ] **步骤 3：选区（鼠标三事件 + 复制）**

```cpp
ZzLogicalPos ZzCoreViewWidget::logicalPosAtPixel(QPoint pos) const
{
    const int row = qBound(0, pos.y() / cellHeight_, term_.size().rows - 1);
    const int col = qBound(0, pos.x() / cellWidth_, term_.size().cols - 1);
    if (unwrapMode_) {
        const ZzUnwrapView& view = term_.unwrapView();
        const std::size_t total = view.lineCount();
        const long long idx =
            (long long)total - scrollOffset_ - term_.size().rows + row;
        if (idx < 0)
            return {0, 0};
        return view.fromStitched(idx, col + hScrollOffset_);
    }
    // 物理模式：统一坐标 = 历史行数 - 纵向偏移 + 可见行
    const auto hist = (std::int64_t)term_.historyView().lineCount();
    return {hist - scrollOffset_ + row, col};
}

void ZzCoreViewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        selecting_ = true;
        const ZzLogicalPos p = logicalPosAtPixel(event->pos());
        term_.setSelection(p, p);
        update();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ZzCoreViewWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (selecting_) {
        term_.extendSelection(logicalPosAtPixel(event->pos()));
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ZzCoreViewWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (selecting_ && event->button() == Qt::LeftButton) {
        selecting_ = false;
        if (term_.hasSelection()) {
            const std::string text = term_.selectedText();
            if (!text.empty())
                QGuiApplication::clipboard()->setText(
                    QString::fromStdString(text)); // 复制完整拼接行（含屏外）
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}
```

include 加 `#include <QClipboard>` 与 `#include <QGuiApplication>`。

选区高亮绘制（两模式）：在 paint 行循环里，有选区时求交画反色。
物理模式：term_.selectionRange(start,end) 返回统一坐标区间，可见行
row 的统一行号 u = hist - scrollOffset_ + row，若 u 在 [start.line,
end.line] 内，列区间 [start.col/end.col 按首末行规则] 画反色矩形。
不换行模式：经 toStitched 把 start/end 换成拼接坐标后同理。实现者
按此语义写，允许抽公共辅助。

- [ ] **步骤 4：main.cpp 横向滚动条装配（runLocal/runSsh 两处相同）**

每处既有垂直 QScrollBar 装配之后加（两处的代码相同）：

```cpp
    // M17b：横向滚动条（不换行模式）；range 跟随 maxCellCount - cols。
    QScrollBar hScrollBar(Qt::Horizontal);
    hScrollBar.setRange(0, 0);
    QObject::connect(&widget, &ZzCoreViewWidget::hScrollRangeChanged,
                     &hScrollBar, [&hScrollBar, &widget](int max) {
                         hScrollBar.setMaximum(max);
                         hScrollBar.setValue(widget.hScrollOffset());
                     });
    QObject::connect(&widget, &ZzCoreViewWidget::hScrollOffsetChanged,
                     &hScrollBar, &QScrollBar::setValue);
    QObject::connect(&hScrollBar, &QScrollBar::valueChanged, &widget,
                     &ZzCoreViewWidget::setHScrollOffset);
```

layout 改 QGridLayout（或 QVBox 套 QHBox）：widget 左上、垂直条右上、
横向条左下：

```cpp
    auto* window = new QWidget;
    auto* layout = new QGridLayout(window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(&widget, 0, 0, 1, 1);
    layout->addWidget(&scrollBar, 0, 1, 1, 1);
    layout->addWidget(&hScrollBar, 1, 0, 1, 1);
    layout->setColumnStretch(0, 1);
    layout->setRowStretch(0, 1);
    window->resize(widget.sizeHint().width() + scrollBar.sizeHint().width(),
                   widget.sizeHint().height() + hScrollBar.sizeHint().height());
```

include 加 `#include <QGridLayout>`。两处（runLocal :155-184 区、
runSsh :331-360 区）同样改。

- [ ] **步骤 5：spike QTest**

`spike/tests/tst_spikerender.cpp` 追加新测试函数（仿既有 QTest
函数风格与注册方式）：

```cpp
// M17b：不换行模式——拼接行渲染 + 横向滚动 + 选区复制完整长行。
void tst_spikerender::unwrapMode()
{
    ZzCoreViewWidget widget;
    // 造一条 25 格长行（10 列屏折 3 行）+ 一行短行
    widget.terminal().resize(10, 4);
    widget.feedForTest("xxxxxxxxxxxxxxxxxxxxxxxxx\r\nb\r\n");
    QCOMPARE(widget.terminal().unwrapView().lineCount(), std::size_t(4));
    // OFF：物理行渲染（现状）
    QCOMPARE(widget.visibleLineTextForTest(0), QString("xxxxxxxxxx"));
    // ON：拼接行渲染——可见行 0 即完整 25 x
    widget.setUnwrapMode(true);
    QCOMPARE(widget.visibleStitchedTextForTest(0), QString(25, 'x'));
    // 横向滚动：窗口 [10, 20)
    widget.setHScrollOffset(10);
    QCOMPARE(widget.visibleStitchedTextForTest(0), QString(10, 'x'));
    QCOMPARE(widget.maxHScrollOffset(), 25 - 10);
    // 选区复制完整长行（含屏外）：像素坐标拖拽行 0 全宽
    //（逻辑坐标经 fromStitched 回映射，selectedText 得完整 25 x）
    // 直接用引擎坐标钉住映射结论：
    const auto& view = widget.terminal().unwrapView();
    const ZzLogicalPos p0 = view.fromStitched(0, 0);
    const ZzLogicalPos p1 = view.fromStitched(0, 24);
    widget.terminal().setSelection(p0, p1);
    QCOMPARE(QString::fromStdString(widget.terminal().selectedText()),
             QString(25, 'x'));
    // 切回 OFF：横向归零、物理渲染还原
    widget.setUnwrapMode(false);
    QCOMPARE(widget.hScrollOffset(), 0);
    QCOMPARE(widget.visibleLineTextForTest(0), QString("xxxxxxxxxx"));
}
```

注意：既有文件若无 unwrapMode 函数声明，需在类声明（文件头部
private slots 区）加 `void unwrapMode();`。若 widget.terminal()
返回 const 引用导致 setSelection/resize 不可调，按既有测试的做法
处理（既有测试如何喂 feed 就如何调）。

运行：

```bash
cmake --build /home/zz/Jackfahdin/github/ZzClawTerm/build/spike-debug
cd /home/zz/Jackfahdin/github/ZzClawTerm/build/spike-debug && QT_QPA_PLATFORM=offscreen ./spike/tst_spikerender
```

（二进制确切路径以构建输出为准；ctest --test-dir 亦可。）
预期：既有用例 + unwrapMode 全绿。

- [ ] **步骤 6：Commit（ZzClawTerm 仓库）**

```bash
cd /home/zz/Jackfahdin/github/ZzClawTerm
git add spike/ZzCoreViewWidget.h spike/ZzCoreViewWidget.cpp spike/main.cpp spike/tests/tst_spikerender.cpp
git commit -m "feat(spike): M17b 不换行显示模式——拼接行渲染 + 横向滚动 + 选区复制完整长行"
```

### 任务 3：文档同步 + ABI 登记（ZzTermCore）

**文件：**
- 修改：`docs/API.md`、`docs/Architecture-v2.md`、`docs/Scrollback-and-Reflow.md`

- [ ] **步骤 1：docs/API.md 加 unwrapView 一节**

仿 historyView（M14）既有小节的格式与行宽，写：获取方式与借用寿命
（`term.unwrapView()`，同 renderView 规则）、拼接行与 ZzStitchedPos
坐标空间、toStitched/fromStitched 双向换算与钳位规则、maxCellCount
滚动条原料、裁尾拼接口径一句话（与 reflow 链流逐行裁尾同口径）、
与 ZzLogicalPos「逻辑行」的术语区分（物理行统一空间 vs 拼接行）。
版本节按 M14 先例登记 ABI 新增符号（ZzTerminal::unwrapView、
ZzUnwrapView、ZzStitchedLineView、ZzStitchedPos），次版本号口径不变。

- [ ] **步骤 2：docs/Architecture-v2.md 两处**

视图层段落（renderView/historyView 描述处）补第三只读边界
ZzUnwrapView 一句；parity 登记区补一条：M17b 拼接行视图为纯视图层
新增，contour 库无对应概念，parity N/A（无可对照行为），双后端经
统一 RenderView/HistoryView 接口消费。

- [ ] **步骤 3：docs/Scrollback-and-Reflow.md 一处**

折链/链流相关小节后补一句：拼接行视图（M17b）消费同一链结构、
同一逐行裁尾口径做不换行显示，见规格
2026-10-10-m17b-unwrap-view-design.md。

- [ ] **步骤 4：doxygen + Commit**

```bash
doxygen Doxyfile   # exit 0 零警告
git add docs/API.md docs/Architecture-v2.md docs/Scrollback-and-Reflow.md
git commit -m "docs(m17b): API/架构/链流文档同步拼接行视图 + ABI 新增登记"
```

### 任务 4：全量回归 + 手测交付

**文件：** 无源码改动（纯验证）。

- [ ] **步骤 1：五项基线**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure   # 62
cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure   # 51
cmake -S . -B build/m2-shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check --output-on-failure   # 62
cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz --output-on-failure   # 3
doxygen Doxyfile   # exit 0 零警告
```

- [ ] **步骤 2：spike 全量（ZzClawTerm）**

```bash
cmake --build /home/zz/Jackfahdin/github/ZzClawTerm/build/spike-debug
cd /home/zz/Jackfahdin/github/ZzClawTerm/build/spike-debug && ctest --output-on-failure
```

- [ ] **步骤 3：汇报手测清单**

向用户交付：`./build/spike-debug/zzcore_spike --local`，手测金标准
（规格 §8）：
1. 长路径 `ll` 后 Ctrl+Shift+U 切不换行：长行不再折行，横向滚动条
   出现且可滚动看全文；
2. 鼠标拖拽选一条屏外有内容的长行 → 粘贴为完整行；
3. 任意缩拉窗口（含极窗）→ 拼接内容不变、无残行累积；
4. 光标在屏外列输入 → 视口自动跟随；
5. 再按 Ctrl+Shift+U 切回：物理行渲染还原，横向偏移归零。

等用户确认后再 tag m17b + push contour --tags + gh run list 盯 7 绿。

## 自检记录

- 规格覆盖：§4.3 API → 任务 1 步骤 4-6；§4.4 五语义 → 测试用例
  1/2/3/4/6/7 + 实现裁尾函数；§4.5 惰性索引 → 实现 ensureFresh；
  §5 演示层全部 bullet（开关/渲染/滚动条/光标跟随/选区/历史滚动）
  → 任务 2（搜索高亮 spike 不落地，规格 §5 已注明为 ZzClawTerm
  预备）；§8 测试 8 条 → 任务 1 步骤 2；§8 spike 手测 → 任务 4
  步骤 3；§9 文档 → 任务 3；§10 ABI → 任务 3 步骤 1。无遗漏。
- 占位符：paintUnwrapped 里「抽成 paintWrapped」为既有代码搬移
  （逐字保留），选区高亮两处给定了精确语义与坐标口径，其余全部
  代码完整。
- 类型一致性：ZzStitchedPos{int64 line, int32 col} 与 Types.h 既有
  ZzLogicalPos 同型；ZzStitchedLineView/ZzUnwrapView 方法名贯穿
  任务 1-2 一致；spike 侧 stitchedTotalLines/stitchedHistoryCount/
  logicalPosAtPixel/followCursorHorizontally 在 .h 声明与 .cpp
  实现一致。
