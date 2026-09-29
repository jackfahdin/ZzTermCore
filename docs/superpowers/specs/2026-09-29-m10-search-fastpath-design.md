# M10：搜索优化（扫描路径行级 ASCII 快路）

- 日期：2026-09-29
- 分支：contour
- 前置：M9b 已合并 master——macOS 全量移植完成，CI 六 workflow 绿；M8b 门控五项中仅剩全量 search 不达标（维持 5s 门移交本里程碑）
- 依据：docs/superpowers/specs/2026-09-22-m8-million-line-probe.md §4 归因（瓶颈在逐 cell 文本提取与比对，不在行定位）与 §6.3（search 维持 5s 门，不达标项移交搜索优化里程碑）；docs/superpowers/specs/2026-09-22-m8-million-line-design.md §4（全量 search 不超过 5s，-O0 门控基线档）
- 用户批准决策：1A 扫描路径提速先行（实测不达标再评估增量索引，数据驱动复杂度递增）；2A 维持 -O0 debug 基线设门；3A 不引入多线程；4A search 公开 API 与行为零变化；方案 A（行级 ASCII 快路，B 首字符预筛对门控负载无效已否决）

## 1. 背景与目标

M8/M8b 复测中 facade 轨 1M 档全量 search 为 ascii 5550.92ms / mixed 9691.16ms，对 5s 门缺口 1.11x / 1.94x，是唯一未达标门控项。瓶颈在 buildLineText 的逐 cell 文本提取（switch 分支 + 逐字节 push text/folded/byteToCell/byteToCellEnd 三张表），而非行定位或 find 本身。门控负载特征：ascii 画像全部物理行、mixed 画像约三分之二物理行为纯窄格 ASCII（或仅含尾随 Empty），一条"整行批量提取 + 回映恒等"的快路即可覆盖绝大多数行。本里程碑把 1M 档双画像搜进 5s 门，search 公开 API 与行为零变化。

搜索经 ZzIPhysicalLineSource 抽象，native 与 contour 双后端共用一路，优化自动惠及双后端。

## 2. 改动点（仅 src/terminal/ZzSearch.cpp）

1. buildLineText 加预检+快路分支：逐 cell 快扫，全行满足（宽度 Narrow 且非聚簇且码点不超过 127）或（宽度 Empty）即走快路——text/folded 一次循环紧凑填充（folded 同循环查表折叠），不再逐字节 push 回映表；任一 cell 不满足回退原慢路（宽格续格出现时其 lead 必含宽码点，必然回退，无漏判）。行尾空白修剪两路共用现有逻辑。
2. 命中位置换算：快路行的 byteToCell/byteToCellEnd 为恒等映射（字节位置 i 即格 i、单元末格之后一格即 i+1），zzSearchLines 对快路行直接换算，不建表。
3. 零公开头改动、零新文件、零 CMake 变更。

正确性论证：窄格 ASCII 码点（1-127）的 UTF-8 编码即其本身，Empty 即空格，故快路 text/folded 与慢路逐字节一致；恒等回映是慢路 push 序列的数学等价。既有 50 测试族的搜索语义用例（大小写折叠、宽格、聚簇、行尾修剪、命中不重叠、不跨逻辑行）全部不变即回归保障；bench 的 search_matches==lines 断言兜底。

## 3. 验证策略与门控

- 门控：facade 轨 1M 档全量 search 双画像不超过 5s（-O0 linux-gcc-debug，M8 既定口径不动）。单元轨不设新门（本项为 facade 门控项）。
- 本机基线五项零回归：linux-gcc-debug 50/50、m2-off-check 40/40、m2-shared-check 50/50、linux-clang-fuzz 2/2、doxygen exit 0 零警告。
- bench 复测：10k/100k/1M 矩阵九条全量重跑，产物入库 tests/perf/records/2026-09-29-m10-*.json；probe 文档 §6 续 M10 对照表（前后对照 + 门控判定 + 达标落账）。
- CI 六 workflow 绿（push contour 后确认，含 macOS 双 job——搜索路径为共用代码，macOS 侧同步受益）。

## 4. 错误处理与回退

- 快路正确性由"与慢路输出逐字节一致"约束，实施期若发现任何边界（如码点 0 的 Narrow cell、Empty 与行尾修剪的交互）导致不一致，以慢路语义为准修快路。
- 若复测 mixed 画像仍不达标（缺口 1.94x 是唯一悬念）：带数据回报用户裁定是否上增量索引，不擅自扩大范围；ascii 达标部分照常落账。
- 无 OOM/线程/平台新风险（纯算法分支，内存占用不增）。

## 5. 排除项（本里程碑不做）

- 增量搜索索引（append/trim/reflow 三路径维护）——仅在快路实测不达标后由用户裁定；
- 多线程扫描（3A 既定）；
- search 新公开 API（进度回调、增量搜索接口等，4A 既定）；
- macOS 性能门控（M9 系列 4A 既定：只记录不设门）；
- 搜索语义扩展（跨逻辑行匹配、正则等——M5b 钉死的 v1 语义不动）。

## 6. 记录与收尾

- probe 文档（2026-09-22-m8-million-line-probe.md）§6 续 M10 对照表：复测值、门控判定、五项门控全达标的收尾声明（若达成）。
- 收尾惯例：finishing 合并 master（--no-ff），合并后本机全基线回归 + doxygen，contour 保留。
