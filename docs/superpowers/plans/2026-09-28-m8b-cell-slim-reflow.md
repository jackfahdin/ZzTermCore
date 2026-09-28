# M8b 优化波 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** Cell 12B bit-pack 瘦身 + ChunkedScrollback 裁剪越界修复 + streaming reflow 峰值治理，复测全矩阵并按数据修正门控。

**架构：** T1 修正确性缺陷（headOffset_ 偏移寻址）；T2 在不触碰公开 API 语义的前提下把 ZzCell 从 16B 压到 12B（width 入 text_ 空闲位、attrs 12 位拆进 fg/bg 色字空闲高位）；T3 把 reflow 链处理核抽出共用，scrollback 侧改流式消除 2 倍峰值；T4 复测对照并走门控修正留痕。

**技术栈：** C++20 header-only 位打包（ZzCell 全 constexpr）、std::deque 分块历史、内部纯函数 reflow 核。

**规格：** docs/superpowers/specs/2026-09-28-m8b-cell-slim-reflow-design.md（d43f4a9）

**全局约束：**
- T2 的 ZzCell 公开 API 语义零变化是红线：除 test_cell.cpp 外任何调用方/测试文件改动都视为异常信号，停下报 BLOCKED
- T3 的 reflow 语义一致性红线：test_reflow / test_screen_reflow / test_native_reflow 既有断言零改动通过
- 强制流程（T1）：最小复现 FAIL → 修复 → PASS，FAIL 证据记入报告
- 基线：linux-gcc-debug 50/50、OFF 40/40、shared 50/50、fuzz 2/2、doxygen 零警告
- third_party/contour 零触碰；contour 后端链接一律 PRIVATE

**公共命令：**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug   # 50/50
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check   # 40/40
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check   # 50/50
cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz   # 2/2（T2/T3 收尾）
doxygen Doxyfile   # 仓库根，exit 0 零警告（T2 改公开头，必跑）
```

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `tests/unit/test_scrollback.cpp`（修改） | T1 新增部分裁剪一致性回归用例 |
| `src/history/ChunkedScrollback.cpp`（修改） | T1 headOffset_ 修复；T3 reflow 流式化 |
| `include/ZzTerm/Cell.h`（修改） | T2 12B 位打包（公开头，语义保持） |
| `tests/unit/test_cell.cpp`（修改） | T2 static_assert 12B + 打包不变量用例 |
| `src/screen/Reflow.h` / `Reflow.cpp`（修改） | T3 链处理核抽取 + ZzReflowStreamer |
| `tests/unit/test_reflow.cpp`（修改） | T3 流式与一次性等价性用例 |
| `docs/superpowers/specs/2026-09-22-m8-million-line-probe.md`（T4 修改） | M8b 前后对照续表 + 门控修正建议（待用户确认） |
| `tests/perf/records/2026-09-28-m8b-*.json`（T4 新建） | 复测物证九份 |

---

## 任务 1：ChunkedScrollback 裁剪越界修复（headOffset_）

**文件：**
- 修改：`tests/unit/test_scrollback.cpp`（新增用例）
- 修改：`src/history/ChunkedScrollback.cpp:51-57`（lineAt）、`:99-104`（clear）、`:121-135`（trimToCapacity）、`:137-142`（成员）

**背景（根因已确认）：** `trimToCapacity()` 对头部块部分擦除后块 0 行数小于 256，`lineAt()` 仍按 `index/256` 定长寻址——index 落在 `[head.size(), 256)` 时越界读块 0，更大时整体错位。既有用例从不触发部分裁剪（容量均为小值单块），故从未暴露。

- [ ] **步骤 1：编写最小复现测试（先 FAIL）**

在 `tests/unit/test_scrollback.cpp` 的 `testClearKeepsCounters()` 之后追加：

```cpp
// 6. 部分裁剪后 lineAt 一致性（M8b 回归：trimToCapacity 头部块部分擦除曾破坏
// "除尾块外每块恰 256 行"的定长寻址不变量——块对齐破坏导致越界/错位读）。
static void testPartialTrimConsistency()
{
    char buf[8];
    auto sb = zzCreateChunkedScrollback(300); // 容量非 256 整数倍
    std::vector<ZzLine> batch;
    for (int i = 0; i < 600; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        batch.push_back(makeLine(8, buf, false));
    }
    sb->append(std::move(batch)); // 600 进 300：裁整块 256 + 部分 44
    ZZ_TEST_EXPECT(sb->lineCount() == 300);
    ZZ_TEST_EXPECT(sb->stats().totalDropped == 300);
    for (int i = 0; i < 300; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", 300 + i);
        ZZ_TEST_EXPECT(lineText(sb->lineAt((std::size_t)i), 5) == buf);
    }

    // 部分裁剪后继续 append 再触发一次部分裁剪，校验偏移记账持续正确
    std::vector<ZzLine> more;
    for (int i = 600; i < 700; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        more.push_back(makeLine(8, buf, false));
    }
    sb->append(std::move(more)); // 再裁 100（head 块 212 -> 112）
    ZZ_TEST_EXPECT(sb->lineCount() == 300);
    for (int i = 0; i < 300; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", 400 + i);
        ZZ_TEST_EXPECT(lineText(sb->lineAt((std::size_t)i), 5) == buf);
    }
}
```

并在 main() 中 `testClearKeepsCounters();` 之后加 `testPartialTrimConsistency();`。

- [ ] **步骤 2：运行确认失败**

运行：

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_scrollback --output-on-failure
```

预期：FAIL——越界触发断言 abort 或内容错位断言失败（任一形态都算复现成功）。把失败输出原文摘录进报告（FAIL 证据）。**注意：** 若进程 abort，ctest 显示 Failed 即达成，不要误判为测试本身写错。

- [ ] **步骤 3：headOffset_ 修复**

`src/history/ChunkedScrollback.cpp` 三处改动：

成员区（约 :140 附近）追加：

```cpp
    std::size_t headOffset_ = 0; ///< 头部块被部分裁剪的槽位数（块 0 向量位置 0 对应定长槽位 headOffset_）。
```

lineAt 替换为：

```cpp
    [[nodiscard]] const ZzLine& lineAt(std::size_t index) const override
    {
        // index 0 为最旧一行。调用方保证 index < lineCount()。
        // headOffset_（M8b）：部分裁剪后块 0 不再对齐 256 槽位边界，
        // 物理槽位 = headOffset_ + index；块 0 的向量下标需再减 headOffset_。
        const std::size_t phys  = headOffset_ + index;
        const std::size_t chunk = phys / kChunkLines;
        const std::size_t inner = (phys % kChunkLines) - (chunk == 0 ? headOffset_ : 0);
        return chunks_[chunk][inner];
    }
```

trimToCapacity 的尾部两分支：

```cpp
            if (head.empty()) {
                chunks_.pop_front();
                headOffset_ = 0; // 整块释放：新头块从槽位 0 起
            } else {
                headOffset_ += removable; // 部分擦除：块 0 向量位置 0 前移
            }
```

clear() 追加 `headOffset_ = 0;`。reflow 重建路径（T3 重写前暂不动 reflow 函数体，但需在 reflow 的重建后加 `headOffset_ = 0;`——现实现 chunks_.clear() 后重建，在 `trimToCapacity()` 调用前加一行）。

文件头注释（:12-13）"裁剪从头部整块释放"改为"裁剪从头部释放（可部分擦除头块，由 headOffset_ 维持定长寻址）"。

- [ ] **步骤 4：运行确认通过 + 全量回归**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：50/50（test_scrollback 含新用例通过）。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_scrollback.cpp src/history/ChunkedScrollback.cpp
git commit -m "fix(scrollback): 裁剪部分擦除后 lineAt 越界——headOffset_ 偏移寻址（M8b T1）"
```

---

## 任务 2：ZzCell 12B bit-pack（公开 API 语义零变化）

**文件：**
- 修改：`include/ZzTerm/Cell.h`（ZzColor 加 friend、ZzCellAttributes 加 raw 写口、ZzCell 三字段位打包、注释与 static_assert）
- 修改：`tests/unit/test_cell.cpp`（static_assert 16→12 + 打包不变量用例）

**位分配定稿（规格 §3）：**
- `text_`（32 位）：bit30 cluster 标志；codepoint 模式 bits[20:0] 码位；cluster 模式 bits[23:0] 侧表索引；**bits[25:24] = width**（两模式均空闲——cluster 索引最高位是 bit23）；bit26 预留（未来稀疏扩展）；bits[29:27]、bit31 空闲
- `fgWord_`（32 位）：ZzColor 编码（kind[31:30] + 数据[23:0]）+ **attrs 低 6 位嵌 bits[29:24]**
- `bgWord_`（32 位）：ZzColor 编码 + **attrs 高 6 位嵌 bits[29:24]**
- ZzCellAttributes 已用位恰好 12（bit0-11）；bits[12:15] 无任何 setter 可达（raw() 高位恒 0），不经 Cell 承载——未来 underline color 走行级稀疏侧表（Cell.h:173 既定方向不变）
- reserved_ 字段删除：hyperlink id 等扩展点改由 text_ bit26 或行级侧表承担（规格已留痕）

- [ ] **步骤 1：改 test_cell.cpp（先 FAIL）**

`tests/unit/test_cell.cpp:23` 的 `static_assert(sizeof(ZzCell) == 16);` 改为 `== 12`。main() 尾部（`cell.reset()` 断言块之后）追加打包不变量用例：

```cpp
    // 位打包不变量（M8b 12B 布局）：属性嵌入色字高位，颜色/属性/宽度三路互不影响。
    ZzCell packed;
    packed.setForeground(ZzColor::Rgb(0x11, 0x22, 0x33));
    packed.setBackground(ZzColor::Indexed(200));
    ZzCellAttributes pa;
    pa.setBold(true);
    pa.setFaint(true);
    pa.setItalic(true);
    pa.setUnderline(ZzUnderlineStyle::Curly);
    pa.setBlink(ZzBlinkStyle::Rapid);
    pa.setInverse(true);
    pa.setInvisible(true);
    pa.setStrikethrough(true);
    pa.setProtected(true);
    packed.setAttributes(pa);
    ZZ_TEST_EXPECT(packed.attributes().raw() == pa.raw());          // 12 位往返无损
    ZZ_TEST_EXPECT(packed.foreground() == ZzColor::Rgb(0x11, 0x22, 0x33));
    ZZ_TEST_EXPECT(packed.background() == ZzColor::Indexed(200));
    packed.setForeground(ZzColor::Default());                       // 改色不动属性
    ZZ_TEST_EXPECT(packed.attributes().raw() == pa.raw());
    packed.setAttributes(ZzCellAttributes{});                       // 清属性不动色
    ZZ_TEST_EXPECT(packed.attributes().raw() == 0);
    ZZ_TEST_EXPECT(packed.background() == ZzColor::Indexed(200));

    // 宽度跨文本操作保持（width 驻 text_ bits[25:24]，文本读写不得触碰）。
    packed.setWidth(ZzCellWidth::WideLead);
    packed.setCodePoint(0x4E2D);
    ZZ_TEST_EXPECT(packed.width() == ZzCellWidth::WideLead && packed.codePoint() == 0x4E2D);
    packed.setCluster(7);
    ZZ_TEST_EXPECT(packed.width() == ZzCellWidth::WideLead);
    ZZ_TEST_EXPECT(packed.isCluster() && packed.clusterIndex() == 7);
    packed.clearText();
    ZZ_TEST_EXPECT(packed.width() == ZzCellWidth::WideLead);
    ZZ_TEST_EXPECT(!packed.isCluster() && packed.codePoint() == 0);

    // 相等比较逐字等价（裸字比较 = 字段逐项比较）。
    const ZzCell qc = packed;
    ZZ_TEST_EXPECT(qc == packed);
    ZzCell q2 = packed;
    ZzCellAttributes qa;
    qa.setBold(true);
    q2.setAttributes(qa);
    ZZ_TEST_EXPECT(q2 != packed);
```

运行：

```bash
cmake --build --preset linux-gcc-debug 2>&1 | tail -5
```

预期：编译失败（static_assert 12 在旧布局下不成立）——此即 FAIL 证据，摘录进报告。

- [ ] **步骤 2：Cell.h 位打包实现**

`include/ZzTerm/Cell.h` 改动清单（未列出的方法保持原样）：

2a. ZzColor 类内（private 区前）加友元：

```cpp
    // ZzCell 12B 打包（M8b）需读写 32 位编码原值（attrs 嵌入色字高位）。
    friend struct ZzCell;
```

2b. ZzCellAttributes 类内 private 区加：

```cpp
    // ZzCell 12B 打包（M8b）重建属性位用；bits[12:15] 无 setter 可达，恒为 0。
    constexpr void zzSetRaw(std::uint16_t v) noexcept { bits_ = v; }
    friend struct ZzCell;
```

2c. ZzCell 的方法替换（语义逐条等同现状）：

```cpp
    [[nodiscard]] constexpr ZzCellWidth width() const noexcept
    {
        return static_cast<ZzCellWidth>((text_ & kWidthMask) >> kWidthShift);
    }
    constexpr void setWidth(ZzCellWidth w) noexcept
    {
        text_ = (text_ & ~kWidthMask) | (static_cast<std::uint32_t>(w) << kWidthShift);
    }
    constexpr void setCodePoint(char32_t cp) noexcept
    {
        text_ = (text_ & kWidthMask) | (static_cast<std::uint32_t>(cp) & kCodePointMask);
    }
    constexpr void setCluster(std::uint32_t index) noexcept
    {
        text_ = (text_ & kWidthMask) | kClusterFlag | (index & kClusterIndexMask);
    }
    /// @brief 清除文本载荷（不改动颜色/属性/宽度）。
    constexpr void clearText() noexcept { text_ &= kWidthMask; }

    [[nodiscard]] constexpr ZzColor foreground() const noexcept
    {
        return ZzColor{fgWord_ & kColorMask};
    }
    constexpr void setForeground(ZzColor c) noexcept
    {
        // ZzColor 编码 bits[29:24] 恒 0，直接或运算保住嵌入属性位。
        fgWord_ = (fgWord_ & ~kColorMask) | c.value_;
    }
    [[nodiscard]] constexpr ZzColor background() const noexcept
    {
        return ZzColor{bgWord_ & kColorMask};
    }
    constexpr void setBackground(ZzColor c) noexcept
    {
        bgWord_ = (bgWord_ & ~kColorMask) | c.value_;
    }
    [[nodiscard]] constexpr ZzCellAttributes attributes() const noexcept
    {
        ZzCellAttributes a;
        a.zzSetRaw(static_cast<std::uint16_t>(
            ((fgWord_ >> kAttrsEmbedShift) & kAttrsEmbedMask)
            | (((bgWord_ >> kAttrsEmbedShift) & kAttrsEmbedMask) << 6)));
        return a;
    }
    constexpr void setAttributes(ZzCellAttributes a) noexcept
    {
        const std::uint32_t raw = a.raw(); // bits[12:15] 恒 0（无 setter 可达）
        fgWord_ = (fgWord_ & ~(kAttrsEmbedMask << kAttrsEmbedShift))
                  | ((raw & kAttrsEmbedMask) << kAttrsEmbedShift);
        bgWord_ = (bgWord_ & ~(kAttrsEmbedMask << kAttrsEmbedShift))
                  | (((raw >> 6) & kAttrsEmbedMask) << kAttrsEmbedShift);
    }
```

（isEmpty/isCluster/codePoint/clusterIndex/reset/operator== 逻辑不变——它们只读 kClusterFlag/kCodePointMask 位段与逐字比较，打包后语义等价。）

2d. ZzCell private 区替换为：

```cpp
private:
    static constexpr std::uint32_t kClusterFlag      = 1u << 30;
    static constexpr std::uint32_t kCodePointMask    = 0x1FFFFFu;   ///< 21 位，覆盖 U+10FFFF
    static constexpr std::uint32_t kClusterIndexMask = 0xFFFFFFu;   ///< 24 位索引空间
    static constexpr int           kWidthShift       = 24;          ///< width 驻 text_ bits[25:24]
    static constexpr std::uint32_t kWidthMask        = 0x3u << kWidthShift;
    static constexpr std::uint32_t kColorMask        = 0xC0FFFFFFu; ///< kind[31:30] + 数据[23:0]
    static constexpr int           kAttrsEmbedShift  = 24;          ///< attrs 嵌色字 bits[29:24]
    static constexpr std::uint32_t kAttrsEmbedMask   = 0x3Fu;       ///< 每色字承载 6 位

    std::uint32_t text_   = 0; ///< 文本载荷 + width（位分配见类注释）。
    std::uint32_t fgWord_ = 0; ///< 前景色编码 + attrs 低 6 位。
    std::uint32_t bgWord_ = 0; ///< 背景色编码 + attrs 高 6 位。
```

2e. static_assert 与注释同步：
- 文件尾：`static_assert(sizeof(ZzCell) == 12, ...)`（message 同步改 12），alignof 4 不变
- 文件头（:20-22）布局记录：`sizeof(ZzCell) == 12 字节`，内存量级行同步（10 万行 × 80 列 × 12 B ≈ 96 MB）
- ZzCell 类注释（:300-321）：文本载荷段补 width 位分配（bits[25:24]、bit26 预留）；内存布局记录改 `text_(4) + fgWord_(4) + bgWord_(4) = 12 字节`，并写明 attrs 12 位拆嵌两色字高位、reserved_ 删除与扩展点去向（bit26 / 行级侧表）
- doxygen 陷阱红线：行内 code span 禁尖括号、禁反斜杠转义、内容禁以点开头、后禁紧跟顿号

- [ ] **步骤 3：全量回归（公开头改动，全配置）**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug   # 50/50
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check   # 40/40
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check   # 50/50
cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz   # 2/2
doxygen Doxyfile   # exit 0 零警告
```

预期：全绿且除 test_cell.cpp 外零测试文件改动（红线）。**红线警报：** 若任何其他测试/调用方需要改动才能通过，说明语义保持被打破——停下报 BLOCKED，不得"顺手修"。

- [ ] **步骤 4：内存 sanity + Commit**

```bash
./build/linux-gcc-debug/tests/zz_bench_scrollback --tier=100k
```

预期：approxBytes 较 M8 基线（132MB 量级）降至约 99MB 量级（12/16 = 75%）；记录数值进报告（仓库根临时 JSON 删除）。

```bash
git add include/ZzTerm/Cell.h tests/unit/test_cell.cpp
git commit -m "refactor(cell): ZzCell 12B bit-pack——width 入 text_ 空闲位、attrs 嵌色字高位（M8b T2）"
```

---

## 任务 3：streaming reflow（峰值治理）

**文件：**
- 修改：`src/screen/Reflow.h`（追加 ZzReflowStreamer 声明）
- 修改：`src/screen/Reflow.cpp`（链处理核抽取 + zzReflowLines 重构 + ZzReflowStreamer 实现）
- 修改：`src/history/ChunkedScrollback.cpp`（reflow 流式化重写）
- 修改：`tests/unit/test_reflow.cpp`（流式与一次性等价性用例）

**设计要点：** 把现 zzReflowLines 的链处理循环体抽为内部函数 `zzReflowChain`（指针+链长入参，const 读入、产出追加），zzReflowLines（vector API，Screen 路径用，含光标跟踪）与 ZzReflowStreamer（scrollback 路径用）共用同一核——两端算法零漂移（Reflow.h 头注释的既定原则）。Screen 路径（Screen.cpp:97，行数 = rows 量级）继续走 vector API 不动。

- [ ] **步骤 1：链处理核抽取 + zzReflowLines 重构（行为保持）**

`src/screen/Reflow.cpp` 重构为：

```cpp
#include "Reflow.h"

#include <algorithm>
#include <cassert>

namespace {

/// @brief 完全默认空白格（可裁）：无文本、默认前景背景、无属性。
bool zzIsPlainBlank(const ZzCell& c)
{
    return c.isEmpty() && c.foreground().isDefault() && c.background().isDefault()
           && c.attributes().raw() == 0;
}

/// @brief 链处理核（M8b 从 zzReflowLines 抽取）：处理一条完整逻辑行链，
/// 重组产出追加到 out。输入只读；trackThis/cursor 仅 vector API 的 screen
/// 路径使用（streamer 传 nullptr/false）。语义与 M4 原实现逐字节一致。
void zzReflowChain(const ZzLine* chainLines, std::size_t chainLen, int oldCols, int newCols,
                   std::vector<ZzLine>& out, ZzReflowCursor* cursor, bool trackThis)
{
    const bool isHardLine = (chainLen == 1) && !chainLines[0].wrapped();

    // 链内容的有效末尾（流偏移，不含）：裁掉末尾完全默认空白格。
    std::size_t trimEnd = chainLen * static_cast<std::size_t>(oldCols);
    while (trimEnd > 0) {
        const std::size_t s = trimEnd - 1;
        const ZzCell& c = chainLines[s / oldCols].cellAt((int)(s % oldCols));
        if (!zzIsPlainBlank(c))
            break;
        --trimEnd;
    }

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

    // 热循环：cells_ 连续存储，取行首指针顺序推进源位置。
    std::size_t srcIdx = 0;
    int srcCol = 0;
    const ZzLine* nextLine = &chainLines[srcIdx];
    const ZzCell* nextCells = &nextLine->cellAt(0);
    for (std::size_t s = 0; s < limit; ++s) {
        const ZzLine* srcLine = nextLine;   // 本格所属行（cluster 文本取自此行）
        const ZzCell* srcCells = nextCells;
        const ZzCell& cell = srcCells[srcCol];
        if (++srcCol == oldCols) {
            srcCol = 0;
            if (s + 1 < limit) { // 链尾最后一格之后不再推进，避免越界
                nextLine = &chainLines[++srcIdx];
                nextCells = &nextLine->cellAt(0);
            }
        }
        if (cell.width() == ZzCellWidth::WideContinuation)
            continue; // 续格随 lead 再生

        const int w = (cell.width() == ZzCellWidth::WideLead) ? 2 : 1;
        if (outCol + w > newCols) {
            if (isHardLine)
                break; // 硬行永不多行化：宽字符落边界时直接截断
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
            placed.setCluster(row.internCluster(srcLine->clusterText(cell.clusterIndex())));
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

    // 链末行收尾（硬行也在此收尾）；兜底需在 flushRow 重置 lastContentCol 前取值。
    const int tailContentCol = lastContentCol;
    flushRow(false);

    if (trackThis && !tracked) {
        // 光标落在被裁空白区：兜底到链末行内容尾。
        cursor->row = (int)out.size() - 1;
        cursor->col = std::min(tailContentCol, newCols - 1);
    }
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
        const bool trackThis = cursor && cursor->chainIndex == chainIndex;
        zzReflowChain(&lines[chainStart], chainEnd - chainStart, oldCols, newCols, out, cursor,
                      trackThis);
        ++chainIndex;
        chainStart = chainEnd;
    }
    return out;
}

ZzReflowStreamer::ZzReflowStreamer(int oldCols, int newCols)
    : oldCols_(oldCols)
    , newCols_(newCols)
{
    // 调用方保证合法且列宽有变化（恒等/非法路径由调用方前置过滤，
    // 语义与 zzReflowLines 早退分支对齐）。
    assert(oldCols_ > 0 && newCols_ > 0 && oldCols_ != newCols_);
}

void ZzReflowStreamer::feed(std::vector<ZzLine>& lines, std::vector<ZzLine>& out)
{
    for (auto& line : lines) {
        assert(line.cellCount() == oldCols_); // 全历史同宽不变量
        pending_.push_back(std::move(line));
        if (!pending_.back().wrapped()) {
            // 链完成（当前行是链尾）：处理并清空暂存（缓冲复用，不逐链分配）。
            zzReflowChain(pending_.data(), pending_.size(), oldCols_, newCols_, out,
                          nullptr, false);
            pending_.clear();
        }
    }
}

void ZzReflowStreamer::finish(std::vector<ZzLine>& out)
{
    // 尾链 dangling wrapped 也按完整链处理（与 zzReflowLines 收尾语义一致）。
    if (!pending_.empty()) {
        zzReflowChain(pending_.data(), pending_.size(), oldCols_, newCols_, out, nullptr, false);
        pending_.clear();
    }
}
```

注意：抽核后的 zzReflowChain 与原循环体逐行对应（chainStart 归零化、lines[chainStart + ...] 变 chainLines[...]）；唯一行为差异点是光标跟踪条件由调用方以 trackThis 传入，vector API 的传法与原 `cursor && cursor->chainIndex == chainIndex` 完全一致。

`src/screen/Reflow.h` 追加（放在 zzReflowLines 声明之后）：

```cpp
/**
 * @brief 流式 reflow 器（M8b）：逐批喂入物理行、产出重组后物理行。
 *
 * 跨批只携带未完成链，峰值内存 O(链长)——zzReflowLines 全量进/出为
 * O(全历史)，百万行下产生约 2 倍瞬时峰值。语义与 zzReflowLines 逐字节
 * 一致（共用 zzReflowChain 核）；不支持光标跟踪（scrollback 路径不需要，
 * screen 路径继续走 zzReflowLines）。
 *
 * 前置约定（调用方保证，debug 断言看护）：oldCols/newCols 均大于 0 且不相等
 *（恒等与非法路径由调用方前置过滤，对齐 zzReflowLines 早退分支语义）；
 * 喂入行均为 oldCols 列（全历史同宽不变量）。
 */
class ZzReflowStreamer {
public:
    /// @brief 构造。oldCols/newCols 语义同 zzReflowLines。
    ZzReflowStreamer(int oldCols, int newCols);

    /// @brief 喂入一批物理行（move 消费，返回后 lines 处于移后状态），产出追加到 out。
    /// @param lines 一批物理行（按 wrapped 链序）。
    /// @param out 重组产出行（追加写，调用方持有）。
    void feed(std::vector<ZzLine>& lines, std::vector<ZzLine>& out);

    /// @brief 收尾：冲刷最后一条未完成链（无暂存时为空操作）。
    /// @param out 重组产出行（追加写）。
    void finish(std::vector<ZzLine>& out);

private:
    int oldCols_;
    int newCols_;
    std::vector<ZzLine> pending_; ///< 未完成链暂存（复用缓冲，避免逐链分配）。
};
```

- [ ] **步骤 2：构建 + 语义一致性门禁（重构先行验证）**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "reflow" --output-on-failure
```

预期：test_reflow / test_screen_reflow / test_native_reflow 全部原样 PASS（零断言改动）——抽核重构行为保持的第一道门。

- [ ] **步骤 3：ChunkedScrollback::reflow 流式化重写**

`src/history/ChunkedScrollback.cpp` 的 reflow 整体替换为：

```cpp
    void reflow(int newCols) override
    {
        if (newCols <= 0 || totalLines_ == 0)
            return;
        const int oldCols = chunks_.front().front().cellCount(); // 不变量：全历史同宽
        if (oldCols == newCols)
            return;
        // 流式重组（M8b）：逐块喂入、旧块即时释放，峰值 O(全历史 + 链长)
        // 而非全量 vector 进/出的约 2 倍峰值。
        ZzReflowStreamer streamer(oldCols, newCols);
        std::deque<std::vector<ZzLine>> rebuilt;
        std::vector<ZzLine> produced;
        produced.reserve(kChunkLines);
        while (!chunks_.empty()) {
            std::vector<ZzLine> chunk = std::move(chunks_.front());
            chunks_.pop_front(); // 旧块即时释放，峰值不叠加
            streamer.feed(chunk, produced);
            if (produced.size() >= kChunkLines) {
                rebuilt.push_back(std::move(produced));
                produced.clear();
                produced.reserve(kChunkLines);
            }
        }
        streamer.finish(produced);
        if (!produced.empty())
            rebuilt.push_back(std::move(produced));
        chunks_ = std::move(rebuilt);
        headOffset_ = 0;
        totalLines_ = 0;
        approxBytes_ = 0;
        for (const auto& chunk : chunks_)
            for (const auto& line : chunk) {
                ++totalLines_;
                approxBytes_ += sizeof(ZzLine) +
                                static_cast<std::size_t>(line.cellCount()) * sizeof(ZzCell);
            }
        trimToCapacity();
    }
```

（头部 `#include <cassert>` 若不存在则补；`using std::size_t` 之类不做——沿用文件现有风格。）

- [ ] **步骤 4：流式等价性测试**

`tests/unit/test_reflow.cpp` 追加（该文件已有 target_sources Reflow.cpp 的 shared 构建先例，ZzReflowStreamer 同文件获得）。先读该文件既有构造/断言风格再写，用例如下（可直接采用，也可贴合既有 helper 改写但断言强度不得降低）：

```cpp
// 流式与一次性等价（M8b）：同一输入经 zzReflowLines 与 ZzReflowStreamer
//（整批 / 逐行 / 不规则分批三种喂法）产出必须逐行逐格一致。
// 覆盖：硬行、多长度 wrapped 链、宽字符跨边界、空链尾、dangling wrapped 尾链。
```

构造要求（具体化）：列宽对 (4→2)、(2→4)、(5→3) 各一组；输入含——单行硬行、两条 2-3 物理行的 wrapped 链、一条含宽字符（WideLead+WideContinuation）的链、末尾一条 dangling wrapped（最后一行 wrapped=true）的链；断言：三种喂法产出与 zzReflowLines 产出逐行比较 cellCount/wrapped/逐格 codePoint/width/foreground/background/attributes().raw()。

- [ ] **步骤 5：全量回归 + Commit**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug   # 50/50
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check   # 40/40
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check   # 50/50
```

红线：test_reflow / test_screen_reflow / test_native_reflow 既有断言零改动（本任务只允许追加新用例）；其余测试文件零改动。

```bash
git add src/screen/Reflow.h src/screen/Reflow.cpp src/history/ChunkedScrollback.cpp tests/unit/test_reflow.cpp
git commit -m "refactor(reflow): 链处理核抽取 + scrollback 流式 reflow 消除 2 倍峰值（M8b T3）"
```

---

## 任务 4：全矩阵复测 + probe 续表 + 门控修正建议

**文件：**
- 修改：`docs/superpowers/specs/2026-09-22-m8-million-line-probe.md`（追加 M8b 节）
- 创建：`tests/perf/records/2026-09-28-m8b-*.json`（九份复测物证）

**执行注意：** bench-long 用后台任务执行（disable_timeout）。本任务**不修门控阈值本体**——probe 续表给出修正建议并标注"待用户确认"，阈值文本修改由主代理在用户确认后单独完成（决策点惯例）。

- [ ] **步骤 1：全回归基线确认**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug   # 50/50
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check   # 40/40
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check   # 50/50
cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz   # 2/2
doxygen Doxyfile   # exit 0 零警告
```

- [ ] **步骤 2：复测全矩阵**

```bash
cmake --build build/linux-gcc-debug --target bench-long
cd build/linux-gcc-debug/tests
./zz_bench_scrollback            # 补齐 10k 短跑档 JSON（同目录）
./zz_bench_feed --profile=ascii
./zz_bench_feed --profile=mixed
```

预期：六条长跑 + 三条短跑全部 exit 0；九份 JSON 落盘（文件名与 M8 相同形态，内容为 M8b 复测值）。

- [ ] **步骤 3：物证入库**

```bash
cd build/linux-gcc-debug/tests
for f in zzterm-bench-scrollback-10k zzterm-bench-scrollback-100k zzterm-bench-scrollback-1m \
         zzterm-bench-feed-ascii-10k zzterm-bench-feed-ascii-100k zzterm-bench-feed-ascii-1m \
         zzterm-bench-feed-mixed-10k zzterm-bench-feed-mixed-100k zzterm-bench-feed-mixed-1m; do
    cp "$f.json" "../../../tests/perf/records/2026-09-28-m8b-${f#zzterm-bench-}.json"
done
```

- [ ] **步骤 4：probe 续表**

在 `docs/superpowers/specs/2026-09-22-m8-million-line-probe.md` 末尾追加（数值从九份 JSON 与 stdout 转录，不得凭记忆编造；与 M8 基线九份 JSON 逐项对照）：

```markdown
## 6. M8b 优化波复测对照（2026-09-28，contour 分支）

- 变更：ZzCell 16B → 12B bit-pack（T2）+ streaming reflow（T3）+ 裁剪越界修复（T1）
- preset 与机器：同 §头部（linux-gcc-debug -O0，同机）
- 数据源：tests/perf/records/2026-09-28-m8b-*.json（九份）

### 6.1 单元轨前后对照

| 档位 | 画像 | append ms 前→后 | reflow widen ms 前→后 | RSS MB 前→后 | approxBytes MB 前→后 |
|---|---|---|---|---|---|
| 1m | ascii | | | | |
| 1m | mixed | | | | |
| 100k | ascii | | | | |
| 100k | mixed | | | | |
| 10k | ascii | | | | |
| 10k | mixed | | | | |

### 6.2 facade 轨前后对照

| 档位 | 画像 | feed ms 前→后 | search ms 前→后 | reflow widen ms 前→后 | RSS MB 前→后 | peak RSS MB 前→后 |
|---|---|---|---|---|---|---|
| 1m | ascii | | | | | |
| 1m | mixed | | | | | |
| 100k | ascii | | | | | |
| 100k | mixed | | | | | |
| 10k | ascii | | | | | |
| 10k | mixed | | | | | |

### 6.3 门控修正建议（待用户确认）

[按复测实测给出四项门控的原值/新值/理由表。预设候选口径（规格
2026-09-28-m8b §5）：单元轨 1M RSS 新阈值 1100MB；facade peak 口径按实测
定（消除峰值翻倍后预期显著低于 6712MB）；search 复测值如实记录并给出
达标/不达标判定建议。所有修正值必须引用 6.1/6.2 实测数字作为理由。]

### 6.4 结论

[优化幅度总结 + search 门控复测结论 + 遗留项（如有）。]
```

- [ ] **步骤 5：Commit + 回报**

```bash
git add docs/superpowers/specs/2026-09-22-m8-million-line-probe.md tests/perf/records/2026-09-28-m8b-*.json
git commit -m "docs(probe): M8b 优化波全矩阵复测对照与门控修正建议"
```

回报主代理：门控修正建议表全文（probe §6.3）、search 复测结论、全部回归证据。主代理报用户确认后再落阈值文本修改。

---

## 自检记录（编写期）

- 规格覆盖：§2 T1 → 任务 1（复现 FAIL→修复→PASS 流程、headOffset_ 三处改动、文件头注释修正）；§3 T2 → 任务 2（位分配定稿、全部受影响方法的真实代码、static_assert 与注释同步、打包不变量用例）；§4 T3 → 任务 3（链核抽取、流式器、scrollback 接入、等价性用例、Screen 路径不动的核实依据）；§5 T4 → 任务 4（复测、续表模板、修正建议待确认通道）；§7 DoD → 各任务验证步骤 + T4 步骤 1 全回归。
- 占位符：probe 续表留空表是 T4 执行期填数项（测量产物）；T3 步骤 4 用例"可读既有文件贴合风格"是明确的实施指引且给出具体构造要求，非占位。
- 类型一致：headOffset_（T1 定义、T3 reflow 重置引用一致）；ZzReflowStreamer/zzReflowChain 签名（步骤 1 定义与步骤 3 使用一致：feed(vector&, vector&)、finish(vector&)）；kWidthMask/kColorMask/kAttrsEmbedShift/kAttrsEmbedMask/kWidthShift 常量名 T2 内自洽；ZzColor::value_ 与 private ctor 经 friend struct ZzCell 可达（ZzCell 确为 struct）。
- 风险预案：T2 红线（除 test_cell.cpp 外零测试改动）与 BLOCKED 上报通道已写入步骤 3。
