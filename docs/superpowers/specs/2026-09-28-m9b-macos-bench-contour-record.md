# M9b macOS 第二波记录

- 日期：2026-09-28
- 分支：contour
- runner：macos-latest（Apple Silicon / Apple Clang / libc++）

## 1. RSS 采样与 bench 解禁

T1（20ddf2a）落地 mach task_info 实现：`tests/bench/bench_common.cpp` 在 `__APPLE__` 分支经 task_info 取 TASK_VM_INFO 的 resident_size 作为当前 RSS，peak 沿用 getrusage 的 ru_maxrss（macOS 上单位为字节，Linux 为 KB，条件编译换算）；接口形态与 M8 设计预留一致，两函数签名未变。同 commit 解除 `tests/CMakeLists.txt` 的 bench APPLE 排除，本机五项基线全绿，macOS 分支 CI-only 验证。

RSS 非 0 实证（artifact 入库 JSON 摘录，`tests/perf/records/2026-09-28-m9b-feed-ascii-1m.json`）：rss_bytes 1124007936、peak_rss_bytes 2742943744。九份 JSON RSS 全部非零，最小 13090816 B（feed-ascii-10k），最大 peak 3062759424 B（`tests/perf/records/2026-09-28-m9b-scrollback-1m.json`），mach 采样确认生效。

macOS 测试计数前后对照（ci-macos-clang）：M9a 终态 36/36 → M9b 终态 40/40（36 + interactive 1 + bench 10k 短跑档 3）。

## 2. contour ON 验证

T2（83bffac / b1639c3）新建 `ci-macos-contour.yml`，显式开启 ZZTERM_WITH_CONTOUR（默认 OFF 维持不动）。首次运行即编译一次通过：Apple Clang 满足 contour 的 C++23 要求，configure 期联网拉第三方依赖顺利，third_party/contour 零改动（红线未触）。ctest 48/48——账目 40 + 9（contour 后端测试）− 1（interactive，该 job 按规格无 pip 步）= 48。run 36410294104。

## 3. interactive 补回

ci-macos-clang 增加 pip 步安装 pexpect/pyte。macos runner 的 Homebrew Python 为 externally-managed（PEP 668），需 `pip install --break-system-packages`（b1639c3，CI-only 修复）。ZzTermSmokeInteractive_native 进入测试名单并实测 Passed（28.11s），macos-clang 达 40/40（run 36410294095）。

## 4. Apple Silicon bench 矩阵

T3（8aee780 / a2e26c8）新建 `ci-macos-bench.yml`（workflow_dispatch 手动触发，upload-artifact v7）。采集 run 36411527133（3m16s 绿），九份 JSON 入库 `tests/perf/records/`：

- 2026-09-28-m9b-scrollback-10k.json / -100k.json / -1m.json
- 2026-09-28-m9b-feed-ascii-10k.json / -100k.json / -1m.json
- 2026-09-28-m9b-feed-mixed-10k.json / -100k.json / -1m.json

1m 档关键读数对照 M8b Linux 基线（x86_64 28 核 -O0，`tests/perf/records/2026-09-28-m8b-*.json`）：

| 指标 | M9b macOS | M8b Linux | 倍数 |
| --- | --- | --- | --- |
| scrollback-1m ascii append | 170.3 ms | 116.2 ms | 1.5x |
| scrollback-1m ascii reflow widen | 5012 ms | 1799 ms | 2.8x |
| feed-ascii-1m feed | 13263 ms | 7390 ms | 1.8x |
| feed-ascii-1m search | 10405 ms | 5551 ms | 1.9x |
| feed-mixed-1m feed | 43575 ms | 21713 ms | 2.0x |
| feed-mixed-1m search | 20369 ms | 9691 ms | 2.1x |
| feed-ascii-1m peak RSS | 2742943744 B | 1589460992 B | 1.7x |
| feed-mixed-1m peak RSS | 2955116544 B | 3256324096 B | 0.9x |

量级简注：feed/search 四行主读数慢约 1.8–2.1 倍（ascii 1.8–1.9x、mixed 2.0–2.1x），属预期（GitHub 共享 runner + 虚拟化，且核心数远少于本机 28 核）；peak RSS 同数量级，无异常膨胀。按 4A 决策只记录不设性能门。

## 5. 最终结果

- CI 六 workflow 全 success（a2e26c8 push 轮）：ubuntu-clang、ubuntu-gcc、windows-msvc、macos-clang、macos-contour、docs；macos-bench 为 dispatch-only 采集 workflow，采集轮绿（36411527133）。
- macOS 测试计数：ci-macos-clang 40/40（36 core + interactive 1 + bench 3）；ci-macos-contour 48/48（contour ON，40 + 9 − 1）。
- 本机五项基线：linux-gcc-debug 50/50、m2-off-check 40/40、m2-shared-check 50/50、linux-clang-fuzz 2/2、doxygen exit 0 零警告（任务 4 终验实测）。
