# M8 百万行实验 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为 native 后端 ZzScrollback 建立 10k/100k/1M 可复测 benchmark 矩阵（单元轨 + facade 轨双 harness），产出基线数据与门控判定。

**架构：** `tests/bench/` 下两个独立可执行（zz_bench_scrollback 单元轨直接驱动 ZzScrollback；zz_bench_feed facade 轨经 ZzTerminal+native 后端 feed 灌流），共享 bench_common（RSS 采样 / 计时 / 确定性负载生成）。10k 短跑档注册默认 ctest；100k/1M 长跑档经 `bench-long` 自定义构建目标手动触发。门控不在 harness 内判决——harness 只测量落盘，T4 对照规格门控人工判定。

**技术栈：** C++20、CMake（既有 tests GLOB 惯例）、ctest、Linux /proc + getrusage RSS 采样。

**规格：** docs/superpowers/specs/2026-09-22-m8-million-line-design.md（c601b87）

**执行边界：**
- T5（优化波）不在本计划内——T4 门控判定为决策点，全达标则里程碑收尾；有不达标项则由主代理归因回报用户，确认后另立优化计划。
- 规格偏差一处（T1 步骤 6 同步修订规格）：规格 §2 写"长跑档打 bench-long label 手动 ctest -L bench-long 触发"，实施改为 `bench-long` 自定义构建目标——ctest 无单测试默认排除机制，label 方案需改动既有 `ctest --preset` 基线命令，自定义目标对 47/47 基线零侵扰。
- 测量波零触碰 src/ 与既有测试断言；仅新增 tests/bench/ 与 tests/CMakeLists.txt 尾部块、T4 新增 probe 文档与 tests/perf/records/ JSON 物证。
- 本计划结束后基线测试数 47 → 50（+zz_bench_scrollback、zz_bench_feed_ascii、zz_bench_feed_mixed）。

**公共命令（每任务收尾必跑）：**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
# T3 收尾加跑 OFF 配置：
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
```

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `tests/bench/bench_common.h`（新建） | 共享件接口：档位解析、RSS 采样声明、计时、负载生成器、校验和 |
| `tests/bench/bench_common.cpp`（新建） | 共享件实现（Linux RSS；非 Linux 返回 0 留 M9） |
| `tests/bench/zz_bench_scrollback.cpp`（新建） | 单元轨 harness：append 吞吐 / lineAt 顺序·随机 / reflow / RSS，双画像 |
| `tests/bench/zz_bench_feed.cpp`（新建） | facade 轨 harness：feed 吞吐 / search / reflow / RSS，`--profile` 双画像 |
| `tests/CMakeLists.txt`（修改，尾部追加块） | bench 收编：3 个短跑 ctest 注册 + bench-long 自定义目标 |
| `docs/superpowers/specs/2026-09-22-m8-million-line-probe.md`（T4 新建） | 全矩阵基线数据 + 门控逐项判定（M7b parity-probe 先例） |
| `tests/perf/records/2026-09-22-m8-*.json`（T4 新建） | 长跑 JSON 物证入库（M4/M6 records 先例） |

---

## 任务 1：bench 共享件与 CMake 收编骨架

**文件：**
- 创建：`tests/bench/bench_common.h`
- 创建：`tests/bench/bench_common.cpp`
- 创建：`tests/bench/zz_bench_scrollback.cpp`（本任务仅自检骨架，T2 扩为全矩阵）
- 修改：`tests/CMakeLists.txt`（尾部追加 M8 块）
- 修改：`docs/superpowers/specs/2026-09-22-m8-million-line-design.md`（§2 长跑档触发方式一行修订）

- [ ] **步骤 1：编写 bench_common.h**

创建 `tests/bench/bench_common.h`：

```cpp
// M8 百万行实验：benchmark 共享件（RSS 采样 / 计时 / 确定性负载生成）。
// 仅 tests/bench 内部使用，非公开 API，不进 doxygen。
#pragma once

#include <ZzTerm/Line.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// 负载画像：Ascii 纯窄格短行；Mixed 每 3 行一条 wrapped 链 + 约三成 CJK 宽格。
enum class ZzBenchProfile { Ascii, Mixed };

/// 档位：10k（短跑，默认 ctest）/ 100k / 1m（长跑，bench-long 目标手动触发）。
struct ZzBenchTier {
    std::size_t lines; ///< 负载行数。
    const char*  name; ///< 档位名（"10k"/"100k"/"1m"），用于输出与 JSON 文件名。
};

/// 解析 --tier=10k|100k|1m，无参默认 10k；未知参数打印用法并 exit(2)。
ZzBenchTier zzBenchTierFromArgs(int argc, char** argv);

/// 当前 RSS（字节）。Linux 经 /proc/self/status 的 VmRSS；解析失败或非 Linux 返回 0
///（macOS 采样适配属 M9，接口形态已预留）。
std::size_t zzBenchRssCurrentBytes();

/// 峰值 RSS（字节）。Linux 经 getrusage(RUSAGE_SELF) 的 ru_maxrss；非 Linux 返回 0。
std::size_t zzBenchRssPeakBytes();

/// t0 至今的毫秒数。
double zzBenchMillisSince(std::chrono::steady_clock::time_point t0);

/// 生成 count 行负载；seed 固定则输出确定（自检与复现依赖此性质）。
std::vector<ZzLine> zzBenchMakeLines(std::uint64_t seed, std::size_t count, int cols,
                                     ZzBenchProfile profile);

/// 生成 lineCount 行的合成 VT 字节流（行末 \r\n；每行恰含一次 ASCII 标记 "payload"
/// 供搜索断言；Mixed 画像掺 CJK UTF-8 长行，超 80 列自然形成 soft wrap 链）。
/// 内容确定性由构造保证（同行号绑定，无随机源）。
std::string zzBenchMakeVtStream(std::size_t lineCount, ZzBenchProfile profile);

/// 行批校验和（FNV-1a 64，生成器确定性自检用）。
std::uint64_t zzBenchLinesChecksum(const std::vector<ZzLine>& lines);

/// VT 流校验和（FNV-1a 64，自检用）。
std::uint64_t zzBenchStreamChecksum(const std::string& stream);
```

- [ ] **步骤 2：编写 bench_common.cpp**

创建 `tests/bench/bench_common.cpp`：

```cpp
#include "bench_common.h"

#include <ZzTerm/Cell.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#endif

ZzBenchTier zzBenchTierFromArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--tier=10k") == 0)
            return {10000, "10k"};
        if (std::strcmp(argv[i], "--tier=100k") == 0)
            return {100000, "100k"};
        if (std::strcmp(argv[i], "--tier=1m") == 0)
            return {1000000, "1m"};
        if (std::strncmp(argv[i], "--profile=", 10) == 0)
            continue; // 画像参数由调用方自行解析（zz_bench_feed）
        std::fprintf(stderr, "unknown arg: %s (expect --tier=10k|100k|1m)\n", argv[i]);
        std::exit(2);
    }
    return {10000, "10k"};
}

std::size_t zzBenchRssCurrentBytes()
{
#if defined(__linux__)
    std::ifstream in("/proc/self/status");
    std::string   key;
    while (in >> key) {
        if (key == "VmRSS:") {
            std::size_t kb = 0;
            in >> kb;
            return kb * 1024;
        }
        in.ignore(4096, '\n');
    }
    return 0; // 解析失败：按 0 处理（调用方仅记录，不断言非零）
#else
    return 0; // macOS 适配属 M9
#endif
}

std::size_t zzBenchRssPeakBytes()
{
#if defined(__linux__)
    struct rusage ru {};
    if (getrusage(RUSAGE_SELF, &ru) == 0)
        return static_cast<std::size_t>(ru.ru_maxrss) * 1024; // Linux ru_maxrss 单位 KB
    return 0;
#else
    return 0; // macOS 适配属 M9
#endif
}

double zzBenchMillisSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
        .count();
}

std::vector<ZzLine> zzBenchMakeLines(std::uint64_t seed, std::size_t count, int cols,
                                     ZzBenchProfile profile)
{
    std::vector<ZzLine> lines;
    lines.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        ZzLine line(cols);
        for (int c = 0; c < cols; ++c) {
            ZzCell cell;
            const std::uint64_t mix = seed + i + static_cast<std::size_t>(c);
            if (profile == ZzBenchProfile::Mixed && (c % 3) == 0) {
                cell.setWidth(ZzCellWidth::Wide);
                cell.setCodePoint(static_cast<char32_t>(0x4E00 + (mix % 64)));
            } else {
                cell.setWidth(ZzCellWidth::Narrow);
                cell.setCodePoint(static_cast<char32_t>(U'a' + (mix % 26)));
            }
            line.setCell(c, cell);
        }
        // Ascii 全部硬换行；Mixed 每 3 行一条 wrapped 链（链内前两条 wrapped=true）。
        line.setWrapped(profile == ZzBenchProfile::Mixed && (i % 3) != 2);
        lines.push_back(line);
    }
    return lines;
}

std::string zzBenchMakeVtStream(std::size_t lineCount, ZzBenchProfile profile)
{
    std::string stream;
    stream.reserve(lineCount * 48); // 估算行长，减少扩容
    for (std::size_t i = 0; i < lineCount; ++i) {
        stream += "line " + std::to_string(i) + " payload text for search";
        if (profile == ZzBenchProfile::Mixed) {
            // CJK UTF-8 长行尾巴：超 80 列由终端自然 soft wrap 成链。
            stream += " 中文负载一二三四五六七八九十甲乙丙丁戊己庚辛壬癸"
                      "中文负载一二三四五六七八九十甲乙丙丁戊己庚辛壬癸";
        }
        stream += "\r\n";
    }
    return stream;
}

std::uint64_t zzBenchLinesChecksum(const std::vector<ZzLine>& lines)
{
    std::uint64_t h = 1469598103934665603ull; // FNV-1a 64 offset basis
    auto          mix = [&h](std::uint64_t v) {
        for (int b = 0; b < 8; ++b) {
            h ^= (v >> (b * 8)) & 0xFF;
            h *= 1099511628211ull; // FNV prime
        }
    };
    for (const ZzLine& line : lines) {
        mix(static_cast<std::uint64_t>(line.cellCount()));
        mix(line.wrapped() ? 1u : 0u);
        for (int c = 0; c < line.cellCount(); ++c)
            mix(static_cast<std::uint64_t>(line.cellAt(c).codePoint()));
    }
    return h;
}

std::uint64_t zzBenchStreamChecksum(const std::string& stream)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const unsigned char ch : stream) {
        h ^= ch;
        h *= 1099511628211ull;
    }
    return h;
}
```

- [ ] **步骤 3：编写 zz_bench_scrollback.cpp 自检骨架**

创建 `tests/bench/zz_bench_scrollback.cpp`（T2 将在此文件上扩展全矩阵，本任务先保证自检可过）：

```cpp
// M8 单元轨 benchmark：直接驱动 ZzScrollback，10k/100k/1M 三档（本任务先立自检骨架，
// T2 扩展 append/lineAt/reflow/RSS 全矩阵）。默认无参跑 10k 短跑，注册默认 ctest；
// 100k/1M 长跑经 bench-long 目标手动触发。结果 JSON 落盘 cwd。
// 门控不在本程序内判决（T4 对照规格门控判定），本程序只测量、断言自洽、落盘。
#include <ZzTerm/Scrollback.h>

#include "bench_common.h"

#include <cstdio>
#include <fstream>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

int main(int argc, char** argv)
{
    const ZzBenchTier tier = zzBenchTierFromArgs(argc, argv);
    constexpr int     kCols = 80;

    // 负载生成器确定性自检：同种子同输出、异种子异输出、画像间异输出。
    {
        const auto a  = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Ascii);
        const auto b  = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Ascii);
        const auto c  = zzBenchMakeLines(43, 100, kCols, ZzBenchProfile::Ascii);
        const auto mx = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Mixed);
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) == zzBenchLinesChecksum(b));
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) != zzBenchLinesChecksum(c));
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) != zzBenchLinesChecksum(mx));
    }

    // T2 在此插入全矩阵测量（append/lineAt/reflow/RSS，Ascii+Mixed 双画像）。

    std::printf("zz_bench_scrollback[%s]: selfcheck done\n", tier.name);
    if (g_failures == 0)
        std::printf("zz_bench_scrollback: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 4：tests/CMakeLists.txt 尾部追加 M8 块**

在 `tests/CMakeLists.txt` 末尾（`if(ZZTERM_FUZZ)` 块之后）追加：

```cmake
# M8：百万行实验 benchmark（tests/bench/，单元轨 + facade 轨双 harness）。
# 短跑档（10k）注册默认 ctest；长跑档（100k/1M）经 bench-long 自定义目标手动触发
#（cmake --build <dir> --target bench-long），不进默认 ctest——ctest 无单测试默认
# 排除机制，label 方案需改动既有 ctest --preset 基线命令，故用构建目标。
# RSS 采样为 Linux 实现，非 Linux 返回 0（macOS 适配属 M9，届时解除 APPLE 排除）。
if(UNIX AND NOT APPLE)
    add_executable(zz_bench_scrollback bench/zz_bench_scrollback.cpp bench/bench_common.cpp)
    target_link_libraries(zz_bench_scrollback PRIVATE ZzTermCore)
    add_test(NAME zz_bench_scrollback COMMAND zz_bench_scrollback)
    set_tests_properties(zz_bench_scrollback PROPERTIES TIMEOUT 60)

    add_custom_target(bench-long
        COMMAND zz_bench_scrollback --tier=100k
        COMMAND zz_bench_scrollback --tier=1m
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "M8 长跑 benchmark（100k/1M，JSON 落盘 ${CMAKE_CURRENT_BINARY_DIR}）")
endif()
```

- [ ] **步骤 5：构建 + 跑测试验证骨架**

运行：

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：构建零告警；测试 48/48 通过（47 + zz_bench_scrollback）；`ctest --preset linux-gcc-debug -R zz_bench_scrollback --output-on-failure` 输出含 "selfcheck done" 与 "all passed"。

- [ ] **步骤 6：修订规格 §2 长跑档触发方式 + Commit**

`docs/superpowers/specs/2026-09-22-m8-million-line-design.md` §2 最后一个 bullet 中"100k/1M 长跑档打 bench-long label，手动 ctest -L bench-long 触发，不进默认跑"改为"100k/1M 长跑档经 bench-long 自定义构建目标手动触发（cmake --build --target bench-long），不进默认 ctest（ctest 无单测试默认排除机制，label 方案需改动既有 preset 基线命令，实施期改裁定）"。

```bash
git add tests/bench/ tests/CMakeLists.txt docs/superpowers/specs/2026-09-22-m8-million-line-design.md
git commit -m "test(bench): M8 bench 共享件与收编骨架（RSS 采样 + 确定性负载生成器 + bench-long 目标）"
```

---

## 任务 2：单元轨全矩阵（zz_bench_scrollback）

**文件：**
- 修改：`tests/bench/zz_bench_scrollback.cpp`（在 T1 骨架的自检之后插入全矩阵测量）

- [ ] **步骤 1：扩展为全矩阵测量**

将 `tests/bench/zz_bench_scrollback.cpp` 整体替换为：

```cpp
// M8 单元轨 benchmark：直接驱动 ZzScrollback，10k/100k/1M 三档 x Ascii/Mixed 双画像。
// 默认无参跑 10k 短跑（含生成器确定性自检），注册默认 ctest；
// 100k/1M 长跑经 bench-long 目标手动触发。结果 JSON 落盘 cwd。
// 门控不在本程序内判决（T4 对照规格门控判定），本程序只测量、断言自洽、落盘。
#include <ZzTerm/Scrollback.h>

#include "bench_common.h"

#include <cstdio>
#include <fstream>
#include <random>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

namespace {

struct ProfileResult {
    double      appendMs    = 0;
    double      lineAtSeqMs = 0;
    double      lineAtRndMs = 0;
    double      widenMs     = 0;
    double      narrowMs    = 0;
    std::size_t rssBytes    = 0;
    std::size_t approxBytes = 0;
};

ProfileResult runProfile(const ZzBenchTier& tier, ZzBenchProfile profile)
{
    constexpr int  kCols = 80;
    ProfileResult  r;
    constexpr std::size_t kBatch = 1000;

    auto sb = zzCreateChunkedScrollback(tier.lines);

    // 1. append 吞吐：kBatch 行一批，逐批生成并移交（生成耗时不计入——先停表式分段：
    //    生成与 append 交替会混入生成耗时，故每批分别计时仅累加 append 段）。
    auto             t0  = std::chrono::steady_clock::now();
    std::size_t      fed = 0;
    const std::size_t batches = (tier.lines + kBatch - 1) / kBatch;
    double           appendOnlyMs = 0;
    for (std::size_t b = 0; b < batches; ++b) {
        const std::size_t n = (fed + kBatch <= tier.lines) ? kBatch : (tier.lines - fed);
        auto              lines = zzBenchMakeLines(42 + b, n, kCols, profile);
        const auto        ta = std::chrono::steady_clock::now();
        sb->append(std::move(lines));
        appendOnlyMs += zzBenchMillisSince(ta);
        fed += n;
    }
    r.appendMs = appendOnlyMs;
    ZZ_TEST_EXPECT(sb->lineCount() == tier.lines);

    // 2. lineAt 顺序全扫 + 随机 10 万次
    t0 = std::chrono::steady_clock::now();
    std::size_t checksum = 0;
    for (std::size_t i = 0; i < tier.lines; ++i)
        checksum += static_cast<std::size_t>(sb->lineAt(i).cellCount());
    r.lineAtSeqMs = zzBenchMillisSince(t0);
    ZZ_TEST_EXPECT(checksum == tier.lines * static_cast<std::size_t>(kCols));

    std::mt19937                            rng(42);
    std::uniform_int_distribution<std::size_t> pick(0, tier.lines - 1);
    t0       = std::chrono::steady_clock::now();
    checksum = 0;
    for (int i = 0; i < 100000; ++i)
        checksum += static_cast<std::size_t>(sb->lineAt(pick(rng)).cellCount());
    r.lineAtRndMs = zzBenchMillisSince(t0);
    ZZ_TEST_EXPECT(checksum == 100000u * static_cast<std::size_t>(kCols));

    // 3. RSS（append 完成后、reflow 前）
    r.rssBytes    = zzBenchRssCurrentBytes();
    r.approxBytes = sb->stats().approxBytes;

    // 4. reflow：80 -> 120 -> 80
    t0 = std::chrono::steady_clock::now();
    sb->reflow(120);
    r.widenMs = zzBenchMillisSince(t0);
    t0 = std::chrono::steady_clock::now();
    sb->reflow(80);
    r.narrowMs = zzBenchMillisSince(t0);

    return r;
}

void writeJson(const ZzBenchTier& tier, const ProfileResult& ascii,
               const ProfileResult& mixed, std::size_t peakBytes)
{
    const std::string path = std::string("zzterm-bench-scrollback-") + tier.name + ".json";
    std::ofstream     js(path);
    js << "{\n"
       << "  \"milestone\": \"M8\",\n"
       << "  \"track\": \"unit-scrollback\",\n"
       << "  \"tier\": \"" << tier.name << "\",\n"
       << "  \"lines\": " << tier.lines << ",\n"
       << "  \"cols\": 80,\n"
       << "  \"ascii\": {\n"
       << "    \"append_ms\": " << ascii.appendMs << ",\n"
       << "    \"lineat_seq_ms\": " << ascii.lineAtSeqMs << ",\n"
       << "    \"lineat_rnd_ms\": " << ascii.lineAtRndMs << ",\n"
       << "    \"reflow_widen_ms\": " << ascii.widenMs << ",\n"
       << "    \"reflow_narrow_ms\": " << ascii.narrowMs << ",\n"
       << "    \"rss_bytes\": " << ascii.rssBytes << ",\n"
       << "    \"approx_bytes\": " << ascii.approxBytes << "\n"
       << "  },\n"
       << "  \"mixed\": {\n"
       << "    \"append_ms\": " << mixed.appendMs << ",\n"
       << "    \"lineat_seq_ms\": " << mixed.lineAtSeqMs << ",\n"
       << "    \"lineat_rnd_ms\": " << mixed.lineAtRndMs << ",\n"
       << "    \"reflow_widen_ms\": " << mixed.widenMs << ",\n"
       << "    \"reflow_narrow_ms\": " << mixed.narrowMs << ",\n"
       << "    \"rss_bytes\": " << mixed.rssBytes << ",\n"
       << "    \"approx_bytes\": " << mixed.approxBytes << "\n"
       << "  },\n"
       << "  \"peak_rss_bytes\": " << peakBytes << "\n"
       << "}\n";
}

} // namespace

int main(int argc, char** argv)
{
    const ZzBenchTier tier = zzBenchTierFromArgs(argc, argv);
    constexpr int     kCols = 80;

    // 负载生成器确定性自检：同种子同输出、异种子异输出、画像间异输出。
    {
        const auto a  = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Ascii);
        const auto b  = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Ascii);
        const auto c  = zzBenchMakeLines(43, 100, kCols, ZzBenchProfile::Ascii);
        const auto mx = zzBenchMakeLines(42, 100, kCols, ZzBenchProfile::Mixed);
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) == zzBenchLinesChecksum(b));
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) != zzBenchLinesChecksum(c));
        ZZ_TEST_EXPECT(zzBenchLinesChecksum(a) != zzBenchLinesChecksum(mx));
    }

    const ProfileResult ascii = runProfile(tier, ZzBenchProfile::Ascii);
    const ProfileResult mixed = runProfile(tier, ZzBenchProfile::Mixed);
    writeJson(tier, ascii, mixed, zzBenchRssPeakBytes());

    std::printf("zz_bench_scrollback[%s] ascii: append %.1fms (%.0f lines/s), lineAt seq %.1fms rnd %.1fms, reflow widen %.1fms narrow %.1fms, rss %zuMB\n",
                tier.name, ascii.appendMs,
                ascii.appendMs > 0 ? tier.lines / (ascii.appendMs / 1000.0) : 0.0,
                ascii.lineAtSeqMs, ascii.lineAtRndMs, ascii.widenMs, ascii.narrowMs,
                ascii.rssBytes / (1024 * 1024));
    std::printf("zz_bench_scrollback[%s] mixed: append %.1fms (%.0f lines/s), lineAt seq %.1fms rnd %.1fms, reflow widen %.1fms narrow %.1fms, rss %zuMB\n",
                tier.name, mixed.appendMs,
                mixed.appendMs > 0 ? tier.lines / (mixed.appendMs / 1000.0) : 0.0,
                mixed.lineAtSeqMs, mixed.lineAtRndMs, mixed.widenMs, mixed.narrowMs,
                mixed.rssBytes / (1024 * 1024));
    if (g_failures == 0)
        std::printf("zz_bench_scrollback: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 2：构建 + 短跑验证**

运行：

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R zz_bench_scrollback --output-on-failure
```

预期：PASS；stdout 两行画像数据非负；`build/linux-gcc-debug/tests/zzterm-bench-scrollback-10k.json` 生成且字段齐全。

- [ ] **步骤 3：100k 档手动冒烟（全矩阵含 1m 留 T4 统一跑）**

运行：

```bash
./build/linux-gcc-debug/tests/zz_bench_scrollback --tier=100k
```

预期：退出码 0；100k 档双画像数据打印，`zzterm-bench-scrollback-100k.json` 落盘当前目录（仓库根，验证后删除该临时 JSON 以免污染工作树）。本步只验证长跑档链路可跑；1m 全矩阵数据在 T4 正式产出。

- [ ] **步骤 4：全量测试 + Commit**

运行：

```bash
ctest --preset linux-gcc-debug
```

预期：48/48。

```bash
git add tests/bench/zz_bench_scrollback.cpp
git commit -m "test(bench): 单元轨 zz_bench_scrollback 全矩阵（append/lineAt/reflow/RSS 三档双画像）"
```

---

## 任务 3：facade 轨全矩阵（zz_bench_feed）

**文件：**
- 创建：`tests/bench/zz_bench_feed.cpp`
- 修改：`tests/CMakeLists.txt`（M8 块内追加 feed 可执行、两个短跑注册、bench-long 追加 feed 命令）

- [ ] **步骤 1：编写 zz_bench_feed.cpp**

创建 `tests/bench/zz_bench_feed.cpp`：

```cpp
// M8 facade 轨 benchmark：ZzTerminal + native 后端 feed 灌合成 VT 流，
// 10k/100k/1M 三档 x --profile=ascii|mixed 双画像（每进程一画像，RSS 隔离干净）。
// 默认无参 = 10k + ascii 短跑；ctest 注册 ascii/mixed 两个 10k 短跑；
// 100k/1M 长跑经 bench-long 目标手动触发。结果 JSON 落盘 cwd。
// 门控不在本程序内判决（T4 对照规格门控判定），本程序只测量、断言自洽、落盘。
#include <ZzTerm/Terminal.h>

#include "bench_common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <span>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

namespace {

ZzBenchProfile profileFromArgs(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--profile=ascii") == 0)
            return ZzBenchProfile::Ascii;
        if (std::strcmp(argv[i], "--profile=mixed") == 0)
            return ZzBenchProfile::Mixed;
        if (std::strncmp(argv[i], "--tier=", 7) == 0)
            continue; // tier 由 zzBenchTierFromArgs 解析
        std::fprintf(stderr, "unknown arg: %s (expect --profile=ascii|mixed --tier=10k|100k|1m)\n",
                     argv[i]);
        std::exit(2);
    }
    return ZzBenchProfile::Ascii;
}

const char* profileName(ZzBenchProfile profile)
{
    return profile == ZzBenchProfile::Ascii ? "ascii" : "mixed";
}

} // namespace

int main(int argc, char** argv)
{
    const ZzBenchProfile profile = profileFromArgs(argc, argv);
    const ZzBenchTier    tier    = zzBenchTierFromArgs(argc, argv);

    // VT 流生成自检：两调用逐字节一致；Ascii 与 Mixed 不同；行数不同则流不同。
    {
        const std::string a = zzBenchMakeVtStream(100, ZzBenchProfile::Ascii);
        const std::string b = zzBenchMakeVtStream(100, ZzBenchProfile::Ascii);
        const std::string m = zzBenchMakeVtStream(100, ZzBenchProfile::Mixed);
        ZZ_TEST_EXPECT(zzBenchStreamChecksum(a) == zzBenchStreamChecksum(b));
        ZZ_TEST_EXPECT(zzBenchStreamChecksum(a) != zzBenchStreamChecksum(m));
    }

    const std::string stream = zzBenchMakeVtStream(tier.lines, profile);

    // 容量留 24 行屏幕余量，保证全部负载行留存在历史区。
    ZzTerminal term(80, 24, ZzBackendKind::Native, tier.lines + 24);

    // 1. feed 吞吐（256KB 分块，test_perf_search 先例）
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t off = 0; off < stream.size();) {
        const std::size_t n = std::min<std::size_t>(256 * 1024, stream.size() - off);
        term.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(stream.data() + off), n));
        off += n;
    }
    const double feedMs = zzBenchMillisSince(t0);

    // 2. 灌满后的 RSS
    const std::size_t rssBytes = zzBenchRssCurrentBytes();

    // 3. 全量 search（每行恰一次 "payload"）
    const auto       ts      = std::chrono::steady_clock::now();
    const std::size_t matches = term.search("payload");
    const double     searchMs = zzBenchMillisSince(ts);
    ZZ_TEST_EXPECT(matches == tier.lines);

    // 4. reflow：80 -> 120 -> 80（facade resize 路径）
    const auto tr = std::chrono::steady_clock::now();
    ZZ_TEST_EXPECT(term.resize(120, 24));
    const double widenMs = zzBenchMillisSince(tr);
    const auto tr2 = std::chrono::steady_clock::now();
    ZZ_TEST_EXPECT(term.resize(80, 24));
    const double narrowMs = zzBenchMillisSince(tr2);

    const std::size_t peakBytes = zzBenchRssPeakBytes();

    // 5. JSON 落盘
    const std::string path = std::string("zzterm-bench-feed-") + profileName(profile) + "-"
                             + tier.name + ".json";
    std::ofstream js(path);
    js << "{\n"
       << "  \"milestone\": \"M8\",\n"
       << "  \"track\": \"facade-feed\",\n"
       << "  \"profile\": \"" << profileName(profile) << "\",\n"
       << "  \"tier\": \"" << tier.name << "\",\n"
       << "  \"lines\": " << tier.lines << ",\n"
       << "  \"stream_bytes\": " << stream.size() << ",\n"
       << "  \"feed_ms\": " << feedMs << ",\n"
       << "  \"search_ms\": " << searchMs << ",\n"
       << "  \"search_matches\": " << matches << ",\n"
       << "  \"reflow_widen_ms\": " << widenMs << ",\n"
       << "  \"reflow_narrow_ms\": " << narrowMs << ",\n"
       << "  \"rss_bytes\": " << rssBytes << ",\n"
       << "  \"peak_rss_bytes\": " << peakBytes << "\n"
       << "}\n";

    std::printf("zz_bench_feed[%s/%s]: stream %zuMB, feed %.1fms (%.1f MB/s), search %.1fms (%zu matches), reflow widen %.1fms narrow %.1fms, rss %zuMB peak %zuMB\n",
                profileName(profile), tier.name, stream.size() / (1024 * 1024), feedMs,
                feedMs > 0 ? (stream.size() / (1024.0 * 1024.0)) / (feedMs / 1000.0) : 0.0,
                searchMs, matches, widenMs, narrowMs, rssBytes / (1024 * 1024),
                peakBytes / (1024 * 1024));
    if (g_failures == 0)
        std::printf("zz_bench_feed: all passed\n");
    return g_failures;
}
```

- [ ] **步骤 2：tests/CMakeLists.txt 的 M8 块内追加 feed 部分**

在 T1 追加的 M8 块中，`add_custom_target(bench-long ...)` 之前插入 feed 可执行与短跑注册，并把 feed 长跑命令补进 bench-long。M8 块最终形态（整块替换）：

```cmake
# M8：百万行实验 benchmark（tests/bench/，单元轨 + facade 轨双 harness）。
# 短跑档（10k）注册默认 ctest；长跑档（100k/1M）经 bench-long 自定义目标手动触发
#（cmake --build <dir> --target bench-long），不进默认 ctest——ctest 无单测试默认
# 排除机制，label 方案需改动既有 ctest --preset 基线命令，故用构建目标。
# RSS 采样为 Linux 实现，非 Linux 返回 0（macOS 适配属 M9，届时解除 APPLE 排除）。
if(UNIX AND NOT APPLE)
    add_executable(zz_bench_scrollback bench/zz_bench_scrollback.cpp bench/bench_common.cpp)
    target_link_libraries(zz_bench_scrollback PRIVATE ZzTermCore)
    add_test(NAME zz_bench_scrollback COMMAND zz_bench_scrollback)
    set_tests_properties(zz_bench_scrollback PROPERTIES TIMEOUT 60)

    add_executable(zz_bench_feed bench/zz_bench_feed.cpp bench/bench_common.cpp)
    target_link_libraries(zz_bench_feed PRIVATE ZzTermCore)
    add_test(NAME zz_bench_feed_ascii COMMAND zz_bench_feed --profile=ascii)
    add_test(NAME zz_bench_feed_mixed COMMAND zz_bench_feed --profile=mixed)
    set_tests_properties(zz_bench_feed_ascii zz_bench_feed_mixed PROPERTIES TIMEOUT 60)

    add_custom_target(bench-long
        COMMAND zz_bench_scrollback --tier=100k
        COMMAND zz_bench_scrollback --tier=1m
        COMMAND zz_bench_feed --profile=ascii --tier=100k
        COMMAND zz_bench_feed --profile=mixed --tier=100k
        COMMAND zz_bench_feed --profile=ascii --tier=1m
        COMMAND zz_bench_feed --profile=mixed --tier=1m
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "M8 长跑 benchmark（100k/1M，JSON 落盘 ${CMAKE_CURRENT_BINARY_DIR}）")
endif()
```

- [ ] **步骤 3：构建 + 短跑验证**

运行：

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：构建零告警；50/50（47 + 3 个 bench 短跑）；两个 feed 短跑 stdout 含 matches=10000。

- [ ] **步骤 4：OFF 配置回归 + Commit**

运行：

```bash
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
```

预期：OFF 套件全绿（37 + 3 = 40/40；bench 仅依赖 native 后端，OFF 构建照常收编）。

```bash
git add tests/bench/zz_bench_feed.cpp tests/CMakeLists.txt
git commit -m "test(bench): facade 轨 zz_bench_feed（feed 吞吐/search/reflow/RSS 双画像三档）"
```

---

## 任务 4：全矩阵测量 + probe 文档 + 门控判定（决策点）

**文件：**
- 创建：`docs/superpowers/specs/2026-09-22-m8-million-line-probe.md`
- 创建：`tests/perf/records/2026-09-22-m8-*.json`（长跑 JSON 拷贝入库，M4/M6 records 先例）

**执行注意：** 步骤 1 长跑可能数分钟（1M 行 feed + reflow，-O0），用后台任务执行（disable_timeout），不得前台等超时。步骤 3 的门控判定若出现不达标项，本任务只负责记录与归因初判——是否进入优化波由主代理回报用户后决定，子代理不得擅自修改 src/。

- [ ] **步骤 1：跑全矩阵（短跑 ctest + 长跑 bench-long）**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/linux-gcc-debug --target bench-long
```

预期：50/50 短跑全过；bench-long 六条命令依次完成退出码 0；`build/linux-gcc-debug/tests/` 下产出 8 份 JSON（scrollback 10k/100k/1m 三份 + feed ascii/mixed 各 10k/100k/1m 六份，其中 10k 三份来自短跑 ctest——若 cwd 不同步则重跑一次 `./tests/zz_bench_scrollback` 与 `./tests/zz_bench_feed --profile=*` 于该目录补齐）。

- [ ] **步骤 2：JSON 物证入库**

```bash
cd build/linux-gcc-debug/tests
cp zzterm-bench-scrollback-10k.json  ../../../tests/perf/records/2026-09-22-m8-scrollback-10k.json
cp zzterm-bench-scrollback-100k.json ../../../tests/perf/records/2026-09-22-m8-scrollback-100k.json
cp zzterm-bench-scrollback-1m.json   ../../../tests/perf/records/2026-09-22-m8-scrollback-1m.json
cp zzterm-bench-feed-ascii-10k.json  ../../../tests/perf/records/2026-09-22-m8-feed-ascii-10k.json
cp zzterm-bench-feed-ascii-100k.json ../../../tests/perf/records/2026-09-22-m8-feed-ascii-100k.json
cp zzterm-bench-feed-ascii-1m.json   ../../../tests/perf/records/2026-09-22-m8-feed-ascii-1m.json
cp zzterm-bench-feed-mixed-10k.json  ../../../tests/perf/records/2026-09-22-m8-feed-mixed-10k.json
cp zzterm-bench-feed-mixed-100k.json ../../../tests/perf/records/2026-09-22-m8-feed-mixed-100k.json
cp zzterm-bench-feed-mixed-1m.json   ../../../tests/perf/records/2026-09-22-m8-feed-mixed-1m.json
```

- [ ] **步骤 3：撰写 probe 文档（含门控逐项判定）**

创建 `docs/superpowers/specs/2026-09-22-m8-million-line-probe.md`，结构如下（数值从步骤 1 的 JSON 与 stdout 转录，不得凭记忆编造；机器信息经 `uname -m && nproc && free -g` 实测填入）：

```markdown
# M8 百万行实验：全矩阵基线与门控判定

- 日期：2026-09-22
- 分支：contour
- preset：linux-gcc-debug（-O0，门控基线档）
- 机器：[uname -m / nproc / 内存 GB 实测填入]
- 数据源：tests/perf/records/2026-09-22-m8-*.json（九份）
- 门控依据：docs/superpowers/specs/2026-09-22-m8-million-line-design.md §4

## 1. 单元轨（zz_bench_scrollback，直接驱动 ZzScrollback）

| 档位 | 画像 | append ms | append lines/s | lineAt 顺序 ms | lineAt 随机 10 万次 ms | reflow 80->120 ms | reflow 120->80 ms | RSS MB | approxBytes MB |
|---|---|---|---|---|---|---|---|---|---|
| 10k | ascii | | | | | | | | |
| 10k | mixed | | | | | | | | |
| 100k | ascii | | | | | | | | |
| 100k | mixed | | | | | | | | |
| 1m | ascii | | | | | | | | |
| 1m | mixed | | | | | | | | |

## 2. facade 轨（zz_bench_feed，ZzTerminal+native 灌 VT 流）

| 档位 | 画像 | 流大小 MB | feed ms | feed MB/s | search ms | reflow widen ms | reflow narrow ms | RSS MB | peak RSS MB |
|---|---|---|---|---|---|---|---|---|---|
| 10k | ascii | | | | | | | | |
| 10k | mixed | | | | | | | | |
| 100k | ascii | | | | | | | | |
| 100k | mixed | | | | | | | | |
| 1m | ascii | | | | | | | | |
| 1m | mixed | | | | | | | | |

## 3. 门控逐项判定（1M 档）

| 门控项 | 阈值 | 实测（取值口径） | 判定 |
|---|---|---|---|
| RSS 峰值 | 不超过 512MB | facade 轨 1m 两画像 peak_rss_bytes 较大者 | |
| append 吞吐 | 不低于 10k 档的 50% | 单元轨 1m lines/s 对 10k lines/s（分画像） | |
| 全量 search | 不超过 5s | facade 轨 1m 两画像 search_ms 较大者 | |
| reflow 80->120 | 不超过 10s | facade 轨 1m 两画像 widen_ms 较大者 | |
| feed 吞吐 | 只记录不设门 | facade 轨 1m 两画像 MB/s | 记录 |

## 4. 归因初判与结论

[全达标：写明"里程碑以纯测量收尾"。有不达标项：用单元轨数据指出瓶颈层
（历史存储/访问路径/reflow 算法/搜索扫描），列候选优化方向
（规格 §1 清单：Cell 瘦身 / warm LZ4 / lazy reflow / 搜索增量索引），
不实施——优化波与否由用户决策。]

## 5. 门控修正记录

[若规格 §4 预设阈值量级错误需修正，在此记录原值/新值/理由；无修正则写"无"。]
```

- [ ] **步骤 4：全回归 + Commit**

```bash
ctest --preset linux-gcc-debug   # 50/50
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check   # 40/40
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check   # 50/50
doxygen Doxyfile   # exit 0 零警告（本里程碑未动公开头/docs 公开面，probe 在 specs/ 惯例豁免面内——仍跑一次确认）
```

```bash
git add docs/superpowers/specs/2026-09-22-m8-million-line-probe.md tests/perf/records/2026-09-22-m8-*.json
git commit -m "docs(probe): M8 百万行全矩阵基线与门控判定"
```

- [ ] **步骤 5：回报决策点**

把 probe §3 判定表与 §4 归因初判汇报主代理；主代理向用户汇报并等待决策（全达标 → finishing；不达标 → 用户确认优化方向后另立优化计划）。本子任务到此为止，不得进入任何 src/ 修改。

---

## 自检记录（编写期）

- 规格覆盖：§2 组件 → T1/T2/T3；§3 矩阵（三档 × 指标 × 双画像）→ T2/T3/T4；§4 门控 → T4 步骤 3；§5 流程（测量波 → 决策点 → 条件优化波）→ T4 步骤 5 + 计划头部执行边界；§6 健壮性（生成器自检/RSS 失败处理/长跑异常记录）→ T1 步骤 3、T2/T3 自检段、harness 只测量不判决设计；§8 DoD → T4 全步骤；§9 任务划分 → 本计划 T1-T4（T5 按规格为条件触发，另立计划）。
- 占位符：probe 文档表格留空是 T4 执行期填数项（测量产物，非计划缺陷）；其余步骤均含完整代码/命令。
- 类型一致：zzBenchTier{lines,name}、zzBenchMakeLines(seed,count,cols,profile)、zzBenchMakeVtStream(lineCount,profile)、zzBenchLinesChecksum/zzBenchStreamChecksum、zzBenchRssCurrentBytes/zzBenchRssPeakBytes/zzBenchMillisSince、ZzBenchProfile 的 Ascii/Mixed——T1 定义与 T2/T3 使用一致；CMake target 名 zz_bench_scrollback/zz_bench_feed/bench-long 三处一致。
