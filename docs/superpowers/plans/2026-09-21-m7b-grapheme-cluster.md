# M7b Unicode 聚簇（UAX #29）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为 native 后端补齐 UAX #29 聚簇生产能力——自研生成表+segmenter（golden 全量验证）、无状态回望续接 putChar 集成、以 contour 实测为基准的双后端 parity，堵上"同喂 a+组合符 contour 1 格 native 2 格"的系统性分歧。

**架构：** T1 纯新增（生成脚本+数据头+segmenter+golden，不碰 putChar）；T2 contour 探针量化 parity 基准（先探后改）；T3 改造唯一生产端缺口 putChar（ZzNativeBackend.cpp:169-221）；T4 双后端 compat 收口。cluster 消费链（M5a 提取/M5b 搜索/reflow/renderView）早已就绪，下游零改动。

**技术栈：** Python 生成脚本（纯标准库、urllib 钉版下载、原子替换，复刻 gen_unicode_width.py）、UCD 16.0.0 四份文件、C++20 segmenter（区间表+二分查找）、ZzTerminal 公开 API 探针、ctest/doxygen/fuzz 门控。

**规格：** docs/superpowers/specs/2026-09-21-m7b-grapheme-cluster-design.md（507182c）。

**第一原则（全计划最高优先级）：** 行为零变化边界——纯 ASCII/CJK 无续接码点的全部既有场景逐位不变：44 基线测试与三份既有 compat 原样绿即证据，任何需要改断言才能过的改动即越界，停下来报告 BLOCKED。公开 API 禁变；third_party/contour 永不改；contour 后端代码不改（它是 parity 基准）。

**命令约定（全计划通用）：**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
# OFF：cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
# shared：cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
# 文档：doxygen Doxyfile（必须在仓库根运行，exit 0 且零警告）
# fuzz 门控：cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz
```

**已核实的关键事实（计划阶段免查）：**

- gen_unicode_width.py 惯例：urllib 下载钉版 UCD → 解析 → 区间合并 → 原子写 include/ZzTerm/detail/*.inc（scripts/gen_unicode_width.py:1-86）；
- putChar 现状（ZzNativeBackend.cpp:169-221）：zzCellWidthOf 定宽 → wrap-pending → 尾列宽字符换行 → putCell → 光标推进；clearWidePairAt 处理宽格对覆盖；
- cluster 机制：ZzLine::internCluster 不查重直接追加（Line.cpp:67-72）、clusterText 按索引读（:74-79）；Cell::setCluster(index)/isCluster()/clusterIndex()（Cell.h:344-388）；ZzNativeRenderView 已对 isCluster 格输出 clusterText（ZzNativeRenderView.cpp:62-63）——putChar 一旦 intern，renderView/compat/selection 自动点亮；
- contour 快照转换的 cluster 写法先例（ZzContourBackend.cpp:88 区域）：clusterSize>1 → setCluster(internCluster(toUtf8))，==1 → setCodePoint；
- compat 测试模板：test_backend_compat.cpp 的 Dual 结构 + checkCellEqual（renderView().lineAt(r).cellAt(c) 比 text/width/前景/背景/属性）；contour 专属测试需 REMOVE_ITEM + if(TARGET ZzTermContourBackend) 条件注册；
- 内部符号测试 shared 构建先例：target_sources(test_xxx PRIVATE "${CMAKE_SOURCE_DIR}/src/...cpp")（tests/CMakeLists.txt:41-64 区域）；
- ZzTerminal 构造与 feed 签名同 M7a 计划（Terminal.h:94/112）；内部头经 src/ 根引用（如 "unicode/GraphemeBreak.h"）。

## 文件结构

| 文件 | 职责 | 任务 |
|---|---|---|
| scripts/gen_grapheme_break.py | 钉版下载四份 UCD → GraphemeBreakData.inc + tests/data/GraphemeBreakTest.txt | T1 |
| include/ZzTerm/detail/GraphemeBreakData.inc | 生成区间表（统一边界，打包属性） | T1 |
| src/unicode/GraphemeBreak.h/.cpp | GCB 属性查询 + GB1-GB11 求值核 + 双口 | T1 |
| tests/unit/test_grapheme_break.cpp | 官方 golden 全量对照 + 属性边界用例 | T1 |
| tests/data/GraphemeBreakTest.txt | 官方测试数据（16.0.0，入库） | T1 |
| tests/CMakeLists.txt | golden 测试数据路径宏 + shared target_sources | T1 |
| tests/probe/cluster_probe.cpp | contour 探针（可执行，不注册 ctest） | T2 |
| docs/superpowers/specs/2026-09-21-m7b-parity-probe.md | 探针结果与 parity 裁定表 | T2 |
| src/backend/native/ZzNativeBackend.cpp | putChar 聚簇续接集成 | T3 |
| tests/unit/test_native_cluster.cpp | putChar 集成格级断言（公开 API） | T3 |
| tests/unit/test_cluster_compat.cpp | 双后端聚簇逐格 compat | T4 |
| docs/VT-Xterm-Checklist.md、docs/Architecture-v2.md | 达成项勾选与标注 | T4 |

---

### 任务 1：生成管线 + segmenter + golden（纯新增）

**文件：**
- 创建：`scripts/gen_grapheme_break.py`
- 创建：`include/ZzTerm/detail/GraphemeBreakData.inc`（生成物，入 git）
- 创建：`src/unicode/GraphemeBreak.h`、`src/unicode/GraphemeBreak.cpp`
- 创建：`tests/unit/test_grapheme_break.cpp`、`tests/data/GraphemeBreakTest.txt`
- 修改：`tests/CMakeLists.txt`

- [ ] **步骤 1：gen_grapheme_break.py（复刻 gen_unicode_width.py 骨架）**

钉 UNICODE_VERSION = "16.0.0"，四份数据源：

```
https://www.unicode.org/Public/16.0.0/ucd/auxiliary/GraphemeBreakProperty.txt
https://www.unicode.org/Public/16.0.0/ucd/emoji/emoji-data.txt
https://www.unicode.org/Public/16.0.0/ucd/DerivedCoreProperties.txt
https://www.unicode.org/Public/16.0.0/ucd/auxiliary/GraphemeBreakTest.txt
```

解析与生成规则：
- GraphemeBreakProperty.txt：收录全部 GCB 类（CR/LF/Control/Extend/Prepend/SpacingMark/L/V/T/LV/LVT/Regional_Indicator/ZWJ；未列出码位默认 Other 不入表）；
- emoji-data.txt：仅收 Extended_Pictographic 区间；
- DerivedCoreProperties.txt：仅收 InCB; Linker 与 InCB; Consonant 区间（InCB; Extend 已由 GCB=Extend 表达，不收）；
- **统一边界合并**：三份属性区间取边界并集，每个统一区间打包 `gcb`（4 位枚举）+ `flags`（bit0=ExtPic，bit1-2=InCB 0=None/1=Linker/2=Consonant）；校验区间互不重叠（复刻 gen_unicode_width.py:55-58 校验段）；
- 输出 include/ZzTerm/detail/GraphemeBreakData.inc：头注释钉数据源四个 URL 与版本日期；表形 `inline constexpr std::array<ZzGcbInterval, N> kZzGcbIntervals {{ ... }};`（ZzGcbInterval 在 GraphemeBreak.h 定义：`std::uint32_t lo, hi; std::uint8_t gcb; std::uint8_t flags;`）；
- GraphemeBreakTest.txt 原样落盘 tests/data/GraphemeBreakTest.txt（golden 数据源，入 git；下载失败即报错退出，不写半截文件——原子替换惯例）；
- 运行 `python3 scripts/gen_grapheme_break.py` 产出两份文件（需要网络；若本机网络不可达，报 BLOCKED 由控制者裁定数据获取替代途径）。

- [ ] **步骤 2：GraphemeBreak.h/.cpp（属性查询 + 求值核 + 双口）**

```cpp
// src/unicode/GraphemeBreak.h（内部头，不进公开 API）
enum class ZzGcb : std::uint8_t {
    Other, CR, LF, Control, Extend, Prepend, SpacingMark,
    L, V, T, LV, LVT, RegionalIndicator, ZWJ
};
enum class ZzIncb : std::uint8_t { None, Linker, Consonant };
struct ZzGraphemeProps { ZzGcb gcb; bool extPic; ZzIncb incb; };

// 单码点属性（二分查找 GraphemeBreakData.inc；未列出 → Other/false/None）。
[[nodiscard]] ZzGraphemeProps zzGraphemePropsOf(char32_t cp) noexcept;

// 全串边界（golden 用）：out 长度 n+1，out[i]=true 表示 cps[i-1] 与 cps[i]
// 之间有聚簇边界；out[0] 与 out[n] 恒 true（GB1/GB2）。
void zzGraphemeBreaks(std::u32string_view cps, std::vector<bool>& out);

// 续接判定（putChar 回望用）：prevCluster 为前格 cluster 码点串（非空），
// next 为新码点；true = 续接并入前格（无边界），false = 断开新格。
[[nodiscard]] bool zzGraphemeContinues(std::u32string_view prevCluster, char32_t next) noexcept;
```

实现纪律：
- **单一求值核**：内部 `bool zzIsGraphemeBoundary(const ZzGraphemeProps* seq, std::size_t n, std::size_t pos)` 判定 seq 内 pos 处是否断开；按 GB 规则顺序求值——GB3（CR×LF）、GB4/GB5（Control|CR|LF 两侧必断）、GB6-GB8（Hangul L/V/T/LV/LVT）、GB9（×Extend/×ZWJ）、GB9a（×SpacingMark）、GB9b（Prepend×）、GB9c（左扫 Consonant [Linker Extend]* Linker [Extend]* 模式 × Consonant；rev 45：字母表为 InCB=Extend ∪ InCB=Linker，ZWJ 经 InCB=Extend 进入，InCB=Extend ⊊ GCB=Extend 真子集故须显式入表——T1 修复波对齐）、GB11（左扫 ExtPic Extend* ZWJ × ExtPic）、GB12/13（RI 对：边界前连续 RI 计数为奇则续）、GB999（断）；以上皆不命中则断。左扫只在窗口内进行（GB9c/GB11 的左上下文需求由窗口全文满足）；
- zzGraphemeBreaks：对 i ∈ [1, n-1] 逐边界调求值核（O(n·k)，golden 场景 n 小，无性能要求）；
- zzGraphemeContinues：栈上小缓冲拼接 prevCluster + next（cluster 很短，上限可断言 64），调求值核判 prevCluster.size() 处边界，取反返回；
- .cpp 顶部 include "unicode/GraphemeBreak.h" 与 "ZzTerm/detail/GraphemeBreakData.inc"（数据结构注释：区间按 lo 升序互不重叠，供二分查找）。

- [ ] **步骤 3：golden 测试 test_grapheme_break.cpp**

- 解析 tests/data/GraphemeBreakTest.txt（路径经编译宏 ZZ_GRAPHEME_TEST_DATA 注入）：行格式 `÷ 0020 × 0308 ÷ # 注释`——÷ 断、× 续，码点十六进制；逐行构造码点串，调 zzGraphemeBreaks 对照全部边界（含首尾）；统计用例总数与失败明细（失败打印行号与期望/实际），全量跑完断言零失败（16.0.0 约 1000+ 用例）；
- 属性查询边界用例：区间端点码点（如 U+0300 是 Extend、U+1F1FA 是 RegionalIndicator、U+200D 是 ZWJ）、未列出码位（如 U+0041 'a'）默认 Other、ExtPic 样例（U+2764）、InCB Linker 样例（U+094D）与 Consonant 样例（U+0915）；
- zzGraphemeContinues 直接用例：{"a"} + U+0301 → 续；{U+1F468, U+200D} + U+1F469 → 续（GB11）；{U+0061, U+200D} + U+1F469 → 断（ZWJ 前非 ExtPic，GB11 不命中）；{U+1F1FA} + U+1F1F8 → 续；{U+1F1FA, U+1F1F8} + U+1F1FA → 断；{U+0915, U+094D} + U+0937 → 续（GB9c）。

- [ ] **步骤 4：tests/CMakeLists.txt 接线**

```cmake
# M7b：聚簇 golden 测试的数据路径与 shared 构建内部符号。
if(TARGET test_grapheme_break)
    target_compile_definitions(test_grapheme_break
        PRIVATE ZZ_GRAPHEME_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data/GraphemeBreakTest.txt")
    target_sources(test_grapheme_break PRIVATE "${CMAKE_SOURCE_DIR}/src/unicode/GraphemeBreak.cpp")
endif()
```

- [ ] **步骤 5：验证**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/test_grapheme_break
ctest --preset linux-gcc-debug
cmake --build build/m2-shared-check && ./build/m2-shared-check/tests/test_grapheme_break
```

预期：golden 零失败（打印用例总数）；45/45 全绿（44 基线 + test_grapheme_break）；shared 构建 golden 同样过（target_sources 生效）。**注意**：本任务纯新增，putChar 未动，44 基线必须原样绿。

- [ ] **步骤 6：Commit**

```bash
git add scripts/gen_grapheme_break.py include/ZzTerm/detail/GraphemeBreakData.inc src/unicode/GraphemeBreak.h src/unicode/GraphemeBreak.cpp tests/unit/test_grapheme_break.cpp tests/data/GraphemeBreakTest.txt tests/CMakeLists.txt
git commit -m "feat(unicode): UAX #29 聚簇 segmenter 与生成管线及 golden 全量验证（M7b T1）"
```

---

### 任务 2：contour 探针 + parity 裁定表（先探后改）

**文件：**
- 创建：`tests/probe/cluster_probe.cpp`（可执行，不注册 ctest，仅 contour 构建）
- 修改：`tests/CMakeLists.txt`（条件编译探针）
- 创建：`docs/superpowers/specs/2026-09-21-m7b-parity-probe.md`（结果与裁定表）

- [ ] **步骤 1：cluster_probe.cpp（探针程序）**

- 结构：复刻 Dual 的喂法，只用 Contour 后端 ZzTerminal（80, 24, Contour, 1000）；每个探针用例：feed 脚本 → 遍历 renderView 前两行逐格打印（col、text 十六进制转储、width 枚举值）→ 新终端重置；
- 探针脚本集（UTF-8 字面量，每例同时跑"整喂"与"逐码点切 feed"两式以覆盖跨 feed 续接）：

```cpp
//  1. 单组合符        "a\xCC\x81"                    a + U+0301
//  2. 双组合符        "e\xCC\x81\xCC\xA7"            e + U+0301 U+0327
//  3. Emoji ZWJ 对    "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"          👨‍👩
//  4. 家庭 ZWJ 四连   "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"  👨‍👩‍👧
//  5. 区旗单发        "\xF0\x9F\x87\xBA"              U+1F1FA
//  6. 区旗成对        "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"                       US
//  7. 区旗三连        "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xBA"
//  8. VS16 变宽       "\xE2\x98\x9D\xEF\xB8\x8F"      U+261D U+FE0F
//  9. VS16 红心       "\xE2\x9D\xA4\xEF\xB8\x8F"      U+2764 U+FE0F
// 10. VS15 变窄       "\xE2\x9D\xA4\xEF\xB8\x8E"      U+2764 U+FE0E
// 11. keycap          "1\xEF\xB8\x8F\xE2\x83\xA3"     1 + U+FE0F U+20E3
// 12. InCB 连字       "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"                   क + ् + ष
// 13. CJK+组合        "\xE4\xB8\xAD\xCC\x81"          中 + U+0301
// 14. a+ZWJ+emoji     "a\xE2\x80\x8D\xF0\x9F\x91\xA9" （ZWJ 前非 ExtPic，预期断）
// 15. Prepend         "\xD8\x80" "61"               U+0600 + a（分两例：连喂/观察）
```

- 边界用例追加：行填满 79 个 a 后再喂组合符（软换行边界续接——前格在物理上行尾，组合符落同行末格还是新行首格，逐格打印记录）；尾列宽字符 + 组合符（宽格对上的续接）；
- 输出直接打印到 stdout（人工抄入裁定表），程序返回 0；不注册 add_test。

- [ ] **步骤 2：tests/CMakeLists.txt 条件编译**

```cmake
# M7b：聚簇 parity 探针（开发期工具，不注册 ctest），仅 Contour 后端启用时构建。
if(TARGET ZzTermContourBackend)
    add_executable(cluster_probe probe/cluster_probe.cpp)
    target_link_libraries(cluster_probe PRIVATE ZzTermCore) # Contour target 一律 PRIVATE
endif()
```

- [ ] **步骤 3：跑探针并记录**

```bash
cmake --build --preset linux-gcc-debug && ./build/linux-gcc-debug/tests/cluster_probe
```

把每个脚本的 contour 实测（格数、各格 text、width、整喂/切 feed 是否一致）整理为裁定表，写入 docs/superpowers/specs/2026-09-21-m7b-parity-probe.md。表后追加**裁定结论**节，至少覆盖：
- 聚簇宽度规则（emoji ZWJ 序列、ExtPic+VS16、keycap、InCB 连字、组合符序列、区旗对各是几格宽）；
- **VS16 变宽位移语义**（基字符已按窄格落格后 VS16 使其变宽时，contour 是否把后续内容右移/插入续格/丢弃尾列——逐字记录实测，这是 T3 最难的语义点）；
- 软换行边界的续接语义（行尾格能否被下一码点续接）；
- a+ZWJ+emoji 的断开行为；
- 跨 feed 切断与整喂的一致性（若有差异逐条钉住）；
- 任何不规则行为（libunicode 非查表能解释的边角）逐条留痕。

注意 doxygen 陷阱（该文件被 doxygen 扫描：行内代码禁尖括号/点开头/反斜杠转义、后禁紧跟顿号；字节序列一律放代码围栏）。

- [ ] **步骤 4：验证与 Commit**

探针仅新增文件，44 基线 + golden（45/45）必须原样绿：

```bash
ctest --preset linux-gcc-debug
git add tests/probe/cluster_probe.cpp tests/CMakeLists.txt docs/superpowers/specs/2026-09-21-m7b-parity-probe.md
git commit -m "test(probe): contour 聚簇行为探针与 parity 裁定表（M7b T2）"
```

---

### 任务 3：native putChar 聚簇集成

**文件：**
- 修改：`src/backend/native/ZzNativeBackend.cpp`（putChar，:169-221 区域）
- 创建：`tests/unit/test_native_cluster.cpp`
- 测量：`tests/unit/test_perf_scrollback.cpp`（append 门控，不改代码只跑数）

- [ ] **步骤 1：putChar 续接判定插入（按 T2 裁定表实现）**

在 putChar 的 wrap-pending 处理之后、落格之前插入续接分支（伪码，最终语义以 T2 裁定表为准）：

```cpp
// M7b 聚簇续接（无状态回望，规格 4.3）：新码点与前格 cluster 判续，
// 续则并入前格不推进光标。快路径：前格为普通单码点格且新码点不可能
// 续接（非 Extend/ZWJ/SpacingMark/RI/ExtPic）时走原路零回望。
```

实现要点（每点都以 T2 裁定表为最终依据，表与伪码冲突时表胜）：
- 前格定位：光标同行 col-1（col==0 时无前格——软换行边界能否续接以 T2 实测为准，若 contour 续到上行尾格则此处按裁定实现，否则无前格即断）；前格须含文本（isCluster 或 codePoint 非 0）才判续，空白格不断定续接；
- 判续：取前格码点串（isCluster → clusterText 解码为码点串；单码点 → 单元素），调 zzGraphemeContinues；UTF-8 解码用既有 ZzUtf8Decoder；
- 续接落格：新码点 UTF-8 追加进 cluster 串，前格 putCell 更新（setCluster(internCluster(新串))，画笔属性保持前格原值——若 T2 实测 contour 用新画笔属性覆盖整格，按实测改）；光标不推进、wrap-pending 不变；旧 cluster 侧表条目弃置不管（internCluster 不查重约定，索引空间充足）；
- 宽度裁定：续接后格宽按 T2 表（预期形态：emoji ZWJ/ExtPic+VS16/keycap → 宽格；组合符/InCB 连字 → 基字符宽度）；**窄变宽情形**（基字符已窄格落格、续接码点使其变宽——典型 VS16）的行内位移语义严格按 T2 实测实现（右移/插续格/尾列丢弃逐字对齐）；宽格续接（CJK+组合符）保持宽格对不动；
- 快路径条件（三点全满足才走原路）：前格非 cluster、前格码点的 GCB 非 Prepend/RegionalIndicator、新码点 GCB 非 Extend/ZWJ/SpacingMark/RegionalIndicator 且非 ExtPic；
- putChar 其余逻辑（尾列宽字符换行、光标推进、wrap-pending 置位）逐行不动。

- [ ] **步骤 2：行为零变化验证（第一原则）**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：45/45 全绿，断言零改动（44 基线 + 三份 compat + golden 原样过——ASCII/CJK 路径逐位不变的证据）。

- [ ] **步骤 3：test_native_cluster.cpp（公开 API 格级断言）**

- 复刻 Dual 喂法但只用 Native；断言经 renderView 逐格：格数、各格 text（cluster 串逐字节）、width 枚举；用例集与 T2 探针同源（15 脚本 + 边界用例），每例的期望值直接引 T2 裁定表（contour 实测值——本测试同时是 T4 compat 的预演）；
- 跨 feed 续接：基字符与组合符分两次 feed、ZWJ 序列逐码点分 feed，断言与整喂一致；
- 光标行为：续接后光标停前格之后（经后续 ASCII 落格位置间接断言——续接后再喂一个 b，b 应落在聚簇格之后）；
- 画笔属性：续接格保持基字符落格时的属性（先 SGR 变色再喂基字符+组合符，断言聚簇格前景）。

- [ ] **步骤 4：append 门控不劣化**

```bash
./build/linux-gcc-debug/tests/test_perf_scrollback
```

记录 append/reflow/内存各项实测与 M6 基线（append 145ms 门控 500ms 区域）对比；快路径下 ASCII append 应无可见劣化；若 append 劣化超门控，报 DONE_WITH_CONCERNS 附数字由控制者裁定（不自行放宽门控）。

- [ ] **步骤 5：Commit**

```bash
git add src/backend/native/ZzNativeBackend.cpp tests/unit/test_native_cluster.cpp
git commit -m "feat(native): putChar 聚簇续接集成（无状态回望，M7b T3）"
```

---

### 任务 4：cluster compat + 文档标注 + 全回归门

**文件：**
- 创建：`tests/unit/test_cluster_compat.cpp`
- 修改：`tests/CMakeLists.txt`（REMOVE_ITEM + 条件注册）
- 修改：`docs/VT-Xterm-Checklist.md`、`docs/Architecture-v2.md`（或 Architecture.md，以聚簇现状记述所在文档为准）

- [ ] **步骤 1：test_cluster_compat.cpp（双后端逐格对照）**

- 复刻 test_backend_compat.cpp 的 Dual + checkCellEqual（text/width/前景/背景/属性全比对，native 以 contour 为基准）；
- 用例集与 T2 探针/T3 集成测试同源（15 脚本 + 软换行边界 + 跨 feed 切断），断言整行前 N 格逐格一致 + 整喂/切 feed 两式一致；
- 若 T2 裁定表留有 native 无法对齐的 contour 边角，按 b 类分歧分别断言 + 注释钉住（惯例同 test_backend_compat.cpp 空单元格宽度类别分歧）；目标是零分歧——出现 b 类需报 DONE_WITH_CONCERNS 由控制者裁定入延后族。

- [ ] **步骤 2：tests/CMakeLists.txt 条件注册**

REMOVE_ITEM 清单追加 unit/test_cluster_compat.cpp，文件尾部仿三份 compat 增加：

```cmake
# M7b：双后端聚簇 compat（同一聚簇脚本逐格比对，native 以 contour 为基准）。
if(TARGET ZzTermContourBackend)
    add_executable(test_cluster_compat unit/test_cluster_compat.cpp)
    target_link_libraries(test_cluster_compat PRIVATE ZzTermCore)
    add_test(NAME test_cluster_compat COMMAND test_cluster_compat)
endif()
```

- [ ] **步骤 3：文档标注**

- docs/VT-Xterm-Checklist.md Unicode 节（v1:89-98）：Combining marks、Variation selectors、Emoji / ZWJ 勾 [x]；Grapheme-aware copy/search 先实证再勾——M5a 提取经 clusterText、M5b 搜索经 cluster emit，本任务跑一个手工验证（cluster 文本的选定复制与搜索命中），达成则勾 [x] 并注释 M7b 达成路径，不达成则留 [ ] 并注释缺口；
- v2:146-158 对应项同样逐项核对勾选（只勾真实达成的）；
- Architecture-v2.md §10 区域（或聚簇记述所在处）追加：M7b native 聚簇生产落地（UAX #29 自研 segmenter + 无状态回望，UCD 16.0.0），双后端 parity 经 test_cluster_compat 门控。

- [ ] **步骤 4：全回归门**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
```

预期：ON 47/47（44 基线 + golden + native_cluster + cluster_compat）、OFF 36/36（35 + golden；无 contour 测试）、shared 47/47；fuzz 双 smoke 绿（putChar 新路径被 feed fuzz 冲击的实证）；doxygen 零警告。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_cluster_compat.cpp tests/CMakeLists.txt docs/VT-Xterm-Checklist.md docs/Architecture-v2.md
git commit -m "test(compat): 双后端聚簇 compat 与 Checklist 勾选（M7b T4）"
```

---

## 自检结论（计划编写后）

- **规格覆盖度**：§3.1 全项——生成管线与 segmenter 与 golden（T1）、contour 探针与裁定表（T2）、putChar 集成与跨 feed 与快路径（T3 步骤 1/3/4）、cluster compat（T4 步骤 1）、Checklist/Architecture（T4 步骤 3）、全回归含 fuzz 与 benchmark（T3 步骤 4、T4 步骤 4）。§3.2 排除项——contour 侧零改动（探针只读）、UCD 钉版、公开 API 禁变均在第一原则与任务边界内。
- **占位符扫描**：T2 裁定表的"实测为准"、T3 的"表与伪码冲突时表胜"为规格批准的"先探后改"既定程序，非占位符；T4 Checklist 勾选以手工实证为准同为既定程序。无 TODO/待定。
- **类型一致性**：ZzGcb/ZzIncb/ZzGraphemeProps 与双口签名在 T1 步骤 2/3 与 T3 引用间一致；ZzGcbInterval 表形与 gen 脚本输出一致；测试接线宏 ZZ_GRAPHEME_TEST_DATA 在 CMake 与测试源码间一致。
- **风险预置**：golden 不过不进 T3（规格 5 已钉）；T2 发现不可对齐行为 → DONE_WITH_CONCERNS 裁定延后族（T4 步骤 1）；append 劣化不自行放宽门控（T3 步骤 4）。
