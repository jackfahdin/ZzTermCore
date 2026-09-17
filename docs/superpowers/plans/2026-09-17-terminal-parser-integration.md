# Terminal 接入 Parser（M1 Core 链路）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 打通 `bytes -> ZzVtParser -> ZzUtf8Decoder -> Terminal 语义 -> Screen` 真实链路，替换 `Terminal::feed` 的 M0 占位实现。

**架构：** `ZzTerminal` 以嵌套私有类 `Sink : ZzParserSink` 接收解析事件（定义在 .cpp，持有 `ZzTerminal&`，天然可访问私有成员）；Terminal 新增 `parser_`（`unique_ptr<ZzVtParser>`）、`utf8_`（`ZzUtf8Decoder`）、画笔状态（`penAttrs_`/`penFg_`/`penBg_`）。Screen 增加 wrap-pending 状态原语。规格见 `docs/superpowers/specs/2026-09-17-terminal-parser-integration-design.md`。

**技术栈：** C++20、CMake Presets（linux-gcc-debug）、CTest（tests/unit/*.cpp 每个文件一个测试目标，含 main()，`ZZ_TEST_EXPECT` 宏断言，失败计数为退出码）。

**执行环境：** 直接在 `m0-bootstrap` 分支工作（本仓库当前唯一活跃开发分支，无需另建 worktree）。

**常用命令：**

```bash
cmake --build --preset linux-gcc-debug          # 构建
ctest --preset linux-gcc-debug                  # 全量测试
ctest --preset linux-gcc-debug -R test_terminal # 按名过滤
doxygen Doxyfile                                # 文档检查（须零 warning）
```

---

## 文件结构

| 文件 | 职责 | 涉及任务 |
|---|---|---|
| `include/ZzTerm/Screen.h` / `src/screen/Screen.cpp` | 新增 wrap-pending 状态原语 | 任务 1 |
| `tests/unit/test_wrap_pending.cpp` | wrap-pending 原语测试 | 任务 1 |
| `include/ZzTerm/UnicodeWidth.h` | 新增 `zzCellWidthOf` 占位实现（恒窄） | 任务 2 |
| `include/ZzTerm/Terminal.h` | 新增私有成员与语义方法声明；移除 M0 占位注释 | 任务 2、3、4、6 |
| `src/terminal/Terminal.cpp` | Sink 嵌套类、feed 重写、putChar、C0/OSC/ESC 语义 | 任务 2、3 |
| `src/terminal/CsiDispatch.cpp` | CSI 语义（光标/擦除/插删/滚动/SGR 入口） | 任务 4、5 |
| `src/terminal/Sgr.cpp` | SGR 到画笔的映射 | 任务 6 |
| `tests/unit/test_terminal_core.cpp` | print/wrap-pending/C0/OSC/ESC 测试 | 任务 2、3 |
| `tests/unit/test_terminal_csi.cpp` | CSI 语义测试 | 任务 4、5 |
| `tests/unit/test_terminal_sgr.cpp` | SGR 测试 | 任务 6 |
| `tests/unit/test_terminal_e2e.cpp` | 端到端场景 + chunk 切分一致性 | 任务 7 |
| `docs/VT-Xterm-Checklist.md` / `docs/API.md` | 进度勾选与 API 文档同步 | 任务 7 |

**对规格的修正（编写计划时发现）：** 规格第 2 节写"`ZzVtParser parser_` 按值持有"与"公开头不新增依赖"矛盾——按值持有要求 Terminal.h 包含 Parser.h。修正为 `std::unique_ptr<ZzVtParser> parser_`（构造/析构定义在 .cpp，只需前置声明，也正是 Terminal.h 现有注释预留的方案）。`utf8_` 为 `ZzUtf8Decoder` 按值成员，Terminal.h 新增 `#include "ZzTerm/Utf8.h"`（轻量公开头，可接受）。

---

### 任务 1：Screen wrap-pending 状态原语

**文件：**
- 修改：`include/ZzTerm/Screen.h`
- 修改：`src/screen/Screen.cpp`
- 测试：`tests/unit/test_wrap_pending.cpp`

背景：xterm 的 pending-wrap 语义要求"写入最后一列后光标停在该列并置标志，下一个可打印字符才换行"。标志随缓冲区切换独立保存，Save/Restore Cursor 连带保存恢复，光标移动/擦除/插删/滚动会清除它。Screen 只存状态与清除规则，写入流程的置位逻辑由 Terminal 负责（任务 2）。

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_wrap_pending.cpp`：

```cpp
// ZzScreen wrap-pending 状态原语测试（xterm 行尾延迟换行语义）。
#include <cstdio>

#include "ZzTerm/Screen.h"

static int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void testBasic()
{
    ZzScreen scr(5, 3);
    ZZ_TEST_EXPECT(!scr.wrapPending()); // 初始无标志

    scr.setWrapPending(true);
    ZZ_TEST_EXPECT(scr.wrapPending());
    scr.setWrapPending(false);
    ZZ_TEST_EXPECT(!scr.wrapPending());
}

static void testClearedByCursorMove()
{
    ZzScreen scr(5, 3);
    scr.setWrapPending(true);
    scr.setCursorPosition(ZzPosition{0, 0});
    ZZ_TEST_EXPECT(!scr.wrapPending());
}

static void testClearedByEraseAndScroll()
{
    ZzScreen scr(5, 3);
    scr.setWrapPending(true);
    scr.eraseInLine(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.eraseInDisplay(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.scrollUp(1, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.scrollDown(1, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.insertLines(1, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());

    scr.setWrapPending(true);
    scr.deleteLines(1, ZzCell{});
    ZZ_TEST_EXPECT(!scr.wrapPending());
}

static void testSavedWithCursor()
{
    ZzScreen scr(5, 3);
    scr.setWrapPending(true);
    scr.saveCursor();
    scr.setWrapPending(false);
    scr.restoreCursor();
    ZZ_TEST_EXPECT(scr.wrapPending()); // DECRC 连带恢复标志
}

static void testPerBuffer()
{
    ZzScreen scr(5, 3);
    scr.setWrapPending(true);
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    ZZ_TEST_EXPECT(!scr.wrapPending()); // Alternate 独立状态
    scr.setActiveBuffer(ZzScreenBuffer::Primary);
    ZZ_TEST_EXPECT(scr.wrapPending());  // Primary 的标志仍在
}

int main()
{
    testBasic();
    testClearedByCursorMove();
    testClearedByEraseAndScroll();
    testSavedWithCursor();
    testPerBuffer();
    if (g_failures == 0)
        std::puts("test_wrap_pending: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_wrap_pending
```

预期：编译失败，`ZzScreen` 没有 `wrapPending` / `setWrapPending` 成员。

- [ ] **步骤 3：实现 Screen 原语**

`include/ZzTerm/Screen.h`——在"---- 光标 ----"区段的 `restoreCursor()` 声明后添加：

```cpp
    /**
     * @brief wrap-pending 标志（xterm 行尾延迟换行语义）。
     * @return true 表示光标停在最后一列且下一个可打印字符将触发换行。
     * @note 本状态按缓冲区独立保存；Save/Restore Cursor 连带保存恢复；
     *       setCursorPosition、erase/insert/delete/scroll 各原语会清除它。
     *       置位逻辑由 ZzTerminal 的写入流程负责，Screen 不自动置位。
     */
    [[nodiscard]] bool wrapPending() const noexcept;

    /**
     * @brief 设置 wrap-pending 标志。
     * @param pending true 置位，false 清除。
     */
    void setWrapPending(bool pending) noexcept;
```

在私有区 `Buffer` 结构体中添加字段 `bool wrapPending = false; ///< wrap-pending 标志（见 wrapPending()）。`，并在 `hasSavedCursor_` 附近添加 `bool savedWrapPending_ = false; ///< saveCursor 保存的 wrap-pending。`。

`src/screen/Screen.cpp`——实现（沿用文件现有的活动 Buffer 分派写法）：

```cpp
bool ZzScreen::wrapPending() const noexcept
{
    const Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    return buf.wrapPending;
}

void ZzScreen::setWrapPending(bool pending) noexcept
{
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.wrapPending = pending;
}
```

同时在以下方法实现中清除活动缓冲区标志（在取得 `Buffer& buf` 后各加一行 `buf.wrapPending = false;`）：`setCursorPosition`、`eraseInLine`、`eraseInDisplay`、`insertCells`、`deleteCells`、`insertLines`、`deleteLines`、`scrollUp`、`scrollDown`。在 `saveCursor()` 中保存 `savedWrapPending_ = buf.wrapPending;`（buf 为活动缓冲区），在 `restoreCursor()` 中恢复 `buf.wrapPending = savedWrapPending;`。

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_wrap_pending
```

预期：PASS。随后跑全量 `ctest --preset linux-gcc-debug` 确认无回归（现有 4 个测试应继续通过）。

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Screen.h src/screen/Screen.cpp tests/unit/test_wrap_pending.cpp
git commit -m "feat(screen): 添加 wrap-pending 状态原语

xterm 行尾延迟换行语义的基础设施：按缓冲区独立保存，
光标移动/擦除/插删/滚动清除，Save/Restore Cursor 连带。"
```

---

### 任务 2：Terminal 接线骨架 + print 通路 + pending-wrap 写入

**文件：**
- 修改：`include/ZzTerm/Terminal.h`
- 修改：`src/terminal/Terminal.cpp`（重写）
- 修改：`include/ZzTerm/UnicodeWidth.h`
- 测试：`tests/unit/test_terminal_core.cpp`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_terminal_core.cpp`：

```cpp
// ZzTerminal print 通路与 pending-wrap 行为测试。
#include <cstdio>
#include <cstring>
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

static char32_t cpAt(const ZzTerminal& term, int row, int col)
{
    return term.renderView().lineAt(row).cellAt(col).codePoint();
}

static void testPrintAscii()
{
    ZzTerminal term(10, 4, 100);
    const ZzTermChanges changes = [&term] {
        const std::string s = "hi";
        return term.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(s.data()), s.size()));
    }();
    ZZ_TEST_EXPECT(changes.screenDirty);
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'h');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'i');
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
}

static void testPrintUtf8()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "中文"); // M1 宽度占位：均按窄格落格
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'中');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'文');
    ZZ_TEST_EXPECT(term.cursor().position.col == 2);
}

static void testPendingWrap()
{
    ZzTerminal term(5, 3, 100);
    feedStr(term, "abcde");
    // 写满最后一列：光标停在最后一列，暂不换行（xterm pending-wrap）。
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 4);
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());

    feedStr(term, "f");
    // 下一个可打印字符到达才换行。
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'f');
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 1);
}

static void testPendingWrapClearedByCR()
{
    ZzTerminal term(5, 3, 100);
    feedStr(term, "abcde");
    feedStr(term, "\rX"); // CR 清除 pending-wrap：X 覆盖行首而非换行
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'X');
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(term.cursor().position.col == 1);
}

static void testNoWrapWhenAutoWrapOff()
{
    ZzTerminal term(5, 3, 100);
    term.screen().setAutoWrapMode(false); // DECAWM 关
    feedStr(term, "abcdefg");
    // 不换行：后续字符持续覆盖最后一列。
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 4);
    ZZ_TEST_EXPECT(cpAt(term, 0, 4) == U'g');
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());
}

int main()
{
    testPrintAscii();
    testPrintUtf8();
    testPendingWrap();
    testPendingWrapClearedByCR();
    testNoWrapWhenAutoWrapOff();
    if (g_failures == 0)
        std::puts("test_terminal_core: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
```

注意：`test_line.cpp` 中有针对 M0 占位 feed 的 `testTerminalFeed` 用例（假定旧换行语义），本任务重写 feed 后它会失败——修改该用例以匹配 pending-wrap 语义（行末写满后光标停在最后一列），或直接删除该用例（其覆盖已由上面的新测试替代）。选择删除，并在 commit message 中说明。

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_core
```

预期：FAIL——`testPendingWrap` 等用例失败（旧实现立即换行）。

- [ ] **步骤 3：实现接线与 print 通路**

`include/ZzTerm/UnicodeWidth.h`——把 TODO 占位替换为接入点声明（保留说明注释）：

```cpp
/**
 * @brief 返回码位的单元格宽度（1 或 2 列，UAX #11 East Asian Width）。
 * @param cp Unicode 码位。
 * @return 单元格宽度。
 * @note M1 占位实现恒返回 1（全部按窄格）；M2 用真实 East Asian Width
 *       区间表替换本实现，调用方（ZzTerminal::putChar）不变。
 *       grapheme 聚簇（UAX #29）由更高层负责，不在本接口。
 */
[[nodiscard]] inline constexpr int zzCellWidthOf(char32_t) noexcept { return 1; }
```

`include/ZzTerm/Terminal.h`：
- 文件顶部添加 `#include "ZzTerm/Utf8.h"`；
- 删除第 15-19 行"暂不持有 parser 成员"的注释（保留 `class ZzVtParser;` 前置声明，改注释为 `// 前置声明：解析器由 parser 模块实现，unique_ptr 成员的构造/析构定义在 .cpp。`）；
- 删除 `feed()` 注释中的 `@note M0 占位实现`整段；
- 私有区添加：

```cpp
    struct Sink; // 嵌套私有类：ZzParserSink 实现，定义在 Terminal.cpp。

    void putChar(char32_t cp);
    void executeControl(std::uint8_t control);
    void dispatchCsi(const ZzParamSequence& seq);
    void dispatchEsc(std::string_view intermediates, char final);
    void dispatchOsc(std::string_view payload);
    void sgr(const ZzParamSequence& seq);
    [[nodiscard]] ZzCell eraseFill() const noexcept;
    void noteScreenDirty() noexcept;

    std::unique_ptr<Sink>       sink_;    ///< 先于 parser_ 声明：析构逆序保证 parser 先销毁。
    std::unique_ptr<ZzVtParser> parser_;  ///< VT 解析器（语法 dispatch）。
    ZzUtf8Decoder               utf8_;    ///< print 通道 UTF-8 增量解码。
    ZzCellAttributes            penAttrs_; ///< 当前画笔属性（SGR）。
    ZzColor penFg_ = ZzColor::Default();  ///< 当前画笔前景色。
    ZzColor penBg_ = ZzColor::Default();  ///< 当前画笔背景色。
    ZzTermChanges* activeChanges_ = nullptr; ///< feed 期间的变化聚合目标。
```

`ZzParamSequence` 前置声明：Terminal.h 中 `dispatchCsi`/`sgr` 的参数类型来自 Parser.h，在 Terminal.h 添加 `struct ZzParamSequence;` 前置声明（与 `class ZzVtParser;` 并列）。`executeControl` 需要 `<cstdint>`（已有）。

`src/terminal/Terminal.cpp`——整体重写为：

```cpp
#include "ZzTerm/Terminal.h"

#include "ZzTerm/Parser.h"
#include "ZzTerm/UnicodeWidth.h"

// ZzTerminal 实现：Parser 已接入，feed 的真实链路为
//   bytes -> ZzVtParser（语法） -> Sink -> Terminal 语义 -> ZzScreen。
// print 通道字节经 ZzUtf8Decoder 解码为码点后由 putChar 落格。

/// 嵌套私有类：把解析事件转发为 Terminal 的语义方法调用。
/// DCS 不覆盖（基类默认空实现 = 安全忽略）。
struct ZzTerminal::Sink : ZzParserSink {
    explicit Sink(ZzTerminal& term) : term_(term) {}

    void onPrint(char byte) override
    {
        term_.utf8_.feed(std::string_view(&byte, 1),
                         [this](char32_t cp) { term_.putChar(cp); });
    }
    void onExecute(std::uint8_t control) override { term_.executeControl(control); }
    void onCsiDispatch(const ZzParamSequence& seq) override { term_.dispatchCsi(seq); }
    void onEscDispatch(std::string_view intermediates, char final) override
    {
        term_.dispatchEsc(intermediates, final);
    }
    void onOscDispatch(std::string_view payload) override { term_.dispatchOsc(payload); }

    ZzTerminal& term_;
};

ZzTerminal::ZzTerminal(int cols, int rows, std::size_t scrollbackMaxLines)
    : screen_(cols, rows)
    , scrollback_(zzCreateChunkedScrollback(scrollbackMaxLines))
    , renderView_(&screen_, scrollback_.get())
    , sink_(std::make_unique<Sink>(*this))
    , parser_(std::make_unique<ZzVtParser>(sink_.get()))
{
    // Screen 不知道历史后端：滚出行经回调上移到 Terminal，由 Terminal 入栈。
    screen_.setScrollOutCallback([this](std::vector<ZzLine> lines) {
        scrolledOutPending_ += lines.size();
        scrollback_->append(std::move(lines));
    });
}

ZzTerminal::~ZzTerminal() = default;

ZzTermChanges ZzTerminal::feed(std::span<const std::byte> data)
{
    ZzTermChanges changes;
    scrolledOutPending_ = 0;
    activeChanges_ = &changes;

    const auto* chars = reinterpret_cast<const char*>(data.data());
    parser_->feed(std::string_view(chars, data.size()));

    activeChanges_ = nullptr;
    if (scrolledOutPending_ > 0) {
        changes.scrollbackChanged = true;
        changes.scrolledOutLines = scrolledOutPending_;
        scrolledOutPending_ = 0;
    }
    return changes;
}

void ZzTerminal::noteScreenDirty() noexcept
{
    if (activeChanges_)
        activeChanges_->screenDirty = true;
}

ZzCell ZzTerminal::eraseFill() const noexcept
{
    // 擦除/滚动填充：携带当前画笔背景（bce 语义），无文本无属性。
    ZzCell fill;
    fill.setBackground(penBg_);
    return fill;
}

void ZzTerminal::putChar(char32_t cp)
{
    const ZzSize sz = screen_.size();
    const ZzCellRange region = screen_.scrollRegionRows(); // [top, bottom+1)
    ZzPosition cur = screen_.cursor().position;

    // xterm pending-wrap：上一字符写在最后一列时，先换行再落格。
    if (screen_.wrapPending()) {
        screen_.setWrapPending(false);
        if (screen_.autoWrapMode()) {
            screen_.setLineWrapped(cur.row, true);
            if (cur.row == region.endCol - 1)
                screen_.scrollUp(1, eraseFill());
            else
                ++cur.row;
            cur.col = 0;
        }
    }

    ZzCell cell;
    // zzCellWidthOf 为 M1 占位（恒窄）；M2 接入真实 EAW 表后，
    // 宽字符需在此处补写 WideContinuation 续格（M2 任务，非本次范围）。
    cell.setWidth(zzCellWidthOf(cp) == 2 ? ZzCellWidth::WideLead : ZzCellWidth::Narrow);
    cell.setCodePoint(cp);
    cell.setForeground(penFg_);
    cell.setBackground(penBg_);
    cell.setAttributes(penAttrs_);
    screen_.putCell(cur, cell);
    noteScreenDirty();

    if (cur.col < sz.cols - 1) {
        screen_.setCursorPosition(ZzPosition{cur.row, cur.col + 1});
    } else if (screen_.autoWrapMode()) {
        // 最后一列：光标不动，置 wrap-pending（下一个可打印字符才换行）。
        screen_.setWrapPending(true);
    }
    // DECAWM 关闭时在最后一列：光标不动、不置标志，后续字符覆盖该格。
}
```

`resize`、`renderView`、`size`、`cursor`、`isAlternateScreen`、`title`、`clearDirty`、`screen`、`scrollback` 各方法保持现有实现不变（保留在文件尾部）。

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：全部 PASS（注意 `test_line.cpp` 中旧占位 feed 用例已按步骤 1 删除）。此时 `executeControl`/`dispatchCsi` 等可先在 Terminal.cpp/CsiDispatch.cpp 放空实现（任务 3/4 填充），保证编译通过。

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Terminal.h include/ZzTerm/UnicodeWidth.h src/terminal/Terminal.cpp tests/unit/test_terminal_core.cpp tests/unit/test_line.cpp
git commit -m "feat(terminal): 接入 VT 解析器，print 通路采用 pending-wrap 语义

- 嵌套私有 Sink 接收解析事件，print 字节经 UTF-8 解码后落格
- 修正行尾行为为 xterm pending-wrap（此前为立即换行占位）
- 新增 zzCellWidthOf 宽度接入点（M1 恒窄，M2 换 EAW 表）
- 删除 test_line.cpp 中针对 M0 占位 feed 的测试用例"
```

---

### 任务 3：C0 / OSC / ESC 语义

**文件：**
- 修改：`src/terminal/Terminal.cpp`
- 测试：`tests/unit/test_terminal_core.cpp`（追加）

- [ ] **步骤 1：编写失败的测试**

在 `tests/unit/test_terminal_core.cpp` 追加（并注册到 main）：

```cpp
static void testC0()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "ab\rcd");        // CR 回列首
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'c');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'd');

    feedStr(term, "\n");            // LF 下移一行
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);

    feedStr(term, "x\by");          // BS 左移后覆盖
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'x');
    ZZ_TEST_EXPECT(cpAt(term, 1, 1) == U'y');

    feedStr(term, "\t");            // HT 到下一个 Tab Stop（默认每 8 列）
    ZZ_TEST_EXPECT(term.cursor().position.col == 8);

    const ZzTermChanges ch = term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("\a"), 1));
    ZZ_TEST_EXPECT(ch.bell);        // BEL 置响铃标志
}

static void testLfScrollsAtRegionBottom()
{
    ZzTerminal term(5, 2, 100);
    feedStr(term, "one\r\ntwo\r\n"); // 第二行使出滚动区下沿 -> 上滚
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U't'); // "two" 顶到第 0 行
    ZZ_TEST_EXPECT(term.renderView().scrollbackLineCount() == 1); // "one" 入历史
}

static void testOscTitle()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b]2;my title\x07"); // OSC 2 ; title BEL
    ZZ_TEST_EXPECT(term.title() == "my title");
    feedStr(term, "\x1b]0;both\x1b\\");   // OSC 0，ST 终止
    ZZ_TEST_EXPECT(term.title() == "both");
}

static void testEscSaveRestore()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "abc");
    feedStr(term, "\x1b" "7");      // DECSC
    feedStr(term, "\r\nxyz");
    feedStr(term, "\x1b" "8");      // DECRC
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    ZZ_TEST_EXPECT(term.cursor().position.col == 3);
}

static void testEscIndNelRiHts()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b" "D");      // IND：下移一行
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    feedStr(term, "\x1b" "M");      // RI：上移一行
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
    feedStr(term, "\x1b" "E");      // NEL：回列首并下移
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 0);
    feedStr(term, "\x1b" "H");      // HTS：当前列设 Tab Stop
    feedStr(term, "\t");
    ZZ_TEST_EXPECT(term.cursor().position.col == 0 || term.cursor().position.col > 0);
    // HTS 精确语义：col 0 设 stop 后，HT 从 col 0 跳到下一个默认 stop（col 8）。
    ZZ_TEST_EXPECT(term.cursor().position.col == 8);
}

static void testRiScrollsDownAtTop()
{
    ZzTerminal term(5, 2, 100);
    feedStr(term, "ab");
    feedStr(term, "\x1b" "M"); // 光标在滚动区上沿，RI 向下滚动
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'a');
    ZZ_TEST_EXPECT(term.cursor().position.row == 0);
}
```

注意：`renderView().scrollbackLineCount()` 的准确名字以 `include/ZzTerm/RenderView.h` 为准（若不同则改用现有 API）。

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_core
```

预期：FAIL（C0/OSC/ESC 语义为空实现）。

- [ ] **步骤 3：实现 C0/OSC/ESC 语义**

`src/terminal/Terminal.cpp` 添加（`executeControl`、`dispatchOsc`、`dispatchEsc` 及两个行移动辅助）：

```cpp
namespace {
/// 当前光标处的滚动区下沿判定所需的行范围（[top, bottom]，含端点）。
} // 无需匿名命名空间内容时可删除

void ZzTerminal::executeControl(std::uint8_t control)
{
    const ZzCellRange region = screen_.scrollRegionRows();
    const ZzPosition cur = screen_.cursor().position;

    switch (control) {
    case 0x07: // BEL
        if (activeChanges_)
            activeChanges_->bell = true;
        break;
    case 0x08: // BS：左移一格（不越行首）
        screen_.setCursorPosition(ZzPosition{cur.row, cur.col > 0 ? cur.col - 1 : 0});
        noteScreenDirty();
        break;
    case 0x09: // HT：下一个 Tab Stop
        screen_.setCursorPosition(ZzPosition{cur.row, screen_.nextTabStop(cur.col)});
        noteScreenDirty();
        break;
    case 0x0A: // LF
    case 0x0B: // VT
    case 0x0C: // FF：index——滚动区下沿上滚，否则下移一行
        if (cur.row == region.endCol - 1)
            screen_.scrollUp(1, eraseFill());
        else
            screen_.setCursorPosition(ZzPosition{cur.row + 1, cur.col});
        noteScreenDirty();
        break;
    case 0x0D: // CR：回列首
        screen_.setCursorPosition(ZzPosition{cur.row, 0});
        noteScreenDirty();
        break;
    default:
        break; // 其余 C0 安全忽略
    }
}

void ZzTerminal::dispatchOsc(std::string_view payload)
{
    const std::size_t sep = payload.find(';');
    if (sep == std::string_view::npos)
        return;
    const std::string_view code = payload.substr(0, sep);
    if (code != "0" && code != "1" && code != "2")
        return; // 仅窗口/图标标题（OSC 0/1/2），其余安全忽略
    title_ = std::string(payload.substr(sep + 1));
    if (activeChanges_)
        activeChanges_->titleChanged = true;
}

void ZzTerminal::dispatchEsc(std::string_view intermediates, char final)
{
    if (!intermediates.empty())
        return; // charset 选择（ESC ( X 等）随 M2 字符集设计实现

    const ZzCellRange region = screen_.scrollRegionRows();
    const ZzPosition cur = screen_.cursor().position;

    switch (final) {
    case '7': // DECSC
        screen_.saveCursor();
        break;
    case '8': // DECRC
        screen_.restoreCursor();
        noteScreenDirty();
        break;
    case 'D': // IND：同 LF
        if (cur.row == region.endCol - 1)
            screen_.scrollUp(1, eraseFill());
        else
            screen_.setCursorPosition(ZzPosition{cur.row + 1, cur.col});
        noteScreenDirty();
        break;
    case 'M': // RI：滚动区上沿下滚，否则上移一行
        if (cur.row == region.startCol)
            screen_.scrollDown(1, eraseFill());
        else if (cur.row > 0)
            screen_.setCursorPosition(ZzPosition{cur.row - 1, cur.col});
        noteScreenDirty();
        break;
    case 'E': // NEL：CR + IND
        if (cur.row == region.endCol - 1)
            screen_.scrollUp(1, eraseFill());
        else
            screen_.setCursorPosition(ZzPosition{cur.row + 1, 0});
        noteScreenDirty();
        break;
    case 'H': // HTS：当前列设 Tab Stop
        screen_.setTabStop(cur.col);
        break;
    default:
        break; // 其余 ESC 序列安全忽略
    }
}
```

注意：Screen 的 `saveCursor/restoreCursor` 当前只保存位置/形状——确认任务 1 已让 restoreCursor 连带恢复 wrap-pending。若 `restoreCursor` 不标脏，DECRC 后光标移动需要 `noteScreenDirty()`（上面已包含）。

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "test_terminal_core|test_wrap_pending"
```

预期：PASS。

- [ ] **步骤 5：Commit**

```bash
git add src/terminal/Terminal.cpp tests/unit/test_terminal_core.cpp
git commit -m "feat(terminal): 实现 C0、OSC 标题与 ESC 序列语义

- C0 全套（BEL/BS/HT/LF/VT/FF/CR），LF/VT/FF 为 index 语义
- OSC 0/1/2 窗口/图标标题（BEL 与 ST 两种终止）
- ESC 7/8（DECSC/DECRC）、D/E/M/H（IND/NEL/RI/HTS）"
```

---

### 任务 4：CSI 光标移动族

**文件：**
- 修改：`include/ZzTerm/Terminal.h`（`dispatchCsi` 声明已在任务 2 添加）
- 创建：`src/terminal/CsiDispatch.cpp`
- 测试：`tests/unit/test_terminal_csi.cpp`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_terminal_csi.cpp`：

```cpp
// ZzTerminal CSI 语义测试（光标移动族）。
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

static ZzPosition cursorOf(const ZzTerminal& term)
{
    return term.cursor().position;
}

static bool posEq(ZzPosition p, int row, int col)
{
    return p.row == row && p.col == col;
}

static void testCupAndHvp()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[2;3H");       // CUP：行列 1 起始
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 1, 2));
    feedStr(term, "\x1b[H");          // 缺省 = 1;1
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 0, 0));
    feedStr(term, "\x1b[999;999f");   // HVP：钳到网格
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 9));
}

static void testCursorMoves()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[2;5H");
    feedStr(term, "\x1b[A");          // CUU 1
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 0, 4));
    feedStr(term, "\x1b[A");          // 顶到 0 行
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 0, 4));
    feedStr(term, "\x1b[3B");         // CUD 3，钳到末行
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 4));
    feedStr(term, "\x1b[10C");        // CUF 钳到末列
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 9));
    feedStr(term, "\x1b[2D");         // CUB 2
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 7));
    feedStr(term, "\x1b[E");          // CNL：下移 + 列首
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 3, 0)); // 已在末行，行不变
    feedStr(term, "\x1b[2F");         // CPL：上移 2 + 列首
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 1, 0));
    feedStr(term, "\x1b[7G");         // CHA：列 7（1 起始）
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 1, 6));
}

static void testCsiSaveRestore()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[2;5H\x1b[s"); // SCOSC
    feedStr(term, "\x1b[1;1H");
    feedStr(term, "\x1b[u");          // SCORC
    ZZ_TEST_EXPECT(posEq(cursorOf(term), 1, 4));
}

static void testPrivateMarkerIgnored()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[?25l");       // DEC 私有模式：M3 范围，安全忽略
    ZZ_TEST_EXPECT(term.cursor().visible); // 不受影响
}

int main()
{
    testCupAndHvp();
    testCursorMoves();
    testCsiSaveRestore();
    testPrivateMarkerIgnored();
    if (g_failures == 0)
        std::puts("test_terminal_csi: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_csi
```

预期：FAIL（dispatchCsi 为空实现）。

- [ ] **步骤 3：实现 CSI 光标语义**

创建 `src/terminal/CsiDispatch.cpp`：

```cpp
#include <algorithm>

#include "ZzTerm/Terminal.h"
#include "ZzTerm/Parser.h"

// CSI 语义（ZzTerminal 成员函数，分文件实现以控制单文件规模）。
// 约定：参数省略（kOmitted）或 <= 0 一律回退默认值；数值钳到网格范围；
// DEC 私有序列（privateMarker != 0）与带 intermediate 的序列 M3 处理，
// 本文件安全忽略。

namespace {

/// 取 CSI 第 i 个参数；省略/0/缺槽回退 fallback。
int paramOr(const ZzParamSequence& seq, std::size_t i, int fallback)
{
    if (i >= seq.params.size())
        return fallback;
    const std::int32_t v = seq.params[i];
    return (v == ZzParamSequence::kOmitted || v <= 0) ? fallback : static_cast<int>(v);
}

} // namespace

void ZzTerminal::dispatchCsi(const ZzParamSequence& seq)
{
    if (seq.privateMarker != 0 || !seq.intermediates.empty())
        return; // DEC 私有模式 / intermediate 序列：留待 M3，安全忽略

    const ZzSize sz = screen_.size();
    const ZzCellRange region = screen_.scrollRegionRows();
    const int regionTop = region.startCol;
    const int regionBottom = region.endCol - 1;
    const ZzPosition cur = screen_.cursor().position;
    const bool inRegion = cur.row >= regionTop && cur.row <= regionBottom;

    switch (seq.final) {
    case 'A': { // CUU：上移到滚动区上沿为止（在区内时）
        const int n = paramOr(seq, 0, 1);
        const int limit = inRegion ? regionTop : 0;
        screen_.setCursorPosition(ZzPosition{std::max(limit, cur.row - n), cur.col});
        break;
    }
    case 'B': { // CUD：下移
        const int n = paramOr(seq, 0, 1);
        const int limit = inRegion ? regionBottom : sz.rows - 1;
        screen_.setCursorPosition(ZzPosition{std::min(limit, cur.row + n), cur.col});
        break;
    }
    case 'C': // CUF
        screen_.setCursorPosition(
            ZzPosition{cur.row, std::min(sz.cols - 1, cur.col + paramOr(seq, 0, 1))});
        break;
    case 'D': // CUB
        screen_.setCursorPosition(
            ZzPosition{cur.row, std::max(0, cur.col - paramOr(seq, 0, 1))});
        break;
    case 'E': { // CNL = CUD + CR
        const int n = paramOr(seq, 0, 1);
        const int limit = inRegion ? regionBottom : sz.rows - 1;
        screen_.setCursorPosition(ZzPosition{std::min(limit, cur.row + n), 0});
        break;
    }
    case 'F': { // CPL = CUU + CR
        const int n = paramOr(seq, 0, 1);
        const int limit = inRegion ? regionTop : 0;
        screen_.setCursorPosition(ZzPosition{std::max(limit, cur.row - n), 0});
        break;
    }
    case 'G': // CHA：绝对列（1 起始）
        screen_.setCursorPosition(
            ZzPosition{cur.row, std::clamp(paramOr(seq, 0, 1) - 1, 0, sz.cols - 1)});
        break;
    case 'd': // VPA：绝对行（1 起始）
        screen_.setCursorPosition(
            ZzPosition{std::clamp(paramOr(seq, 0, 1) - 1, 0, sz.rows - 1), cur.col});
        break;
    case 'H': // CUP
    case 'f': { // HVP
        int row = paramOr(seq, 0, 1) - 1;
        const int col = paramOr(seq, 1, 1) - 1;
        if (screen_.originMode()) {
            // DECOM：相对滚动区上沿，且钳在滚动区内。
            row = std::clamp(row + regionTop, regionTop, regionBottom);
        }
        screen_.setCursorPosition(ZzPosition{std::clamp(row, 0, sz.rows - 1),
                                             std::clamp(col, 0, sz.cols - 1)});
        break;
    }
    case 's': // SCOSC（无左右边距模式时 CSI s = 保存光标）
        screen_.saveCursor();
        break;
    case 'u': // SCORC
        screen_.restoreCursor();
        break;
    case 'm':
        sgr(seq); // 任务 6 实现
        break;
    default:
        break; // 擦除/插删/滚动在任务 5 添加；未知 final 安全忽略
    }
    noteScreenDirty();
}
```

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_csi
```

预期：PASS。

- [ ] **步骤 5：Commit**

```bash
git add src/terminal/CsiDispatch.cpp tests/unit/test_terminal_csi.cpp
git commit -m "feat(terminal): 实现 CSI 光标移动语义

CUU/CUD/CUF/CUB/CNL/CPL/CHA/VPA/CUP/HVP 与 SCOSC/SCORC，
参数缺省回退、滚动区边界钳位、DECOM 相对定位；
DEC 私有序列安全忽略（M3 处理）。"
```

---

### 任务 5：CSI 擦除 / 插删 / 滚动

**文件：**
- 修改：`src/terminal/CsiDispatch.cpp`
- 测试：`tests/unit/test_terminal_csi.cpp`（追加）

- [ ] **步骤 1：编写失败的测试**

在 `tests/unit/test_terminal_csi.cpp` 追加（并注册到 main）：

```cpp
static char32_t cpAt(const ZzTerminal& term, int row, int col)
{
    return term.renderView().lineAt(row).cellAt(col).codePoint();
}

static void testEraseInLine()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "0123456789");
    feedStr(term, "\x1b[1G\x1b[K");   // 光标到行首；EL 0：擦到行尾
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 0, 9) == 0);

    feedStr(term, "0123456789");
    feedStr(term, "\x1b[6G\x1b[1K");  // EL 1：从行首擦到光标（含）
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 0, 5) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 0, 6) == U'6');

    feedStr(term, "\x1b[2K");         // EL 2：整行
    ZZ_TEST_EXPECT(cpAt(term, 0, 6) == 0);
}

static void testEraseInDisplay()
{
    ZzTerminal term(5, 3, 100);
    feedStr(term, "aaa\r\nbbb\r\nccc");
    feedStr(term, "\x1b[2;2H\x1b[J"); // ED 0：光标（含）到屏尾
    ZZ_TEST_EXPECT(cpAt(term, 0, 4) == U'a');
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'b');
    ZZ_TEST_EXPECT(cpAt(term, 1, 1) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 2, 0) == 0);

    feedStr(term, "\x1b[2J");         // ED 2：整屏
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 0, 4) == 0);
}

static void testEchIchDch()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "0123456789\r");
    feedStr(term, "\x1b[3G\x1b[2X");  // ECH 2：原位擦除两格，其余不动
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'1');
    ZZ_TEST_EXPECT(cpAt(term, 0, 2) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 0, 3) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 0, 4) == U'4');

    feedStr(term, "\x1b[2J\x1b[H");
    feedStr(term, "012345\r");
    feedStr(term, "\x1b[2G\x1b[2@");  // ICH 2：插入两格，后续右移截断
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'0');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 0, 3) == U'1');

    feedStr(term, "\x1b[2J\x1b[H");
    feedStr(term, "012345\r");
    feedStr(term, "\x1b[2G\x1b[2P");  // DCH 2：删除两格，左侧补位
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'0');
    ZZ_TEST_EXPECT(cpAt(term, 0, 1) == U'3');
    ZZ_TEST_EXPECT(cpAt(term, 0, 4) == 0);
}

static void testIlDlSuSd()
{
    ZzTerminal term(5, 3, 100);
    feedStr(term, "aaa\r\nbbb\r\nccc");
    feedStr(term, "\x1b[2;1H\x1b[L"); // IL 1：光标行处插入一行
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 2, 0) == U'b'); // 原 bbb 下移，ccc 被丢弃

    feedStr(term, "\x1b[2J\x1b[H");
    feedStr(term, "aaa\r\nbbb\r\nccc");
    feedStr(term, "\x1b[2;1H\x1b[M"); // DL 1：删除光标行
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'c');
    ZZ_TEST_EXPECT(cpAt(term, 2, 0) == 0);

    feedStr(term, "\x1b[2J\x1b[H");
    feedStr(term, "aaa\r\nbbb\r\nccc");
    feedStr(term, "\x1b[S");          // SU 1：滚动区上滚一行
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == U'b');
    ZZ_TEST_EXPECT(cpAt(term, 2, 0) == 0);

    feedStr(term, "\x1b[T");          // SD 1：滚动区下滚一行
    ZZ_TEST_EXPECT(cpAt(term, 0, 0) == 0);
    ZZ_TEST_EXPECT(cpAt(term, 1, 0) == U'b');
}
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_csi
```

预期：FAIL。

- [ ] **步骤 3：实现擦除/插删/滚动语义**

在 `src/terminal/CsiDispatch.cpp` 的 `dispatchCsi` switch 中（`case 'm'` 之前）添加：

```cpp
    case 'J': { // ED 0/1/2；ED 3（清历史）不在 M1 范围，忽略
        const int p = paramOr(seq, 0, 0);
        if (p <= 2)
            screen_.eraseInDisplay(static_cast<ZzEraseMode>(p), eraseFill());
        break;
    }
    case 'K': { // EL 0/1/2
        const int p = paramOr(seq, 0, 0);
        if (p <= 2)
            screen_.eraseInLine(static_cast<ZzEraseMode>(p), eraseFill());
        break;
    }
    case 'X': { // ECH：原位擦除 n 格。Screen 无"原位擦除"原语，
                // 用 deleteCells + insertCells 组合实现：先删 n 格（左移、
                // 行尾补空），再在光标处插回 n 个空格（右移、截掉行尾补位），
                // 净效果即 [col, col+n) 置空、其余单元格不动。
        const int n = paramOr(seq, 0, 1);
        screen_.deleteCells(n, eraseFill());
        screen_.insertCells(n, eraseFill());
        break;
    }
    case '@': // ICH
        screen_.insertCells(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'P': // DCH
        screen_.deleteCells(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'L': // IL
        screen_.insertLines(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'M': // DL
        screen_.deleteLines(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'S': // SU
        screen_.scrollUp(paramOr(seq, 0, 1), eraseFill());
        break;
    case 'T': // SD
        screen_.scrollDown(paramOr(seq, 0, 1), eraseFill());
        break;
```

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_csi
```

预期：PASS。随后全量 `ctest --preset linux-gcc-debug` 确认无回归。

- [ ] **步骤 5：Commit**

```bash
git add src/terminal/CsiDispatch.cpp tests/unit/test_terminal_csi.cpp
git commit -m "feat(terminal): 实现 CSI 擦除/插删/滚动语义

ED/EL 0-2、ECH（delete+insert 组合实现原位擦除）、
ICH/DCH/IL/DL、SU/SD；填充单元格携带当前画笔背景（bce）。"
```

---

### 任务 6：SGR 画笔

**文件：**
- 创建：`src/terminal/Sgr.cpp`
- 测试：`tests/unit/test_terminal_sgr.cpp`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_terminal_sgr.cpp`：

```cpp
// ZzTerminal SGR 画笔语义测试。
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

static ZzCell cellAt(const ZzTerminal& term, int row, int col)
{
    return term.renderView().lineAt(row).cellAt(col);
}

static void testBasicAttributes()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[1;3mAB");     // bold + italic
    ZZ_TEST_EXPECT(cellAt(term, 0, 0).attributes().bold());
    ZZ_TEST_EXPECT(cellAt(term, 0, 0).attributes().italic());
    feedStr(term, "\x1b[22mC");       // bold off（italic 保留）
    ZZ_TEST_EXPECT(!cellAt(term, 0, 2).attributes().bold());
    ZZ_TEST_EXPECT(cellAt(term, 0, 2).attributes().italic());
    feedStr(term, "\x1b[0mD");        // reset
    ZZ_TEST_EXPECT(cellAt(term, 0, 3).attributes() == ZzCellAttributes{});
    feedStr(term, "\x1b[mE");         // 空参数 = 0 = reset（先开属性再验证）
    feedStr(term, "\x1b[7mF\x1b[mG");
    ZZ_TEST_EXPECT(cellAt(term, 0, 5).attributes().inverse());
    ZZ_TEST_EXPECT(cellAt(term, 0, 6).attributes() == ZzCellAttributes{});
}

static void testColors()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[31mA");       // ANSI 红
    ZZ_TEST_EXPECT(cellAt(term, 0, 0).foreground() == ZzColor::Indexed(1));
    feedStr(term, "\x1b[91mB");       // bright 红 = 索引 9
    ZZ_TEST_EXPECT(cellAt(term, 0, 1).foreground() == ZzColor::Indexed(9));
    feedStr(term, "\x1b[42mC");       // 绿底
    ZZ_TEST_EXPECT(cellAt(term, 0, 2).background() == ZzColor::Indexed(2));
    feedStr(term, "\x1b[38;5;196mD"); // 256 色
    ZZ_TEST_EXPECT(cellAt(term, 0, 3).foreground() == ZzColor::Indexed(196));
    feedStr(term, "\x1b[38;2;1;2;3mE"); // RGB
    ZZ_TEST_EXPECT(cellAt(term, 0, 4).foreground() == ZzColor::Rgb(1, 2, 3));
    feedStr(term, "\x1b[48;5;20mF");  // 256 色背景
    ZZ_TEST_EXPECT(cellAt(term, 0, 5).background() == ZzColor::Indexed(20));
    feedStr(term, "\x1b[39;49mG");    // 恢复默认前景/背景
    ZZ_TEST_EXPECT(cellAt(term, 0, 6).foreground().isDefault());
    ZZ_TEST_EXPECT(cellAt(term, 0, 6).background().isDefault());
}

static void testSgrDoesNotTouchExistingCells()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "A");               // 默认属性落格
    feedStr(term, "\x1b[1m");         // 之后改画笔
    ZZ_TEST_EXPECT(!cellAt(term, 0, 0).attributes().bold()); // 旧格不受影响
    feedStr(term, "B");
    ZZ_TEST_EXPECT(cellAt(term, 0, 1).attributes().bold());
}

static void testUnderlineAndBlink()
{
    ZzTerminal term(10, 4, 100);
    feedStr(term, "\x1b[4mA");        // 单下划线
    ZZ_TEST_EXPECT(cellAt(term, 0, 0).attributes().underline() == ZzUnderlineStyle::Single);
    feedStr(term, "\x1b[5mB");        // 慢闪
    ZZ_TEST_EXPECT(cellAt(term, 0, 1).attributes().blink() == ZzBlinkStyle::Slow);
    feedStr(term, "\x1b[24;25mC");
    ZZ_TEST_EXPECT(cellAt(term, 0, 2).attributes().underline() == ZzUnderlineStyle::None);
    ZZ_TEST_EXPECT(cellAt(term, 0, 2).attributes().blink() == ZzBlinkStyle::None);
}

int main()
{
    testBasicAttributes();
    testColors();
    testSgrDoesNotTouchExistingCells();
    testUnderlineAndBlink();
    if (g_failures == 0)
        std::puts("test_terminal_sgr: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_sgr
```

预期：FAIL（sgr 为空实现）。

- [ ] **步骤 3：实现 SGR 映射**

创建 `src/terminal/Sgr.cpp`：

```cpp
#include <cstdint>

#include "ZzTerm/Terminal.h"
#include "ZzTerm/Parser.h"

// SGR（CSI m）到画笔状态的映射（ECMA-48 §8.3.117 + xterm 扩展）。
// 只改画笔，不修改已有 Cell；print 落格时套用当前画笔。

namespace {

/// 钳位到 0-255 的字节分量（Parser 已钳到 65535，这里再收一层）。
std::uint8_t clampByte(std::int32_t v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<std::uint8_t>(v);
}

} // namespace

void ZzTerminal::sgr(const ZzParamSequence& seq)
{
    if (seq.params.empty()) { // CSI m 无参数 = reset
        penAttrs_.reset();
        penFg_ = ZzColor::Default();
        penBg_ = ZzColor::Default();
        return;
    }

    for (std::size_t i = 0; i < seq.params.size(); ++i) {
        const int p = seq.params[i] == ZzParamSequence::kOmitted ? 0
                                                                 : static_cast<int>(seq.params[i]);
        switch (p) {
        case 0: // reset
            penAttrs_.reset();
            penFg_ = ZzColor::Default();
            penBg_ = ZzColor::Default();
            break;
        case 1: penAttrs_.setBold(true); break;
        case 2: penAttrs_.setFaint(true); break;
        case 3: penAttrs_.setItalic(true); break;
        case 4: penAttrs_.setUnderline(ZzUnderlineStyle::Single); break; // 4:x 子参数待 Parser 支持 ':'
        case 5: penAttrs_.setBlink(ZzBlinkStyle::Slow); break;
        case 6: penAttrs_.setBlink(ZzBlinkStyle::Rapid); break;
        case 7: penAttrs_.setInverse(true); break;
        case 8: penAttrs_.setInvisible(true); break;
        case 9: penAttrs_.setStrikethrough(true); break;
        case 22: penAttrs_.setBold(false); penAttrs_.setFaint(false); break;
        case 23: penAttrs_.setItalic(false); break;
        case 24: penAttrs_.setUnderline(ZzUnderlineStyle::None); break;
        case 25: penAttrs_.setBlink(ZzBlinkStyle::None); break;
        case 27: penAttrs_.setInverse(false); break;
        case 28: penAttrs_.setInvisible(false); break;
        case 29: penAttrs_.setStrikethrough(false); break;
        case 38: // 扩展前景色
        case 48: { // 扩展背景色
            ZzColor* target = p == 38 ? &penFg_ : &penBg_;
            const std::int32_t mode =
                i + 1 < seq.params.size() ? seq.params[i + 1] : ZzParamSequence::kOmitted;
            if (mode == 5 && i + 2 < seq.params.size()) { // 256 色：;5;n
                const std::int32_t idx = seq.params[i + 2];
                if (idx >= 0 && idx <= 255)
                    *target = ZzColor::Indexed(static_cast<std::uint8_t>(idx));
                i += 2;
            } else if (mode == 2 && i + 4 < seq.params.size()) { // RGB：;2;r;g;b
                *target = ZzColor::Rgb(clampByte(seq.params[i + 2]),
                                       clampByte(seq.params[i + 3]),
                                       clampByte(seq.params[i + 4]));
                i += 4;
            }
            // 参数不足：静默忽略（不消费后续参数）
            break;
        }
        case 39: penFg_ = ZzColor::Default(); break;
        case 49: penBg_ = ZzColor::Default(); break;
        default:
            if (p >= 30 && p <= 37)
                penFg_ = ZzColor::Indexed(static_cast<std::uint8_t>(p - 30));
            else if (p >= 40 && p <= 47)
                penBg_ = ZzColor::Indexed(static_cast<std::uint8_t>(p - 40));
            else if (p >= 90 && p <= 97)
                penFg_ = ZzColor::Indexed(static_cast<std::uint8_t>(p - 90 + 8));
            else if (p >= 100 && p <= 107)
                penBg_ = ZzColor::Indexed(static_cast<std::uint8_t>(p - 100 + 8));
            // 其余未知参数安全忽略
            break;
        }
    }
}
```

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_sgr
```

预期：PASS。

- [ ] **步骤 5：Commit**

```bash
git add src/terminal/Sgr.cpp tests/unit/test_terminal_sgr.cpp
git commit -m "feat(terminal): 实现 SGR 画笔映射

基本属性（0-9、22-29）与颜色（ANSI 16/bright、256 色、RGB
TrueColor、默认前景/背景）；SGR 只改画笔不碰已有 Cell。"
```

---

### 任务 7：端到端场景 + chunk 一致性 + 文档收尾

**文件：**
- 创建：`tests/unit/test_terminal_e2e.cpp`
- 修改：`docs/VT-Xterm-Checklist.md`、`docs/API.md`

- [ ] **步骤 1：编写端到端与一致性测试**

创建 `tests/unit/test_terminal_e2e.cpp`：

```cpp
// ZzTerminal 端到端场景与 chunk 切分一致性测试。
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

/// 屏幕文本快照（非 ASCII 以 '?' 占位；仅用于断言行内容）。
static std::string screenText(const ZzTerminal& term)
{
    std::string out;
    for (int row = 0; row < term.size().rows; ++row) {
        const ZzLine& line = term.renderView().lineAt(row);
        for (int col = 0; col < line.cellCount(); ++col) {
            const ZzCell& cell = line.cellAt(col);
            if (cell.isEmpty())
                out.push_back(' ');
            else if (!cell.isCluster() && cell.codePoint() < 0x80)
                out.push_back(static_cast<char>(cell.codePoint()));
            else
                out.push_back('?');
        }
        out.push_back('\n');
    }
    return out;
}

static void testColoredLs()
{
    // 模拟 ls --color：蓝色加粗目录名 + 普通文件名。
    ZzTerminal term(20, 5, 100);
    feedStr(term, "\x1b[1;34msrc\x1b[0m/  README.md\r\n");
    ZZ_TEST_EXPECT(screenText(term).substr(0, 20) == "src/  README.md     ");
    const ZzCell dir = term.renderView().lineAt(0).cellAt(0);
    ZZ_TEST_EXPECT(dir.attributes().bold());
    ZZ_TEST_EXPECT(dir.foreground() == ZzColor::Indexed(4));
    const ZzCell file = term.renderView().lineAt(0).cellAt(7);
    ZZ_TEST_EXPECT(file.attributes() == ZzCellAttributes{});
    ZZ_TEST_EXPECT(file.foreground().isDefault());
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 0);
}

static void testVimStyleRedraw()
{
    // 模拟全屏程序：清屏 + 光标归位 + 逐行重绘。
    ZzTerminal term(10, 3, 100);
    feedStr(term, "junk\r\njunk\r\njunk");
    feedStr(term, "\x1b[2J\x1b[H");
    feedStr(term, "~\r\n~\r\n~");
    ZZ_TEST_EXPECT(screenText(term) == "~         \n~         \n~         \n");
}

static void testSplitInvariance()
{
    // 混合场景在任意切分点分两段喂入，终态必须与一次性喂入一致。
    const std::string scenarios[] = {
        "hello \x1b[1;31mworld\x1b[0m!\r\nnext \x1b]2;t\x07line",
        "\xe4\xb8\xad\xe6\x96\x87\x1b[2;5Hz", // UTF-8 + CUP
        "abc\x1b[K\x1b[1;3H\x1b[7mQ\x1b[0m",
    };
    for (const std::string& input : scenarios) {
        ZzTerminal reference(20, 5, 100);
        feedStr(reference, input);
        const std::string want = screenText(reference);

        for (std::size_t cut = 0; cut <= input.size(); ++cut) {
            ZzTerminal term(20, 5, 100);
            feedStr(term, input.substr(0, cut));
            feedStr(term, input.substr(cut));
            ZZ_TEST_EXPECT(screenText(term) == want);
            ZZ_TEST_EXPECT(term.cursor() == reference.cursor());
            ZZ_TEST_EXPECT(term.title() == reference.title());
        }
    }
}

int main()
{
    testColoredLs();
    testVimStyleRedraw();
    testSplitInvariance();
    if (g_failures == 0)
        std::puts("test_terminal_e2e: all tests passed");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_e2e
```

预期：PASS。若 `testSplitInvariance` 在特定切分点失败，逐个排查是 Parser 续传问题还是 Terminal 状态问题，修复后重跑（禁止跳过用例）。

- [ ] **步骤 3：Checklist 与 API 文档同步**

`docs/VT-Xterm-Checklist.md` 勾选以下项：

- C0/C1：`IND / NEL / RI / HTS`
- Cursor / CSI：全部 5 行（CUU/CUD/CUF/CUB、CNL/CPL、CHA/VPA、CUP/HVP、Save/Restore Cursor）
- Erase / Insert / Delete / Scroll：除 `ED 0/1/2/3` 改勾并注明"（ED 3 清历史除外）"外全部
- SGR：Reset、Bold/Faint、Italic、Blink/Inverse/Invisible/Strikethrough、ANSI 16/Bright、256 colors、RGB TrueColor、Default FG/BG（不勾 Underline variants、Underline color）
- OSC / DCS：`Window/Icon title`

`docs/API.md` 的 Terminal 章节补充：feed 真实链路说明（parser → utf8 → 语义）、pending-wrap 行为、SGR 画笔模型、OSC 标题支持，保持与 `include/ZzTerm/Terminal.h` 注释一致。

- [ ] **步骤 4：全量验证**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
doxygen Doxyfile   # 零 warning
cmake -S . -B build/shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/shared-check && (cd build/shared-check && ctest)
```

预期：静态/动态构建全部测试 PASS，doxygen exit 0 零 warning。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_terminal_e2e.cpp docs/VT-Xterm-Checklist.md docs/API.md
git commit -m "test(terminal): 端到端场景与 chunk 切分一致性测试

- 彩色 ls / vim 式清屏重绘场景断言屏幕文本+属性+光标
- 混合场景全切分点两段喂入终态一致
- Checklist 勾选 C0/C1、光标 CSI、擦除插删滚动、SGR、OSC 标题
- docs/API.md 同步 Terminal 语义"
```

---

## 自检记录

- **规格覆盖度：** 规格第 1 节纳入项 → C0（任务 3）、CSI 光标（任务 4）、擦除/插删/滚动（任务 5）、SGR（任务 6）、pending-wrap（任务 1+2）、OSC 标题（任务 3）、ESC 7/8/D/E/M/H（任务 3）、DCS 安全忽略（任务 2 Sink 不覆盖 DCS 回调）。规格第 5 节宽度接入点 → 任务 2。DoD → 任务 7。无遗漏。
- **类型一致性：** `paramOr`（任务 4 定义，任务 5 同文件复用）、`eraseFill`/`noteScreenDirty`/`putChar`/`executeControl`/`dispatch*`/`sgr`（任务 2 在 Terminal.h 统一声明）、`zzCellWidthOf`（任务 2 UnicodeWidth.h）、`wrapPending`/`setWrapPending`（任务 1 Screen.h）。任务 3 的 `scrollbackLineCount()` 已标注以 RenderView.h 实际名为准。
- **已知留白（有意）：** ED 3、DEC 私有模式、intermediate CSI、charset、EAW 宽度——均已在对应任务注释中说明归属里程碑。
