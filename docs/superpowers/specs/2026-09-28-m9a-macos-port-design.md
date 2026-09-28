# M9a：macOS 功能移植第一波（core + PTY 绿）

- 日期：2026-09-28
- 分支：contour
- 前置：M8b 已合并 master（3e9a3c5）——ZzCell 12B bit-pack、裁剪越界修复、streaming reflow 落账，50/50 全绿；仓库根杂项已清理（d76ebf7）
- 依据：docs/Architecture.md 第 14 节路线图（M9 macOS）；docs/superpowers/specs/2026-09-17-unix-pty-smoke-demo-design.md（macOS PTY 头文件差异留待本里程碑）；docs/superpowers/specs/2026-09-22-m8-million-line-design.md §2 平台边界（bench RSS 采样 macOS 适配随 M9，经拆分归属 M9b）
- 用户批准决策：1A 推送 origin 走 GitHub Actions macos runner 迭代验证（先推 contour，master 推送后置）；2B 分两波——M9a 只做 core+测试+PTY 绿，bench RSS 与 contour ON 验证归 M9b；3B fuzz 维持 Linux 专属；4A macOS 只要求功能绿，bench 数据只记录不设性能门；方案 A（本机 libc++ 预检 + CI 迭代闭环）；细 A（ci.yml push branches 加 contour）

## 1. 背景与目标

ZzTermCore 核心库（src/parser、screen、history、unicode、input）为零平台条件代码的纯可移植 C++20；平台相关面集中在 PTY 层与 bench 的 RSS 采样。macos-clang-debug preset 与 CI macos-clang job 自 M0 起存在，但 origin/master 落后本地 202 commits，从未验证过当前代码体。本里程碑（M9a）使 macos-latest runner（Apple Silicon / Apple Clang / libc++）上 core 编译通过、默认 ctest 全绿、PTY 与 examples 解除排除并测试通过，验证通路为推送 contour 触发 CI 迭代。

范围拆分（2B）：M9a 不含 bench RSS macOS 采样、bench 的 APPLE 排除解除、contour ON macOS 验证——三者归 M9b；fuzz 维持 Linux 专属（3B）；macOS 不设性能门控，bench 数据（M9b 解禁后）只记录（4A）。

## 2. 改动点（共 5 处，全部小步）

1. `pty/unix/ZzPty.cpp`：条件包含——`__APPLE__` 走 `<util.h>`，其余 UNIX 走 `<pty.h>`。TIOCSWINSZ / waitpid / poll 均为 POSIX 通用，预期零逻辑改动。
2. 根 `CMakeLists.txt`：`if(UNIX AND NOT APPLE)`（pty + examples 两处收编点）放宽为 `if(UNIX)`；Windows 仍排除（ConPTY 里程碑靠后）。既有 `find_library(util)` 找到才链接的逻辑天然兼容（macOS openpty 在 libSystem 内，无需显式库）。
3. `tests/unit/test_pty.cpp`：编译守卫从 `__unix__ && !__APPLE__` 放宽为 `__unix__ || __APPLE__`（else 分支平凡 main 空跑通过的结构不动）。
4. 根 `CMakeLists.txt` contour 选项：APPLE 上 `ZZTERM_WITH_CONTOUR` 默认 OFF（该选项要求 C++23 编译器且 configure 期联网拉第三方依赖，两大不确定源按 2B 归 M9b；Linux 默认 ON 不动，submodule 未初始化降级逻辑不动）。
5. `.github/workflows/ci.yml`：push branches 加 `contour`（push 触发器原仅 main/master；M9 收尾后保留作分支常态防护）。（实施期旁注：经用户 3B 决策演变为按平台拆分五文件 ci-ubuntu-clang/ci-ubuntu-gcc/ci-windows-msvc/ci-macos-clang/ci-docs，各文件均含 contour 触发；同 commit 按用户"官方托管 runner 用最新稳定 Major"规则将 actions/checkout 升至 v7。）

## 3. 本机预检（实施第一步）

装 `libc++-dev`（与系统 clang 版本配套），以 `clang++ -stdlib=libc++` 建独立预检构建目录（`build/` 已 gitignore，目录名实施期定，不进 CMakePresets 不入库），消灭 libstdc++/libc++ 标准库差异类错误（头文件传递包含、字面量运算符、charconv 覆盖度等）后再推 CI。预检只做编译与 ctest，不替代本机基线回归。

## 4. CI 迭代与错误处理

- 迭代闭环：本机预检绿 → 本机基线回归绿 → 推 contour → macos job 红则按日志最小修复 → 复推，直至绿。
- 本机不可复现的失败（SDK 特有）在 commit message 或记录文档标注 CI-only 验证。
- 红线：若出现必须修改 third_party/contour 才能通过的情况，违反"永不改"约定，立即停下回报用户，不擅自绕过。
- 已知风险排序：libc++ 差异（预检覆盖大半）> contour APPLE 分支（默认 OFF 规避）> ARM64 ABI 差异（char 符号性、对齐敏感 UB，由测试暴露）。

## 5. 测试策略与门控

- macOS 门控：CI macos-clang job 全绿（configure + build + ctest 全量通过）。测试计数预期为 Linux OFF 配置减 bench 三档（bench 的 APPLE 排除本波维持），具体数字实施期实测如实记录（probe 式记录文档，见 §7）。
- 本机基线不回归：linux-gcc-debug 50/50、m2-off-check 40/40、m2-shared-check 50/50、linux-clang-fuzz 2/2、doxygen exit 0 零警告。
- 行为保持：core 公开 API 语义零变化；Linux 侧构建配置与测试断言零变化（contour 默认值改动仅作用 APPLE 分支）。（实施期旁注：856d2ae 经用户 4A 裁定将 test_perf_scrollback reflow 门控 500ms 放宽至 1500ms——CI 共享 runner 噪声擦线，属用户中途批准的例外，三处留痕：commit message、测试注释、记录文档。）
- 文档顺带更新：docs/VT-Xterm-Checklist.md 与 docs/VT-Xterm-Checklist-v2.md 的 macOS PTY TIOCSWINSZ 项勾选（实测通过后）。

## 6. 排除项（本里程碑不做）

- ~~bench RSS macOS 采样（mach task_info / proc_pid_info）与 tests/CMakeLists.txt 的 bench APPLE 排除解除~~ → 已完成于 M9b；
- ~~contour ON macOS 构建验证（C++23 + 联网拉依赖）~~ → 已完成于 M9b（默认 OFF 维持，验证经 ci-macos-contour）；
- fuzz macOS job（3B：维持 Linux 专属）；
- macOS 性能门控（4A：只记录不设门，且 bench 未解禁本波无数据）；
- master 推送（1A：后置，用户另行决策）；
- Windows ConPTY（路线图靠后里程碑）。

## 7. 记录与收尾

- CI 迭代过程与最终结果写入记录文档 `docs/superpowers/specs/2026-09-28-m9a-macos-port-record.md`：各轮失败与修复摘要、最终 macOS 测试计数、CI run 链接、本机预检发现的问题清单。
- 收尾惯例：finishing 合并 master（--no-ff），合并后本机全基线回归 + doxygen，contour 保留。
