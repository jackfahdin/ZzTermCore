# M4 resize 真 reflow + 10 万行容量性能门控 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为 ZzTermCore 实现 resize 真 reflow（屏幕+scrollback 全量重组）、Contour 后端对齐、默认容量 10 万行与 benchmark 门控。

**架构：** 物理行存储不变，resize 列变化时沿 wrapped 链合并逻辑行再按新列宽重切（Konsole/Contour 同族）。共享纯函数 zzReflowLines 供 ZzScreen::reflow 原语与 ZzScrollback::reflow 两端复用；ZzNativeBackend::resize 负责"先历史后屏幕"的协调；Contour 侧显式钉住 Settings.allowReflowOnResize = true 并以 compat 对照验证。

**技术栈：** C++20、CMake preset linux-gcc-debug（构建目录 build/linux-gcc-debug）、ctest、pexpect+pyte 冒烟。

**规格：** docs/superpowers/specs/2026-09-20-m4-resize-reflow-design.md（已批准，commit 19f7404）

**规格实现核定（计划阶段对规格的两处细化，不算变更）：**
1. 规格 5.1 说"行尾空白不 trim"。实现发现必须裁掉逻辑行末尾的**完全默认空白格**（无文本+默认前景背景+无属性），否则 80→40 时短行的尾部空白会产生幽灵行。带背景色/属性的尾部格不裁。此为 Konsole/Contour 通行做法。
2. 规格 5.4 第 5 条提到 resize 置位 scrollbackChanged——ZzTerminalBackend::resize 只返回 bool，无 ZzTermChanges 通道；核定改为：reflow 全屏标脏 + RenderView 失效契约（既有），前端 resize 后重取视图。

**通用命令：**
- 配置+构建+测试：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
- 单跑某测试：`ctest --preset linux-gcc-debug -R <测试名> --output-on-failure`
- 新增测试文件后必须重新 `cmake --preset linux-gcc-debug`（GLOB CONFIGURE_DEPENDS 会自动收编，但需要触发一次 configure）
- OFF 回归：`cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check`
- shared 回归：`cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check`（目录已存在）
- 文档：`doxygen Doxyfile`（exit 0 零警告为门）
- doxygen 注释陷阱：行内 code span 内禁尖括号、内容禁以点开头、后禁紧跟顿号、禁井号预处理词；正文与行内代码中禁反斜杠转义，代码围栏内不受影响

**测试约定（tests/unit/*.cpp）：** 每个文件独立可执行、自带 main()、用 `ZZ_TEST_EXPECT`/`ZZ_CHECK` 风格宏（失败打印 FILE/LINE/条件并计数）、main 返回失败数。新文件零 CMake 改动。

---

### 任务 1：zzReflowLines 纯函数 + 单测矩阵

**文件：**
- 创建：`src/screen/Reflow.h`（内部头，不安装）
- 创建：`src/screen/Reflow.cpp`
- 修改：`CMakeLists.txt`（src 源文件列表处，把 Reflow.cpp 加入 ZzTermCore）
- 测试：`tests/unit/test_reflow.cpp`

算法语义（规格 5.1 + 两处核定）：
- 链划分：从行 i 起，若 lines[j].wrapped() 为 true 则 j+1 属于同一逻辑行链；
- 硬行（单链单行且 wrapped=false）：裁尾部默认空白格后截断/补空到新列宽，永不多行化；
- wrapped 链：内容 = 各行 cells 顺序拼接，裁掉链末尾的完全默认空白格；按新列宽重切；除末行外各行 wrapped=true；
- 宽字符原子搬运：WideContinuation 格跳过（随 lead 再生）；WideLead 落边界（剩余不足 2 列）时本行以默认空白收尾提前换行，lead 落到下一行开头；
- 续格再生规则与 putChar 一致（ZzNativeBackend.cpp:205-209）：续格只带 width=WideContinuation 与 lead 的前景/背景，不带属性；
- cluster 格跨行搬运必须重新登记：targetRow.internCluster(srcLine.clusterText(cell.clusterIndex()))；
- 光标跟踪（可选）：输入链序号+链内流偏移，输出重组后物理 (row, col)；偏移指向 WideContinuation 时记到其 lead 位置；偏移落在被裁空白区时兜底为链末行内容尾。

- [ ] **步骤 1：编写失败的测试**（tests/unit/test_reflow.cpp）

```cpp
// zzReflowLines 纯函数测试矩阵（M4 resize reflow）。
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/screen/Reflow.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// 造一行：text 为 ASCII 串，按 cols 补空；wrapped 置链标记。
static ZzLine makeLine(int cols, const std::string& text, bool wrapped)
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

// 提取行的可见文本（逐格码位，空格调出空格）。
static std::string lineText(const ZzLine& line)
{
    std::string s;
    for (int i = 0; i < line.cellCount(); ++i) {
        const ZzCell& c = line.cellAt(i);
        s += (c.width() == ZzCellWidth::WideContinuation) ? '\0' // 续格占位，调用方注意
             : (char)(c.isEmpty() ? ' ' : c.codePoint());
    }
    return s;
}

// 1. 变宽合并：80 列 3 行链 -> 120 列 2 行
static void testWidenMergesChain()
{
    std::vector<ZzLine> lines;
    std::string a(80, 'a'), b(80, 'b'), c(40, 'c');
    lines.push_back(makeLine(80, a, true));
    lines.push_back(makeLine(80, b, true));
    lines.push_back(makeLine(80, c, false));
    auto out = zzReflowLines(std::move(lines), 80, 120);
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(!out[1].wrapped());
    ZZ_TEST_EXPECT(out[0].cellCount() == 120);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 80) == a);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(80, 40) == b.substr(0, 40));
    ZZ_TEST_EXPECT(lineText(out[1]).substr(40, 40) == c);
}

// 2. 变窄重切：80 列 1 行硬行 -> 40 列截断（硬行不多行化）
static void testNarrowTruncatesHardLine()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, std::string(80, 'a'), false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 1);
    ZZ_TEST_EXPECT(!out[0].wrapped());
    ZZ_TEST_EXPECT(lineText(out[0]) == std::string(40, 'a'));
}

// 3. 变窄重切：wrapped 链 80x2 -> 40 列 4 行
static void testNarrowResplitsChain()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, std::string(80, 'a'), true));
    lines.push_back(makeLine(80, std::string(50, 'b'), false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 4);
    for (std::size_t i = 0; i + 1 < out.size(); ++i)
        ZZ_TEST_EXPECT(out[i].wrapped());
    ZZ_TEST_EXPECT(!out.back().wrapped());
    ZZ_TEST_EXPECT(lineText(out[3]).substr(0, 10) == std::string(10, 'b'));
}

// 4. 短行变窄不产生幽灵行（尾部默认空白被裁）
static void testNoPhantomRows()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, "hello", false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 1);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 5) == "hello");
}

// 5. 宽字符边界钳制：宽字符不拆半，落边界时前移一格
static void testWideCharBoundaryClamp()
{
    // 10 列行：4 个窄字符 + 3 个宽字符（占 6 列），链两行。
    std::vector<ZzLine> lines;
    ZzLine l0(10);
    for (int i = 0; i < 4; ++i) {
        ZzCell c; c.setWidth(ZzCellWidth::Narrow); c.setCodePoint(U'a' + i);
        l0.setCell(i, c);
    }
    for (int i = 0; i < 3; ++i) {
        ZzCell lead; lead.setWidth(ZzCellWidth::WideLead); lead.setCodePoint(char32_t(0x4E2D + i));
        ZzCell cont; cont.setWidth(ZzCellWidth::WideContinuation);
        l0.setCell(4 + i * 2, lead);
        l0.setCell(5 + i * 2, cont);
    }
    l0.setWrapped(true);
    ZzLine l1(10); // 链末行：2 个窄字符
    for (int i = 0; i < 2; ++i) {
        ZzCell c; c.setWidth(ZzCellWidth::Narrow); c.setCodePoint(U'x' + i);
        l1.setCell(i, c);
    }
    lines.push_back(std::move(l0));
    lines.push_back(std::move(l1));
    // 重切到 5 列：'abcd' + 宽字符1(2列) => 第一行 abcd+空白? 宽字符1 不落第5列(剩1列不足) -> 前移
    auto out = zzReflowLines(std::move(lines), 10, 5);
    // 内容流：abcd 中 中 中 xy（宽字符各2列）=> 行0: abcd + 空白(宽字符前移)
    // 行1: 中1 中2 空白(中3前移)  行2: 中3 xy
    ZZ_TEST_EXPECT(out.size() == 3);
    ZZ_TEST_EXPECT(out[0].cellCount() == 5);
    ZZ_TEST_EXPECT(out[0].cellAt(4).isEmpty()); // 边界补空白
    ZZ_TEST_EXPECT(out[1].cellAt(0).width() == ZzCellWidth::WideLead);
    ZZ_TEST_EXPECT(out[1].cellAt(0).codePoint() == 0x4E2D);
    ZZ_TEST_EXPECT(out[1].cellAt(1).width() == ZzCellWidth::WideContinuation);
    ZZ_TEST_EXPECT(out[1].cellAt(4).isEmpty()); // 第二个边界补空白
    ZZ_TEST_EXPECT(out[2].cellAt(0).codePoint() == 0x4E2F);
    ZZ_TEST_EXPECT(out[2].cellAt(2).codePoint() == U'x');
    ZZ_TEST_EXPECT(!out[2].wrapped());
}

// 6. 光标跟踪：链内偏移映射
static void testCursorTracking()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, std::string(80, 'a'), true));
    lines.push_back(makeLine(80, std::string(80, 'b'), false));
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 100; // 第二物理行第 20 列
    auto out = zzReflowLines(std::move(lines), 80, 40, &cur);
    ZZ_TEST_EXPECT(out.size() == 4);
    ZZ_TEST_EXPECT(cur.row == 2);  // 偏移 100 -> 40 列下第 2 行
    ZZ_TEST_EXPECT(cur.col == 20); // 100 % 40 = 20
}

// 7. 光标偏移指向续格时记到 lead 位置
static void testCursorOnContinuation()
{
    std::vector<ZzLine> lines;
    ZzLine l0(4);
    ZzCell lead; lead.setWidth(ZzCellWidth::WideLead); lead.setCodePoint(0x4E2D);
    ZzCell cont; cont.setWidth(ZzCellWidth::WideContinuation);
    l0.setCell(0, lead);
    l0.setCell(1, cont);
    lines.push_back(std::move(l0));
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 1; // 续格偏移
    auto out = zzReflowLines(std::move(lines), 4, 8, &cur);
    ZZ_TEST_EXPECT(cur.row == 0 && cur.col == 0); // 归到 lead
}

// 8. 空行与全空白行
static void testBlankLines()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, "", false));
    lines.push_back(makeLine(80, "tail", false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(lineText(out[1]).substr(0, 4) == "tail");
}

// 9. cluster 跨行搬运重新登记
static void testClusterReintern()
{
    std::vector<ZzLine> lines;
    ZzLine l0(4);
    ZzCell c;
    c.setWidth(ZzCellWidth::Narrow);
    c.setCluster(l0.internCluster("e\xCC\x81")); // e + 组合重音符
    l0.setCell(3, c); // 放行尾，变窄后应被挤到下一行
    lines.push_back(std::move(l0));
    ZzLine l1(4);
    ZzCell x; x.setWidth(ZzCellWidth::Narrow); x.setCodePoint(U'z');
    l1.setCell(0, x);
    lines.push_back(std::move(l1));
    lines[0].setWrapped(true);
    auto out = zzReflowLines(std::move(lines), 4, 2);
    // 链内容：空空空cluster z -> 2 列重切：行0 空空 / 行1 空+cluster / 行2 z
    ZZ_TEST_EXPECT(out.size() == 3);
    ZZ_TEST_EXPECT(out[1].cellAt(1).isCluster());
    ZZ_TEST_EXPECT(out[1].clusterText(out[1].cellAt(1).clusterIndex()) == "e\xCC\x81");
}

// 10. 光标落在被裁空白区的兜底
static void testCursorInTrimmedBlanks()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, "hi", false));
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 79; // 尾部空白区，变窄后被裁
    auto out = zzReflowLines(std::move(lines), 80, 40, &cur);
    ZZ_TEST_EXPECT(out.size() == 1);
    ZZ_TEST_EXPECT(cur.row == 0);
    ZZ_TEST_EXPECT(cur.col >= 0 && cur.col < 40); // 兜底到链内容尾/clamp
}

int main()
{
    testWidenMergesChain();
    testNarrowTruncatesHardLine();
    testNarrowResplitsChain();
    testNoPhantomRows();
    testWideCharBoundaryClamp();
    testCursorTracking();
    testCursorOnContinuation();
    testBlankLines();
    testClusterReintern();
    testCursorInTrimmedBlanks();
    if (g_failures == 0)
        std::printf("test_reflow: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug 2>&1 | tail -5`
预期：编译失败，`Reflow.h` 不存在（fatal error）

- [ ] **步骤 3：编写最少实现代码**

创建 `src/screen/Reflow.h`：

```cpp
#pragma once

// 内部头（不安装）：resize reflow 共享纯函数（M4）。
// 物理行序列沿 wrapped 链合并为逻辑行后按新列宽重切；
// ZzScreen::reflow 与 ZzChunkedScrollback::reflow 两端共用，杜绝算法漂移。

#include <cstddef>
#include <vector>

#include "ZzTerm/Line.h"

/// @brief reflow 光标跟踪：输入链坐标，输出重组后物理坐标。
struct ZzReflowCursor {
    std::size_t chainIndex = 0; ///< 输入：光标所在逻辑行链序号（0 起）。
    int chainOffset = 0;        ///< 输入：链内流偏移（单元格，含各物理行整行宽）。
    int row = 0;                ///< 输出：重组后物理行号。
    int col = 0;                ///< 输出：重组后物理列号。
};

/**
 * @brief 将物理行序列从旧列宽重组到新列宽（soft-wrap reflow）。
 * @param lines 物理行序列（按值传入，调用方可 move；函数不保留引用）。
 * @param oldCols 旧列宽（> 0，不变量：所有行均为该宽度）。
 * @param newCols 新列宽（> 0）。
 * @param cursor 可选光标跟踪（nullptr 表示不跟踪）。
 * @return 重组后的物理行序列（每行 newCols 列，wrapped 标记已重算）。
 * @note 硬行（未 wrapped 的单行链）截断/补空，永不多行化；
 *       链末尾的完全默认空白格被裁除（避免短行变窄产生幽灵行）；
 *       宽字符原子搬运不落边界（边界前移一格补默认空白）；
 *       cluster 格在新行重新 internCluster。
 */
std::vector<ZzLine> zzReflowLines(std::vector<ZzLine> lines, int oldCols, int newCols,
                                  ZzReflowCursor* cursor = nullptr);
```

创建 `src/screen/Reflow.cpp`（完整实现）：

```cpp
#include "Reflow.h"

namespace {

/// @brief 完全默认空白格（可裁）：无文本、默认前景背景、无属性。
bool zzIsPlainBlank(const ZzCell& c)
{
    return c.isEmpty() && c.foreground().isDefault() && c.background().isDefault()
           && c.attributes().raw() == 0;
}

} // namespace

std::vector<ZzLine> zzReflowLines(std::vector<ZzLine> lines, int oldCols, int newCols,
                                  ZzReflowCursor* cursor)
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
        const std::size_t chainLen = chainEnd - chainStart;
        const bool isHardLine = (chainLen == 1) && !lines[chainStart].wrapped();

        // 链内容的有效末尾（流偏移，不含）：裁掉末尾完全默认空白格。
        std::size_t trimEnd = chainLen * static_cast<std::size_t>(oldCols);
        while (trimEnd > 0) {
            const std::size_t s = trimEnd - 1;
            const ZzCell& c = lines[chainStart + s / oldCols].cellAt((int)(s % oldCols));
            if (!zzIsPlainBlank(c))
                break;
            --trimEnd;
        }

        const bool trackThis = cursor && cursor->chainIndex == chainIndex;
        bool tracked = false;

        ZzLine row(newCols);
        int outCol = 0;
        int lastContentCol = 0; // 本行最后一个内容格之后的列（兜底用）

        auto flushRow = [&](bool wrapped) {
            row.setWrapped(wrapped);
            out.push_back(std::move(row));
            row = ZzLine(newCols);
            outCol = 0;
            lastContentCol = 0;
        };

        const std::size_t limit = isHardLine
            ? std::min(trimEnd, static_cast<std::size_t>(newCols)) // 硬行截断
            : trimEnd;

        for (std::size_t s = 0; s < limit; ++s) {
            const ZzLine& srcLine = lines[chainStart + s / oldCols];
            const ZzCell& cell = srcLine.cellAt((int)(s % oldCols));
            if (cell.width() == ZzCellWidth::WideContinuation)
                continue; // 续格随 lead 再生

            const int w = (cell.width() == ZzCellWidth::WideLead) ? 2 : 1;
            if (outCol + w > newCols) {
                // 宽字符落边界：本行以默认空白收尾，提前换行。
                flushRow(true);
            }

            if (trackThis && !tracked
                && (cursor->chainOffset == (int)s
                    || (w == 2 && cursor->chainOffset == (int)s + 1))) {
                cursor->row = (int)out.size();
                cursor->col = outCol;
                tracked = true;
            }

            ZzCell placed = cell;
            if (placed.isCluster())
                placed.setCluster(row.internCluster(srcLine.clusterText(cell.clusterIndex())));
            row.setCell(outCol, placed);
            if (w == 2) {
                // 续格再生规则与 ZzNativeBackend::putChar 一致：
                // 仅 width + 前景/背景，不带属性。
                ZzCell cont;
                cont.setWidth(ZzCellWidth::WideContinuation);
                cont.setForeground(cell.foreground());
                cont.setBackground(cell.background());
                row.setCell(outCol + 1, cont);
            }
            outCol += w;
            lastContentCol = outCol;
        }

        flushRow(false); // 链末行（硬行也在此收尾）

        if (trackThis && !tracked) {
            // 光标落在被裁空白区：兜底到链末行内容尾。
            cursor->row = (int)out.size() - 1;
            cursor->col = std::min(lastContentCol, newCols - 1);
        }
        ++chainIndex;
        chainStart = chainEnd;
    }
    return out;
}
```

注意：上述兜底中 `lastContentCol` 在 flushRow 后归零，需在 flushRow(false) 之前保存链末行的值——实现时把兜底记录放在 flushRow(false) 调用前读取，或将 flushRow 改为返回收尾行号/列。以编译通过且测试 10 全绿为准微调。

修改 `CMakeLists.txt`：在 ZzTermCore 源文件列表（src/screen/Screen.cpp 附近）加入 `src/screen/Reflow.cpp`。

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_reflow --output-on-failure`
预期：PASS，`test_reflow: all passed`

- [ ] **步骤 5：Commit**

```bash
git add src/screen/Reflow.h src/screen/Reflow.cpp tests/unit/test_reflow.cpp CMakeLists.txt
git commit -m "feat(screen): M4 zzReflowLines 逻辑行重组纯函数与单测矩阵"
```

---

### 任务 2：ZzScreen::reflow 原语（屏幕区重组 + 光标映射 + 备用屏）

**文件：**
- 修改：`include/ZzTerm/Screen.h`（新增 reflow 声明 + 私有 reflowBuffer；更新文件头 M4 预留措辞）
- 修改：`src/screen/Screen.cpp`
- 测试：`tests/unit/test_screen_reflow.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
// ZzScreen::reflow 网格原语测试（M4）。
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

// 1. 变窄重切 + 溢出行经 ScrollOutCallback 入历史（Primary）
static void testNarrowOverflowToCallback()
{
    ZzScreen scr(4, 2);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    writeRow(scr, 0, "abcd");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "efgh");
    scr.setCursorPosition(ZzPosition{1, 2});

    scr.reflow(2); // 4 列 -> 2 列：链 abcd/efgh -> ab/cd/ef/gh 共 4 行，溢出 2 行
    ZZ_TEST_EXPECT(spilled.size() == 2);
    ZZ_TEST_EXPECT(rowText(spilled[0], 2) == "ab");
    ZZ_TEST_EXPECT(rowText(spilled[1], 2) == "cd");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "ef");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "gh");
    ZZ_TEST_EXPECT(scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
    // 光标原 (1,2)：链偏移 4+2=6 -> 2 列下行 3 列 0，溢出 2 行后行 1 列 0
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
    ZZ_TEST_EXPECT(scr.cursor().position.col == 0);
}

// 2. 变宽合并 + 底部补空行
static void testWidenPadsBottom()
{
    ZzScreen scr(2, 4);
    writeRow(scr, 0, "ab");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "cd");
    writeRow(scr, 2, "xy");
    scr.reflow(4); // 链 ab/cd 合并为 abcd 一行
    ZZ_TEST_EXPECT(scr.size().rows == 4);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 4) == "abcd");
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 4) == "xy  ");
    ZZ_TEST_EXPECT(scr.lineAt(2).cellCount() == 4); // 补的空行
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 4) == "    ");
}

// 3. 备用屏重组但溢出不入历史
static void testAlternateReflowNoHistory()
{
    ZzScreen scr(4, 2);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    writeRow(scr, 0, "abcd");
    scr.setLineWrapped(0, true);
    writeRow(scr, 1, "efgh");
    scr.reflow(2);
    ZZ_TEST_EXPECT(spilled.empty()); // 备用屏溢出丢弃，不进历史
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "ef");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "gh");
}

// 4. wrapPending 清除 + 滚动区复位 + tab stops 重建 + 全屏标脏
static void testSideEffects()
{
    ZzScreen scr(8, 3);
    scr.setWrapPending(true);
    scr.setScrollRegion(1, 2);
    scr.setTabStop(4);
    const std::uint64_t genBefore = scr.dirtyGeneration();
    scr.clearDirty();
    scr.reflow(4);
    ZZ_TEST_EXPECT(!scr.wrapPending());
    ZZ_TEST_EXPECT(scr.scrollRegionRows().startCol == 0);
    ZZ_TEST_EXPECT(scr.scrollRegionRows().endCol == 2);
    ZZ_TEST_EXPECT(scr.nextTabStop(0) == 3); // tab stops 按新列宽重建为空，nextTabStop 返回最后一列
    ZZ_TEST_EXPECT(scr.dirtyGeneration() > genBefore);
    ZZ_TEST_EXPECT(scr.rowDirty(0) && scr.rowDirty(1) && scr.rowDirty(2));
}

// 5. 列宽未变与非法参数为空操作
static void testNoOp()
{
    ZzScreen scr(4, 2);
    writeRow(scr, 0, "abcd");
    scr.reflow(4); // 同宽：不动
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 4) == "abcd");
    scr.reflow(0); // 非法：不崩
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 4) == "abcd");
}

int main()
{
    testNarrowOverflowToCallback();
    testWidenPadsBottom();
    testAlternateReflowNoHistory();
    testSideEffects();
    testNoOp();
    if (g_failures == 0)
        std::printf("test_screen_reflow: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug 2>&1 | tail -5`
预期：编译失败，`ZzScreen` 没有 `reflow` 成员

- [ ] **步骤 3：编写最少实现代码**

`include/ZzTerm/Screen.h`：
- 文件头职责注释中"resize 时的 soft-wrap reflow 属于 Terminal/History 协同逻辑，Screen 仅提供网格级 resize 原语"改为："resize 列变化时的 soft-wrap reflow 由 reflow() 原语承担（M4 落地），ZzNativeBackend::resize 负责屏幕与历史的协调顺序"。
- resize() 注释中"不做 reflow"保留（行向调整仍走它）。
- 新增公开方法声明（放在 resize 之后）：

```cpp
    /**
     * @brief 列向 soft-wrap reflow 原语：屏幕区行按新列宽重组（M4）。
     * @param newCols 新列宽（> 0；等于当前列宽或非法时为空操作）。
     * @note Primary/Alternate 两套网格各自重组；重组导致行数超出时，
     *       Primary 顶部溢出行经 ScrollOutCallback 上移（Alternate 溢出
     *       直接丢弃，备用屏无历史）；行数不足时底部补空行。
     *       光标按"逻辑行链 + 链内偏移"跟随内容映射并 clamp；
     *       wrapPending 清除；滚动区复位全屏；tab stops 按新列宽重建；
     *       全屏标脏。行数不变（行向调整由 resize 负责）。
     */
    void reflow(int newCols);
```

- 私有区新增：`void reflowBuffer(Buffer& buf, int newCols, bool mayScrollOut);`

`src/screen/Screen.cpp` 实现：

```cpp
void ZzScreen::reflow(int newCols)
{
    if (newCols <= 0 || newCols == cols_)
        return;
    reflowBuffer(primary_, newCols, true);
    reflowBuffer(alternate_, newCols, false);
    cols_ = newCols;
    tabStops_.assign(static_cast<std::size_t>(cols_), 0); // tab stops 不跨列宽保留
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
    ++dirtyGeneration_;
    markAllDirty();
}

void ZzScreen::reflowBuffer(Buffer& buf, int newCols, bool mayScrollOut)
{
    // 光标 -> 链坐标：向上找链起点，统计链序号，偏移 = 链内整行宽累加 + 光标列。
    const int cursorRow = buf.cursor.position.row;
    int chainStartRow = cursorRow;
    while (chainStartRow > 0 && buf.lines[static_cast<std::size_t>(chainStartRow - 1)].wrapped())
        --chainStartRow;
    ZzReflowCursor track;
    {
        std::size_t index = 0;
        int r = 0;
        const int total = static_cast<int>(buf.lines.size());
        while (r < chainStartRow) { // 逐链跳过
            ++index;
            while (r < total && buf.lines[static_cast<std::size_t>(r)].wrapped())
                ++r;
            ++r; // 链末行
        }
        track.chainIndex = index;
        track.chainOffset = (cursorRow - chainStartRow) * cols_ + buf.cursor.position.col;
    }

    std::vector<ZzLine> out = zzReflowLines(buf.lines, cols_, newCols, &track);

    // 行数平衡：溢出上移（仅 Primary）或丢弃（Alternate），不足底部补空行。
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
        while (static_cast<int>(out.size()) < rows_)
            out.push_back(ZzLine(newCols));
    }

    buf.lines = std::move(out);
    buf.cursor.position.row = std::clamp(track.row, 0, rows_ - 1);
    buf.cursor.position.col = std::clamp(track.col, 0, newCols - 1);
    buf.wrapPending = false;
}
```

文件顶部 include 加 `#include "Reflow.h"` 与 `#include <algorithm>`（clamp 若未引入）。

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "test_screen_reflow|test_wrap_pending|test_terminal_core" --output-on-failure`
预期：全部 PASS（既有测试不回归）

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Screen.h src/screen/Screen.cpp tests/unit/test_screen_reflow.cpp
git commit -m "feat(screen): M4 ZzScreen::reflow 屏幕区重组原语（光标映射/溢出回流/备用屏）"
```

---

### 任务 3：ZzScrollback::reflow + stats 累计记账

**文件：**
- 修改：`include/ZzTerm/Scrollback.h`（reflow 虚方法 + ZzScrollbackStats 加字段 + 头注释更新）
- 修改：`src/history/ChunkedScrollback.cpp`
- 测试：`tests/unit/test_scrollback.cpp`（新建；顺带补上存量 append/裁剪行为的首次直接覆盖）

- [ ] **步骤 1：编写失败的测试**

```cpp
// ZzScrollback（chunked 实现）行为测试：append/裁剪/reflow/stats 记账（M4）。
#include <cstdio>
#include <string>
#include <vector>

#include "ZzTerm/Scrollback.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static ZzLine makeLine(int cols, const std::string& text, bool wrapped)
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

static std::string lineText(const ZzLine& line, int n)
{
    std::string s;
    for (int i = 0; i < n && i < line.cellCount(); ++i) {
        const ZzCell& c = line.cellAt(i);
        s += (char)(c.isEmpty() ? ' ' : c.codePoint());
    }
    return s;
}

// 1. 存量行为：append + 容量裁剪 + lineAt 顺序
static void testAppendAndTrim()
{
    auto sb = zzCreateChunkedScrollback(3);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "l1", false));
    batch.push_back(makeLine(4, "l2", false));
    batch.push_back(makeLine(4, "l3", false));
    batch.push_back(makeLine(4, "l4", false));
    sb->append(std::move(batch));
    ZZ_TEST_EXPECT(sb->lineCount() == 3);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "l2"); // 最旧的 l1 被裁
    ZZ_TEST_EXPECT(lineText(sb->lineAt(2), 2) == "l4");
    const ZzScrollbackStats st = sb->stats();
    ZZ_TEST_EXPECT(st.totalAppended == 4);
    ZZ_TEST_EXPECT(st.totalDropped == 1);
    ZZ_TEST_EXPECT(st.hotLines == 3);
    ZZ_TEST_EXPECT(st.approxBytes > 0);
}

// 2. 容量 0 不保留历史，记账照常
static void testZeroCapacity()
{
    auto sb = zzCreateChunkedScrollback(0);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "l1", false));
    sb->append(std::move(batch));
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    sb->reflow(2); // 空历史 reflow 为空操作，不崩
}

// 3. reflow 变窄重切历史行
static void testReflowNarrow()
{
    auto sb = zzCreateChunkedScrollback(100);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "abcd", true));
    batch.push_back(makeLine(4, "efgh", false));
    batch.push_back(makeLine(4, "tail", false));
    sb->append(std::move(batch));
    sb->reflow(2);
    // 链 abcd/efgh -> ab/cd/ef/gh（4 行），tail -> ta（硬行截断）
    ZZ_TEST_EXPECT(sb->lineCount() == 5);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "ab");
    ZZ_TEST_EXPECT(sb->lineAt(0).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(3), 2) == "gh");
    ZZ_TEST_EXPECT(!sb->lineAt(3).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(4), 2) == "ta");
}

// 4. reflow 变宽合并 + 重组后超容量仍从最旧端裁
static void testReflowWidenAndTrim()
{
    auto sb = zzCreateChunkedScrollback(2);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(2, "ab", true));
    batch.push_back(makeLine(2, "cd", false));
    batch.push_back(makeLine(2, "xy", false));
    sb->append(std::move(batch));
    sb->reflow(4); // 链 abcd 合并 1 行 + xy 1 行 = 2 行，不超容量
    ZZ_TEST_EXPECT(sb->lineCount() == 2);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 4) == "abcd");
    ZZ_TEST_EXPECT(!sb->lineAt(0).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(1), 4) == "xy  ");
    const ZzScrollbackStats st = sb->stats();
    ZZ_TEST_EXPECT(st.totalAppended == 3);
}

// 5. clear 后累计记账不复位
static void testClearKeepsCounters()
{
    auto sb = zzCreateChunkedScrollback(10);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "l1", false));
    sb->append(std::move(batch));
    sb->clear();
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    ZZ_TEST_EXPECT(sb->stats().totalAppended == 1);
}

int main()
{
    testAppendAndTrim();
    testZeroCapacity();
    testReflowNarrow();
    testReflowWidenAndTrim();
    testClearKeepsCounters();
    if (g_failures == 0)
        std::printf("test_scrollback: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug 2>&1 | tail -5`
预期：编译失败，`ZzScrollback` 没有 `reflow` 成员、`ZzScrollbackStats` 没有 `totalAppended`

- [ ] **步骤 3：编写最少实现代码**

`include/ZzTerm/Scrollback.h`：
- ZzScrollbackStats 增加两字段（注释钉住用途）：

```cpp
    std::uint64_t totalAppended = 0; ///< 累计入库行数（含随后被裁的）；为 M5 绝对行号选区坐标铺路。
    std::uint64_t totalDropped  = 0; ///< 累计因容量裁剪丢弃的行数；clear 不复位。
```

（需 `#include <cstdint>`。）
- append() 注释中"不做 logical line 合并/拆分"改为"logical line 重组由 reflow() 承担"。
- 新增虚方法（放在 setCapacity 之后）：

```cpp
    /**
     * @brief 列向 soft-wrap reflow：全部历史行按新列宽重组（M4）。
     * @param newCols 新列宽（> 0；等于当前行宽或历史为空时为空操作）。
     * @note 不变量：历史行宽度与终端当前列宽一致（resize 先历史后屏幕，
     *       屏幕溢出行以新宽度入库）；重组算法与屏幕区共用 zzReflowLines；
     *       重组后超容量仍从最旧一端裁剪并计入 totalDropped。
     */
    virtual void reflow(int newCols) = 0;
```

`src/history/ChunkedScrollback.cpp`：
- include 加 `#include "../screen/Reflow.h"` 与 `#include <cstdint>`。
- append 开头累计：`totalAppended_ += lines.size();`（容量 0 提前返回时改为：计入 appended 与 dropped 后返回，见测试 2）。
- trimToCapacity 中累计：`totalDropped_ += removable;`
- stats() 填充两新字段。
- 新增成员 `std::uint64_t totalAppended_ = 0; std::uint64_t totalDropped_ = 0;`，clear() 不复位两者。
- 实现 reflow：

```cpp
    void reflow(int newCols) override
    {
        if (newCols <= 0 || totalLines_ == 0)
            return;
        const int oldCols = chunks_.front().front().cellCount(); // 不变量：全历史同宽
        if (oldCols == newCols)
            return;
        std::vector<ZzLine> all;
        all.reserve(totalLines_);
        for (auto& chunk : chunks_)
            for (auto& line : chunk)
                all.push_back(std::move(line));
        all = zzReflowLines(std::move(all), oldCols, newCols);
        chunks_.clear();
        totalLines_ = 0;
        approxBytes_ = 0;
        for (auto& line : all) {
            if (chunks_.empty() || chunks_.back().size() >= kChunkLines)
                chunks_.emplace_back();
            approxBytes_ += sizeof(ZzLine) +
                            static_cast<std::size_t>(line.cellCount()) * sizeof(ZzCell);
            chunks_.back().push_back(std::move(line));
            ++totalLines_;
        }
        trimToCapacity();
    }
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "test_scrollback|test_reflow|test_screen_reflow" --output-on-failure`
预期：全部 PASS

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Scrollback.h src/history/ChunkedScrollback.cpp tests/unit/test_scrollback.cpp
git commit -m "feat(history): M4 ZzScrollback::reflow 与 totalAppended/totalDropped 累计记账"
```

---

### 任务 4：ZzNativeBackend::resize 协调 + facade 集成测试

**文件：**
- 修改：`src/backend/native/ZzNativeBackend.cpp`（resize 协调）
- 修改：`include/ZzTerm/Terminal.h`（resize 文档更新 + 默认容量 10000 → 100000）
- 测试：`tests/unit/test_native_reflow.cpp`（新建，经 ZzTerminal facade 端到端）

协调语义（规格 5.4）：列变化先 `scrollback_->reflow(cols)` 再 `screen_.reflow(cols)`（屏幕溢出行以新宽度经既有 ScrollOutCallback 回流历史）；行变化走既有 `screen_.resize(cols, rows)`；行列同时变先列后行。

- [ ] **步骤 1：编写失败的测试**

```cpp
// native 后端 resize reflow 端到端测试：ZzTerminal facade -> feed -> resize -> 断言（M4）。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void feed(ZzTerminal& term, const std::string& bytes)
{
    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                         bytes.size()));
}

static std::string rowText(const ZzTerminal& term, int row, int n)
{
    std::string s;
    const auto& line = term.renderView().lineAt(row);
    for (int i = 0; i < n; ++i)
        s += term.renderView().lineAt(row).cellAt(i).text.empty() ? ' '
             : term.renderView().lineAt(row).cellAt(i).text[0];
    (void)line;
    return s;
}

// 1. 历史中的长行随变宽合并、变窄重切
static void testHistoryReflow()
{
    ZzTerminal term(10, 3, ZzBackendKind::Native, 100);
    // 20 字符长行（10 列折 2 行）+ 30 行把它顶入历史
    feed(term, "aaaaaaaaaaaaaaaaaaaax\r\n");
    for (int i = 0; i < 30; ++i)
        feed(term, "filler\r\n");
    ZZ_TEST_EXPECT(term.scrollback().lineCount() >= 2);

    term.resize(20, 3); // 变宽：历史中的 2 行链合并为 1 行
    bool foundMerged = false;
    for (std::size_t i = 0; i < term.scrollback().lineCount(); ++i) {
        const ZzLine& l = term.scrollback().lineAt(i);
        if (l.cellCount() == 20 && !l.wrapped() && l.cellAt(19).codePoint() == U'x')
            foundMerged = true;
    }
    ZZ_TEST_EXPECT(foundMerged);

    term.resize(10, 3); // 变窄：重新折成 2 行链
    bool foundChain = false;
    for (std::size_t i = 0; i + 1 < term.scrollback().lineCount(); ++i) {
        const ZzLine& l0 = term.scrollback().lineAt(i);
        const ZzLine& l1 = term.scrollback().lineAt(i + 1);
        if (l0.wrapped() && !l1.wrapped() && l1.cellAt(0).codePoint() != 0
            && l0.cellAt(0).codePoint() == U'a' && l1.cellAt(0).codePoint() == U'a')
            foundChain = true;
    }
    ZZ_TEST_EXPECT(foundChain);
}

// 2. 屏幕区随 resize 重组且光标 clamp 在界内
static void testScreenReflowAndCursor()
{
    ZzTerminal term(8, 4, ZzBackendKind::Native, 100);
    feed(term, "abcdefg hijklm"); // 折行：abcdefgh/ijklm
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text == "a");
    term.resize(4, 4);
    // 4 列下：abcd/efgh/ijkl/m（后续 filler 行被挤出/光标 clamp）
    const ZzPosition cur = term.cursor().position;
    ZZ_TEST_EXPECT(cur.row >= 0 && cur.row < 4 && cur.col >= 0 && cur.col < 4);
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text.size() == 1);
}

// 3. CJK 长行 resize 后不拆半
static void testCjkNotSplit()
{
    ZzTerminal term(6, 4, ZzBackendKind::Native, 100);
    feed(term, "中文中文中"); // 5 个宽字符占 10 列，折 2 行
    term.resize(5, 4);
    // 5 列下每个宽字符 2 列：每行最多 2 个宽字符（第 5 列留白）
    bool noOrphanContinuation = true;
    for (int r = 0; r < 4; ++r) {
        const auto& line = term.renderView().lineAt(r);
        for (int c = 0; c < 5; ++c) {
            const auto cv = line.cellAt(c);
            // 续格左侧必须是 lead
            if ((int)cv.width == 3 && c == 0)
                noOrphanContinuation = false;
        }
    }
    ZZ_TEST_EXPECT(noOrphanContinuation);
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text == "中");
}

// 4. 1049 备用屏与 resize 交错：进出后主屏内容仍在且已重组
static void testAltScreenInterleave()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feed(term, "primary-content-here\r\n"); // 20 字符折 2 行
    feed(term, "\x1b[?1049h");
    ZZ_TEST_EXPECT(term.isAlternateScreen());
    feed(term, "alt\r\n");
    term.resize(20, 4); // 备用屏期间 resize
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text == "a");
    feed(term, "\x1b[?1049l");
    ZZ_TEST_EXPECT(!term.isAlternateScreen());
    // 主屏长行已合并为 1 行
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).text == "p");
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(19).text == "e");
}

// 5. resize 返回值语义不变
static void testResizeReturn()
{
    ZzTerminal term(8, 4, ZzBackendKind::Native, 100);
    ZZ_TEST_EXPECT(!term.resize(8, 4));   // 尺寸未变
    ZZ_TEST_EXPECT(term.resize(10, 4));   // 列变
    ZZ_TEST_EXPECT(term.resize(10, 6));   // 纯行变
    ZZ_TEST_EXPECT(!term.resize(0, 4));   // 非法
}

int main()
{
    testHistoryReflow();
    testScreenReflowAndCursor();
    testCjkNotSplit();
    testAltScreenInterleave();
    testResizeReturn();
    if (g_failures == 0)
        std::printf("test_native_reflow: all passed\n");
    return g_failures;
}
```

注意：rowText 中 ZzCellView 的字段名以 RenderView.h 实际定义为准（text/width 等，参照 tests/unit/test_backend_compat.cpp 的用法）；若 facade 无逐格文本的便捷取法，改用 renderView 已有断言风格重写断言。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_reflow --output-on-failure`
预期：FAIL——历史行未重组（testHistoryReflow 的 foundMerged 为 false）

- [ ] **步骤 3：编写最少实现代码**

`src/backend/native/ZzNativeBackend.cpp` 的 resize 改为：

```cpp
bool ZzNativeBackend::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return false;
    const ZzSize old = screen_.size();
    if (old == ZzSize{cols, rows})
        return false;
    // M4：列变化触发 soft-wrap reflow，先历史后屏幕
    //（屏幕溢出行以新宽度经 ScrollOutCallback 回流到已重组的历史，宽度不变量自洽）。
    if (cols != old.cols) {
        scrollback_->reflow(cols);
        screen_.reflow(cols);
    }
    if (rows != old.rows)
        screen_.resize(cols, rows); // 行向语义维持 M0 现状（截断/填充/clamp）
    return true;
}
```

`include/ZzTerm/Terminal.h`：
- 构造函数默认参数 `std::size_t scrollbackMaxLines = 10000` 改为 `= 100000`，注释更新："历史容量上限（行），0 表示不保留历史；默认 10 万行（M4 benchmark 门控保障）"。
- resize 注释（:119 附近）重写：

```cpp
    /**
     * @brief 调整终端尺寸；列变化触发 soft-wrap reflow（M4 起）。
     * @param cols 新列数（> 0）。
     * @param rows 新行数（> 0）。
     * @return true 表示尺寸实际变化。
     * @note 列变化：屏幕区与 scrollback 历史一起重组（logical line 合并后
     *       按新列宽重切，宽字符不拆半，硬行截断/补空），光标跟随内容；
     *       行变化仅做网格增减，不触发 reflow。两后端语义对齐
     *       （Contour 经 allowReflowOnResize）。resize 后 RenderView 失效，
     *       前端需重新获取视图。
     */
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure`
预期：全量 PASS（含既有 28 项不回归；test_native_reflow 全绿）

- [ ] **步骤 5：Commit**

```bash
git add src/backend/native/ZzNativeBackend.cpp include/ZzTerm/Terminal.h tests/unit/test_native_reflow.cpp
git commit -m "feat(backend): M4 native resize reflow 协调（先历史后屏幕）与默认容量 10 万行"
```

---

### 任务 5：Contour 对齐 + compat 对照

**文件：**
- 修改：`src/backend/contour/ZzContourBackend.cpp`（Settings 显式钉住）
- 测试：`tests/unit/test_backend_compat.cpp`（扩充 resize reflow 用例）

- [ ] **步骤 1：编写失败的测试**（追加到 test_backend_compat.cpp）

```cpp
// N. resize reflow：长行历史随列宽重组，两后端屏幕逐格比对。
void testResizeReflow()
{
    Dual d;
    std::string longLine(200, 'x');
    d.feedBoth(longLine + "\r\n");
    for (int i = 0; i < 30; ++i)
        d.feedBoth("filler\r\n");
    d.native.resize(40, 24);
    d.contour.resize(40, 24);
    for (int r = 0; r < 24; ++r)
        checkRowEqual(d.native, d.contour, r, 40, "reflow-40");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
    d.native.resize(80, 24);
    d.contour.resize(80, 24);
    for (int r = 0; r < 24; ++r)
        checkRowEqual(d.native, d.contour, r, 80, "reflow-80");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
}

// N+1. CJK 长行 resize 后两后端逐格比对（宽字符边界规则对照）。
void testResizeReflowCjk()
{
    Dual d;
    std::string cjk;
    for (int i = 0; i < 45; ++i)
        cjk += "中文"; // 90 个宽字符共 180 列，80 列下折 3 行
    d.feedBoth(cjk + "\r\n");
    for (int i = 0; i < 30; ++i)
        d.feedBoth("filler\r\n");
    d.native.resize(37, 24); // 奇数列宽逼出宽字符边界钳制
    d.contour.resize(37, 24);
    for (int r = 0; r < 24; ++r)
        checkRowEqual(d.native, d.contour, r, 37, "reflow-cjk-37");
}
```

main() 注册两个新用例。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_backend_compat --output-on-failure`
预期：可能 FAIL（Contour 宽字符边界规则/光标映射细节与 native 分歧）

- [ ] **步骤 3：实现对齐与分歧处理**

`src/backend/contour/ZzContourBackend.cpp` Impl 构造函数中 settings 处加一行显式钉住：

```cpp
        settings.allowReflowOnResize = true; // M4：显式钉住（上游默认即 true），与 native reflow 语义对齐
```

运行 compat。若仍分歧：分析差异性质（宽字符边界钳制规则、光标跟随策略、历史参与范围），参照既有 b 类惯例在测试注释中钉住分歧并放宽对应断言（注释格式沿用该文件既有"分歧钉住"写法），并在任务报告中说明分歧点。**不得**为对齐去改 third_party/contour 上游代码。

- [ ] **步骤 4：运行测试验证通过**

运行：`ctest --preset linux-gcc-debug --output-on-failure`
预期：全量 PASS

- [ ] **步骤 5：Commit**

```bash
git add src/backend/contour/ZzContourBackend.cpp tests/unit/test_backend_compat.cpp
git commit -m "feat(contour): M4 显式钉住 allowReflowOnResize 并补 resize reflow 双后端对照"
```

---

### 任务 6：benchmark 门控 + JSON 落盘入库

**文件：**
- 创建：`tests/unit/test_perf_scrollback.cpp`
- 创建：`tests/perf/records/2026-09-20-m4-reflow.json`（测试运行产物拷贝入库）

- [ ] **步骤 1：编写测试（直接含门控断言）**

```cpp
// scrollback 性能门控与记录（M4）：10 万行 append/lineAt/reflow 耗时与内存上限。
// 结果写 cwd 下 zzterm-perf-scrollback.json；计划任务负责拷贝入 tests/perf/records/ 入库。
// 门控宽松（防回归绊线，非精确基准）；CI 机器慢 2-3 倍仍应通过。
#include <ZzTerm/Scrollback.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
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

static ZzLine makeFullLine(int cols, bool wrapped)
{
    // 满行 RGB 场景：每格带独立 RGB 前景（内存上限断言的最差情形）。
    ZzLine line(cols);
    for (int i = 0; i < cols; ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint(U'a' + (i % 26));
        c.setForeground(ZzColor::Rgb((std::uint8_t)i, (std::uint8_t)(i * 2), (std::uint8_t)(i * 3)));
        line.setCell(i, c);
    }
    line.setWrapped(wrapped);
    return line;
}

static double millisSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int main()
{
    constexpr std::size_t kLines = 100000;
    constexpr int kCols = 80;

    auto sb = zzCreateChunkedScrollback(kLines);

    // 1. append 吞吐：100 批 x 1000 行（每 3 行一条 wrapped 链）
    auto t0 = std::chrono::steady_clock::now();
    for (int batch = 0; batch < 100; ++batch) {
        std::vector<ZzLine> lines;
        lines.reserve(1000);
        for (int i = 0; i < 1000; ++i)
            lines.push_back(makeFullLine(kCols, (i % 3) != 2));
        sb->append(std::move(lines));
    }
    const double appendMs = millisSince(t0);
    ZZ_TEST_EXPECT(sb->lineCount() == kLines);
    ZZ_TEST_EXPECT(appendMs < 5000); // 门控宽松

    // 2. lineAt 随机访问 10 万次
    std::mt19937 rng(42);
    std::uniform_int_distribution<std::size_t> pick(0, kLines - 1);
    std::size_t checksum = 0;
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i)
        checksum += (std::size_t)sb->lineAt(pick(rng)).cellCount();
    const double lineAtMs = millisSince(t0);
    ZZ_TEST_EXPECT(checksum == 100000u * (std::size_t)kCols);
    ZZ_TEST_EXPECT(lineAtMs < 200);

    // 3. 内存门控：10 万行 x 80 列满行 RGB
    const std::size_t bytes = sb->stats().approxBytes;
    ZZ_TEST_EXPECT(bytes < 160u * 1024u * 1024u); // 理论约 133MB，留 20% 余量

    // 4. reflow 耗时：80 -> 120 -> 40（10 万行全量重组）
    t0 = std::chrono::steady_clock::now();
    sb->reflow(120);
    const double widenMs = millisSince(t0);
    ZZ_TEST_EXPECT(widenMs < 200);
    t0 = std::chrono::steady_clock::now();
    sb->reflow(40);
    const double narrowMs = millisSince(t0);
    ZZ_TEST_EXPECT(narrowMs < 200);

    // 5. JSON 落盘（cwd，ctest 下为 build/linux-gcc-debug/tests）
    std::ofstream js("zzterm-perf-scrollback.json");
    js << "{\n"
       << "  \"milestone\": \"M4\",\n"
       << "  \"lines\": " << kLines << ",\n"
       << "  \"cols\": " << kCols << ",\n"
       << "  \"append_ms\": " << appendMs << ",\n"
       << "  \"lineat_ms\": " << lineAtMs << ",\n"
       << "  \"reflow_widen_ms\": " << widenMs << ",\n"
       << "  \"reflow_narrow_ms\": " << narrowMs << ",\n"
       << "  \"approx_bytes\": " << bytes << "\n"
       << "}\n";

    std::printf("append %.1fms, lineAt %.1fms, widen %.1fms, narrow %.1fms, bytes %zu\n",
                appendMs, lineAtMs, widenMs, narrowMs, bytes);
    if (g_failures == 0)
        std::printf("test_perf_scrollback: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 2：运行并拷贝记录入库**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug -R test_perf_scrollback --output-on-failure
mkdir -p tests/perf/records
cp build/linux-gcc-debug/tests/zzterm-perf-scrollback.json tests/perf/records/2026-09-20-m4-reflow.json
```

预期：测试 PASS，JSON 字段齐全

- [ ] **步骤 3：Commit**

```bash
git add tests/unit/test_perf_scrollback.cpp tests/perf/records/2026-09-20-m4-reflow.json
git commit -m "test(history): M4 scrollback 性能门控（10 万行 append/lineAt/reflow/内存）与记录落盘"
```

---

### 任务 7：冒烟 resize 步骤 + 文档收尾 + 全量回归

**文件：**
- 修改：`tests/interactive/verify_smoke.py`（新增 resize reflow 步骤）
- 修改：`include/ZzTerm/Screen.h`、`include/ZzTerm/Scrollback.h`、`include/ZzTerm/Line.h`（M4 预留措辞扫尾）
- 修改：`docs/Architecture.md`（§19 M4 勾选、§6/§14 相关段落现状化）
- 修改：`docs/API.md`（resize 语义节）
- 修改：`docs/VT-Xterm-Checklist.md`（第 13 项 resize/reflow 状态）

- [ ] **步骤 1：冒烟新增 reflow 步骤**

在 verify_smoke.py 既有步骤 5（SIGWINCH，屏幕已是 20x100）之后插入：

```python
    # 5b. resize reflow（M4）：200 字符长行在 100 列占 2 行，50 列应重排为 4 行
    marker = "zzreflow" + "x" * 192
    child.sendline(f"printf '%s\\n' '{marker}'")
    settle(child, stream)
    snap = snapshot(screen, "reflow-before")
    check(marker in "".join(line.rstrip() for line in screen.display),
          "5b.reflow基线(100列)", f"(快照 {snap})")
    child.setwinsize(20, 50)
    child.sendline("stty size")  # 触发重绘确认尺寸同步
    settle(child, stream)
    screen.resize(20, 50)
    snap = snapshot(screen, "reflow-narrow-50")
    check(marker in "".join(line.rstrip() for line in screen.display),
          "5c.reflow窄化重排(50列)", f"(快照 {snap})")
    child.setwinsize(20, 100)
    child.sendline("stty size")
    settle(child, stream)
    screen.resize(20, 100)
    snap = snapshot(screen, "reflow-back-100")
    check(marker in "".join(line.rstrip() for line in screen.display),
          "5d.reflow变宽合并(回100列)", f"(快照 {snap})")
```

同时更新文件头 docstring 追加一行 M4 说明。若 Contour 后端在该步骤失败：先确认是 demo 渲染层问题还是 Core reflow 分歧，Core 分歧回任务 5 的 compat 注释钉住流程。

- [ ] **步骤 2：文档扫尾**

- `include/ZzTerm/Line.h:71` resize() 注释中"reflow 由 Screen/Terminal 在 resize 流程中负责"改为"reflow 由 ZzScreen::reflow / zzReflowLines 承担（M4 已落地）"；
- `docs/Architecture.md` §19 的 M4 行（约 :252）更新为已完成状态（10 万行 scrollback 与 reflow 第一版；selection/copy/search/highlight 移入 M5 表述）；§6 或 §14 中关于"reflow 属 M4 预留"的措辞现状化；
- `docs/API.md`：resize 一节补充 reflow 语义（列变重组/行变不重组/光标跟随/RenderView 失效）与默认容量 10 万；
- `docs/VT-Xterm-Checklist.md` 第 13 项（约 :292 resize/reflow）标记为已实现（双后端）；
- 全部文档遵守 doxygen 陷阱约定。

- [ ] **步骤 3：全量回归**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check --output-on-failure
doxygen Doxyfile
```

预期：三配置全绿（ON 含新增 5 个测试 + 冒烟新步骤）、doxygen exit 0 零警告。
注意：OFF/shared 构建目录若无本分支新文件的 GLOB 收编，需先各自重新 cmake configure（`cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF`；shared 目录同理按其原配置）。

- [ ] **步骤 4：Commit**

```bash
git add tests/interactive/verify_smoke.py include/ZzTerm/Screen.h include/ZzTerm/Scrollback.h include/ZzTerm/Line.h docs/Architecture.md docs/API.md docs/VT-Xterm-Checklist.md
git commit -m "docs(m4): reflow 冒烟步骤与 M4 文档现状化（Architecture/API/Checklist）"
```

---

## 自检记录

- 规格覆盖：5.1 → 任务 1；5.2 → 任务 2；5.3 → 任务 3；5.4 → 任务 4；5.5 → 任务 5；5.6 → 任务 6（默认容量在任务 4 的 Terminal.h 修改中一并落地）；5.7 测试 → 任务 1/2/3/4/5/7；5.8 错误处理 → 各任务非法参数分支；5.9 验收 → 任务 7 步骤 3。
- 类型一致性：zzReflowLines（by-value 签名）、ZzReflowCursor、ZzScreen::reflow、ZzScrollback::reflow、totalAppended/totalDropped 在各任务间一致。
- 已知留给实现者的微调点：任务 1 兜底分支的 lastContentCol 时序（注释已说明）；任务 4 断言中 ZzCellView 字段名以 RenderView.h 为准。
