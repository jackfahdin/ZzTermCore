# M11 M7a 台账清零（decoder harness + 深度 fuzz）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** UTF-8 decoder 独立 fuzz harness（带不变量断言）+ 深度 fuzz 机制（dispatch/schedule 长跑 workflow + 种子扩充），M7a 台账清零。

**架构：** 新增 tests/fuzz/fuzz_utf8.cpp 第三 harness（三路解码一致性 + 三条不变量，断言用 __builtin_trap——fuzz preset 是 RelWithDebInfo，assert 会被 NDEBUG 编译掉）；CMakeLists foreach 列表加名收编；新建 dispatch/schedule 双触发的 ci-fuzz-deep.yml；种子三目录扩充。

**技术栈：** libFuzzer + ASan（linux-clang-fuzz preset，clang++-20）、ZzUtf8Decoder（include/ZzTerm/Utf8.h，语义准绳为其文档：RFC 3629 Table 3-7 + maximal subpart）、GitHub Actions。

**规格：** docs/superpowers/specs/2026-09-29-m11-fuzz-deepening-design.md（断言设计、排除项、crash 处置以规格为准）

**基线命令（本机 Linux，全程不得回归）：**
- ON：`ctest --preset linux-gcc-debug`（50/50）
- OFF：`ctest --test-dir build/m2-off-check`（40/40）
- shared：`ctest --test-dir build/m2-shared-check`（50/50）
- fuzz：`ctest --preset linux-clang-fuzz -R fuzz`（本里程碑 2/2 → 3/3 自然增长；configure 重建需 `-D CMAKE_CXX_COMPILER=clang++-20`）
- doxygen：`doxygen Doxyfile`（exit 0 零警告）

---

### 任务 1：fuzz_utf8 harness + 种子 + 收编

**文件：**
- 创建：`tests/fuzz/fuzz_utf8.cpp`
- 创建：`tests/fuzz/corpus/utf8/`（6-10 条种子 + README.md 覆盖意图说明）
- 修改：`tests/fuzz/CMakeLists.txt`（foreach 列表、file(COPY)、add_test 三处同款追加）

- [ ] **步骤 1：编写 fuzz_utf8.cpp**

```cpp
// M11：ZzUtf8Decoder 独立 Fuzz target——增量解码器在任意字节流与任意分块下
// 不崩、不断言、不 UB（ASan + libFuzzer），并校验三条不变量（见 checkCodePoints
// 与续接一致性断言）。decoder 语义准绳为 include/ZzTerm/Utf8.h 文档（RFC 3629
// Table 3-7 拒绝 overlong/代理区/超 U+10FFFF/5-6 字节序列；非法按 maximal
// subpart 输出 U+FFFD）。断言用 __builtin_trap——fuzz preset 为 RelWithDebInfo，
// assert 会被 NDEBUG 编译掉。
#include "ZzTerm/Utf8.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

void checkCodePoints(const std::vector<char32_t>& cps, std::size_t inputSize)
{
    // 不变量一：输出码点数不超过输入字节数加一（每字节至多产一码点，
    // finish 收尾至多追加一个 U+FFFD）。
    if (cps.size() > inputSize + 1)
        __builtin_trap();
    for (char32_t cp : cps) {
        // 不变量二/三：不落在代理区 U+D800-U+DFFF、不超过 U+10FFFF。
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            __builtin_trap();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size == 0)
        return 0;
    const std::string_view input(reinterpret_cast<const char*>(data), size);

    // 路径一：整喂 decodeAll（内部调 finish 收尾并复位）。
    ZzUtf8Decoder whole;
    const std::vector<char32_t> expect = whole.decodeAll(input);
    checkCodePoints(expect, size);

    // 路径二：首字节定切点分两段 feed + finish——续接不变量：与整喂一致
    //（Utf8.h feed 文档承诺：任意切块与一次性喂入输出完全一致）。
    ZzUtf8Decoder chunked;
    std::vector<char32_t> got;
    auto emit = [&got](char32_t cp) { got.push_back(cp); };
    const std::size_t cut = data[0] % size; // [0, size-1]，第二段恒非空
    chunked.feed(input.substr(0, cut), emit);
    chunked.feed(input.substr(cut), emit);
    chunked.finish(emit);
    if (got != expect)
        __builtin_trap();

    // 路径三：逐字节 feed + finish——最细分块下的续接不变量。
    ZzUtf8Decoder bytewise;
    std::vector<char32_t> gotByte;
    auto emitByte = [&gotByte](char32_t cp) { gotByte.push_back(cp); };
    for (const std::uint8_t b : input) {
        const char ch = static_cast<char>(b);
        bytewise.feed(std::string_view(&ch, 1), emitByte);
    }
    bytewise.finish(emitByte);
    if (gotByte != expect)
        __builtin_trap();

    return 0;
}
```

- [ ] **步骤 2：corpus/utf8/ 种子（6-10 条）**

用 printf 写二进制种子（每条覆盖意图写入同目录 README.md）：

```bash
cd tests/fuzz/corpus/utf8
printf 'hello, world' > ascii_basic                       # 纯 ASCII 基线
printf '\xe4\xb8\xad\xe6\x96\x87' > cjk_2x               # 两个合法三字节 CJK
printf '\xc0\x80' > overlong_nul                          # overlong 两字节编码 NUL（非法）
printf '\xe0\x80\x80' > overlong_3byte                    # overlong 三字节形态（非法）
printf '\xed\xa0\x80' > surrogate_d800                    # 代理区编码（非法）
printf '\xf4\x90\x80\x80' > beyond_u10ffff                # 超 U+10FFFF（非法）
printf '\xf8\x88\x80\x80\x80' > five_byte_seq             # 5 字节序列（非法）
printf '\xe4\xb8' > truncated_tail                        # 三字节序列截断（finish 收尾路径）
printf 'a\xe4\xb8\xad\xcc\x81b' > mixed_cjk_combining     # ASCII + CJK + 组合符混合
printf '\x80\x80\xbf' > stray_continuations               # 裸续接字节流（非法起点）
```

README.md 列每条的覆盖意图（格式对照 corpus/ 既有目录是否有 README 先例——有则从，无则新建并说明"每行：文件名 + 覆盖意图"）。

- [ ] **步骤 3：tests/fuzz/CMakeLists.txt 三处追加**

对照文件内 parser/feed 的既有三处写法同款追加 utf8：foreach 的 ITEMS 列表加 fuzz_utf8；file(COPY) 段加 corpus/utf8 到 corpus-work；add_test 段加 fuzz_utf8_smoke（corpus-work/utf8 + 源目录双参数、-max_total_time=30、TIMEOUT 60、ASAN_OPTIONS=symbolize=0 环境，全部与既有 smoke 同款）。

- [ ] **步骤 4：构建 + fuzz smoke 3/3**

```bash
cmake --preset linux-clang-fuzz -D CMAKE_CXX_COMPILER=clang++-20
cmake --build --preset linux-clang-fuzz
ctest --preset linux-clang-fuzz -R fuzz
```

预期：3/3（30s smoke 不撞不变量失败）。**若撞 trap**：先判断言方向——对照 Utf8.h 文档逐条复核；断言错则修断言，decoder 错则停下报 BLOCKED（decoder 是公开 API 内部件，修复需主代理决策），不得带病交付。

- [ ] **步骤 5：本机基线回归**

```bash
ctest --preset linux-gcc-debug
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
doxygen Doxyfile
```

预期：50/50、40/40、50/50、exit 0 零警告（fuzz 文件不进这三个配置，doxygen 不扫 tests/fuzz——确认零警告即可）。

- [ ] **步骤 6：Commit**

```bash
git add tests/fuzz/
git commit -m "test(fuzz): M11 UTF-8 decoder 独立 harness（三路一致性 + 三条不变量）与 utf8 种子目录"
```

---

### 任务 2：种子扩充 + ci-fuzz-deep workflow + dispatch 实证

**文件：**
- 创建：`tests/fuzz/corpus/parser/` 与 `tests/fuzz/corpus/feed/` 新种子数条
- 创建：.github/workflows/ci-fuzz-deep.yml（纯文本引用，code span 以点开头触发 doxygen 陷阱）

- [ ] **步骤 1：feed/parser 种子扩充**

parser 补三条：DEC 私有模式组合（`ESC[?25l ESC[?1049h` 类）、OSC 超长串（百字节级）、CSI 参数溢出形态（多参数大数值）。feed 补两条：resize 高频抖动交织流、大文本混合流（CJK + SGR + soft wrap）。文件名与既有种子风格一致（小写下划线），写入对应目录 README 或在任务报告记录覆盖意图。

- [ ] **步骤 2：新建 ci-fuzz-deep.yml**

upload-artifact 版本先 FetchURL https://github.com/actions/upload-artifact/releases 核最新稳定 Major（M9b 期为 v7，执行期复核）。完整内容：

```yaml
name: CI fuzz-deep（手动触发 / 每周长跑）

on:
  workflow_dispatch:
  schedule:
    - cron: '13 7 * * 0'  # 每周日 07:13 UTC（避整点；schedule 仅默认分支生效，master 推送前以 dispatch 为准）

jobs:
  fuzz-deep:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
      - name: 安装工具链（Clang + Ninja）
        run: |
          sudo apt-get update
          sudo apt-get install -y clang ninja-build
      - name: Fuzz Configure
        run: cmake --preset linux-clang-fuzz
      - name: Fuzz Build
        run: cmake --build --preset linux-clang-fuzz
      - name: 深度 fuzz（三 harness 各 30 分钟）
        env:
          ASAN_OPTIONS: symbolize=0
        run: |
          cd build/linux-clang-fuzz/tests/fuzz
          for t in parser feed utf8; do
            ./fuzz_$t corpus-work/$t "$(pwd)/corpus/$t" \
              -max_total_time=1800 -print_final_stats=1 \
              "-artifact_prefix=$(pwd)/fuzz-artifacts/"
          done
      - name: 上传 crash artifact 与语料（失败时）
        if: failure()
        uses: actions/upload-artifact@v〈N〉
        with:
          name: fuzz-deep-artifacts
          path: build/linux-clang-fuzz/tests/fuzz/fuzz-artifacts/
```

- [ ] **步骤 3：Commit + push + dispatch 实证**

```bash
git add tests/fuzz/corpus/ .github/workflows/ci-fuzz-deep.yml
git commit -m "ci: M11 深度 fuzz workflow（dispatch + 每周 schedule，三 harness 各 30 分钟）与种子扩充"
git push origin contour
gh workflow run ci-fuzz-deep.yml --ref contour
gh run list --workflow=ci-fuzz-deep.yml --limit 1
```

等待完成（上限约 100 分钟：30×3 + 构建）。判据：跑通即成功（三 harness final stats 可见执行轮次与覆盖计数）；**撞 crash 不阻塞**——记录 artifact 与复现输入（gh run download），crash 单元拷入对应 corpus 目录作回归种子并如实记录，修复排期报主代理转用户裁定（规格 §4）。

- [ ] **步骤 4：push CI 六 workflow 绿确认**

```bash
gh run list --branch contour --limit 6
```

预期：同一 push 的六 workflow 全 success（新 smoke 在 ubuntu-clang 的 fuzz 三步内自然覆盖，30s 形态不变）。

---

### 任务 3：记录文档 + 终验

**文件：**
- 创建：`docs/superpowers/specs/2026-09-29-m11-fuzz-deepening-record.md`

- [ ] **步骤 1：记录文档**

骨架（全部实测填充，禁留 〈〉 占位；doxygen 陷阱：行内 code span 禁尖括号/点开头/井号预处理词/反斜杠/双冒号）：

```markdown
# M11 M7a 台账清零记录

- 日期：2026-09-29
- 分支：contour

## 1. decoder harness 断言设计
〈三路解码一致性 + 三条不变量的逐条说明与语义出处（Utf8.h 文档锚点）；smoke 实测轮次〉

## 2. 种子清单与覆盖意图
〈utf8 新目录全量 + feed/parser 扩充条，每条一行〉

## 3. 深度 fuzz 首次运行
〈dispatch run 链接；三 harness 各 30 分钟的 exec/覆盖计数；是否撞 crash 及处置〉

## 4. 台账清零声明
〈M7a 台账两项（深度 fuzz、decoder 独立 harness）全部落账；本机与 CI 证据〉
```

写完 `doxygen Doxyfile` 确认 exit 0 零警告。

- [ ] **步骤 2：全基线终验**

```bash
ctest --preset linux-gcc-debug
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
```

预期：50/50、40/40、50/50、3/3、exit 0 零警告。

- [ ] **步骤 3：Commit + push**

```bash
git add docs/
git commit -m "docs(m11): M7a 台账清零记录（decoder harness + 深度 fuzz 首跑）"
git push origin contour
```

---

## 自检结论

- 规格覆盖：§2 改动点 1/2/3 = 任务 1；改动点 4/5 = 任务 2；§3 验证（dispatch 实证、CI 绿）= 任务 2 步骤 3/4；§4 crash 处置 = 任务 1 步骤 4 分支 + 任务 2 步骤 3 分支；§6 记录 = 任务 3；§5 排除项无任务（正确）。
- 占位符：任务 2 步骤 2 的 〈N〉 为执行期查实指令（用户"最新稳定 Major"规则落地），任务 3 骨架 〈〉 为实测填充指令，均非实现缺口。
- 类型一致性：无新类型；fuzz_utf8.cpp 仅用 Utf8.h 既有公开接口（decodeAll/feed/finish），签名在任务 1 步骤 1 代码中与头文件一致。
