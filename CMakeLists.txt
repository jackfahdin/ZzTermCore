# ZzTermCore 根构建脚本
# 布局约定见 docs/Architecture.md 第 3、4 节（v1 文档；现行设计见 docs/Architecture-v2.md）：
#   - include/ZzTerm/*.h   公开头文件
#   - src/*/*.cpp          Core 实现（parser/unicode/screen/history/input/terminal）
#   - tests/unit/*.cpp     单元测试（见 tests/CMakeLists.txt）
cmake_minimum_required(VERSION 3.26)

project(ZzTermCore
    VERSION 0.1.0
    DESCRIPTION "独立、跨平台、自研的终端模拟器 Core 组件"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

option(ZZTERM_BUILD_TESTS "构建 ZzTermCore 单元测试" ON)

# Contour 后端集成（v2.1 双后端路线；设计见 docs/Architecture-v2.md）。
# submodule 未初始化时自动降级为 OFF，保证 native-only 路径始终可构建。
# M9a：APPLE 默认 OFF（C++23 + configure 期联网拉依赖两大不确定源归 M9b 验证），Linux 维持 ON。
if(APPLE)
    set(ZZTERM_WITH_CONTOUR_DEFAULT OFF)
else()
    set(ZZTERM_WITH_CONTOUR_DEFAULT ON)
endif()
option(ZZTERM_WITH_CONTOUR "集成 Contour vtbackend/vtparser 作为终端后端（需要 C++23 编译器，configure 时需网络拉取第三方依赖）" ${ZZTERM_WITH_CONTOUR_DEFAULT})
if(ZZTERM_WITH_CONTOUR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/contour/src/vtparser/CMakeLists.txt")
        include(cmake/ContourBackend.cmake)
    else()
        message(STATUS "third_party/contour 未初始化（git submodule update --init），ZZTERM_WITH_CONTOUR 降级为 OFF")
        set(ZZTERM_WITH_CONTOUR OFF)
    endif()
endif()

# Fuzz 基建（M7a）：libFuzzer 仅 Clang 可用，默认 OFF。
# ON 时全构建追加 ASan（库与测试同被覆盖，保证 harness 链接一致）；
# libFuzzer 运行时（-fsanitize=fuzzer）只加在 tests/fuzz 的 harness target 上。
# 覆盖率插桩分层：fuzzer-no-link 全构建只插桩（sancov）不含 main/运行时，
# main 与 libFuzzer 运行时仍只进 harness（-fsanitize=fuzzer），否则 cov
# 拿不到库内部反馈、等同盲 fuzz。
option(ZZTERM_FUZZ "构建 libFuzzer Fuzz target（需要 Clang，全构建追加 ASan）" OFF)
if(ZZTERM_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "ZZTERM_FUZZ=ON 需要 Clang（libFuzzer）；当前编译器：${CMAKE_CXX_COMPILER_ID}")
    endif()
    add_compile_options(-fsanitize=address -fsanitize=fuzzer-no-link -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# ---------------------------------------------------------------------------
# ZzTermCore 库目标
# 使用 GLOB + CONFIGURE_DEPENDS 自动收编新文件，新增源码无需修改本文件。
# ---------------------------------------------------------------------------
file(GLOB ZZTERM_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/ZzTerm/*.h")
file(GLOB ZZTERM_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.cpp")

# M0 阶段 src/ 可能尚无实现文件：写入一个空编译单元，
# 保证空目录下 configure/generate/build 同样可用。
if(NOT ZZTERM_SOURCES)
    set(ZZTERM_EMPTY_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/zzterm_empty.cpp")
    file(WRITE "${ZZTERM_EMPTY_SOURCE}"
        "// M0 占位空编译单元：src/ 尚无实现文件时保证 ZzTermCore 目标可生成。\n")
    list(APPEND ZZTERM_SOURCES "${ZZTERM_EMPTY_SOURCE}")
endif()

# 遵循 BUILD_SHARED_LIBS：未显式指定类型时由它决定静态/动态。
add_library(ZzTermCore ${ZZTERM_PUBLIC_HEADERS} ${ZZTERM_SOURCES})
add_library(ZzTerm::Core ALIAS ZzTermCore)

target_compile_features(ZzTermCore PUBLIC cxx_std_20)

# 统一导出宏 ZZTERM_API，定义在 include/ZzTerm/Export.h：
# 静态构建注入 ZZTERM_STATIC（宏为空）；动态构建库本体注入
# ZZTERM_BUILDING_LIBRARY（导出），下游不定义（导入）。
target_compile_definitions(ZzTermCore PRIVATE ZZTERM_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(ZzTermCore PUBLIC ZZTERM_STATIC)
endif()

set_target_properties(ZzTermCore PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(ZzTermCore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# 内部头（如 backend/native/ZzNativeRenderView.h）供 src/ 内部跨目录引用。
target_include_directories(ZzTermCore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# M1b：ZzTermCore 双后端接入——PRIVATE 链接 Contour 后端库并开通编译宏。
# （简报要求在 include(cmake/ContourBackend.cmake) 之后生效；因 ZzTermCore target
#  在上方才创建，实际落点在其后。ContourBackend.cmake 内部的降级 OFF 在此可见。）
if(ZZTERM_WITH_CONTOUR)
    # BUILD_LOCAL_INTERFACE（CMake 3.26+）：Contour 后端为纯内部实现细节，不进 install(EXPORT)。
    target_link_libraries(ZzTermCore PRIVATE $<BUILD_LOCAL_INTERFACE:ZzTermContourBackend>)
    target_compile_definitions(ZzTermCore PRIVATE ZZTERM_WITH_CONTOUR=1)
endif()

# native 后端适配层（src/backend/native/ 为三级深度，src/*/*.cpp GLOB 不覆盖）。
add_subdirectory(src/backend/native)

# 源文件统一 UTF-8（含中文注释），MSVC 需要 /utf-8 才能正确解析；
# 公开头文件同样是 UTF-8，因此该选项对下游也生效。
target_compile_options(ZzTermCore
    PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# ---------------------------------------------------------------------------
# PTY 与示例（Unix：Linux + macOS；Windows ConPTY 里程碑靠后）。
# 必须在 tests 之前：tests/CMakeLists.txt 用 if(TARGET ZzTermPty) 决定是否链接。
# ---------------------------------------------------------------------------
if(UNIX)
    add_subdirectory(pty)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
if(ZZTERM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# 安装与导出：支持 find_package(ZzTermCore)
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

set(ZZTERM_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/ZzTermCore")

install(TARGETS ZzTermCore
    EXPORT ZzTermCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ZzTermCoreTargets
    FILE ZzTermCoreTargets.cmake
    NAMESPACE ZzTerm::
    DESTINATION ${ZZTERM_CMAKE_INSTALL_DIR})

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ZzTermCoreConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ZzTermCoreConfig.cmake"
    INSTALL_DESTINATION ${ZZTERM_CMAKE_INSTALL_DIR})

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ZzTermCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ZzTermCoreConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ZzTermCoreConfigVersion.cmake"
    DESTINATION ${ZZTERM_CMAKE_INSTALL_DIR})
