# M9a macOS 移植记录

- 日期：2026-09-28
- 分支：contour
- runner：macos-latest（Apple Silicon / Apple Clang / libc++）

## 1. 本机 libc++ 预检发现

零修复通过。以 `clang++-20 -stdlib=libc++` 全量重建（236 个目标编译）并运行 ctest 50/50 通过，未发现任何 libc++ 专属编译或测试问题。唯一偏离：简报写的编译器名 `clang++`，本机实际使用 `clang++-20`（主代理裁定，版本钉死为 20）。

## 2. CI 迭代轮次

共 5 轮收敛，观察通路经历 gh token 失效 → 临时插桩回传 annotations → 用户修复 token → 插桩移除。

### 第 1 轮（commit 0c4f1ef，run 36395927927）→ failure

- 失败现象：ubuntu-gcc 绿；ubuntu-clang 测试败、windows-msvc 配置败、macos-clang 构建败、docs 败；job 日志不可读（token 失效）。
- 修复：盲修两处并本机复现验证——历史文档 8 处 doxygen 1.9.8 解析陷阱改写（commit 015ff05）；windows-msvc-debug preset 去除 hidden 标记（commit 0601170）。
- 结果：进入第 2 轮。

### 第 2 轮（commit 015ff05，run 36397532624）→ failure

- 失败现象：四个 job 仍败，证明盲修不充分/不对路。
- 修复：加入临时日志插桩，失败步骤尾部经 workflow 命令转 annotations（commit e4917b1）。
- 结果：拿到真实错误文本，进入第 3 轮。

### 第 3 轮（commit e4917b1，run 36398766114）→ failure，但定位全部根因

- windows：找不到任何 Visual Studio 实例（生成器钉死 VS2022，windows-latest 已无该实例）。
- macos：ZzTermSmoke `main.cpp:399` 报 `pipe2` 不存在（pipe2 为 Linux 专属），410 行级联错误。
- docs：`dot: not found`（CI 未装 graphviz，WARN_AS_ERROR 下 dot 失败即 error）。
- ubuntu-clang / ubuntu-gcc：`test_perf_scrollback` 擦线失败（reflow 门控 500ms，CI 共享 runner 负载下边缘抖动）。

### 第 4 轮（commit 856d2ae，run 36399893609）→ 四 job 转绿，macos 构建仍败

- 修复：`pipe2` 改为 `pipe` + `fcntl`（O_NONBLOCK / FD_CLOEXEC）；windows preset 去钉死生成器交由 CMake 自选最新 VS；docs job 安装 graphviz；`test_perf_scrollback` 门控 500 放宽至 1500ms（用户 4A 裁定：CI 共享 runner 噪声，本机门控不动，留痕于测试注释）。
- 结果：ubuntu-clang / ubuntu-gcc / windows-msvc / docs 转绿；macos 新错误 `main.cpp:410` `::sigemptyset`——macOS 上 sigemptyset 是宏，不能加 `::` 限定。

### 第 5 轮（commit 0efa136，run 36400779655）→ 五 job 全绿

- 修复：`sigemptyset` / `sigaction` 去 `::` 限定（macOS 适配本体收尾）。
- 结果：五 job 全绿。随后用户拆分 workflow 为按平台五文件并升级 actions/checkout 至 v7（commit 31c5a33）；临时计数注记（commit b14c4d7，run 36404174626）回传 macOS ctest 36/36；用户移除全部插桩（commit b7eba1f）后五个 workflow 再次全绿确认。

## 3. 最终结果

- macos-clang job：绿（ctest 36/36，run 链接 https://github.com/jackfahdin/ZzTermCore/actions/runs/36404273262 ）
- 其余四 job：绿（ubuntu-clang run 36404273222、ubuntu-gcc run 36404273331、windows-msvc run 36404273242、docs run 36404273247，head_sha 均为 b7eba1f）
- 本机基线：50/50（linux-gcc-debug）、40/40（m2-off-check）、50/50（m2-shared-check）、fuzz 2/2（linux-clang-fuzz）、doxygen 零警告

### macOS 36 与 Linux 40 的计数差构成

Linux 本机 OFF 配置（m2-off-check）收录 40 个测试，macOS CI 收录 36 个，差 4 个，逐项如下：

| 测试名 | 缺席原因 |
|---|---|
| zz_bench_scrollback | bench 三档由 tests/CMakeLists.txt 的 `if(UNIX AND NOT APPLE)` 门控，RSS 采样为 Linux 实现，APPLE 排除维持至 M9b |
| zz_bench_feed_ascii | 同上 |
| zz_bench_feed_mixed | 同上 |
| ZzTermSmokeInteractive_native | configure 期探测 python3 的 pexpect/pyte 依赖，macos runner 未安装，按设计静默跳过（tests/CMakeLists.txt:187），不影响基础套件 |

macOS 侧 contour 后端因 APPLE 默认 OFF 亦不产生 contour 变体（contour 适配整体属 M9b，不在本计划范围）。
