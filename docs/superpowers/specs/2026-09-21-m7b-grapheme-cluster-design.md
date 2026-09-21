# M7b 设计：Unicode 聚簇（UAX #29）

- 日期：2026-09-21
- 分支：contour
- 前置：M7a 已合并 master（a16f37d）——Fuzz 基建落地，双 smoke 门控全绿，coverage-guided 生效
- 路线图依据：Architecture-v2.md §10 双后端 parity 硬要求；VT-Xterm-Checklist Unicode 节（v1:89-98 Combining/VS/Emoji·ZWJ 等未勾；v2:146-158 全未勾）
- 用户批准决策：自研生成表+状态机路线（M7a 阶段批准，libunicode 仅作参考——OFF 构建完全缺席已实证，native 不能依赖）；完整范围一次到位（M7a 阶段批准：完整 UAX #29 + putChar 集成 + 跨 feed 续接）；本里程碑三件：1A 聚簇宽度以 contour 实测为基准逐条对齐（parity 锚点）；2A 无状态回望 replay 续接（对齐 contour 慢路径结构，不驻留显式状态机）；3A UCD 钉 16.0.0（与 gen_unicode_width.py 一致）

## 1. 背景与目标

双后端存在系统性分歧且无任何测试捕获：同喂 a + 组合符，contour 经 libunicode 聚簇出 1 格 cluster，native 一码点一格出 2 个独立窄格（ZzNativeBackend.cpp:169-221 从不调 internCluster）；三份 compat 套件脚本全 ASCII+CJK，此盲区至今敞开。本里程碑为 native 补齐 UAX #29 聚簇生产能力，以 contour 实测行为为基准达成双后端 parity，并把 Checklist Unicode 节的达成项勾选。M7a 的 fuzz 门控自然转为 putChar 改动的安全网。

## 2. 已核实的关键事实（决策依据）

1. **生成管线惯例**：scripts/gen_unicode_width.py 纯标准库、钉版 16.0.0、输出 include/ZzTerm/detail/UnicodeWidthData.inc，开发期工具不进构建链；M7b 复刻此惯例（数据来源与下载方式计划阶段对齐该脚本）；
2. **cluster 消费链早已就绪**：M5a 提取走 clusterText、M5b 搜索三表经 cluster emit、reflow 已 internCluster 搬运（Reflow.cpp:105）、contour 快照转换已 internCluster（ZzContourBackend.cpp:88）——本里程碑只补生产端，下游零改动；
3. **contour 跨 print 续接机制**：third_party/contour/src/vtbackend/screen/Screen.cpp:529-617 慢路径 grapheme_process + 前格 replay 重建状态——2A 的无状态回望与其同构；
4. **putChar 改造点**：ZzNativeBackend.cpp:169-221 一码点一格，是唯一生产端缺口；
5. **数据需求**：UAX #29 全规则需 GraphemeBreakProperty.txt（GCB 属性）+ emoji-data.txt（Extended_Pictographic，GB11）+ DerivedCoreProperties.txt（InCB，GB9c 连字），官方 GraphemeBreakTest.txt 作 golden。

## 3. 范围

### 3.1 包含

- scripts/gen_grapheme_break.py：纯标准库、钉 UCD 16.0.0、开发期工具不进构建链；输出 include/ZzTerm/detail/GraphemeBreakData.inc（区间表+二分查找，复刻宽度表惯例）；
- src/unicode/GraphemeBreak.h/.cpp segmenter（约 200 行，GB1-GB11 全规则：Extend/Prepend/SpacingMark、ZWJ emoji 序列 GB11、区旗对 GB12/13、连字 GB9c）；双口：全串切分口（golden 用）+ 续接判定口（putChar 回望用）；
- segmenter 单元测试：官方 GraphemeBreakTest.txt 全量 golden 对照 + 属性查询边界用例；
- **contour 探针先行**（先探后改）：喂代表性聚簇脚本量 contour 的渲染宽度与格级行为，产出 parity 裁定表——聚簇宽度规则、软换行边界续接语义、VS15/VS16 呈现等边角全以实测为准，不猜 libunicode 内部规则；探针结果入规格台账；
- **native putChar 聚簇集成**（ZzNativeBackend.cpp:169-221 改造）：无状态回望 replay——新码点到达时取前一格 cluster 文本 + 新码点跑 segmenter 判续；续接并入前一格（既有 internCluster 侧表），否则新格；聚簇宽度按裁定表；热路径保快路径（单窄码点且前格非 cluster 走原路）；
- 跨 feed 续接：无状态方案天然覆盖（前一格 cluster 即持久状态，feed 边界无感）；reset/清屏/交替屏切换无状态清理负担（2A 红利）；
- tests/unit/test_cluster_compat.cpp 双后端逐格对照（组合符/Emoji·ZWJ/区旗/VS15·16/连字/CJK 混合脚本），口径同 M5 三份 compat；
- putChar 集成单元测试（native 后端格级断言：聚簇成格、宽度、续接、跨 feed 续接）；
- Checklist v1:89-98/v2:146-158 达成项勾选（计划阶段逐项核对实际达成度）；Architecture 文档标注；
- 全回归（三配置+doxygen+benchmark 门控不劣化+fuzz 双 smoke）。

### 3.2 明确排除

- contour 侧任何改动——libunicode 的聚簇是 parity 基准而非改造对象；third_party 永不改；
- UCD 版本升级——16.0.0 钉死，与宽度表一致；升级另立里程碑；
- Grapheme-aware 光标移动/编辑类 API——前端职责，core 无此语义；
- 深度 fuzz / nightly fuzz——M7a 已钉基建边界；
- 全部既有终审延后族（M2-M7a）——维持原裁定；
- 公开 API 变化——禁止。

## 4. 设计

### 4.1 生成表与 segmenter（T1，纯新增不动 putChar）

- gen_grapheme_break.py 从三份 UCD 文件生成：码点→GCB 属性枚举（Other/CR/LF/Control/Extend/Prepend/SpacingMark/L/V/T/LV/LVT/RegionalIndicator/ZWJ）+ 两个辅标志（Extended_Pictographic、InCB=Linker/Consonant）；区间表+二分查找，输出 GraphemeBreakData.inc 入 git（同宽度表 .inc 惯例）；
- GraphemeBreak.h/.cpp：按 GB1-GB11 规则顺序求值；全串切分口（输入码点串，输出边界序列，golden 对照用）与续接判定口（输入前格 cluster 码点串 + 新码点，返回续/断，putChar 回望用）——两口共用同一求值核，杜绝两套规则漂移；
- golden：GraphemeBreakTest.txt（16.0.0）全量用例逐条对照；测试数据获取与存放方式计划阶段对齐 gen_unicode_width.py 惯例。

### 4.2 contour 探针与 parity 裁定表（T2，先探后改）

- 探针脚本：经 ZzTerminal（Contour 后端）喂代表性聚簇脚本——组合符序列、Emoji·ZWJ（含多跳）、区旗单发/成对/三连、VS15/VS16 切换、InCB 连字、CJK+组合混合、软换行边界续接、跨 feed 切断——记录格数、格宽、cluster 文本、跨 feed 行为；
- 产出：parity 裁定表（每条：脚本 → contour 实测 → native 对齐目标），写入规格附录或独立台账文件（计划阶段定落点），作为 T3 的实现依据与 T4 compat 的断言来源；
- 探针发现的任何不规则行为（libunicode 非简单查表的边角）逐条实测留痕，不臆测。

### 4.3 native putChar 集成（T3）

- 回望 replay：putChar 新码点到达时，若光标前一格存在（含软换行连续面的格间关系——能否续接以 T2 探针裁定为准），取其 cluster 码点串 + 新码点走续接判定口；续接则把新码点追加进前格 cluster（internCluster 侧表既有口），不推进光标格；否则按现行一码点一格；
- 宽度：聚簇格宽按 T2 裁定表（预期形态：emoji ZWJ 序列、ExtPic+VS16 → 宽格；其余 → 基字符宽度；实测为准）；
- 快路径：单窄码点且前格非 cluster 时走原路，零回望开销；append benchmark 门控验证不劣化；
- 行为零变化边界：纯 ASCII/CJK 无续接码点的全部既有场景逐位不变——44 测试与三份 compat 原样绿即证据。

### 4.4 双后端 compat（T4）

- test_cluster_compat.cpp：同一聚簇脚本喂 Native/Contour 逐格比对（格数、格宽、cluster 文本逐字节），native 以 contour 为基准——口径与断言风格对齐既有三份 compat；脚本集与 T2 探针同源（探针即 compat 的种子）。

### 4.5 验收（DoD）

- GraphemeBreakTest.txt golden 全过；
- cluster compat 双后端零分歧（含跨 feed 切断脚本）；
- 单元/集成测试全绿（44 基线 + 新增）；三份既有 compat 原样绿（行为零变化边界）；
- append benchmark 门控不劣化；fuzz 双 smoke 绿；
- 三配置（44/44、35/35、44/44）+ doxygen 零警告；
- Checklist 达成项勾选、Architecture 标注；规格/计划入库。

### 4.6 任务划分

- T1：gen_grapheme_break.py + GraphemeBreakData.inc + segmenter + golden 测试（纯新增）；
- T2：contour 探针 + parity 裁定表（先探后改，产出 T3 依据）；
- T3：native putChar 聚簇集成 + 集成单元测试 + benchmark 验证；
- T4：test_cluster_compat + Checklist/Architecture 标注 + 全回归门。

## 5. 风险与对策

- **探针发现 libunicode 宽度行为不规则**：裁定表逐条实测留痕（4.2 已钉），宁可表肥不臆测；若出现 native 无法对齐的行为（如 contour 内部特殊渲染），报 DONE_WITH_CONCERNS 由控制者裁定是否记为新一轮 contour 边角族延后项；
- **GB9c 连字需多码点上下文**：回望 replay 取前格 cluster 全文重跑，天然覆盖（2A 结构红利）；
- **putChar 热路径劣化**：快路径 + append 门控双保险（4.3 已钉）；
- **软换行边界续接语义踩坑**：深水区交由 T2 探针裁定后写死，不在 T3 临时拍脑袋；
- **segmenter 规则实现与官方不一致**：GraphemeBreakTest.txt 全量 golden 兜底，不过不进 T3；
- **行为零变化边界被破坏**：44 基线 + 三份 compat 原样绿为硬门（4.3 已钉）。

## 6. 里程碑外后续（记录不实施）

- UCD 版本升级（宽度表与聚簇表需同步升）；
- Grapheme-aware 光标/编辑 API（前端职责）；
- UTF-8 decoder 独立 fuzz harness、深度 fuzz（M7a 台账）；
- 路线图后续组：百万行实验 → macOS。
