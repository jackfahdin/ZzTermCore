# M5a 设计：Selection / Copy（逻辑行坐标 + Core 持有选区）

- 日期：2026-09-21
- 分支：contour
- 前置：M4 已合并 master（resize 真 reflow、默认 10 万行容量、ZzScrollbackStats totalAppended/totalDropped 累计记账）
- 架构依据：Architecture.md §13（Search 不得拼全部历史为大字符串，按 logical line/chunk）、§504-505（Selection anchor 必须保持、Search Match 必须保持）、§894（Logical Position 模型随 M5）、§19（M5 里程碑行）
- 用户批准决策：范围切分 A（M5a = selection/copy 先行，M5b = search/highlight 随后，各自独立规格→计划→执行）；坐标模型 A（逻辑行坐标：逻辑行绝对序号 + 行内列偏移）；状态归属 A（Core 持有选区状态，facade 提供 API，前端只做鼠标换算）；实现方案一（独立 ZzSelection 引擎 + 统一逻辑行只读视图）

## 1. 背景与目标

M4 完成真 reflow 后，选区/复制建设的坐标地基已具备。本里程碑交付：逻辑行坐标模型（ZzLogicalPos）、后端无关的统一逻辑行只读视图、Core 持有的选区状态引擎、纯文本提取复制、facade 选区 API。一套代码双后端共用；鼠标事件换算与高亮渲染均不在本里程碑。

## 2. 旧实现调研结论（决策依据）

ZzTermWidget（Konsole 谱系）selection/copy 评估（M4 阶段四路调研）：

- 最大架构债：选区用 line 乘 columns 加 col 的线性 int 索引，与 reflow 天然冲突（重排后索引全废）。本里程碑的逻辑行坐标正是对此的正面回答；
- 值得移植：Decoder 策略模式的文本提取规则（逐格拼串 + 位置回映）、三层职责分离（模型存选区 / 窗口换算 / 交互层——本项目交互层归前端，Core 只保留模型与提取）、宽字符按宽度步进跳过续格、软换行复制不插换行；
- 不学：同步阻塞搜索（M5b 规避）、每帧全量重建过滤缓冲。

## 3. 范围

### 3.1 包含

- 坐标类型 ZzLogicalPos（逻辑行绝对序号 + 行内列偏移）与锚定不变量（见 5.1）；
- 统一逻辑行只读视图：内部数据源接口 ZzILogicalLineSource，拼接历史区与屏幕区（含接缝软换行续接），native 由 ZzScrollback + ZzScreen 组合实现，Contour 由适配器新增历史只读口实现；
- ZzSelection 选区模型：anchor/extent、规范化半开区间、丢弃平移、clamp、判空；
- 纯文本提取器 zzExtractSelectionText（纯函数，四条提取规则见 5.4）；
- facade 选区 API（见 5.5），resize/scroll 后 Core 自动维护锚点；
- Contour 适配：历史只读口补齐（补齐 facade 历史无读口缺口），锚定映射采用 Grid stable line id（见 5.6）；
- M4 终审遗留观察项①收口：历史/屏幕分域 reflow 的跨域逻辑行接缝续接规则在本里程碑钉死并配 compat 测试；
- 测试矩阵与文档更新（见 5.8/5.9）。

### 3.2 明确排除

- 搜索、高亮渲染、Search Match 保持 → M5b（Architecture.md §504 的 match 保持要求随 M5b 落地）；
- 鼠标/触摸事件换算为选区操作 → 前端职责（facade 只收逻辑坐标）；
- 矩形（块）选区、HTML/富文本复制、OSC 52 → 按真实需求再评估；
- RenderView 对选区状态的表达 → 随 M5b 高亮一并评估；
- grapheme 聚簇（UAX #29）→ 里程碑外后续。

## 4. 现状盘点（已核实的落点）

- facade：ZzTerminal::screen()/scrollback() 为 native 专用测试口（include/ZzTerm/Terminal.h:227/:234），Contour 后端调用抛 std::logic_error；Contour 历史无 facade 读口（本里程碑在适配层补，不改 facade 这两个口的既有契约）；
- Contour 适配层：ZzContourBackend 已有 historyLineCount()（ZzContourBackend.h:70）与主屏 lineWrapped(row)（:72），但无历史行内容读口、无历史行 wrapped 读口；M1b 适配器以 historyLineCount 差值近似 scrolledOutLines（ZzContourBackendAdapter.cpp:35-41），容量满后近似失效的语义差已在 ZzTermChanges 注释钉住；
- Contour Grid stable line id：stableLineIdOf(LineOffset) → int64（Grid.hpp:818）、id 有效性检查（id 低于 stable floor 即已丢弃，:826-829）、stableRangeFloor()（:836）；reflow 的 stable id 记账集中在 rotateBuffersLeft（Grid.cpp:1116-1119 注释），reflow 与 stable id 的交互细节列为计划阶段核实项（见 5.6）；
- ZzLine：wrapped() 标记（Line.h:101 附近），logical line 归属规则已在文件头注释钉住（Copy/Search/Reflow 一律基于 logical line）；cluster 侧表随行存续（internCluster），跨行搬运需重新 intern；
- ZzCell：width() 四态（Empty/Narrow/WideLead/WideContinuation）、isCluster()/clusterIndex()/codePoint()；续格只带 width + 前景背景；
- ZzScrollback：stats() 已含 totalAppended/totalDropped（uint64 累计，M4 落地），不变量"全历史同宽"已钉注释；
- reflow 语义（M4 已批准并验证）：逻辑行集合在 reflow 前后不变；硬行缩窄截断不多行化；历史/屏幕分域重组，跨域逻辑行在接缝断链（观察项①，本里程碑收口）。

## 5. 设计

### 5.1 坐标模型与锚定不变量

```cpp
struct ZzLogicalPos { int64_t line; int32_t col; };
```

- line：统一空间逻辑行序号。0 = 当前最早一条有效逻辑行（历史区头部），屏幕区紧跟其后；append 与滚动不改变已有内容的序号（新内容只加在末尾、屏幕行迁入历史序号不变）；scrollback 头部丢弃时全体有效行序号下移，Core 以丢弃计数平移锚点（平移后为负 clamp 到 0）；序号空间只描述当前有效内容，不做跨丢弃的身份追踪；
- col：逻辑行内单元格偏移（0 起），指向格不指向字符；落在宽字符续格（WideContinuation）上归一到 lead 格；
- 逻辑行判定：物理行 wrapped() 链续接为一条逻辑行；scrollback 末行 wrapped 且屏幕首行是其延续时跨域续接（接缝规则，见 5.3）；
- 锚定不变量：
  - reflow 不改变逻辑行集合（M4 已钉），故 reflow 后锚点 line 天然保持；硬行缩窄截断时越界 col clamp 到行末；
  - scrollback 丢弃：native 以 stats().totalDropped 差值平移锚点 line；锚点平移后为负时 clamp 到 0（当前最早有效行，不复位选区，保持"选区存活到内容真正消失"语义）；
  - Alternate 屏无历史：逻辑空间仅屏幕区，切回 Primary 时选区清空（v1 简化，注释钉住）；
- 选区内部表示：anchor + extent 两个 ZzLogicalPos，对外暴露规范化半开区间 [start, end)。

### 5.2 组件拆分（四件，各自独立可测）

- ZzSelection（src/terminal/）：选区模型——anchor/extent、规范化、丢弃平移、clamp、isEmpty；不依赖任何后端类型；
- ZzILogicalLineSource（内部接口）：logicalLineCount() / logicalLineAt(i)（返回逻辑行的格序列只读视图）；native 实现组合 ZzScrollback + ZzScreen，Contour 实现走适配层新增历史只读口；
- zzExtractSelectionText(source, range)（纯函数）：文本提取，规则见 5.4；
- facade：ZzTerminal 持有 ZzSelection 实例与当前后端的 source 适配；feed/resize 引发的丢弃与 reflow 由 facade 统一通知 ZzSelection 维护锚点。

### 5.3 统一逻辑行只读视图

- 序号空间：历史区逻辑行（0 .. H-1）紧跟屏幕区逻辑行（H .. H+S-1）；屏幕区顶部与历史区尾部之间的软换行续接按 wrapped 链判定合并；
- 接缝规则收口 M4 观察项①：分域 reflow 后跨域逻辑行以"历史末行 wrapped 且屏幕首行非新逻辑行起点"判定续接；该规则写成 compat 用例钉死，与 Contour 行为分歧时按 b 类惯例注释钉住；
- 只读：视图不提供任何写口；feed/resize 后视图失效（沿用 RenderView 失效契约）；
- 性能：logicalLineAt 按逻辑行索引，历史区 O(块) 定位（复用 chunked 结构），禁止全量拼接（Architecture.md §13 精神前置适用）。

### 5.4 提取规则（移植旧实现四条，逐条钉死）

1. 宽字符：按格步进，遇 WideLead 取整字符、跳过续格；
2. cluster：isCluster 格经 clusterIndex/clusterText 取整串，不逐 codePoint 拼；
3. 软换行：逻辑行内部不插换行；选区跨多条逻辑行时行间插单个 `\n`；
4. 行尾空白：每条逻辑行末尾的空白格（空单元格与空格）修剪，行内空白保留；
5. 空选区（anchor == extent）返回空字符串。

### 5.5 facade API

```cpp
void setSelection(ZzLogicalPos anchor, ZzLogicalPos extent);
void extendSelection(ZzLogicalPos extent);
void clearSelection();
bool hasSelection() const;
bool selectionRange(ZzLogicalPos& start, ZzLogicalPos& end) const;  // 规范化区间，供前端画高亮
std::string selectedText() const;                                    // 纯文本，规则见 5.4
```

- 坐标越界一律 clamp 到有效范围，不抛异常；
- hasSelection 语义：选区非空（anchor != extent）即 true，即使内容已被全部丢弃（此时 selectedText 为空串，selectionRange 返回 clamp 后的退化区间）。

### 5.6 Contour 适配

- 适配层新增历史只读口：历史行数（已有 historyLineCount）、历史行内容（格序列只读）、历史行 wrapped 标记；主屏 lineWrapped 已有；均为只读适配，不动 third_party 上游；
- 锚定映射：Contour Grid stable line id（stableLineIdOf / stableRangeFloor / id 有效性检查）为物理行身份；逻辑行以链首物理行 stable id 为代表，丢弃探测经 stableRangeFloor 前移实现；
- 计划阶段核实项：Contour reflow（rotateBuffersLeft 路径）对 stable id 的重排语义——若 reflow 后链首 id 不能稳定代表同一逻辑行，退化为"序号映射 + reflow 时按逻辑行序重建锚点"，并以 compat 用例钉住实际语义（分歧按 b 类注释）；
- Contour 侧历史容量上限由 facade 构造参数统一传入（与 native 默认 10 万行对齐），容量满后的丢弃记账不再依赖 M1b 的差值近似。

### 5.7 错误处理与边界

- 坐标越界 clamp；空选区返回空串 / false；
- 行数变化 resize 不动选区（列变化才触发 reflow 路径）；
- reflow 截断行上越界 col clamp 到行末；
- Alternate 切换清选区（5.1 已钉）；
- 提取过程的内存压力：选区上限即统一空间全量（10 万行级），selectedText 一次性返回 std::string 为 v1 语义（复制场景本就需全量文本），分块提取接口留 M5b 搜索复用时再评估。

### 5.8 测试

- 单元（新 test_selection.cpp）：ZzSelection 规范化/平移/clamp/判空；zzExtractSelectionText 四规则矩阵（宽字符续格、cluster、软换行拼接、行尾修剪、空选区）；接缝续接判定；
- 单元（视图）：native source 拼接（历史+屏幕、跨域逻辑行、容量边界处被截断的半条逻辑行）；Contour source 等价用例（经适配层）；
- 集成：feed 构造历史与长行 → setSelection → resize reflow → 断言 selectionRange 与 selectedText 不变；scroll 丢弃后锚点平移正确；1049 进出清选区；
- compat：双后端同脚本（含跨接缝逻辑行、宽字符、cluster）选区提取文本逐字节一致；
- 回归门：ON 全绿、OFF 全绿、shared 全绿、doxygen 零警告。

### 5.9 验收（DoD）

- 三配置全绿（含新增用例）与 doxygen 零警告；
- compat 双后端选区提取一致（含接缝用例）；
- Architecture.md §894 Logical Position 模型落定为已实现、§19 M5a 行更新、§13/§504 中 selection 相关条款状态更新；v2 Checklist 补 Selection 章；
- M4 观察项①接缝规则 compat 钉死。

## 6. 风险与对策

- **Contour reflow 与 stable id 交互不明**：计划阶段第一核实项；退化路径（序号映射重建）已备好，compat 钉住实际语义；
- **接缝续接与 Contour 分歧**：compat 对照兜底，分歧按 b 类钉注释（既有惯例）；
- **锚点平移记账遗漏路径**（append/reflow/容量裁剪多入口）：记账集中在 facade 通知点 + ZzSelection 内部平移，集成测试覆盖交错场景；
- **提取规则与旧实现行为差**：四规则逐条单测钉死，作为后续前端的既定行为契约；
- **10 万行全选提取的内存峰值**：v1 接受一次性 string（复制语义所需）；若实测成为问题，M5b 评估分块口。

## 7. 里程碑外后续（记录不实施）

- M5b：search/highlight（复用 ZzILogicalLineSource 按 logical line/chunk 扫描，Architecture.md §13）、Search Match 保持（§504）、RenderView 选区/高亮表达；
- 矩形选区、OSC 52、富文本复制：按真实应用需求评估；
- grapheme 聚簇（UAX #29）：独立里程碑候选；
- M2/M3b/M4 终审延后族维持原裁定。
