# M9b：macOS 移植第二波（bench 解禁 + contour ON 验证 + 计数对齐）

- 日期：2026-09-28
- 分支：contour
- 前置：M9a 已合并 master（77640fd）——macOS core+PTY 绿，CI 按平台拆五文件，checkout v7，macOS ctest 36/36
- 依据：docs/superpowers/specs/2026-09-28-m9a-macos-port-design.md §6 排除项（bench RSS 采样、bench 解禁、contour ON 验证归本波）；M8 规格 §2 平台边界（RSS 采样 macOS 适配随 M9）
- 用户批准决策：1A 全量四项（bench RSS 采样 + bench 解禁 + contour ON 验证 + CI 装 pexpect/pyte 补回 interactive）；2A contour ON 验证以 CI 绿为准（既有 50 测试族即 parity 覆盖，不另建对照）；3A contour APPLE 默认维持 OFF，验证走显式开启的独立 CI job；4A 采 10k/100k/1M 矩阵归档只记录不设门；分歧点 1 选 A（mach task_info 采样）；分歧点 2 选 A（独立第六 workflow）；分歧点 3 选 A（workflow_dispatch 手动触发 + artifact 上传）

## 1. 背景与目标

M9a 完成 macOS 功能移植第一波后，macOS 侧遗留三个排除项与一个计数差：bench 双 harness 因 RSS 采样 Linux-only 被 APPLE 排除（tests/CMakeLists.txt :203 附近）；contour 后端在 APPLE 默认 OFF 未验证；macos runner 缺 pexpect/pyte 致 ZzTermSmokeInteractive_native 跳过（36 对 Linux OFF 40 的差额之一）。本里程碑补齐全部四项：macOS 上 bench 编译运行且 RSS 采样产出真实值、contour ON 构建与测试绿、interactive 补回、采一份 Apple Silicon 性能基线归档。目标计数：macos-clang 默认配置向 Linux 全量对齐（预期 50 量级，确切数字实施期实测落账）。

## 2. 改动点

1. `tests/bench/bench_common.cpp`：RSS 采样改三分支——Linux 维持现状（/proc/self/status + getrusage）；APPLE 用 mach task_info（TASK_VM_INFO 的 resident_size）取当前 RSS，峰值仍用 getrusage 的 ru_maxrss 但单位按 macOS 语义为字节（Linux 为 KB，条件编译换算）；其他平台返回 0 的行为不变。bench_common.h 接口形态不变（M8 规格 §2 已预留）。
2. `tests/CMakeLists.txt`：解除 bench 双 harness 的 APPLE 排除（门控从 UNIX AND NOT APPLE 放宽为 UNIX），bench-long 自定义目标同步解禁；注释更新。
3. 新建 `.github/workflows/ci-macos-contour.yml`：macos-latest，显式 -DZZTERM_WITH_CONTOUR=ON configure + build + ctest，验证 contour 后端 macOS 可构建可测（默认 OFF 不动，3A）。
4. `.github/workflows/ci-macos-clang.yml`：加一步 pip3 install pexpect pyte（补回 ZzTermSmokeInteractive_native；安装失败则该测试维持跳过并在记录中注明，不阻断）。
5. 新建 `.github/workflows/ci-macos-bench.yml`：workflow_dispatch 手动触发，构建并跑 bench-long（10k/100k/1M 矩阵），产物 JSON 经 artifact 上传；下载后本地 commit 进 tests/perf/records/（文件名日期前缀按入库日，m9b 中缀）。push 不触发本 workflow（长跑不拖慢常规 CI）。
6. M9a 规格 §6 排除项对应行划掉（标注已归 M9b 完成）；M8 规格 §2 平台边界行同步更新。

## 3. 验证策略

- 本机基线五项零回归：linux-gcc-debug 50/50、m2-off-check 40/40、m2-shared-check 50/50、linux-clang-fuzz 2/2、doxygen exit 0 零警告。
- CI 六 workflow 绿：既有五 + ci-macos-contour。ci-macos-bench 为手动触发，不进 push 绿名单。
- 红线：third_party/contour 永不改。若 contour 在 Apple Clang 下编译失败且必须改 third_party 才能通过，立即停下回报用户——该情况下 contour ON 项整体退回（规格回退留痕），其余三项照常收尾。
- 已知风险：Apple Clang 的 C++23 支持度、contour configure 期联网拉依赖（CI runner 有网络）、mach 头文件包含形态。全部 CI 日志实证，本机不可复现的标注 CI-only 验证。

## 4. 错误处理

- pexpect/pyte 安装失败：interactive 维持跳过，记录文档注明，不阻断里程碑。
- bench 在 macOS 跑出的 RSS 若恒为 0：说明 mach 采样路径未生效，按 bug 处理修复后重验（RSS 为 0 恰是 bench_common 既有"解析失败返回 0"约定，可观测）。
- contour job 失败：先判是否 third_party 本体问题——是则触红线流程；否则（如 CMake 接线、编译宏）按普通迭代修复。

## 5. 测试策略与门控

- 不设任何 macOS 性能门（4A 既定）；bench 数据只记录。
- macOS 功能门：ci-macos-clang 与 ci-macos-contour 的 ctest 全量绿。
- 本机 Linux 行为零变化：RSS 采样三分支中 Linux 分支代码不动；测试计数仅 macOS 侧变化。

## 6. 排除项（本里程碑不做）

- contour APPLE 默认值翻转（3A：维持 OFF，翻转属单独产品决策）；
- macOS fuzz job（M9a 3B 既定）；
- macOS 性能门控设立（4A 既定）；
- M9b 之后的路线图项（搜索优化、M7a 台账）。

## 7. 记录与收尾

- 记录文档 docs/superpowers/specs/2026-09-28-m9b-macos-bench-contour-record.md：各项实施结果、contour job 首次运行结论、bench 矩阵数据文件名清单与关键读数（对照 M8 Linux 基线简注量级差）、计数对齐实测。
- 收尾惯例：finishing 合并 master（--no-ff），合并后本机全基线回归 + doxygen，contour 保留。
