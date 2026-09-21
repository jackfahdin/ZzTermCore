# M7a Fuzz 基建实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 立起 libFuzzer Fuzz 基建——ZZTERM_FUZZ option + linux-clang-fuzz preset + tests/fuzz/ 双 harness（ZzVtParser 裸解析、ZzTerminal feed+resize 交织）+ seed corpus 入库 + 两个 30 秒 ctest smoke + CI 挂点。

**架构：** 全部改动为增量（构建脚本、新目录 tests/fuzz/、CI yml、文档标注），不触碰任何现有源码与测试。ZZTERM_FUZZ 默认 OFF，OFF 路径无新增源码参与编译——常规三配置构建产物与测试矩阵逐位不变是本计划第一约束。

**技术栈：** CMake option/preset 惯例（ninja-base 继承）、libFuzzer（-fsanitize=fuzzer，仅 Clang）、ASan 全构建注入、ctest smoke 门控（-max_total_time=30，TIMEOUT 60）、doxygen 零警告门。

**规格：** docs/superpowers/specs/2026-09-21-m7a-fuzz-infrastructure-design.md（commit 522d9b3 + Action 策略 466636a + sink 修正 23bb895）。

**第一原则（全计划最高优先级）：** ZZTERM_FUZZ=OFF 下一切不变——linux-gcc-debug 44/44、OFF 35/35、shared 44/44、doxygen 零警告，且新增代码（tests/fuzz/）完全不参与编译。任何污染 OFF 路径的改动即越界——停下来报告 BLOCKED，不硬闯。

**已核实的关键签名（计划阶段免查）：**

- `explicit ZzVtParser(ZzParserSink* sink, ZzParserLimits limits = {})`——Parser.h:246；`feed(std::string_view)` 增量入口 Parser.h:259；
- ZzParserSink 基类默认回调全部 no-op（src/parser/Parser.cpp:47-54），harness 直接使用基类即可，无需子类覆写；
- `ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines = 100000)`——Terminal.h:94；
- `ZzTermChanges feed(std::span<const std::byte> data)`——Terminal.h:112，无 nodiscard，返回值可弃；
- `bool resize(int cols, int rows)`——Terminal.h:125，无 nodiscard；
- 本机无裸 clang++，有 clang++-20（LLVM 20，-fsanitize=fuzzer,address 实测通过）；CI apt 装 clang 包后有 clang++ 通用名。

**命令约定（全计划通用）：**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
# OFF：cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
# shared：cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
# 文档：doxygen Doxyfile（必须在仓库根运行，exit 0 且零警告）
# fuzz preset（本机用 -D 覆盖编译器为 clang++-20；CI 直接用通用名）：
cmake --preset linux-clang-fuzz -D CMAKE_CXX_COMPILER=clang++-20
cmake --build --preset linux-clang-fuzz
ctest --preset linux-clang-fuzz -R fuzz
```

## 文件结构

| 文件 | 职责 | 任务 |
|---|---|---|
| CMakeLists.txt | ZZTERM_FUZZ option + Clang 校验 + 全构建 ASan 注入 | T1 |
| tests/CMakeLists.txt | 尾部条件收录 tests/fuzz 子目录 | T1 |
| tests/fuzz/CMakeLists.txt | 双 harness target + fuzzer sanitizer + smoke 注册 | T1 |
| tests/fuzz/fuzz_parser.cpp | T1 占位 → T2 实装 ZzVtParser harness | T1/T2 |
| tests/fuzz/fuzz_feed.cpp | T1 占位 → T3 实装 feed+resize harness | T1/T3 |
| CMakePresets.json | linux-clang-fuzz configure/build/test 三件套 | T1 |
| .github/workflows/ci.yml | ubuntu-clang job 追加 fuzz smoke 三步 | T1 |
| tests/fuzz/corpus/parser/ | parser seed corpus（16 个） | T1 起建/T2 充实 |
| tests/fuzz/corpus/feed/ | feed seed corpus（10 个） | T1 起建/T3 充实 |
| docs/Architecture.md | §204 Fuzz 进度标注 | T4 |

---

### 任务 1：构建集成（option + preset + CI + 骨架 harness 验证编译链）

**文件：**
- 修改：`CMakeLists.txt`
- 修改：`tests/CMakeLists.txt`
- 创建：`tests/fuzz/CMakeLists.txt`、`tests/fuzz/fuzz_parser.cpp`（占位）、`tests/fuzz/fuzz_feed.cpp`（占位）
- 创建：`tests/fuzz/corpus/parser/seed_plain`、`tests/fuzz/corpus/feed/seed_plain`（各一个占位种子）
- 修改：`CMakePresets.json`
- 修改：.github/workflows/ci.yml

- [ ] **步骤 1：根 CMakeLists.txt 增加 ZZTERM_FUZZ option 与 ASan 注入**

在 ZZTERM_WITH_CONTOUR 块（:23-31）之后插入：

```cmake
# Fuzz 基建（M7a）：libFuzzer 仅 Clang 可用，默认 OFF。
# ON 时全构建追加 ASan（库与测试同被覆盖，保证 harness 链接一致）；
# libFuzzer 运行时（-fsanitize=fuzzer）只加在 tests/fuzz 的 harness target 上。
option(ZZTERM_FUZZ "构建 libFuzzer Fuzz target（需要 Clang，全构建追加 ASan）" OFF)
if(ZZTERM_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ZZTERM_FUZZ=ON 需要 Clang（libFuzzer）；当前编译器：${CMAKE_CXX_COMPILER_ID}")
    endif()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
```

- [ ] **步骤 2：tests/CMakeLists.txt 尾部条件收录 fuzz 子目录**

文件末尾追加：

```cmake
# M7a：Fuzz harness 与 smoke，仅 ZZTERM_FUZZ=ON（linux-clang-fuzz preset）时收录；
# OFF 路径本目录完全不参与编译（第一原则）。
if(ZZTERM_FUZZ)
    add_subdirectory(fuzz)
endif()
```

- [ ] **步骤 3：tests/fuzz/CMakeLists.txt（target + smoke 注册）**

```cmake
# M7a Fuzz target 约定：
#   - 本目录仅 ZZTERM_FUZZ=ON 时收录（tests/CMakeLists.txt 尾部）；
#   - 每个 fuzz_*.cpp 使用 libFuzzer 提供的 main（-fsanitize=fuzzer），
#     独立于 tests/unit 的 GLOB 体系，命名空间不冲突；
#   - ASan 由根 CMakeLists 全局注入，本文件只追加 fuzzer。
foreach(fuzz_target IN ITEMS fuzz_parser fuzz_feed)
    add_executable(${fuzz_target} ${fuzz_target}.cpp)
    target_link_libraries(${fuzz_target} PRIVATE ZzTermCore)
    target_compile_options(${fuzz_target} PRIVATE -fsanitize=fuzzer)
    target_link_options(${fuzz_target} PRIVATE -fsanitize=fuzzer)
endforeach()

# 崩溃产物目录（位于 build/ 下，既被 gitignore 的 build/ 规则覆盖）。
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fuzz-artifacts")

# ctest smoke：seed corpus 起步各跑 30 秒，TIMEOUT 60。
# 撞出崩溃时的处置按规格 M7a 4.5 崩溃预案裁定。
add_test(NAME fuzz_parser_smoke
    COMMAND fuzz_parser "${CMAKE_CURRENT_SOURCE_DIR}/corpus/parser"
            -max_total_time=30 -print_final_stats=1
            "-artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/fuzz-artifacts/")
set_tests_properties(fuzz_parser_smoke PROPERTIES TIMEOUT 60)
add_test(NAME fuzz_feed_smoke
    COMMAND fuzz_feed "${CMAKE_CURRENT_SOURCE_DIR}/corpus/feed"
            -max_total_time=30 -print_final_stats=1
            "-artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/fuzz-artifacts/")
set_tests_properties(fuzz_feed_smoke PROPERTIES TIMEOUT 60)
```

- [ ] **步骤 4：占位 harness 与占位种子（验证编译链用，T2/T3 实装替换）**

tests/fuzz/fuzz_parser.cpp 与 tests/fuzz/fuzz_feed.cpp 同形占位：

```cpp
// M7a 占位 harness（T1）：验证 ZZTERM_FUZZ 编译链；T2/T3 实装替换。
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t)
{
    return 0;
}
```

占位种子（保证 smoke 的 corpus 目录非空，libFuzzer 对不存在/空 corpus 目录会报错）：

```bash
printf 'hello' > tests/fuzz/corpus/parser/seed_plain
printf 'hello\r\n' > tests/fuzz/corpus/feed/seed_plain
```

- [ ] **步骤 5：CMakePresets.json 增加 linux-clang-fuzz 三件套**

configurePresets 追加（仿 linux-clang-debug 位置，置于其后）：

```json
    {
      "name": "linux-clang-fuzz",
      "inherits": "ninja-base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
        "CMAKE_CXX_COMPILER": "clang++",
        "ZZTERM_FUZZ": "ON",
        "ZZTERM_WITH_CONTOUR": "OFF"
      }
    },
```

buildPresets 追加：

```json
    { "name": "linux-clang-fuzz", "configurePreset": "linux-clang-fuzz" },
```

testPresets 追加：

```json
    { "name": "linux-clang-fuzz", "inherits": "test-base", "configurePreset": "linux-clang-fuzz" },
```

注意 preset 钉通用名 clang++（CI apt 装包后可用）；本机无裸 clang++，用 cmake --preset 后随的 -D CMAKE_CXX_COMPILER=clang++-20 覆盖。**计划阶段核实项**：验证 preset cacheVariables 可被命令行 -D 覆盖（configure 后查 CMakeCache 的 CMAKE_CXX_COMPILER 值）；若不能覆盖，回退方案为 preset 移除 CMAKE_CXX_COMPILER 行、改由 env CXX 指定（CI 步骤同步写 CXX=clang++），回退需记录 commit message。

- [ ] **步骤 6：本机 fuzz preset 全链路验证**

```bash
cmake --preset linux-clang-fuzz -D CMAKE_CXX_COMPILER=clang++-20
cmake --build --preset linux-clang-fuzz
ctest --preset linux-clang-fuzz -R fuzz
```

预期：configure 无 FATAL_ERROR；双占位 harness 编译链接通过（libFuzzer main 生效）；两个 smoke 各跑 30 秒退出码 0（占位 harness 立即返回，libFuzzer 仍做语料变异循环）。同时确认单元测试在 ASan 下可用：`ctest --preset linux-clang-fuzz` 全量（应为 35 单元 + 2 smoke 全绿，contour OFF 矩阵）。

- [ ] **步骤 7：ci.yml 的 ubuntu-clang job 追加 fuzz smoke 步骤**

在 ubuntu-clang 的 Test 步骤（:21-22）之后追加：

```yaml
      - name: Fuzz Configure
        run: cmake --preset linux-clang-fuzz
      - name: Fuzz Build
        run: cmake --build --preset linux-clang-fuzz
      - name: Fuzz Smoke（双 harness 各 30 秒）
        run: ctest --preset linux-clang-fuzz -R fuzz
```

- [ ] **步骤 8：OFF 零污染验证（第一原则）**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：44/44 全绿；git status 确认 tests/fuzz/ 未参与该构建（构建目录无 fuzz 产物）。

- [ ] **步骤 9：Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/fuzz/ CMakePresets.json .github/workflows/ci.yml
git commit -m "build(fuzz): ZZTERM_FUZZ option 与 linux-clang-fuzz preset 及 CI 挂点（M7a T1）"
```

---

### 任务 2：fuzz_parser 实装 + parser seed corpus

**文件：**
- 修改：`tests/fuzz/fuzz_parser.cpp`（占位替换为实装）
- 创建：`tests/fuzz/corpus/parser/` 下 15 个新种子（含 T1 占位共 16 个）

- [ ] **步骤 1：实装 fuzz_parser.cpp（整喂 + 首字节取模切两刀）**

```cpp
// M7a：ZzVtParser Fuzz target——状态机在任意字节流与任意 chunk 边界下
// 不崩、不断言、不 UB（ASan + libFuzzer）。sink 基类默认回调全 no-op
//（src/parser/Parser.cpp:47-54），语义不在本 target 校验范围。
#include "ZzTerm/Parser.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size == 0)
        return 0;
    const std::string_view input(reinterpret_cast<const char*>(data), size);
    ZzParserSink sink; // 基类默认回调全 no-op
    // 路径一：整喂一次（完整序列路径）。
    {
        ZzVtParser parser(&sink);
        parser.feed(input);
    }
    // 路径二：首字节对输入长度取模定切点，切两段先后喂——跨 chunk
    // 续接路径（OSC 字符串与多字节序列在边界中断续接是最脆的接缝）。
    {
        ZzVtParser parser(&sink);
        const std::size_t cut = data[0] % size; // [0, size-1]，第二段恒非空
        parser.feed(input.substr(0, cut));
        parser.feed(input.substr(cut));
    }
    return 0;
}
```

- [ ] **步骤 2：parser seed corpus 充实（15 个新种子）**

```bash
cd tests/fuzz/corpus/parser
printf '\x1b[1;31m' > csi_sgr
printf '\x1b[2;5H' > csi_cursor_pos
printf '\x1b]0;window title\x07' > osc_title
printf '\x1b]8;;https://example.com\x07link\x1b]8;;\x07' > osc_hyperlink
printf '\x1b[?25l\x1b[?25h' > dec_cursor_visibility
printf '\x1b[?1049h\x1b[?1049l' > alt_screen_toggle
printf '\x1b[?1006h\x1b[<0;10;5M' > mouse_sgr_1006
printf '\x1b[?2004h\x1b[200~pasted\x1b[201~' > bracketed_paste
printf '\xe4\xb8\xad\xe6\x96\x87' > cjk_utf8
printf 'a\xcc\x81' > combining_mark
printf '\x80\xff\xc0\xaf' > invalid_utf8
printf '\xe4\xb8' > truncated_utf8
printf '\x1b' > lone_esc
printf '\x1b[1;' > truncated_csi
printf '\x1bP1;2|payload\x1b\\' > dcs_payload
```

（T1 的 seed_plain 保留，合计 16 个。）

- [ ] **步骤 3：本机 smoke 验证**

```bash
cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz_parser_smoke
```

预期：30 秒跑完退出码 0，-print_final_stats 输出 cov/ft 计数（记录代表值，供 T4 台账）。**若撞出崩溃**：不自行修复，留最小复现输入（fuzz-artifacts/ 下的 crash-* 文件），报告 DONE_WITH_CONCERNS 由控制者按规格 4.5 预案裁定。

- [ ] **步骤 4：Commit**

```bash
git add tests/fuzz/fuzz_parser.cpp tests/fuzz/corpus/parser/
git commit -m "test(fuzz): ZzVtParser Fuzz target 与 parser seed corpus（M7a T2）"
```

---

### 任务 3：fuzz_feed 实装 + feed seed corpus

**文件：**
- 修改：`tests/fuzz/fuzz_feed.cpp`（占位替换为实装）
- 创建：`tests/fuzz/corpus/feed/` 下 9 个新种子（含 T1 占位共 10 个）

- [ ] **步骤 1：实装 fuzz_feed.cpp（ZzTerminal 跨输入复用 + 分片 feed + 交织 resize）**

```cpp
// M7a：ZzTerminal feed+resize 交织 Fuzz target——有态终端在任意输入
// 分片与尺寸抖动下不崩、不断言、不 UB（Architecture §204 第三类
// target；reflow 历史 bug 集中于 feed+resize 交织路径）。
#include "ZzTerm/Terminal.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size < 2)
        return 0;
    // 跨输入复用：终端是有态对象，复用才能触达长程状态路径；跨输入的
    // 状态污染本身属于 fuzz 面（规格 M7a 4.3；若实证崩溃难复现，再
    // 裁定为按输入重建并记录）。
    static ZzTerminal term(80, 24, ZzBackendKind::Native, 1000);

    const std::size_t chunk = 1 + data[0] % 17;   // 分片粒度 1-17 字节
    const std::uint8_t knob = data[size / 2];      // resize 判定字节
    const bool doResize = (knob & 1) != 0;
    const int resizeCols = 40 + knob % 41;         // 列数 40-80 抖动

    const std::byte* bytes = reinterpret_cast<const std::byte*>(data);
    const std::size_t mid = size / 2;
    for (std::size_t off = 0; off < size; off += chunk) {
        const std::size_t n = (off + chunk <= size) ? chunk : size - off;
        term.feed(std::span<const std::byte>(bytes + off, n));
        if (doResize && off < mid && mid < off + n)
            term.resize(resizeCols, 24); // feed 中段穿插 resize
    }
    return 0;
}
```

- [ ] **步骤 2：feed seed corpus 充实（9 个新种子）**

```bash
cd tests/fuzz/corpus/feed
printf '\x1b[1;31mred\x1b[0m normal\r\n' > sgr_mix
printf '\x1b[?1049h\x1b[2J\x1b[Halt content\x1b[?1049l' > alt_screen_cycle
printf 'a\xcc\x81\xe4\xb8\xad\r\n' > utf8_combining_cjk
printf '\x1b[200~paste data here\x1b[201~\r\n' > paste_flow
printf '\x1b[?1006h\x1b[<0;5;5M\x1b[<0;5;5m' > mouse_flow
head -c 300 /dev/zero | tr '\0' 'x' > long_soft_wrap
printf '\x1b[1;31m\x1b]0;t\x07' > esc_mix
cat ../parser/csi_sgr ../parser/osc_title ../parser/alt_screen_toggle > mixed_stream
printf '\x80\xff\x1b[;Hgarbage' > garbage_mix
```

（T1 的 seed_plain 保留，合计 10 个；与 parser 16 个共计 26 个种子，在规格 20-40 区间内。）

- [ ] **步骤 3：本机 smoke 验证**

```bash
cmake --build --preset linux-clang-fuzz && ctest --preset linux-clang-fuzz -R fuzz_feed_smoke
```

预期与崩溃处置同 T2 步骤 3（30 秒退出码 0；撞崩留复现报 DONE_WITH_CONCERNS）。

- [ ] **步骤 4：Commit**

```bash
git add tests/fuzz/fuzz_feed.cpp tests/fuzz/corpus/feed/
git commit -m "test(fuzz): ZzTerminal feed+resize Fuzz target 与 feed seed corpus（M7a T3）"
```

---

### 任务 4：双 smoke 全量验证 + 文档标注 + 全回归门

**文件：**
- 修改：`docs/Architecture.md`（§204 Fuzz 行进度标注）

- [ ] **步骤 1：fuzz preset 全新全链路（干净目录重建验证）**

```bash
rm -rf build/linux-clang-fuzz
cmake --preset linux-clang-fuzz -D CMAKE_CXX_COMPILER=clang++-20
cmake --build --preset linux-clang-fuzz
ctest --preset linux-clang-fuzz -R fuzz
```

预期：双 smoke 各 30 秒退出码 0；记录两个 -print_final_stats 的 cov/ft 代表值（写进本任务 commit message）。任一非零退出 → 按规格 4.5 崩溃预案：留最小复现，报告控制者裁定（少量明确 bug 顺手修需独立 commit 并在规格留痕；成批/深水区记录为后续必修项、崩溃输入入 corpus、门控语义调整为「已知集合外无新崩溃」）。

- [ ] **步骤 2：Architecture.md §204 进度标注**

docs/Architecture.md:204「建立 UTF-8 decoder、VT Parser、feed+resize 的 Fuzz target。」行末追加：

「（M7a 已立基建：ZZTERM_FUZZ option 与 linux-clang-fuzz preset，tests/fuzz 双 harness——fuzz_parser 覆盖 VT Parser，fuzz_feed 覆盖 feed+resize 交织并经 print 通道覆盖 UTF-8 decoder；两个 30 秒 ctest smoke 入 CI。decoder 独立 harness 留作后续候选。）」

- [ ] **步骤 3：全回归门（第一原则终验）**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake --build build/m2-off-check && ctest --test-dir build/m2-off-check
cmake --build build/m2-shared-check && ctest --test-dir build/m2-shared-check
doxygen Doxyfile
```

预期：ON 44/44、OFF 35/35、shared 44/44、doxygen 零警告——与 M6 基线逐位一致（ZZTERM_FUZZ=OFF 下 fuzz 代码完全缺席）。

- [ ] **步骤 4：Commit**

```bash
git add docs/Architecture.md
git commit -m "docs(arch): Fuzz target 基建落地标注（M7a T4，双 smoke cov 见 tests/fuzz）"
```

（若步骤 1 有统计代表值，补进 message；若有崩溃裁定产物，按预案一并提交 corpus 新增与台账记录。）

---

## 自检结论（计划编写后）

- **规格覆盖度**：§3.1 全项——option 与 preset（T1 步骤 1/5）、双 harness（T2/T3）、seed corpus 20-40 个（T1 起建 + T2/T3 充实共 26 个）、ctest smoke 双注册（T1 步骤 3）、CI 挂点（T1 步骤 7）、Architecture §204 标注（T4 步骤 2）、常规三配置与 doxygen 回归（T1 步骤 8 + T4 步骤 3）。§4.5 崩溃预案 → T2/T3/T4 各验证步骤的 DONE_WITH_CONCERNS 分支。§4.6 DoD 全项有对应步骤。
- **占位符扫描**：T1 步骤 5 的 preset 覆盖语义回退为「计划阶段核实项」既定程序（实证后二选一并留痕）；T4 的 cov/ft 代表值为测量纪律既定程序（实测替换）。无 TODO/待定。
- **类型一致性**：harness 签名 extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) 与 libFuzzer 约定一致；feed/resize/构造签名均按本仓库公开头实测（Terminal.h:94/112/125、Parser.h:246/259），无 nodiscard 返回值可弃已核实。
