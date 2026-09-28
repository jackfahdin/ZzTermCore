# M0：Contour 最小构建集成与 Backend 边界 — 设计规格

> 2026-09-19 · 分支 `contour` · 对应 `docs/Architecture-v2.md`（v2.1）整合版里程碑 M0
> 本规格经头脑风暴确认：pin commit（1C）、M0 范围（2A）、CPM 依赖获取（3A）、嵌入方式（方案一）。

## 1. 背景与目标

ZzTermCore 已确认双后端路线（v2.1）：Contour `vtbackend`/`vtparser` 为默认后端，
现有自研引擎保留为一等后端 `ZzNativeBackend`。本规格覆盖整合版里程碑 M0：

-   Contour submodule 固定 commit、最小 Contour 构建（`cmake/ContourBackend.cmake`）、
    Backend 边界与 Public API/PImpl。

`ZzContourBackend` 的真正实现属于 M1，不在本规格范围。

## 2. 已确认的决策

| 决策点 | 结论 |
|---|---|
| Contour 固定 commit | `6777ff05014f8ff163b071e8b0e942830119db80`（PoC 实际验证过的 commit，即当时 master HEAD） |
| M0 范围 | 构建集成 + Backend 抽象接口 + `ZzTerminal` PImpl 骨架；native 实现行为零变化 |
| 第三方依赖获取 | 沿用 Contour 的 CPM 机制，configure 时拉取固定版本（需网络） |
| 嵌入方式 | 方案一：选择性 `add_subdirectory` + 目录作用域变量注入，零 patch |

## 3. 关键事实（Contour `6777ff0` 实测调查结论）

-   最小 target 集（全 STATIC）：`crispy-core` → `vtpty` → `vtparser` → `vtbackend`。
-   第三方依赖闭包（均 PUBLIC 链接）：Microsoft.GSL、libunicode ≥ 0.9.3
    （CPM `contour-terminal/libunicode` v0.9.3，target `unicode::unicode`）、
    boxed-cpp（target 同名 `boxed-cpp`）、reflection-cpp（target 同名 `reflection-cpp`）、
    `Threads::Threads`；另有 `contour::tracy` INTERFACE（`CONTOUR_TRACY=OFF` 时
    用 `src/crispy/tracy-stub/` 空实现头）。
-   不需要：net、OpenSSL、libssh2（系统不装即自动禁用）、Qt、freetype、harfbuzz、
    yaml-cpp、fmt、range-v3。
-   `vtpty` 是 vtbackend 的 PUBLIC 硬依赖（公共头互包含），headless 也必须编译
    vtpty；运行期用 `vtpty::MockViewPty`/`MockPty` 喂。需 `-DCONTOUR_WITH_UTEMPTER=OFF`
    避免 Linux 下链接系统 utempter。
-   C++23 强制（`std::format`/`std::expected`/`std::ranges::to` 等），要求
    GCC 14+ / Clang 18+；本机 g++ 15.2 满足。
-   顶层 `cmake/Version.cmake` 用 `CMAKE_SOURCE_DIR` 找版本文件，嵌入构建必炸——
    方案一绕开顶层 CMake，不涉及该文件。
-   四个子目录 CMakeLists 均无自己的 `project()`，对顶层的耦合共约 8 行：
    各 1 处 `BUILD_INTERFACE` 形式的 include 根 `${PROJECT_SOURCE_DIR}/src`（4 处），
    vtbackend 另 4 行 `LIBTERMINAL_VERSION_*=${PROJECT_VERSION_*}` 与
    `CONTOUR_VERSION_STRING` 编译宏。
-   头文件形式为尖括号 include：`vtparser/Parser.hpp`、`vtbackend/screen/Terminal.hpp`，
    include 根为 Contour 的 `src/`。
-   headless 驱动范例：`src/vtbackend/bench-headless.cpp`；集成测试参考：
    `src/vtbackend/` 的 `vtbackend_test`（60+ 测试文件，Catch2）。

## 4. 设计

### 4.1 仓库变更

```
third_party/contour/          # 新增 git submodule，固定 6777ff05
cmake/ContourBackend.cmake    # 新增：最小构建聚合层
src/backend/
└── ZzTerminalBackend.h       # 新增：内部 backend 抽象接口（非公开头）
tests/unit/test_contour_smoke.cpp  # 新增：compile/link 级 smoke（条件构建）
```

`.gitmodules` 新增 `third_party/contour` 条目。根 `CMakeLists.txt` 新增
`ZZTERM_WITH_CONTOUR` 选项与 `include(cmake/ContourBackend.cmake)` 调用点。

### 4.2 ContourBackend.cmake（方案一：零 patch 聚合）

职责：

1.  定义选项 `ZZTERM_WITH_CONTOUR`（默认 ON；检测到编译器不支持 C++23 时
    自动降级 OFF 并仅构建 native，打印状态消息）。
2.  以 CPM（沿用 Contour 顶层使用的 CPM v0.43.1 机制）拉取固定版本第三方：
    Microsoft.GSL、libunicode v0.9.3、boxed-cpp、reflection-cpp——版本号与
    Contour `cmake/ContourThirdParties.cmake` 中的 pin 保持一致。
3.  定义 `contour::tracy` INTERFACE target，SYSTEM include 指向
    `third_party/contour/src/crispy/tracy-stub/`（等价于 `CONTOUR_TRACY=OFF` 路径）。
4.  目录作用域变量注入（顶替 Contour 顶层 `project()` 的角色）：
    -   `PROJECT_SOURCE_DIR` = contour 仓库根（使其 `${PROJECT_SOURCE_DIR}/src`
        include 根解析正确）；
    -   `PROJECT_VERSION_MAJOR/MINOR/PATCH` 与 `CONTOUR_VERSION_STRING`（供
        vtbackend 的 `LIBTERMINAL_VERSION_*` 编译宏；版本值从 submodule 内
        `metainfo.xml` 的最新 release 条目解析，解析失败则回退为 `0.0.0` 加
        commit 短哈希，并在 configure 日志中打印实际使用的值）；
    -   `CMAKE_CXX_STANDARD 23` 目录作用域设置（仅 Contour 四个子目录的 target
        生效；ZzTermCore 自身 target 保持 C++20）。
5.  预置裁剪选项：`CONTOUR_WITH_UTEMPTER=OFF`、`CONTOUR_TESTING=OFF`、
    `VTPARSER_TESTING=OFF`、`LIBTERMINAL_TESTING=OFF`、`CRISPY_TESTING=OFF`、
    `VTPTY_TESTING=OFF`、`CONTOUR_TRACY=OFF`。
6.  仅 `add_subdirectory` 四个目录（各自指定独立 binary dir）：
    `src/crispy`、`src/vtpty`、`src/vtparser`、`src/vtbackend`。
    不引入 `src/net`、`src/contour`、`text_shaper`、`vtrasterizer` 等。
7.  导出供 Core 使用的变量/target 清单；Contour targets 后续只许以 PRIVATE
    方式链接（本里程碑由 smoke 测试验证链接可行性）。

已知脆弱点：变量注入依赖 Contour 子目录 CMake 的当前假设（上述 8 行耦合）。
Contour 升级时由 upgrade gate（smoke + regression）兜底；若注入失效再退到
patch 文件方案（`cmake/patches/`）。

### 4.3 Backend 抽象接口与 PImpl 骨架

-   新增内部头 `src/backend/ZzTerminalBackend.h`（不安装、不进 `include/ZzTerm/`）：
    定义 `ZzTerminalBackend` 抽象接口，覆盖 feed、resize、screen/render 访问、
    terminal modes、cursor/state、input/output、events、dirty/update 通知的
    最小虚函数集。M0 只定义接口，不要求完整实现。
-   `ZzTerminal` 增加 PImpl：公开头仅新增 `class Impl;` 前向声明与对应的
    `std::unique_ptr` 成员（及必要的析构/移动声明）；现有
    Screen/Parser/dispatch 成员机械搬入 `Impl`。
-   公开 API 签名不变；不暴露任何后端类型（Contour 或 native）。
-   native 实现此里程碑不接 `ZzTerminalBackend` 接口（避免无需求的抽象扭转）；
    M1 实现 `ZzContourBackend` 时一并让 native 实现该接口。

### 4.4 Smoke 测试

新增 `tests/unit/test_contour_smoke.cpp`，仅在 `ZZTERM_WITH_CONTOUR=ON` 时编译：

-   以尖括号 include `vtparser/Parser.hpp`，实例化 `vtparser::Parser` 并喂入
    一段最小字节流（如 `"$ echo hi\r\n"`），验证事件回调触发；
-   链接 `vtparser` 静态库（其 PUBLIC 传递依赖随之带入），证明 include 路径、
    C++23、链接三件套打通；
-   接入 CTest（命名 `ZzTermContourSmoke`），随现有测试套件运行。

### 4.5 文档与 checklist 同步

-   `docs/VT-Xterm-Checklist-v2.md` 勾选/标注：Contour submodule 固定 commit、
    最小 Contour build、vtbackend/vtparser/crispy/libunicode 四组件、
    Contour targets 全部 PRIVATE 等 Build / Backend Boundary 节条目。
-   `docs/API.md`：PImpl 属内部变更，公开 API 签名不变，无需更新；
    doxygen 保持零 warning。

## 5. 验收标准（Definition of Done）

1.  干净克隆 + `git submodule update --init` 后，
    `cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug`
    一次通过（configure 阶段 CPM 拉取第三方成功）。
2.  `ZZTERM_WITH_CONTOUR=ON` 时 `ZzTermContourSmoke` 编译、链接、运行通过。
3.  现有 12/12 测试零改动全绿（PImpl 重构无行为变化）。
4.  `ZZTERM_WITH_CONTOUR=OFF` 时完整构建 + 12/12 测试全绿（native-only 路径不回归）。
5.  动态库构建（`BUILD_SHARED_LIBS=ON`）两种选项组合均通过。
6.  `doxygen Doxyfile` 零 warning。
7.  Contour targets 未出现在 `ZzTermCore` 的 PUBLIC/INTERFACE 链接中
    （以 `cmake --build` 后检查或 smoke 的链接方式验证）。
8.  checklist 条目勾选与实现状态一致。

## 6. 明确不做（YAGNI）

-   不实现 `ZzContourBackend`（M1）；不改 `ZzTerminal` 公开行为；
    不让 native 实现接 backend 接口（M1 一并做）。
-   不引入 Contour GUI/Renderer/net/OpenSSL/libssh2/freetype/harfbuzz/yaml-cpp。
-   不维护 Contour patch 文件（方案一零 patch；失效时再议）。
-   不做 Windows ConPTY/macOS/CI 矩阵扩展（后续里程碑）。
-   不迁移 native 代码到 `src/backend/native/`（可选规划项，本里程碑不动）。

## 7. 风险与对策

| 风险 | 对策 |
|---|---|
| CPM configure 需网络 | 已接受（决策 3A）；失败时报错信息指引检查网络 |
| 变量注入依赖 Contour 子目录 CMake 假设 | upgrade gate（smoke/regression）兜底；失效退 patch 文件方案 |
| g++ 之外的编译器 C++23 兼容性 | 本里程碑仅本机 g++ 15.2 验证；Clang/MSVC 留 CI（checklist 未勾项） |
| vtpty 编译牵扯平台代码 | 仅 Linux 验证；`CONTOUR_WITH_UTEMPTER=OFF`、无 libssh2 环境 |
