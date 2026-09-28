# M9a macOS 功能移植第一波 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 使 macos-latest runner（Apple Silicon / Apple Clang / libc++）上 core 编译通过、默认 ctest 全绿、PTY 与 examples 解除排除并测试通过。

**架构：** 纯平台适配，无架构变更。4 处源码/构建小改动（PTY 条件包含、根 CMake 解禁、contour APPLE 默认 OFF、test_pty 守卫放宽）+ 本机 libc++ 预检 + 推 contour 走 GitHub Actions macos job 迭代至绿。

**技术栈：** CMake 3.21+ presets、Apple Clang / libc++、POSIX PTY（macOS `<util.h>` openpty）、GitHub Actions、`gh` CLI。

**规格：** docs/superpowers/specs/2026-09-28-m9a-macos-port-design.md（改动点、排除项、红线均以规格为准）

**基线命令（本机 Linux，全程不得回归）：**
- ON：`ctest --preset linux-gcc-debug`（50/50）
- OFF：`ctest --test-dir build/m2-off-check`（40/40）
- shared：`ctest --test-dir build/m2-shared-check`（50/50）
- fuzz：`ctest --preset linux-clang-fuzz -R fuzz`（2/2）
- doxygen：`doxygen Doxyfile`（exit 0 零警告）

---

### 任务 1：四处适配改动（PTY 条件包含 + CMake 解禁 + contour APPLE 默认 OFF + test_pty 守卫）

**文件：**
- 修改：`pty/unix/ZzPty.cpp:8`
- 修改：`CMakeLists.txt`（contour 选项块约 :23、pty/examples 收编点约 :113-118）
- 修改：`tests/unit/test_pty.cpp:2`

- [ ] **步骤 1：ZzPty.cpp 条件包含**

`pty/unix/ZzPty.cpp` 第 8 行：

```cpp
#include <pty.h> // openpty（Linux；macOS 为 <util.h>，M5 处理）
```

改为：

```cpp
#if defined(__APPLE__)
#include <util.h> // openpty（macOS）
#else
#include <pty.h> // openpty（Linux 等 glibc 平台）
#endif
```

- [ ] **步骤 2：根 CMakeLists.txt 解禁 pty/examples（macOS）**

`CMakeLists.txt` 约 :113-118，现状：

```cmake
# ---------------------------------------------------------------------------
# PTY 与示例（仅 Linux；macOS PTY 头文件差异 M5 处理，Windows ConPTY 里程碑靠后）。
# 必须在 tests 之前：tests/CMakeLists.txt 用 if(TARGET ZzTermPty) 决定是否链接。
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    add_subdirectory(pty)
    add_subdirectory(examples)
endif()
```

改为：

```cmake
# ---------------------------------------------------------------------------
# PTY 与示例（Unix：Linux + macOS；Windows ConPTY 里程碑靠后）。
# 必须在 tests 之前：tests/CMakeLists.txt 用 if(TARGET ZzTermPty) 决定是否链接。
# ---------------------------------------------------------------------------
if(UNIX)
    add_subdirectory(pty)
    add_subdirectory(examples)
endif()
```

注意：`pty/CMakeLists.txt` 的 `find_library(ZZTERM_UTIL_LIBRARY util)` 找到才链接，macOS 上 openpty 在 libSystem 内、libutil.dylib 为其 re-export，找到即链接无害，找不到亦不影响——本文件不动。

- [ ] **步骤 3：contour 选项 APPLE 默认 OFF**

`CMakeLists.txt` 约 :21-31，现状：

```cmake
# Contour 后端集成（v2.1 双后端路线；设计见 docs/Architecture-v2.md）。
# submodule 未初始化时自动降级为 OFF，保证 native-only 路径始终可构建。
option(ZZTERM_WITH_CONTOUR "集成 Contour vtbackend/vtparser 作为终端后端（需要 C++23 编译器，configure 时需网络拉取第三方依赖）" ON)
```

在 option 行前插入平台默认值（option 行本身改默认值引用）：

```cmake
# Contour 后端集成（v2.1 双后端路线；设计见 docs/Architecture-v2.md）。
# submodule 未初始化时自动降级为 OFF，保证 native-only 路径始终可构建。
# M9a：APPLE 默认 OFF（C++23 + configure 期联网拉依赖两大不确定源归 M9b 验证），Linux 维持 ON。
if(APPLE)
    set(ZZTERM_WITH_CONTOUR_DEFAULT OFF)
else()
    set(ZZTERM_WITH_CONTOUR_DEFAULT ON)
endif()
option(ZZTERM_WITH_CONTOUR "集成 Contour vtbackend/vtparser 作为终端后端（需要 C++23 编译器，configure 时需网络拉取第三方依赖）" ${ZZTERM_WITH_CONTOUR_DEFAULT})
```

（其后的 `if(ZZTERM_WITH_CONTOUR)` / submodule 降级块不动。）

- [ ] **步骤 4：test_pty.cpp 守卫放宽**

`tests/unit/test_pty.cpp` 第 2 行：

```cpp
#if defined(__unix__) && !defined(__APPLE__)
```

改为：

```cpp
#if defined(__unix__) || defined(__APPLE__)
```

（`#else` 分支平凡 main 空跑通过的结构不动。）

- [ ] **步骤 5：本机 Linux 基线回归（改动不得影响 Linux）**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
doxygen Doxyfile
```

预期：50/50、40/40、50/50、doxygen exit 0 零警告。本步任一项红则停下修复，不进入下一步。

- [ ] **步骤 6：Commit**

```bash
git add pty/unix/ZzPty.cpp CMakeLists.txt tests/unit/test_pty.cpp
git commit -m "feat(pty): M9a macOS 适配四处改动（util.h 条件包含 + CMake 解禁 + contour APPLE 默认 OFF + test_pty 守卫放宽）"
```

---

### 任务 2：本机 libc++ 预检

**文件：** 无入库改动（预检目录在 `build/` 下，已 gitignore）；若预检暴露代码问题，修复各自所属文件并单独 commit。

- [ ] **步骤 1：安装 libc++ 开发包（需 sudo，若已装跳过）**

```bash
dpkg -l | grep -q "libc++-dev" || sudo apt-get install -y libc++-dev libc++abi-dev
```

预期：libc++-dev 与 libc++abi-dev 可用。若 apt 源无对应版本，停下回报用户，不擅自换源。

- [ ] **步骤 2：独立预检目录 configure + build**

```bash
cmake -S . -B build/m9a-libcxx-precheck -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
  -DZZTERM_BUILD_TESTS=ON
cmake --build build/m9a-libcxx-precheck
```

预期：全量编译通过。若报错（头文件传递包含、字面量运算符、charconv 等 libstdc++/libc++ 差异类），最小修复后回到本步重跑；修复不得改变公开 API 语义。

- [ ] **步骤 3：预检目录 ctest**

```bash
ctest --test-dir build/m9a-libcxx-precheck --output-on-failure
```

预期：全绿（计数与 linux-gcc-debug ON 配置一致，50/50）。

- [ ] **步骤 4：若有修复则 Commit**

```bash
git add <修复文件>
git commit -m "fix(libc++): M9a 预检修复<具体问题>"
```

若无修复，跳过本步并在任务回执注明"预检零修复通过"。

---

### 任务 3：CI 触发与 macOS 迭代

**文件：**
- 修改：`.github/workflows/ci.yml:3-5`
- 迭代期可能修改：任意 CI 报错指向的源码文件（最小修复）

- [ ] **步骤 1：ci.yml push branches 加 contour**

`.github/workflows/ci.yml` 现状：

```yaml
on:
  push:
    branches: [main, master]
  pull_request:
```

改为：

```yaml
on:
  push:
    branches: [main, master, contour]
  pull_request:
```

```bash
git add .github/workflows/ci.yml
git commit -m "ci: push 触发器加 contour 分支（M9a macOS 迭代通路，收尾后保留作常态防护）"
```

- [ ] **步骤 2：推送 contour 并触发 CI**

```bash
git push -u origin contour
```

预期：push 成功，macos-clang job 被触发。（推送会同步本地领先 origin 的全部 contour 历史，符合规格 1A 决策。）

- [ ] **步骤 3：观察 macos-clang job 结果**

```bash
gh run list --branch contour --limit 5
gh run watch <run-id>
```

若 `gh` 未安装或未登录，停下回报用户改用 GitHub 网页观察，不擅自安装/登录。

- [ ] **步骤 4：迭代修复至绿**

每个失败：读 job 日志 → 最小修复 → 本机基线回归（至少 linux-gcc-debug 50/50 与 doxygen）→ commit（`fix(macos): <具体问题>`，CI-only 验证的在 message 标注）→ push → 复观。

红线：任何需要修改 `third_party/contour` 才能通过的情况，立即停下回报用户，不擅自绕过（规格 §4）。

预期终点：macos-clang job configure + build + ctest 全绿；ubuntu-clang / ubuntu-gcc / windows-msvc / docs 四 job 同步保持绿。

- [ ] **步骤 5：记录 macOS 实测测试计数**

macos job 绿后，从 CI 日志摘录 ctest 通过计数（预期为 Linux OFF 配置减 bench 三档），留待任务 4 写入记录文档。

---

### 任务 4：记录文档 + checklist 勾选 + 终验

**文件：**
- 创建：`docs/superpowers/specs/2026-09-28-m9a-macos-port-record.md`
- 修改：`docs/VT-Xterm-Checklist.md:305`（macOS PTY TIOCSWINSZ 行）
- 修改：`docs/VT-Xterm-Checklist-v2.md:335`（同上）

- [ ] **步骤 1：编写记录文档**

创建 `docs/superpowers/specs/2026-09-28-m9a-macos-port-record.md`，内容骨架（逐项以实测填充，禁止留空）：

```markdown
# M9a macOS 移植记录

- 日期：2026-09-28
- 分支：contour
- runner：macos-latest（Apple Silicon / Apple Clang / libc++）

## 1. 本机 libc++ 预检发现
<任务 2 的问题清单；零修复则写"零修复通过">

## 2. CI 迭代轮次
<每轮：失败现象 -> 修复 commit -> 结果；一轮过则如实写一轮过>

## 3. 最终结果
- macos-clang job：绿（ctest 计数 N/N，run 链接 <url>）
- 其余四 job：绿
- 本机基线：50/50、40/40、50/50、fuzz 2/2、doxygen 零警告
```

- [ ] **步骤 2：checklist 勾选 macOS PTY TIOCSWINSZ**

`docs/VT-Xterm-Checklist.md` 约 :305：

```markdown
-   [ ] macOS PTY `TIOCSWINSZ`（M5）
```

改为：

```markdown
-   [x] macOS PTY `TIOCSWINSZ`（M9a，CI macos-clang job 实测通过）
```

`docs/VT-Xterm-Checklist-v2.md` 约 :335 同款修改：

```markdown
-   [x] \[Zz-Native\]\[PASS-Native\] macOS PTY `TIOCSWINSZ`（M9a，CI macos-clang job 实测通过）
```

（v2 行的既有前缀格式以文件实际为准，仅把 `[ ]` 改 `[x]` 并追加 M9a 标注。）

- [ ] **步骤 3：全基线终验**

```bash
cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
ctest --test-dir build/m2-off-check
ctest --test-dir build/m2-shared-check
ctest --preset linux-clang-fuzz -R fuzz
doxygen Doxyfile
```

预期：50/50、40/40、50/50、2/2、exit 0 零警告。

- [ ] **步骤 4：Commit**

```bash
git add docs/superpowers/specs/2026-09-28-m9a-macos-port-record.md docs/VT-Xterm-Checklist.md docs/VT-Xterm-Checklist-v2.md
git commit -m "docs(m9a): macOS 移植记录与 checklist 勾选（CI macos-clang 绿）"
```

---

## 自检结论

- 规格覆盖：§2 五处改动 = 任务 1 步骤 1-4 + 任务 3 步骤 1；§3 预检 = 任务 2；§4 CI 迭代与红线 = 任务 3 步骤 3-4；§5 门控与文档勾选 = 任务 4；§6 排除项无对应任务（正确，本波不做）；§7 记录文档 = 任务 4 步骤 1，finishing 合并不在计划内（收尾另行）。
- 占位符：无 TODO/待定；任务 4 步骤 1 的骨架尖括号段为"以实测填充"的指令性占位，属记录文档模板而非实现缺口。
- 类型一致性：无新类型；所有改动为条件编译与 CMake 逻辑。
