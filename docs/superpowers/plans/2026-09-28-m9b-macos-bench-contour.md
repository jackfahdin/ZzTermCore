# M9b macOS 第二波（bench 解禁 + contour ON 验证 + 计数对齐）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** macOS 上 bench 双 harness 编译运行且 RSS 采样产出真实值、contour ON 构建测试绿（默认 OFF 不动）、interactive 测试补回、采一份 Apple Silicon 性能基线归档。

**架构：** 纯增量适配：bench_common.cpp RSS 采样改 Linux/APPLE/其他三分支；tests/CMakeLists.txt 解除 bench 的 APPLE 排除；CI 加两个 workflow（contour 验证 push 触发、bench 采集 dispatch 手动触发），ci-macos-clang 补 pip 依赖。

**技术栈：** mach task_info（TASK_VM_INFO resident_size）、getrusage ru_maxrss（macOS 字节 / Linux KB）、CMake presets、GitHub Actions（gh CLI 已可用）。

**规格：** docs/superpowers/specs/2026-09-28-m9b-macos-bench-contour-design.md（范围、红线、排除项以规格为准）

**基线命令（本机 Linux，全程不得回归）：**
- ON：`ctest --preset linux-gcc-debug`（50/50）
- OFF：`ctest --test-dir build/m2-off-check`（40/40）
- shared：`ctest --test-dir build/m2-shared-check`（50/50）
- fuzz：`ctest --preset linux-clang-fuzz -R fuzz`（2/2；configure 重建需 `-D CMAKE_CXX_COMPILER=clang++-20`）
- doxygen：`doxygen Doxyfile`（exit 0 零警告）

---

### 任务 1：RSS 采样 macOS 实现 + bench 解禁

**文件：**
- 修改：`tests/bench/bench_common.cpp`（:9-12 包含段、:31-48 zzBenchRssCurrentBytes、:50-60 zzBenchRssPeakBytes）
- 修改：`tests/bench/bench_common.h`（RSS 两函数的注释）
- 修改：`tests/CMakeLists.txt`（约 :198-203 注释与门控行）

- [ ] **步骤 1：bench_common.cpp 包含段加 APPLE 分支**

现状（:9-12）：

```cpp
#if defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#endif
```

改为：

```cpp
#if defined(__linux__)
#include <fstream>
#include <sys/resource.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/resource.h>
#endif
```

- [ ] **步骤 2：zzBenchRssCurrentBytes 加 APPLE 分支**

现状函数体（:31-48）的 `#else return 0; // macOS 适配属 M9` 结构，改为：

```cpp
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
#elif defined(__APPLE__)
    task_vm_info_data_t    info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<std::size_t>(info.resident_size);
    return 0; // 采样失败：按 0 处理（约定同上）
#else
    return 0; // 其他平台未实现
#endif
}
```

- [ ] **步骤 3：zzBenchRssPeakBytes 改 Linux/APPLE 共路 + 单位条件**

getrusage 两平台通用，仅 ru_maxrss 单位不同。函数体改为：

```cpp
std::size_t zzBenchRssPeakBytes()
{
#if defined(__linux__) || defined(__APPLE__)
    struct rusage ru {};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
        return static_cast<std::size_t>(ru.ru_maxrss); // macOS ru_maxrss 单位字节
#else
        return static_cast<std::size_t>(ru.ru_maxrss) * 1024; // Linux ru_maxrss 单位 KB
#endif
    }
    return 0;
#else
    return 0; // 其他平台未实现
#endif
}
```

- [ ] **步骤 4：bench_common.h 注释同步**

两处注释去掉"macOS 适配属 M9"表述，改为：

```cpp
/// 当前 RSS（字节）。Linux 经 /proc/self/status 的 VmRSS，macOS 经 mach task_info
///（TASK_VM_INFO resident_size）；解析/采样失败或未实现平台返回 0。
std::size_t zzBenchRssCurrentBytes();

/// 峰值 RSS（字节）。经 getrusage(RUSAGE_SELF) 的 ru_maxrss（Linux 单位 KB、macOS
/// 单位字节，实现内换算）；未实现平台返回 0。
std::size_t zzBenchRssPeakBytes();
```

- [ ] **步骤 5：tests/CMakeLists.txt 解除 bench APPLE 排除**

门控行与注释，现状：

```cmake
# RSS 采样为 Linux 实现，非 Linux 返回 0（macOS 适配属 M9，届时解除 APPLE 排除）。
if(UNIX AND NOT APPLE)
```

改为：

```cmake
# RSS 采样：Linux 与 macOS 均有实现（M9b 补齐 mach task_info 路径），其他平台返回 0。
if(UNIX)
```

（块内 add_executable/add_test/bench-long 目标均不动。）

- [ ] **步骤 6：本机基线回归**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
```

预期：50/50、40/40、50/50、2/2、exit 0 零警告。Linux 分支代码零改动，应全绿；任何红停下修复。macOS 分支本机不可编译验证，属 CI-only（commit message 标注）。

- [ ] **步骤 7：Commit**

```bash
git add tests/bench/bench_common.cpp tests/bench/bench_common.h tests/CMakeLists.txt
git commit -m "feat(bench): M9b RSS 采样 macOS 实现（mach task_info + ru_maxrss 单位换算）并解除 bench APPLE 排除（macOS 分支 CI-only 验证）"
```

---

### 任务 2：CI——interactive 补回 + contour ON 验证 workflow

**文件：**
- 修改：.github/workflows/ci-macos-clang.yml（纯文本引用，code span 以点开头触发 doxygen 陷阱）
- 创建：.github/workflows/ci-macos-contour.yml

- [ ] **步骤 1：ci-macos-clang.yml 加 pip 依赖步**

在 checkout 步之后、Configure 步之前插入：

```yaml
      - name: 安装 Python 依赖（pexpect/pyte 供 ZzTermSmokeInteractive 交互测试）
        run: pip3 install pexpect pyte
```

若 runner 的 pip3 有 externally-managed 限制报错，改用 `pip3 install --user pexpect pyte` 或 `python3 -m pip install --break-system-packages pexpect pyte`，按 CI 日志实证择一（CI-only 验证）。

- [ ] **步骤 2：新建 ci-macos-contour.yml**

完整内容：

```yaml
name: CI macos-contour

on:
  push:
    branches: [main, master, contour]
  pull_request:

jobs:
  macos-contour:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v7
        with:
          submodules: true
      - name: Configure（显式开启 Contour 后端，默认 OFF 不动）
        run: cmake --preset macos-clang-debug -DZZTERM_WITH_CONTOUR=ON
      - name: Build
        run: cmake --build --preset macos-clang-debug
      - name: Test
        run: ctest --preset macos-clang-debug
```

注意：`submodules: true` 必需（third_party/contour 是 submodule）；configure 期 contour 会联网拉第三方依赖，runner 有网络属预期。

- [ ] **步骤 3：Commit + push**

```bash
git add .github/workflows/ci-macos-clang.yml .github/workflows/ci-macos-contour.yml
git commit -m "ci: M9b macos job 装 pexpect/pyte 补回 interactive 测试 + 新增 ci-macos-contour 验证 contour ON（显式开启，默认 OFF 不动）"
git push origin contour
```

- [ ] **步骤 4：观察迭代至绿**

```bash
gh run list --branch contour --limit 8
```

对新触发的 ci-macos-clang 与 ci-macos-contour 两个 workflow：红则 `gh run view 〈run-id〉 --log-failed` 诊断、最小修复、本机基线回归（至少 50/50 + doxygen）后复推。

**红线**：contour 编译失败若必须改 third_party/contour 才能通过——立即停下报 BLOCKED，不擅自绕过（规格 §3：该情形下 contour ON 项整体退回，其余项照常）。

预期终点：ci-macos-clang 绿且测试计数较 36 增加（interactive +1、bench 三档 +3，contour OFF 默认配置下预期 40/40）；ci-macos-contour 绿（contour ON，计数与 Linux ON 对齐预期 50 量级——实测落账）。

---

### 任务 3：bench 采集 workflow + Apple Silicon 矩阵入库

**文件：**
- 创建：.github/workflows/ci-macos-bench.yml
- 创建（下载后入库）：`tests/perf/records/〈入库日期〉-m9b-*.json`（六份长跑 + 三份短跑档，实际份数按产物清点）

- [ ] **步骤 1：新建 ci-macos-bench.yml（dispatch 手动触发）**

先读 zz_bench_scrollback.cpp / zz_bench_feed.cpp 的 JSON 落盘文件名规则（实测文件名以源码为准），再写 workflow：

```yaml
name: CI macos-bench（手动触发采集）

on:
  workflow_dispatch:

jobs:
  macos-bench:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v7
      - name: Configure
        run: cmake --preset macos-clang-debug
      - name: Build
        run: cmake --build --preset macos-clang-debug
      - name: 短跑档（10k 三份）
        run: ctest --preset macos-clang-debug -R zz_bench
      - name: 长跑档（100k/1m 六份）
        run: cmake --build build/macos-clang-debug --target bench-long
      - name: 上传 bench JSON
        uses: actions/upload-artifact〈最新稳定 Major，先查 releases 页确认〉@v〈N〉
        with:
          name: m9b-bench-macos
          path: build/macos-clang-debug/tests/*.json
```

（upload-artifact 版本按用户规则取最新稳定 Major——先 FetchURL https://github.com/actions/upload-artifact/releases 确认再写定。）

```bash
git add .github/workflows/ci-macos-bench.yml
git commit -m "ci: M9b macOS bench 采集 workflow（workflow_dispatch 手动触发 + artifact 上传）"
git push origin contour
```

- [ ] **步骤 2：手动触发并等待完成**

```bash
gh workflow run ci-macos-bench.yml --ref contour
gh run list --workflow=ci-macos-bench.yml --limit 1
```

长跑档含 1m 六条，预估数分钟。红则诊断修复复推；绿则下一步。

- [ ] **步骤 3：下载 artifact 入库**

```bash
gh run download 〈run-id〉 -n m9b-bench-macos -D /tmp/m9b-bench
ls /tmp/m9b-bench/
```

清点 JSON 份数与内容（每份 rss_bytes 应非 0——为 0 说明 mach 采样未生效，按规格 §4 当 bug 修复后重采）。重命名入库：

```bash
cd tests/perf/records
# 按入库日期与既有命名惯例（日期-m9b-轨-画像-档位.json）重命名后
git add tests/perf/records/
git commit -m "test(perf): M9b Apple Silicon bench 矩阵入库（九份，只记录不设门）"
git push origin contour
```

---

### 任务 4：记录文档 + 旧规格划销 + 终验

**文件：**
- 创建：`docs/superpowers/specs/2026-09-28-m9b-macos-bench-contour-record.md`
- 修改：`docs/superpowers/specs/2026-09-28-m9a-macos-port-design.md` §6
- 修改：`docs/superpowers/specs/2026-09-22-m8-million-line-design.md` §2 平台边界行

- [ ] **步骤 1：记录文档**

创建 `docs/superpowers/specs/2026-09-28-m9b-macos-bench-contour-record.md`，骨架（全部实测填充，禁留占位）：

```markdown
# M9b macOS 第二波记录

- 日期：2026-09-28
- 分支：contour
- runner：macos-latest（Apple Silicon / Apple Clang / libc++）

## 1. RSS 采样与 bench 解禁
〈mach task_info 实现落地情况；RSS 非 0 实证（artifact JSON 摘录一行）；macOS 测试计数前后对照〉

## 2. contour ON 验证
〈ci-macos-contour 首次运行结论：编译是否一次过、C++23/联网拉依赖是否顺利、ctest 计数、run 链接〉

## 3. interactive 补回
〈pip 安装形态（是否需 --user / --break-system-packages）、ZzTermSmokeInteractive_native 是否进入测试名单〉

## 4. Apple Silicon bench 矩阵
〈九份 JSON 文件名清单；关键读数表：1m 档 RSS/peak/search/reflow 对照 M8 Linux 基线的量级简注；只记录不设门（4A）〉

## 5. 最终结果
〈CI 六 workflow 状态、macOS 各配置测试计数、本机五项基线〉
```

doxygen 陷阱（写文档必须规避）：行内 code span 禁尖括号、内容禁以点开头、禁井号预处理词、禁反斜杠转义、禁双冒号全局限定词（如 sigemptyset 的前车之鉴 0c418fc）。写完先跑 `doxygen Doxyfile` 确认零警告。

- [ ] **步骤 2：旧规格划销**

`docs/superpowers/specs/2026-09-28-m9a-macos-port-design.md` §6 排除项前两行标注已完成归属：

```markdown
- ~~bench RSS macOS 采样（mach task_info / proc_pid_info）与 tests/CMakeLists.txt 的 bench APPLE 排除解除~~ → 已完成于 M9b；
- ~~contour ON macOS 构建验证（C++23 + 联网拉依赖）~~ → 已完成于 M9b（默认 OFF 维持，验证经 ci-macos-contour）；
```

`docs/superpowers/specs/2026-09-22-m8-million-line-design.md` §2 平台边界行：

```markdown
- 平台边界：RSS 采样为 Linux 专属实现，macOS 适配（M9 移植 bench 时）以条件编译预留接口形态，本里程碑不实现。
```

改为：

```markdown
- 平台边界：RSS 采样 Linux 实现于本里程碑；macOS 实现（mach task_info）已由 M9b 补齐，接口形态未变。
```

- [ ] **步骤 3：全基线终验**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
gh run list --branch contour --limit 6
```

预期：本机 50/50、40/40、50/50、2/2、doxygen 零警告；CI 六 workflow 全 success。

- [ ] **步骤 4：Commit + push**

```bash
git add docs/
git commit -m "docs(m9b): macOS 第二波记录与旧规格排除项划销（CI 六 workflow 绿）"
git push origin contour
```

---

## 自检结论

- 规格覆盖：§2 改动点 1/2 = 任务 1；改动点 3/4 = 任务 2；改动点 5 = 任务 3；改动点 6 = 任务 4 步骤 2；§3 验证与红线 = 各任务验证步 + 任务 2 步骤 4 红线；§4 错误处理 = 任务 2 步骤 1 回退、任务 3 步骤 3 RSS 为 0 检查、任务 2 步骤 4 红线；§7 记录 = 任务 4 步骤 1；§6 排除项无任务（正确）。
- 占位符：任务 3 的 〈run-id〉〈N〉〈入库日期〉为执行期取值指令，非实现缺口；upload-artifact 版本号按用户规则执行期查实——这是用户"最新稳定 Major"规则的直接落地，非待定。
- 类型一致性：无新公开类型；RSS 两函数签名不变（bench_common.h 仅注释更新）。
