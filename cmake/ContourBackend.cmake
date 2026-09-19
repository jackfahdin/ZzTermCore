# Contour 最小构建聚合层（方案一：选择性 add_subdirectory + 目录作用域变量注入，零 patch）。
# 设计：docs/superpowers/specs/2026-09-19-contour-m0-build-backend-design.md
# 本文件由根 CMakeLists.txt 在 ZZTERM_WITH_CONTOUR=ON 且 submodule 已初始化时 include。
# 任务 2/3 将在此填入第三方依赖拉取与四个子目录的聚合。

message(STATUS "ZZTERM_WITH_CONTOUR=ON：Contour submodule 已就位（M0 构建接入进行中）")
