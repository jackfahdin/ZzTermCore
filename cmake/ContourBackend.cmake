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
# libunicode 有编译产物（unicode/unicode_ucd 静态库）且在下方 PIC set 之前创建，
# 需显式补 PIC，否则 BUILD_SHARED_LIBS=ON 时链接 libZzTermCore.so 报 R_X86_64_PC32。
set_target_properties(unicode unicode_ucd PROPERTIES POSITION_INDEPENDENT_CODE ON)
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

# M1b：ZzTermContourBackend 将 PRIVATE 链入 ZzTermCore；
# BUILD_SHARED_LIBS=ON 时静态库进动态库必须全员 PIC。
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
    # 与 Contour 顶层 CMakeLists.txt:82 一致：crispy-core 假定 Threads::Threads 已由顶层提供。
    find_package(Threads)
    add_subdirectory("${ZZTERM_CONTOUR_ROOT}/src/crispy"   "${CMAKE_BINARY_DIR}/contour/crispy")
    add_subdirectory("${ZZTERM_CONTOUR_ROOT}/src/vtpty"    "${CMAKE_BINARY_DIR}/contour/vtpty")
    add_subdirectory("${ZZTERM_CONTOUR_ROOT}/src/vtparser" "${CMAKE_BINARY_DIR}/contour/vtparser")
    add_subdirectory("${ZZTERM_CONTOUR_ROOT}/src/vtbackend" "${CMAKE_BINARY_DIR}/contour/vtbackend")
endfunction()
zzterm_add_contour_backend()

# Contour targets 只允许被 PRIVATE 链接（Architecture-v2.md §7）；消费方为
# 下方的 ZzTermContourBackend（链接 vtbackend）与测试 test_contour_smoke（链接 vtpty）。

# M1a：ZzContourBackend 核心封装 target（依赖上面聚合好的 vtbackend）。
add_subdirectory("${CMAKE_SOURCE_DIR}/src/backend/contour" "${CMAKE_BINARY_DIR}/src/backend/contour")
