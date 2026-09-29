# ZzTermPty：PTY 薄封装（纯 OS 模块，不依赖 ZzTermCore；Unix openpty / Windows ConPTY）。
if(WIN32)
    add_library(ZzTermPty windows/ZzPty.cpp ZzPty.h ZzPtyExport.h)
else()
    add_library(ZzTermPty unix/ZzPty.cpp ZzPty.h ZzPtyExport.h)
endif()
add_library(ZzTerm::Pty ALIAS ZzTermPty)

target_compile_features(ZzTermPty PUBLIC cxx_std_20)

# 导出宏约定与 ZzTermCore 一致（见 ZzPtyExport.h）。
target_compile_definitions(ZzTermPty PRIVATE ZZTERM_PTY_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(ZzTermPty PUBLIC ZZTERM_PTY_STATIC)
endif()

set_target_properties(ZzTermPty PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# 公开头以 "ZzPty.h" 形式引用，include 根即 pty/ 目录。
target_include_directories(ZzTermPty
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

# openpty 在部分 Unix 平台位于 libutil（glibc >= 2.34 已并入 libc），找到才链接。
if(NOT WIN32)
    find_library(ZZTERM_UTIL_LIBRARY util)
    if(ZZTERM_UTIL_LIBRARY)
        target_link_libraries(ZzTermPty PRIVATE ${ZZTERM_UTIL_LIBRARY})
    endif()
endif()
