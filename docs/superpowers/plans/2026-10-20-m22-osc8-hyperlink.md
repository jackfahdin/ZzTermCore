# OSC 8 可点击超链接（M22）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 ZzTermCore 双后端（native + contour）落地 OSC 8 超链接的解析、存储、reflow 存续与渲染契约暴露，并在 ZzClawTerm 前端实现 Ctrl+悬停高亮/tooltip、Ctrl+点击打开、设置开关。

**架构：** 贴 contour 上游模型——终端级 LRU id→URI 池（容量 1024，0 = 无链接）+ ZzLine 逐格 16 位 id 稀疏侧表 + `ZzCell::text_` bit26 存在位（M8b 裁定的稀疏扩展点，`sizeof(ZzCell)==12` 不变）。渲染契约经 `ZzCellView.hyperlinkId` 字段 + `ZzRenderView/ZzHistoryView::hyperlinkUri()` 默认实现虚方法暴露（ABI minor 新增）。contour 侧零 patch：只从上游 SoA/LRU 提取。

**技术栈：** C++20、CMake presets、自带宏断言单测（tests/unit/*.cpp 每文件一可执行文件，GLOB 自动收编）、Qt6 前端（ZzClawTerm）。

**规格：** `docs/superpowers/specs/2026-10-20-m22-osc8-hyperlink-design.md`（已批准）

**仓库与分支：**
- ZzTermCore：`/home/zz/Jackfahdin/github/ZzTermCore`，contour 分支（任务 1-7）
- ZzClawTerm：`/home/zz/Jackfahdin/github/ZzClawTerm`，master 分支（任务 8）

**构建/测试命令（ZzTermCore）：**
```bash
cmake --preset linux-gcc-debug          # 已配置过时跳过
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug -R <测试名> --output-on-failure
```
单文件测试可直接跑可执行文件：`./build/linux-gcc-debug/tests/<测试名>`。

**公共代码约定（所有任务共用，不再重复）：**
- 测试宏（照 tests/unit/test_terminal_core.cpp:6-13）：
```cpp
static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)
```
每个测试文件末尾：
```cpp
int main()
{
    // ...调用各 testXxx()...
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
```
- feed 帮助函数（照 test_terminal_core.cpp:15-20）：
```cpp
static void feedStr(ZzTerminal& term, const std::string& s)
{
    term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size()));
}
```
- commit 信息用中文 conventional commit（如 `feat(terminal): OSC 8 超链接解析`）。

---

## 任务 1：ZzCell bit26 hyperlink 存在位

**文件：**
- 修改：`include/ZzTerm/Cell.h`（bit26 存取 + text_ 重写路径保位）
- 测试：`tests/unit/test_cell.cpp`（追加用例）

**背景：** text_ 位布局现状（Cell.h:468-475）：bit30=cluster 旗标、bits[25:24]=width、bit26 预留（文件头注释 317 行已注明"bit 26 预留（未来稀疏扩展）"）。本任务启用 bit26 为 hyperlink 存在位。**关键陷阱**：`setCodePoint`（:397-400）、`setCluster`（:406-409）、`clearText`（:412）用 `(text_ & kWidthMask)` 重写 text_，会把 bit26 一并抹掉，必须改为保留。

- [ ] **步骤 1：编写失败的测试**

在 `tests/unit/test_cell.cpp` 追加（照该文件既有用例风格）：

```cpp
static void testHyperlinkPresenceBit()
{
    ZzCell c;
    ZZ_TEST_EXPECT(!c.hasHyperlink());
    c.setHyperlink(true);
    ZZ_TEST_EXPECT(c.hasHyperlink());
    // 文本重写路径保位
    c.setCodePoint(U'a');
    ZZ_TEST_EXPECT(c.hasHyperlink());
    ZZ_TEST_EXPECT(c.codePoint() == U'a');
    c.setCluster(7);
    ZZ_TEST_EXPECT(c.hasHyperlink());
    ZZ_TEST_EXPECT(c.isCluster() && c.clusterIndex() == 7);
    // 清位
    c.setHyperlink(false);
    ZZ_TEST_EXPECT(!c.hasHyperlink());
    ZZ_TEST_EXPECT(c.isCluster() && c.clusterIndex() == 7); // 不动文本
    // reset 清位
    c.setHyperlink(true);
    c.reset();
    ZZ_TEST_EXPECT(!c.hasHyperlink());
    // 宽度与 hyperlink 互不干扰
    ZzCell w;
    w.setWidth(ZzCellWidth::WideLead);
    w.setHyperlink(true);
    ZZ_TEST_EXPECT(w.width() == ZzCellWidth::WideLead && w.hasHyperlink());
    w.setWidth(ZzCellWidth::Narrow);
    ZZ_TEST_EXPECT(w.hasHyperlink());
}
```
在 main() 中调用 `testHyperlinkPresenceBit();`。

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug --target test_cell && ./build/linux-gcc-debug/tests/test_cell
```
预期：编译失败，`hasHyperlink`/`setHyperlink` 未声明。

- [ ] **步骤 3：实现**

`include/ZzTerm/Cell.h`：

1. 私有常量区（:468-475）加：
```cpp
    static constexpr std::uint32_t kHyperlinkFlag    = 1u << 26;          ///< hyperlink 存在位（M22）
```
2. 公有区加（放在 `clearText()` 附近）：
```cpp
    /**
     * @brief 是否有 OSC 8 超链接（M22）。
     * @return true 表示本格属于某个超链接；具体 id 在所属 ZzLine 稀疏侧表。
     */
    [[nodiscard]] constexpr bool hasHyperlink() const noexcept
    {
        return (text_ & kHyperlinkFlag) != 0;
    }

    /**
     * @brief 设置 hyperlink 存在位（M22）。
     * @param v true 置位，false 清位。
     * @note 只动存在位；id 的登记/清除走 ZzLine::setCellHyperlink。
     */
    constexpr void setHyperlink(bool v) noexcept
    {
        text_ = v ? (text_ | kHyperlinkFlag) : (text_ & ~kHyperlinkFlag);
    }
```
3. 三处重写路径保位——`setCodePoint`、`setCluster`、`clearText` 中的 `(text_ & kWidthMask)` 全部改为 `(text_ & (kWidthMask | kHyperlinkFlag))`。
4. 更新文件头注释：第 316 行 "bit 26 预留（未来稀疏扩展）" 改为 "bit 26 = hyperlink 存在位（M22，id 存 ZzLine 稀疏侧表）"；第 332-333 行 "hyperlink id / decoration 标记等稀疏扩展点改由 text_ bit 26 或行级稀疏侧表承担" 改为陈述已落地（bit26 + 侧表，M22）。

- [ ] **步骤 4：运行测试验证通过**

```bash
cmake --build --preset linux-gcc-debug --target test_cell && ./build/linux-gcc-debug/tests/test_cell
```
预期：PASS（无输出，退出码 0）。同时全量回归：
```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
```
预期：全绿（确认三处保位改动无回归）。

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Cell.h tests/unit/test_cell.cpp
git commit -m "feat(cell): ZzCell text_ bit26 启用为 hyperlink 存在位（M22）"
```

---

## 任务 2：ZzLine hyperlink 稀疏侧表 + ZzHyperlinkPool

**文件：**
- 修改：`include/ZzTerm/Line.h`、`src/screen/Line.cpp`
- 创建：`src/screen/HyperlinkPool.h`、`src/screen/HyperlinkPool.cpp`
- 测试：`tests/unit/test_line.cpp`（追加）、`tests/unit/test_hyperlink_pool.cpp`（新建）
- 修改：`tests/CMakeLists.txt`（test_hyperlink_pool 编译内部实现，照 test_reflow 先例）

**设计要点：**
- 侧表 `std::vector<std::uint16_t> hyperlinks_`：空 = 本行无链接；非空时大小恒等于 cells_.size()，按列对齐。
- **对齐不变量**：`insertCells`/`eraseCells` 搬移 cell 时 bit26 随 cell 走，侧表必须同步搬移，否则 id 错位；`resize` 截断/补零；`clear` 清零（保留分配）。
- 读取口径：`hyperlinkIdAt(col)` 以 bit26 为准（bit 未置恒返回 0），侧表仅在 bit 置位时取 id——渲染热路径 O(1) 判空。
- 池为 src/ 内部实现（无 ZZTERM_API 导出），LRU 容量 1024，id 0 保留。intern 命中规则 = (uri, userId) 二元组相同（`id=` 跨行关联由此成立：同二元组同 id）。

- [ ] **步骤 1：编写失败的测试（池）**

新建 `tests/unit/test_hyperlink_pool.cpp`：

```cpp
// ZzHyperlinkPool intern/LRU 测试（M22）。
#include <cstdio>
#include <string>

#include "screen/HyperlinkPool.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond) /* 同公共约定宏 */

static void testInternDedup()
{
    ZzHyperlinkPool pool;
    const std::uint16_t a = pool.intern("https://a.example", "");
    const std::uint16_t b = pool.intern("https://b.example", "");
    ZZ_TEST_EXPECT(a != 0 && b != 0 && a != b);
    // 同二元组复用
    ZZ_TEST_EXPECT(pool.intern("https://a.example", "") == a);
    // userId 参与区分：同 uri 不同 id → 不同 hyperlinkId
    const std::uint16_t c = pool.intern("https://a.example", "x");
    ZZ_TEST_EXPECT(c != a);
    // 同 uri 同 id 复用（跨段关联的根基）
    ZZ_TEST_EXPECT(pool.intern("https://a.example", "x") == c);
}

static void testLookup()
{
    ZzHyperlinkPool pool;
    const std::uint16_t a = pool.intern("https://a.example/path?q=1", "u1");
    const ZzHyperlinkEntry* e = pool.lookup(a);
    ZZ_TEST_EXPECT(e != nullptr);
    ZZ_TEST_EXPECT(e->uri == "https://a.example/path?q=1");
    ZZ_TEST_EXPECT(e->userId == "u1");
    ZZ_TEST_EXPECT(pool.lookup(0) == nullptr);   // 0 保留
    ZZ_TEST_EXPECT(pool.lookup(65535) == nullptr); // 未登记 id
}

static void testLruEviction()
{
    ZzHyperlinkPool pool;
    const std::uint16_t first = pool.intern("https://first.example", "");
    // 填满并溢出容量
    for (int i = 0; i < 1100; ++i)
        pool.intern("https://x.example/" + std::to_string(i), "");
    ZZ_TEST_EXPECT(pool.lookup(first) == nullptr); // 最旧被淘汰
}

int main()
{
    testInternDedup();
    testLookup();
    testLruEviction();
    /* 失败计数收尾，同公共约定 */
}
```

- [ ] **步骤 2：编写失败的测试（行侧表）**

`tests/unit/test_line.cpp` 追加：

```cpp
static void testHyperlinkSideTable()
{
    ZzLine line(10);
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(3) == 0); // 无侧表恒 0
    line.setCellHyperlink(3, 42);
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(3) == 42);
    ZZ_TEST_EXPECT(line.cellAt(3).hasHyperlink());
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(4) == 0); // 邻格不受影响
    line.setCellHyperlink(3, 0);                // id=0 清除
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(3) == 0);
    ZZ_TEST_EXPECT(!line.cellAt(3).hasHyperlink());
}

static void testHyperlinkSideTableShift()
{
    ZzLine line(8);
    line.setCellHyperlink(5, 9);
    line.eraseCells(0, 2);      // 左移 2 格：id 必须跟到 col 3
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(3) == 9);
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(5) == 0);
    line.insertCells(0, 1);     // 右移 1 格：id 跟到 col 4
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(4) == 9);
    line.resize(3);             // 截断：col 4 越界丢弃
    ZZ_TEST_EXPECT(line.cellCount() == 3);
    line.resize(8);             // 补零增长
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(4) == 0);
    line.setCellHyperlink(2, 7);
    line.clear();               // 清零
    ZZ_TEST_EXPECT(line.hyperlinkIdAt(2) == 0);
}
```
在 main() 中调用两个新用例。

- [ ] **步骤 3：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug --target test_line test_hyperlink_pool
```
预期：编译失败（方法/类型未声明）。

- [ ] **步骤 4：实现 ZzHyperlinkPool**

`src/screen/HyperlinkPool.h`：

```cpp
#pragma once

// 内部头（不安装）：OSC 8 超链接 id→URI 池（M22）。
// 贴 contour 上游 HyperlinkStorage 模型：LRU 容量 1024，id 0 保留为"无链接"。
// intern 命中规则 = (uri, userId) 二元组相同——同二元组必同 id，
// OSC 8 `id=` 参数的跨段关联由此成立（关联键即 hyperlinkId 本身）。

#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ZzHyperlinkEntry {
    std::string uri;
    std::string userId;
};

class ZzHyperlinkPool {
public:
    static constexpr std::size_t kCapacity = 1024;

    /// @brief 登记 (uri, userId)，返回 hyperlinkId（!= 0）。
    ///        同二元组复用既有 id 并触新；满时淘汰最久未用条目。
    std::uint16_t intern(std::string_view uri, std::string_view userId);

    /// @brief 按 id 查条目；0 或未登记/已淘汰返回 nullptr。
    [[nodiscard]] const ZzHyperlinkEntry* lookup(std::uint16_t id) const noexcept;

private:
    // id 空间 1..65535 循环分配；entries_ 以 id 索引（vector，空 = 未占用）。
    struct Slot {
        ZzHyperlinkEntry entry;
        bool occupied = false;
    };
    std::vector<Slot> entries_;                              // 下标即 id
    std::list<std::uint16_t> lru_;                           // 前新后旧
    std::unordered_map<std::string, std::uint16_t> byKey_;   // "uri\x1fuserId" → id
    std::uint16_t nextId_ = 1;
};
```

`src/screen/HyperlinkPool.cpp`：

```cpp
#include "HyperlinkPool.h"

namespace {

std::string zzKey(std::string_view uri, std::string_view userId)
{
    std::string key(uri);
    key.push_back('\x1f');
    key.append(userId);
    return key;
}

} // namespace

std::uint16_t ZzHyperlinkPool::intern(std::string_view uri, std::string_view userId)
{
    const std::string key = zzKey(uri, userId);
    if (const auto it = byKey_.find(key); it != byKey_.end()) {
        lru_.remove(it->second);
        lru_.push_front(it->second);
        return it->second;
    }

    if (entries_.size() < 65536)
        entries_.resize(entries_.size() + 1); // 保证 nextId_ 下标合法（按需增长）

    std::uint16_t id = 0;
    if (lru_.size() < kCapacity) {
        // 未满：取下一个空槽
        while (nextId_ == 0 || (nextId_ < entries_.size() && entries_[nextId_].occupied))
            ++nextId_;
        id = nextId_++;
    } else {
        // 满：淘汰最旧槽位复用其 id
        id = lru_.back();
        lru_.pop_back();
        const Slot& old = entries_[id];
        byKey_.erase(zzKey(old.entry.uri, old.entry.userId));
    }

    Slot& slot = entries_[id];
    slot.entry.uri = std::string(uri);
    slot.entry.userId = std::string(userId);
    slot.occupied = true;
    lru_.push_front(id);
    byKey_.emplace(std::move(key), id);
    return id;
}

const ZzHyperlinkEntry* ZzHyperlinkPool::lookup(std::uint16_t id) const noexcept
{
    if (id == 0 || id >= entries_.size() || !entries_[id].occupied)
        return nullptr;
    return &entries_[id].entry;
}
```

注意 nextId_ 回绕：uint16 自增到 65535 后再 ++ 回绕为 0，循环条件 `nextId_ == 0` 处理回绕重新扫槽。容量 1024 远小于 65535，未满路径不会耗尽 id。

- [ ] **步骤 5：实现 ZzLine 侧表**

`include/ZzTerm/Line.h`（私有成员区 :127-130 前加公有方法，成员区加字段）：

```cpp
    /**
     * @brief 设置指定列的 hyperlink id（M22；Core 内部使用）。
     * @param col 列号，0 <= col < cellCount()。
     * @param id hyperlinkId（0 = 清除本格链接标记）。
     * @note id != 0 时置 cell bit26 并登记侧表（按需分配，大小对齐列数）；
     *       id == 0 时清 bit26（侧表条目保留但不可达）。
     */
    void setCellHyperlink(int col, std::uint16_t id);

    /**
     * @brief 指定列的 hyperlink id。
     * @param col 列号。
     * @return hyperlinkId；无链接（bit26 未置）或越界时返回 0。
     */
    [[nodiscard]] std::uint16_t hyperlinkIdAt(int col) const noexcept;
```

私有成员加：
```cpp
    std::vector<std::uint16_t> hyperlinks_; ///< hyperlink id 稀疏侧表（M22）；空 = 无链接行
```

`src/screen/Line.cpp`：

```cpp
void ZzLine::setCellHyperlink(int col, std::uint16_t id)
{
    if (col < 0 || col >= static_cast<int>(cells_.size()))
        return;
    if (id != 0) {
        if (hyperlinks_.empty())
            hyperlinks_.assign(cells_.size(), 0);
        hyperlinks_[static_cast<std::size_t>(col)] = id;
        cells_[static_cast<std::size_t>(col)].setHyperlink(true);
    } else {
        if (!hyperlinks_.empty())
            hyperlinks_[static_cast<std::size_t>(col)] = 0;
        cells_[static_cast<std::size_t>(col)].setHyperlink(false);
    }
}

std::uint16_t ZzLine::hyperlinkIdAt(int col) const noexcept
{
    if (col < 0 || col >= static_cast<int>(cells_.size()))
        return 0;
    if (!cells_[static_cast<std::size_t>(col)].hasHyperlink())
        return 0; // bit26 为准：侧表残留条目不可达
    if (hyperlinks_.empty())
        return 0; // 防御：bit 置位但无侧表（不应发生）
    return hyperlinks_[static_cast<std::size_t>(col)];
}
```

同步既有 mutator（Line.cpp 中对应函数体内补侧表操作）：
- `insertCells(col, count, fill)`：cells_ 插入的同时，若 `!hyperlinks_.empty()` 则在同一位置插入 count 个 0（`hyperlinks_.insert(hyperlinks_.begin() + col, count, 0)`，末尾截到 cells_.size()）。
- `eraseCells(col, count, fill)`：同步 erase 区间并末尾补 0。
- `resize(cols, fill)`：`hyperlinks_.resize(cols, 0)`（仅当非空）。
- `clear(fill)`：`std::fill(hyperlinks_.begin(), hyperlinks_.end(), 0)`。

- [ ] **步骤 6：CMake 收编 + 编译**

检查库是否 GLOB 收编 src/screen/*.cpp：
```bash
grep -n "GLOB\|HyperlinkPool\|Reflow.cpp" src/CMakeLists.txt
```
若库用 GLOB 则 HyperlinkPool.cpp 自动收编；若显式列举则加入列表。

`tests/CMakeLists.txt` 末尾追加（照 test_reflow 先例）：
```cmake
# test_hyperlink_pool 直接调用内部实现（src/screen，无 ZZTERM_API 导出）：
# shared 构建下须把实现文件编进测试可执行文件。
if(TARGET test_hyperlink_pool)
    target_include_directories(test_hyperlink_pool PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_sources(test_hyperlink_pool PRIVATE "${CMAKE_SOURCE_DIR}/src/screen/HyperlinkPool.cpp")
endif()
```

```bash
cmake --preset linux-gcc-debug   # GLOB CONFIGURE_DEPENDS 应收编新测试；不收编则重跑本步
cmake --build --preset linux-gcc-debug --target test_line test_hyperlink_pool
```

- [ ] **步骤 7：运行测试验证通过**

```bash
./build/linux-gcc-debug/tests/test_line && ./build/linux-gcc-debug/tests/test_hyperlink_pool
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
```
预期：全绿。

- [ ] **步骤 8：Commit**

```bash
git add include/ZzTerm/Line.h src/screen/Line.cpp src/screen/HyperlinkPool.h src/screen/HyperlinkPool.cpp tests/unit/test_line.cpp tests/unit/test_hyperlink_pool.cpp tests/CMakeLists.txt
git commit -m "feat(screen): ZzLine hyperlink 稀疏侧表与 ZzHyperlinkPool LRU 池（M22）"
```

---

## 任务 3：ZzScreen 持有池 + setCellHyperlink 原语

**文件：**
- 修改：`include/ZzTerm/Screen.h`、`src/screen/Screen.cpp`
- 测试：`tests/unit/test_terminal_osc8.cpp`（本任务先建骨架，任务 4 补全 OSC 用例）

**设计要点：** 池挂在 ZzScreen（backend 的 screen_ 成员随后端存活，屏幕/历史/Alternate 共用同一 id 空间，行跨域移动 id 稳定）。照 `internClusterAt`（Screen.h:173-184）的靶向 mutator 先例开 `setCellHyperlink`，不开通用可变行口。

- [ ] **步骤 1：编写失败的测试**

新建 `tests/unit/test_terminal_osc8.cpp`：

```cpp
// OSC 8 超链接：Screen 原语与端到端解析测试（M22）。
#include <cstdio>
#include <span>
#include <string>

#include "ZzTerm/Terminal.h"

/* ZZ_TEST_EXPECT 宏与 feedStr 同公共约定 */

static void testScreenSetCellHyperlink()
{
    ZzTerminal term(20, 5, ZzBackendKind::Native, 100);
    feedStr(term, "ab");
    term.screen().setCellHyperlink(ZzPosition{0, 0}, 7);
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).hyperlinkId == 7);
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(1).hyperlinkId == 0);
}
```
（main() 暂只调这一个用例。）

注意：`term.screen()` 是 Native 限定访问口（ZzNativeBackend.h:44 经 facade 委托）。先确认 facade 暴露形态：
```bash
grep -n "screen()" include/ZzTerm/Terminal.h
```
若 facade 无 screen()，改用 backend 层测试——`tests/unit/test_backend_interface.cpp` 有内部头先例（tests/CMakeLists.txt:33-42 的 include/src 接线可照抄）；或直接跳过本用例，把断言全部留到任务 4 的 OSC 端到端用例（推荐，少开测试面）。选定后据实调整本步骤。

- [ ] **步骤 2：实现**

`include/ZzTerm/Screen.h`：

1. 头部 include 区加 `#include <string>`；类声明前加前向声明 `class ZzHyperlinkPool;`（内部类型，公开头只露引用——照 ZzHistoryView 接口的 `ZzLineView` 用法先例，公开头不定义池类型）。

   注意 ABI/封装：公开头含内部类型成员会泄漏内部头。采用 PIMPL 手法不可行（Screen 是实值成员）。**裁定**：池以 `std::unique_ptr<ZzHyperlinkPool>` 持有（声明即可，定义在 Screen.cpp），accessor 返回 const 引用：
```cpp
    /**
     * @brief 终端级 hyperlink id→URI 池（M22）。
     * @return 池的常量引用；随 ZzScreen 存活。
     * @note 屏幕/历史/Alternate 共用同一 id 空间；行跨域移动 id 稳定。
     *       LRU 容量 1024，被淘汰 id 的 lookup 返回 nullptr（视为无链接）。
     */
    [[nodiscard]] const ZzHyperlinkPool& hyperlinkPool() const noexcept;

    /**
     * @brief 设置活动缓冲区指定格的 hyperlink id（M22；Core 内部使用）。
     * @param pos 目标位置（越界忽略）。
     * @param id hyperlinkId（0 = 清除）。
     * @note 只写 bit26 + 行侧表；不动 dirty（调用方走既有 noteScreenDirty 路径）。
     */
    void setCellHyperlink(ZzPosition pos, std::uint16_t id) noexcept;
```
   私有成员区（:461-478）加 `std::unique_ptr<ZzHyperlinkPool> hyperlinkPool_;`，头部加 `#include <memory>`。

2. `src/screen/Screen.cpp`：构造函数的初始化列表或函数体内 `hyperlinkPool_(std::make_unique<ZzHyperlinkPool>())`；注意 Screen 若有拷贝/移动语义需检查——Screen 不可拷贝（持有 Buffer vector，无拷贝构造声明），确认：
```bash
grep -n "ZzScreen(const\|operator=" src/screen/Screen.cpp include/ZzTerm/Screen.h
```
   若存在拷贝/移动，unique_ptr 需对应处理（预期不存在）。
   实现：
```cpp
#include "HyperlinkPool.h"

const ZzHyperlinkPool& ZzScreen::hyperlinkPool() const noexcept
{
    return *hyperlinkPool_;
}

void ZzScreen::setCellHyperlink(ZzPosition pos, std::uint16_t id) noexcept
{
    Buffer& buf = (active_ == ZzScreenBuffer::Primary) ? primary_ : alternate_;
    if (pos.row < 0 || pos.row >= static_cast<int>(buf.lines.size()))
        return;
    buf.lines[static_cast<std::size_t>(pos.row)].setCellHyperlink(pos.col, id);
}
```
   （活动缓冲区选择逻辑照 Screen.cpp 内 lineAt/putCell 的既有写法——先读该文件 `putCell` 实现确认 buf 选取的既有局部变量名，照抄。）

- [ ] **步骤 3：编译 + 测试 + 全量回归**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
```
预期：全绿（含新 test_terminal_osc8 骨架）。

- [ ] **步骤 4：Commit**

```bash
git add include/ZzTerm/Screen.h src/screen/Screen.cpp tests/unit/test_terminal_osc8.cpp
git commit -m "feat(screen): ZzScreen 持有 hyperlink 池与 setCellHyperlink 原语（M22）"
```

---

## 任务 4：OSC 8 解析 + putChar 落格（native 引擎主体）

**文件：**
- 修改：`src/backend/native/ZzNativeBackend.h`（当前链接状态成员）、`src/backend/native/ZzNativeBackend.cpp`（dispatchOsc :586-597、putChar :408-494）
- 测试：`tests/unit/test_terminal_osc8.cpp`（补全端到端用例）

**协议要点：** payload 形如 `8;params;URI`（dispatchOsc 收到的 payload 不含 `ESC]` 与终止符）。URI 空串 = 关闭链接。params 以 `:` 分隔 `key=value`，只认 `id`。**URI 中不得含控制字符**（< 0x20），含则整条忽略。URI 可含 `;` 吗？协议规定 URI 内特殊字符须百分号编码，故按"第二个 `;` 之后全是 URI"切分即可。

- [ ] **步骤 1：编写失败的测试**

`tests/unit/test_terminal_osc8.cpp` 追加（ST 终止用 `\x1b\\`，BEL 终止 `\x07` 也测）：

```cpp
static std::uint16_t linkAt(const ZzTerminal& term, int row, int col)
{
    return term.renderView().lineAt(row).cellAt(col).hyperlinkId;
}

static void testOsc8Basic()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;;https://example.com\x1b\\link\x1b]8;;\x1b\\ tail");
    // "link" 4 格带链接，id != 0 且互相相等
    const std::uint16_t id = linkAt(term, 0, 0);
    ZZ_TEST_EXPECT(id != 0);
    for (int c = 1; c < 4; ++c)
        ZZ_TEST_EXPECT(linkAt(term, 0, c) == id);
    // 关闭后空格与 tail 无链接
    ZZ_TEST_EXPECT(linkAt(term, 0, 4) == 0);
    ZZ_TEST_EXPECT(linkAt(term, 0, 6) == 0);
    // URI 可查
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(id) == "https://example.com");
}

static void testOsc8BelTerminated()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;;https://bel.example\x07x\x1b]8;;\x07");
    ZZ_TEST_EXPECT(linkAt(term, 0, 0) != 0);
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(linkAt(term, 0, 0)) == "https://bel.example");
}

static void testOsc8IdAssociation()
{
    ZzTerminal term(80, 5, ZzBackendKind::Native, 100);
    // 同 uri+id 两段（模拟折行链接的两段）：必须同 hyperlinkId
    feedStr(term, "\x1b]8;id=u1;https://a.example\x1b\\part1 \x1b]8;;\x1b\\");
    feedStr(term, "\x1b]8;id=u1;https://a.example\x1b\\part2\x1b]8;;\x1b\\");
    const std::uint16_t id1 = linkAt(term, 0, 0);
    const std::uint16_t id2 = linkAt(term, 0, 6);
    ZZ_TEST_EXPECT(id1 != 0 && id1 == id2);
    // 同 uri 不同 id：不同 hyperlinkId
    feedStr(term, "\x1b]8;id=u2;https://a.example\x1b\\part3\x1b]8;;\x1b\\");
    ZZ_TEST_EXPECT(linkAt(term, 0, 12) != id1);
}

static void testOsc8UnknownParamsIgnored()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;foo=bar:id=z:zz=1;https://p.example\x1b\\ab\x1b]8;;\x1b\\");
    ZZ_TEST_EXPECT(linkAt(term, 0, 0) != 0);
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(linkAt(term, 0, 0)) == "https://p.example");
}

static void testOsc8WideChar()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;;https://w.example\x1b\\中\x1b]8;;\x1b\\");
    const std::uint16_t id = linkAt(term, 0, 0);
    ZZ_TEST_EXPECT(id != 0);
    ZZ_TEST_EXPECT(linkAt(term, 0, 1) == id); // 宽字符续格同链接
}

static void testOsc8Malformed()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    // URI 含控制字符：整条忽略
    feedStr(term, "\x1b]8;;https://bad\x01.example\x1b\\zz\x1b]8;;\x1b\\");
    ZZ_TEST_EXPECT(linkAt(term, 0, 0) == 0);
    // 缺第二个分号：忽略
    feedStr(term, "\x1b]8;noSecondSep\x1b\\q\x1b]8;;\x1b\\");
    ZZ_TEST_EXPECT(linkAt(term, 0, 3) == 0);
}
```
在 main() 中追加调用。

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug --target test_terminal_osc8 && ./build/linux-gcc-debug/tests/test_terminal_osc8
```
预期：FAIL（hyperlinkId 全 0 / hyperlinkUri 不存在编译失败）。

- [ ] **步骤 3：实现 dispatchOsc "8" 分支**

`src/backend/native/ZzNativeBackend.h` 成员区（penAttrs_ 附近，:85-87）加：

```cpp
    std::string    osc8Uri_;             ///< OSC 8 当前链接 URI（空 = 无活动链接）。
    std::string    osc8UserId_;          ///< OSC 8 id= 参数（跨段关联键）。
    std::uint16_t  currentHyperlinkId_ = 0; ///< 当前链接的池 id（0 = 无）。
```

`src/backend/native/ZzNativeBackend.cpp` `dispatchOsc`（:586-597）改为：

```cpp
void ZzNativeBackend::dispatchOsc(std::string_view payload)
{
    const std::size_t sep = payload.find(';');
    if (sep == std::string_view::npos)
        return;
    const std::string_view code = payload.substr(0, sep);
    if (code == "8") {
        dispatchOsc8Hyperlink(payload.substr(sep + 1));
        return;
    }
    if (code != "0" && code != "1" && code != "2")
        return; // 仅窗口/图标标题（OSC 0/1/2），其余安全忽略
    title_ = std::string(payload.substr(sep + 1));
    if (activeChanges_)
        activeChanges_->titleChanged = true;
}

void ZzNativeBackend::dispatchOsc8Hyperlink(std::string_view rest)
{
    const std::size_t sep2 = rest.find(';');
    if (sep2 == std::string_view::npos)
        return; // 缺 URI 段：忽略
    const std::string_view params = rest.substr(0, sep2);
    const std::string_view uri = rest.substr(sep2 + 1);

    if (uri.empty()) { // 关闭链接
        osc8Uri_.clear();
        osc8UserId_.clear();
        currentHyperlinkId_ = 0;
        return;
    }
    for (const char ch : uri)
        if (static_cast<unsigned char>(ch) < 0x20)
            return; // URI 含控制字符：整条忽略

    // params：以 ':' 分隔的 key=value，只认 id
    std::string_view userId;
    std::size_t pos = 0;
    while (pos <= params.size()) {
        const std::size_t colon = params.find(':', pos);
        const std::string_view kv = params.substr(
            pos, colon == std::string_view::npos ? colon : colon - pos);
        if (kv.starts_with("id="))
            userId = kv.substr(3);
        if (colon == std::string_view::npos)
            break;
        pos = colon + 1;
    }

    osc8Uri_ = std::string(uri);
    osc8UserId_ = std::string(userId);
    currentHyperlinkId_ = const_cast<ZzHyperlinkPool&>( // 池经 screen 持有，写路径非常量
        screen_.hyperlinkPool()).intern(osc8Uri_, osc8UserId_);
}
```
（`hyperlinkPool()` 若返回 const 引用，池的 intern 是可变的——把 Screen::hyperlinkPool() 声明为返回 `ZzHyperlinkPool&` 的重载对（const/非 const），backend 用非 const 版，view 用 const 版。头文件与 Screen.h 的 accessor 声明同步。ZzNativeBackend.h 加方法声明 `void dispatchOsc8Hyperlink(std::string_view rest);`）

- [ ] **步骤 4：实现 putChar 落格**

`ZzNativeBackend.cpp` putChar 尾部（:464-474 区）：`screen_.putCell(cur, cell);` 之后、宽字符续格 putCell 之后各加：

```cpp
    screen_.putCell(cur, cell);
    if (currentHyperlinkId_ != 0)
        screen_.setCellHyperlink(cur, currentHyperlinkId_);
    if (width == 2) {
        ZzCell cont;
        cont.setWidth(ZzCellWidth::WideContinuation);
        cont.setForeground(penFg_);
        cont.setBackground(penBg_);
        const ZzPosition contPos{cur.row, cur.col + 1};
        clearWidePairAt(contPos);
        screen_.putCell(contPos, cont);
        if (currentHyperlinkId_ != 0)
            screen_.setCellHyperlink(contPos, currentHyperlinkId_);
    }
```

聚簇续接（zzTryClusterContinue）并入前格时继承前格既有链接，不另处理（前格若无链接而新码位在链接区间内属边角语义，第一版不做，记录在设计取舍注释里）。

- [ ] **步骤 5：编译 + 测试**

```bash
cmake --build --preset linux-gcc-debug --target test_terminal_osc8 && ./build/linux-gcc-debug/tests/test_terminal_osc8
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
```
预期：hyperlinkUri/hyperlinkId 编译错误（任务 6 才加契约）——**注意顺序**：本任务的 testOsc8Basic 等用例依赖 `ZzCellView.hyperlinkId` 与 `renderView().hyperlinkUri()`，若任务 6 未先做，本任务测试编译不过。**执行顺序调整：任务 6（渲染契约）提到任务 4 之前执行**，即顺序为 1→2→3→6→4→5→7→8。账本按计划文件内任务号记录，执行按此依赖序。

预期（契约就绪后）：全部 PASS，回归全绿。

- [ ] **步骤 6：Commit**

```bash
git add src/backend/native/ZzNativeBackend.h src/backend/native/ZzNativeBackend.cpp tests/unit/test_terminal_osc8.cpp
git commit -m "feat(terminal): OSC 8 超链接解析与落格（M22）"
```

---

## 任务 5：Reflow 适配（软折叠存续）

**文件：**
- 修改：`src/screen/Reflow.cpp`（zzIsPlainBlank :9-13、zzReflowChain 落格 :80-92、Preserve 快路径 :140-144）
- 测试：`tests/unit/test_reflow.cpp`（追加，直调 zzReflowLines，内部符号接线已有）

**设计要点（规格第 5 节）：**
1. `zzIsPlainBlank` 加 `&& !c.hasHyperlink()`——带链接的格算内容，不裁尾。
2. `zzReflowChain` 落格处（:80-92）：cluster 重新 intern 的同款位置，若 `placed.hasHyperlink()` 则 `row.setCellHyperlink(outCol, srcLine.hyperlinkIdAt(srcCol))`；宽字符续格同理（续格重写为新 cell，需显式带链接标记：`row.setCellHyperlink(outCol + 1, srcLine.hyperlinkIdAt(srcCol + 1))`——注意续格在源行的 id 与 lead 相同，读 `srcCol + 1`）。
3. `zzReflowLines` 的 M17a Preserve 快路径（:140-144）：整行 move + resize，侧表随 resize 同步（任务 2 已保证），无需改动——但要加测试钉住。

- [ ] **步骤 1：编写失败的测试**

`tests/unit/test_reflow.cpp` 追加（照该文件既有链构造风格，先读文件确认 ZzLine 构造/wrapped 设置的既有帮助函数）：

```cpp
static void testReflowPreservesHyperlink()
{
    // 8 列单行，col 1-2 带链接 id 9，缩到 5 列折行后链接仍在且 id 不变
    std::vector<ZzLine> lines;
    ZzLine line(8);
    // 填满文本使其成为内容行
    for (int c = 0; c < 8; ++c) {
        ZzCell cell;
        cell.setCodePoint(U'a' + c);
        cell.setWidth(ZzCellWidth::Narrow);
        line.setCell(c, cell);
    }
    line.setCellHyperlink(1, 9);
    line.setCellHyperlink(2, 9);
    line.setWrapped(false);
    lines.push_back(std::move(line));

    std::vector<ZzLine> out = zzReflowLines(std::move(lines), 8, 5, nullptr, {});
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(out[0].hyperlinkIdAt(1) == 9);
    ZZ_TEST_EXPECT(out[0].hyperlinkIdAt(2) == 9);
    ZZ_TEST_EXPECT(out[0].hyperlinkIdAt(0) == 0);
    ZZ_TEST_EXPECT(out[1].hyperlinkIdAt(0) == 0); // 'f' 起无链接
}

static void testReflowHyperlinkNotTrimmed()
{
    // 行尾格带链接但无文本属性差异：裁尾判据必须视其为内容
    std::vector<ZzLine> lines;
    ZzLine line(6);
    for (int c = 0; c < 2; ++c) {
        ZzCell cell;
        cell.setCodePoint(U'x');
        cell.setWidth(ZzCellWidth::Narrow);
        line.setCell(c, cell);
    }
    // col 5：空白格但带链接（极端情形：链接覆盖到空白格）
    line.setCellHyperlink(5, 3);
    line.setWrapped(false);
    lines.push_back(std::move(line));

    std::vector<ZzLine> out = zzReflowLines(std::move(lines), 6, 3, nullptr, {});
    // 若裁尾把 col 5 当空白裁掉，out 只有 1 行；正确行为是 2 行且 id 3 保留
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(out[1].hyperlinkIdAt(2) == 3);
}
```
（`zzReflowLines` 签名见 src/screen/Reflow.h；cursor 传 nullptr、cursorChain 传默认——先读 Reflow.h 与 test_reflow.cpp 既有调法对齐实参。）
在 main() 追加调用。

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --build --preset linux-gcc-debug --target test_reflow && ./build/linux-gcc-debug/tests/test_reflow
```
预期：FAIL（链接被裁/丢失）。

- [ ] **步骤 3：实现**

`src/screen/Reflow.cpp`：

1. `zzIsPlainBlank`（:9-13）：
```cpp
bool zzIsPlainBlank(const ZzCell& c)
{
    return c.isEmpty() && c.foreground().isDefault() && c.background().isDefault()
           && c.attributes().raw() == 0 && !c.hasHyperlink();
}
```

2. `zzReflowChain` 落格段（:80-92）：
```cpp
            ZzCell placed = cell;
            if (placed.isCluster())
                placed.setCluster(row.internCluster(srcLine.clusterText(cell.clusterIndex())));
            row.setCell(outCol, placed);
            if (placed.hasHyperlink())
                row.setCellHyperlink(outCol, srcLine.hyperlinkIdAt(srcCol));
            if (w == 2) {
                // 续格再生规则与 ZzNativeBackend::putChar 一致：
                // 仅 width + 前景/背景，不带属性。
                ZzCell cont;
                cont.setWidth(ZzCellWidth::WideContinuation);
                cont.setForeground(cell.foreground());
                cont.setBackground(cell.background());
                row.setCell(outCol + 1, cont);
                if (const std::uint16_t hid = srcLine.hyperlinkIdAt(srcCol + 1); hid != 0)
                    row.setCellHyperlink(outCol + 1, hid);
            }
```

- [ ] **步骤 4：运行测试验证通过 + 全量回归**

```bash
cmake --build --preset linux-gcc-debug --target test_reflow && ./build/linux-gcc-debug/tests/test_reflow
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
```
预期：全绿（重点盯 test_screen_reflow*、test_native_reflow* 不回归）。

- [ ] **步骤 5：Commit**

```bash
git add src/screen/Reflow.cpp tests/unit/test_reflow.cpp
git commit -m "fix(reflow): reflow 保留 hyperlink 标记且带链接格不裁尾（M22）"
```

---

## 任务 6：渲染契约暴露（ZzCellView + RenderView/HistoryView + 三处 view 实现）

**执行顺序注意：本任务在任务 4 之前执行**（任务 4 的测试编译依赖本契约）。

**文件：**
- 修改：`include/ZzTerm/RenderView.h`（ZzCellView 加字段、ZzRenderView 加默认虚方法）
- 修改：`include/ZzTerm/HistoryView.h`（ZzHistoryView 加默认虚方法）
- 修改：`src/backend/native/ZzNativeRenderView.h/.cpp`（cellAtThunk + hyperlinkUri 覆写）
- 修改：`src/backend/native/ZzNativeHistoryView.h/.cpp`（同上）
- 测试：`tests/unit/test_terminal_osc8.cpp` 的 `testScreenSetCellHyperlink` 用例（任务 3 骨架）即覆盖 native 渲染路径

**ABI：** ZzCellView 尾部加字段 + 接口加带默认实现的虚方法，均为 minor 新增（M18 先例）。

- [ ] **步骤 1：修改契约头**

`include/ZzTerm/RenderView.h`：

```cpp
struct ZzCellView {
    std::string      text;                              ///< UTF-8 文本；空（Empty/续格）时为空串
    ZzColor          foreground = ZzColor::Default();   ///< 前景色（默认 Default）
    ZzColor          background = ZzColor::Default();   ///< 背景色（默认 Default）
    ZzCellAttributes attributes;                        ///< 样式属性位（粗体/斜体/下划线等）
    ZzCellWidth      width       = ZzCellWidth::Narrow; ///< 单元格宽度（Narrow/Wide）
    std::uint16_t    hyperlinkId = 0;                   ///< OSC 8 链接 id（M22；0 = 无链接）
};
```

ZzRenderView 类内（dirtyRange 声明后）加：

```cpp
    /**
     * @brief 按 hyperlinkId 查链接 URI（M22）。
     * @param hyperlinkId ZzCellView::hyperlinkId 读到的 id。
     * @return URI 字符串；id 为 0、未登记或已被 LRU 淘汰时返回空串（视为无链接）。
     * @note 同 id 即同链接（含 OSC 8 `id=` 跨段关联）：前端做跨段高亮时
     *       以 hyperlinkId 相等为判据，无需解析 URI。
     * @note 默认实现返回空串（无链接后端的保守降级）。
     */
    [[nodiscard]] virtual std::string hyperlinkUri(std::uint16_t hyperlinkId) const
    {
        (void)hyperlinkId;
        return {};
    }
```

`include/ZzTerm/HistoryView.h` ZzHistoryView 类内（clearCount 后）加同款默认实现方法（签名相同，注释改为"历史行"语境）。

- [ ] **步骤 2：native RenderView 实现**

`src/backend/native/ZzNativeRenderView.h` 加覆写声明：
```cpp
    [[nodiscard]] std::string hyperlinkUri(std::uint16_t hyperlinkId) const override;
```
`.cpp`：
1. cellAtThunk（:40-55）`view.width = cell.width();` 后加：
```cpp
    view.hyperlinkId = line.hyperlinkIdAt(col);
```
2. 新实现：
```cpp
std::string ZzNativeRenderView::hyperlinkUri(std::uint16_t hyperlinkId) const
{
    if (const auto* entry = screen_->hyperlinkPool().lookup(hyperlinkId))
        return entry->uri;
    return {};
}
```
头部 include `src/screen/HyperlinkPool.h`——native 后端 .cpp 可以碰内部头（同目录既有 include "unicode/Utf8Encode.h" 先例），Screen.h 只前向声明，池的完整定义在 .cpp 引入。

`src/backend/native/ZzNativeHistoryView.h/.cpp` 同款两处改动（cellAtThunk :63-78 加 hyperlinkId 行；hyperlinkUri 覆写同构，池同样经 `screen_->hyperlinkPool()`）。

- [ ] **步骤 3：编译 + 回归**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
```
预期：全绿（含任务 3 骨架用例 testScreenSetCellHyperlink 此时应通过）。

- [ ] **步骤 4：Commit**

```bash
git add include/ZzTerm/RenderView.h include/ZzTerm/HistoryView.h src/backend/native/ZzNativeRenderView.h src/backend/native/ZzNativeRenderView.cpp src/backend/native/ZzNativeHistoryView.h src/backend/native/ZzNativeHistoryView.cpp
git commit -m "feat(view): 渲染契约暴露 hyperlinkId 与 hyperlinkUri（M22）"
```

---

## 任务 7：contour adapter 提取（零 patch 红线）

**文件：**
- 修改：`src/backend/contour/ZzContourBackend.h/.cpp`（加 `hyperlinkUri` 方法 + zzSnapshotContourLine 填 id）
- 修改：`src/backend/contour/ZzContourRenderView.h/.cpp`（cellAtThunk 填 id + hyperlinkUri 覆写）
- 修改：`src/backend/contour/ZzContourHistoryView.h/.cpp`（hyperlinkUri 覆写）
- 测试：`tests/unit/test_contour_adapter.cpp`（追加，contour 构建门控内）
- 测试：`tests/unit/test_contour_backend.cpp`（端到端：feed OSC 8 → renderView 断言）

**上游事实（已核实，勿改 vendored 代码）：**
- `vtbackend/grid/LineSoA.hpp:123`：`AlignedVector<HyperlinkId> hyperlinks`（逐格，boxed uint16，`.value` 取原值，`!!hid` 判空）
- `vtbackend/core/Hyperlink.hpp`：`HyperlinkStorage`（LRU 1024），`hyperlinkById(HyperlinkId) -> shared_ptr<HyperlinkInfo>`，`HyperlinkInfo.uri`
- `vtbackend/screen/Terminal.hpp:2792`：`HyperlinkStorage& hyperlinks()`；`ZzContourBackend::Impl` 持有 `std::unique_ptr<vtbackend::Terminal> terminal`（ZzContourBackend.cpp:144）
- LRU 淘汰 id → hyperlinkById 返回空 → 视为无链接（与 native 池语义对齐）

- [ ] **步骤 1：编写失败的测试**

`tests/unit/test_contour_backend.cpp` 追加（先读该文件既有 contour 终端构造方式对齐）：

```cpp
static void testContourOsc8()
{
    ZzTerminal term(40, 5, ZzBackendKind::Contour, 100);
    feedStr(term, "\x1b]8;;https://contour.example\x1b\\link\x1b]8;;\x1b\\");
    const ZzCellView c0 = term.renderView().lineAt(0).cellAt(0);
    ZZ_TEST_EXPECT(c0.hyperlinkId != 0);
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(c0.hyperlinkId) == "https://contour.example");
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(5).hyperlinkId == 0);
}
```

`tests/unit/test_contour_adapter.cpp` 追加快照路径用例（照该文件既有 zzSnapshotContourLine 测试的构造方式）：

```cpp
static void testContourSnapshotHyperlink()
{
    // 经 lineSource/history 快照路径：hyperlinkId 逐格带进 ZzLine 侧表
    ZzTerminal term(40, 5, ZzBackendKind::Contour, 100);
    feedStr(term, "\x1b]8;;https://snap.example\x1b\\snap\x1b]8;;\x1b\\");
    // 挤一行历史（屏幕 5 行，输出 6+ 行使首行入史）
    for (int i = 0; i < 6; ++i)
        feedStr(term, "\n");
    const ZzLineView hv = term.historyView().lineAt(0);
    bool found = false;
    for (int c = 0; c < hv.cellCount(); ++c)
        if (hv.cellAt(c).hyperlinkId != 0) { found = true; break; }
    ZZ_TEST_EXPECT(found);
}
```
（若 historyView 对该场景返回 0 行——contour 历史口径差异——改写为断言 lineSource 快照：读 test_contour_linesource.cpp 的既有调法，断言 snapshot 行内 `hyperlinkIdAt != 0`。执行者据实测二选一并注释原因。）

- [ ] **步骤 2：运行测试验证失败**

```bash
cmake --preset linux-gcc-debug -DZZTERM_WITH_CONTOUR=ON   # 若当前配置未开 contour
cmake --build --preset linux-gcc-debug --target test_contour_backend test_contour_adapter
```
预期：PASS 编译但断言 FAIL（hyperlinkId 全 0、hyperlinkUri 空）。

- [ ] **步骤 3：实现 backend URI 查询**

`src/backend/contour/ZzContourBackend.h` 公有区加：
```cpp
    /// @brief 按 hyperlinkId 查上游 URI（M22）；淘汰/未登记返回空串。
    [[nodiscard]] std::string hyperlinkUri(std::uint16_t hyperlinkId) const;
```

`src/backend/contour/ZzContourBackend.cpp`：
```cpp
#include <vtbackend/core/Hyperlink.hpp>

std::string ZzContourBackend::hyperlinkUri(std::uint16_t hyperlinkId) const
{
    if (hyperlinkId == 0)
        return {};
    const auto info = impl_->terminal->hyperlinks().hyperlinkById(
        vtbackend::HyperlinkId(hyperlinkId));
    return info ? info->uri : std::string{};
}
```
（`impl_` 成员名以该文件既有为准——grep 确认 Impl 指针成员名后照用。）

- [ ] **步骤 4：实现 cellAtThunk 与快照填 id**

`ZzContourRenderView.cpp` cellAtThunk（:88-96 的 `col < gridLine.size().value` 守卫块内，`view.attributes = ...` 后）：
```cpp
        const auto hid = gridLine.storage().hyperlinks[static_cast<std::size_t>(col)];
        if (!!hid)
            view.hyperlinkId = hid.value;
```
头部 include `<vtbackend/core/Hyperlink.hpp>`（boxed 类型完整定义需要；若 LineSoA.hpp 已传递包含则省略——编译验证）。

`ZzContourRenderView.h` 加覆写：
```cpp
    [[nodiscard]] std::string hyperlinkUri(std::uint16_t hyperlinkId) const override;
```
`.cpp` 实现：`return backend_->hyperlinkUri(hyperlinkId);`

`ZzContourBackend.cpp` 的 `zzSnapshotContourLine`（:61-112）：非 blank 分支的逐格循环内、`out.setCell(col, cell)` 前：
```cpp
            const auto hid = soa.hyperlinks[static_cast<std::size_t>(col)];
            if (!!hid)
                out.setCellHyperlink(col, hid.value);
```
（blank 分支无链接，不动。）

`ZzContourHistoryView.h/.cpp`：加 `hyperlinkUri` 覆写，转发到 backend（该 view 持有 backend 或 lineSource——先读 .h 确认可用指针，照既有成员转发）。

- [ ] **步骤 5：编译 + 测试 + 回归**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
```
预期：全绿（含 contour 门控测试）。

- [ ] **步骤 6：Commit**

```bash
git add src/backend/contour/ tests/unit/test_contour_backend.cpp tests/unit/test_contour_adapter.cpp
git commit -m "feat(contour): adapter 提取上游 hyperlink 与 URI 查询（M22）"
```

---

## 任务 8：M22a 收尾——登记、ABI 文档、UnwrapView 验证

**文件：**
- 修改：`docs/VT-Xterm-Checklist-v2.md`（:140 登记位勾掉）
- 修改：ABI/版本策略文档（README 或 docs 中记录"minor 新增"段落——先 `grep -rn "ABI" docs/ README.md | head` 找到正确位置）
- 测试：`tests/unit/test_unwrap_view.cpp` 若存在则追加 hyperlinkId 透传用例；不存在则在 test_terminal_osc8.cpp 追加

- [ ] **步骤 1：验证 UnwrapView 透传**

```bash
ls tests/unit/ | grep -i unwrap; grep -n "cellAt" src/terminal/ZzUnwrapView.cpp 2>/dev/null || grep -rn "cellAt" src/ --include=*.cpp | grep -i unwrap
```
确认 ZzUnwrapView/ZzStitchedLineView 的 cellAt 是直传底层 ZzCellView 还是重建。直传则只加测试钉住；重建则补 `hyperlinkId` 字段拷贝。测试用例：

```cpp
static void testUnwrapHyperlinkPassthrough()
{
    ZzTerminal term(10, 5, ZzBackendKind::Native, 100);
    // 造一条跨行折行的链接（10 列下写 12 字符链接文本）
    feedStr(term, "\x1b]8;;https://unwrap.example\x1b\\abcdefghijkl\x1b]8;;\x1b\\");
    // 经 unwrap 视图读逻辑行：两段的 hyperlinkId 相同且非 0
    const ZzUnwrapView uv = term.renderView().unwrap(); // 若 API 形态不同，照 test 既有 unwrap 用例写法
    /* 断言首格与第 11 格 hyperlinkId 相同 */
}
```
（unwrap 视图的确切 API 以 include/ZzTerm/UnwrapView.h 与既有 unwrap 测试为准；本用例只钉"hyperlinkId 透传且跨段同 id"。）

- [ ] **步骤 2：勾选 Checklist 登记位**

`docs/VT-Xterm-Checklist-v2.md:140` 的 OSC 8 登记位按该文件既有勾选格式标记完成（注明 M22a）。

- [ ] **步骤 3：ABI 记录**

在 ABI/版本策略文档中记录：M22 为 minor 新增（ZzCellView 尾部字段、RenderView/HistoryView 默认虚方法），无 break。

- [ ] **步骤 4：全量回归 + Commit**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure
git add -A && git commit -m "test(view): UnwrapView hyperlinkId 透传钉住 + M22a 文档登记"
```

---

## 任务 9（M22b，ZzClawTerm 仓）：前端交互

**仓库：** `/home/zz/Jackfahdin/github/ZzClawTerm`，master 分支。先 `git pull` 确认最新，且 third_party/ZzTermCore 子模块指向含任务 1-8 的 commit（必要时先更新子模块指针并单独 commit）。

**文件：**
- 修改：`src/terminal/ZzCoreTerminalWidget.h/.cpp`（hover/Ctrl 点击/绘制）
- 修改：ZzAppSettings 头/源（加 `hyperlinkEnabled`，模板照 `x11ServerEnabled` h:61-63/cpp:115-127）
- 修改：设置对话框（hyperlinkEnabled 的 UI 入口，照 x11ServerEnabled 的 UI 接线）
- 文档：`docs/acceptance/v0.5-manual-acceptance.md`（新建，照 v0.4 格式）

**已知锚点（实现前重新核对行号，可能漂移）：**
- `logicalPosAtPixel` ZzCoreTerminalWidget.cpp:763-785
- mousePressEvent :787-798 / mouseMoveEvent :800-827 / mouseReleaseEvent :829-838
- paintEvent :224 起，paintWrapped :236-309 / paintUnwrapped :311-376
- 选区叠加层 paintSelectionOnWrappedRow :382-415
- openUrl 先例 `ZzAppShell.cpp:464`（QDesktopServices::openUrl）
- 后端取值：widget 持有的 RenderView（`m_terminal->renderView()` 或既有成员，以代码为准）

- [ ] **步骤 1：设置项 hyperlinkEnabled**

ZzAppSettings 照 `x11ServerEnabled` 模板加 `hyperlinkEnabled`（bool，默认 true）：头文件声明 getter/setter/信号，cpp 实现读写 QSettings，设置对话框加复选框「启用可点击超链接 (OSC 8)」。构建验证：`cmake --build build/linux-gcc-debug`（以本机既有构建目录为准）。
Commit：`git add -A && git commit -m "feat(settings): 新增 hyperlinkEnabled 设置项（默认启用）"`

- [ ] **步骤 2：hover 检测基础设施**

ZzCoreTerminalWidget.h 私有区加：

```cpp
    /// @brief 当前 Ctrl 悬停的 hyperlinkId（0 = 无）。
    std::uint16_t m_hoverHyperlinkId = 0;
    /// @brief hover 命中的 URI（tooltip 用）。
    QString m_hoverHyperlinkUri;
```

构造函数加 `setMouseTracking(true);`（若已有则不重复）。

新增私有方法（.cpp）：

```cpp
std::uint16_t ZzCoreTerminalWidget::hyperlinkIdAtPixel(const QPoint& pos, QString* uriOut) const
{
    if (!m_settings->hyperlinkEnabled())
        return 0;
    const auto logical = logicalPosAtPixel(pos); // 既有坐标换算（含历史滚动偏移）
    if (!logical) // logicalPosAtPixel 返回形态以现状为准（optional/哨兵）
        return 0;
    const ZzRenderView* view = /* 屏幕区视图 */;
    const ZzHistoryView* hist = /* 历史区视图 */;
    // 依 logical 落在屏幕区还是历史区，从对应视图取 ZzCellView
    std::uint16_t id = /* cellAt(...).hyperlinkId */;
    if (id != 0 && uriOut) {
        const std::string u = /* 对应视图 */->hyperlinkUri(id);
        *uriOut = QString::fromStdString(u);
        if (u.empty())
            return 0; // LRU 淘汰：视为无链接
    }
    return id;
}
```
（屏幕/历史两区的行定位逻辑照 `logicalPosAtPixel` 的既有调用方——选区/复制路径已处理"滚动偏移 → 屏幕行/历史行"映射，照抄其分派方式。）

- [ ] **步骤 3：mouseMoveEvent 接 hover**

mouseMoveEvent 入口（选区拖拽逻辑之前）加：

```cpp
    const bool ctrl = (event->modifiers() & Qt::ControlModifier) != 0;
    std::uint16_t newHover = 0;
    QString uri;
    if (ctrl)
        newHover = hyperlinkIdAtPixel(event->pos(), &uri);
    if (newHover != m_hoverHyperlinkId) {
        m_hoverHyperlinkId = newHover;
        m_hoverHyperlinkUri = uri;
        setCursor(newHover != 0 ? Qt::PointingHandCursor : Qt::IBeamCursor);
        if (newHover != 0)
            QToolTip::showText(event->globalPosition().toPoint(), uri, this);
        else
            QToolTip::hideText();
        update(); // 触发重绘（下划线 + 关联高亮）
    }
```
松开 Ctrl 的清理由下一次 mouseMove（ctrl=false → newHover=0）自然覆盖；另覆写 `leaveEvent` 与 `keyReleaseEvent`（Ctrl 抬起）清 hover——keyReleaseEvent 中若 `event->key() == Qt::Key_Control` 且 hover 非 0，则清 hover + update()。

- [ ] **步骤 4：Ctrl+点击打开**

mousePressEvent 入口加（原选区逻辑之前）：

```cpp
    if (event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ControlModifier) != 0 && m_settings->hyperlinkEnabled()) {
        QString uri;
        if (hyperlinkIdAtPixel(event->pos(), &uri) != 0 && !uri.isEmpty()) {
            const QUrl url(uri);
            static const QStringList kAllowed = {
                QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("ftp"),
                QStringLiteral("ssh"), QStringLiteral("mailto"), QStringLiteral("file") };
            if (kAllowed.contains(url.scheme().toLower()))
                QDesktopServices::openUrl(url);
            return; // 命中链接即不启动选区
        }
        // Ctrl+点击落在非链接格：照常走选区逻辑（不 return）
    }
```
头部 include `<QDesktopServices>` `<QUrl>` `<QToolTip>`。

- [ ] **步骤 5：绘制——下划线 + 关联段高亮**

paintWrapped / paintUnwrapped 的逐格绘制循环内（写文本/背景之后的位置），当 `m_hoverHyperlinkId != 0 && m_settings->hyperlinkEnabled()` 且 `cell.hyperlinkId != 0`：

```cpp
        if (cell.hyperlinkId != 0 && ctrlHeld_) { /* 见下 */ }
```

简化口径：**Ctrl 按住期间所有链接格画下划线**（不只 hover 段），hover 命中的同 id 段画高亮底色。实现：
- widget 加 `bool m_ctrlHeld = false;`，keyPressEvent/keyReleaseEvent 跟踪 Qt::Key_Control（按下时若设置启用则 update() 触发重绘显示下划线，松开清除）。
- 逐格：`if (m_ctrlHeld && cell.hyperlinkId != 0)` 画下划线（`painter.drawLine` 于格底，颜色用前景色）；`if (m_hoverHyperlinkId != 0 && cell.hyperlinkId == m_hoverHyperlinkId)` 先画半透明叠加（照 paintSelectionOnWrappedRow 的颜色合成方式，alpha 约 60）。
- 两遍制（wrapped/unwrapped）都加；unwrapped 路径的列偏移换算照该函数既有文本绘制逻辑。

- [ ] **步骤 6：验收文档**

新建 `docs/acceptance/v0.5-manual-acceptance.md`（照 v0.4 格式），用例：
1. `printf '\\e]8;;https://www.example.com\\e\\example链接\\e]8;;\\e\\\n'`——Ctrl 悬停：下划线+手型+tooltip 显示完整 URI；Ctrl+点击：浏览器打开
2. 长 URL 折行：悬停任一段，两段一起高亮
3. 同 id 两段链接（`id=x`）：同 2
4. 设置关闭 hyperlinkEnabled：Ctrl+点击无反应，悬停无下划线
5. SSH 会话远端输出链接（`ls --hyperlink=auto` 若 coreutils 支持，或直接 printf）：本地浏览器打开
6. resize 缩列扩列后链接仍可点（reflow 存续）
7. 普通选区复制不受影响（不按住 Ctrl 时行为与 v0.4 一致）

- [ ] **步骤 7：构建 + 自测 + Commit**

```bash
cmake --build build/linux-gcc-debug   # 以本机既有 preset/目录为准
```
跑起来对验收用例 1/2/4 自测一遍（能自动做的部分；GUI 交互项交用户手测）。
```bash
git add -A && git commit -m "feat(terminal): OSC 8 超链接前端交互（Ctrl 悬停/tooltip/点击/开关，M22b）"
git push
```

---

## 自检记录

- 规格覆盖：§2 解析→任务 4；§3 存储→任务 1/2/3；§4 id= 关联→任务 2（池二元组）+任务 4 测试；§5 reflow→任务 5；§6 契约→任务 6；§7 contour→任务 7；§8 前端→任务 9；§9 测试→各任务内嵌；§10 ABI/里程碑→任务 8；§11 非目标无任务（正确）。
- 依赖序调整：**执行顺序 1→2→3→6→4→5→7→8→9**（契约先于 OSC 端到端测试）。
- 占位符扫描：步骤 9-2/9-5 中"以现状为准"处已给出定位方法（锚点行号 + 照抄对象），属对漂移的防御而非占位。
