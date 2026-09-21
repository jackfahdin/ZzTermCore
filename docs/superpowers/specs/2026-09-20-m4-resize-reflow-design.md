# M4 设计：resize 真 reflow + 10 万行容量与性能门控

- 日期：2026-09-20
- 分支：contour
- 前置：M3b 已合并 master（鼠标/粘贴/焦点输入）；存储层自 M0 起即为 chunked 块式实现
- 架构依据：Architecture.md §6/§14（scrollback 分层演进）、§19（M4：10 万行 scrollback、resize reflow 第一版）；Screen.h 职责注释明确"reflow 属 Terminal/History 协同，M4 里程碑"
- 参考调研：ZzClawTerm/third_party/ZzTermWidget（qtermwidget 深度 fork）四路调研结论，见第 2 节

## 1. 背景与目标

用户已批准范围组合：全量真 reflow（屏幕+历史一起重组）+ 双后端对齐 + 默认容量提升至 10 万行并配 benchmark 门控。实现方案选定"物理行存储 + resize 时重组"（Konsole/Contour 同族），拒绝"逻辑行存储重构"（重写 Screen/Terminal 写入流程，过度重构）与"屏幕物理行 + 历史逻辑行混合"（两套表示双向转换，复杂度高于收益）。

## 2. 旧实现调研结论（决策依据）

ZzTermWidget（Konsole 谱系）三块能力评估：

- scrollback：环形缓冲骨架与 HistoryType 策略接口可借鉴，但行级 QVector 堆分配（源码 FIXME 自承内存投诉）与多张平行表手工同步（commit 史全是此类一致性 bug）明确不学。本项目 M0 的 chunked 块式存储已是更优解，无需改造；
- reflow：旧实现没有真 reflow，缓冲区永不重排，靠显示层 SoftWrap 折叠映射绕行；代价是双坐标系换算散布几十处、约 15 个 fix commit 全是换算 bug，复制/搜索永远按入库宽度断行。此调研反向验证了真 reflow 的正确性；
- selection/search：选区用 line 乘 columns 加 col 的线性 int 索引，与 reflow 天然冲突（重排后索引全废，resize 只能丢选区）。结论：selection/search 必须在真 reflow 之后建设，故排入 M5。

可移植零件（本里程碑采纳）：宽度感知边界钳制规则、"特殊行不参与重排"的钳制思想（当前项目无图形/链接行，规则先只覆盖宽字符）、绝对行号记账思想的最小化移植（stats 累计计数，为 M5 选区坐标铺路）。

## 3. 范围

### 3.1 包含

- 共享纯函数 zzReflowLines（src/screen/Reflow.cpp）：物理行序列 + 新旧列宽 → 重组后行序列，屏幕区与 scrollback 共用；
- native 屏幕区 reflow：ZzScreen 新增网格级 reflow 原语（含光标内容跟随映射）；
- native 历史 reflow：ZzScrollback 接口新增 reflow 虚方法，chunked 实现块内重组；
- ZzScrollbackStats 增加 totalAppended/totalDropped 累计记账（M5 铺路，成本极低）；
- native resize 协调：ZzNativeBackend::resize 在列变化时先历史后屏幕做 rewrap；行增减维持现有语义；
- Contour 对齐：适配层显式钉住 reflow 开关与语义，compat 验证；
- 容量与性能：ZzTerminal 默认 scrollbackMaxLines 10000 → 100000；benchmark 测试含门控断言与 JSON 落盘；
- 测试矩阵与文档更新（见 4.7/4.9）。

### 3.2 明确排除

- selection/copy/search/highlight → M5（依赖本里程碑的坐标地基）；
- Warm(LZ4)/Cold(mmap) 分层、历史读回前插通道、行内 RLE 压缩 → 按真实需求再评估；
- OSC 8 链接行、图像行的重排钳制 → 特性引入时随行实现；
- 行数变化的 resize 语义调整（截断/填充/光标 clamp 维持现状）。

## 4. 现状盘点（已核实的落点）

- ZzLine：wrapped() 标记已在（Line.h:101），logical line 归属规则已在文件头注释钉住（Copy/Search/Reflow 一律基于 logical line）；cluster 侧表随行存续（internCluster，Line.h:116），重组拼行时 cluster 索引需在新行重新登记；
- ZzScrollback/ChunkedScrollback：append/lineAt/setCapacity/clear/stats 齐全，deque 块链每块 256 行，头部整块裁剪；无批量重写接口（本里程碑加 reflow 方法）；
- ZzNativeBackend::resize（ZzNativeBackend.cpp:63）：M0 网格级 resize，注释明说"不做 reflow"；screen_ 与 scrollback_ 同归本类持有，协调逻辑天然落在这里；
- ZzScreen::resize：网格原语（逐行截断/填充、光标 clamp），注释预留 M4 reflow 协同；屏幕行无批量重写口（本里程碑加 reflow 原语）；
- Contour：vtbackend Settings.allowReflowOnResize 默认 true（Settings.hpp:198），即 Contour 后端当前很可能已在 reflow 而 native 没有——现存隐性分歧，本里程碑显式钉住并对齐；Grid 另有 setReflowOnResize 运行期开关（Grid.hpp:632）；
- ZzTerminal 默认容量 10000（Terminal.h:94）；facade 的 screen()/scrollback() 为 native 专用测试口（dynamic_cast 断言），Contour 历史无 facade 读口；
- RenderView 契约：feed/resize 后视图失效（既有约定），Contour 侧 Grid generation bump 已覆盖行身份失效。

## 5. 设计

### 5.1 核心算法 zzReflowLines（纯函数）

- 签名形态（以计划阶段为准）：输入物理行序列（const ZzLine 列表或移动语义）、旧列宽、新列宽，输出重组后的物理行序列；
- 步骤：沿 wrapped 链合并为逻辑行（拼接 cells，cluster 经 internCluster 重新登记到新行）→ 按新列宽重切 → wrapped 标记全部重算；
- 宽字符边界钳制：切分边界落在宽字符中间时边界前移一格，该物理行尾部补空白格，宽字符完整落入下一行；不拆半、不丢字；
- 硬行（wrapped=false）不参与合并：缩窄截断、变宽补空白（与 Konsole/Contour reflow 语义对齐，compat 验证，分歧钉注释）；
- 行尾空白不 trim（合并与重切都保留原样；复制时的行尾空白裁剪是 M5 decoder 层职责）；
- 空行/全空格行、单行超宽、链跨容量边界（历史最旧行是被截断的半个逻辑行）等 case 进测试矩阵。

### 5.2 屏幕区 reflow 与光标映射（ZzScreen 新原语）

- ZzScreen 新增 reflow 原语（列向重组，行列数不变时的纯列变化）：对 Primary/Alternate 两套网格各自重组（备用屏无历史，仅屏幕区）；
- 光标内容跟随：重组前记录光标所在逻辑行标识（链起点偏移 + 行内列偏移），重组后映射回新物理坐标，clamp 到屏幕边界；
- wrapPending：resize 重组后清除（v1 简化，注释钉住，待真实应用反馈再评估精细重建）；
- 滚动区复位全屏、tab stops 重建：沿用现有 resize 语义；
- 重组导致屏幕物理行数变化（合并使行数变少/重切使行数变多）时：多出的行向上溢出经 ScrollOutCallback 入历史（仅 Primary），不足补空行；光标行保持可见。

### 5.3 ZzScrollback 接口扩展

- 新增虚方法 reflow(int newCols)：chunked 实现块内重组（复用 zzReflowLines），容量裁剪语义不变（重组后超容量仍从最旧端裁）；
- ZzScrollbackStats 增加 totalAppended/totalDropped（uint64 累计计数）：append/裁剪/reflow 裁剪处记账；注释钉住"为 M5 绝对行号选区坐标铺路，当前仅供 Inspector/测试"；
- Screen.h 与 Scrollback.h 的职责注释同步更新（M4 落地后"reflow 属 M4 里程碑"等预留措辞改为现状描述）。

### 5.4 native resize 协调顺序（ZzNativeBackend::resize）

1. 参数校验与尺寸未变短路（既有）；
2. 列变化：先 scrollback_.reflow(newCols)，再 screen_ reflow 原语（屏幕区重组，溢出经既有 ScrollOutCallback 回流历史——注意回调进入的是已重组后的历史，顺序自洽）；
3. 行变化：沿用现有 Screen::resize 行向语义（截断/填充/clamp），不触发 rewrap；
4. 行列同时变：先列向 rewrap 全程，再行向调整；
5. 全屏标脏 + scrollbackChanged 置位（历史内容已变，前端需刷新滚动视图）。

### 5.5 Contour 对齐

- 适配层在 Settings 构造处显式设置 allowReflowOnResize = true（钉住语义，不再依赖上游默认值）；
- compat 新增 resize reflow 对照用例：同序列构造历史 + 长行 → 两后端 resize 同新列宽 → 经 RenderView 逐格比对屏幕区；历史区 Contour 无 facade 读口，以 native 单元/集成断言为准，Contour 侧比对屏幕区与 resize 后行为；
- 宽字符边界规则、硬行截断等细节若与 Contour 分歧，按既有惯例在 compat 注释钉住（b 类）。

### 5.6 容量与性能门控

- Terminal.h 默认值 10000 → 100000，注释更新；
- 新增 benchmark 测试（入 ctest，带门控断言，结果 JSON 落盘 tests/perf/records/ 并入库，参照旧项目 perf 实践）：
  - 10 万行 append 吞吐与 lineAt 随机访问；
  - 10 万行历史下 80→120 与 120→40 全量 reflow 耗时，门控小于 200ms（预期远低于此，门控留余量防回归）；
  - 内存门控：10 万行乘 80 列满行 RGB 场景，approxBytes 上限断言（按 sizeof(ZzCell) 实测值定阈值，约 200MB 量级）；
- 行数变化不触发 rewrap，现有 O(rows) 路径不变。

### 5.7 测试

- 单元（新 test_reflow.cpp）：zzReflowLines 矩阵——变宽合并、变窄重切、宽字符边界钳制、空行/全空格行、硬行截断/补空、链跨历史容量截断、cluster 重新登记；ZzScreen reflow 原语——光标映射、wrapPending 清除、备用屏重组、溢出入历史；
- scrollback 单测扩充：reflow 后行序与内容、容量裁剪、totalAppended/totalDropped 记账；
- native 集成：feed 构造历史与长行 → resize 序列 → 逐格断言屏幕与历史；vim 式 1049 进出与 resize 交错；
- compat：5.5 所述对照；
- 冒烟：verify_smoke.py 增加 resize 步骤（含 CJK 长行对齐保持）；
- 回归门：ON 全绿、OFF 全绿、shared 全绿、doxygen 零警告。

### 5.8 错误处理与边界

- newCols 小于等于 0：resize 入口既有校验拦截；
- 容量 0（不保留历史）：reflow 为空操作；
- 重组中途内存分配失败：标准异常安全（先构建新序列再替换，原状态不变）；
- RenderView 失效契约不变：前端在 resize 后重新取视图。

### 5.9 验收（DoD）

- ON/OFF/shared 三配置全绿（含新增用例）；
- doxygen 零警告；
- benchmark 门控全过且 JSON 记录入库；
- 冒烟双后端全过（含新增 resize 步骤）。

## 6. 风险与对策

- **Contour 与 native 边界规则分歧**（宽字符钳制、硬行处理）：compat 对照兜底，分歧按 b 类钉注释；
- **reflow 性能不达标**：zzReflowLines 只对含 wrapped 链的区域做合并重切，硬行区域直通；benchmark 门控卡住回归；
- **屏幕区重组行数变化引发的溢出回流顺序问题**：协调顺序已规定先历史后屏幕，集成测试覆盖交错场景；
- **cluster 索引跨行失效**：纯函数内统一重新登记，单测覆盖含 cluster 行的重组；
- **现有预留注释滞后**：Screen.h/Scrollback.h/Terminal.h 的 M4 预留措辞随实现一并更新，终审检查。

## 7. 里程碑外后续（记录不实施）

- M5：selection/copy/search/highlight（基于 logical line 坐标与 totalAppended/totalDropped 记账）、grapheme 聚簇（UAX #29）；
- Warm/Cold 分层、历史读回前插、RLE 压缩：按真实应用需求评估；
- M2 终审延后族（xterm 细节差异、上游 contour 两项）与 M3b 延后项维持原裁定。
