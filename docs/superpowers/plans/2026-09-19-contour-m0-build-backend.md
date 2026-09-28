# Contour M0：最小构建集成与 Backend 边界 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 `contour` 分支完成 v2.1 里程碑 M0——Contour submodule 固定 commit + 最小构建（`cmake/ContourBackend.cmake`）+ Backend 抽象接口与 `ZzTerminal` PImpl 骨架。

**架构：** 方案一（零 patch 聚合）：自建 `cmake/ContourBackend.cmake`，用 CPM 拉取 GSL/libunicode/boxed-cpp/reflection-cpp，目录作用域变量注入顶替 Contour 顶层 `project()` 的角色，只 `add_subdirectory` 四个目标目录（crispy/vtpty/vtparser/vtbackend，全 STATIC，C++23 仅限这四个目录）。`ZzTerminal` 改 PImpl：现有成员与语义方法机械搬入 `ZzTerminal::Impl`（定义在内部头 `src/terminal/TerminalImpl.h`），公开 API 与行为零变化。`ZzTerminalBackend` 内部接口仅定义不实现（M1 接 Contour/native）。

**技术栈：** CMake ≥ 3.19 + Ninja + CPM.cmake v0.43.1；Contour `6777ff05014f8ff163b071e8b0e942830119db80`；C++20（Zz 代码）/ C++23（Contour 四个 target）；g++ 15.2。

**规格：** `docs/superpowers/specs/2026-09-19-contour-m0-build-backend-design.md`（已定稿，勿偏离；本计划与规格冲突时停下来报告）。

**关键事实（来自对 Contour `6777ff0` 的实测调查，直接可用）：**

- 最小 target 集：`crispy-core`（alias `crispy::core`）、`vtpty`、`vtparser`、`vtbackend`，均 STATIC，include 根均为 `${PROJECT_SOURCE_DIR}/src`（指 Contour 仓库根的 src/）。
- vtbackend PUBLIC 链接：`Microsoft.GSL::GSL`、`Threads::Threads`、`contour::tracy`、`crispy::core`、`unicode::unicode`、`vtparser`、`vtpty`。vtpty 在 UNIX 另链接系统 `util`（libutil，glibc 自带）。
- `contour::tracy` 是 INTERFACE 库；`CONTOUR_TRACY=OFF` 等价物 = SYSTEM include 指向 `third_party/contour/src/crispy/tracy-stub`（内含 `tracy/Tracy.hpp` 空实现）。
- vtbackend 需要 5 个 PRIVATE 编译宏（其 CMakeLists:146-152 从顶层变量取值）：`LIBTERMINAL_VERSION_MAJOR/MINOR/PATCH`（取自 `PROJECT_VERSION_*`）、`LIBTERMINAL_VERSION_STRING`（取自 `CONTOUR_VERSION_STRING`）、`LIBTERMINAL_NAME`（取自 `PROJECT_NAME`）。注入这些变量即可，值的内容不影响功能。
- 裁剪选项：`CONTOUR_TESTING=OFF`（级联关闭 CRISPY/VTPTY/VTPARSER/LIBTERMINAL_TESTING）、`CONTOUR_WITH_UTEMPTER=OFF`（避免链接系统 utempter）。libssh2 由 pkg-config 自动探测，本机无此包即自动禁用，无需处理。
- 第三方 pin（与 Contour `cmake/ContourThirdParties.cmake` 一致）：GSL `v3.1.0`、libunicode `v0.9.3`（OPTIONS 见任务 2）、boxed-cpp `v1.4.3`、reflection-cpp `v0.4.0`。target 名：`Microsoft.GSL` 的 `GSL`、`unicode` 的 `unicode`、`boxed-cpp` 的 `boxed-cpp`、`reflection-cpp` 的 `reflection-cpp`。
- `vtparser::Parser` 是模板类（模板形参为事件监听器类型 `EventListener` 与可选的 `TraceStateChanges`），必须带 listener 构造；喂数据方法为 `parseFragment`（接受 gsl::span of const char）。`vtparser::NullParserEvents`（`vtparser/ParserEvents.hpp`）是全部空实现的基类，可直接继承按需 override。注意（任务 4 实测纠正）：bulk 快路径 `print(std::string_view, size_t)` 的返回值被 parser **忽略**（`Parser-impl.hpp:660`），不会退回逐字符 `print(char32_t)`——ASCII 段只走 bulk 通道，需要文本时必须 override bulk 并自行累积。
- vtparser/vtbackend/crispy/vtpty 编译无任何代码生成步骤。
- Doxyfile `INPUT = include docs pty`：`src/` 不被 doxygen 扫描，内部头不影响 doxygen；`include/ZzTerm/Terminal.h` 改动必须保持 doxygen 零 warning。

---

### 任务 1：Contour submodule 固定 + 顶层选项接线

**文件：**
- 创建：`third_party/contour`（git submodule @ `6777ff05`）以及 `.gitmodules`
- 创建：`cmake/ContourBackend.cmake`（本任务仅为占位骨架）
- 修改：`CMakeLists.txt`（第 19 行 option 区后新增选项与 include 调用）

- [ ] **步骤 1：添加 submodule 并固定 commit**

```bash
git submodule add https://github.com/contour-terminal/contour.git third_party/contour
git -C third_party/contour checkout 6777ff05014f8ff163b071e8b0e942830119db80
```

- [ ] **步骤 2：写 ContourBackend.cmake 占位骨架**

创建 `cmake/ContourBackend.cmake`，内容：

```cmake
# Contour 最小构建聚合层（方案一：选择性 add_subdirectory + 目录作用域变量注入，零 patch）。
# 设计：docs/superpowers/specs/2026-09-19-contour-m0-build-backend-design.md
# 本文件由根 CMakeLists.txt 在 ZZTERM_WITH_CONTOUR=ON 且 submodule 已初始化时 include。
# 任务 2/3 将在此填入第三方依赖拉取与四个子目录的聚合。

message(STATUS "ZZTERM_WITH_CONTOUR=ON：Contour submodule 已就位（M0 构建接入进行中）")
```

- [ ] **步骤 3：根 CMakeLists.txt 接线**

在 `CMakeLists.txt` 第 19 行（`option(ZZTERM_BUILD_TESTS ...)`）之后插入：

```cmake
# Contour 后端集成（v2.1 双后端路线；设计见 docs/Architecture-v2.md）。
# submodule 未初始化时自动降级为 OFF，保证 native-only 路径始终可构建。
option(ZZTERM_WITH_CONTOUR "集成 Contour vtbackend/vtparser 作为终端后端（需要 C++23 编译器，configure 时需网络拉取第三方依赖）" ON)
if(ZZTERM_WITH_CONTOUR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/contour/src/vtparser/CMakeLists.txt")
        include(cmake/ContourBackend.cmake)
    else()
        message(STATUS "third_party/contour 未初始化（git submodule update --init），ZZTERM_WITH_CONTOUR 降级为 OFF")
        set(ZZTERM_WITH_CONTOUR OFF)
    endif()
endif()
```

- [ ] **步骤 4：验证 configure/build/test 三路径**

```bash
rm -rf build/linux-gcc-debug
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake -S . -B build/m0-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m0-off-check && (cd build/m0-off-check && ctest --output-on-failure)
```

预期：两条路径 configure/build 全过，12/12 测试全绿；ON 路径打印任务 2 骨架的 STATUS 消息；OFF 路径不打印。验证后删除临时目录 `rm -rf build/m0-off-check`。

- [ ] **步骤 5：Commit**

```bash
git add .gitmodules third_party/contour cmake/ContourBackend.cmake CMakeLists.txt
git commit -m "build(contour): Contour submodule 固定 6777ff05 + ZZTERM_WITH_CONTOUR 选项接线"
```

### 任务 2：第三方依赖拉取（CPM ×4 + tracy stub）

**文件：**
- 修改：`cmake/ContourBackend.cmake`（替换占位骨架为真实内容的前半部分）

- [ ] **步骤 1：实现依赖拉取与 tracy stub**

将 `cmake/ContourBackend.cmake` 内容替换为：

```cmake
# Contour 最小构建聚合层（方案一：选择性 add_subdirectory + 目录作用域变量注入，零 patch）。
# 设计：docs/superpowers/specs/2026-09-19-contour-m0-build-backend-design.md
# 本文件由根 CMakeLists.txt 在 ZZTERM_WITH_CONTOUR=ON 且 submodule 已初始化时 include。

# --- 编译器 C++23 能力门槛（Contour 要求 GCC 14+ / Clang 18+） ---
if((CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
   OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18))
    message(STATUS "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} 不满足 Contour 的 C++23 要求，ZZTERM_WITH_CONTOUR 降级为 OFF")
    set(ZZTERM_WITH_CONTOUR OFF)
    return()
endif()

set(ZZTERM_CONTOUR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/contour")

# --- CPM（与 Contour 顶层 CMakeLists.txt 一致：v0.43.1 + SHA256 校验） ---
file(DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.43.1/CPM.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake"
    EXPECTED_HASH SHA256=1c40fc102ce9625d7de7eb14f541cab30cc3138dca627f0b0ec40293ce6c2934)
include("${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake")

# --- 第三方依赖（pin 与 Contour cmake/ContourThirdParties.cmake 一致） ---
CPMAddPackage(NAME GSL GITHUB_REPOSITORY microsoft/GSL GIT_TAG v3.1.0
    OPTIONS "GSL_TEST=OFF" EXCLUDE_FROM_ALL YES)
CPMAddPackage(NAME libunicode GITHUB_REPOSITORY contour-terminal/libunicode GIT_TAG v0.9.3
    OPTIONS "LIBUNICODE_TESTING OFF" "LIBUNICODE_BENCHMARK OFF" "LIBUNICODE_TOOLS OFF"
            "LIBUNICODE_EXAMPLES OFF" "BUILD_SHARED_LIBS OFF"
    EXCLUDE_FROM_ALL YES)
CPMAddPackage("gh:contour-terminal/boxed-cpp#v1.4.3")
CPMAddPackage("gh:contour-terminal/reflection-cpp#v0.4.0")

foreach(dep IN ITEMS Microsoft.GSL::GSL unicode::unicode boxed-cpp::boxed-cpp reflection-cpp::reflection-cpp)
    if(NOT TARGET ${dep})
        message(FATAL_ERROR "Contour 第三方依赖 ${dep} 拉取失败")
    endif()
endforeach()

# --- contour::tracy stub（等价 Contour CONTOUR_TRACY=OFF 路径） ---
add_library(contour_tracy INTERFACE)
add_library(contour::tracy ALIAS contour_tracy)
target_include_directories(contour_tracy SYSTEM INTERFACE
    $<BUILD_INTERFACE:${ZZTERM_CONTOUR_ROOT}/src/crispy/tracy-stub>)

# --- 裁剪选项（作用于后续 add_subdirectory 的 Contour 子目录） ---
set(CONTOUR_TESTING OFF)          # 级联关闭 CRISPY_/VTPTY_/VTPARSER_/LIBTERMINAL_TESTING
set(CONTOUR_WITH_UTEMPTER OFF)    # 避免 Linux 下链接系统 utempter
```

- [ ] **步骤 2：验证 configure 拉取成功**

```bash
rm -rf build/linux-gcc-debug
cmake --preset linux-gcc-debug
```

预期：configure 通过；输出含 CPM 拉取 GSL/libunicode/boxed-cpp/reflection-cpp 的日志；无 FATAL_ERROR。随后 `cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug` 确认 12/12 不回归（本任务尚未 add_subdirectory，不产生 Contour target）。

- [ ] **步骤 3：Commit**

```bash
git add cmake/ContourBackend.cmake
git commit -m "build(contour): CPM 拉取 GSL/libunicode/boxed-cpp/reflection-cpp + tracy stub"
```

### 任务 3：聚合四个 Contour 子目录（变量注入 + C++23 作用域）

**文件：**
- 修改：`cmake/ContourBackend.cmake`（在任务 2 内容后追加）

- [ ] **步骤 1：实现版本解析与聚合函数**

在 `cmake/ContourBackend.cmake` 末尾追加：

```cmake
# --- Contour 版本（供 vtbackend 的 LIBTERMINAL_VERSION_* 编译宏） ---
# 从 submodule 的 metainfo.xml 解析最新 release 版本；失败回退 0.0.0。
set(ZZTERM_CONTOUR_VERSION "0.0.0")
if(EXISTS "${ZZTERM_CONTOUR_ROOT}/metainfo.xml")
    file(READ "${ZZTERM_CONTOUR_ROOT}/metainfo.xml" _zz_metainfo)
    string(REGEX MATCH "version=\"([0-9]+\\.[0-9]+\\.[0-9]+)" _zz_vm "${_zz_metainfo}")
    if(CMAKE_MATCH_1)
        set(ZZTERM_CONTOUR_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _zz_vp "${ZZTERM_CONTOUR_VERSION}")
set(ZZTERM_CONTOUR_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(ZZTERM_CONTOUR_VERSION_MINOR "${CMAKE_MATCH_2}")
set(ZZTERM_CONTOUR_VERSION_PATCH "${CMAKE_MATCH_3}")
message(STATUS "Contour backend version: ${ZZTERM_CONTOUR_VERSION} (pinned commit 6777ff05)")

# --- 聚合：目录/函数作用域变量注入，顶替 Contour 顶层 project() 的角色 ---
# Contour 四个子目录的 CMakeLists 没有自己的 project()，会读取
# PROJECT_SOURCE_DIR（include 根 ${PROJECT_SOURCE_DIR}/src）、PROJECT_VERSION_*、
# PROJECT_NAME、CONTOUR_VERSION_STRING。函数作用域内 set 的变量会传入
# add_subdirectory 的子目录，从而实现零 patch。该注入依赖 Contour 子目录
# CMake 的当前假设（共约 8 行耦合），Contour 升级时由 smoke/regression 兜底。
function(zzterm_add_contour_backend)
    set(PROJECT_SOURCE_DIR "${ZZTERM_CONTOUR_ROOT}")
    set(PROJECT_NAME "contour")
    set(PROJECT_VERSION_MAJOR "${ZZTERM_CONTOUR_VERSION_MAJOR}")
    set(PROJECT_VERSION_MINOR "${ZZTERM_CONTOUR_VERSION_MINOR}")
    set(PROJECT_VERSION_PATCH "${ZZTERM_CONTOUR_VERSION_PATCH}")
    set(CONTOUR_VERSION_STRING "${ZZTERM_CONTOUR_VERSION}")
    # C++23 仅限 Contour 四个子目录的 target；ZzTermCore 自身保持 C++20。
    set(CMAKE_CXX_STANDARD 23)
    # 复刻 Contour 顶层 CMakeLists.txt:82：crispy-core PUBLIC 链接 Threads::Threads，
    # 而 vtbackend 自带的 find_package(Threads) 是目录作用域，对兄弟目录 crispy 不可见。
    find_package(Threads)
    add_subdirectory("${ZZTERM_CONTOUR_ROOT}/src/crispy"   "${CMAKE_BINARY_DIR}/contour/crispy")
    add_subdirectory("${ZZTERM_CONTOUR_ROOT}/src/vtpty"    "${CMAKE_BINARY_DIR}/contour/vtpty")
    add_subdirectory("${ZZTERM_CONTOUR_ROOT}/src/vtparser" "${CMAKE_BINARY_DIR}/contour/vtparser")
    add_subdirectory("${ZZTERM_CONTOUR_ROOT}/src/vtbackend" "${CMAKE_BINARY_DIR}/contour/vtbackend")
endfunction()
zzterm_add_contour_backend()

# Contour targets 只允许被 PRIVATE 链接（Architecture-v2.md §7）；本里程碑的
# 唯一消费方是 test_contour_smoke（任务 4），M1 的 ZzContourBackend 同样 PRIVATE。
```

注意：若 `ZZTERM_CONTOUR_VERSION` 为 `0.0.0`（metainfo.xml 解析失败），三个分量自然为 0，可接受；configure 日志会打印实际使用的版本值。

- [ ] **步骤 2：全量构建，验证四个静态库产出**

```bash
rm -rf build/linux-gcc-debug
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug
ls build/linux-gcc-debug/contour/crispy/libcrispy-core.a \
   build/linux-gcc-debug/contour/vtpty/libvtpty.a \
   build/linux-gcc-debug/contour/vtparser/libvtparser.a \
   build/linux-gcc-debug/contour/vtbackend/libvtbackend.a
```

预期：configure/build 全过（Contour 编译警告不阻断；我们自己的 target 警告策略不变）；四个 `.a` 文件存在。若 vtpty 报 `utempter` 相关错误，检查 `CONTOUR_WITH_UTEMPTER OFF` 是否生效；若报 libssh2 相关错误，说明本机装了 libssh2 dev 包，停下来报告（不要自行绕过）。

- [ ] **步骤 3：确认 native 测试不回归**

```bash
ctest --preset linux-gcc-debug
```

预期：12/12 全绿。

- [ ] **步骤 4：Commit**

```bash
git add cmake/ContourBackend.cmake
git commit -m "build(contour): 聚合 crispy/vtpty/vtparser/vtbackend 四个最小 target（零 patch 变量注入）"
```

### 任务 4：Contour smoke 测试（compile/link 级）

**文件：**
- 创建：`tests/unit/test_contour_smoke.cpp`
- 修改：`tests/CMakeLists.txt`（GLOB 排除 + 条件注册）

注意：CTest 命名遵循项目约定“文件名即测试名”，故测试名为 `test_contour_smoke`（规格中 `ZzTermContourSmoke` 的提法以本计划为准）。

- [ ] **步骤 1：编写 smoke 测试**

创建 `tests/unit/test_contour_smoke.cpp`：

```cpp
// Contour 后端 smoke：验证 vtparser 的 include 路径 / C++23 / 静态链接三件套。
// 仅构建集成验证，不断言完整终端语义（那是 M1 ZzContourBackend 与兼容性测试的职责）。
#include <vtparser/Parser.hpp>
#include <vtparser/ParserEvents.hpp>

#include <gsl/span>

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

class SmokeEvents : public vtparser::NullParserEvents {
public:
    void print(char32_t cp) override { printed += static_cast<char>(cp); }
    std::size_t print(std::string_view /*chars*/, std::size_t /*cellCount*/) override
    {
        return 0; // 拒绝 bulk 快路径：退回逐字符 print(char32_t)
    }
    void execute(char controlCode) override
    {
        if (controlCode == '\r') ++crCount;
        if (controlCode == '\n') ++lfCount;
    }
    void dispatchCSI(char finalChar) override
    {
        if (finalChar == 'm') ++sgrCount;
    }

    std::string printed;
    int crCount = 0;
    int lfCount = 0;
    int sgrCount = 0;
};

} // namespace

int main()
{
    SmokeEvents events;
    vtparser::Parser<SmokeEvents> parser { events };

    const std::string_view bytes = "\x1b[1;31mZZ\x1b[0m ok\r\n";
    parser.parseFragment(gsl::span<const char> { bytes.data(), bytes.size() });

    assert(events.printed == "ZZ ok");
    assert(events.sgrCount == 2);
    assert(events.crCount == 1);
    assert(events.lfCount == 1);
    return 0;
}
```

- [ ] **步骤 2：tests/CMakeLists.txt 条件接入**

在 `tests/CMakeLists.txt` 的 `file(GLOB ...)` 之后、`foreach` 之前插入排除；在 `foreach` 结束之后插入条件注册：

```cmake
# Contour smoke 仅在 Contour 后端启用时构建（vtparser target 存在才有意义），
# 从通用 GLOB 中剔除后单独注册。
list(REMOVE_ITEM ZZTERM_UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_contour_smoke.cpp")
```

```cmake
if(TARGET vtparser)
    add_executable(test_contour_smoke unit/test_contour_smoke.cpp)
    target_link_libraries(test_contour_smoke PRIVATE vtparser) # Contour target 一律 PRIVATE
    target_compile_features(test_contour_smoke PRIVATE cxx_std_23)
    add_test(NAME test_contour_smoke COMMAND test_contour_smoke)
endif()
```

- [ ] **步骤 3：运行 smoke 确认通过（ON 路径）**

```bash
rm -rf build/linux-gcc-debug
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：13/13 通过（12 个既有 + `test_contour_smoke`）。若 `printed` 断言失败（bulk 退回语义与预期不符），允许仅调整 smoke 的断言方式（例如改为只统计 `print` 两通道合计字符数），但必须在 commit message 中记录实际行为；其它断言不得放宽。

- [ ] **步骤 4：确认 OFF 路径不含 smoke**

```bash
cmake -S . -B build/m0-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m0-off-check && (cd build/m0-off-check && ctest -N)
```

预期：`ctest -N` 列表中无 `test_contour_smoke`，共 12 个测试。验证后 `rm -rf build/m0-off-check`。

- [ ] **步骤 5：Commit**

```bash
git add tests/unit/test_contour_smoke.cpp tests/CMakeLists.txt
git commit -m "test(contour): vtparser compile/link smoke（NullParserEvents + parseFragment 断言）"
```

### 任务 5：ZzTerminalBackend 内部接口 + 编译契约测试

**文件：**
- 创建：`src/backend/ZzTerminalBackend.h`（内部头，不安装）
- 创建：`tests/unit/test_backend_interface.cpp`
- 修改：`tests/CMakeLists.txt`（为该测试追加 src/ include 路径）

- [ ] **步骤 1：定义接口头**

创建 `src/backend/ZzTerminalBackend.h`：

```cpp
#pragma once

// 内部头（不安装、不进 include/ZzTerm/）：终端后端抽象边界。
// 设计见 docs/Architecture-v2.md（v2.1）§7：Public API 不暴露任何后端类型，
// ZzContourBackend（M1）与 ZzNativeBackend 实现本接口。
// 接口面即 ZzTerminal 当前公开语义面；M1 接入首个实现时可按需修订
//（例如 renderView 的返回类型随 ZzRenderView/ZzCellView 设计演化）。

#include <cstddef>
#include <span>
#include <string>

#include "ZzTerm/RenderView.h"
#include "ZzTerm/Screen.h"
#include "ZzTerm/Terminal.h" // ZzTermChanges
#include "ZzTerm/Types.h"

/// 终端后端抽象：feed/resize/渲染访问/状态查询/dirty 复位。
/// 线程安全与 ownership 约定同 ZzTerminal（非线程安全；视图借用后端）。
class ZzTerminalBackend {
public:
    virtual ~ZzTerminalBackend() = default;

    virtual ZzTermChanges feed(std::span<const std::byte> data) = 0;
    virtual bool resize(int cols, int rows) = 0;
    [[nodiscard]] virtual const ZzRenderView& renderView() const noexcept = 0;
    [[nodiscard]] virtual ZzSize size() const noexcept = 0;
    [[nodiscard]] virtual ZzCursorState cursor() const noexcept = 0;
    [[nodiscard]] virtual bool isAlternateScreen() const noexcept = 0;
    [[nodiscard]] virtual const std::string& title() const noexcept = 0;
    virtual void clearDirty() noexcept = 0;
};
```

- [ ] **步骤 2：编写编译契约测试**

创建 `tests/unit/test_backend_interface.cpp`：

```cpp
// ZzTerminalBackend 接口编译契约：证明接口可实例化实现且签名与
// ZzTerminal 公开语义面一致。不含任何真实终端行为（M1 才有实现）。
#include "backend/ZzTerminalBackend.h"

#include <cassert>

namespace {

class FakeBackend : public ZzTerminalBackend {
public:
    FakeBackend() : view_(nullptr, nullptr) {}

    ZzTermChanges feed(std::span<const std::byte> data) override
    {
        fedBytes += data.size();
        return {};
    }
    bool resize(int cols, int rows) override
    {
        lastSize = ZzSize{cols, rows};
        return true;
    }
    const ZzRenderView& renderView() const noexcept override { return view_; }
    ZzSize size() const noexcept override { return lastSize; }
    ZzCursorState cursor() const noexcept override { return {}; }
    bool isAlternateScreen() const noexcept override { return false; }
    const std::string& title() const noexcept override { return title_; }
    void clearDirty() noexcept override { ++clearCount; }

    std::size_t fedBytes = 0;
    ZzSize lastSize{80, 24};
    ZzRenderView view_;
    std::string title_;
    int clearCount = 0;
};

} // namespace

int main()
{
    FakeBackend backend;
    ZzTerminalBackend& base = backend; // 可经由基类指针调用

    const char bytes[] = "hi";
    base.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(bytes), 2));
    assert(backend.fedBytes == 2);

    assert(base.resize(100, 30));
    assert(base.size() == ZzSize{100, 30});
    assert(!base.isAlternateScreen());
    base.clearDirty();
    assert(backend.clearCount == 1);
    return 0;
}
```

注意：若 `ZzRenderView` 的实际构造签名不是 `(nullptr, nullptr)`（以 `include/ZzTerm/RenderView.h` 为准），按真实签名调整 FakeBackend 的构造；若 `ZzSize`/`ZzCursorState` 无可比较/可默认构造的用法，同样按真实定义调整。只允许调整 FakeBackend，不得修改接口头去迁就。

- [ ] **步骤 3：tests/CMakeLists.txt 追加 include 路径**

在 `foreach` 结束后追加：

```cmake
# 内部头（src/）的编译契约测试需要 src/ 在 include 路径上。
if(TARGET test_backend_interface)
    target_include_directories(test_backend_interface PRIVATE "${CMAKE_SOURCE_DIR}/src")
endif()
```

- [ ] **步骤 4：运行测试**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：14/14 通过（13 个 + `test_backend_interface`）。

- [ ] **步骤 5：Commit**

```bash
git add src/backend/ZzTerminalBackend.h tests/unit/test_backend_interface.cpp tests/CMakeLists.txt
git commit -m "feat(backend): ZzTerminalBackend 内部抽象接口与编译契约测试"
```

### 任务 6：ZzTerminal PImpl 重构（行为零变化）

**文件：**
- 修改：`include/ZzTerm/Terminal.h`（私有成员收缩为 Impl 指针）
- 创建：`src/terminal/TerminalImpl.h`（Impl 定义）
- 修改：`src/terminal/Terminal.cpp`、`src/terminal/CsiDispatch.cpp`、`src/terminal/Sgr.cpp`

总规则：这是一次**机械搬运**——所有现有函数体逐字保留，仅改变宿主（`ZzTerminal` → `ZzTerminal::Impl`）与必要的限定符。禁止借重构之名修改任何逻辑。

- [ ] **步骤 1：跑基线**

```bash
ctest --preset linux-gcc-debug
```

预期：14/14 全绿（重构前基线）。

- [ ] **步骤 2：Terminal.h 收缩私有区**

`include/ZzTerm/Terminal.h` 改动：

1. 删除 `#include "ZzTerm/Utf8.h"`，删除第 17-18 行的 `ZzVtParser`/`ZzParamSequence` 前置声明（它们随实现迁入内部头）。保留 `RenderView.h`/`Screen.h`/`Scrollback.h`/`Types.h`（公开签名需要）。
2. 私有区整体替换为：

```cpp
private:
    // PImpl：实现细节（含 native 引擎全部状态）定义在内部头
    // src/terminal/TerminalImpl.h；公开 API 不暴露任何后端类型
    //（docs/Architecture-v2.md §7）。Backend 抽象见 src/backend/ZzTerminalBackend.h。
    class Impl;
    std::unique_ptr<Impl> impl_;
```

3. 类docstring 中“持有并协调 ZzVtParser/ZzScreen/ZzScrollback/ZzRenderView”一句后补一句：“实现经 PImpl 隔离（M0 起），后端抽象边界见 docs/Architecture-v2.md §7。”其余公开 API 文档注释不动。

- [ ] **步骤 3：创建 src/terminal/TerminalImpl.h**

内容（成员清单与原 `ZzTerminal` 私有区一一对应，逐字搬运注释）：

```cpp
#pragma once

// 内部头（不安装）：ZzTerminal 的 PImpl 实现细节。
// 重构规则：本头内容机械搬运自原 include/ZzTerm/Terminal.h 私有区，
// 成员语义与注释保持逐字一致。

#include "ZzTerm/Terminal.h"

#include "ZzTerm/Parser.h"
#include "ZzTerm/Utf8.h"

class ZzTerminal::Impl {
public:
    Impl(int cols, int rows, std::size_t scrollbackMaxLines);

    // ---- 语义方法（原 ZzTerminal 私有方法；实现分布在
    //      Terminal.cpp / CsiDispatch.cpp / Sgr.cpp） ----
    ZzTermChanges feed(std::span<const std::byte> data);
    void putChar(char32_t cp);
    void executeControl(std::uint8_t control);
    void dispatchCsi(const ZzParamSequence& seq);
    void dispatchEsc(std::string_view intermediates, char final);
    void dispatchOsc(std::string_view payload);
    void sgr(const ZzParamSequence& seq);
    [[nodiscard]] ZzCell eraseFill() const noexcept;
    void noteScreenDirty() noexcept;

    struct Sink; // 嵌套类：ZzParserSink 实现，定义在 Terminal.cpp。

    ZzScreen                     screen_;     ///< 工作区（内含 Primary/Alternate）。
    std::unique_ptr<ZzScrollback> scrollback_; ///< 历史后端（接口指针，实现可替换）。
    ZzRenderView                 renderView_; ///< 渲染边界（借用上两者）。
    std::string                  title_;      ///< OSC 标题（UTF-8）。
    std::size_t                  scrolledOutPending_ = 0; ///< feed 内滚出行计数（回调聚合用）。
    std::unique_ptr<Sink>       sink_;    ///< 先于 parser_ 声明：析构逆序保证 parser 先销毁。
    std::unique_ptr<ZzVtParser> parser_;  ///< VT 解析器（语法 dispatch）。
    ZzUtf8Decoder               utf8_;    ///< print 通道 UTF-8 增量解码。
    ZzCellAttributes            penAttrs_; ///< 当前画笔属性（SGR）。
    ZzColor penFg_ = ZzColor::Default();  ///< 当前画笔前景色。
    ZzColor penBg_ = ZzColor::Default();  ///< 当前画笔背景色。
    ZzTermChanges* activeChanges_ = nullptr; ///< feed 期间的变化聚合目标。
};
```

- [ ] **步骤 4：改写 Terminal.cpp**

- 顶部 include 改为 `#include "TerminalImpl.h"` + `#include "ZzTerm/UnicodeWidth.h"`。
- `struct ZzTerminal::Sink` 改为 `struct ZzTerminal::Impl::Sink`，构造函数参数 `ZzTerminal& term` 改为 `Impl& impl`，成员 `term_` 改 `impl_`，方法体内 `term_.xxx` 改 `impl_.xxx`（其余逐字保留）。
- 原 `ZzTerminal::ZzTerminal(...)` 构造函数改为 `ZzTerminal::Impl::Impl(...)`（初始化列表与方法体逐字保留）。
- 原 `feed`/`noteScreenDirty`/`eraseFill`/`putChar`/`executeControl`/`dispatchEsc`/`dispatchOsc` 的限定符 `ZzTerminal::` 改为 `ZzTerminal::Impl::`，函数体逐字保留。
- 文件末尾新增 ZzTerminal 公开 API 的转发层：

```cpp
// ---- 公开 API：全部转发到 Impl（PImpl） ----

ZzTerminal::ZzTerminal(int cols, int rows, std::size_t scrollbackMaxLines)
    : impl_(std::make_unique<Impl>(cols, rows, scrollbackMaxLines))
{
}

ZzTerminal::~ZzTerminal() = default;

ZzTermChanges ZzTerminal::feed(std::span<const std::byte> data) { return impl_->feed(data); }

bool ZzTerminal::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return false;
    if (impl_->screen_.size() == ZzSize{cols, rows})
        return false;
    // M0：网格级 resize，不做 reflow（见 Terminal.h 注释）。
    impl_->screen_.resize(cols, rows);
    return true;
}

const ZzRenderView& ZzTerminal::renderView() const noexcept { return impl_->renderView_; }
ZzSize ZzTerminal::size() const noexcept { return impl_->screen_.size(); }
ZzCursorState ZzTerminal::cursor() const noexcept { return impl_->screen_.cursor(); }
bool ZzTerminal::isAlternateScreen() const noexcept
{
    return impl_->screen_.activeBuffer() == ZzScreenBuffer::Alternate;
}
const std::string& ZzTerminal::title() const noexcept { return impl_->title_; }
void ZzTerminal::clearDirty() noexcept { impl_->screen_.clearDirty(); }
ZzScreen& ZzTerminal::screen() noexcept { return impl_->screen_; }
ZzScrollback& ZzTerminal::scrollback() noexcept { return *impl_->scrollback_; }
```

（原 `ZzTerminal::resize` 方法体按上面转发层版本落定，语义逐字等价。）

- [ ] **步骤 5：改写 CsiDispatch.cpp 与 Sgr.cpp**

- 两个文件的 `#include "ZzTerm/Terminal.h"` 改为 `#include "TerminalImpl.h"`（同目录）。
- `ZzTerminal::dispatchCsi` → `ZzTerminal::Impl::dispatchCsi`，`ZzTerminal::sgr` → `ZzTerminal::Impl::sgr`；函数体逐字保留（成员访问不变，成员已在 Impl 上）。
- 两文件顶部的注释块保留；若注释中有“ZzTerminal 私有方法”之类表述，改为“ZzTerminal::Impl 方法”。

- [ ] **步骤 6：构建 + 全量测试 + doxygen**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
doxygen Doxyfile
```

预期：编译零新增警告；14/14 全绿（与步骤 1 基线一致）；doxygen exit 0 零 warning。

- [ ] **步骤 7：验证动态库构建不回归**

```bash
cmake -S . -B build/shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/shared-check && (cd build/shared-check && ctest --output-on-failure)
```

预期：构建通过、14/14 全绿。

- [ ] **步骤 8：Commit**

```bash
git add include/ZzTerm/Terminal.h src/terminal/TerminalImpl.h src/terminal/Terminal.cpp src/terminal/CsiDispatch.cpp src/terminal/Sgr.cpp
git commit -m "refactor(terminal): ZzTerminal 改 PImpl（Impl 迁入内部头 TerminalImpl.h，行为零变化）"
```

### 任务 7：checklist 同步 + 全量验收

**文件：**
- 修改：`docs/VT-Xterm-Checklist-v2.md`（Build / Backend Boundary 节）

- [ ] **步骤 1：勾选 checklist 条目**

`docs/VT-Xterm-Checklist-v2.md` 的 `## Build / Backend Boundary` 节：

- `[Zz-Adapter] Contour 固定 commit 的 Git submodule` → 勾选，行尾加注释 `（third_party/contour @ 6777ff05）`
- `[Zz-Adapter] 最小 Contour build，不构建顶层完整工程` → 勾选，行尾加注释 `（cmake/ContourBackend.cmake：仅 crispy/vtpty/vtparser/vtbackend）`
- `[Zz-Adapter] vtbackend / vtparser / crispy / libunicode` → 勾选
- `[Zz-Adapter] Contour targets 全部 PRIVATE` → **保持未勾**，行尾加注释 `（当前唯一消费方 test_contour_smoke 为 PRIVATE 链接；接入 Core 在 M1）`
- `ZzExternalTransportAdapter` / `ZzContourEvents` / `ZzRenderView / ZzCellView` 不动（M1）

- [ ] **步骤 2：全量验收（对应规格 §5 验收标准）**

```bash
# 1) 干净路径（模拟新克隆后的完整流程）
rm -rf build/linux-gcc-debug
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
# 2) OFF 路径
cmake -S . -B build/m0-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m0-off-check && (cd build/m0-off-check && ctest --output-on-failure)
# 3) 动态库路径
cmake -S . -B build/shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/shared-check && (cd build/shared-check && ctest --output-on-failure)
# 4) doxygen
doxygen Doxyfile
rm -rf build/m0-off-check
```

预期：ON 路径 14/14；OFF 路径 13/13（12 个既有 + `test_backend_interface`，无 `test_contour_smoke`）；动态库 14/14；doxygen exit 0 零 warning。

另验证 PRIVATE 约束（规格 §5.7）：

```bash
grep -rn 'vtparser\|vtbackend\|crispy\|vtpty' CMakeLists.txt pty/CMakeLists.txt tests/CMakeLists.txt
```

预期：Contour target 名只出现在 `tests/CMakeLists.txt` 的 `test_contour_smoke` 块（PRIVATE 链接）与注释中；`ZzTermCore` 不以任何方式链接 Contour target。

- [ ] **步骤 3：Commit**

```bash
git add docs/VT-Xterm-Checklist-v2.md
git commit -m "docs(checklist): M0 完成项勾选（submodule/最小构建/四组件，PRIVATE 留 M1）"
```
