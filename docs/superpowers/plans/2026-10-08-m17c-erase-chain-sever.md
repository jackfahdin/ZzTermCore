# M17c 整行擦除斩链（erase severs wrap chain）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** EL/ED 整行擦除时把被擦行从 soft-wrap 折链上摘除（清本行出链 + 前驱入链，前驱在历史区时跨界斩断历史末行链标），消除 M17a Preserve 保护「僵尸折链」导致的提示符残片事故。

**架构：** 斩链语义集中在 `ZzScreen` 内部（`severRowLinks` 帮助函数），EL/ED 三原语在整行覆盖时调用；屏幕首行的跨界斩断经新增 `SeverSeamLinkCallback` 回调上报，`ZzNativeBackend` 接线到 `ZzScrollback` 接口新增的 `severNewestWrapped()` 纯虚。contour 后端不动（第三方冻结），parity 偏离登记。

**技术栈：** C++20、CMake preset（linux-gcc-debug 等）、自研 ZZ_TEST_EXPECT 单测风格。

**规格：** `docs/superpowers/specs/2026-10-08-m17c-erase-chain-sever-design.md`（含自检勘误 d937bbb：ED3 被分发层忽略，不在触发集合）

**关键既有事实（探索已核实，实现前不必重查）：**

- `src/screen/Screen.cpp:372` `eraseInLine`：setCell 循环填充，**从不动 wrapped**；
  `src/screen/Screen.cpp:391` `eraseInDisplay`：整行用 `ZzLine::clear()`（**已自动
  setWrapped(false)**，见 `src/screen/Line.cpp:39-43`），即 ED 的「出链」已斩断，
  缺的是「入链」（前驱行 wrapped）。
- Screen 不知道历史：`scrollOutCallback_`/`historyPullCallback_` 回调分层先例
  （`include/ZzTerm/Screen.h:59/:70`，setter 在 `:402/:408`，成员在 `:451/:452`），
  新回调照搬该模式。
- 接线点：`src/backend/native/ZzNativeBackend.cpp:196-219` 构造函数，
  `scrollback_` 为 `std::unique_ptr<ZzScrollback>`（`zzCreateChunkedScrollback`）。
- `ZzScrollback` 接口（`include/ZzTerm/Scrollback.h`）无任何可变行口；
  M15 曾新增纯虚 `takeNewest` 并在 API.md 登记（先例）。
- `ZzChunkedScrollback`（`src/history/ChunkedScrollback.cpp`）存储为
  `std::deque<std::vector<ZzLine>> chunks_`，最新行恒为 `chunks_.back().back()`
  （`totalLines_ > 0` 时）。
- EL/ED 分发：`src/backend/native/NativeCsiDispatch.cpp:107-118`，只受理
  ED/EL p<=2，直调 `screen_.eraseInDisplay/eraseInLine`——**本计划不改分发层**。
- 测试 GLOB 自动注册（`tests/CMakeLists.txt:6` CONFIGURE_DEPENDS），新增
  测试文件无需改 CMake；重新 configure 即生效。
- M17a 事故测试参考：`tests/unit/test_native_reflow_topfill.cpp` 的
  `testActiveChainGuardVsReadlineErase`（:179）与 `feedStr`/`screenRowText` 帮助函数。
- 基线：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`（57 例）；
  `ctest --test-dir build/m2-off-check`（46）；`ctest --test-dir build/m2-shared-check`（57）；
  fuzz `ctest --preset linux-clang-fuzz -R fuzz`（3）；`doxygen Doxyfile` 零警告。

**纪律：** 测试数据必须手工推演行数账；实测与推演不符时先核账（找出推演错误
或实现 bug），确属计划笔误才在本文末尾登记勘误（格式 `## 勘误 E-N`）。

---

## 任务 1：ZzScrollback::severNewestWrapped 接口与实现

**文件：**
- 修改：`include/ZzTerm/Scrollback.h`（`takeNewest` 声明 :106 后插入新纯虚）
- 修改：`src/history/ChunkedScrollback.cpp`（`takeNewest` 实现 :147 后插入实现）
- 测试：`tests/unit/test_scrollback.cpp`（追加用例，复用其 `makeLine`/宏）

- [ ] **步骤 1：编写失败的测试**

`tests/unit/test_scrollback.cpp` 追加（`main` 里注册，风格对齐既有用例）：

```cpp
// N. severNewestWrapped：斩断最新历史行出链（M17c）
static void testSeverNewestWrapped()
{
    auto sb = zzCreateChunkedScrollback(10);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "l1", false));
    batch.push_back(makeLine(4, "l2", true)); // 末行带链标（接缝粘连场景）
    sb->append(std::move(batch));
    ZZ_TEST_EXPECT(sb->lineAt(1).wrapped());
    sb->severNewestWrapped();
    ZZ_TEST_EXPECT(!sb->lineAt(1).wrapped());     // 链标被斩
    ZZ_TEST_EXPECT(sb->lineCount() == 2);         // 行数/内容不动
    ZZ_TEST_EXPECT(lineText(sb->lineAt(1), 2) == "l2");
    const ZzScrollbackStats st = sb->stats();     // 统计记账不受影响
    ZZ_TEST_EXPECT(st.totalAppended == 2);
    ZZ_TEST_EXPECT(st.totalDropped == 0);
    sb->severNewestWrapped();                     // 幂等：已无链标再斩不崩
    ZZ_TEST_EXPECT(sb->lineCount() == 2);
}

static void testSeverNewestWrappedEmpty()
{
    auto sb = zzCreateChunkedScrollback(10);
    sb->severNewestWrapped(); // 空历史空操作，不崩
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug --target test_scrollback && ./build/linux-gcc-debug/tests/test_scrollback`
预期：编译失败，报错含 `severNewestWrapped` 不是 `ZzScrollback` 的成员。

- [ ] **步骤 3：接口声明**

`include/ZzTerm/Scrollback.h`，`takeNewest` 声明（:106）后插入：

```cpp
    /**
     * @brief 斩断最新历史行的出链（wrapped 置 false，M17c erase 斩链）。
     * @note 空历史为空操作；幂等。语义配对场景：屏幕首行被整行擦除时其
     *       入链（历史末行 wrapped）必须同步死亡，否则已死内容跨代粘连成
     *       僵尸折链（M17a Preserve 会被迫保护整条死链）。
     * @note 就地改旗标：行数/内容/统计记账（totalAppended/totalDropped）
     *       与绝对行号均不受影响（区别于 append/takeNewest）。
     */
    virtual void severNewestWrapped() noexcept = 0;
```

- [ ] **步骤 4：ChunkedScrollback 实现**

`src/history/ChunkedScrollback.cpp`，`takeNewest` 实现（:147）后插入：

```cpp
    void severNewestWrapped() noexcept override
    {
        if (totalLines_ == 0)
            return;
        chunks_.back().back().setWrapped(false);
    }
```

- [ ] **步骤 5：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug --target test_scrollback && ./build/linux-gcc-debug/tests/test_scrollback`
预期：PASS（含既有用例无回归）。

- [ ] **步骤 6：Commit**

```bash
git add include/ZzTerm/Scrollback.h src/history/ChunkedScrollback.cpp tests/unit/test_scrollback.cpp
git commit -m "feat(scrollback): ZzScrollback 新增 severNewestWrapped——erase 斩链跨界原语（M17c 任务 1）"
```

---

## 任务 2：ZzScreen 斩链核心（severRowLinks + EL/ED 触发 + SeverSeamLinkCallback）

**文件：**
- 修改：`include/ZzTerm/Screen.h`（回调类型 :70 后、erase 文档 :301-315、setter :408 后、私有声明与成员 :451 后）
- 修改：`src/screen/Screen.cpp`（`severRowLinks` 实现、`eraseInLine` :372、`eraseInDisplay` :391、setter :525 后）
- 测试：`tests/unit/test_screen_erase_sever.cpp`（新建，GLOB 自动注册）

- [ ] **步骤 1：编写失败的测试**

新建 `tests/unit/test_screen_erase_sever.cpp`：

```cpp
// M17c 整行擦除斩链规则测试（Screen 级，mock 接缝回调）。
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

// 构造 5 行屏，rows 1-2-3 连成折链（r1.wrapped=1, r2.wrapped=1, r3 收尾）。
static ZzScreen makeChainScreen()
{
    ZzScreen scr(10, 5);
    scr.setLineWrapped(1, true);
    scr.setLineWrapped(2, true);
    return scr;
}

// 1. EL \e[K 从列 0（\r\e[K 形态）整行擦除：斩本行出链 + 前驱入链
static void testElFromCol0SeversChain()
{
    ZzScreen scr = makeChainScreen();
    scr.setCursorPosition(ZzPosition{2, 0});
    scr.eraseInLine(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped()); // 本行出链斩
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped()); // 前驱入链斩
}

// 2. EL 行尾部分擦除（起点非列 0）：不斩链（视为编辑）
static void testElPartialKeepsChain()
{
    ZzScreen scr = makeChainScreen();
    scr.setCursorPosition(ZzPosition{2, 5});
    scr.eraseInLine(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(scr.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());
}

// 3. EL \e[2K 整行擦除（任意光标列）：斩链
static void testElAllSeversChain()
{
    ZzScreen scr = makeChainScreen();
    scr.setCursorPosition(ZzPosition{2, 4});
    scr.eraseInLine(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
}

// 4. EL \e[1K 光标在末列=整行覆盖：斩链；光标在中列=部分：不斩
static void testElFromStartBoundary()
{
    ZzScreen full = makeChainScreen();
    full.setCursorPosition(ZzPosition{2, 9}); // 末列（宽 10）
    full.eraseInLine(ZzEraseMode::FromStart, ZzCell{});
    ZZ_TEST_EXPECT(!full.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(!full.lineAt(1).wrapped());

    ZzScreen part = makeChainScreen();
    part.setCursorPosition(ZzPosition{2, 8});
    part.eraseInLine(ZzEraseMode::FromStart, ZzCell{});
    ZZ_TEST_EXPECT(part.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(part.lineAt(1).wrapped());
}

// 5. ED \e[2J 全屏：所有链标清 + 接缝回调触发（Primary）
static void testEdAllSeversAllAndSeam()
{
    ZzScreen scr = makeChainScreen();
    int seamCalls = 0;
    scr.setSeverSeamLinkCallback([&] { ++seamCalls; });
    scr.setLineWrapped(0, true);
    scr.setCursorPosition(ZzPosition{4, 3});
    scr.eraseInDisplay(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(seamCalls == 1); // row 0 入链跨界斩，仅一次
}

// 6. ED \e[J（光标行起向下）：下方整行死亡；光标行出链随之下落斩断，
//    光标行自身（部分擦除）保留入链
static void testEdToEndSeversBelow()
{
    ZzScreen scr = makeChainScreen();          // 链 r1-r2-r3
    scr.setLineWrapped(3, true);               // 延长链到 r4：r1-r2-r3-r4
    scr.setCursorPosition(ZzPosition{3, 4});
    scr.eraseInDisplay(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(3).wrapped());  // r3 出链斩（r4 入链被斩的传导）
    ZZ_TEST_EXPECT(scr.lineAt(2).wrapped());   // r2→r3 链接保留（r3 是部分擦除）
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());
}

// 7. ED \e[J 光标在列 0：光标行算整行覆盖，入链也斩
static void testEdToEndCursorAtCol0()
{
    ZzScreen scr = makeChainScreen(); // 链 r1-r2-r3
    scr.setCursorPosition(ZzPosition{3, 0});
    scr.eraseInDisplay(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(3).wrapped());
    ZZ_TEST_EXPECT(!scr.lineAt(2).wrapped()); // r3 入链斩
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());  // r1→r2 保留
}

// 8. ED \e[1J（向上）：上方整行死亡含 row 0 → 接缝斩；光标行部分保留
static void testEdFromStartSeversAboveAndSeam()
{
    ZzScreen scr = makeChainScreen(); // 链 r1-r2-r3
    int seamCalls = 0;
    scr.setSeverSeamLinkCallback([&] { ++seamCalls; });
    scr.setCursorPosition(ZzPosition{2, 3});
    scr.eraseInDisplay(ZzEraseMode::FromStart, ZzCell{});
    ZZ_TEST_EXPECT(seamCalls == 1);           // row 0 整行被擦 → 跨界斩
    ZZ_TEST_EXPECT(!scr.lineAt(1).wrapped()); // r1 出链斩（自身被整行擦）
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
    ZZ_TEST_EXPECT(scr.lineAt(2).wrapped());  // r2 部分擦除，出链保留
}

// 9. EL 整行擦除 row 0：接缝回调触发
static void testElRow0FiresSeamCallback()
{
    ZzScreen scr(10, 5);
    scr.setLineWrapped(0, true);
    int seamCalls = 0;
    scr.setSeverSeamLinkCallback([&] { ++seamCalls; });
    scr.setCursorPosition(ZzPosition{0, 0});
    scr.eraseInLine(ZzEraseMode::ToEnd, ZzCell{});
    ZZ_TEST_EXPECT(seamCalls == 1);
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
}

// 10. Alternate 缓冲不触发接缝回调（无历史）
static void testAlternateNeverFiresSeam()
{
    ZzScreen scr(10, 5);
    int seamCalls = 0;
    scr.setSeverSeamLinkCallback([&] { ++seamCalls; });
    scr.setActiveBuffer(ZzScreenBuffer::Alternate);
    scr.setCursorPosition(ZzPosition{0, 0});
    scr.eraseInDisplay(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(seamCalls == 0);
}

// 11. 无回调武装时擦 row 0 不崩（Screen 单测无接线环境）
static void testRow0EraseWithoutCallbackNoCrash()
{
    ZzScreen scr(10, 5);
    scr.setLineWrapped(0, true);
    scr.setCursorPosition(ZzPosition{0, 0});
    scr.eraseInLine(ZzEraseMode::All, ZzCell{});
    ZZ_TEST_EXPECT(!scr.lineAt(0).wrapped());
}

// 12. 覆盖写不斩链（规格 §3.3：无擦除直接改写视为编辑）
static void testOverwriteKeepsChain()
{
    ZzScreen scr = makeChainScreen();
    ZzCell c;
    c.setWidth(ZzCellWidth::Narrow);
    c.setCodePoint(U'x');
    scr.putCell(ZzPosition{2, 3}, c);
    ZZ_TEST_EXPECT(scr.lineAt(2).wrapped());
    ZZ_TEST_EXPECT(scr.lineAt(1).wrapped());
}

int main()
{
    testElFromCol0SeversChain();
    testElPartialKeepsChain();
    testElAllSeversChain();
    testElFromStartBoundary();
    testEdAllSeversAllAndSeam();
    testEdToEndSeversBelow();
    testEdToEndCursorAtCol0();
    testEdFromStartSeversAboveAndSeam();
    testElRow0FiresSeamCallback();
    testAlternateNeverFiresSeam();
    testRow0EraseWithoutCallbackNoCrash();
    testOverwriteKeepsChain();
    if (g_failures == 0)
        std::puts("PASS test_screen_erase_sever");
    return g_failures == 0 ? 0 : 1;
}
```

注意：用例 6 的前提是「ED ToEnd 光标行部分擦除时，其出链随下方整行的
入链斩断而传导死亡」——推演：severRowLinks(r4) 置 r3.wrapped=false。
用例 7 推演：severRowLinks(r3)（列 0 起整行覆盖）置 r3.wrapped=false 且
r2.wrapped=false。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_screen_erase_sever`
预期：编译失败，报错含 `setSeverSeamLinkCallback` 不是 `ZzScreen` 的成员。
（GLOB CONFIGURE_DEPENDS：新建测试文件需重新 configure 才纳入构建。）

- [ ] **步骤 3：Screen.h 声明**

其一，`HistoryPullCallback` 类型（:70）后插入：

```cpp
    /// @brief 屏幕首行整行擦除时斩断历史末行链标的通知（M17c）。
    ///        仅 Primary 缓冲触发；Alternate 无历史不触发。由持有方接线到
    ///        ZzScrollback::severNewestWrapped。
    using SeverSeamLinkCallback = std::function<void()>;
```

其二，`eraseInLine` 文档（:301-306）追加语义说明：

```cpp
     * @note M17c 斩链：擦除范围覆盖整行（ToEnd 起点列 0 / All /
     *       FromStart 终点末列）时，本行出链与前驱入链一并斩断——整行
     *       擦除 = 内容死亡 = 从折链摘除。部分擦除视为编辑，不动链标。
```

`eraseInDisplay` 文档（:308-315）追加：

```cpp
     * @note M17c 斩链：被整行覆盖的行按 eraseInLine 同规则斩链；row 0
     *       被整行覆盖时经 SeverSeamLinkCallback 通知斩断历史末行链标。
```

其三，`setHistoryPullCallback`（:408）后插入：

```cpp
    /// @brief 设置接缝斩链回调（M17c）；空回调时跳过跨界斩（不崩）。
    void setSeverSeamLinkCallback(SeverSeamLinkCallback callback);
```

其四，私有区 `historyPullCallback_`（:452）后插入：

```cpp
    SeverSeamLinkCallback severSeamLinkCallback_;

    /// @brief M17c 整行擦除斩链：清 row 出链与前驱入链；row==0 且
    ///        Primary 时经 severSeamLinkCallback_ 跨界斩历史末行。
    void severRowLinks(Buffer& buf, int row) noexcept;
```

（`Buffer` 为私有嵌套类型，声明位置需在使用点之前——放在私有方法区。）

- [ ] **步骤 4：Screen.cpp 实现**

其一，`severRowLinks` 实现（放在 `eraseInLine` 之前）：

```cpp
// M17c：整行擦除斩链——内容死亡即从折链摘除：清本行出链，并斩断入链
//（前驱行 wrapped）。前驱在历史区（row==0）时经 SeverSeamLinkCallback
// 通知持有方斩断历史末行链标（仅 Primary；Alternate 无历史不触发）。
void ZzScreen::severRowLinks(Buffer& buf, int row) noexcept
{
    buf.lines[static_cast<std::size_t>(row)].setWrapped(false);
    if (row > 0) {
        buf.lines[static_cast<std::size_t>(row - 1)].setWrapped(false);
    } else if (&buf == &primary_ && severSeamLinkCallback_) {
        severSeamLinkCallback_();
    }
}
```

其二，`eraseInLine`（:372-389）末尾（`markDirty` 之后）追加：

```cpp
    // M17c：整行覆盖才斩链；行尾/行首部分擦除视为编辑，不动链。
    if (from == 0 && to == cols_ - 1)
        severRowLinks(buf, cur.row);
```

其三，`eraseInDisplay`（:391-422）各分支末尾（`break` 前）追加：

```cpp
    case ZzEraseMode::ToEnd:
        // …既有清格与 markRowDirty…
        // M17c：下方整行死亡；光标行仅当列 0 起才算整行覆盖。
        for (int r = (cur.col == 0 ? cur.row : cur.row + 1); r < rows_; ++r)
            severRowLinks(buf, r);
        break;
    case ZzEraseMode::FromStart:
        // …既有清格与 markRowDirty…
        // M17c：上方整行死亡；光标行仅当末列才算整行覆盖。
        for (int r = 0; r < cur.row; ++r)
            severRowLinks(buf, r);
        if (cur.col == cols_ - 1)
            severRowLinks(buf, cur.row);
        break;
    case ZzEraseMode::All:
        // …既有清格与 markAllDirty…
        for (int r = 0; r < rows_; ++r)
            severRowLinks(buf, r);
        break;
```

（ToEnd/FromStart 分支中被 `ZzLine::clear()` 清过的行出链已自动置
false，`severRowLinks` 重复置位无害；关键增量是入链斩断。）

其四，setter 实现（`setHistoryPullCallback` :522-525 后）：

```cpp
void ZzScreen::setSeverSeamLinkCallback(SeverSeamLinkCallback callback)
{
    severSeamLinkCallback_ = std::move(callback);
}
```

- [ ] **步骤 5：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug --target test_screen_erase_sever && ./build/linux-gcc-debug/tests/test_screen_erase_sever`
预期：PASS（11 用例）。随后全量回归：
运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：57+1 例全绿（既有用例无回归——特别注意 test_terminal_csi /
test_screen_reflow 中擦除相关用例的 wrapped 断言若与新语义冲突，先核账：
冲突属「旧断言钉住了未斩链的旧行为」则按新语义修正该断言并在 commit
信息中说明，属实现错误则修实现）。

- [ ] **步骤 6：Commit**

```bash
git add include/ZzTerm/Screen.h src/screen/Screen.cpp tests/unit/test_screen_erase_sever.cpp
git commit -m "feat(screen): 整行擦除斩链——EL/ED 触发与 SeverSeamLinkCallback（M17c 任务 2）"
```

---

## 任务 3：backend 接线 + Terminal 事故复刻测试 + compat 偏离登记

**文件：**
- 修改：`src/backend/native/ZzNativeBackend.cpp:213-218`（构造函数接线）
- 测试：`tests/unit/test_native_reflow_topfill.cpp`（追加事故复刻用例）
- 测试：`tests/unit/test_backend_compat.cpp`（追加 parity 偏离登记用例，仿 M17a 既有偏离用例模式）

- [ ] **步骤 1：编写失败的事故复刻测试**

`tests/unit/test_native_reflow_topfill.cpp` 追加（复用 `feedStr`/
`screenRowText` 帮助函数与宏，`main` 注册）。场景行数账已手工推演：

```cpp
// M17c 事故复刻：readline 两代重绘死链不粘——erase 斩链后残片收链、
// 活代被 bash 帧擦除精确命中，内容零损失。
// 行数账（20x6）：
//   A) "C0\r\nC1\r\nC2\r\n" + 45 字符提示符 → r0-2=C0..C2，
//      链 [r3(w),r4(w),r5]（20+20+5），光标 (5,5)；
//   B) bash 二代重绘：\r\e[K\r + 同 45 字符 → \e[K 斩 r4.wrapped（M17c），
//      写入折行触发 2 次滚动（C0、C1 入历史），屏：
//      r0=C2, r1=gen1r0(w), r2=gen1r1(斩尾), r3=gen2r0(w), r4=gen2r1(w),
//      r5=gen2r2，光标 (5,5)；历史 [C0,C1]；
//   C) resize(40,6)：gen1 [r1,r2] 收链为 40 宽 1 行；gen2 是光标链，
//      M17a 豁免保持 20 宽 3 行；产出 5 行缺 1 → 顶补 C1，历史 [C0]；
//   D) bash 拉回重绘：\r\e[K + \e[A\e[K×2（帧=3 行，精确命中 gen2 三行）
//      + 45 字符按 40 宽重印 → r3(40 字符) + r4("PROMP")，光标 (5,4)。
//   无 M17c 时：B 不斩链 → gen1/gen2 粘成 5 行僵尸链 → C 整链豁免 →
//   D 只擦 3 行 → r1/r2 残留 20 列碎片（本测试的否定断言点）。
static void testEraseSeverVsZombieChain()
{
    ZzTerminal term(20, 6, ZzBackendKind::Native, 100);
    const std::string prompt =
        "0123456789abcdefghij0123456789abcdefghijPROMP"; // 45 字符
    feedStr(term, "C0\r\nC1\r\nC2\r\n");
    feedStr(term, prompt);                       // 链 r3-r5，光标 (5,5)
    feedStr(term, "\r\x1b[K\r");                 // bash 二代重绘：整行擦 r5（斩链点）
    feedStr(term, prompt);                       // 重写 → 2 次滚动，C0/C1 入历史
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 2);
    ZZ_TEST_EXPECT(term.resize(40, 6));          // 拉回（M17a 豁免活代）
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1); // 顶补取走 C1
    feedStr(term, "\r\x1b[K\x1b[A\x1b[K\x1b[A\x1b[K"); // bash 帧擦除 3 行
    feedStr(term, prompt);                       // 40 宽重印
    // 内容零损失 + 无 20 列碎片：r2 是收链后的 40 列残骸（方案 A 形态）
    ZZ_TEST_EXPECT(screenRowText(term, 0) == "C1");
    ZZ_TEST_EXPECT(screenRowText(term, 1) == "C2");
    ZZ_TEST_EXPECT(screenRowText(term, 2) ==
                   "0123456789abcdefghij0123456789abcdefghij"); // gen1 收链残骸
    ZZ_TEST_EXPECT(screenRowText(term, 3) ==
                   "0123456789abcdefghij0123456789abcdefghij"); // 活提示符
    ZZ_TEST_EXPECT(screenRowText(term, 4) == "PROMP");
    ZZ_TEST_EXPECT(screenRowText(term, 5).empty());
    ZZ_TEST_EXPECT(term.cursor().position.row == 4);
    ZZ_TEST_EXPECT(term.cursor().position.col == 5);
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 1); // C0 完好
}

// M17c 跨界斩链接线实测（Terminal 级：Screen 回调 → backend →
// scrollback_->severNewestWrapped 全链路）。
// 行数账（20x6）：feed "L0\r\n"×6 → 历史 [L0]，光标 (0,5)；
// 再连续写 130 个 'a'（无 \r\n）：每 20 字符折行触发 1 次滚屏，共 6 次——
// L1..L5 依次入历史，第 6 次滚出的是折行首段 seg0（wrapped=true），
// 历史 [L0..L5, seg0(w)] 共 7 行，seg0 续接屏幕 r0（seg1）构成接缝链；
// 屏幕 r0-r4=seg1..seg5(w)，r5=seg6（10 字符），光标 (10,5)。
// \e[H 光标回 row 0 → \e[K 整行擦除 r0 → 跨界斩：历史末行 seg0 链标死。
static void testEraseSeverAcrossSeam()
{
    ZzTerminal term(20, 6, ZzBackendKind::Native, 100);
    feedStr(term, "L0\r\nL1\r\nL2\r\nL3\r\nL4\r\nL5\r\n");
    feedStr(term, std::string(130, 'a'));
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 7);
    ZZ_TEST_EXPECT(term.historyView().lineAt(6).wrapped()); // 接缝链前提成立
    const auto g0 = term.historyView().generation();
    feedStr(term, "\x1b[H");   // CUP：光标到 (0,0)
    feedStr(term, "\x1b[K");   // EL 列 0 整行擦除 r0 → 跨界斩链
    ZZ_TEST_EXPECT(!term.renderView().lineAt(0).wrapped());  // r0 出链斩
    ZZ_TEST_EXPECT(!term.historyView().lineAt(6).wrapped()); // 历史末行入链斩
    ZZ_TEST_EXPECT(term.historyView().lineCount() == 7);     // 行数不动
    ZZ_TEST_EXPECT(term.historyView().generation() > g0);    // 旗标变化计代
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug --target test_native_reflow_topfill && ./build/linux-gcc-debug/tests/test_native_reflow_topfill`
预期：FAIL——`testEraseSeverVsZombieChain`：无斩链时 r1/r2 为 20 列碎片
（`0123456789abcdefghij` 形态）、r2 不等于 40 列收链残骸；
`testEraseSeverAcrossSeam`：无跨界斩链时历史末行 wrapped 保持 true。
若失败形态与推演不符，先核行数账再判实现/计划。

- [ ] **步骤 3：ZzNativeBackend 接线**

`src/backend/native/ZzNativeBackend.cpp` 构造函数，`setHistoryPullCallback`
（:213-218）后追加：

```cpp
    // M17c：屏幕首行整行擦除时斩断历史末行链标（erase 斩链跨界段）；
    // 旗标变化影响 HistoryView 可见内容，代计数递增（M14「不得漏增」同口径）。
    screen_.setSeverSeamLinkCallback([this] {
        if (scrollback_->lineCount() > 0) {
            scrollback_->severNewestWrapped();
            ++historyGeneration_;
        }
    });
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug --target test_native_reflow_topfill && ./build/linux-gcc-debug/tests/test_native_reflow_topfill`
预期：PASS（含 M17a 既有用例无回归）。

- [ ] **步骤 5：compat 偏离登记**

`tests/unit/test_backend_compat.cpp` 仿 M17a 偏离用例模式追加：双后端
20x6 构造同一折链（两行链 r0(w)-r1），光标到 r1 列 0 喂 `\r\x1b[K`——
**native 断言**：r0.wrapped()==false（斩链）；**contour 断言**：
r0.wrapped()==true（不斩，登记偏离，第三方冻结不改）。
预期：编写即通过（行为已在任务 2 落地，本用例是登记钉住）；若 contour
侧断言失败说明 contour 行为认知有误，核账后修正断言注释。

- [ ] **步骤 6：全量回归 + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿。
运行：`ctest --test-dir build/m2-off-check && ctest --test-dir build/m2-shared-check`
预期：全绿。

```bash
git add src/backend/native/ZzNativeBackend.cpp tests/unit/test_native_reflow_topfill.cpp tests/unit/test_backend_compat.cpp
git commit -m "feat(backend): erase 斩链接线与事故复刻——僵尸折链不再粘连（M17c 任务 3）"
```

---

## 任务 4：文档同步 + 全基线回归

**文件：**
- 修改：`docs/Scrollback-and-Reflow.md`（斩链小节 + 已知外部问题小节）
- 修改：`docs/API.md`（版本/ABI 节 M17c 条目 + parity 偏离）
- 修改：`docs/Architecture.md`（parity 章节登记，若有该章）

- [ ] **步骤 1：Scrollback-and-Reflow.md**

reflow 章节（M17a「光标活动链保护」小节附近）追加「整行擦除斩链
（M17c）」小节，要点：

- 语义：整行擦除 = 内容死亡 = 从折链摘除（清本行出链 + 前驱入链；
  前驱在历史区经 SeverSeamLinkCallback → ZzScrollback::severNewestWrapped
  跨界斩断）；
- 触发集合：EL 整行覆盖（`\r\e[K`、`\e[2K`、`\e[1K`@末列）、ED 0/1/2
  的整行覆盖行；不斩：部分擦除、覆盖写、ECH/DL/IL（首版豁免）；
  ED 3 分发层忽略不达 Screen；
- 动机：M17a Preserve 被僵尸折链（readline 多代重绘尸体粘连）污染的
  事故（spike 留痕实证）；
- 效果：Preserve 只保护活代 → bash 帧擦除精确命中；死代残骸正常收链
  成短行留在滚动区（方案 A 形态）。

同文追加「已知外部问题」小节（readline 8.3 光标错位），要点照规格 §5：
触发条件（变宽+提示符折行+2 段以上隐形字符）、错位量=隐形段字节数、
上游状态（bug-readline 2026-08-10 报告，devel 已修 commit 1e9f5e10b2，
≤8.3-p003 未带）、自愈方式（回车换新提示符；C-l 无效）、终端侧结论
（无忠实修复手段，Core/spike 不动）。

- [ ] **步骤 2：API.md**

`grep -n "M17a" docs/API.md` 定位版本节，追加 M17c 条目：

- `ZzScrollback` 新增纯虚 `severNewestWrapped()`（接口标注 Core 内部使
  用但属公共头；实现类仅仓内 ChunkedScrollback 一个——M15 takeNewest
  同先例登记）；
- `ZzScreen` 新增 `SeverSeamLinkCallback`/`setSeverSeamLinkCallback`；
- 行为语义变化：EL/ED 整行擦除斩断 wrapped 链标（reflow/选区拼链随之
  按硬行处理被擦行）；contour parity 偏离登记（erase 触及 wrapped 行
  场景）。

- [ ] **步骤 3：Architecture.md parity 登记**

`grep -n "parity\|偏离" docs/Architecture.md` 定位 parity 章节，在 M17a
偏离条目后追加 M17c 斩链偏离一行。若无该章节则跳过（登记在 API.md 即可）。

- [ ] **步骤 4：全基线回归 + doxygen**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`（全绿）
运行：`ctest --test-dir build/m2-off-check && ctest --test-dir build/m2-shared-check`（全绿）
运行：`ctest --preset linux-clang-fuzz -R fuzz`（3/3）
运行：`doxygen Doxyfile 2>&1 | grep -i warning`（零输出；注意行内 code
span 禁尖括号/裸反斜杠字母/游离 @词，转义序列用「ESC [ A」写法或放
fenced code block）

- [ ] **步骤 5：Commit**

```bash
git add docs/Scrollback-and-Reflow.md docs/API.md docs/Architecture.md
git commit -m "docs(m17c): 斩链语义与已知外部问题（readline 8.3 光标错位）文档同步"
```

---

## 任务 5：收尾验证与发布

- [ ] **步骤 1：留痕重放人工验证（不入库）**

重建重放器并对同一份留痕重放：

```bash
cd /tmp/zz-resize-repro && ZB=/home/zz/Jackfahdin/github/ZzTermCore/build/linux-gcc-debug && \
g++ -std=c++20 -g replay.cpp -I/home/zz/Jackfahdin/github/ZzTermCore/include \
  -L$ZB -L$ZB/src/backend/contour -L$ZB/contour/vtbackend -L$ZB/contour/vtparser \
  -L$ZB/contour/vtpty -L$ZB/contour/crispy -L$ZB/_deps/libunicode-build/src/libunicode \
  -lZzTermCore -lZzTermContourBackend -lvtbackend -lvtparser -lvtpty -lcrispy-core \
  -lunicode -lunicode_ucd -o replay
stdbuf -oL ./replay /tmp/spike-trace.bin verbose
```

预期：native 终态不再出现 14 列提示符碎片行（` ~/tiplus7100/`、
`rk3399_8.1_pud` 形态）——死代收链为宽残骸短行，listing 内容完好，
光标位置与字节流一致（(49,70)，readline 8.3 bug 所致，见规格 §5）；
contour 终态不变（偏离登记）。若仍有碎片，先核斩链触发是否遗漏
（核账：留痕里 bash 用了哪些擦除序列）。

- [ ] **步骤 2：tag 与推送**

```bash
git tag m17c
git push origin contour --tags
```

- [ ] **步骤 3：CI 确认**

运行：`gh run list --branch contour --limit 7`
预期：7 个 workflow 全绿（等末次提交触发的 run 全部完成再确认）。

- [ ] **步骤 4：spike 重建交付用户复验**

```bash
cmake --build /home/zz/Jackfahdin/github/ZzClawTerm/build/spike-debug
```

告知用户复验路径：`cd /home/zz/Jackfahdin/github/ZzClawTerm &&
./build/spike-debug/zzcore_spike --local`，重点手势：长路径目录下
`ll` → 拖至极窄窗（十几列×几行）→ 拉回最大化，预期：提示符区域无
残片（死代残骸至多 1-3 条收链短行留在滚动区上方）、内容完整。光标停
在提示符中间属 readline 8.3 已知 bug（回车自愈），不在本次复验范围。

## 勘误

- **E-1**（任务 2 实施中发现）：任务 2 步骤 5 预期写「11 用例」，实际测试
  文件含 12 用例（计划自检后补的「覆盖写不斩链」用例计入），属计划笔误，
  实现按 12 用例落地。
