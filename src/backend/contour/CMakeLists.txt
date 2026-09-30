# ZzTermContourBackend：Contour vtbackend 的 headless 核心封装（M1a）。
# 仅 ZZTERM_WITH_CONTOUR=ON 时由 cmake/ContourBackend.cmake 聚合进来。
# ZzContourBackend.h/ZzContourEvents.h 不暴露 Contour 类型；ZzContourPtyBridge.h 为内部头
# 但随目录 PUBLIC 暴露（含 vtpty/Pty.hpp），仅测试消费，故测试 PRIVATE 链接 vtpty。
# Contour target 一律 PRIVATE 链接。
add_library(ZzTermContourBackend STATIC
    ZzContourPtyBridge.cpp
    ZzContourBackend.cpp
    ZzContourRenderView.cpp
    ZzContourBackendAdapter.cpp
    ZzContourLineSource.cpp
    ZzContourHistoryView.cpp
)
target_include_directories(ZzTermContourBackend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# ZzContourBackend.h 引用 ZzTermCore 公开头（<ZzTerm/Cell.h>，header-only），只引 include 路径、不链接 ZzTermCore。
target_include_directories(ZzTermContourBackend PUBLIC "${CMAKE_SOURCE_DIR}/include")
# M7c：unicode/Utf8Encode.h（src 内部头）经本路径引入，UTF-8 编码与主库统一。
target_include_directories(ZzTermContourBackend PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ZzTermContourBackend PRIVATE vtbackend)
target_compile_features(ZzTermContourBackend PRIVATE cxx_std_23)
