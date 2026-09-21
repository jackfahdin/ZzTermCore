# M7c 聚簇收尾与工程债清理实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 清偿 M7b 留下的四笔小债——zzMutableLine const_cast 收编（ZzScreen 增 internClusterAt 靶向口）、UTF-8 编码四处统一、VS16 非 ExtPic 基字符补探对齐、Grapheme-aware copy/search 固化用例。

**架构：** T1/T2 纯行为保持重构（全测试原样绿为证据）；T3 探针驱动（contour 实测裁定后对齐或收案）；T4 既有 compat 文件加用例。四件相互独立，顺序执行。

**技术栈：** C++20（contour 库 C++23）、ZzScreen 靶向 mutator 先例（putCell/setLineWrapped）、gen_grapheme_break.py 标志位增补先例（T1 的 InCB）、探针/compat 既有管线。

**规格：** docs/superpowers/specs/2026-09-22-m7c-grapheme-cleanup-design.md（9e2fe98 + 修正 87ecca2）。

**第一原则（全计划最高优先级）：** T1/T2 行为保持——47/47 原样绿、断言零改动、任何偏差报 BLOCKED；公开 API 仅允许规格修正 1A 批准的 ZzScreen::internClusterAt 一处增量，其余禁变；third_party/contour 永不改。

**命令约定（全计划通用）：**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
# OFF：cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
# shared：cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
# 文档：doxygen Doxyfile（必须在仓库根运行，exit 0 且零警告）
# fuzz 门控：cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz
```

**已核实的关键事实（计划阶段免查）：**

- ZzScreen 公开类（include/ZzTerm/Screen.h:51 ZZTERM_API）；putCell 声明 :127、lineAt :118——internClusterAt 声明落 putCell 之后；ZzLine::internCluster 公开非常量（Line.h:116）；
- zzMutableLine 现状：ZzNativeBackend.cpp:24-28（const_cast + assert），唯一调用点 :165（merged.setCluster(...)）；
- 编码重复五点核对：canonical 现居 src/terminal/ZzUtf8Encode.h（zzEncodeUtf8 + zzAppendCodePoint，ZzSearch.cpp:4 与 ZzSelectionText.cpp:4 以 "ZzUtf8Encode.h" 引用）；重复实现：ZzNativeRenderView.cpp:7-24 appendUtf8、ZzContourConvert.h:66-86 zzAppendUtf8（含"有意为之"注释 :67-68）、InputEncoder.cpp 局部 appendUtf8、ZzNativeBackend.cpp:31 区域 zzAppendUtf8；五者编码逻辑逐字节同构（均为标准四分支标量值编码，无代理位/越界特殊处理）；
- ZzTermContourBackend include 路径现状（src/backend/contour/CMakeLists.txt）：自身目录 + include/——加 src/ 一行即可共享 src/unicode/Utf8Encode.h；
- gen_grapheme_break.py 的 emoji-data.txt 解析（:83-92）现仅收 Extended_Pictographic；flags 位布局注释 :196（bit0=ExtPic、bit1-2=InCB）——Emoji_Presentation 增补落 bit3；
- 探针/compat 管线同 M7b（cluster_probe 双式 dump、test_cluster_compat 的 Dual + checkCellEqual）。

## 文件结构

| 文件 | 职责 | 任务 |
|---|---|---|
| include/ZzTerm/Screen.h | internClusterAt 声明（公开头，doxygen 纪律） | T1 |
| src/screen/Screen.cpp | internClusterAt 实现（镜像 putCell 行访问） | T1 |
| src/backend/native/ZzNativeBackend.cpp | zzMutableLine 删除、调用点迁移（T1）；zzAppendUtf8 删除（T2） | T1/T2 |
| src/unicode/Utf8Encode.h | canonical 编码口（自 src/terminal 迁入） | T2 |
| src/terminal/ZzUtf8Encode.h | 删除 | T2 |
| src/terminal/ZzSearch.cpp、ZzSelectionText.cpp | include 调整 | T2 |
| src/backend/native/ZzNativeRenderView.cpp、src/input/InputEncoder.cpp | 本地编码器删除换统一口 | T2 |
| src/backend/contour/ZzContourConvert.h、src/backend/contour/CMakeLists.txt | contour 侧统一 + include 路径 | T2 |
| tests/probe/cluster_probe.cpp | VS16 补探例 | T3 |
| scripts/gen_grapheme_break.py、include/ZzTerm/detail/GraphemeBreakData.inc、src/unicode/GraphemeBreak.h/.cpp | Emoji_Presentation 标志（仅 contour 判宽时需要） | T3 |
| src/backend/native/ZzNativeBackend.cpp、tests/unit/test_native_cluster.cpp、tests/unit/test_cluster_compat.cpp | 按实测对齐 | T3 |
| tests/unit/test_selection_compat.cpp、test_search_compat.cpp | 聚簇固化用例 | T4 |
| docs/superpowers/specs/2026-09-21-m7b-parity-probe.md、docs/VT-Xterm-Checklist.md、docs/VT-Xterm-Checklist-v2.md | 裁定留痕与勾选注释补出处 | T3/T4 |

---

### 任务 1：ZzScreen internClusterAt + zzMutableLine 收编（行为保持）

**文件：**
- 修改：`include/ZzTerm/Screen.h`
- 修改：`src/screen/Screen.cpp`
- 修改：`src/backend/native/ZzNativeBackend.cpp`

- [ ] **步骤 1：ZzScreen 声明（公开头，doxygen 纪律）**

include/ZzTerm/Screen.h 的 putCell 声明（:127）之后追加：

```cpp
    /**
     * @brief 向第 row 行的 grapheme cluster 侧表注册 cluster 文本，返回索引。
     *
     * 配合 Cell::setCluster 使用：聚簇续接等场景把新 cluster 串注册进
     * 行侧表后，以索引更新目标格。遵循 putCell/setLineWrapped 的靶向
     * mutator 先例——只开侧表注册一条通道，不开通用可变行口。
     * @param row 物理行号（0 <= row < size().rows）。
     * @param utf8 cluster 的 UTF-8 编码（必须非空）。
     * @return 侧表索引（ZzLine::clusterText 可取回文本）。
     */
    std::uint32_t internClusterAt(int row, std::string_view utf8);
```

（注意 string_view 与 cstdint 两个标准头在 Screen.h 已可用——计划阶段核实项：若无则补 include；注释行文遵守 doxygen 陷阱：行内代码禁尖括号/点开头、禁反斜杠、后禁紧跟顿号。）

- [ ] **步骤 2：Screen.cpp 实现**

镜像 putCell 的行访问方式（计划阶段核实项：putCell 如何取可变行——按其实现同通道 intern）：

```cpp
std::uint32_t ZzScreen::internClusterAt(int row, std::string_view utf8)
{
    // 与 putCell 同通道取可变行；row 界内前提同 lineAt 注释约定。
    return <可变行>(row).internCluster(utf8);
}
```

- [ ] **步骤 3：ZzNativeBackend.cpp 收编**

- 删除 zzMutableLine 函数与其注释块（:19-28 区域）；
- 调用点（:165）：`merged.setCluster(zzMutableLine(screen, prev.row).internCluster(newText));` → `merged.setCluster(screen.internClusterAt(prev.row, newText));`（screen 为 ZzScreen& 成员/参数，按现场变量名）；
- assert 头若因此无引用则一并清理（保留其他在用的 assert 不动）。

- [ ] **步骤 4：行为保持验证**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
grep -rn "const_cast" src/backend/native/ZzNativeBackend.cpp
doxygen Doxyfile
```

预期：47/47 原样绿；const_cast grep 零命中；doxygen 零警告（公开头新增注释的纪律验证）。

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/Screen.h src/screen/Screen.cpp src/backend/native/ZzNativeBackend.cpp
git commit -m "refactor(screen): ZzScreen 增 internClusterAt 收编 putChar 的 const_cast（M7c T1）"
```

---

### 任务 2：UTF-8 编码四处统一（行为保持）

**文件：**
- 创建：`src/unicode/Utf8Encode.h`（canonical，自 src/terminal/ZzUtf8Encode.h 迁入）
- 删除：`src/terminal/ZzUtf8Encode.h`
- 修改：`src/terminal/ZzSearch.cpp`、`src/terminal/ZzSelectionText.cpp`（include 调整）
- 修改：`src/backend/native/ZzNativeRenderView.cpp`、`src/input/InputEncoder.cpp`、`src/backend/native/ZzNativeBackend.cpp`（本地编码器删除）
- 修改：`src/backend/contour/ZzContourConvert.h`、`src/backend/contour/CMakeLists.txt`

- [ ] **步骤 1：canonical 迁移**

src/terminal/ZzUtf8Encode.h 内容原样迁为 src/unicode/Utf8Encode.h，头注释更新（编码/解码同模块：解码器在 ZzTerm/Utf8.h；canonical 供主库与 contour 后端库共享——header-only inline、C++20 写法 C++23 兼容）。删除旧文件。ZzSearch.cpp:4 与 ZzSelectionText.cpp:4 的 include 改为 "unicode/Utf8Encode.h"。

- [ ] **步骤 2：主库三处替换**

- ZzNativeRenderView.cpp：删除匿名命名空间 appendUtf8（:7-24），include "unicode/Utf8Encode.h"，调用点 appendUtf8(out, cp) → zzAppendCodePoint(out, cp)；
- InputEncoder.cpp：删除局部 appendUtf8 与其注释，同法替换；
- ZzNativeBackend.cpp：删除 zzAppendUtf8（:31 区域）与其注释，调用点 → zzAppendCodePoint。

- [ ] **步骤 3：contour 侧统一**

- src/backend/contour/CMakeLists.txt 追加：`target_include_directories(ZzTermContourBackend PRIVATE "${CMAKE_SOURCE_DIR}/src")`；
- ZzContourConvert.h：删除 zzAppendUtf8 与"有意为之"注释（:66-86 区域），include "unicode/Utf8Encode.h"，调用点 → zzAppendCodePoint。

- [ ] **步骤 4：语义核对与行为保持验证**

- 五点编码逻辑逐字节同构已在计划头部核实——实现时仍逐点比对删除前的本地实现与 canonical（特别 InputEncoder 的 <= 边界写法与 < 写法的等价性）；
- 验证：

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
```

预期：ON 47/47（含三份 contour compat——contour 转换路径的编码一致性证据）、OFF 37/37、shared 47/47。

- [ ] **步骤 5：Commit**

```bash
git add src/unicode/Utf8Encode.h src/terminal/ZzUtf8Encode.h src/terminal/ZzSearch.cpp src/terminal/ZzSelectionText.cpp src/backend/native/ZzNativeRenderView.cpp src/input/InputEncoder.cpp src/backend/native/ZzNativeBackend.cpp src/backend/contour/ZzContourConvert.h src/backend/contour/CMakeLists.txt
git commit -m "refactor(unicode): UTF-8 编码四处统一至 unicode/Utf8Encode.h（M7c T2）"
```

---

### 任务 3：VS16 非 ExtPic 基字符补探 + 按实测对齐

**文件：**
- 修改：`tests/probe/cluster_probe.cpp`
- 修改：`docs/superpowers/specs/2026-09-21-m7b-parity-probe.md`（续表留痕）
- 条件修改（contour 判宽时）：`scripts/gen_grapheme_break.py`、`include/ZzTerm/detail/GraphemeBreakData.inc`、`src/unicode/GraphemeBreak.h/.cpp`、`src/backend/native/ZzNativeBackend.cpp`、`tests/unit/test_native_cluster.cpp`、`tests/unit/test_cluster_compat.cpp`

- [ ] **步骤 1：探针补例（整喂/切 feed 双式）**

cluster_probe.cpp 追加用例（UTF-8 字面量）：

```cpp
// V1 数字+VS16   "0\xEF\xB8\x8F" ~ "9\xEF\xB8\x8F"（十个可合并为一例逐格打印）
// V2 #+VS16      "#\xEF\xB8\x8F"
// V3 *+VS16      "*\xEF\xB8\x8F"
// V4 ©+VS16      "\xC2\xA9\xEF\xB8\x8F"     U+00A9（Emoji_Presentation=No 代表）
// V5 ‼+VS16      "\xE2\x80\xBC\xEF\xB8\x8F"  U+203C（Emoji_Presentation=No 代表）
// V6 a+VS16      "a\xEF\xB8\x8F"             （判别例：连纯 ASCII 都判宽则规则为 VS16 普遍变宽）
```

- [ ] **步骤 2：跑探针记录裁定**

```bash
cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/cluster_probe
```

结果逐条记入 parity-probe.md 续表（doxygen 陷阱）。裁定两分：
- **contour 全部保窄** → 记录收案，本任务不再动实现（跳步骤 3/4，直接验证+commit 探针部分）；
- **contour 判宽**（全部或部分）→ 2A 路线进步骤 3。

- [ ] **步骤 3（条件）：扩表对齐**

- 规则形态判定：V6 判宽 → "cluster 含 VS16 即宽"普遍规则（无需新标志位，zzClusterWantsWide 直接扩）；V6 保窄而 V1-V5 部分判宽 → 需 Emoji_Presentation 标志：gen_grapheme_break.py 的 emoji-data.txt 解析补收 Emoji_Presentation（flags bit3），重跑生成 .inc，ZzGraphemeProps 加 emojiPres 成员，zzClusterWantsWide 按实测规则扩表；
- test_native_cluster 与 test_cluster_compat 同步补 V 系用例（期望值引探针实测）；若某 V 例 contour 行为无法对齐，钉 b 类报 DONE_WITH_CONCERNS。

- [ ] **步骤 4（条件）：对齐验证**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
./build/linux-gcc-debug/tests/test_grapheme_break
```

预期：47/47（新用例进既有测试文件不加测试数）；golden 1093/0（生成表 flags 增补不影响既有区间语义——若 golden 有变动必为事故，报 BLOCKED）。

- [ ] **步骤 5：Commit**

```bash
git add tests/probe/cluster_probe.cpp docs/superpowers/specs/2026-09-21-m7b-parity-probe.md <条件改动文件>
git commit -m "test(probe): VS16 非 ExtPic 基字符补探<与对齐/收案>（M7c T3）"
```

（按裁定结果二选一写 message 尾巴；对齐则 type 改 feat(native)。）

---

### 任务 4：copy/search 固化用例 + 全回归门

**文件：**
- 修改：`tests/unit/test_selection_compat.cpp`
- 修改：`tests/unit/test_search_compat.cpp`
- 修改：`docs/VT-Xterm-Checklist.md`、`docs/VT-Xterm-Checklist-v2.md`（勾选注释补固化出处）

- [ ] **步骤 1：test_selection_compat 聚簇复制用例**

仿文件内既有用例形态追加：Dual 喂 "a\xCC\x81XY" 与 "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"，setSelection 覆盖聚簇格，selectedText 双后端逐字节一致（聚簇串完整复制、不丢组合符/ZWJ）。

- [ ] **步骤 2：test_search_compat 聚簇命中用例**

追加：同一脚本双后端 search——模式一为完整聚簇串（a+U+0301 两码点、👨‍👩 整串），模式二为单组合符（U+0301 命中聚簇格）；match 计数与 match 列表双后端一致（checkSearchEqual 既有口）。

- [ ] **步骤 3：Checklist 注释补出处**

v1:97 与 v2:155 的 Grapheme-aware copy/search 勾选注释补"（M7c 固化：test_selection_compat/test_search_compat 聚簇用例）"——保持勾选状态不变，只补物证出处。

- [ ] **步骤 4：全回归门**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
./build/linux-gcc-debug/tests/test_perf_scrollback
```

预期：ON 47/47、OFF 37/37、shared 47/47（用例进既有文件，测试数不变）；fuzz 2/2；doxygen 零警告；append 门控内（M7b 后 151ms 基线）。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_selection_compat.cpp tests/unit/test_search_compat.cpp docs/VT-Xterm-Checklist.md docs/VT-Xterm-Checklist-v2.md
git commit -m "test(compat): 聚簇 copy/search 固化用例与 Checklist 出处补注（M7c T4）"
```

---

## 自检结论（计划编写后）

- **规格覆盖度**：§2.1 收编（T1 三步骤+const_cast grep 物证）、§2.2 编码统一含 contour 侧与语义核对（T2 步骤 2-4）、§2.3 补探与双裁定路径（T3 步骤 2 分叉）、§2.4 固化与回归（T4）。§3 排除项——公开 API 仅 T1 一处批准增量、third_party 零触碰、其余延后族不动。
- **占位符扫描**：T1 的"镜像 putCell 行访问"与 Screen.h include 为计划阶段核实项既定程序（一行级实证）；T3 的"按实测裁定"为规格批准的探针驱动程序。无 TODO/待定。
- **类型一致性**：internClusterAt 签名（std::uint32_t / int / std::string_view）在 T1 声明/实现/调用点一致；zzEncodeUtf8/zzAppendCodePoint 符号名在 canonical 与全部消费点一致；flags bit3 布局在 gen 脚本/.inc 注释/GraphemeBreak.h 间一致（条件分支触发时）。
