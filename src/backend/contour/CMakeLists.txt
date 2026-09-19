# ZzTermContourBackend：Contour vtbackend 的 headless 核心封装（M1a）。
# 仅 ZZTERM_WITH_CONTOUR=ON 时由 cmake/ContourBackend.cmake 聚合进来。
# 公开头不暴露 Contour 类型；Contour target 一律 PRIVATE 链接。
add_library(ZzTermContourBackend STATIC
    ZzContourPtyBridge.cpp
)
target_include_directories(ZzTermContourBackend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ZzTermContourBackend PRIVATE vtbackend)
target_compile_features(ZzTermContourBackend PRIVATE cxx_std_23)
