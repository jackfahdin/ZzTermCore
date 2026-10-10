# M17b 拼接行视图（unwrap view）+ 不换行显示模式设计

- 日期：2026-10-10
- 前置：M17e 扩行填满（2026-10-10-m17e-grow-fill-design.md，tag m17e）
- 分支：contour

## 1. 目的与范围

用户核心诉求（换引擎的根本原因）：列比内容窄时，**自动换行**与
**不换行**两种模式都要支持、运行时可切换；不换行时内容保持逻辑行
不软折、横向滚动条查看溢出部分；无论怎么缩拉窗口内容必须完整；
不影响引擎存储的真实内容。

**定案路径（brainstorming 五项决策全按推荐）**：纯显示层方案。Core
新增只读「拼接行视图」API，把折链拼回完整行 + 横向视口原料；滚动
条与渲染切换在演示层（spike/ZzClawTerm）。引擎写入路径、reflow、
resize（M15-M17e）、搜索、选区语义零改动。

范围内：

- Core：`ZzUnwrapView` 拼接行视图（新公共头，只读，无新存储）——
  拼接访问、坐标换算、内容最大宽度
- Core 单元测试（拼接正确性 / 映射往返 / 边界）
- spike 演示层：不换行模式开关 + 横向滚动条 + 光标跟随 + 选区复制
  完整长行（`ZzCoreViewWidget`，ZzClawTerm 仓库）
- 文档同步（Architecture-v2、API.md）

范围外：

- 引擎写入路径任何改动（不引入 DECAWM-off 式存储层不换行）
- contour 后端行为变更（视图层对双后端通用；contour 无对应概念，
  parity 登记 N/A 说明）
- 不换行模式下的性能优化（拼接缓存等，YAGNI——先正确后快）
- 比例字体（明确排除，等宽单元格前提不变）

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | 实现层位置 | 纯显示层：Core 提供拼接行视图 API，滚动条 UI 在演示层。引擎存储/reflow/resize/搜索/选区零改动。引擎侧写入模式（DECAWM-off 式）剔除：动写入路径与 M16 折链体系风险大，且「切回自动换行」时已有长行的处置是新难题 |
| 2 | 光标跑出横向视口 | 自动跟随：光标拼接列出视口时平移 offset 保证可见（主流编辑器行为） |
| 3 | 不换行模式选区复制 | 完整拼接行（含屏外部分）——复制被截断长行正是关键场景 |
| 4 | 默认模式 | 默认自动换行（现状），不换行 opt-in，运行时切换即时生效（引擎无感） |
| 5 | 横向滚动条归属 | 演示层 QScrollBar 接线；Core 只暴露拼接视图与 maxCellCount，保持零 Qt |

## 3. 命名

既有 `ZzLogicalPos`（Types.h:50）的「逻辑行」= 历史+屏幕统一空间的
**物理行**序号，已被选区/搜索占用。本特性的「拼接后的完整行」另名
**拼接行（stitched line）**，类型 `ZzUnwrapView` / `ZzStitchedLineView`，
避免术语撞车。

## 4. Core：拼接行视图 API

### 4.1 获取与生命周期

`ZzTerminal::unwrapView()` 返回 `const ZzUnwrapView&`（与 renderView/
historyView 同级的视图门面）。feed/resize/reflow 后既有视图与句柄
全部失效（同 RenderView 既有规则）；非线程安全。native/contour 双
后端通用——只依赖 RenderView + HistoryView 的既有暴露。

### 4.2 行序空间

统一空间与 ZzLogicalPos 一致：索引 u < historyCount 为历史行，否则
为屏幕行 u - historyCount（spike 既有消费方式，
ZzCoreViewWidget.cpp:266-268 同构）。拼接视图在此基础上把折链
（wrapped 链，wrapped=true 表示「本行末尾软换行续接下一物理行」，
Line.h:101-107）合并为拼接行。

### 4.3 接口

```cpp
class ZzUnwrapView {
public:
    // 拼接行数（<= 统一空间物理行数；无折链时相等）
    [[nodiscard]] std::size_t lineCount() const;
    // 第 i 条拼接行视图
    [[nodiscard]] ZzStitchedLineView lineAt(std::size_t index) const;
    // 全部拼接行的最大 cellCount（滚动条 range 原料；空时为 0）
    [[nodiscard]] int maxCellCount() const;
    // 引擎坐标 → 拼接坐标（拼接行索引 + 拼接列）
    [[nodiscard]] ZzStitchedPos toStitched(ZzLogicalPos pos) const;
    // 拼接坐标 → 引擎坐标（拼接列落在链内第几物理行、行内哪一格）
    [[nodiscard]] ZzLogicalPos fromStitched(std::int64_t line, int col) const;
};

class ZzStitchedLineView {
public:
    [[nodiscard]] int cellCount() const noexcept;       // 链有效全长（各物理行裁尾后有效段累加）
    [[nodiscard]] ZzCellView cellAt(int col) const;     // 跨链寻址
    [[nodiscard]] std::int64_t sourceLine() const noexcept; // 链头统一行号
    [[nodiscard]] int sourceLineCount() const noexcept;     // 链行数（1 = 无折）
};
```

`ZzStitchedPos{ std::int64_t line; std::int32_t col; }`（新公共类型，
入 Types.h）。越界访问语义对齐 ZzLineView 既有契约（调用方保证），
唯一例外：`fromStitched` 的 col 超出拼接行有效全长时钳到行尾
（选区拖拽越界是正常输入，必须良定义）。

### 4.4 关键语义

- **裁尾拼接**：每个物理行接入链流前裁掉尾部无效格（视图层判据：
  `text.empty()` 且非宽字符续格），与 reflow 链流的逐行裁尾
  （Reflow.cpp:21-26：行尾默认空白恒为填充、码位 0x20 真空格不受
  影响）同口径——拼接行内容 = reflow 眼中的链内容，宽字符跨缝
  两格完整保留。
- **拼接内容跨 resize 稳定**：reflow（M16 系）重排链边界但保逻辑
  内容，拼接行内容因此天然稳定——「缩拉后内容完整」在视图层白拿。
- **跨历史-屏幕缝的链**：链头在历史、续行在屏幕（M16b 跨缝）时照常
  拼接；sourceLine 指向链头（可能在历史区）。
- **空拼接行**：物理空行是独立拼接行（sourceLineCount=1，裁尾后
  cellCount=0），不被吞掉——行数账可与物理空间对账。
- **Alternate 屏**：历史为空，链在屏幕内，照常拼接。

### 4.5 实现形态

纯视图层：惰性拼接索引——首次查询或双代计数
（RenderView.dirtyGeneration + HistoryView.generation）变化时遍历
统一空间物理行按 wrapped 旗标重建（O(行数)；重建分配内存，故
lineCount/maxCellCount/toStitched/fromStitched 不标 noexcept）。
索引是派生缓存非新存储，代计数失效即重建，无一致性负担。dirty
追踪不复用 RenderView 行级 dirty（拼接行与物理行多对一），第一版
前端在不换行模式下整帧重绘（spike 本就如此）。

## 5. 演示层（spike/ZzClawTerm）：不换行模式

- **开关**：运行时切换，快捷键 Ctrl+Shift+U（spike 无配置系统，
  演示层硬编码；ZzClawTerm 集成时可再入设置项）；OFF = 现状渲染
  物理行，ON = unwrapView 渲染。
- **渲染**：一个拼接行占一个显示行，列窗口 `[offset, offset+cols)`；
  可见拼接行数不足屏幕行数时底部留白（屏幕物理行数不变，只是
  显示行变少——折链原本多占的行释放出来）。
- **横向滚动条**：QScrollBar（Qt 接线全在演示层），range =
  max(0, maxCellCount - cols)，单步 = 1 列。
- **光标跟随**：光标拼接列 < offset 或 >= offset+cols 时调 offset
  使光标可见（贴边滚动，不居中）。
- **选区**：鼠标显示坐标 (row, col) →（拼接行 = 当前拼接视口顶 +
  row，拼接列 = col + offset）→ fromStitched → 引擎 setSelection/
  extendSelection；复制文本经引擎既有选区提取（selectedText），得到
  完整拼接行（含屏外部分）。选中格在渲染时反色高亮（spike 最小视觉
  反馈）。
- **搜索高亮**：searchMatch 的 ZzLogicalRange → toStitched 换算
  到拼接坐标显示；命中在屏外横向区时高亮不可见属正常（滚动可见）。
  spike 无搜索 UI，本条为 ZzClawTerm 集成预备，spike 不落地。
- **历史滚动回看**：不换行模式下纵向滚动以**拼接行**为单位——
  最大偏移 = 完全落在历史区的拼接行数（startLine + 链行数 <=
  物理历史行数；跨缝链归屏幕侧），前端按 sourceLine/
  sourceLineCount 与 historyView().lineCount() 自算，Core 不加
  API。自动换行模式下纵向滚动维持既有物理行语义不变。

## 6. 与既有机制的交互

- **正交性**：本特性是纯读取视角，M15/M16/M17a-e 全部引擎语义
  不变；引擎测试基线零改动。
- **选区钉住**：选区存的是 ZzLogicalPos 物理坐标，reflow/resize
  的钉住逻辑（M16b/M17d 已核查）不动；拼接换算只是显示时刻的
  双向映射。
- **M17e 回填**：扩行回填改变物理行布局，拼接行内容不变（链内容
  守恒），不换行显示在 resize 后视觉上零抖动——这正是本特性相对
  物理行渲染的最大卖点。

## 7. parity

contour 库无对应概念（其前端不做不换行显示）。本特性为纯视图层
新增，不产生行为分歧，compat 登记一条 N/A 说明（为什么无可对照）。

## 8. 测试

Core 单测（新文件 tests/unit/test_unwrap_view.cpp）：

1. 无折链时拼接行 = 物理行（lineCount 相等、逐行内容一致）；
2. 多行折链拼接：3 物理行链 → 1 拼接行，cellCount 为累加，
   cellAt 跨缝寻址正确；
3. 跨历史-屏幕缝的链照常拼接，sourceLine 指历史区链头；
4. EAW 宽字符跨缝：拼接后宽字符两格完整；
5. 坐标映射往返：fromStitched∘toStitched = id（链头/链中/链尾
   抽样）；toStitched∘fromStitched = id；
6. maxCellCount：多链混合取最大；空缓冲为 0；
7. Alternate 屏拼接正常；空拼接行（isEmpty 行）不被吞掉；
8. resize/reflow 前后拼接行内容稳定（M16 联动回归）。

回归：既有基线（linux-gcc-debug 61 等五项）零变化——纯新增。

spike 手测金标准（演示层落地后）：

- 长路径 `ll`（48 字符提示符 + 长行）切不换行模式：横向滚动条
  出现、可滚动看全文；
- 选区复制一条屏外有内容的长行 → 粘贴为完整行；
- 任意缩拉窗口（含极窗）→ 拼接内容不变、无残行累积（M17a 豁免
  在物理层照旧，拼接视图自动重拼）；
- 光标在屏外列输入 → 视口跟随。

## 9. 文档同步

- include/ZzTerm/UnwrapView.h：doxygen 注释（新公共头，API.md
  收录口径）；
- docs/API.md：unwrapView 一节（生命周期、坐标空间、与
  ZzLogicalPos 的术语区分）；
- docs/Architecture-v2.md：视图层段落补拼接行视图；parity 登记
  N/A 说明；
- docs/Scrollback-and-Reflow.md：折链表后补一句「拼接行视图
  （M17b）消费同一链结构做不换行显示」。

## 10. 版本与 ABI

`ZzTerminal::unwrapView()` 与新公共头/类型为纯新增符号，不改动既
有符号——按 M14（historyView 引入）先例登记为 ABI 新增，次版本
号递增口径沿用项目版本策略（docs/API.md 版本节），无迁移负担。
