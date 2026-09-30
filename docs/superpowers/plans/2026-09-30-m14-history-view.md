# M14 历史访问契约（ZzHistoryView）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为前端提供后端无关的历史行只读访问契约 ZzHistoryView（双后端实现 + 测试），并在 ZzClawTerm spike widget 接上滚动做端到端实证。

**架构：** 新增公共只读视图 ZzHistoryView，与 ZzRenderView 平行的第二只读边界：RenderView 覆盖屏幕区，HistoryView 覆盖 scrollback 历史区（坐标 0 = 最旧历史行）。ZzTerminal 新增 const 访问器 historyView()，经内部 ZzTerminalBackend 多态分派；native 借 ZzScrollback 的 const 引用（O(1)），contour 经既有 historyLineSnapshot 快照口覆写视图内部单行缓冲。变化侦测走 generation() 代计数（append/裁剪/reflow/Alternate 切换递增），禁止每帧全扫历史。

**技术栈：** C++20、CMake（GLOB + CONFIGURE_DEPENDS 自动收编）、Contour vtbackend（私有后端）、Qt6 Widgets/QTest（spike 验证，ZzClawTerm 仓）。

**规格：** docs/superpowers/specs/2026-09-30-m14-history-view-design.md（已审定）

**基线命令（ZzTermCore 仓根目录）：**
- `cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`（现状 50/50）
- `ctest --test-dir build/m2-off-check`（现状 40/40，contour OFF 静态）
- `ctest --test-dir build/m2-shared-check`（现状 50/50，contour ON 动态）
- `doxygen Doxyfile`（exit 0 零警告，WARN_AS_ERROR=YES）

---

## 文件结构

**创建（ZzTermCore 仓）：**
- `include/ZzTerm/HistoryView.h` — 公共契约（纯虚接口，ZZTERM_API 导出）
- `src/backend/ZzTerminalBackend.cpp` — 接口默认实现：兜底空视图（根 CMakeLists 的 `src/*/*.cpp` GLOB 自动收编）
- `src/backend/native/ZzNativeHistoryView.h` / `.cpp` — native 实现（借 ZzScrollback const 引用）
- `src/backend/contour/ZzContourHistoryView.h` / `.cpp` — contour 实现（快照覆写内部单行缓冲）
- `tests/unit/test_historyview.cpp` — native facade 单测（只碰公开头，GLOB 自动收编）
- `tests/unit/test_historyview_compat.cpp` — 双后端 compat 对照（contour 门控注册）

**修改（ZzTermCore 仓）：**
- `src/backend/ZzTerminalBackend.h` — 加 include 与 historyView() 声明（非纯虚，默认空视图兜底）
- `src/backend/native/ZzNativeBackend.h` / `.cpp` — historyView_ 成员与接线、historyGeneration_ 代计数 3 处递增点
- `src/backend/native/CMakeLists.txt` — target_sources 加 ZzNativeHistoryView.cpp
- `src/backend/contour/ZzContourBackendAdapter.cpp` — historyView_ 成员与接线、代计数递增逻辑
- `src/backend/contour/CMakeLists.txt` — 显式源文件清单加 ZzContourHistoryView.cpp
- `include/ZzTerm/Terminal.h` — include + historyView() 声明
- `src/terminal/Terminal.cpp` — historyView() 委托一行
- `tests/CMakeLists.txt` — REMOVE_ITEM 清单 + test_historyview_compat 注册块
- `docs/API.md`、`README.md`、`docs/Architecture-v2.md` — 任务 3 文档收尾

**修改（ZzClawTerm 仓，任务 4）：**
- `spike/ZzCoreViewWidget.h` / `.cpp` — 滚动偏移、wheelEvent、历史+屏幕拼接绘制
- `spike/main.cpp` — 两链各装一个 QScrollBar
- `spike/tests/tst_spikerender.cpp` — 滚动拼接 QTest 用例

---

## 任务 1：公共契约 + 接口默认 + native 实现 + facade + native 单测

**文件：**
- 创建：`include/ZzTerm/HistoryView.h`
- 创建：`src/backend/ZzTerminalBackend.cpp`
- 创建：`src/backend/native/ZzNativeHistoryView.h`、`src/backend/native/ZzNativeHistoryView.cpp`
- 修改：`src/backend/ZzTerminalBackend.h`（51 行 lineSource() 声明后追加）
- 修改：`src/backend/native/ZzNativeBackend.h`、`src/backend/native/ZzNativeBackend.cpp`
- 修改：`src/backend/native/CMakeLists.txt`
- 修改：`include/ZzTerm/Terminal.h`（renderView() 声明在 :132 附近）
- 修改：`src/terminal/Terminal.cpp`（renderView() 委托在 :93）
- 测试：`tests/unit/test_historyview.cpp`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_historyview.cpp`：

```cpp
// ZzTerminal 历史视图（ZzHistoryView，M14）native 后端单测：
// 行数与 0=最旧顺序、容量裁剪与 droppedLineCount、wrapped 标记、
// generation 在滚出/reflow/Alternate 切换时递增、Alternate 历史归零。
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

// 历史第 index 行全部格文本拼接（空白格 text 为空串自然跳过）。
static std::string historyLineText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

// 喂 8 行（a..h 各带 \r\n）进 10x4 终端：滚出 a..e 共 5 行进历史。
static void feedEightLines(ZzTerminal& term)
{
    for (char c = 'a'; c <= 'h'; ++c) {
        std::string s;
        s += c;
        s += "\r\n";
        feedStr(term, s);
    }
}

static void testEmptyInitial()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().droppedLineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().generation() == 0);
}

static void testScrollOutOrder()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(term.historyView().droppedLineCount() == 0); // 容量未满不裁
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "a");            // 0 = 最旧
    ZZ_TEST_EXPECT(historyLineText(term, 4) == "e");
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).cellCount() == 10); // 行宽=当前列宽
    ZZ_TEST_EXPECT(!term.historyView().lineAt(0).wrapped());
}

static void testCapacityTrimAndDropped()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 3); // 历史容量 3
    feedEightLines(term);                             // 滚出 a..e 共 5 行
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 3);
    ZZ_TEST_EXPECT(term.historyView().droppedLineCount() == 2); // a、b 被裁
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "c");            // 最旧留存
    // 绝对行号换算：droppedLineCount() + index（c 是第 3 条滚出行，0 起序号 2）。
    ZZ_TEST_EXPECT(term.historyView().droppedLineCount() + 0 == 2);
}

static void testWrappedFlag()
{
    ZzTerminal term(5, 2, ZzBackendKind::Native, 100);
    feedStr(term, "abcdefghij\r\n"); // 5 列软换行：「abcde」(wrapped) 滚入历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1);
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "abcde");
    feedStr(term, "klmno\r\n"); // 「fghij」(非 wrapped) 滚入历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);
    ZZ_TEST_EXPECT(historyLineText(term, 1) == "fghij");
    ZZ_TEST_EXPECT(!term.historyView().lineAt(1).wrapped());
}

static void testGenerationBumpsOnScrollOut()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    const std::uint64_t g0 = term.historyView().generation();
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
}

static void testGenerationBumpsOnReflow()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    const std::uint64_t g0 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(20, 4)); // 列变化触发历史 reflow
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    ZZ_TEST_EXPECT(historyLineText(term, 0) == "a");            // 内容保留
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).cellCount() == 20); // 行宽不变量
}

static void testAlternateZero()
{
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    feedEightLines(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    const std::uint64_t g0 = term.historyView().generation();
    feedStr(term, "\x1b[?1049h"); // 进 Alternate：历史不可见
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);
    const std::uint64_t g1 = term.historyView().generation();
    feedStr(term, "\x1b[?1049l"); // 回 Primary：历史恢复可见
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 5);
    ZZ_TEST_EXPECT(term.historyView().generation() > g1);
}

int main()
{
    testEmptyInitial();
    testScrollOutOrder();
    testCapacityTrimAndDropped();
    testWrappedFlag();
    testGenerationBumpsOnScrollOut();
    testGenerationBumpsOnReflow();
    testAlternateZero();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug --target test_historyview 2>&1 | tail -5`
预期：编译失败，报错含 `historyView` 成员不存在 / `ZzTerm/HistoryView.h` 找不到（测试文件经 GLOB 收编需先重新 configure：`cmake --preset linux-gcc-debug`，下同）。

- [ ] **步骤 3：创建公共契约头 `include/ZzTerm/HistoryView.h`**

```cpp
#pragma once

/// \file
/// \brief 后端无关的历史行只读视图契约（M14）。
/// 与 ZzRenderView 平行的第二只读边界：RenderView 覆盖屏幕区，本契约覆盖
/// scrollback 历史区。视图为借用式，须与 feed 同线程使用；feed/resize/clear
/// 后既有视图产出的 ZzLineView 句柄全部失效。非线程安全。
///
/// 性能红线：禁止每帧扫描全部历史；变化侦测用 generation()，滚动查看按需
/// lineAt 取可见行（native O(1)，contour 为单行快照拷贝）。
///
/// 固有边界（分域 reflow）：历史与屏幕分域重组，跨域逻辑行在接缝处拆成
/// 两条链——内容零丢失，但折行位置可能与 contour 后端不同（与 ZzScrollback
/// 接口注释同一免责声明）。

#include <ZzTerm/RenderView.h> // ZzLineView

#include <cstddef>
#include <cstdint>

/**
 * @brief 历史行只读视图接口（双后端统一）。
 *
 * 坐标模型：index 属于 [0, lineCount())，0 = 最旧历史行；
 * 绝对行号 = droppedLineCount() + index（供选区锚点平移与应用侧
 * 归档行号对齐）。Alternate 屏 lineCount() 恒 0（Alternate 无历史），
 * 历史数据本身保留，切回 Primary 后恢复可见。
 */
class ZZTERM_API ZzHistoryView {
public:
    virtual ~ZzHistoryView() = default;

    /**
     * @brief 当前可读历史行数。
     * @return 历史行数；Alternate 屏恒 0。
     */
    [[nodiscard]] virtual std::size_t lineCount() const noexcept = 0;

    /**
     * @brief 第 index 行只读句柄。
     * @param index 行号（0 = 最旧历史行，须小于 lineCount()）。
     * @return 该行只读访问句柄。
     * @note 句柄借用后端行（contour 为视图内部单行缓冲）：feed/resize/clear
     *       或下一次 lineAt 调用后失效，不得长期持有。
     * @note 行宽不变量：cellCount() 恒等于终端当前列宽（resize 经 reflow 维持）。
     */
    [[nodiscard]] virtual ZzLineView lineAt(std::size_t index) const = 0;

    /**
     * @brief 历史头部累计裁掉的行数。
     * @return 单调不减的裁剪计数。
     * @note contour 后端的计数以其下层 Grid 能力为限（列变 reflow 的
     *       行身份重建不计入，与内部 LineSource 同一口径）。
     */
    [[nodiscard]] virtual std::uint64_t droppedLineCount() const noexcept = 0;

    /**
     * @brief 历史变化代计数。
     * @return 单调递增的代计数。
     * @note append/裁剪/reflow/clear/Alternate 切换时递增；允许保守多增，
     *       不得漏增。前端据此刷新滚动条上限，禁止每帧全扫历史。
     */
    [[nodiscard]] virtual std::uint64_t generation() const noexcept = 0;
};
```

- [ ] **步骤 4：内部接口加 historyView()（非纯虚，默认空视图兜底）**

`src/backend/ZzTerminalBackend.h` 两处修改。其一，include 区（`#include "ZzTerm/RenderView.h"` 后）追加：

```cpp
#include "ZzTerm/HistoryView.h"
```

其二，`lineSource()` 声明（文件末尾 :51）后追加：

```cpp
    /// 历史行只读视图（M14；借用语义同 renderView）。
    /// 非纯虚：默认返回兜底空视图（lineCount 恒 0），后端按能力覆写。
    [[nodiscard]] virtual const ZzHistoryView& historyView() const noexcept;
```

创建 `src/backend/ZzTerminalBackend.cpp`（根 GLOB `src/*/*.cpp` 自动收编）：

```cpp
#include "ZzTerminalBackend.h"

#include <ZzTerm/Line.h>

namespace {

// 无历史后端的兜底空视图（M14）：lineCount 恒 0。lineAt 的契约前置条件是
// index < lineCount()，对本视图即永不满足；防御性返回 0 列空行句柄。
class ZzEmptyHistoryView final : public ZzHistoryView {
public:
    [[nodiscard]] std::size_t lineCount() const noexcept override { return 0; }

    [[nodiscard]] ZzLineView lineAt(std::size_t /*index*/) const override
    {
        static const ZzLine emptyLine; // 0 列空行
        return ZzLineView(&emptyLine, &cellAtThunk, &cellCountThunk, &wrappedThunk);
    }

    [[nodiscard]] std::uint64_t droppedLineCount() const noexcept override { return 0; }
    [[nodiscard]] std::uint64_t generation() const noexcept override { return 0; }

private:
    static ZzCellView cellAtThunk(const void* /*storage*/, int /*col*/) { return {}; }
    static int cellCountThunk(const void* /*storage*/) noexcept { return 0; }
    static bool wrappedThunk(const void* /*storage*/) noexcept { return false; }
};

ZzEmptyHistoryView g_emptyHistoryView; // 内部静态，经基类引用借出

} // namespace

const ZzHistoryView& ZzTerminalBackend::historyView() const noexcept
{
    return g_emptyHistoryView;
}
```

- [ ] **步骤 5：native 实现 ZzNativeHistoryView**

创建 `src/backend/native/ZzNativeHistoryView.h`：

```cpp
#pragma once

#include <ZzTerm/HistoryView.h>

#include <cstdint>

class ZzScreen;
class ZzScrollback;

/// \brief native 后端的 scrollback 历史只读视图（M14）。
/// 借用 screen/scrollback/代计数（寿命须包住本对象）；
/// feed/resize 后经 lineAt 重新取行句柄即可。
class ZzNativeHistoryView final : public ZzHistoryView {
public:
    ZzNativeHistoryView(const ZzScreen& screen, const ZzScrollback& scrollback,
                        const std::uint64_t& generation) noexcept;
    [[nodiscard]] std::size_t lineCount() const noexcept override;
    [[nodiscard]] ZzLineView lineAt(std::size_t index) const override;
    [[nodiscard]] std::uint64_t droppedLineCount() const noexcept override;
    [[nodiscard]] std::uint64_t generation() const noexcept override;

private:
    static ZzCellView cellAtThunk(const void* storage, int col);
    static int cellCountThunk(const void* storage) noexcept;
    static bool wrappedThunk(const void* storage) noexcept;
    const ZzScreen* screen_;
    const ZzScrollback* scrollback_;
    const std::uint64_t* generation_;
};
```

创建 `src/backend/native/ZzNativeHistoryView.cpp`（thunk 与 ZzNativeRenderView.cpp 同款，操作同一个 const ZzLine 借用模型）：

```cpp
#include "ZzNativeHistoryView.h"

#include <ZzTerm/Line.h>
#include <ZzTerm/Screen.h>
#include <ZzTerm/Scrollback.h>

#include "unicode/Utf8Encode.h"

namespace {

// ZzLineView 内联存储的是 const ZzLine*（指针值），thunk 需先取指针再解引用。
const ZzLine& lineFrom(const void* storage)
{
    return **static_cast<const ZzLine* const*>(storage);
}

} // namespace

ZzNativeHistoryView::ZzNativeHistoryView(const ZzScreen& screen,
                                         const ZzScrollback& scrollback,
                                         const std::uint64_t& generation) noexcept
    : screen_(&screen), scrollback_(&scrollback), generation_(&generation)
{
}

std::size_t ZzNativeHistoryView::lineCount() const noexcept
{
    if (screen_->activeBuffer() == ZzScreenBuffer::Alternate)
        return 0; // Alternate 无历史（与内部 LineSource 同一规则）
    return scrollback_->lineCount();
}

ZzLineView ZzNativeHistoryView::lineAt(std::size_t index) const
{
    return ZzLineView(&scrollback_->lineAt(index), &cellAtThunk, &cellCountThunk,
                      &wrappedThunk);
}

std::uint64_t ZzNativeHistoryView::droppedLineCount() const noexcept
{
    return scrollback_->stats().totalDropped;
}

std::uint64_t ZzNativeHistoryView::generation() const noexcept
{
    return *generation_;
}

ZzCellView ZzNativeHistoryView::cellAtThunk(const void* storage, int col)
{
    const ZzLine& line = lineFrom(storage);
    const ZzCell& cell = line.cellAt(col);
    ZzCellView view;
    if (cell.isCluster()) {
        view.text = line.clusterText(cell.clusterIndex());
    } else if (cell.codePoint() != 0) {
        zzAppendCodePoint(view.text, cell.codePoint());
    }
    view.foreground = cell.foreground();
    view.background = cell.background();
    view.attributes = cell.attributes();
    view.width      = cell.width();
    return view;
}

int ZzNativeHistoryView::cellCountThunk(const void* storage) noexcept
{
    return lineFrom(storage).cellCount();
}

bool ZzNativeHistoryView::wrappedThunk(const void* storage) noexcept
{
    return lineFrom(storage).wrapped();
}
```

`src/backend/native/CMakeLists.txt` 的 target_sources 清单加一行（ZzNativeLineSource.cpp 后）：

```cmake
    ZzNativeHistoryView.cpp
```

- [ ] **步骤 6：ZzNativeBackend 接线与代计数递增点**

`src/backend/native/ZzNativeBackend.h` 三处。其一，include 区（`#include "ZzNativeLineSource.h"` 后）追加：

```cpp
#include "ZzNativeHistoryView.h"
```

其二，接口区（`lineSource()` override 后）追加：

```cpp
    [[nodiscard]] const ZzHistoryView& historyView() const noexcept override { return historyView_; }
```

其三，成员区（`renderView_` 声明后）追加（historyView_ 借用前三者，必须声明在它们之后）：

```cpp
    std::uint64_t                historyGeneration_ = 0; ///< 历史变化代计数（M14：append/reflow/Alternate 切换递增）。
    ZzNativeHistoryView          historyView_; ///< 历史只读边界（借用 screen_/scrollback_/historyGeneration_）。
```

`src/backend/native/ZzNativeBackend.cpp` 四处。其一，构造函数初始化列表（`renderView_(screen_)` 后）追加：

```cpp
    , historyView_(screen_, *scrollback_, historyGeneration_)
```

其二，滚出回调（:205-208）改为：

```cpp
    screen_.setScrollOutCallback([this](std::vector<ZzLine> lines) {
        scrolledOutPending_ += lines.size();
        scrollback_->append(std::move(lines));
        ++historyGeneration_; // M14：历史 append（含容量裁剪）代计数递增
    });
```

其三，resize() 列变化分支（:239-242）改为：

```cpp
    if (cols != old.cols) {
        scrollback_->reflow(cols);
        ++historyGeneration_; // M14：历史 reflow 代计数递增（屏幕回流 append 经回调另计）
        screen_.reflow(cols);
    }
```

其四，Alternate 切换两处（先 `grep -n "ZzNativeBackend::switchToAlternate\|ZzNativeBackend::switchToPrimary" src/backend/native/*.cpp` 定位实现文件与行号），在两个函数体首行各加：

```cpp
    ++historyGeneration_; // M14：Alternate 切换改变可见历史（lineCount 归零/恢复）
```

- [ ] **步骤 7：facade 暴露 historyView()**

`include/ZzTerm/Terminal.h` 两处。其一，include 区（`#include "ZzTerm/RenderView.h"` 后）追加：

```cpp
#include "ZzTerm/HistoryView.h"
```

其二，`renderView()` 声明（:132）后追加：

```cpp
    /**
     * @brief 获取历史行只读视图（M14；与 renderView 平行的第二只读边界）。
     * @return 历史视图常量引用；借用 Terminal，不得比 Terminal 长寿。
     * @note 坐标 0 = 最旧历史行；Alternate 屏 lineCount() 恒 0；
     *       feed/resize 后经 lineAt 重新取行句柄。变化侦测用 generation()，
     *       禁止每帧全扫历史。
     */
    [[nodiscard]] const ZzHistoryView& historyView() const noexcept;
```

`src/terminal/Terminal.cpp`（`renderView()` 委托 :93 后）追加：

```cpp
const ZzHistoryView& ZzTerminal::historyView() const noexcept { return impl_->backend->historyView(); }
```

- [ ] **步骤 8：构建并运行新测试**

运行：
```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_historyview
./build/linux-gcc-debug/tests/test_historyview
```
预期：编译通过，测试退出码 0（无 FAIL 行）。

- [ ] **步骤 9：全基线回归**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
```
预期：`ctest --preset linux-gcc-debug` **51/51**（50 + 新增 test_historyview；contour 后端未覆写 historyView，走默认空视图，compat 测试尚不存在故无回归）；OFF **41/41**（native 单测在 OFF 构建同样收编）；shared **51/51**；doxygen exit 0 零警告（若新公共头注释报新警告，按警告文本修正注释措辞）。

- [ ] **步骤 10：Commit**

```bash
git add include/ZzTerm/HistoryView.h src/backend/ZzTerminalBackend.h src/backend/ZzTerminalBackend.cpp \
    src/backend/native/ZzNativeHistoryView.h src/backend/native/ZzNativeHistoryView.cpp \
    src/backend/native/ZzNativeBackend.h src/backend/native/ZzNativeBackend.cpp \
    src/backend/native/CMakeLists.txt include/ZzTerm/Terminal.h src/terminal/Terminal.cpp \
    tests/unit/test_historyview.cpp
git commit -m "feat(history): M14 历史只读视图契约 ZzHistoryView 与 native 实现"
```

---

## 任务 2：contour 实现 + 双后端 compat 对照

**文件：**
- 创建：`src/backend/contour/ZzContourHistoryView.h`、`src/backend/contour/ZzContourHistoryView.cpp`
- 修改：`src/backend/contour/CMakeLists.txt`（显式源文件清单 :6-12）
- 修改：`src/backend/contour/ZzContourBackendAdapter.cpp`（适配器类全在匿名命名空间内，:14-152）
- 修改：`tests/CMakeLists.txt`（REMOVE_ITEM 清单 :11-19 + 注册块）
- 测试：`tests/unit/test_historyview_compat.cpp`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_historyview_compat.cpp`：

```cpp
// M14：双后端历史视图 compat（同一 VT 脚本历史文本逐行比对，native 为基准）
// + contour 侧 generation/alternate 行为钉住（facade 层，只碰公开头）。
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

static std::string historyLineText(const ZzTerminal& term, std::size_t index)
{
    const ZzLineView line = term.historyView().lineAt(index);
    std::string out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += line.cellAt(col).text;
    return out;
}

// 纯滚动脚本：30 行编号行 + 21 字符软换行长行 + CJK 行。
static void runScript(ZzTerminal& term)
{
    for (int i = 0; i < 30; ++i)
        feedStr(term, "line-" + std::to_string(i) + "\r\n");
    feedStr(term, "abcdefghijklmnopqrstu\r\n"); // 21 字符在 20 列软换行为 2 行
    feedStr(term, "\xe4\xb8\xad\xe6\x96\x87\xe8\xa1\x8c\r\n"); // 「中文行」
}

static void testHistoryTextParity()
{
    ZzTerminal native(20, 6, ZzBackendKind::Native, 100);
    ZzTerminal contour(20, 6, ZzBackendKind::Contour, 100);
    runScript(native);
    runScript(contour);
    const std::size_t n = native.historyView().lineCount();
    ZZ_TEST_EXPECT(n > 0);
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == n);
    const std::size_t common = n < contour.historyView().lineCount()
        ? n : contour.historyView().lineCount();
    for (std::size_t i = 0; i < common; ++i)
        ZZ_TEST_EXPECT(historyLineText(native, i) == historyLineText(contour, i));
}

static void testTrimParity()
{
    ZzTerminal native(20, 6, ZzBackendKind::Native, 10); // 容量 10 触发裁剪
    ZzTerminal contour(20, 6, ZzBackendKind::Contour, 10);
    runScript(native);
    runScript(contour);
    // 纯滚动脚本下双后端均只计真实容量裁剪（contour 经 noteFloor 口径）。
    ZZ_TEST_EXPECT(native.historyView().lineCount() == 10);
    ZZ_TEST_EXPECT(contour.historyView().lineCount() == 10);
    ZZ_TEST_EXPECT(native.historyView().droppedLineCount()
                   == contour.historyView().droppedLineCount());
    for (std::size_t i = 0; i < 10; ++i)
        ZZ_TEST_EXPECT(historyLineText(native, i) == historyLineText(contour, i));
}

static void testContourGenerationAndAlternate()
{
    ZzTerminal term(20, 6, ZzBackendKind::Contour, 100);
    const std::uint64_t g0 = term.historyView().generation();
    runScript(term);
    ZZ_TEST_EXPECT(term.historyView().lineCount() > 0);
    ZZ_TEST_EXPECT(term.historyView().generation() > g0); // 滚出 append 递增
    const std::uint64_t g1 = term.historyView().generation();
    ZZ_TEST_EXPECT(term.resize(40, 6));
    ZZ_TEST_EXPECT(term.historyView().generation() > g1); // 列变 reflow 递增
    ZZ_TEST_EXPECT(term.historyView().lineAt(0).cellCount() == 40); // 行宽不变量
    const std::uint64_t g2 = term.historyView().generation();
    feedStr(term, "\x1b[?1049h");
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 0); // Alternate 历史归零
    ZZ_TEST_EXPECT(term.historyView().generation() > g2);
    feedStr(term, "\x1b[?1049l");
    ZZ_TEST_EXPECT(term.historyView().lineCount() > 0); // 回 Primary 恢复
}

int main()
{
    testHistoryTextParity();
    testTrimParity();
    testContourGenerationAndAlternate();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```

`tests/CMakeLists.txt` 两处。其一，REMOVE_ITEM 清单（:11-19）追加一行：

```cmake
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_historyview_compat.cpp"
```

其二，`test_cluster_compat` 注册块（:143-148）后追加：

```cmake
# M14：双后端历史视图 compat（同一脚本历史文本逐行一致，native 为基准）。
# 只碰公开头（C++20 即可），仅链接 ZzTermCore（同 test_backend_compat 的 shared 构建理由）。
if(TARGET ZzTermContourBackend)
    add_executable(test_historyview_compat unit/test_historyview_compat.cpp)
    target_link_libraries(test_historyview_compat PRIVATE ZzTermCore) # Contour target 一律 PRIVATE
    add_test(NAME test_historyview_compat COMMAND test_historyview_compat)
endif()
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_historyview_compat 2>&1 | tail -5`
预期：链接或运行失败——contour 后端仍走默认空视图，`lineCount() == 0` 使断言失败（退出码非 0）。

- [ ] **步骤 3：contour 实现 ZzContourHistoryView**

创建 `src/backend/contour/ZzContourHistoryView.h`：

```cpp
#pragma once

#include <ZzTerm/HistoryView.h>
#include <ZzTerm/Line.h>

#include <cstdint>

class ZzContourBackend;
class ZzContourLineSource;

/// \brief Contour 后端的历史只读视图（M14）。
/// Contour 历史只能快照读：lineAt 把快照覆写进视图内部单行缓冲后借出，
/// 下一次 lineAt 或 feed/resize 后句柄失效（契约 §3.1 的 contour 形态）。
/// 借用 backend/lineSource/代计数（寿命须包住本对象）。
class ZzContourHistoryView final : public ZzHistoryView {
public:
    ZzContourHistoryView(const ZzContourBackend& backend,
                         const ZzContourLineSource& lineSource,
                         const std::uint64_t& generation) noexcept;
    [[nodiscard]] std::size_t lineCount() const noexcept override;
    [[nodiscard]] ZzLineView lineAt(std::size_t index) const override;
    [[nodiscard]] std::uint64_t droppedLineCount() const noexcept override;
    [[nodiscard]] std::uint64_t generation() const noexcept override;

private:
    static ZzCellView cellAtThunk(const void* storage, int col);
    static int cellCountThunk(const void* storage) noexcept;
    static bool wrappedThunk(const void* storage) noexcept;
    const ZzContourBackend* backend_;
    const ZzContourLineSource* lineSource_;
    const std::uint64_t* generation_;
    mutable ZzLine lineBuf_; ///< lineAt 快照缓冲（mutable：const 接口下覆写）
};
```

创建 `src/backend/contour/ZzContourHistoryView.cpp`：

```cpp
#include "ZzContourHistoryView.h"

#include "ZzContourBackend.h"
#include "ZzContourLineSource.h"

namespace {

// ZzLineView 内联存储的是 const ZzLine*（指针值），thunk 需先取指针再解引用。
const ZzLine& lineFrom(const void* storage)
{
    return **static_cast<const ZzLine* const*>(storage);
}

} // namespace

ZzContourHistoryView::ZzContourHistoryView(const ZzContourBackend& backend,
                                           const ZzContourLineSource& lineSource,
                                           const std::uint64_t& generation) noexcept
    : backend_(&backend), lineSource_(&lineSource), generation_(&generation)
{
}

std::size_t ZzContourHistoryView::lineCount() const noexcept
{
    if (backend_->isAlternateScreen())
        return 0; // Alternate 无历史（与内部 LineSource 同一规则）
    return static_cast<std::size_t>(backend_->historyLineCount());
}

ZzLineView ZzContourHistoryView::lineAt(std::size_t index) const
{
    lineBuf_ = backend_->historyLineSnapshot(static_cast<int>(index));
    return ZzLineView(&lineBuf_, &cellAtThunk, &cellCountThunk, &wrappedThunk);
}

std::uint64_t ZzContourHistoryView::droppedLineCount() const noexcept
{
    return lineSource_->droppedLineCount();
}

std::uint64_t ZzContourHistoryView::generation() const noexcept
{
    return *generation_;
}

ZzCellView ZzContourHistoryView::cellAtThunk(const void* storage, int col)
{
    const ZzLine& line = lineFrom(storage);
    const ZzCell& cell = line.cellAt(col);
    ZzCellView view;
    if (cell.isCluster()) {
        view.text = line.clusterText(cell.clusterIndex());
    } else if (cell.codePoint() != 0) {
        zzAppendCodePoint(view.text, cell.codePoint());
    }
    view.foreground = cell.foreground();
    view.background = cell.background();
    view.attributes = cell.attributes();
    view.width      = cell.width();
    return view;
}

int ZzContourHistoryView::cellCountThunk(const void* storage) noexcept
{
    return lineFrom(storage).cellCount();
}

bool ZzContourHistoryView::wrappedThunk(const void* storage) noexcept
{
    return lineFrom(storage).wrapped();
}
```

cellAtThunk 需要 `zzAppendCodePoint`：文件顶部 include 区追加 `#include "unicode/Utf8Encode.h"`（contour 后端库已把 `${CMAKE_SOURCE_DIR}/src` 加进 PRIVATE include 路径，见 src/backend/contour/CMakeLists.txt :17，先例 ZzContourRenderView.cpp 同款引用）。

`src/backend/contour/CMakeLists.txt` 的 add_library 源文件清单（:6-12）追加一行：

```cmake
    ZzContourHistoryView.cpp
```

- [ ] **步骤 4：ZzContourBackendAdapter 接线与代计数**

`src/backend/contour/ZzContourBackendAdapter.cpp` 五处（适配器类在匿名命名空间内）。其一，include 区（`#include "ZzContourLineSource.h"` 后）追加：

```cpp
#include "ZzContourHistoryView.h"
```

其二，构造函数初始化列表（`lineSource_(*backend_)` 后）追加：

```cpp
        // historyView_ 借用 backend_/lineSource_/historyGeneration_：声明顺序须在它们之后。
        , historyView_(*backend_, lineSource_, historyGeneration_)
```

其三，feed() 末尾（`lineSource_.noteFloor();` 行之后、`ZzTermChanges changes;` 之前）插入：

```cpp
        // M14：历史代计数——行数变化（append）、真实裁剪（floor 前移）、
        // Alternate 切换（可见历史归零/恢复）任一发生即递增；允许保守多增。
        if (historyAfter != historyBefore || lineSource_.droppedLineCount() != lastDropped_
            || activeBufferChanged_)
            ++historyGeneration_;
        lastDropped_ = lineSource_.droppedLineCount();
```

其四，resize() 的 floor 收账段（:62-68，双入口划分注释及其后 if/else）整段替换为：

```cpp
        // 双入口划分（ZzContourLineSource.cpp 文件头结论①）：列变化触发 reflow，
        // floor 前移是行身份重建副产而非真实丢弃——reanchorFloor 直接对齐不累计；
        // 纯行数变化 floor 仅在真实裁剪时前移——noteFloor 累计。
        const int historyBefore = backend_->historyLineCount();
        if (cols != oldCols)
            lineSource_.reanchorFloor();
        else
            lineSource_.noteFloor();
        // M14：列变 reflow 历史内容必变（保守递增）；行变仅在历史行数或
        // 裁剪计数实际变化时递增。
        if (cols != oldCols || backend_->historyLineCount() != historyBefore
            || lineSource_.droppedLineCount() != lastDropped_)
            ++historyGeneration_;
        lastDropped_ = lineSource_.droppedLineCount();
```

其五，接口区（`lineSource()` override 后）与成员区。接口区追加：

```cpp
    [[nodiscard]] const ZzHistoryView& historyView() const noexcept override { return historyView_; }
```

成员区（`lineSource_` 声明后、`state_` 前）追加：

```cpp
    std::uint64_t                     historyGeneration_ = 0; // M14 历史代计数（historyView_ 借用，须声明在其前）
    std::uint64_t                     lastDropped_ = 0;       // M14 裁剪计数快照（代计数递增判定用）
    ZzContourHistoryView              historyView_;  // 借用 backend_/lineSource_/historyGeneration_，须声明在它们之后
```

- [ ] **步骤 5：构建并运行 compat 测试**

运行：
```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_historyview_compat
./build/linux-gcc-debug/tests/test_historyview_compat
```
预期：编译通过，退出码 0。若 `testTrimParity` 的 droppedLineCount 相等断言失败，说明 contour 计数口径在该脚本下与 native 有真实差异——不得擅自放宽，先对照 ZzContourLineSource.cpp 文件头结论①-④定位差异来源，回报后再定钉住方式。

- [ ] **步骤 6：全基线回归（含 OFF 构建不回归确认）**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
```
预期：linux-gcc-debug **52/52**（+test_historyview_compat）；OFF **41/41**（compat 受 ZzTermContourBackend 门控不收编，contour 源文件改动不参与 OFF 构建）；shared **52/52**；doxygen exit 0。

- [ ] **步骤 7：Commit**

```bash
git add src/backend/contour/ZzContourHistoryView.h src/backend/contour/ZzContourHistoryView.cpp \
    src/backend/contour/CMakeLists.txt src/backend/contour/ZzContourBackendAdapter.cpp \
    tests/unit/test_historyview_compat.cpp tests/CMakeLists.txt
git commit -m "feat(history): M14 contour 历史视图实现与双后端 compat 对照"
```

---

## 任务 3：文档与全基线收尾

**文件：**
- 修改：`docs/API.md`（层级链 :14、RenderView 节 :296-301 后）
- 修改：`README.md`（特性清单 :13、层级链 :20）
- 修改：`docs/Architecture-v2.md`（后端接口枚举处，行号以 grep 为准）

- [ ] **步骤 1：docs/API.md 补 HistoryView 节**

其一，层级链行（:14）：

旧：`      -> Cell/Line/Screen -> Scrollback -> RenderView`
新：`      -> Cell/Line/Screen -> Scrollback -> RenderView + HistoryView`

其二，`### RenderView` 节（:296-301）结束后新增一节：

```markdown
### HistoryView

M14 新增的后端无关历史行只读视图，与 RenderView 平行的第二只读边界：
RenderView 覆盖屏幕区，HistoryView 覆盖 scrollback 历史区。

- `ZzTerminal::historyView()` 返回 `const ZzHistoryView&`，借用 Terminal，
  不得比 Terminal 长寿。
- `ZzHistoryView` 为纯虚接口：`lineCount` / `lineAt` / `droppedLineCount` /
  `generation`。
- 坐标：index 属于 [0, lineCount())，0 = 最旧历史行；
  绝对行号 = droppedLineCount() + index（选区锚点平移/应用侧归档对齐用）。
- `lineAt(index)` 返回 ZzLineView 借用句柄：feed/resize/clear 或下一次
  lineAt 调用后失效（contour 为视图内部单行缓冲覆写，native 借 scrollback
  const 引用）。行宽恒等于终端当前列宽（resize 经 reflow 维持）。
- Alternate 屏 lineCount() 恒 0（Alternate 无历史），回 Primary 恢复。
- 变化侦测用 generation()（append/裁剪/reflow/clear/Alternate 切换递增，
  允许保守多增）；禁止每帧全扫历史，滚动查看按需取可见行。
- 线程：非线程安全，与 ZzTerminal 同线程。
```

- [ ] **步骤 2：README.md 两处**

其一，特性清单行（:13）整行替换：

旧行：
```markdown
-   稳定只读的 `ZzRenderView` 渲染边界，Renderer 不接触 Core 私有容器
```

新行：
```markdown
-   稳定只读的 `ZzRenderView` 渲染边界与 `ZzHistoryView` 历史边界（M14），Renderer 不接触 Core 私有容器
```

其二，层级链行（:20）同 docs/API.md 的改法（`-> RenderView` 结尾改为 `-> RenderView + HistoryView`）。

- [ ] **步骤 3：docs/Architecture-v2.md 接口枚举补一行**

运行 `grep -n "renderView\|lineSource" docs/Architecture-v2.md` 定位 ZzTerminalBackend / facade 接口枚举处，在该清单追加一行（措辞对齐相邻行）：

```markdown
- historyView()：历史行只读视图（M14，ZzHistoryView；0=最旧，Alternate 恒 0，generation 代计数侦测变化）。
```

再运行 `grep -n "版本\|ABI" docs/API.md | head -5`：若存在版本/变更节，补一条 M14 条目（新增公共类 ZzHistoryView + ZzTerminal::historyView 非虚方法，向后兼容的 minor 新增；内部 ZzTerminalBackend 加非纯虚默认实现，不影响公共 ABI）；无版本节则跳过。

- [ ] **步骤 4：全基线 + doxygen + 推送 + CI 确认**

运行：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
git add docs/API.md README.md docs/Architecture-v2.md
git commit -m "docs(history): M14 HistoryView 契约入 API 文档与架构文档"
git push origin contour
gh run list --branch contour --limit 7
```
预期：52/52、41/41、52/52、doxygen exit 0；推送后 7 个 workflow 全绿（含 docs 与 downstream 双形态），若有红按日志修复（CI-only 修复 commit message 标注）。

---

## 任务 4：spike widget 滚动端到端实证（ZzClawTerm 仓）

**仓库：** `/home/zz/Jackfahdin/github/ZzClawTerm`（spike 代码仓；提交走该仓约定式提交，中文描述）。spike 经 add_subdirectory 消费本仓 ZzTermCore 源码，任务 1-3 落地后重新构建即自动获得 historyView()（GLOB + CONFIGURE_DEPENDS 收编，无需改 spike 的 CMake）。**纪律：ZzTermCore 的 src/include/pty 在 spike 中零改动，撞 API 缺口只记录绕行。**

**文件：**
- 修改：`spike/ZzCoreViewWidget.h`（91 行）
- 修改：`spike/ZzCoreViewWidget.cpp`（196 行）
- 修改：`spike/main.cpp`（runLocal 与 runSsh 两链尾部装配相同，:151-153 与 :299-301）
- 测试：`spike/tests/tst_spikerender.cpp`（99 行，追加用例）

- [ ] **步骤 1：编写失败的测试**

`spike/tests/tst_spikerender.cpp` 三处。其一，文件头注释的用例清单追加两行：

```cpp
 * - case 3（M14）：滚动偏移拼接——上滚后可见行为历史行，回底恢复屏幕行；
 * - case 4（M14）：Alternate 屏历史归零，滚动偏移夹取到 0。
```

其二，test 类 slots 区（`void ptyChainRendersMark();` 后）追加：

```cpp
    /**
     * @brief case 3（M14）：滚动偏移拼接——上滚后可见行为历史行，回底恢复屏幕行。
     */
    void scrollOffsetComposesHistory();

    /**
     * @brief case 4（M14）：Alternate 屏历史归零，滚动偏移夹取到 0。
     */
    void alternateClampsScroll();
```

其三，类实现区（`ptyChainRendersMark` 实现后）追加：

```cpp
void TstSpikeRender::scrollOffsetComposesHistory()
{
    ZzCoreViewWidget widget; // 80x24，历史容量 10000
    std::string script;
    for (int i = 0; i < 40; ++i)
        script += "hist-" + std::to_string(i) + "\r\n";
    widget.feedForTest(script);

    // 24 行屏幕喂 40 个换行：滚出 17 行进历史（hist-0 .. hist-16）。
    QCOMPARE(widget.maxScrollOffset(), 17);

    // 上滚到底：视口首行 = 最旧历史行。
    widget.setScrollOffset(widget.maxScrollOffset());
    QCOMPARE(widget.scrollOffset(), 17);
    QVERIFY(widget.visibleLineTextForTest(0).contains(QStringLiteral("hist-0")));

    // 回底恢复：屏幕末行上方为最新内容行 hist-39。
    widget.setScrollOffset(0);
    QCOMPARE(widget.scrollOffset(), 0);
    const int rows = widget.terminal().size().rows;
    QVERIFY(widget.visibleLineTextForTest(rows - 2).contains(QStringLiteral("hist-39")));

    // 越界夹取。
    widget.setScrollOffset(9999);
    QCOMPARE(widget.scrollOffset(), 17);
}

void TstSpikeRender::alternateClampsScroll()
{
    ZzCoreViewWidget widget;
    std::string script;
    for (int i = 0; i < 40; ++i)
        script += "hist-" + std::to_string(i) + "\r\n";
    widget.feedForTest(script);
    QVERIFY(widget.maxScrollOffset() > 0);

    widget.feedForTest("\x1b[?1049h"); // 进 Alternate：契约 lineCount 恒 0
    QCOMPARE(widget.maxScrollOffset(), 0);
    widget.setScrollOffset(5);
    QCOMPARE(widget.scrollOffset(), 0); // 夹取到 0

    widget.feedForTest("\x1b[?1049l"); // 回 Primary 恢复
    QCOMPARE(widget.maxScrollOffset(), 17);
}
```

- [ ] **步骤 2：运行测试验证失败**

运行（ZzClawTerm 仓根目录）：
```bash
cmake --build build/spike-debug 2>&1 | tail -5
```
预期：编译失败，报错含 `maxScrollOffset` / `setScrollOffset` / `visibleLineTextForTest` 成员不存在。（若报 `historyView` 不存在，说明 ZzTermCore 侧任务 1 未落地或 add_subdirectory 指向未更新，先回任务 1-3 确认。）

- [ ] **步骤 3：widget 实现滚动（ZzCoreViewWidget.h/.cpp）**

`spike/ZzCoreViewWidget.h` 五处。其一，文件头职责注释：删去 `无历史滚动（RenderView 无历史行访问件，见任务 1 报告 API 缺口登记）。` 一句，改为：

```cpp
 * - 历史滚动（M14）：滚轮/滚动条偏移经 ZzHistoryView 拼接历史段与屏幕段
 *   绘制；键盘输入自动回底；Alternate 屏（vim 等）滚动禁用。
```

其二，include 区（`#include <QWidget>` 后）追加：

```cpp
#include <QString>
```

其三，public 区（`terminal()` 声明后）追加：

```cpp
    /**
     * @brief 设置滚动偏移（0 = 底部最新；向上滚动查看历史的行数）。
     * @param offset 目标偏移，自动夹取到 [0, maxScrollOffset()]。
     */
    void setScrollOffset(int offset);

    /**
     * @brief 当前滚动偏移。
     * @return 0 表示底部。
     */
    [[nodiscard]] int scrollOffset() const noexcept { return scrollOffset_; }

    /**
     * @brief 最大滚动偏移（即可读历史行数；Alternate 屏契约恒 0）。
     * @return 历史行数。
     */
    [[nodiscard]] int maxScrollOffset() const noexcept;

    /**
     * @brief 测试钩子：可见第 row 行拼接后的纯文本（历史段+屏幕段统一视口）。
     * @param row 可见行号（0 起）。
     * @return 该行全部格文本拼接。
     */
    [[nodiscard]] QString visibleLineTextForTest(int row) const;
```

其四，signals 区（`gridResized` 声明后）追加：

```cpp
    /**
     * @brief 可滚动范围变化（历史行数变化；宿主据此更新滚动条上限）。
     * @param maximum 新的最大滚动偏移。
     */
    void scrollRangeChanged(int maximum);

    /**
     * @brief 滚动偏移变化（宿主据此同步滚动条位置）。
     * @param offset 新偏移（0 = 底部）。
     */
    void scrollOffsetChanged(int offset);
```

其五，protected 区（`resizeEvent` 声明后）与 private 区追加：

```cpp
    void wheelEvent(QWheelEvent* event) override;
```

```cpp
    /**
     * @brief 统一坐标取行：历史区在前、屏幕区在后，视口底部对齐。
     * @param row 可见行号（0 起）。
     * @return 该行只读句柄（当帧有效）。
     */
    [[nodiscard]] ZzLineView lineViewAtVisibleRow(int row) const;

    int scrollOffset_ = 0; ///< 滚动偏移（0 = 底部最新）。
```

`spike/ZzCoreViewWidget.cpp` 六处。其一，include 区（`#include <QPaintEvent>` 后）追加：

```cpp
#include <QWheelEvent>
```

其二，paintEvent 的行取用处（:87）把 `const ZzLineView line = view.lineAt(row);` 改为：

```cpp
        const ZzLineView line = lineViewAtVisibleRow(row);
```

其三，paintEvent 的光标块条件（:100）把 `if (cursor.visible && ...)` 改为（光标属屏幕区，上滚时不画）：

```cpp
    if (scrollOffset_ == 0 && cursor.visible && cursor.position.row >= 0
        && cursor.position.row < grid.rows && cursor.position.col >= 0
        && cursor.position.col < grid.cols) {
```

其四，keyPressEvent 函数体首行（`ZzKeyEvent keyEvent;` 前）插入（xterm 语义：键盘输入自动回底）：

```cpp
    if (scrollOffset_ != 0)
        setScrollOffset(0);
```

其五，feedForTest（:56-60）整函数替换为（feed 后历史行数可能变化，重发范围信号并夹取偏移）：

```cpp
void ZzCoreViewWidget::feedForTest(std::string_view data)
{
    const int maxBefore = maxScrollOffset();
    term_.feed(std::as_bytes(std::span<const char>(data.data(), data.size())));
    if (maxScrollOffset() != maxBefore)
        emit scrollRangeChanged(maxScrollOffset());
    setScrollOffset(scrollOffset_); // 夹取（reflow/裁剪后偏移可能越界）
    update();
}
```

其六，resizeEvent 的 `if (term_.resize(cols, rows))` 块（:192-195）替换为：

```cpp
    if (term_.resize(cols, rows)) {
        emit gridResized(cols, rows);
        emit scrollRangeChanged(maxScrollOffset()); // M14：reflow 改变历史行数
        setScrollOffset(scrollOffset_);             // 夹取
        update();
    }
```

其七，文件末尾追加新函数实现：

```cpp
int ZzCoreViewWidget::maxScrollOffset() const noexcept
{
    return static_cast<int>(term_.historyView().lineCount()); // Alternate 屏契约恒 0
}

void ZzCoreViewWidget::setScrollOffset(int offset)
{
    offset = qBound(0, offset, maxScrollOffset());
    if (offset == scrollOffset_)
        return;
    scrollOffset_ = offset;
    emit scrollOffsetChanged(scrollOffset_);
    update();
}

ZzLineView ZzCoreViewWidget::lineViewAtVisibleRow(int row) const
{
    // 统一坐标：历史区 [0, hist) 在前、屏幕区 [hist, hist+rows) 在后；
    // 视口底部对齐，scrollOffset_ 为离底行数。
    const auto hist = term_.historyView().lineCount();
    const auto u    = hist - static_cast<std::size_t>(scrollOffset_)
                    + static_cast<std::size_t>(row);
    if (u < hist)
        return term_.historyView().lineAt(u);
    return term_.renderView().lineAt(static_cast<int>(u - hist));
}

QString ZzCoreViewWidget::visibleLineTextForTest(int row) const
{
    const ZzLineView line = lineViewAtVisibleRow(row);
    QString out;
    for (int col = 0; col < line.cellCount(); ++col)
        out += QString::fromStdString(line.cellAt(col).text);
    return out;
}

void ZzCoreViewWidget::wheelEvent(QWheelEvent* event)
{
    if (term_.isAlternateScreen()) { // Alternate 无历史（契约 lineCount 恒 0）
        event->ignore();
        return;
    }
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0) {
        setScrollOffset(scrollOffset_ - steps * 3); // 每滚轮格 3 行
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}
```

- [ ] **步骤 4：main.cpp 两链装滚动条**

`spike/main.cpp` 三处。其一，include 区（`#include <QInputDialog>` 前按字母序）追加：

```cpp
#include <QHBoxLayout>
#include <QScrollBar>
```

其二，runLocal 尾部（:151-153）把：

```cpp
    widget.resize(widget.sizeHint());
    widget.show();
    return QCoreApplication::exec();
```

替换为（runSsh 尾部 :299-301 同款替换，两处相同代码）：

```cpp
    // M14：滚动条与 widget 同窗装配（spike 最小形态：上限跟随历史行数）。
    QScrollBar scrollBar(Qt::Vertical);
    scrollBar.setRange(0, 0);
    QObject::connect(&widget, &ZzCoreViewWidget::scrollRangeChanged, &scrollBar,
                     &QScrollBar::setMaximum);
    QObject::connect(&widget, &ZzCoreViewWidget::scrollOffsetChanged, &scrollBar,
                     &QScrollBar::setValue);
    QObject::connect(&scrollBar, &QScrollBar::valueChanged, &widget,
                     &ZzCoreViewWidget::setScrollOffset);

    QWidget window;
    QHBoxLayout layout(&window);
    layout.setContentsMargins(0, 0, 0, 0);
    layout.addWidget(&widget, 1);
    layout.addWidget(&scrollBar);
    window.resize(widget.sizeHint().width() + scrollBar.sizeHint().width(),
                  widget.sizeHint().height());
    window.show();
    return QCoreApplication::exec();
```

其三，runSsh 尾部（:299-301）做与其二完全相同的替换（两链装配一致；probe 模式不受影响——轮询的是 renderView 屏幕文本，offset 恒 0）。

- [ ] **步骤 5：构建 + 自动测试 + probe 双链自检**

运行（ZzClawTerm 仓根目录）：
```bash
cmake --build build/spike-debug
ctest --test-dir build/spike-debug --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/spike-debug/zzcore_spike --local --probe
QT_QPA_PLATFORM=offscreen ./build/spike-debug/zzcore_spike --ssh localhost --probe
```
预期：构建零告警；ctest **1/1**（4 用例全绿）；两链 probe 均输出 PROBE-OK、退出码 0。

- [ ] **步骤 6：Commit（ZzClawTerm 仓）**

```bash
git add spike/ZzCoreViewWidget.h spike/ZzCoreViewWidget.cpp spike/main.cpp spike/tests/tst_spikerender.cpp
git commit -m "feat(spike): M14 历史滚动——滚动偏移拼接历史段与屏幕段、滚轮与滚动条"
```

- [ ] **步骤 7：人工验证清单移交（用户执行）**

向用户移交以下清单（两链各一遍）：

```bash
cd /home/zz/Jackfahdin/github/ZzClawTerm
./build/spike-debug/zzcore_spike --local          # 本地 PTY 链
./build/spike-debug/zzcore_spike --ssh localhost  # SSH 链
```

- [ ] ls 若干次产出历史后，滚轮上滚可见历史行、滚动条位置同步
- [ ] 回滚到底部后显示最新屏幕内容，光标正常
- [ ] 上滚状态下按键盘，视口自动回底
- [ ] vim 打开后滚轮不翻历史（Alternate 滚动禁用），退出 vim 后历史恢复可滚
- [ ] 拖动缩放窗口后滚动位置不越界、内容不错乱

---

## 收尾（任务 4 完成后）

- ZzTermCore 仓打 tag `m14` 并推送（`git tag m14 && git push origin m14`）。
- 更新 `docs/superpowers/specs/2026-09-29-m13-spike-record.md` §3 缺口 1 的状态标注为「M14 已闭环」（一行编辑，随 m14 tag 前最后 commit 或独立 docs commit）。
- 人工验证现象按 M13 惯例定性入 M14 完成记录（新现象入 spike widget bug backlog，Core 契约层缺陷单独跟踪）。

## 实施勘误（SDD 审查中修正的计划原文缺陷，代码以仓库为准）

1. 任务 4 步骤 3 其七 wheelEvent：`scrollOffset_ - steps * 3` 方向写反（offset 语义为离底行数，上滚应增大），实施修为 `+ steps * 3` 并提单点钩子 scrollByWheelStepsForTest（ZzClawTerm f0ed50b）。
2. 任务 4 步骤 4 其二滚动条装配：`QWidget window` 栈对象写法致 double free（layout 收编栈对象后析构顺序冲突），实施修为 new 分配由 QApplication 析构回收（ZzClawTerm 5fb5fcd）。
3. 任务 4 步骤 4 滚动条连接：直连绑定致 thumb 朝向倒置且历史增长时稳态脱同步，实施修为 lambda 映射 value = maxScrollOffset - offset 并在 scrollRangeChanged 处理中同步 value（ZzClawTerm f0ed50b、5e11e71）。
