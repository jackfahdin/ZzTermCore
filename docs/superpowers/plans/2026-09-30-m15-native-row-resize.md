# M15 native 行变 resize 语义对齐（缩行压历史 / 扩行回抽）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** native 后端行变 resize 语义对齐 contour/xterm——缩行先裁光标下方、不够再压顶行入历史；扩行光标贴底时从历史回抽，根治「resize 截断拉大不恢复」（M13 spike 现象 5/7）。

**架构：** 回调对偶——ZzScreen 新增 HistoryPullCallback（历史到屏幕，与既有 ScrollOutCallback 对偶），行变逻辑收进 ZzScreen::resize，ZzNativeBackend 构造时安装为 scrollback takeNewest；ZzScrollback 接口新增 takeNewest（尾块向头块取行）。Alternate 缓冲无回调路径，维持尾部截断/补空。

**技术栈：** C++20、CMake（tests GLOB 自动收编新测试文件）。

**规格：** docs/superpowers/specs/2026-09-30-m15-native-row-resize-design.md（已审定）

**contour 参照语义（fork Grid.cpp 实证，规格钉住的基准）：**
- 缩行 shrinkLines（Grid.cpp:784-828）：`cutoffCount = min(缩行数, 光标下方行数)` 从底部裁（不入历史，光标下方=全部下方行、不限空行）；剩余 `numLinesToPushUp = 缩行数 - cutoff` 仅在光标贴末行时把顶部行压入历史（Require 光标在末行——cutoff 裁完后自然成立），光标行号 -= pushUp。
- 扩行 growLines（Grid.cpp:736-772）：仅当光标在末行时 `min(扩行数, 历史行数)` 从最新历史回抽注入顶部，光标行号 += 回抽数；其余底部补空。

**基线命令（ZzTermCore 仓根目录）：**
- `cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`（现状 52/52）
- `ctest --test-dir build/m2-off-check`（41/41）、`ctest --test-dir build/m2-shared-check`（52/52）
- `doxygen Doxyfile`（exit 0 零警告）

---

## 文件结构

**创建：**
- `tests/unit/test_scrollback_takenewest.cpp` — takeNewest 单测（GLOB 自动收编）
- `tests/unit/test_screen_rowresize.cpp` — ZzScreen 行变条件语义单测（GLOB 自动收编）
- `tests/unit/test_native_rowresize.cpp` — facade 级行变行为 + M14 契约联动（GLOB 自动收编）

**修改：**
- `include/ZzTerm/Scrollback.h`（reflow 声明 :94 后）— takeNewest 接口声明
- `src/history/ChunkedScrollback.cpp` — takeNewest 实现（含 <iterator> include）
- `include/ZzTerm/Screen.h`（ScrollOutCallback :59 后、setter :370 后、私有区）— HistoryPullCallback 类型/setter/成员/resizeBuffer 私有方法/resize 文档更新
- `src/screen/Screen.cpp` — ZzScreen::resize 重构为 resizeBuffer 双缓冲分发 + 行变条件语义
- `src/backend/native/ZzNativeBackend.cpp`（构造回调安装区 :205-210 后）— pull 回调接线
- `include/ZzTerm/Terminal.h`（resize 文档 :115-123）— 行变语义文档更新
- `tests/unit/test_backend_compat.cpp`（用例 9 :206-215 后）— 行变 parity 对照用例
- `docs/API.md`、`docs/superpowers/specs/2026-09-29-m13-spike-record.md`、`docs/superpowers/specs/2026-09-30-m14-history-view-design.md` — 任务 3 文档收尾

---

## 任务 1：ZzScrollback 新增 takeNewest

**文件：**
- 修改：`include/ZzTerm/Scrollback.h`（reflow 声明 :94 与 capacity 声明 :96 之间插入）
- 修改：`src/history/ChunkedScrollback.cpp`（include 区 :1-8、reflow 实现 :116 后插入）
- 测试：`tests/unit/test_scrollback_takenewest.cpp`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_scrollback_takenewest.cpp`：

```cpp
// ZzScrollback::takeNewest 单测（M15）：基本取行顺序、跨块、超取与取空、
// stats 语义（回抽非裁剪）、headOffset 复位。
#include <cstdio>
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

namespace {

// 构造 cols 列、首格码位为 cp 的行（cp 须非 0，0x100 + i 避让）。
ZzLine makeLine(int cols, char32_t cp)
{
    ZzLine line;
    line.resize(cols);
    ZzCell cell;
    cell.setWidth(ZzCellWidth::Narrow);
    cell.setCodePoint(cp);
    line.setCell(0, cell);
    return line;
}

std::vector<ZzLine> makeBatch(int cols, int count, char32_t base)
{
    std::vector<ZzLine> batch;
    for (int i = 0; i < count; ++i)
        batch.push_back(makeLine(cols, base + static_cast<char32_t>(i)));
    return batch;
}

} // namespace

static void testBasicOrderAndCount()
{
    auto sb = zzCreateChunkedScrollback(100);
    sb->append(makeBatch(10, 5, 0x100)); // a..e（码位 0x100..0x104）
    auto taken = sb->takeNewest(2);
    ZZ_TEST_EXPECT(taken.size() == 2);
    ZZ_TEST_EXPECT(taken[0].cellAt(0).codePoint() == 0x103); // 旧到新：d 在前
    ZZ_TEST_EXPECT(taken[1].cellAt(0).codePoint() == 0x104); // e 在后
    ZZ_TEST_EXPECT(sb->lineCount() == 3);
    ZZ_TEST_EXPECT(sb->lineAt(2).cellAt(0).codePoint() == 0x102); // 留存最新为 c
}

static void testCrossChunk()
{
    auto sb = zzCreateChunkedScrollback(1000);
    sb->append(makeBatch(10, 300, 0x100)); // 跨块（256 + 44）
    auto taken = sb->takeNewest(50);       // 尾块 44 行 + 前块 6 行
    ZZ_TEST_EXPECT(taken.size() == 50);
    ZZ_TEST_EXPECT(taken[0].cellAt(0).codePoint() == 0x100 + 250); // 全局旧到新
    ZZ_TEST_EXPECT(taken[49].cellAt(0).codePoint() == 0x100 + 299);
    ZZ_TEST_EXPECT(sb->lineCount() == 250);
    ZZ_TEST_EXPECT(sb->lineAt(0).cellAt(0).codePoint() == 0x100);
    ZZ_TEST_EXPECT(sb->lineAt(249).cellAt(0).codePoint() == 0x100 + 249);
}

static void testTakeMoreThanAvailableAndEmpty()
{
    auto sb = zzCreateChunkedScrollback(100);
    sb->append(makeBatch(10, 5, 0x100));
    auto taken = sb->takeNewest(10); // 超取：全部返回
    ZZ_TEST_EXPECT(taken.size() == 5);
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    ZZ_TEST_EXPECT(sb->takeNewest(3).empty()); // 取空
    // 取空后再 append：寻址正常（headOffset_ 复位路径）
    sb->append(makeBatch(10, 1, 0x1FF));
    ZZ_TEST_EXPECT(sb->lineCount() == 1);
    ZZ_TEST_EXPECT(sb->lineAt(0).cellAt(0).codePoint() == 0x1FF);
}

static void testStatsUnchanged()
{
    auto sb = zzCreateChunkedScrollback(100);
    sb->append(makeBatch(10, 5, 0x100));
    const auto before = sb->stats();
    sb->takeNewest(2);
    const auto after = sb->stats();
    ZZ_TEST_EXPECT(after.totalAppended == before.totalAppended); // 只增计数不动
    ZZ_TEST_EXPECT(after.totalDropped == before.totalDropped);   // 回抽非裁剪
    ZZ_TEST_EXPECT(after.lineCount == 3);
    ZZ_TEST_EXPECT(after.approxBytes < before.approxBytes);      // 字节数按行扣减
}

static void testHeadOffsetReset()
{
    auto sb = zzCreateChunkedScrollback(3); // 容量 3，触发部分裁剪产生 headOffset_
    sb->append(makeBatch(10, 5, 0x100));    // 留存 c,d,e；headOffset_=2
    ZZ_TEST_EXPECT(sb->stats().totalDropped == 2);
    auto taken = sb->takeNewest(3); // 全取空（单块且带 headOffset_）
    ZZ_TEST_EXPECT(taken.size() == 3);
    ZZ_TEST_EXPECT(taken[0].cellAt(0).codePoint() == 0x102); // c
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    sb->append(makeBatch(10, 2, 0x200)); // 复位后再 append：寻址正常
    ZZ_TEST_EXPECT(sb->lineCount() == 2);
    ZZ_TEST_EXPECT(sb->lineAt(0).cellAt(0).codePoint() == 0x200);
    ZZ_TEST_EXPECT(sb->lineAt(1).cellAt(0).codePoint() == 0x201);
}

int main()
{
    testBasicOrderAndCount();
    testCrossChunk();
    testTakeMoreThanAvailableAndEmpty();
    testStatsUnchanged();
    testHeadOffsetReset();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_scrollback_takenewest 2>&1 | tail -5`
预期：编译失败，报错含 `takeNewest` 不是 ZzScrollback 的成员。

- [ ] **步骤 3：接口声明（Scrollback.h）**

`include/ZzTerm/Scrollback.h` 在 `reflow` 声明（:94）后、`capacity` 声明（:96）前插入：

```cpp
    /**
     * @brief 从最新端取走最多 n 行并删除（Core 内部使用；M15 行变回抽原语）。
     * @param n 最多取走行数。
     * @return 取走的行（旧到新顺序、以值移交所有权），不足 n 行时全部返回。
     * @note 调用后既有 lineAt 引用失效（同 append 的失效规则）。
     * @note stats 语义：本操作不是容量裁剪——totalDropped 不变；totalAppended
     *       只增不改（绝对行号产生回退空洞，与 contour rotateBuffersRight 的
     *       stableBase 回退同构，选区锚点按不透明行号处理）。
     */
    [[nodiscard]] virtual std::vector<ZzLine> takeNewest(std::size_t n) = 0;
```

同时把类注释（:43-44）的失效规则列举从「append/clear/setCapacity/reflow」扩为「append/clear/setCapacity/reflow/takeNewest」。

- [ ] **步骤 4：ChunkedScrollback 实现**

`src/history/ChunkedScrollback.cpp` 两处。其一，include 区（`#include <deque>` 后）追加：

```cpp
#include <iterator>
```

其二，`reflow` 实现（:116 结束）后插入：

```cpp
    [[nodiscard]] std::vector<ZzLine> takeNewest(std::size_t n) override
    {
        std::vector<ZzLine> out;
        out.reserve(n);
        while (out.size() < n && !chunks_.empty()) {
            auto& back = chunks_.back();
            const std::size_t take  = std::min(n - out.size(), back.size());
            const std::size_t first = back.size() - take;
            // 段内旧到新；先取到的是更新的段，整段前插维持全局旧到新。
            std::vector<ZzLine> seg;
            seg.reserve(take);
            for (std::size_t i = first; i < back.size(); ++i) {
                approxBytes_ -= sizeof(ZzLine) +
                                static_cast<std::size_t>(back[i].cellCount()) * sizeof(ZzCell);
                seg.push_back(std::move(back[i]));
            }
            back.erase(back.begin() + static_cast<std::ptrdiff_t>(first), back.end());
            totalLines_ -= take;
            if (back.empty()) {
                chunks_.pop_back();
                if (chunks_.empty())
                    headOffset_ = 0; // 全取空：复位部分裁剪槽位计数
            }
            seg.insert(seg.end(), std::make_move_iterator(out.begin()),
                       std::make_move_iterator(out.end()));
            out = std::move(seg);
        }
        return out;
    }
```

实现要点（审查锚点）：尾部物理弹出不动 headOffset_ 定长寻址（死槽物理擦除由 trimToCapacity 负责，头部操作与尾部无关）；单块（头块即尾块）全取空时必须复位 headOffset_，否则后续 append 的 lineAt 寻址错乱；approxBytes_ 按 append 同一公式逐行扣。

- [ ] **步骤 5：构建并运行新测试**

运行：
```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_scrollback_takenewest
./build/linux-gcc-debug/tests/test_scrollback_takenewest
```
预期：编译通过，退出码 0（无 FAIL 行）。

- [ ] **步骤 6：基线 + doxygen + Commit**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
doxygen Doxyfile
git add include/ZzTerm/Scrollback.h src/history/ChunkedScrollback.cpp tests/unit/test_scrollback_takenewest.cpp
git commit -m "feat(scrollback): M15 takeNewest 行变回抽原语"
```
预期：53/53（52+1 新测试）；doxygen exit 0。

---

## 任务 2：ZzScreen 行变条件语义 + HistoryPullCallback

**文件：**
- 修改：`include/ZzTerm/Screen.h`（ScrollOutCallback 类型 :59 后、resize 文档 :74-81、setter :370 后、私有区 :387-411）
- 修改：`src/screen/Screen.cpp`（ZzScreen::resize :43-59 重构、新增 resizeBuffer）
- 测试：`tests/unit/test_screen_rowresize.cpp`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_screen_rowresize.cpp`（writeRow/rowText/makeTextLine 帮手与 test_screen_reflow.cpp 同款——测试文件各自自带帮手是本仓既有约定）：

```cpp
// ZzScreen 行变 resize 条件语义测试（M15）：缩行裁光标下方 / pushUp 压历史、
// 扩行回抽 / 非末行补空、无回调降级、Alternate 无历史路径。
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

// 1. 缩行先裁光标下方行（不入历史）
static void testShrinkCutsBelowCursor()
{
    ZzScreen scr(10, 6);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    for (int r = 0; r < 6; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{2, 1}); // 光标行 2，下方 3 行
    scr.resize(10, 4);                       // 缩 2：全部从光标下方裁（r4/r5）
    ZZ_TEST_EXPECT(spilled.empty());
    ZZ_TEST_EXPECT(scr.size().rows == 4);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "r3");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 2);
}

// 2. 缩行 pushUp：光标贴底时顶行压入历史
static void testShrinkPushUpToCallback()
{
    ZzScreen scr(10, 4);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    for (int r = 0; r < 4; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{3, 1}); // 光标贴底，下方 0 行
    scr.resize(10, 2);                       // 缩 2：pushUp 顶两行 r0/r1
    ZZ_TEST_EXPECT(spilled.size() == 2);
    ZZ_TEST_EXPECT(rowText(spilled[0], 2) == "r0");
    ZZ_TEST_EXPECT(rowText(spilled[1], 2) == "r1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "r3");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1); // 3 - 2 = 1
}

// 3. 缩行混合：先裁下方再 pushUp
static void testShrinkMixed()
{
    ZzScreen scr(10, 6);
    std::vector<ZzLine> spilled;
    scr.setScrollOutCallback([&](std::vector<ZzLine> lines) {
        for (auto& l : lines)
            spilled.push_back(std::move(l));
    });
    for (int r = 0; r < 6; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{4, 1}); // 下方 1 行
    scr.resize(10, 3);                       // 缩 3：裁下方 1（r5）+ pushUp 2（r0/r1）
    ZZ_TEST_EXPECT(spilled.size() == 2);
    ZZ_TEST_EXPECT(rowText(spilled[0], 2) == "r0");
    ZZ_TEST_EXPECT(rowText(spilled[1], 2) == "r1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "r4");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 2); // 4 - 2 = 2
}

// 4. 缩行无回调时 pushUp 丢弃（同 reflow 溢出「Primary 且回调存在才上移」语义）
static void testShrinkPushUpDroppedWithoutCallback()
{
    ZzScreen scr(10, 4); // 不装回调
    for (int r = 0; r < 4; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{3, 1});
    scr.resize(10, 2);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r2"); // 内容位移同 pushUp，行被丢弃
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
}

// 5. 扩行回抽：光标贴底 + pull 回调
static void testGrowPullsBack()
{
    ZzScreen scr(10, 2);
    writeRow(scr, 0, "r2");
    writeRow(scr, 1, "r3");
    scr.setCursorPosition(ZzPosition{1, 0}); // 贴底
    scr.setHistoryPullCallback([](std::size_t maxLines) {
        std::vector<ZzLine> pulled;
        ZZ_TEST_EXPECT(maxLines == 2); // 索取数 = 扩行数
        pulled.push_back(makeTextLine(10, "r0")); // 旧到新
        pulled.push_back(makeTextLine(10, "r1"));
        return pulled;
    });
    scr.resize(10, 4); // 扩 2：回抽 2 行注入顶部
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "r1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(2), 2) == "r2");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "r3");
    ZZ_TEST_EXPECT(scr.cursor().position.row == 3); // 1 + 2 = 3
}

// 6. 扩行光标不在末行：不调回调、底部补空
static void testGrowNoPullWhenCursorNotLast()
{
    ZzScreen scr(10, 3);
    for (int r = 0; r < 3; ++r)
        writeRow(scr, r, "r" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{0, 0}); // 不在末行
    bool pullCalled = false;
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.resize(10, 5);
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(4), 2) == "  "); // 底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 0);
}

// 7. 扩行回调返回空（无历史可取）：底部补空
static void testGrowEmptyPull()
{
    ZzScreen scr(10, 2);
    writeRow(scr, 0, "r0");
    writeRow(scr, 1, "r1");
    scr.setCursorPosition(ZzPosition{1, 0}); // 贴底
    scr.setHistoryPullCallback([](std::size_t) { return std::vector<ZzLine>{}; });
    scr.resize(10, 4);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "r0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "  "); // 底部补空
    ZZ_TEST_EXPECT(scr.cursor().position.row == 1);
}

// 8. Alternate 缓冲：缩行尾部截断不压历史、扩行底部补空不回抽
static void testAlternateNoHistoryPath()
{
    ZzScreen scr(10, 4);
    bool scrollOutCalled = false;
    bool pullCalled = false;
    scr.setScrollOutCallback([&](std::vector<ZzLine>) { scrollOutCalled = true; });
    scr.setHistoryPullCallback([&](std::size_t) {
        pullCalled = true;
        return std::vector<ZzLine>{};
    });
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    for (int r = 0; r < 4; ++r)
        writeRow(scr, r, "a" + std::to_string(r));
    scr.setCursorPosition(ZzPosition{3, 0});
    scr.resize(10, 2); // alt 缩行：尾部截断（a2/a3 丢弃），不压历史
    ZZ_TEST_EXPECT(!scrollOutCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(0), 2) == "a0");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "a1");
    scr.resize(10, 4); // alt 扩行：底部补空，不回抽
    ZZ_TEST_EXPECT(!pullCalled);
    ZZ_TEST_EXPECT(rowText(scr.lineAt(1), 2) == "a1");
    ZZ_TEST_EXPECT(rowText(scr.lineAt(3), 2) == "  ");
}

int main()
{
    testShrinkCutsBelowCursor();
    testShrinkPushUpToCallback();
    testShrinkMixed();
    testShrinkPushUpDroppedWithoutCallback();
    testGrowPullsBack();
    testGrowNoPullWhenCursorNotLast();
    testGrowEmptyPull();
    testAlternateNoHistoryPath();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_screen_rowresize 2>&1 | tail -5`
预期：编译失败，报错含 `setHistoryPullCallback` 不是 ZzScreen 的成员。

- [ ] **步骤 3：Screen.h 接口（四处）**

其一，`ScrollOutCallback` 类型声明（:59）后插入：

```cpp
    /**
     * @brief 历史回抽回调（M15）。行变扩行且光标贴末行时，ZzScreen 经本回调
     *        向历史后端索取最多 maxLines 行最新历史（旧到新顺序、以值移交
     *        所有权）注入屏幕顶部；无历史可取时返回空向量。
     *        仅为 Primary 缓冲区调用；Alternate 扩行永不触发。
     */
    using HistoryPullCallback = std::function<std::vector<ZzLine>(std::size_t maxLines)>;
```

其二，`resize` 文档注释（:74-81）整段替换为：

```cpp
    /**
     * @brief 网格级 resize 原语：行变按条件语义搬行（M15），列向逐行截断/填充。
     * @param cols 新列数（> 0）。
     * @param rows 新行数（> 0）。
     * @note 行变语义（对齐 contour shrinkLines/growLines）：缩行先裁光标下方
     *       行（不入历史），不够裁时把 Primary 顶部行经 ScrollOutCallback
     *       压入历史（无回调则丢弃，同 reflow 溢出语义），光标随内容平移；
     *       扩行仅当光标贴末行时经 HistoryPullCallback 从最新历史回抽注入
     *       顶部，不足部分底部补空。Alternate 缓冲无回调路径：尾部截断/补空。
     * @note 不做列向 reflow；列变化的 soft-wrap reflow 由 reflow() 原语承担，
     *       ZzNativeBackend::resize 协调顺序（先历史后屏幕，M4 已落地）。全屏标脏。
     */
    void resize(int cols, int rows);
```

其三，`setScrollOutCallback`（:370）后插入：

```cpp
    /**
     * @brief 设置历史回抽回调（由 ZzTerminal/backend 安装，M15）。
     * @param callback 回调；传空表示扩行不回抽（底部补空）。
     */
    void setHistoryPullCallback(HistoryPullCallback callback);
```

其四，私有区：`reflowBuffer` 声明（:388）后加一行方法声明，成员区 `scrollOutCallback_`（:411）后加一行成员：

```cpp
    /// @brief 单套缓冲区的 resize 实现（M15 行变条件语义 + 列向截断/填充）。
    void resizeBuffer(Buffer& buf, int cols, int rows, bool mayUseHistory);
```

```cpp
    HistoryPullCallback historyPullCallback_;
```

- [ ] **步骤 4：Screen.cpp 实现**

其一，`ZzScreen::resize`（:43-59）整函数替换为：

```cpp
void ZzScreen::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return;
    resizeBuffer(primary_, cols, rows, true);    // M15：Primary 行变条件语义
    resizeBuffer(alternate_, cols, rows, false); // Alternate 无历史：尾部截断/补空
    cols_ = cols;
    rows_ = rows;
    tabStops_.resize(static_cast<std::size_t>(cols), 0);
    scrollTop_ = 0;
    scrollBottom_ = rows - 1;
    ++dirtyGeneration_;
    markAllDirty();
}

// M15：行变条件语义（对齐 contour shrinkLines/growLines，规格 §4）。
// 缩行：先裁光标下方行（不入历史），不够裁时顶部行经 ScrollOutCallback
// 压入历史（无回调则丢弃，同 reflowBuffer 溢出语义）；光标随内容平移。
// 扩行：光标贴末行时经 HistoryPullCallback 回抽注入顶部，不足底部补空。
void ZzScreen::resizeBuffer(Buffer& buf, int cols, int rows, bool mayUseHistory)
{
    const int oldRows = static_cast<int>(buf.lines.size());
    if (rows < oldRows) {
        const int k = oldRows - rows;
        if (mayUseHistory) {
            const int below  = oldRows - 1 - buf.cursor.position.row;
            const int cutoff = std::min(k, below); // 光标下方行直接裁（不入历史）
            buf.lines.erase(buf.lines.end() - cutoff, buf.lines.end());
            const int pushUp = k - cutoff; // 不足部分顶部压入历史（此时光标必贴底）
            if (pushUp > 0) {
                if (scrollOutCallback_) {
                    std::vector<ZzLine> spilled;
                    spilled.reserve(static_cast<std::size_t>(pushUp));
                    for (int i = 0; i < pushUp; ++i)
                        spilled.push_back(std::move(buf.lines[static_cast<std::size_t>(i)]));
                    scrollOutCallback_(std::move(spilled));
                }
                buf.lines.erase(buf.lines.begin(), buf.lines.begin() + pushUp);
                buf.cursor.position.row -= pushUp;
            }
        } else {
            buf.lines.resize(static_cast<std::size_t>(rows)); // Alternate：尾部截断
        }
    } else if (rows > oldRows) {
        const int k = rows - oldRows;
        if (mayUseHistory && historyPullCallback_
            && buf.cursor.position.row == oldRows - 1) { // 光标贴末行才回抽
            auto pulled = historyPullCallback_(static_cast<std::size_t>(k));
            if (!pulled.empty()) {
                buf.lines.insert(buf.lines.begin(),
                                 std::make_move_iterator(pulled.begin()),
                                 std::make_move_iterator(pulled.end()));
                buf.cursor.position.row += static_cast<int>(pulled.size());
            }
        }
        buf.lines.resize(static_cast<std::size_t>(rows)); // 不足部分底部补空
    }
    // 列向：逐行截断/填充（无 reflow，维持 M0 语义）；新补空行同获列宽。
    for (auto& line : buf.lines)
        if (line.cellCount() != cols)
            line.resize(cols);
    dirtyRows.assign(buf.lines.size(), 1);
    dirtyRanges.assign(buf.lines.size(), ZzCellRange{0, cols});
    buf.cursor.position.row = std::clamp(buf.cursor.position.row, 0, rows - 1);
    buf.cursor.position.col = std::clamp(buf.cursor.position.col, 0, cols - 1);
    buf.wrapPending = false;
}
```

其二，`setScrollOutCallback` 实现旁（文件后部，按既有 setter 位置）追加：

```cpp
void ZzScreen::setHistoryPullCallback(HistoryPullCallback callback)
{
    historyPullCallback_ = std::move(callback);
}
```

实现要点（审查锚点）：Buffer::resize（:11-22）保留不动，继续服务构造函数（:31-32）的纯网格初始化；resizeBuffer 是行变语义的唯一落点；`std::make_move_iterator` 需要 `#include <iterator>`（Screen.cpp include 区追加）；`std::min` 已有 <algorithm>。

- [ ] **步骤 5：构建并运行新测试**

运行：
```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_screen_rowresize
./build/linux-gcc-debug/tests/test_screen_rowresize
```
预期：编译通过，退出码 0。若 testShrinkMixed/testGrowPullsBack 失败，先核对自己对 cutoff/pushUp/回抽数的推演（contour 语义见计划头部参照段），不要盲目改期望值。

- [ ] **步骤 6：全量回归 + Commit**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
doxygen Doxyfile
git add include/ZzTerm/Screen.h src/screen/Screen.cpp tests/unit/test_screen_rowresize.cpp
git commit -m "feat(screen): M15 行变 resize 条件语义与 HistoryPullCallback"
```
预期：54/54（53+1）；既有用例零回归——三个重点推演：① test_terminal_core 的 testRestoreCursorClampedAfterResize 在新语义下断言应仍然通过（term(10,6) 光标 row 5、resize(4,4) 缩 2、below=0、pushUp=2 顶两行入历史、cursor.row=5-2=3、clamp 后 row==3/col==3 不变）；② test_native_reflow 用例 4（1049 期间 resize(20,4)）行数 4→4 无行变，不受影响；用例 5 的纯行变 resize(10,6) 光标不在末行且无 pull 回调（backend 接线在任务 3），底部补空行为不变；③ test_backend_compat 用例 18/19（resize reflow 对照）若因行变新语义出现分歧，属预期演进方向（native 向 contour 对齐），按推演重钉断言而不是回退实现。若失败先复核推演再查实现；doxygen exit 0。

---

## 任务 3：backend 接线 + facade/compat 测试 + 文档收尾

**文件：**
- 修改：`src/backend/native/ZzNativeBackend.cpp`（构造回调安装区 :205-210 后）
- 修改：`include/ZzTerm/Terminal.h`（resize 文档 :115-123）
- 修改：`tests/unit/test_backend_compat.cpp`（用例 9 :206-215 后新增用例 + main 注册）
- 测试：`tests/unit/test_native_rowresize.cpp`
- 修改：`docs/API.md`、`docs/superpowers/specs/2026-09-29-m13-spike-record.md`、`docs/superpowers/specs/2026-09-30-m14-history-view-design.md`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_native_rowresize.cpp`：

```cpp
// M15：native 行变 resize 的 facade 级行为——缩行压历史/扩行回抽还原、
// 裁光标下方不动历史、非末行不回抽、Alternate 期间主屏按缓冲区分、
// M14 historyView 契约联动（lineCount/generation）。
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

// 喂 8 行（a..h 各带 CRLF）进 10x4 终端：滚出 a..e 共 5 行进历史，
// 屏幕 f/g/h（行 0..2），光标行 3（空行）。
static void feedEightLines(ZzTerminal& term)
{
    for (char c = 'a'; c <= 'h'; ++c) {
        std::string s;
        s += c;
        s += "\r\n";
        feedStr(term, s);
    }
}

// 1. 缩行压历史 + 扩行回抽还原（「resize 截断拉大不恢复」根治钉住）
static void testShrinkPushAndGrowPull()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);

    ZZ_TEST_EXPECT(term.resize(10, 3)); // 缩 1：光标贴底，f 压入历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 6);
    ZZ_TEST_EXPECT(historyLineText(term, 5) == "f");
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "g");
    ZZ_TEST_EXPECT(term.cursor().position.row == 2);

    const std::uint64_t g0 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(10, 4)); // 扩 1：光标贴底（行 2=末行）回抽 f
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "f");
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0); // 回抽代计数递增
}

// 2. 缩行裁光标下方：历史不变
static void testShrinkCutsBelowCursorKeepsHistory()
{
    ZzTerminal term(10, 6, ZzBackendKind::Native, 100);
    feedStr(term, "top\r\n");
    feedStr(term, "mid\r\n"); // 光标行 2，下方 3 行空行
    ZZ_TEST_EXPECT(term.resize(10, 4)); // 缩 2：全部从光标下方裁
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "mid");
    ZZ_TEST_EXPECT(term.cursor().position.row == 2);
}

// 3. 光标不在末行时扩行不回抽（底部补空）
static void testGrowNoPullWhenCursorNotLast()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    feedStr(term, "\x1b[1;1H"); // CUP 回行 0（不在末行）
    ZZ_TEST_EXPECT(term.resize(10, 6)); // 扩 2：不回抽
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "f");
}

// 4. Alternate 期间 resize：主屏按缓冲区分照样压历史（规格 §3）
static void testAlternateResizePrimaryStillPushes()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term); // 主屏历史 5（a..e），屏幕 f/g/h，光标行 3
    feedStr(term, "\x1b[?1049h");
    feedStr(term, "alt\r\n");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // Alternate 恒 0（M14 契约）
    ZZ_TEST_EXPECT(term.resize(10, 2)); // alt 期间缩 2：主屏网格 pushUp f/g
    feedStr(term, "\x1b[?1049l");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 7); // 5 + f/g 两行
    ZZ_TEST_EXPECT(historyLineText(term, 6) == "g");
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "h"); // 主屏余 h 与光标行
}

int main()
{
    testShrinkPushAndGrowPull();
    testShrinkCutsBelowCursorKeepsHistory();
    testGrowNoPullWhenCursorNotLast();
    testAlternateResizePrimaryStillPushes();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_native_rowresize 2>&1 | tail -5`
预期：编译通过但用例 1/4 FAIL——backend 未装 pull 回调时扩行底部补空（回抽断言失败），且缩行压历史后 lineCount 方向性断言部分已能过（push 回调 M0 已装）；逐条记录失败形态。

- [ ] **步骤 3：ZzNativeBackend 装 pull 回调**

`src/backend/native/ZzNativeBackend.cpp` 构造函数滚出回调安装（:205-210）后追加：

```cpp
    // M15：扩行回抽回调——Screen 经此从 scrollback 取最新行注入屏幕顶部；
    // 实取非空时历史可见行数减少，代计数递增（M14「不得漏增」）。
    screen_.setHistoryPullCallback([this](std::size_t maxLines) {
        auto pulled = scrollback_->takeNewest(maxLines);
        if (!pulled.empty())
            ++historyGeneration_;
        return pulled;
    });
```

- [ ] **步骤 4：构建并运行 facade 测试**

运行：
```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_native_rowresize
./build/linux-gcc-debug/tests/test_native_rowresize
```
预期：4 用例全过，退出码 0。用例 4 失败时先核对推演（alt 期间主屏 pushUp 属规格 §3「按缓冲区分」语义），不要改断言迁就实现。

- [ ] **步骤 5：compat 行变 parity 用例**

`tests/unit/test_backend_compat.cpp` 在用例 9（testResize，:206-216）后新增（Dual/feedBoth/checkRowEqual 用法照用例 9 同款；include 区若无 <string> 则补）：

```cpp
// 20. 行变 resize parity（M15）：缩行压历史与扩行回抽双后端一致。
void testRowResizeParity()
{
    Dual d;
    for (int i = 0; i < 30; ++i)
        d.feedBoth("row-" + std::to_string(i) + "\r\n"); // 30 行进 24 行屏：双后端各 7 行历史
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());

    ZZ_CHECK(d.native.resize(80, 10) == d.contour.resize(80, 10)); // 缩 14：光标贴底 pushUp
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    ZZ_CHECK(d.native.historyView().lineCount() == 21); // 7 + 14
    for (std::size_t i = 0; i < d.native.historyView().lineCount(); ++i) {
        const std::string n = historyText(d.native, i);
        const std::string c = historyText(d.contour, i);
        ZZ_CHECK(n == c);
    }
    checkRowEqual(d.native, d.contour, 0, 9, "rowresize-shrink");

    ZZ_CHECK(d.native.resize(80, 24) == d.contour.resize(80, 24)); // 扩 14：光标贴底回抽
    ZZ_CHECK(d.native.historyView().lineCount() == d.contour.historyView().lineCount());
    ZZ_CHECK(d.native.historyView().lineCount() == 7);
    checkRowEqual(d.native, d.contour, 0, 23, "rowresize-grow");
}
```

其中 historyText 帮手（若文件内已有等价物则复用，否则在匿名命名空间/帮手区新增）：

```cpp
std::string historyText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}
```

main() 注册一行（照既有用例注册同款）：`testRowResizeParity();`

注意：双后端 lineCount 相等断言成立的前提是 contour 缩行 pushUp 与 native 同数（光标贴底 + 同脚本，Grid shrinkLines 语义保证）；droppedLineCount 不比对（contour 行变过冲是 M14 §8 已立案项）。若 lineCount 不相等，先对照 fork Grid.cpp:784-828 复核两边搬行数推演，不得放宽断言。

- [ ] **步骤 6：Terminal.h resize 文档更新**

`include/ZzTerm/Terminal.h` 的 resize 文档注释（:115-123）整段替换为：

```cpp
    /**
     * @brief 调整终端尺寸；列变化触发 soft-wrap reflow（M4 起），
     *        行变化按条件语义搬行（M15 起）。
     * @param cols 新列数（> 0）。
     * @param rows 新行数（> 0）。
     * @return true 表示尺寸实际变化。
     * @note 列变化：屏幕区与 scrollback 历史一起重组（先历史后屏幕，
     *       光标跟随内容）；行变化（M15，双后端语义对齐 contour
     *       shrinkLines/growLines）：缩行先裁光标下方行，不够裁时把
     *       顶部行压入历史（Primary）；扩行光标贴末行时从最新历史
     *       回抽注入顶部，不足部分底部补空。Alternate 屏不产生历史。
     *       resize 后 RenderView/HistoryView 既有行句柄失效，前端需
     *       重新获取视图。
     */
    bool resize(int cols, int rows);
```

- [ ] **步骤 7：文档收尾（三处）**

其一，`docs/API.md`：grep 定位 resize 语义描述（`grep -n "行变化\|网格增减" docs/API.md`），把行变描述更新为 M15 语义（缩行压历史/扩行回抽，与 Terminal.h 新注释同口径）；`grep -n "版本与 ABI" docs/API.md` 定位版本节，追加 M15 条目（ZzScrollback 新增 takeNewest 纯虚——该接口标注 Core 内部使用但属公共头，实现类仅仓内 ChunkedScrollback 一个；ZzScreen 新增 HistoryPullCallback/setHistoryPullCallback；ZzTerminal 无签名变化、行为语义变化）。

其二，`docs/superpowers/specs/2026-09-29-m13-spike-record.md` §4 现象 5：在该条末尾追加「**M15 已根治**：native 行变语义对齐 contour（缩行压历史/扩行回抽），spec 2026-09-30-m15-native-row-resize-design.md」。

其三，`docs/superpowers/specs/2026-09-30-m14-history-view-design.md` §8：把「契约注释补一行：双后端行变 resize 语义不对称……」一条改为「**M15 已作废**（语义已对齐，无需免责声明）」。

- [ ] **步骤 8：全基线 + doxygen + Commit + 推送 + CI**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
git add src/backend/native/ZzNativeBackend.cpp include/ZzTerm/Terminal.h \
    tests/unit/test_native_rowresize.cpp tests/unit/test_backend_compat.cpp \
    docs/API.md docs/superpowers/specs/2026-09-29-m13-spike-record.md \
    docs/superpowers/specs/2026-09-30-m14-history-view-design.md
git commit -m "feat(native): M15 行变回抽接线与 facade/compat parity 钉住+文档收尾"
git push origin contour
gh run list --branch contour --limit 7
```
预期：linux-gcc-debug **55/55**（54+test_native_rowresize；compat 为既有目标内新增用例不加计数）；OFF **42/42**；shared **55/55**；doxygen exit 0；CI 7 个 workflow 全绿。

---

## 收尾（全部任务完成后）

- ZzTermCore 仓打 tag `m15` 并推送（`git tag m15 && git push origin m15`）。
- 人工复验移交（用户执行）：`cd /home/zz/Jackfahdin/github/ZzClawTerm && ./build/spike-debug/zzcore_spike --local`——ls 若干次后：① 拖矮窗口，最近输出保持可见、旧内容滚轮上滚可见（不再丢失）；② 拉高窗口，历史行拉回屏幕顶部（拉大恢复）；③ 重复缩放多次内容不错乱。spike 侧零改动（add_subdirectory 重构建即得新语义）。
