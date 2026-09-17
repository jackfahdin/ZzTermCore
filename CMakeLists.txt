# ZzTermCore 根构建脚本
# 布局约定见 docs/Architecture.md 第 3、4 节：
#   - include/ZzTerm/*.h   公开头文件
#   - src/*/*.cpp          Core 实现（parser/unicode/screen/history/input/terminal）
#   - tests/unit/*.cpp     单元测试（见 tests/CMakeLists.txt）
cmake_minimum_required(VERSION 3.19)

project(ZzTermCore
    VERSION 0.1.0
    DESCRIPTION "独立、跨平台、自研的终端模拟器 Core 组件"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

option(ZZTERM_BUILD_TESTS "构建 ZzTermCore 单元测试" ON)

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

# 源文件统一 UTF-8（含中文注释），MSVC 需要 /utf-8 才能正确解析；
# 公开头文件同样是 UTF-8，因此该选项对下游也生效。
target_compile_options(ZzTermCore
    PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# ---------------------------------------------------------------------------
# PTY 与示例（仅 Linux；macOS PTY 头文件差异 M5 处理，Windows ConPTY 里程碑靠后）。
# 必须在 tests 之前：tests/CMakeLists.txt 用 if(TARGET ZzTermPty) 决定是否链接。
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
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
