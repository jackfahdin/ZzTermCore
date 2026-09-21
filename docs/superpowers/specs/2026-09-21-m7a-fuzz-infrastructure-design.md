# M7a 设计：Fuzz 基建（libFuzzer 双 harness）

- 日期：2026-09-21
- 分支：contour
- 前置：M6 已合并 master（215063c）——性能优化三债清偿，44 测试全绿，benchmark 门控收紧
- 路线图依据：Architecture.md §204「建立 UTF-8 decoder、VT Parser、feed+resize 的 Fuzz target」；VT-Xterm-Checklist 鲁棒性项
- 用户批准决策：1A 里程碑拆分（M7a Fuzz 基建先行、M7b Unicode 聚簇随后，各自走完整规格→计划→SDD 流程）；2A UAX #29 路线（M7b 用：自研生成表+状态机，复刻 gen_unicode_width.py 惯例，不依赖 libunicode）；3A 聚簇范围（M7b 用：完整 UAX #29 + putChar 集成 + 跨 feed 续接一次到位）；4A Fuzz 范围（双 harness + 独立 preset + ctest 30s smoke）；实现方案一

## 1. 背景与目标

Architecture §204 早已点名三类 Fuzz target，至今空白。M7b（Unicode 聚簇）将大改 putChar 文本路径，动手之前先把 Fuzz 门控立起来：既为存量 VT Parser 与 feed+resize 路径提供持续崩溃防护，也为 M7b 的改动备好安全网。本里程碑只建基建与 smoke 门控，不做深度 fuzzing。

## 2. 已核实的关键事实（决策依据）

1. **工具链**：本机无裸 clang++，有 clang++-20（LLVM 20），`-fsanitize=fuzzer,address` 实测编译运行通过；gcc 15.2 无 libFuzzer 支持；CI ubuntu-clang 任务 apt 装 clang 包后即有 clang++ 符号链接（.github/workflows/ci.yml:16），preset 用通用名 clang++ 即可两边通吃，本机以 `cmake --preset linux-clang-fuzz -D CMAKE_CXX_COMPILER=clang++-20` 覆盖（计划阶段核实项结论：命令行 -D 可覆盖 preset cacheVariables）；
2. **入口签名**：ZzVtParser 构造 `ZzVtParser(ZzParserSink*, ZzParserLimits={})`（Parser.h:246），增量入口 `feed(std::string_view)`（Parser.h:259），文件头注释明示可独立 Fuzz（Parser.h:24）；终端入口 `ZzTerminal::feed` 接收 std::span 字节视图（Terminal.h:112）；
3. **构建惯例**：CMakePresets.json 现有 configure/build/test 三组（linux-clang-debug/release、linux-gcc-debug、windows-msvc、macos-clang-debug），均继承 hidden ninja-base；无 fuzz 目录与惯例，本里程碑新建；
4. **CI 现状**：ci.yml 的 ubuntu-clang 任务已具备 clang 工具链，fuzz smoke 挂点直接复用该 job；
5. **排除 contour 的依据**：fuzz 迭代速度优先，且 contour 后端经 CPM 拉取依赖拖慢构建；`ZZTERM_WITH_CONTOUR=OFF` 已有 m2-off-check 实证可行（35 测试绿）。

## 3. 范围

### 3.1 包含

- `ZZTERM_FUZZ` CMake option（默认 OFF）与 `linux-clang-fuzz` preset（configure/build/test 三件套）；
- tests/fuzz/ 双 harness：fuzz_parser.cpp（ZzVtParser 裸解析）、fuzz_feed.cpp（ZzTerminal feed+resize 交织）；
- seed corpus 入库：tests/fuzz/corpus/{parser,feed}/，共 20-40 个小输入；
- ctest smoke：fuzz_parser_smoke / fuzz_feed_smoke 各跑 30 秒，仅 ZZTERM_FUZZ=ON 时注册；
- CI yml 集成：ubuntu-clang job 追加 fuzz smoke 步骤；
- Architecture.md §204 进度标注；规格/计划入库；
- 常规三配置（linux-gcc-debug、OFF、shared）与 doxygen 全回归——ZZTERM_FUZZ=OFF 下构建产物与测试矩阵必须逐位不变。

### 3.2 明确排除

- 深度 fuzz、nightly 长时 fuzz、OSS-Fuzz 接入 → 基建先行，量级后续评估；
- MSVC / macOS fuzz preset → libFuzzer 以 Clang 为前提，本里程碑只立 linux-clang-fuzz；
- contour 后端适配层 fuzz → OFF 构建不含 contour；native 路径先覆盖，contour 适配层候选留后续；
- fuzz 撞出的存量 bug 修复本身 → 按 §4.5 崩溃预案裁定，不被基建任务绑架；
- M7b 全部内容（UAX #29 生成表、状态机、putChar 集成）→ 下一里程碑，本里程碑仅记录已批决策。

## 4. 设计

### 4.1 构建集成（CMake option + preset + CI）

- 根 CMakeLists.txt 增加 `option(ZZTERM_FUZZ "Build libFuzzer targets" OFF)`；ON 时校验 `CMAKE_CXX_COMPILER_ID MATCHES "Clang"`，否则 FATAL_ERROR（gcc 无 libFuzzer，提前硬失败优于链接期玄学报错）；
- 编译选项分层：ON 时全 build 追加 `-fsanitize=address`（库与测试同被 ASan 覆盖，保证 harness 链接一致）；仅 fuzz target 追加 `fuzzer`（即 target 级 `-fsanitize=fuzzer,address`，libFuzzer 的 main 只进 harness）；全构建另加 `-fsanitize=fuzzer-no-link`（只插桩不含 main，coverage-guided 的反馈来源，main 仍只进 harness）；
- preset `linux-clang-fuzz`：继承 ninja-base 惯例，RelWithDebInfo（保留符号便于崩溃定位，优化级别贴近真实运行）、`ZZTERM_FUZZ=ON`、`ZZTERM_WITH_CONTOUR=OFF`（砍 contour 构建换迭代速度）；编译器写通用名 clang/clang++，本机无裸名时以 `cmake --preset linux-clang-fuzz -D CMAKE_CXX_COMPILER=clang++-20` 覆盖（命令行 -D 实证可覆盖 preset cacheVariables），CI 装包后通用名直接可用；
- CI：ci.yml 的 ubuntu-clang job 追加 fuzz preset 配置 + 构建 + 两个 smoke 测试步骤（ctest -R fuzz）；
- Action 版本策略（本里程碑起生效的仓库惯例）：GitHub 官方托管 Runner（ubuntu-latest、windows-latest、macos-latest）默认采用 Action 的最新稳定 Major 版本；self-hosted Runner 升级 Action Major 前必须核查最低 Runner 版本、Node.js runtime 要求与 Breaking Changes。现状核查：ci.yml 全部为 actions/checkout v4（当前最新稳定 Major），合规。

### 4.2 fuzz_parser.cpp（ZzVtParser 裸解析）

- 空 sink：ZzParserSink 基类默认回调全部 no-op（src/parser/Parser.cpp:47-54 已核实），直接使用或空子类皆可——目标是解析器状态机不崩、不断言、不 UB，不校验语义；
- 喂法两段：先整输入一次 feed（完整序列路径）；再取首字节对输入长度取模得切点，切两段先后 feed（跨 chunk 续接路径，OSC 字符串、UTF-8 序列中断续接是解析器最脆的接缝）；
- 每个 LLVMFuzzerTestOneInput 内构造全新 parser 实例（ZzVtParser 轻量、无全局态），输入间零共享。

### 4.3 fuzz_feed.cpp（ZzTerminal feed+resize 交织）

- 一次性构造 `ZzTerminal(80, 24, ZzBackendKind::Native, 1000)` 跨输入复用（终端是有态对象，复用才能触达长程状态路径；构造签名计划阶段以 Terminal.h 实测为准）；
- 首字节派生 chunk 大小（1 + 首字节 % 17，覆盖 1-17 字节粒度），剩余输入按该粒度逐段 feed；
- 交织 resize：取输入中某固定偏移字节的低位为真时，于 feed 中段穿插 `resize(40 + 该字节 % 41, 24)`（列数 40-80 区间抖动）——feed+resize 交织是 Architecture §204 点名的第三类 target，reflow 路径的历史 bug 集中于此；
- 复用约定：每个输入跑完不重置终端（跨输入的状态污染本身就是 fuzz 面的一部分）；若后续实证 ASan 报告因跨输入状态难以复现，再改为按输入重建（计划阶段留出此活口）。

### 4.4 seed corpus（入库）

- tests/fuzz/corpus/parser/：CSI/SGR 常用序列、OSC 标题与超链接、DEC 私有模式（?25、?1049 交替屏）、鼠标 ?1006、bracketed paste 包围段、CJK 与组合符 UTF-8、非法与截断 UTF-8、ESC 开头的孤立/截断序列；
- tests/fuzz/corpus/feed/：上述各类的混合长流、长行软换行（触发 reflow）、交替屏切换 + resize 组合；
- 合计 20-40 个小输入，纯二进制/文本小文件直接入库 git，构建链不依赖生成。

### 4.5 smoke 测试与崩溃预案

- 仅 ZZTERM_FUZZ=ON 时注册两个 ctest 用例：fuzz_parser_smoke、fuzz_feed_smoke，各以 `-max_total_time=30 -print_final_stats=1` 跑 seed corpus 起始的 30 秒 fuzz，TIMEOUT 60 秒；
- 崩溃预案（钉死）：smoke 撞出崩溃 → 留最小复现输入 → 控制者裁定两条路——少量且成因明确的 bug 顺手修掉；成批出现或触及深水区的，记录为后续必修项，崩溃输入加入 corpus，门控语义改为「已知崩溃集合外无新崩溃」（用 -ignore_crashes 或等价机制隔离已知项）。不搞假绿，也不让基建被存量 bug 绑架；
- 崩溃产物：CI 与本地产物目录约定为 build 下 fuzz-artifacts/，gitignore 排除。

### 4.6 验收（DoD）

- `cmake --preset linux-clang-fuzz -D CMAKE_CXX_COMPILER=clang++-20` 全链路本机通过：双 harness 构建、两个 smoke 30 秒跑完、退出码干净（或按崩溃预案记录裁定）；
- 常规三配置零变化：linux-gcc-debug 44/44、OFF 35/35、shared 44/44（ZZTERM_FUZZ=OFF 下新增代码完全缺席，测试矩阵逐位不变）；doxygen 零警告；
- CI yml 挂点生效（yml 语法与步骤序列审查通过，实际 CI 运行以推送后为准）；
- Architecture.md §204 行更新进度标注；规格/计划入库。

### 4.7 任务划分

- T1：构建集成——ZZTERM_FUZZ option + linux-clang-fuzz preset + CI yml 步骤（先立骨架，空 harness 占位验证编译链）；
- T2：fuzz_parser.cpp + parser corpus + fuzz_parser_smoke 注册；
- T3：fuzz_feed.cpp + feed corpus + fuzz_feed_smoke 注册；
- T4：双 smoke 30 秒本机验证（含崩溃预案裁定）+ Architecture §204 标注 + 全回归门。

## 5. 风险与对策

- **存量 bug 被 smoke 撞出导致门控红**：§4.5 预案已钉死裁定路径，T4 预留裁定余量；解析器经 M0 以来 44 测试与 PTY 实测洗礼，预估浅层；
- **clang 版本差**（本机 LLVM 20 vs CI apt 版本）：preset 用通用名、不钉版本；smoke 的 30 秒墙钟与版本无关；
- **ASan 误报/环境噪音**（CI 容器 ASan 偶发问题）：smoke 失败先本地复现甄别，确为环境问题再裁定 CI 步骤的豁免形式，不直接删门控；
- **fuzz target 污染常规构建**：option 默认 OFF，OFF 路径无新增源码参与编译（tests/fuzz/ 目录整体条件收录），三配置回归为证；
- **跨输入复用终端致崩溃难复现**：§4.3 已留活口，实证后裁定按输入重建。

## 6. 里程碑外后续（记录不实施）

- M7b：Unicode 聚簇（UAX #29）——已批决策：自研生成表+状态机（复刻 gen_unicode_width.py 惯例，输入 GraphemeBreakProperty.txt + emoji-data.txt + DerivedCoreProperties.txt，输出 GraphemeBreak 数据头，官方 GraphemeBreakTest.txt golden 对照）；完整范围一次到位（putChar 集成 + 跨 feed 续接 + 双后端 parity）；libunicode 仅作语义参考（OFF 构建完全缺席已实证，native 不能依赖）；
- 深度 fuzz / nightly fuzz / OSS-Fuzz：基建跑稳后评估量级；
- contour 适配层 fuzz target：待 contour 路径稳定后候选；
- 路线图后续组：百万行实验 → macOS。
