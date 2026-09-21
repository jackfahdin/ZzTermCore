# M7c 设计：聚簇收尾与工程债清理

- 日期：2026-09-22
- 分支：contour
- 前置：M7b 已合并 master（50beaab）——UAX #29 segmenter + putChar 无状态回望续接 + 双后端 parity，47/47 全绿
- 依据：M7b 终审延后台账（VS16 非 ExtPic 基字符未探、copy/search 勾选无固化物证、zzMutableLine const_cast 待收编）；M6 起既定延后族（UTF-8 编码重复统一——M7b putChar 就地编码器使其增为四处）
- 用户批准决策：1A 四件全做（收编 + 编码统一 + VS16 补探 + 固化用例）；2A VS16 补探若 contour 判宽则本里程碑对齐（扩表 + compat 用例，parity 不欠新账）

## 1. 背景与目标

M7b 交付聚簇生产能力的同时留下四笔小债：zzMutableLine 的 const_cast 先例（不及时收编会被后续代码模仿）、UTF-8 编码第四处重复、VS16 非 ExtPic 基字符的 parity 盲区、copy/search 勾选缺门控物证。本里程碑在路线图大项（百万行实验）之前一次性清偿，全部为行为保持或探针驱动的小改动。

## 2. 四件细目

### 2.1 T1：zzMutableLine 收编（行为保持）

- ZzScreen 增最小靶向口 `std::uint32_t internClusterAt(int row, std::string_view utf8)`——只开"向某行侧表注册 cluster"一条通道，不开通用可变行口（行不变量仍集中在 Screen）；行号越界按 Screen 既有约定处理（计划阶段对齐 lineAt 的界内前提）；
- ZzNativeBackend.cpp 的 zzMutableLine（const_cast）与 assert 看护整体删除，续接落格改经 internClusterAt + putCell；
- 行为保持证据：47/47 原样绿、断言零改动。

### 2.2 T2：UTF-8 编码四统一（行为保持）

- 四处重复：src/backend/native/ZzNativeRenderView.cpp:13、src/backend/contour/ZzContourConvert.h:73、src/input/InputEncoder.cpp:20（路径以实际为准）、src/backend/native/ZzNativeBackend.cpp（M7b 新增）；
- 统一口落点计划阶段核实：src/terminal/ZzUtf8Encode.h 现状可复用则以它为准（必要时迁 src/unicode/ 与解码器 Utf8 同模块，编码/解码毗邻）；迁动涉及 include 调整但与构建链兼容（GLOB 收编）；
- 替换前逐点核对语义（代理位/越界码点/返回值约定），有任何一点语义不同则该点保持原样并注释钉住，不强行统一；
- 行为保持证据：47/47 原样绿（含 contour 转换路径的 compat 三份）。

### 2.3 T3：VS16 非 ExtPic 基字符补探 + 对齐

- 探针补例（tests/probe/cluster_probe.cpp，整喂/切 feed 双式）：数字 0-9 / # / * + VS16（无 keycap 环绕）、代表性 Emoji_Presentation=No 基字符 + VS16；
- contour 判宽（2A 路线）：zzClusterWantsWide 按实测扩表——若判定需要 Emoji_Presentation 标志而生成表无此位，gen_grapheme_break.py 从 emoji-data.txt 增补标志并重生成 .inc（同 T1 的 InCB 增补先例）；test_native_cluster 与 test_cluster_compat 同步用例；
- contour 不判宽：裁定表记录收案，不动实现；
- 裁定逐条留痕进 docs/superpowers/specs/2026-09-21-m7b-parity-probe.md（续表）。

### 2.4 T4：copy/search 固化 + 全回归门

- test_selection_compat 加聚簇复制用例：a+U+0301、👨‍👩 聚簇的 selectedText 双后端逐字节一致；
- test_search_compat 加聚簇命中用例：整聚簇模式与含组合符模式的 match 列表双后端一致；
- "Grapheme-aware copy/search"由手工实证升级为门控物证（Checklist 勾选维持，注释可补固化出处）；
- 全回归：三配置 + doxygen + fuzz 双 smoke + perf（append 门控）。

## 3. 明确排除

- 四件之外的任何行为语义变化；
- VS16 之外的 contour 边角族新探（I-1/I-2/I-6/I-7 维持 b 类原裁定）；
- 公开 API 变化（ZzScreen/ZzUtf8Encode 均为内部件）；
- third_party/contour 任何改动；
- 既有终审延后族其余成员——维持原裁定。

## 4. 验收（DoD）

- T1：const_cast 归零（grep 物证），47/47 原样绿；
- T2：UTF-8 编码单口（grep 物证：四处重复实现删除），47/47 原样绿；
- T3：探针记录 + 裁定留痕；若对齐则扩表与双测试用例齐全；
- T4：固化用例进 compat 门控；三配置 + doxygen + fuzz + perf 全绿；
- 规格/计划入库。

## 5. 任务划分

- T1：ZzScreen internClusterAt + zzMutableLine 收编；
- T2：UTF-8 编码四统一；
- T3：VS16 补探 + 按实测对齐；
- T4：copy/search 固化 + 全回归门。

四件相互独立，顺序执行；任何一件发现行为偏差即停下报 BLOCKED（行为保持是本里程碑第一原则）。

## 6. 里程碑外后续（记录不实施）

- 路线图大项：百万行实验 → macOS（下一里程碑组）；
- M7a 台账：深度 fuzz、UTF-8 decoder 独立 harness；
- 既有 contour 边角族（b 类四组 + 更早族）维持原裁定。
