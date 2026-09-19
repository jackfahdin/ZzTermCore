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
