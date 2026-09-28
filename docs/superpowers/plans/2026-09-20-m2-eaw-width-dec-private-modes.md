# M2（EAW 真实宽度表 + DEC 私有模式）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为 native 引擎接入真实 East Asian Width 宽度表与宽字符落格流，并接线 DEC 私有模式 ?1049/?1047/?1048（备用屏幕）、?7（DECAWM）、?25（DECTCEM），使双后端 compat 测试 3 处 b 类分歧恢复逐格强对照。

**架构：** Unicode 官方钉版数据经 Python 脚本生成紧凑区间表内嵌公开头（二分查找，构建期无网络依赖）；宽字符流在 `ZzNativeBackend::putChar` 内闭环（双格写入、右边界换行、半格覆写清理）；DEC 私有模式在 `NativeCsiDispatch.cpp` 新增分发入口，语义组合 `ZzScreen` 已有原语（setActiveBuffer/saveCursor/setAutoWrapMode/setCursorStyle）。Contour 后端零改动。

**技术栈：** C++20、Python 3 标准库（仅开发期生成脚本）、CMake Presets（linux-gcc-debug）、CTest。

**规格：** `docs/superpowers/specs/2026-09-20-m2-eaw-width-dec-private-modes-design.md`（已批准）

**通用约定：**

- 构建/测试：`cmake --preset linux-gcc-debug`（首次或 CMake 变更后）、`cmake --build --preset linux-gcc-debug`、`ctest --preset linux-gcc-debug`（全量）、`ctest --preset linux-gcc-debug -R <测试名> --output-on-failure`（单个）。
- 测试风格：每个 `tests/unit/*.cpp` 自带 `main()`，`ZZ_CHECK` 宏计数失败（仿 `tests/unit/test_backend_compat.cpp:13-19`），文件名即 CTest 名，GLOB 自动收编，无需改 CMake。
- 公开 API 测试只 include `ZzTerm/Terminal.h` 等公开头、链接 `ZzTermCore`。
- commit 规范：`type(scope): 中文描述`。
- doxygen 陷阱（docs/ 与公开头注释）：行内 code span 内禁尖括号、内容禁以点开头、后禁紧跟顿号、禁 `#ifdef` 等 `#` 预处理词。
- 每个任务结束必须全量 ctest 绿 + `doxygen Doxyfile` 零警告（改了公开头或 docs 时）再 commit。

---

### 任务 1：EAW 宽度表 + putChar 宽字符流（native 端到端）

**文件：**
- 创建：`scripts/gen_unicode_width.py`
- 创建：`include/ZzTerm/detail/UnicodeWidthData.inc`（脚本生成物，入库）
- 重写：`include/ZzTerm/UnicodeWidth.h`
- 修改：`src/backend/native/ZzNativeBackend.h`（新增 `clearWidePairAt` 声明）
- 修改：`src/backend/native/ZzNativeBackend.cpp:103-140`（putChar 宽字符流 + clearWidePairAt 实现）
- 测试：`tests/unit/test_unicode_width.cpp`（新）、`tests/unit/test_native_widechar.cpp`（新）、`tests/unit/test_backend_compat.cpp:97-118`（用例 5 恢复强对照）

- [ ] **步骤 1：写生成脚本 `scripts/gen_unicode_width.py`**

```python
#!/usr/bin/env python3
"""从 unicode.org 拉取钉版 EastAsianWidth.txt，生成紧凑区间表 .inc（开发期工具，不进构建链）。

用法：python3 scripts/gen_unicode_width.py（仓库根目录下运行；仅标准库）。
"""
import datetime
import os
import sys
import tempfile
import urllib.request

UNICODE_VERSION = "16.0.0"
URL = f"https://www.unicode.org/Public/{UNICODE_VERSION}/ucd/EastAsianWidth.txt"
OUT = "include/ZzTerm/detail/UnicodeWidthData.inc"
KEEP = {"W": "Wide", "F": "Fullwidth", "A": "Ambiguous"}


def main() -> int:
    try:
        with urllib.request.urlopen(URL, timeout=30) as resp:
            if resp.status != 200:
                print(f"下载失败：HTTP {resp.status}", file=sys.stderr)
                return 1
            text = resp.read().decode("utf-8")
    except Exception as exc:  # 网络不可达等
        print(f"下载失败：{exc}", file=sys.stderr)
        return 1

    intervals = []  # [(lo, hi, cls)]
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(";")]
        if len(fields) != 2:
            print(f"第 {lineno} 行格式不符：{raw!r}", file=sys.stderr)
            return 1
        rng, cls = fields
        if cls not in KEEP:
            continue
        lo_s, _, hi_s = rng.partition("..")
        lo, hi = int(lo_s, 16), int(hi_s or lo_s, 16)
        if lo > hi:
            print(f"第 {lineno} 行区间方向异常：{rng!r}", file=sys.stderr)
            return 1
        intervals.append((lo, hi, cls))

    intervals.sort()
    merged = []
    for lo, hi, cls in intervals:
        if merged and merged[-1][2] == cls and lo <= merged[-1][1] + 1:
            merged[-1] = (merged[-1][0], max(merged[-1][1], hi), cls)
        else:
            merged.append((lo, hi, cls))
    for prev, cur in zip(merged, merged[1:]):
        if cur[0] <= prev[1]:
            print(f"合并后区间重叠：{prev} vs {cur}", file=sys.stderr)
            return 1

    date = datetime.date.today().isoformat()
    out = [
        "// 本文件由 scripts/gen_unicode_width.py 生成，请勿手改。",
        f"// 数据源：{URL}",
        f"// Unicode 版本：{UNICODE_VERSION}  生成日期：{date}",
        "// 仅收录 W/F/A 三类；N/Na/H 与未列出码位默认窄（返回 1），不入表。",
        "// 区间按 lo 升序、互不重叠，供 zzCellWidthOf 二分查找。",
        f"inline constexpr std::array<ZzEawInterval, {len(merged)}> kZzEawIntervals {{{{",
    ]
    out += [
        f"    ZzEawInterval{{0x{lo:04X}u, 0x{hi:04X}u, ZzEawClass::{KEEP[cls]}}},"
        for lo, hi, cls in merged
    ]
    out.append("}};")
    content = "\n".join(out) + "\n"

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(OUT), suffix=".tmp")
    with os.fdopen(fd, "w") as fh:
        fh.write(content)
    os.replace(tmp, OUT)  # 原子替换，不产生半截生成物
    print(f"生成 {OUT}：{len(merged)} 个区间，Unicode {UNICODE_VERSION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **步骤 2：运行脚本生成区间表**

运行：`python3 scripts/gen_unicode_width.py`
预期：输出 `生成 include/ZzTerm/detail/UnicodeWidthData.inc：<数百> 个区间，Unicode 16.0.0`；`head include/ZzTerm/detail/UnicodeWidthData.inc` 可见版本与来源注释。网络不可达时显式报错非零退出（不得手造数据）。

- [ ] **步骤 3：重写 `include/ZzTerm/UnicodeWidth.h`（真实查找替换 M1 占位）**

完整替换为：

```cpp
#pragma once

/**
 * @file UnicodeWidth.h
 * @brief East Asian Width 单元格宽度计算（UAX #11，per-codepoint）。
 *
 * Architecture.md 第 6 节禁止假设 1 code point 等于 1 cell，第 8 节要求
 * 支持 East Asian Width；第 2 节平台无关约束禁止依赖系统 wcwidth(3)，
 * 故宽度数据以紧凑区间表内嵌（由 scripts/gen_unicode_width.py 从
 * Unicode 官方钉版数据生成，版本见 UnicodeWidthData.inc 头部）。
 * grapheme 聚簇（UAX #29：combining/VS/emoji/ZWJ）在此接口之上的
 * 分层实现属 M3，本接口只回答单码位列宽。
 */
#include <array>
#include <cstdint>

/// @brief EAW 类别（仅收录需查表的三类；N/Na/H 与未列出码位默认窄，不入表）。
enum class ZzEawClass : std::uint8_t {
    Wide,      ///< W：全宽（CJK 表意文字等），2 列。
    Fullwidth, ///< F：全角（全角 ASCII 等），2 列。
    Ambiguous  ///< A：歧义（部分希腊/符号等），列宽由配置决定。
};

/// @brief EAW 区间表条目（闭区间码位范围 + 类别）。
struct ZzEawInterval {
    std::uint32_t lo; ///< 区间起点码位（含）。
    std::uint32_t hi; ///< 区间终点码位（含）。
    ZzEawClass cls;   ///< 类别。
};

#include "ZzTerm/detail/UnicodeWidthData.inc"

/**
 * @brief 返回码位的单元格宽度（1 或 2 列，UAX #11 East Asian Width）。
 * @param cp Unicode 码位（调用方保证 UTF-8 解码合法；大于 0x10FFFF 防御性返回 1）。
 * @param ambiguousWide true 时 Ambiguous 类别按 2 列（CJK 环境）；
 *        默认 false 按 1 列（xterm 兼容行为）。
 * @return 单元格宽度（1 或 2）。
 * @note W/F 返回 2；A 由 ambiguousWide 决定；其余（含 combining/控制区间）
 *       返回 1 并独立落格——聚簇归并属 M3，此处注释钉住。
 */
[[nodiscard]] inline constexpr int zzCellWidthOf(char32_t cp, bool ambiguousWide = false) noexcept
{
    const std::uint32_t u = static_cast<std::uint32_t>(cp);
    if (u > 0x10FFFFu)
        return 1;
    std::size_t lo = 0;
    std::size_t hi = kZzEawIntervals.size();
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const ZzEawInterval& e = kZzEawIntervals[mid];
        if (u < e.lo) {
            hi = mid;
        } else if (u > e.hi) {
            lo = mid + 1;
        } else {
            switch (e.cls) {
            case ZzEawClass::Wide:
            case ZzEawClass::Fullwidth:
                return 2;
            case ZzEawClass::Ambiguous:
                return ambiguousWide ? 2 : 1;
            }
        }
    }
    return 1;
}
```

- [ ] **步骤 4：写表完整性测试 `tests/unit/test_unicode_width.cpp`**

```cpp
// EAW 区间表完整性 + 已知码位抽查（M2）。
#include <ZzTerm/UnicodeWidth.h>

#include <cstdio>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

// 表结构：升序、不重叠、码位合法、条目非空。
void testTableIntegrity()
{
    ZZ_CHECK(kZzEawIntervals.size() > 100); // 合并后约数百区间；过少说明生成退化
    for (std::size_t i = 0; i < kZzEawIntervals.size(); ++i) {
        const ZzEawInterval& e = kZzEawIntervals[i];
        ZZ_CHECK(e.lo <= e.hi);
        ZZ_CHECK(e.hi <= 0x10FFFFu);
        if (i > 0)
            ZZ_CHECK(e.lo > kZzEawIntervals[i - 1].hi);
    }
}

// 已知码位抽查（Unicode 16.0 稳定值）。
void testKnownCodePoints()
{
    ZZ_CHECK(zzCellWidthOf(U'中') == 2);  // U+4E2D W
    ZZ_CHECK(zzCellWidthOf(U'世') == 2);  // U+4E16 W
    ZZ_CHECK(zzCellWidthOf(U'Ａ') == 2);  // U+FF21 F（全角 A）
    ZZ_CHECK(zzCellWidthOf(U'A') == 1);   // U+0041 Na
    ZZ_CHECK(zzCellWidthOf(U'1') == 1);   // U+0031 Na
    ZZ_CHECK(zzCellWidthOf(U'·') == 1);   // U+00B7 A，默认窄
    ZZ_CHECK(zzCellWidthOf(U'·', true) == 2);  // Ambiguous 宽模式
    ZZ_CHECK(zzCellWidthOf(U'α') == 1);   // U+03B1 A（希腊字母），默认窄
    ZZ_CHECK(zzCellWidthOf(static_cast<char32_t>(0x110000)) == 1); // 越界防御
}

// 表内区间端点抽查：首/末区间两端点的判定与类别一致。
void testIntervalEndpoints()
{
    const ZzEawInterval& first = kZzEawIntervals.front();
    const ZzEawInterval& last = kZzEawIntervals.back();
    for (const ZzEawInterval* e : { &first, &last }) {
        const int expected = (e->cls == ZzEawClass::Ambiguous) ? 1 : 2;
        ZZ_CHECK(zzCellWidthOf(static_cast<char32_t>(e->lo)) == expected);
        ZZ_CHECK(zzCellWidthOf(static_cast<char32_t>(e->hi)) == expected);
    }
}

} // namespace

int main()
{
    testTableIntegrity();
    testKnownCodePoints();
    testIntervalEndpoints();
    if (g_failures != 0)
        std::fprintf(stderr, "test_unicode_width: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 5：构建并跑表测试**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_unicode_width --output-on-failure`
预期：PASS。（注意：`test_backend_compat` 此时会红——putChar 已对新宽度表响应但宽字符流未实现，属预期，步骤 8 修复。）

- [ ] **步骤 6：写 native 宽字符流测试 `tests/unit/test_native_widechar.cpp`（先红）**

```cpp
// native 引擎宽字符落格流（M2）：双格写入、光标 +2、行尾换行、
// 半格覆写清理、行尾 wrap-pending。仅公开 API（facade + RenderView）。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string_view>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

void feed(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                      bytes.size()));
}

ZzCellView cell(const ZzTerminal& t, int row, int col)
{
    return t.renderView().lineAt(row).cellAt(col);
}

// 宽字符双格写入 + 光标前进 2 列 + 后续窄字符落在第 3 列。
void testWidePair()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "\xE4\xB8\xAD" "A"); // "中A"
    ZZ_CHECK(cell(t, 0, 0).text == "\xE4\xB8\xAD");
    ZZ_CHECK(cell(t, 0, 0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(cell(t, 0, 1).width == ZzCellWidth::WideContinuation);
    ZZ_CHECK(cell(t, 0, 1).text.empty());
    ZZ_CHECK(cell(t, 0, 2).text == "A");
    ZZ_CHECK(cell(t, 0, 2).width == ZzCellWidth::Narrow);
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 3 }));
}

// 宽字符在行尾最后一列放不下：留空、立即换行到新行行首落格。
void testWideAtRightEdge()
{
    ZzTerminal t(5, 4, ZzBackendKind::Native, 0);
    feed(t, "ABCD\xE4\xB8\xAD"); // "ABCD中"
    ZZ_CHECK(cell(t, 0, 3).text == "D");
    ZZ_CHECK(cell(t, 0, 4).text.empty());           // 行尾留空
    ZZ_CHECK(cell(t, 0, 4).width != ZzCellWidth::WideLead);
    ZZ_CHECK(cell(t, 1, 0).text == "\xE4\xB8\xAD"); // 宽字符落新行行首
    ZZ_CHECK(cell(t, 1, 0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(cell(t, 1, 1).width == ZzCellWidth::WideContinuation);
    ZZ_CHECK(t.cursor().position == (ZzPosition { 1, 2 }));
}

// 半格覆写：新字符落在宽字符的续格上，首格清为空格；落在首格上，续格清空。
void testOverwriteHalfWide()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "\xE4\xB8\xAD");
    feed(t, "\x1b[1;2H"); // 光标到行 0 列 1（续格位置，1 起始）
    feed(t, "X");
    ZZ_CHECK(cell(t, 0, 1).text == "X");
    ZZ_CHECK(cell(t, 0, 0).text.empty());                 // 首格被清理
    ZZ_CHECK(cell(t, 0, 0).width != ZzCellWidth::WideLead);

    ZzTerminal t2(10, 4, ZzBackendKind::Native, 0);
    feed(t2, "\xE4\xB8\xAD");
    feed(t2, "\x1b[1;1H"); // 回首格位置
    feed(t2, "Y");
    ZZ_CHECK(cell(t2, 0, 0).text == "Y");
    ZZ_CHECK(cell(t2, 0, 1).text.empty());                       // 续格被清理
    ZZ_CHECK(cell(t2, 0, 1).width != ZzCellWidth::WideContinuation);
}

// 宽字符占满行尾两格：光标停最后一列并置 wrap-pending，下一字符换行。
void testWideWrapPending()
{
    ZzTerminal t(4, 4, ZzBackendKind::Native, 0);
    feed(t, "AB\xE4\xB8\xAD"); // "AB中"（中占列 2-3）
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 3 }));
    feed(t, "Z");
    ZZ_CHECK(cell(t, 1, 0).text == "Z"); // Z 换行落新行行首
    ZZ_CHECK(t.renderView().lineAt(0).wrapped()); // 行 0 软换行标记
}

} // namespace

int main()
{
    testWidePair();
    testWideAtRightEdge();
    testOverwriteHalfWide();
    testWideWrapPending();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_widechar: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 7：跑新测试确认红**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_widechar --output-on-failure`
预期：FAIL（行尾换行、续格写入、覆写清理均未实现）。

- [ ] **步骤 8：实现 putChar 宽字符流**

`src/backend/native/ZzNativeBackend.h` 语义方法区（`putChar` 声明附近）新增：

```cpp
    /// 覆写一致性：pos 覆盖既有宽字符任一半时，另一半清为空格（保留被清格背景）。
    void clearWidePairAt(ZzPosition pos) noexcept;
```

`src/backend/native/ZzNativeBackend.cpp` 在 `eraseFill()` 之后新增实现，并整体替换 `putChar`：

```cpp
void ZzNativeBackend::clearWidePairAt(ZzPosition pos) noexcept
{
    const ZzSize sz = screen_.size();
    const ZzLine& line = screen_.lineAt(pos.row);
    const ZzCell& c = line.cellAt(pos.col);
    if (c.width() == ZzCellWidth::WideLead && pos.col + 1 < sz.cols) {
        const ZzCell& right = line.cellAt(pos.col + 1);
        if (right.width() == ZzCellWidth::WideContinuation) {
            ZzCell blank;
            blank.setBackground(right.background());
            screen_.putCell(ZzPosition{pos.row, pos.col + 1}, blank);
        }
    } else if (c.width() == ZzCellWidth::WideContinuation && pos.col > 0) {
        const ZzCell& left = line.cellAt(pos.col - 1);
        if (left.width() == ZzCellWidth::WideLead) {
            ZzCell blank;
            blank.setBackground(left.background());
            screen_.putCell(ZzPosition{pos.row, pos.col - 1}, blank);
        }
    }
}

void ZzNativeBackend::putChar(char32_t cp)
{
    const ZzSize sz = screen_.size();
    const ZzCellRange region = screen_.scrollRegionRows(); // [top, bottom+1)
    ZzPosition cur = screen_.cursor().position;
    const int width = zzCellWidthOf(cp); // Ambiguous 配置口任务 4 接入第二参

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

    // 宽字符在最后一列放不下：当前格留空（画笔背景），立即换行到新行行首。
    // 与 DECAWM 无关：xterm 宽字符不可截半显示，总是换行（若实测 Contour
    // 行为不同，compat 用例注释钉住分歧）。
    if (width == 2 && cur.col == sz.cols - 1) {
        ZzCell blank;
        blank.setBackground(penBg_);
        clearWidePairAt(cur);
        screen_.putCell(cur, blank);
        screen_.setLineWrapped(cur.row, true);
        if (cur.row == region.endCol - 1)
            screen_.scrollUp(1, eraseFill());
        else
            ++cur.row;
        cur.col = 0;
    }

    ZzCell cell;
    cell.setWidth(width == 2 ? ZzCellWidth::WideLead : ZzCellWidth::Narrow);
    cell.setCodePoint(cp);
    cell.setForeground(penFg_);
    cell.setBackground(penBg_);
    cell.setAttributes(penAttrs_);
    clearWidePairAt(cur);
    screen_.putCell(cur, cell);
    if (width == 2) {
        ZzCell cont;
        cont.setWidth(ZzCellWidth::WideContinuation);
        cont.setForeground(penFg_);
        cont.setBackground(penBg_);
        const ZzPosition contPos{cur.row, cur.col + 1};
        clearWidePairAt(contPos);
        screen_.putCell(contPos, cont);
    }
    noteScreenDirty();

    if (width == 2) {
        // 宽字符占满行尾两格：光标停最后一列，置 wrap-pending（下一字符换行）。
        if (cur.col + 1 == sz.cols - 1) {
            screen_.setCursorPosition(ZzPosition{cur.row, sz.cols - 1});
            if (screen_.autoWrapMode())
                screen_.setWrapPending(true);
        } else {
            screen_.setCursorPosition(ZzPosition{cur.row, cur.col + 2});
        }
    } else if (cur.col < sz.cols - 1) {
        screen_.setCursorPosition(ZzPosition{cur.row, cur.col + 1});
    } else if (screen_.autoWrapMode()) {
        // 最后一列：光标不动，置 wrap-pending（下一个可打印字符才换行）。
        screen_.setWrapPending(true);
    }
    // DECAWM 关闭时在最后一列：光标不动、不置标志，后续字符覆盖该格。
}
```

注意：`ZzNativeBackend.cpp` 顶部需 `#include "ZzTerm/Line.h"`（若未间接包含）以获得 `ZzLine::cellAt`。

- [ ] **步骤 9：跑宽字符测试确认绿**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "test_native_widechar|test_unicode_width" --output-on-failure`
预期：全部 PASS。

- [ ] **步骤 10：compat 用例 5 恢复逐格强对照**

`tests/unit/test_backend_compat.cpp:97-118` 的 `testCjkWide` 整体替换为：

```cpp
// 5. CJK 宽字符（M2：两后端均为真实 UAX #11 宽度，恢复逐格强对照）。
// 样例码位取两后端 Unicode 数据中稳定为宽的常用 CJK 区间，规避版本漂移。
void testCjkWide()
{
    Dual d;
    d.feedBoth("\xE4\xB8\xAD" "A" "\xE4\xB8\x96"); // "中A世"
    checkRowEqual(d.native, d.contour, 0, 5, "cjk-wide");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
}
```

若实测 Contour 在某格表现不同（Unicode 版本漂移），先确认漂移码位、换用稳定码位；确属语义分歧再按既有 b 类规则分别断言并注释钉住（规格 5 风险节）。

- [ ] **步骤 11：全量测试 + doxygen**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug && doxygen Doxyfile`
预期：ctest 全绿（20/20 起）；doxygen 零警告。

- [ ] **步骤 12：Commit**

```bash
git add scripts/gen_unicode_width.py include/ZzTerm/detail/UnicodeWidthData.inc \
        include/ZzTerm/UnicodeWidth.h src/backend/native/ZzNativeBackend.h \
        src/backend/native/ZzNativeBackend.cpp tests/unit/test_unicode_width.cpp \
        tests/unit/test_native_widechar.cpp tests/unit/test_backend_compat.cpp
git commit -m "feat(native): EAW 真实宽度表 + putChar 宽字符流（双格/行尾换行/半格覆写清理）"
```

---

### 任务 2：备用屏幕（?1049/?1047/?1048）

**文件：**
- 修改：`src/backend/native/ZzNativeBackend.h`（新方法/成员声明）
- 修改：`src/backend/native/NativeCsiDispatch.cpp`（DEC 私有分发入口 + 1049/1047/1048 + 文件头注释）
- 测试：`tests/unit/test_native_altscreen.cpp`（新）、`tests/unit/test_backend_compat.cpp:120-151`（用例 6 恢复强对照）

- [ ] **步骤 1：写备用屏幕测试 `tests/unit/test_native_altscreen.cpp`（先红）**

```cpp
// native 备用屏幕（DECSET 1049/1047/1048，M2）。仅公开 API。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

void feed(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                      bytes.size()));
}

ZzCellView cell(const ZzTerminal& t, int row, int col)
{
    return t.renderView().lineAt(row).cellAt(col);
}

// 1049h：保存光标 + 切 alt + 清 alt；1049l：回主屏 + 恢复内容与光标。
void test1049RoundTrip()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "MAIN\x1b[?1049h");
    ZZ_CHECK(t.isAlternateScreen());
    ZZ_CHECK(cell(t, 0, 0).text.empty());        // alt 已清屏
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 0 }));
    feed(t, "ALT");
    ZZ_CHECK(cell(t, 0, 0).text == "A");
    feed(t, "\x1b[?1049l");
    ZZ_CHECK(!t.isAlternateScreen());
    ZZ_CHECK(cell(t, 0, 0).text == "M");         // 主屏恢复
    ZZ_CHECK(cell(t, 0, 3).text == "N");
    ZZ_CHECK(cell(t, 0, 4).text.empty());        // alt 的 ALT 不污染主屏
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 4 })); // 光标恢复
}

// 幂等：重复 1049h 不炸不叠加；主屏上 1049l 为空操作。
void test1049Idempotent()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "AB\x1b[?1049h\x1b[?1049h");
    ZZ_CHECK(t.isAlternateScreen());
    feed(t, "\x1b[?1049l\x1b[?1049l");
    ZZ_CHECK(!t.isAlternateScreen());
    ZZ_CHECK(cell(t, 0, 0).text == "A");
}

// 1047h/l：切 buffer + 进 alt 清屏；不保存/恢复光标（主屏光标原位保留）。
void test1047NoCursorSave()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "AB\x1b[?1047h");
    ZZ_CHECK(t.isAlternateScreen());
    feed(t, "XY\x1b[?1047l");
    ZZ_CHECK(!t.isAlternateScreen());
    ZZ_CHECK(cell(t, 0, 0).text == "A");
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 2 })); // 主屏光标原位
}

// 1048h/l：仅保存/恢复光标，不切 buffer。
void test1048CursorOnly()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "AB\x1b[?1048h\x1b[3;5H\x1b[?1048l");
    ZZ_CHECK(!t.isAlternateScreen());
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 2 }));
}

// alt 滚动永不进历史（ZzScreen 既有语义，经 1049 路径回归验证）。
void testAltScrollNoHistory()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 1000);
    feed(t, "\x1b[?1049h");
    std::size_t scrolled = 0;
    for (int i = 0; i < 20; ++i) {
        const std::string line = "L" + std::to_string(i) + "\r\n";
        const ZzTermChanges ch = t.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(line.data()), line.size()));
        scrolled += ch.scrolledOutLines;
    }
    ZZ_CHECK(scrolled == 0);
}

// 滚动区随 buffer 切换保存/恢复（间接验证，公开 API 无滚动区查询）：
// 主屏 6 行写满，设滚动区 [3,5]（1 起始，即 0 起始 2-4 行），1049 往返后，
// 光标到区底喂换行 → 区内滚动（行 0/1/5 不动，行 2 原内容被顶走）。
// 若滚动区未恢复（全屏），行 0 会被顶走——据此区分。
void testScrollRegionRestored()
{
    ZzTerminal t(10, 6, ZzBackendKind::Native, 0);
    feed(t, "AAAAAAAA\r\nBBBBBBBB\r\nCCCCCCCC\r\nDDDDDDDD\r\nEEEEEEEE\r\nFFFFFFFF");
    feed(t, "\x1b[3;5r");       // 滚动区行 2-4（0 起始）
    feed(t, "\x1b[?1049h\x1b[?1049l");
    feed(t, "\x1b[5;1H");       // 光标到区底行 4（1 起始第 5 行）
    feed(t, "\n");
    ZZ_CHECK(cell(t, 0, 0).text == "A"); // 区外行 0 不动
    ZZ_CHECK(cell(t, 1, 0).text == "B"); // 区外行 1 不动
    ZZ_CHECK(cell(t, 2, 0).text == "D"); // 区内上滚：原行 3 内容上移
    ZZ_CHECK(cell(t, 5, 0).text == "F"); // 区外行 5 不动
}

} // namespace

int main()
{
    test1049RoundTrip();
    test1049Idempotent();
    test1047NoCursorSave();
    test1048CursorOnly();
    testAltScrollNoHistory();
    testScrollRegionRestored();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_altscreen: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：跑测试确认红**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_altscreen --output-on-failure`
预期：FAIL（DEC 私有 CSI 仍被整体忽略，isAlternateScreen 恒 false）。

- [ ] **步骤 3：实现 DEC 私有分发与备用屏幕**

`src/backend/native/ZzNativeBackend.h`：

语义方法区新增声明：

```cpp
    void dispatchDecPrivate(const ZzParamSequence& seq); // DEC 私有 CSI（? 前缀）
    void switchToAlternate(bool saveCursor); // 1049h/1047h 进入备用屏幕
    void switchToPrimary(bool restoreCursor); // 1049l/1047l 退回主屏幕
```

成员区（`scrolledOutPending_` 附近）新增：

```cpp
    int  savedScrollTop_ = 0;    ///< 切 Alternate 时保存的主屏滚动区上沿（0 起始）。
    int  savedScrollBottom_ = 0; ///< 切 Alternate 时保存的主屏滚动区下沿（0 起始，含）。
    bool hasSavedScrollRegion_ = false; ///< 是否有待恢复的滚动区。
```

`src/backend/native/NativeCsiDispatch.cpp`：

文件头注释（5-10 行）更新为：

```cpp
// CSI 语义（ZzNativeBackend 方法，分文件实现以控制单文件规模）。
// 约定：参数省略（kOmitted）或 <= 0 一律回退默认值；数值钳到网格范围。
// DEC 私有序列（privateMarker == '?'）走 dispatchDecPrivate：M2 已交付
// 备用屏幕（1049/1047/1048）、DECAWM（?7）、DECTCEM（?25）；其余 DEC
// 私有模式（mouse/bracketed paste 等）与 intermediate 序列安全忽略，属
// M3（里程碑划分见 Architecture.md 第 19 节）。
```

`dispatchCsi` 开头的早退（27-28 行）替换为：

```cpp
    if (seq.privateMarker == '?') {
        if (seq.intermediates.empty())
            dispatchDecPrivate(seq);
        return; // 带 intermediate 的 DEC 私有：安全忽略（M3）
    }
    if (seq.privateMarker != 0 || !seq.intermediates.empty())
        return; // 其余私有 / intermediate 序列：安全忽略（M3，见文件头注释）
```

文件末尾新增：

```cpp
void ZzNativeBackend::dispatchDecPrivate(const ZzParamSequence& seq)
{
    const bool set = (seq.final == 'h');
    if (!set && seq.final != 'l')
        return; // DEC 私有非 h/l final：安全忽略
    // CSI ? Pm h/l 可携带多个模式参数，逐个应用（如 ESC[?1049;25h）。
    for (const std::int32_t p : seq.params) {
        if (p == ZzParamSequence::kOmitted || p <= 0)
            continue;
        switch (static_cast<int>(p)) {
        case 1047: // 使用备用屏幕（进入清屏），不动光标保存
            if (set)
                switchToAlternate(false);
            else
                switchToPrimary(false);
            break;
        case 1048: // 仅保存/恢复光标
            if (set)
                screen_.saveCursor();
            else
                screen_.restoreCursor();
            break;
        case 1049: // = 1048（保存光标）+ 1047（切 alt 清屏）；退出恢复光标
            if (set)
                switchToAlternate(true);
            else
                switchToPrimary(true);
            break;
        default:
            continue; // 其余 DEC 私有模式：M3，安全忽略（不标脏）
        }
        noteScreenDirty();
    }
}

void ZzNativeBackend::switchToAlternate(bool saveCur)
{
    if (saveCur)
        screen_.saveCursor();
    if (screen_.activeBuffer() != ZzScreenBuffer::Alternate) {
        // 保存主屏滚动区；alt 期间复位为全屏（xterm 语义），回主屏时恢复。
        const ZzCellRange r = screen_.scrollRegionRows();
        savedScrollTop_ = r.startCol;
        savedScrollBottom_ = r.endCol - 1;
        hasSavedScrollRegion_ = true;
        screen_.setActiveBuffer(ZzScreenBuffer::Alternate);
        screen_.resetScrollRegion();
    }
    // xterm：1049h/1047h 进入 alt 均清全屏；alt 光标为该 buffer 自存位置
    // （初次进入即原点），eraseInDisplay 不动光标。
    screen_.eraseInDisplay(ZzEraseMode::All, eraseFill());
    noteScreenDirty();
}

void ZzNativeBackend::switchToPrimary(bool restoreCur)
{
    if (screen_.activeBuffer() == ZzScreenBuffer::Primary)
        return; // 幂等
    screen_.setActiveBuffer(ZzScreenBuffer::Primary);
    if (hasSavedScrollRegion_) {
        screen_.setScrollRegion(savedScrollTop_, savedScrollBottom_);
        hasSavedScrollRegion_ = false;
    }
    if (restoreCur)
        screen_.restoreCursor();
    noteScreenDirty();
}
```

- [ ] **步骤 4：跑测试确认绿**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_altscreen --output-on-failure`
预期：全部 PASS。

- [ ] **步骤 5：compat 用例 6 恢复逐格强对照**

`tests/unit/test_backend_compat.cpp:120-151` 的 `testAltScreen` 整体替换为：

```cpp
// 6. Alternate Screen（M2：两后端均实现 1049，恢复逐格强对照）。
void testAltScreen()
{
    Dual d;
    d.feedBoth("MAIN\x1b[?1049h");
    ZZ_CHECK(d.native.isAlternateScreen());
    ZZ_CHECK(d.contour.isAlternateScreen());
    d.feedBoth("ALT");
    checkRowEqual(d.native, d.contour, 0, 3, "alt-write");
    d.feedBoth("\x1b[?1049l");
    ZZ_CHECK(!d.native.isAlternateScreen());
    ZZ_CHECK(!d.contour.isAlternateScreen());
    checkRowEqual(d.native, d.contour, 0, 4, "alt-restore");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
}
```

若光标恢复时机与 Contour 不一致（xterm 细节分歧），按规格 5 风险节处理：以 xterm 实际行为为准；Contour 确属上游语义不同则该断言回退 b 类分别断言并注释钉住，不强行对齐。

- [ ] **步骤 6：全量测试 + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿。

```bash
git add src/backend/native/ZzNativeBackend.h src/backend/native/NativeCsiDispatch.cpp \
        tests/unit/test_native_altscreen.cpp tests/unit/test_backend_compat.cpp
git commit -m "feat(native): DEC 私有 1049/1047/1048 备用屏幕（光标与滚动区保存恢复）"
```

---

### 任务 3：DECAWM ?7 + DECTCEM ?25

**文件：**
- 修改：`src/backend/native/NativeCsiDispatch.cpp`（dispatchDecPrivate 加 case 7/25）
- 测试：`tests/unit/test_native_dec_modes.cpp`（新）、`tests/unit/test_backend_compat.cpp:210-222`（用例 10 恢复 + 新用例 11）

- [ ] **步骤 1：写模式测试 `tests/unit/test_native_dec_modes.cpp`（先红）**

```cpp
// native DECAWM(?7) 与 DECTCEM(?25)（M2）。仅公开 API。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

void feed(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                      bytes.size()));
}

ZzCellView cell(const ZzTerminal& t, int row, int col)
{
    return t.renderView().lineAt(row).cellAt(col);
}

// DECTCEM：?25l 隐藏、?25h 恢复；默认可见。
void testCursorVisibility()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    ZZ_CHECK(t.cursor().visible);
    feed(t, "AB\x1b[?25l");
    ZZ_CHECK(!t.cursor().visible);
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 2 })); // 可见性不影响位置
    feed(t, "\x1b[?25h");
    ZZ_CHECK(t.cursor().visible);
}

// DECAWM 关闭：行尾覆写不换行；重新开启后恢复换行。
void testAutoWrapToggle()
{
    ZzTerminal t(4, 4, ZzBackendKind::Native, 0);
    feed(t, "\x1b[?7l");
    feed(t, "ABCDE"); // ABCD 填满，E 覆写最后一格
    ZZ_CHECK(cell(t, 0, 0).text == "A");
    ZZ_CHECK(cell(t, 0, 3).text == "E"); // D 被覆写
    ZZ_CHECK(cell(t, 1, 0).text.empty()); // 未换行
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 3 }));
    feed(t, "\x1b[?7h");
    feed(t, "F"); // 换行落新行行首
    ZZ_CHECK(cell(t, 1, 0).text == "F");
    ZZ_CHECK(t.cursor().position == (ZzPosition { 1, 1 }));
}

// 复合序列：ESC[?25;7l 一次关两个模式。
void testCombinedModes()
{
    ZzTerminal t(4, 4, ZzBackendKind::Native, 0);
    feed(t, "\x1b[?25;7l");
    ZZ_CHECK(!t.cursor().visible);
    feed(t, "ABCDE");
    ZZ_CHECK(cell(t, 1, 0).text.empty()); // DECAWM 也已关
}

} // namespace

int main()
{
    testCursorVisibility();
    testAutoWrapToggle();
    testCombinedModes();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_dec_modes: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：跑测试确认红**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_dec_modes --output-on-failure`
预期：FAIL（case 7/25 未接线：visible 恒 true、?7l 无效果）。

- [ ] **步骤 3：接线 case 7/25**

`src/backend/native/NativeCsiDispatch.cpp` 的 `dispatchDecPrivate` switch 中 `case 1047` 之前插入：

```cpp
        case 7: // DECAWM 自动换行（默认开）
            screen_.setAutoWrapMode(set);
            break;
        case 25: { // DECTCEM 光标可见性（形状/闪烁位不动）
            const ZzCursorState cur = screen_.cursor();
            screen_.setCursorStyle(cur.shape, set, cur.blinking);
            break;
        }
```

- [ ] **步骤 4：跑测试确认绿**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_native_dec_modes --output-on-failure`
预期：全部 PASS。

- [ ] **步骤 5：compat 用例 10 恢复强对照 + 新增用例 11**

`tests/unit/test_backend_compat.cpp:210-222` 的 `testCursorVisibility` 整体替换为：

```cpp
// 10. 光标可见性（DECTCEM ?25l/h；M2：两后端均上报真实值，恢复强对照）。
void testCursorVisibility()
{
    Dual d;
    d.feedBoth("AB\x1b[?25l");
    ZZ_CHECK(!d.native.cursor().visible);
    ZZ_CHECK(!d.contour.cursor().visible);
    d.feedBoth("\x1b[?25h");
    ZZ_CHECK(d.native.cursor().visible);
    ZZ_CHECK(d.contour.cursor().visible);
}

// 11. DECAWM ?7l：右边界覆写不换行（M2 新增强对照）。
void testAutoWrapMode()
{
    Dual d; // 80x24
    d.feedBoth("\x1b[?7l");
    std::string seq(80, 'X');
    seq += "YZ"; // 前 80 填满行 0；Y/Z 依次覆写最后一格
    d.feedBoth(seq);
    checkRowEqual(d.native, d.contour, 0, 80, "decawm-off");
    ZZ_CHECK(d.native.cursor().position == d.contour.cursor().position);
    ZZ_CHECK(d.native.renderView().lineAt(1).cellAt(0).text.empty());
    ZZ_CHECK(d.contour.renderView().lineAt(1).cellAt(0).text.empty());
}
```

`main()` 中 `testCursorVisibility();` 之后追加 `testAutoWrapMode();`。

- [ ] **步骤 6：全量测试 + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿。

```bash
git add src/backend/native/NativeCsiDispatch.cpp tests/unit/test_native_dec_modes.cpp \
        tests/unit/test_backend_compat.cpp
git commit -m "feat(native): DECAWM ?7 与 DECTCEM ?25 接线，compat 恢复强对照"
```

---

### 任务 4：Ambiguous 宽度配置口

**文件：**
- 修改：`include/ZzTerm/Terminal.h`（facade 方法声明）
- 修改：`src/terminal/Terminal.cpp`（facade 实现委托）
- 修改：`src/backend/ZzTerminalBackend.h`（新纯虚）
- 修改：`src/backend/native/ZzNativeBackend.h` / `ZzNativeBackend.cpp`（存储标志 + putChar 传参）
- 修改：`src/backend/contour/ZzContourBackendAdapter.h` / `.cpp`（空操作 + 钉住注释）
- 修改：`docs/superpowers/specs/2026-09-20-m2-eaw-width-dec-private-modes-design.md`（4.1 节"C 接口与 C++ 方法各一"表述修正为 facade C++ 成员方法）
- 测试：`tests/unit/test_ambiguous_width.cpp`（新）

- [ ] **步骤 1：写配置口测试 `tests/unit/test_ambiguous_width.cpp`（先红）**

```cpp
// Ambiguous 宽度配置口（M2）：默认窄（xterm 兼容），可切 CJK 宽。仅公开 API。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string_view>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

void feed(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                      bytes.size()));
}

// 默认：Ambiguous 码位按窄（1 列）。
void testDefaultNarrow()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    feed(t, "\xC2\xB7"); // U+00B7 ·
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(0).width == ZzCellWidth::Narrow);
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 1 }));
}

// 开启宽模式：Ambiguous 码位按 2 列（双格写入）。
void testAmbiguousWide()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    t.setAmbiguousWidthMode(true);
    feed(t, "\xC2\xB7" "A"); // "·A"
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(1).width == ZzCellWidth::WideContinuation);
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(2).text == "A");
    ZZ_CHECK(t.cursor().position == (ZzPosition { 0, 3 }));
}

// 配置不影响 W 类（始终宽）与 Na 类（始终窄）。
void testOtherClassesUnaffected()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    t.setAmbiguousWidthMode(true);
    feed(t, "\xE4\xB8\xAD" "B"); // "中B"
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(0).width == ZzCellWidth::WideLead);
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(2).text == "B");
    ZZ_CHECK(t.renderView().lineAt(0).cellAt(2).width == ZzCellWidth::Narrow);
}

} // namespace

int main()
{
    testDefaultNarrow();
    testAmbiguousWide();
    testOtherClassesUnaffected();
    if (g_failures != 0)
        std::fprintf(stderr, "test_ambiguous_width: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：跑测试确认红**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_ambiguous_width --output-on-failure`
预期：编译失败（`setAmbiguousWidthMode` 未定义）。

- [ ] **步骤 3：实现配置口（facade → backend → native；Contour 空操作）**

`src/backend/ZzTerminalBackend.h` 接口尾部（`setOutputHandler` 之后）新增：

```cpp
    /// Ambiguous 宽度模式（true=CJK 按 2 列）；Contour 无对应配置，空操作。
    virtual void setAmbiguousWidthMode(bool wide) noexcept = 0;
```

`include/ZzTerm/Terminal.h` 公开方法区（`setOutputHandler` 声明之后）新增：

```cpp
    /**
     * @brief 设置 Ambiguous 宽度模式（UAX #11 A 类别码位列宽）。
     * @param wide true 按 2 列（CJK 环境）；false 按 1 列（xterm 默认，构造初值）。
     * @note 仅 native 后端生效；Contour 后端无对应配置项，调用为空操作
     *       （适配层注释钉住的已知分歧）。设置对其后的 feed 生效，
     *       已落格内容不 retroactive 重排。
     */
    void setAmbiguousWidthMode(bool wide) noexcept;
```

`src/terminal/Terminal.cpp` 实现：

```cpp
void ZzTerminal::setAmbiguousWidthMode(bool wide) noexcept
{
    backend_->setAmbiguousWidthMode(wide);
}
```

`src/backend/native/ZzNativeBackend.h`：接口实现声明区加 `void setAmbiguousWidthMode(bool wide) noexcept override;`；成员区加 `bool ambiguousWide_ = false; ///< Ambiguous 按 2 列（CJK 模式）。`

`src/backend/native/ZzNativeBackend.cpp`：

```cpp
void ZzNativeBackend::setAmbiguousWidthMode(bool wide) noexcept
{
    ambiguousWide_ = wide;
}
```

`putChar` 中 `const int width = zzCellWidthOf(cp);` 改为：

```cpp
    const int width = zzCellWidthOf(cp, ambiguousWide_);
```

`src/backend/contour/ZzContourBackendAdapter.h` / `.cpp`：

```cpp
void ZzContourBackendAdapter::setAmbiguousWidthMode(bool /*wide*/) noexcept
{
    // Contour 未暴露 ambiguous 宽度配置：空操作（已知分歧，规格 4.1 钉住；
    // compat 测试不含 Ambiguous 维度对照）。
}
```

（头文件类内加 override 声明；若 adapter 的接口实现为类内联义，则直接类内给出上述实现。）

- [ ] **步骤 4：跑测试确认绿 + OFF 构建编译验证**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_ambiguous_width --output-on-failure`
预期：PASS。
再验证 OFF：`cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure`
预期：全绿（新增纯虚在 OFF 路径仅 native 实现，链接完整）。

- [ ] **步骤 5：修正规格 4.1 表述**

`docs/superpowers/specs/2026-09-20-m2-eaw-width-dec-private-modes-design.md` 4.1 节中"facade 新增 `zzSetAmbiguousWidthMode(ZzTerminal&, bool wide)`（C 接口与 C++ 方法各一，沿用现有 facade 双形态惯例）"改为：

```markdown
- Ambiguous 配置口：facade 新增 C++ 成员方法 `ZzTerminal::setAmbiguousWidthMode(bool wide)`（facade 为纯 C++ 类，无 C 接口层），经 ZzTerminalBackend 新纯虚下发；native 存储标志并传入查表；Contour 无对应配置项，适配层空操作并在注释钉住为已知分歧，compat 不含 Ambiguous 维度对照；
```

- [ ] **步骤 6：全量测试 + doxygen + Commit**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug && doxygen Doxyfile`
预期：全绿、零警告。

```bash
git add include/ZzTerm/Terminal.h src/terminal/Terminal.cpp src/backend/ZzTerminalBackend.h \
        src/backend/native/ZzNativeBackend.h src/backend/native/ZzNativeBackend.cpp \
        src/backend/contour/ZzContourBackendAdapter.h src/backend/contour/ZzContourBackendAdapter.cpp \
        tests/unit/test_ambiguous_width.cpp \
        docs/superpowers/specs/2026-09-20-m2-eaw-width-dec-private-modes-design.md
git commit -m "feat(api): Ambiguous 宽度配置口（native 生效，Contour 空操作钉住）"
```

---

### 任务 5：收尾验收 + 文档同步

**文件：**
- 修改：`docs/API.md`（新公开 API 与行为同步）
- 修改：`src/backend/native/NativeCsiDispatch.cpp` 文件头注释（任务 2 步骤 3 已更新，此处复核）
- 修改：`include/ZzTerm/UnicodeWidth.h`（任务 1 已重写，此处复核 doxygen）
- 修改：`tests/interactive/verify_smoke.py`（冒烟样例追加 1049 与 CJK 序列断言，若结构允许）

- [ ] **步骤 1：docs/API.md 同步**

在 API.md 适当章节（Terminal facade 方法列表 / 渲染视图行为说明）补充：

- `ZzTerminal::setAmbiguousWidthMode(bool)`：语义、默认值、native-only 生效、Contour 空操作分歧；
- `zzCellWidthOf(cp, ambiguousWide)` 新签名与数据来源（Unicode 16.0.0 钉版，生成脚本 scripts/gen_unicode_width.py）；
- native 已支持 DEC 私有 1049/1047/1048/?7/?25，`isAlternateScreen()` 与 `cursor().visible` 对 native 也上报真实值；
- 宽字符落格语义：双格（WideLead/WideContinuation）、行尾放不下即换行、半格覆写清理。

注意 doxygen markdown 陷阱（行内 code 禁尖括号/禁以点开头/后禁紧跟顿号/禁 `#` 预处理词）。

- [ ] **步骤 2：冒烟脚本追加样例（如结构允许）**

读 `tests/interactive/verify_smoke.py` 现有 pyte 断言结构，向双后端冒烟追加：

- 1049 进出：主屏写标记、1049h、alt 写标记、1049l、主屏标记恢复；
- CJK 行：中文与 ASCII 混排的格位断言。

若脚本结构与单元测试重复度高（compat 已覆盖），本步骤改为在脚本注释中指向 compat 用例，不重复实现。

- [ ] **步骤 3：全量四项验收**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake -S . -B build/m2-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check --output-on-failure
cmake -S . -B build/m2-shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check --output-on-failure
doxygen Doxyfile
```

预期：ON 全绿（19 + 新增约 5 个测试）；OFF 全绿；shared 全绿；doxygen 零警告。
（OFF/shared 构建目录沿用 M1b 惯例，验完可留可删。）

- [ ] **步骤 4：demo 实测**

运行 examples/ZzTermSmoke 双后端（`--backend=native` 与 `--backend=contour`），在真实 shell 中验证：

- `less <file>` / `vim` 进出：备用屏切换、退出后 shell 原内容恢复；
- CJK 文本（如 `echo 中文混排ABC`）：对齐无错位、光标位置正确；
- `printf 'ESC[?25l'` / `printf 'ESC[?25h'`：光标隐藏/恢复。

实测异常先对照 compat 用例定位后端归属，再按规格 5 风险节处理。

- [ ] **步骤 5：Commit**

```bash
git add docs/API.md tests/interactive/verify_smoke.py
git commit -m "docs(api): M2 同步——Ambiguous 配置口、EAW 宽度表、DEC 私有模式行为说明"
```

---

## 自检记录

- **规格覆盖度：** 规格 4.1（EAW 表）→ 任务 1 步骤 1-5；规格 4.2（putChar 宽字符流）→ 任务 1 步骤 6-9；规格 4.3（DEC 私有接线）→ 任务 2（1049/1047/1048）+ 任务 3（?7/?25）；规格 4.1 Ambiguous 配置口 → 任务 4；规格 4.4 测试（表完整性/宽字符/DEC 模式/compat 恢复/demo）→ 任务 1-4 各自测试步骤 + 任务 5 步骤 2/4；规格 4.5 错误处理 → 任务 1 步骤 1（脚本）+ zzCellWidthOf 越界防御；规格 4.6 验收 → 任务 5 步骤 3。规格 2.2 排除项（grapheme/鼠标粘贴/Reflow/Contour 上游）在计划注释中保持钉住。
- **类型一致性：** `ZzEawInterval`/`ZzEawClass`/`kZzEawIntervals`（任务 1 定义，任务 1/4 使用）；`zzCellWidthOf(char32_t, bool)`（任务 1 定义，任务 1 putChar 用默认参、任务 4 传 `ambiguousWide_`）；`clearWidePairAt`（任务 1 声明+实现+使用）；`dispatchDecPrivate`/`switchToAlternate`/`switchToPrimary`/`savedScrollTop_`/`savedScrollBottom_`/`hasSavedScrollRegion_`（任务 2 定义并使用，任务 3 复用 dispatchDecPrivate 的 switch）；`setAmbiguousWidthMode`（任务 4 全链路一致）；测试辅助 `feed`/`cell` 局部于各测试文件，互不构成依赖。
- **占位符扫描：** 无待定/TODO；任务 5 步骤 2 的两可分支（追加样例或指向 compat）给了明确判断标准与默认动作。
