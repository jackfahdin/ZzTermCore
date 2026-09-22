# M8 设计：百万行实验（native ZzScrollback benchmark 矩阵）

- 日期：2026-09-22
- 分支：contour
- 前置：M7c 已合并 master（fa0bb15）——聚簇收尾与工程债清理完毕，47/47 全绿，路线图大项无债开启
- 依据：docs/Architecture-v2.md 第 17 节（建立 10k/100k/1M benchmark，记录 feed throughput、history access、reflow、search、RSS/peak memory，优化由 benchmark 数据驱动）；docs/Architecture.md 第 14 节（百万行为扩展目标，禁止朴素历史矩阵）；include/ZzTerm/Scrollback.h 分层演进注释（hot/warm/cold 统计字段已预留）
- 用户批准决策：1A 范围拆分（M8 百万行实验、M9 macOS，各自独立 spec→plan 周期）；2A 测量先行（先立矩阵拿数据，不达标项的优化含在本里程碑内）；3A 仅 native 后端 ZzScrollback（contour 侧历史由 contour 内部 buffer 管理，不归本实验）；4A 新建 tests/bench/ 独立目录（ctest 短跑档注册、长跑档 label 手动触发）；5A 数据写入 probe 文档（人读）；方案一（单元轨 + facade 轨双 harness，共享负载生成与 RSS 采样）；门控 A（规格预设阈值，基线实测后可修正并留痕）

## 1. 背景与目标

ZzScrollback 当前为 chunked RAM 实现，1M 行乘以 80 列乘以 sizeof(Cell)（约 16B）约合 1.3GB 明文，RSS 大概率是首要矛盾；但"大概率"不是数据。本里程碑建立可复测的 10k/100k/1M benchmark 矩阵，双轨测量 native 后端历史子系统，按预设门控判定 1M 达标性：全达标则记录收尾（纯测量里程碑），不达标项按单元轨数据归因后在本里程碑内优化并复测固化。

不预承诺优化方向。候选方向（按实测裁定，可多项或零项）：Cell 内存瘦身、warm LZ4 压缩层、lazy reflow、搜索增量索引。

## 2. 架构与组件（新建 tests/bench/）

- `bench_common.h/.cpp`——共享件：RSS 采样（Linux /proc/self/status 的 VmRSS 当前值 + getrusage 的 ru_maxrss 峰值）、计时段封装、确定性负载生成器（固定种子，产出合成行批与合成 VT 字节流，可控 wrapped 链比例与 CJK 混合比）；
- `zz_bench_scrollback.cpp`——单元轨：直接驱动 ZzScrollback，测 append 吞吐（lines/s）、lineAt 顺序/随机访问（ns/op）、reflow 80 列到 120 列耗时、RSS；
- `zz_bench_feed.cpp`——facade 轨：Terminal + native 后端经 feed() 灌合成 VT 流，测端到端 feed 吞吐（MB/s）、1M 行后的全量 search 耗时、reflow 耗时、RSS/peak；
- CMake 走 tests 现有 GLOB+CONFIGURE_DEPENDS 收编惯例；ctest 注册 10k 短跑档（单档耗时小于 2s，随默认 ctest 运行，测试总数随惯例自然增长）；100k/1M 长跑档打 bench-long label，手动 ctest -L bench-long 触发，不进默认跑；
- 平台边界：RSS 采样为 Linux 专属实现，macOS 适配（M9 移植 bench 时）以条件编译预留接口形态，本里程碑不实现。

## 3. 测量矩阵

- 档位：10k / 100k / 1M 行；
- 指标：append lines/s、feed MB/s、lineAt 顺序/随机 ns/op、search 全扫 ms、reflow ms、RSS 当前值与峰值 MB；
- 负载画像两种：纯 ASCII 短行（80 列无换行链）；wrapped 长链 + CJK 混合（每 3 行一条 wrapped 链，CJK 占比约三成，固定种子可复现）；
- 数值注明 preset（linux-gcc-debug 为 -O0 偏保守，作为门控基线；release 档可附测但不设门）。

## 4. 达标门控（1M 档，基线实测后可修正并留痕）

- RSS 峰值不超过 512MB；
- append 吞吐不低于 10k 档的 50%；
- 全量 search 不超过 5s；
- reflow（80 列到 120 列）不超过 10s；
- feed 吞吐只记录不设门（参考值）。

门控修正规则：仅当基线数据显示预设阈值量级错误（过松或过严一个数量级以上）时允许修正，修正须在 probe 文档留痕原值与新值及理由，并回报用户确认。

## 5. 执行流程（测量波 → 决策点 → 优化波或收尾）

1. 测量波：立 harness、跑全矩阵、数据写入 docs/superpowers/specs/2026-09-22-m8-million-line-probe.md（M7b parity-probe 先例）；
2. 决策点：对照门控逐项判定，全达标 → 跳 4；不达标项 → 用单元轨数据归因定位；
3. 归因结论与优化方案回报用户确认后进入优化波（方向从第 1 节候选清单按实测裁定）；
4. 收尾：规格/计划/probe 文档入库，里程碑关闭；
5. 优化波（条件触发）：走强制流程（最小复现测试 FAIL → 修复 → PASS），复测全矩阵，前后数据对照入 probe 文档。

## 6. 错误处理与测试策略

- harness 健壮性：负载生成器确定性自检（同种子同输出，进短跑档断言）；RSS 解析失败显式报错退出非零；长跑档 OOM 或异常时记录已得数据非崩溃退出；
- 行为保持：测量波零触碰 src/ 与既有测试断言；优化波（若触发）适用全部门控——47/47（含新增短跑档后的自然计数）、OFF/shared 配置、doxygen 零警告、fuzz 双 smoke、既有 perf 门控；
- 本里程碑不新增公开 API（harness 全部在 tests/bench/ 内部，经既有公开接口与 target_sources 先例驱动内部件）；若优化波确需公开 API 增量，回报用户批准后走规格修正（M7c 先例）。

## 7. 明确排除

- contour 后端历史子系统的任何测量与改动；third_party/contour 零触碰；
- macOS 平台适配（M9）；Windows 平台；
- warm LZ4 / cold mmap 分层接口的预建（优化波若裁定要做才实施，且只做数据支持的层）；
- renderer repaint cost 测量（Architecture-v2 第 17 节列举项，本项目 renderer 在 ZzTermWidget 侧，超出本仓库测量面）；
- 公开 API 变化（第 6 节规格修正通道除外）。

## 8. 验收（DoD）

- tests/bench/ 两 harness + 共享件落地，ctest 短跑档随默认跑通过，长跑档 label 可手动触发；
- probe 文档记录完整矩阵数据（档位 × 指标 × 画像），门控逐项判定留痕；
- 全达标：里程碑以纯测量收尾；不达标：优化波前后对照数据入库，复测达标；
- 全回归绿（测量波结束时与优化波结束时各一轮）；
- 规格/计划入库。

## 9. 任务划分

- T1：bench 基建（bench_common 共享件 + CMake 收编 + ctest 注册与 label 分层）；
- T2：单元轨 harness（zz_bench_scrollback）+ 短跑档自检；
- T3：facade 轨 harness（zz_bench_feed）；
- T4：全矩阵测量 + probe 文档 + 门控判定（决策点，回报用户）；
- T5（条件触发）：优化波（归因 → 方案确认 → 实施 → 复测固化）。

T1-T3 顺序执行（T2/T3 依赖 T1 共享件）；T4 是决策点；T5 按 T4 判定触发。

## 10. 里程碑外后续（记录不实施）

- M9：macOS（bench 的 RSS 采样 macOS 适配随 M9）；
- M7a 台账：深度 fuzz、UTF-8 decoder 独立 harness；
- contour 侧历史行为观测（若未来需要，另立实验）。
