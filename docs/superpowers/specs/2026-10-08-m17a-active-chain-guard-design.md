# M17a 光标活动链 reflow 收链保护（readline 重绘兼容）设计

- 日期：2026-10-08
- 前置：M16c 列变 reflow 历史顶补（2026-10-08-m16c-reflow-topfill-design.md）；M16d contour adapter 行列同变拆步修复（commit 9af2bb4）
- 分支：contour

## 1. 目的与范围

消除 reflow 终端与 readline WINCH 重绘之间的内容破坏冲突（用户实测事故）：

**事故链**（spike 留痕 `/tmp/spike-trace.bin` 离线重放实证）：

1. 窗口拖至极窄（14x3）：57 字符提示符折成 5 个物理行，bash/readline
   记住「输入区高 5 行」；
2. 拉回最大化（271x75）：Core reflow 把折链**收链为 1 行**（M16c 顶补
   正确回填，屏幕 75/75 全满）；
3. bash 的 WINCH 重绘仍按旧布局算账，发 `\e[A\e[K`×4「向上 4 行逐行
   擦除再重印」——它以为上面 4 行是自己提示符的碎片，实际已是收链后
   无辜的 listing 内容行，被 `\e[K` 永久清空。光标同时错位到提示符中间。

**contour 对照裁决**（M16d 修复后同留痕重放）：contour 终态与 native
完全一致（cursor=(44,70)、非空 71/75、欠填）——contour 同样不收保护，
两者同为 reflow 终端通病（xterm/VTE 同族问题）。本里程碑的收链保护是
**超越 contour 的差异化语义**，正面回应用户换引擎的根本诉求
「resize 后内容必须完整」。

范围内：

- native 后端 ZzScreen::reflowBuffer：扩列收链时豁免光标所在折链
- 豁免链的光标跟踪与行数账务（顶补/溢出联动）
- Core 单元测试（事故场景 Core 级复刻）与 compat 偏离登记
- 文档同步（Scrollback-and-Reflow.md、API.md、Screen.h 注释）

范围外：

- contour 后端行为变更（第三方冻结； parity 偏离见 §4 声明）
- spike 演示层改动（复验载体，不动）
- Alternate 缓冲（全屏应用自带全量重绘，无 readline 帧假设；不豁免）
- M17b 不换行显示模式与横向滚动条（显示层职责，正交）

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | 修复方向 | 方案 C：先修 contour 崩溃（M16d 已落地）→  contour 净行为裁决 → 光标活动链保护（方案 A） |
| 2 | contour 裁决结论 | contour 同样被 readline 擦除（终态逐点一致）——保护语义属差异化，不与 contour 对齐 |
| 3 | 豁免布局 | 保持**旧宽度拆分**（不改写为新宽重排）：readline 的光标行号假设基于旧布局帧，豁免必须同时保住链的物理行数与光标在帧内的行位 |

## 3. 核心机制：扩列收链豁免

### 3.1 豁免对象识别

reflowBuffer 入口已算光标链坐标（chainStartRow/chainIndex，M16b 起）。
豁免对象 = **光标所在的折链**（含链头到链末全部物理行）。光标不在折链上
（光标行非 wrapped 且前一行非 wrapped）时无豁免对象，走现状路径。

### 3.2 豁免规则

- **仅扩列合并方向豁免**：缩列拆分照常（内容必须适配新窄宽，无豁免余地）；
- **仅 Primary 缓冲**：Alternate 无历史且全屏应用按新尺寸全量重绘；
- 豁免链各行**保持旧宽度布局**（内容左对齐在原片段位置），行存储扩宽到
  newCols（网格宽度不变量不破），wrapped 链旗标原样保持；
- 光标位置不变（同行号语义：片段序号与片段内列均不动）——readline 的
  `\e[A`×N 相对位移因此命中自己的帧行；
- 行数账务自动联动：豁免链贡献 N 行（而非收链后的 1 行），顶补少取
  N-1 行（这些行留在历史里，不被拉进擦除区），溢出分支逻辑不变。

### 3.3 豁免后演进（无残留设计）

- **应用重写即自愈**：readline 重绘把提示符重印为 1 行、擦除剩余片段行
  （其 wrapped 旗标随擦除/重写语义走现有单元格路径），链不复存在，后续
  reflow 无豁免对象；
- **应用不重绘（cat 流式）**：光标恒在链末片段，新输出在末片段旧布局
  列位继续追加，autowrap 到 271 列时才开新行——内容正确，视觉上暂时
  维持旧折点，属可接受的过渡态；
- **再次 resize**：链若仍存在且仍是光标链，继续豁免（幂等）。

### 3.4 已排除的替代方案

「豁免收链但按新宽度重排为相同行数」：行数虽同，光标会被换算到新布局的
链首行（列 57），readline 的 `\e[A`×4 从错误行起算、依然越界擦除——
光标行位必须与 readline 的旧帧假设逐行对齐，故豁免必须保持旧布局（§2-3）。

### 3.5 降级边界（best-effort 声明）

readline 的帧行数按其自身宽度算法（宽字符/tab/不可见序列）估算，与 Core
reflow 的拆分数在 exotic 内容下可能不一致；不一致时保护退化为部分命中
（少保几行），不产生新破坏。ASCII 提示符场景（本次事故）两者恒一致。

## 4. contour parity 偏离声明

本语义起，native 与 contour 后端在「扩列收链且光标在折链上」场景**有意
偏离**：native 保持链拆分，contour 收链。既存 compat 用例中触发该形态的
（testResizeReflowSeamChain 的 60→80 回程等）需改写为偏离登记断言：
native 断言链保持 + 内容完好，contour 断言收链——两侧各自钉住，不再
逐点等同。docs/Architecture.md 与 API.md 的 parity 章节同步登记。

## 5. 测试策略

1. **Core 事故复刻**（native）：合成事故序列——满屏内容 + 窄屏折链
   （提示符折 5 行）→ 扩列 → 模拟 readline 重绘字节
   （`\r\e[K` + `\e[A\e[K`×4 + 重印提示符）→ 断言：内容行零损失、
   无内容空洞、光标落位与 bash 预期一致；
2. **对照组**：同序列去掉豁免对象（光标不在链上）走现状收链路径，
   防止豁免范围外溢；
3. **豁免后演进**：豁免态继续喂流式输出（cat 场景）断言内容连续正确；
4. **compat 偏离登记**：§4 所列用例改写；
5. **留痕重放验证**：`/tmp/zz-resize-repro/replay` 对 spike-trace.bin
   的 native 终态应由「非空 71/75 欠填」变为「擦除命中碎片行、listing
   尾行完好」（作为人工验证步骤记录，不入库——留痕文件属会话产物）。

## 6. 文档与发布

- docs/Scrollback-and-Reflow.md：reflow 章节补「光标活动链保护」小节；
- docs/API.md：resize 语义段 + 版本节追加 M17a 条目（ZzScreen 无签名
  变化、行为语义变化；contour parity 偏离登记）；
- include/ZzTerm/Screen.h：reflow 注释同步；
- 收尾：tag m17a，contour 分支推送，CI 7 workflow 全绿确认；
- spike 复验：重建 spike 后用户实测拖拽（含极窄拖拽）确认内容零损失。
