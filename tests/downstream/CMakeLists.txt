# M13 下游消费验证工程：不构成本仓库构建的一部分，由 ci-downstream 或手动
# 以安装前缀（CMAKE_PREFIX_PATH）独立 configure——验证 find_package(ZzTermCore)
# 安装态可用（导出目标、头文件、链接），而非 ZzTermCore 行为（行为归主仓测试族）。
cmake_minimum_required(VERSION 3.21)
project(zzterm_downstream_check CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZzTermCore REQUIRED)

add_executable(check main.cpp)
target_link_libraries(check PRIVATE ZzTerm::ZzTermCore)

if(TARGET ZzTerm::Pty)
    target_link_libraries(check PRIVATE ZzTerm::Pty)
    target_compile_definitions(check PRIVATE ZZ_HAVE_PTY_TARGET=1)
endif()
