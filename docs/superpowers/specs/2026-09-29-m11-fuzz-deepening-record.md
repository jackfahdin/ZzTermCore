# M11 M7a 台账清零记录

- 日期：2026-09-29
- 分支：contour
- 交付 commit：T1 `7a0000e`（decoder harness + utf8 种子）、T2 `c6260bb`（深度 fuzz workflow + 种子扩充）

## 1. decoder harness 断言设计

`tests/fuzz/fuzz_utf8.cpp` 直接驱动公开 API `ZzUtf8Decoder`（include/ZzTerm/Utf8.h）的三条独立解码路径，两两断言输出码点流完全一致：

- **路径一（基准）**：`decodeAll()` 整喂，内部调 `finish()` 收尾并复位，输出作为参照序列，并先校验三条不变量。
- **路径二**：首字节定切点（`cut = data[0] % size`，第二段恒非空）分两段 `feed` 加 `finish`。
- **路径三**：逐字节 `feed` 加 `finish`——最细分块下的同一续接不变量。

三条不变量（先作用于基准序列，再经一致性断言覆盖三路）：

1. 输出码点数 ≤ 输入字节数 + 1——每字节至多产一码点，`finish()` 对未完成序列至多追加一个 U+FFFD（Utf8.h finish 文档：未完成序列输出 ZzReplacementChar）；
2. 无码点落入代理区 U+D800–U+DFFF；
3. 无码点超过 U+10FFFF。

语义出处：一致性断言锚定 Utf8.h `feed` 文档承诺"对多次任意切块调用与一次性喂入全量字节，输出的码点流完全一致"；加一上界锚定 `finish` 文档与 `decodeAll` 的 note（调用 finish 收尾并复位）。断言一律 `__builtin_trap`（fuzz preset 为 RelWithDebInfo，NDEBUG 会编译掉 `assert`），撞 trap 时 libFuzzer 落 crash artifact 到 build 下 fuzz-artifacts 目录。只 fuzz 现状，decoder 实现零改动。

smoke 实测（30s，`-max_total_time=30 -print_final_stats=1`）：**3,637,976 轮**（约 11.7 万 exec/s），DONE cov 101 / ft 420，corpus 由 11 条（10 种子 + README）长到 107 条，三条不变量与两路续接一致性在全部轮次中均成立，未撞 trap。

## 2. 种子清单与覆盖意图

`tests/fuzz/corpus/utf8/` 新建 10 条 + 同目录 README（逐条记录意图）：

| 文件 | 覆盖意图 |
| --- | --- |
| `ascii_basic` | 纯 ASCII 基线（单字节直通） |
| `cjk_2x` | 两个合法三字节 CJK |
| `overlong_nul` | overlong 两字节 NUL（非法） |
| `overlong_3byte` | overlong 三字节形态（非法） |
| `surrogate_d800` | 代理区编码（非法） |
| `beyond_u10ffff` | 超 U+10FFFF（非法） |
| `five_byte_seq` | 5 字节序列（RFC 3629 已废除） |
| `truncated_tail` | 三字节截断，驱动 finish 收尾路径 |
| `mixed_cjk_combining` | ASCII + CJK + 组合符 U+0301 混合 |
| `stray_continuations` | 裸续接字节，非法起点重解释路径 |

`tests/fuzz/corpus/parser/` 16 → 19 条：

| 种子 | 覆盖意图 |
| --- | --- |
| `dec_private_combo` | DEC 私有模式组合交替开关（`ESC[?25l` + `ESC[?1049h` 交织），压私有模式 DECSET/DECRST 状态机组合切换 |
| `osc_long_string` | OSC 超长串（256 B payload），压 OSC 字符串缓冲与跨 chunk 续接 |
| `csi_param_overflow` | CSI 参数溢出形态（20 参数大数值 CUP + 扩展色多段 SGR），压参数解析数值钳制与参数槽上限 |

`tests/fuzz/corpus/feed/` 10 → 12 条：

| 种子 | 覆盖意图 |
| --- | --- |
| `resize_jitter_wrap` | resize 高频抖动交织流（8 行 × 120 列），两种宽度下均软换行，压 reflow 与分片 feed 交织 |
| `cjk_sgr_soft_wrap` | CJK 宽字符 + SGR 混合大文本，80 列下连续软换行，压宽字符换行 + 属性跨行 + reflow 组合 |

## 3. 深度 fuzz 首次运行

workflow ci-fuzz-deep.yml（T2 新建，dispatch + 每周日 07:13 UTC，push 不触发）首次 dispatch 实证：

- run：36519529836，https://github.com/jackfahdin/ZzTermCore/actions/runs/36519529836 ，结论 **success**，全程约 91 分钟（三 harness 串行各跑满 1800 秒）。
- 一处已核准偏差：第二 corpus 参数用 GITHUB_WORKSPACE 绝对路径（简报原文相对路径在 cd build 后不存在，libFuzzer 会即时报错退出；改后与 smoke 的 CMAKE_CURRENT_SOURCE_DIR 语义同构）。

| harness | 执行轮次 | exec/s | DONE cov / ft / corp | peak RSS |
| --- | --- | --- | --- | --- |
| parser | 75,646,122 | 42,002 | 160 / 859 / 524 | 441 MB |
| feed | 1,225,548 | 680 | 1329 / 7579 / 2464 | 687 MB |
| utf8 | 104,950,467 | 58,273 | 101 / 419 / 116 | 495 MB |

**未撞 crash**：日志 0 处 ERROR / ASan SUMMARY / Test unit written，upload-artifact 步骤按 `if: failure()` 正确 skipped，run artifacts total_count=0，无需走 crash 处置流程。feed exec/s 低是有态终端 harness 的预期形态（每输入构造终端 + feed + resize），30 分钟仍积累 122 万轮与 2464 单元语料。upload-artifact 版本执行期经 releases 页复核取最新稳定 Major v7（v7.0.1）。

## 4. 台账清零声明

M7a 规格（docs/superpowers/specs/2026-09-21-m7a-fuzz-infrastructure-design.md）两处台账原文——§3.2 排除项"深度 fuzz、nightly 长时 fuzz、OSS-Fuzz 接入 → 基建先行，量级后续评估"与 §6"深度 fuzz / nightly fuzz / OSS-Fuzz：基建跑稳后评估量级"，以及路线图三项 Fuzz target 中 UTF-8 decoder 此前仅经 parser 间接覆盖——其属本里程碑的两项（**深度 fuzz 机制**、**UTF-8 decoder 独立 harness**）至此全部落账：

- **UTF-8 decoder 独立 harness**：`fuzz_utf8` 第三 target 收编（7a0000e），三路一致性 + 三条不变量断言，本机 30s smoke 364 万轮与 CI 深度档 1.05 亿轮均未撞 trap（§1、§3）。
- **深度 fuzz 机制**：ci-fuzz-deep.yml 独立 workflow（c6260bb），dispatch 首跑三 harness 各 30 分钟全绿零 crash（§3）；每周日 07:13 UTC schedule 已登记，push CI 的 30s smoke 形态不变。

nightly 长时 fuzz 与 OSS-Fuzz 接入仍属"量级后续评估"项，M7a 原文并未将其划归本里程碑，不在本次清零范围。

**本机终验五项**（2026-09-29 实测）：linux-gcc-debug 50/50、m2-off-check 40/40、m2-shared-check 50/50、linux-clang-fuzz 3/3、doxygen exit 0 零警告。

**CI 证据**：push c6260bb 轮六 workflow（docs / ubuntu-gcc / ubuntu-clang / windows-msvc / macos-clang / macos-contour）全 success；深度 fuzz dispatch run 36519529836 success。
