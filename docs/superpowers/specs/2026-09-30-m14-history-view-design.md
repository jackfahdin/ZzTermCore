# M14 历史访问契约（ZzHistoryView）设计

- 日期：2026-09-30
- 前置：M13 集成 spike 记录（2026-09-29-m13-spike-record.md，判定「行」，历史行访问登记为 P0 缺口 1）
- 分支：contour

## 1. 目的与范围

为前端提供**后端无关的历史行只读访问契约**，补上 M13 登记的 P0 缺口：ZzRenderView 仅覆盖屏幕区，前端无法实现滚动查看历史。

范围内：

- Core 侧：新公共只读视图 ZzHistoryView 的 API、双后端（Native / Contour）实现、测试
- ZzClawTerm spike 侧：spike widget 接滚动条与滚轮做端到端实证（延续 M13「契约经真实前端验证」模式）
- 历史主从分工的决策记录（本文 §6，只记录不实现）

范围外（特性对齐阶段议题，本里程碑不实现）：

- 历史段的选择/复制/搜索 UI（Core 侧选区/搜索已有内部 LineSource 通路，不在本契约重复开口）
- LogEngine 调和落地、越顶回填的实现（仅出决策，见 §6）
- ZzPty 非阻塞配置（M13 缺口 2，独立小项）
- 拷贝语义的公共历史行提取重载（YAGNI——内部 ZzIPhysicalLineSource 已服务选区/搜索）

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | 里程碑范围 | Core 契约 + spike widget 滚动端到端实证 |
| 2 | 历史主从 | Core 历史为显示之主，ZzClawTerm 的 ZzLogEngine 为归档之从 |
| 3 | 越顶回填形态 | 前端拼接两段：Core 契约只管 Core 持有的历史，更老历史由前端从 LogEngine 拉取后自拼于滚动 UI 顶部，Core 不开历史写入口 |
| 4 | API 形态 | 独立只读视图 ZzHistoryView，与 ZzRenderView 平行 |

## 3. 契约形状

新公共头 `include/ZzTerm/HistoryView.h`。ZzTerminal 新增 const 访问器 `historyView()`，双后端均可产出。视图为值语义轻句柄，风格对齐 ZzRenderView / ZzLineView。

API 全集（四件，刻意最小）：

| 方法 | 返回 | 语义 |
| --- | --- | --- |
| `lineCount()` | size_t | 当前可读历史行数；alternate 屏恒 0 |
| `lineAt(index)` | ZzLineView | index 属于区间 [0, lineCount())，0 = 最旧历史行 |
| `droppedLineCount()` | uint64_t | 累计裁掉行数（单调，即内部 totalDropped） |
| `generation()` | uint64_t | 变化代计数：append / clear / reflow / 裁剪时递增 |

行句柄能力：ZzLineView 复用现有类型（cellAt / ZzCellView.text 渲染路径前端零分叉），额外暴露 `wrapped()` 软换行标记，供前端拼接逻辑行与复制时还原长行。cluster 文本经 cellAt 已可达。

### 3.1 行内容语义：借用，不拷贝

`lineAt` 返回借用视图，寿命规则与 RenderView 帧模型同构：**feed / resize / clear / 下一次 lineAt 调用后即失效**。

- Native 后端：直接借 ZzScrollback 的 const 引用（O(1) 定长槽位寻址）
- Contour 后端：历史只能快照读，由视图内部持一行可变缓冲覆写实现（复用选区在用的 historyLineSnapshot 快照口，零新后端机制）

### 3.2 坐标模型

- index 属于 [0, lineCount())，**0 = 最旧历史行**——与内部统一物理行坐标的历史段完全一致
- 绝对行号换算：`绝对行号 = droppedLineCount() + index`，供选区锚点平移，以及「前端拼接两段」时对齐 LogEngine 侧行号（M5 选区已验证的同套模式）

## 4. 边界语义（契约照实文档化）

- **Alternate 屏**：`lineCount()` 恒 0。两后端一致行为（内部 LineSource 已钉住），契约直接吸收；历史数据本身保留，切回 primary 后可见
- **宽度不变量**：历史行宽恒等于终端当前列宽；resize 列变时先历史 reflow 后屏幕 reflow（现有协调顺序不变），reflow 完成前旧视图失效，前端按 generation 变化重取
- **分域 reflow 固有边界**：历史与屏幕分域重组，跨域逻辑行在接缝处拆成两条链——内容零丢失，但折行位置可能与 Contour 不同；契约注释照抄现有免责声明
- **裁剪计数差异**：Contour 后端的 scrolledOutLines 饱和停报语义已在 compat 测试钉住；本契约的 droppedLineCount 双后端语义以「单调累计、用于锚点平移」为准，Contour 侧的精确性以下层能力为限，差异点写进契约注释
- **性能红线**（写入契约注释）：禁止每帧全扫历史；Native lineAt O(1)，Contour 为单行快照拷贝；前端滚动应按需只取可见行
- **线程**：非线程安全，与 ZzTerminal 同线程

## 5. 实现与测试

### 5.1 实现面

- ZzTerminalBackend 多态加 `historyView()` 产出点
- Native：ZzNativeHistoryView 借 ZzScrollback
- Contour：ZzContourHistoryView 经 historyLineSnapshot 快照口填内部行缓冲
- 行宽不变量与 reflow 协调顺序沿用现有机制，本里程碑不改 Scrollback / Reflow 任何行为
- 现有逃生舱 `ZzTerminal::scrollback()`（native-only、可变、标注 Core 内部使用）保持不变，新契约不取代其内部用途

### 5.2 测试

- 双后端单测：行数与 0=最旧顺序、append 后内容正确、裁剪后 droppedLineCount 与 index 换算、alternate 归零、generation 在 append/clear/reflow/裁剪时递增、wrapped 标记、resize 后旧视图失效并重取
- compat 对照：双后端喂同一输入序列，历史行文本一致（scrolledOutLines 饱和差异沿用已钉住豁免）
- 性能：复用现有 scrollback 性能门控（test_perf_scrollback），不为视图新设门控；契约注释承载红线
- 基线全绿：linux-gcc-debug 50/50、m2-off-check 40/40、m2-shared-check 50/50、clang fuzz 3/3、doxygen 零警告

### 5.3 spike 滚动实证（ZzClawTerm 仓 spike/）

spike widget 加滚动条与 wheelEvent：

- 滚动位置 p = 0 为底（纯 RenderView 屏幕区）；上滚时从 historyView 取顶部历史行与屏幕行拼接绘制
- 选择 / 复制 / 搜索仍排除；IME、回显错位、Ctrl+C、resize 内容丢失等已登记 widget bug backlog 不在本批
- 人工验证点：滚轮上滚见历史、回底恢复、resize 后滚动位置钳制不越界、vim（alternate）中滚动禁用

## 6. 历史主从决策（只记录，不实现）

- **Core 历史为显示之主**：滚动、（未来的）选择/复制/搜索的近期历史全部走本契约
- **ZzLogEngine 为归档之从**：热层环（1 万行）/ 温层 mmap（100 万行）/ 冷层 SQLite + ZSTD + FTS5（10GB / 90 天）全部保留，不重复造轮子
- **越顶拼接**：Core 容量耗尽后，更老历史由前端从 LogEngine 拉取（纯文本、无属性），拼在滚动 UI 顶部；Core 契约不开历史写入口，与现行 QTermWidget 越顶回填模式效果等价但职责更清
- 正式调和（Core 滚出行镜像进 LogEngine、拼接 UI、FTS5 搜索 UI）属特性对齐阶段，届时以本契约为显示侧输入

## 7. 对三阶段路线的输入

- 特性对齐阶段 P0（历史行访问）随本里程碑闭环，剩余 P0 面清空；P1 队列：ZzPty 非阻塞配置、IME 中文输入
- 删 ZzTermWidget 风险排序更新：历史/滚动由「最高（依赖 Core 新 API）」降为「中（契约已备，剩前端工程量与越顶拼接）」
