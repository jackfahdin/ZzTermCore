# 单元测试约定（M0）：
#   - tests/unit/*.cpp 每个文件编译为一个独立测试可执行文件；
#   - 每个测试自带 main()，用 assert 或简单宏断言，失败返回非零；
#   - 文件名即 CTest 测试名，需保证在目标命名空间内唯一。
# 新增测试文件无需修改本文件，GLOB + CONFIGURE_DEPENDS 自动收编。
file(GLOB ZZTERM_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")

# Contour smoke 仅在 Contour 后端启用时构建（vtparser target 存在才有意义），
# 从通用 GLOB 中剔除后单独注册。
list(REMOVE_ITEM ZZTERM_UNIT_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_contour_smoke.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_contour_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_contour_adapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_contour_linesource.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_backend_compat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_selection_compat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_search_compat.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_cluster_compat.cpp")

foreach(test_source IN LISTS ZZTERM_UNIT_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE ZzTermCore)
    if(TARGET ZzTermPty)
        target_link_libraries(${test_name} PRIVATE ZzTermPty)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# 内部头（src/）的编译契约测试需要 src/ 在 include 路径上。
if(TARGET test_backend_interface)
    target_include_directories(test_backend_interface PRIVATE "${CMAKE_SOURCE_DIR}/src")
    # OFF 路径断言（Contour kind 抛 logic_error）依赖该宏透传。
    target_compile_definitions(test_backend_interface
        PRIVATE $<$<BOOL:${ZZTERM_WITH_CONTOUR}>:ZZTERM_WITH_CONTOUR=1>)
endif()

# test_reflow 直接调用内部纯函数 zzReflowLines（src/screen/Reflow.h，无
# ZZTERM_API 导出）：shared 构建（hidden visibility）下须把实现文件一并
# 编进测试可执行文件，否则链接不到符号。
if(TARGET test_reflow)
    target_sources(test_reflow PRIVATE "${CMAKE_SOURCE_DIR}/src/screen/Reflow.cpp")
endif()

# test_selection / test_selection_text 直接调用 src/terminal 内部实现（无
# ZZTERM_API 导出）：shared 构建下须把实现文件编进测试可执行文件。
if(TARGET test_selection)
    target_sources(test_selection PRIVATE "${CMAKE_SOURCE_DIR}/src/terminal/ZzSelection.cpp")
endif()
if(TARGET test_selection_text)
    target_sources(test_selection_text PRIVATE "${CMAKE_SOURCE_DIR}/src/terminal/ZzSelectionText.cpp")
endif()
if(TARGET test_native_linesource)
    target_sources(test_native_linesource PRIVATE
        "${CMAKE_SOURCE_DIR}/src/backend/native/ZzNativeLineSource.cpp")
endif()
if(TARGET test_search_state)
    target_sources(test_search_state PRIVATE "${CMAKE_SOURCE_DIR}/src/terminal/ZzSearchState.cpp")
endif()
if(TARGET test_search)
    target_sources(test_search PRIVATE
        "${CMAKE_SOURCE_DIR}/src/terminal/ZzSearch.cpp"
        "${CMAKE_SOURCE_DIR}/src/terminal/ZzSelectionText.cpp")
endif()

# M7b：聚簇 golden 测试的数据路径与 shared 构建内部符号。
# GraphemeBreak.h 为 src/ 内部头（经 "unicode/GraphemeBreak.h" 引用），
# 需 src/ 在 include 路径上（先例见上方 test_backend_interface）。
if(TARGET test_grapheme_break)
    target_include_directories(test_grapheme_break PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(test_grapheme_break
        PRIVATE ZZ_GRAPHEME_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data/GraphemeBreakTest.txt")
    target_sources(test_grapheme_break PRIVATE "${CMAKE_SOURCE_DIR}/src/unicode/GraphemeBreak.cpp")
endif()

# M7b：聚簇 parity 探针（开发期工具，不注册 ctest），仅 Contour 后端启用时构建。
if(TARGET ZzTermContourBackend)
    add_executable(cluster_probe probe/cluster_probe.cpp)
    target_link_libraries(cluster_probe PRIVATE ZzTermCore) # Contour target 一律 PRIVATE
endif()

if(TARGET vtparser)
    add_executable(test_contour_smoke unit/test_contour_smoke.cpp)
    target_link_libraries(test_contour_smoke PRIVATE vtparser) # Contour target 一律 PRIVATE
    target_compile_features(test_contour_smoke PRIVATE cxx_std_23)
    add_test(NAME test_contour_smoke COMMAND test_contour_smoke)
endif()

# M1a：ZzContourBackend headless 回归测试，仅 Contour 后端启用时构建。
# ZzTermCore 自 M5a 起必需：ZzContourBackend 的快照口产出 ZzLine
# （src/screen/Line.cpp，非 header-only，不在 contour 后端库内）。
if(TARGET ZzTermContourBackend)
    add_executable(test_contour_backend unit/test_contour_backend.cpp)
    target_link_libraries(test_contour_backend PRIVATE ZzTermContourBackend ZzTermCore vtpty) # Contour target 一律 PRIVATE
    target_compile_features(test_contour_backend PRIVATE cxx_std_23)
    add_test(NAME test_contour_backend COMMAND test_contour_backend)
endif()

# M1b：ZzContourBackendAdapter 经 ZzTerminal facade 的端到端测试，仅 Contour 后端启用时构建。
# 只碰公开头（C++20 即可），仅链接 ZzTermCore（多链 ZzTermContourBackend 会在
# shared 构建下把静态 contour 库的第二份副本链进测试可执行文件）。
if(TARGET ZzTermContourBackend)
    add_executable(test_contour_adapter unit/test_contour_adapter.cpp)
    target_link_libraries(test_contour_adapter PRIVATE ZzTermCore) # Contour target 一律 PRIVATE
    add_test(NAME test_contour_adapter COMMAND test_contour_adapter)
endif()

# M5a：ZzContourLineSource 数据源测试，仅 Contour 后端启用时构建。
# 除 ZzTermContourBackend 外还需 ZzTermCore：ZzLine 实现（src/screen/Line.cpp）
# 不在 contour 后端库内（该库此前只消费 header-only 的 ZzCell）。
if(TARGET ZzTermContourBackend)
    add_executable(test_contour_linesource unit/test_contour_linesource.cpp)
    target_link_libraries(test_contour_linesource PRIVATE ZzTermContourBackend ZzTermCore vtpty) # Contour target 一律 PRIVATE
    target_compile_features(test_contour_linesource PRIVATE cxx_std_23)
    add_test(NAME test_contour_linesource COMMAND test_contour_linesource)
endif()

# M1b：双后端对照测试，同一 VT 序列喂 Native/Contour 逐格比对（native 为基准）。
# 只碰公开头（C++20 即可），仅链接 ZzTermCore。
if(TARGET ZzTermContourBackend)
    add_executable(test_backend_compat unit/test_backend_compat.cpp)
    target_link_libraries(test_backend_compat PRIVATE ZzTermCore)
    add_test(NAME test_backend_compat COMMAND test_backend_compat)
endif()

# M5a：双后端选区提取 compat（同一脚本 selectedText 逐字节一致，native 为基准）。
# 只碰公开头，仅链接 ZzTermCore（同 test_backend_compat 的 shared 构建理由）。
if(TARGET ZzTermContourBackend)
    add_executable(test_selection_compat unit/test_selection_compat.cpp)
    target_link_libraries(test_selection_compat PRIVATE ZzTermCore)
    add_test(NAME test_selection_compat COMMAND test_selection_compat)
endif()

# M5b：双后端搜索 compat（同一脚本 match 列表逐一相等，native 为基准）。
# 只碰公开头，仅链接 ZzTermCore（同 test_backend_compat 的 shared 构建理由）。
if(TARGET ZzTermContourBackend)
    add_executable(test_search_compat unit/test_search_compat.cpp)
    target_link_libraries(test_search_compat PRIVATE ZzTermCore)
    add_test(NAME test_search_compat COMMAND test_search_compat)
endif()

# M7b：双后端聚簇 compat（同一聚簇脚本逐格比对，native 以 contour 为基准）。
if(TARGET ZzTermContourBackend)
    add_executable(test_cluster_compat unit/test_cluster_compat.cpp)
    target_link_libraries(test_cluster_compat PRIVATE ZzTermCore)
    add_test(NAME test_cluster_compat COMMAND test_cluster_compat)
endif()

# 脚本化冒烟：PTY 驱动 Core 全链路——真实 bash 输出经 ZzTerminal 渲染回 stdout，
# 断言标记串出现且以子进程退出码 0 退出（仅 Unix 构建存在 demo 目标时）。
if(TARGET ZzTermSmoke)
    foreach(ZZTERM_SMOKE_BACKEND IN ITEMS native contour)
        if(ZZTERM_SMOKE_BACKEND STREQUAL "contour" AND NOT TARGET ZzTermContourBackend)
            continue() # OFF 构建无 Contour 后端
        endif()
        add_test(NAME ZzTermSmokeEcho_${ZZTERM_SMOKE_BACKEND}
            COMMAND ZzTermSmoke --backend=${ZZTERM_SMOKE_BACKEND} -- bash -c "echo zz-smoke-ok")
        set_tests_properties(ZzTermSmokeEcho_${ZZTERM_SMOKE_BACKEND} PROPERTIES
            PASS_REGULAR_EXPRESSION "zz-smoke-ok"
            TIMEOUT 30)
    endforeach()

    # 交互式冒烟：pexpect 驱动真实 PTY + pyte 独立终端模拟器交叉验证渲染
    #（bash 提示符/echo/ls 着色/SIGWINCH/less/vim/Ctrl+C/退出码）。
    # 依赖 python3 + pexpect + pyte，缺失时静默跳过（不影响基础套件）。
    find_program(ZZTERM_PYTHON3 NAMES python3)
    if(ZZTERM_PYTHON3)
        execute_process(
            COMMAND ${ZZTERM_PYTHON3} -c "import pexpect, pyte"
            RESULT_VARIABLE ZZTERM_PY_DEPS
            OUTPUT_QUIET ERROR_QUIET)
        if(ZZTERM_PY_DEPS EQUAL 0)
            foreach(ZZTERM_SMOKE_BACKEND IN ITEMS native contour)
                if(ZZTERM_SMOKE_BACKEND STREQUAL "contour" AND NOT TARGET ZzTermContourBackend)
                    continue()
                endif()
                add_test(NAME ZzTermSmokeInteractive_${ZZTERM_SMOKE_BACKEND}
                    COMMAND ${ZZTERM_PYTHON3}
                        ${CMAKE_CURRENT_SOURCE_DIR}/interactive/verify_smoke.py
                        $<TARGET_FILE:ZzTermSmoke>
                        ${CMAKE_CURRENT_BINARY_DIR}/interactive-scratch-${ZZTERM_SMOKE_BACKEND}
                        ${ZZTERM_SMOKE_BACKEND})
                set_tests_properties(ZzTermSmokeInteractive_${ZZTERM_SMOKE_BACKEND} PROPERTIES TIMEOUT 180)
            endforeach()
        else()
            message(STATUS "python3 pexpect/pyte 不可用，跳过 ZzTermSmokeInteractive")
        endif()
    endif()
endif()

# M7a：Fuzz harness 与 smoke，仅 ZZTERM_FUZZ=ON（linux-clang-fuzz preset）时收录；
# OFF 路径本目录完全不参与编译（第一原则）。
if(ZZTERM_FUZZ)
    add_subdirectory(fuzz)
endif()

# M8：百万行实验 benchmark（tests/bench/，单元轨 + facade 轨双 harness）。
# 短跑档（10k）注册默认 ctest；长跑档（100k/1M）经 bench-long 自定义目标手动触发
#（cmake --build <dir> --target bench-long），不进默认 ctest——ctest 无单测试默认
# 排除机制，label 方案需改动既有 ctest --preset 基线命令，故用构建目标。
# RSS 采样：Linux 与 macOS 均有实现（M9b 补齐 mach task_info 路径），其他平台返回 0。
if(UNIX)
    add_executable(zz_bench_scrollback bench/zz_bench_scrollback.cpp bench/bench_common.cpp)
    target_link_libraries(zz_bench_scrollback PRIVATE ZzTermCore)
    add_test(NAME zz_bench_scrollback COMMAND zz_bench_scrollback)
    set_tests_properties(zz_bench_scrollback PROPERTIES TIMEOUT 60)

    add_executable(zz_bench_feed bench/zz_bench_feed.cpp bench/bench_common.cpp)
    target_link_libraries(zz_bench_feed PRIVATE ZzTermCore)
    add_test(NAME zz_bench_feed_ascii COMMAND zz_bench_feed --profile=ascii)
    add_test(NAME zz_bench_feed_mixed COMMAND zz_bench_feed --profile=mixed)
    set_tests_properties(zz_bench_feed_ascii zz_bench_feed_mixed PROPERTIES TIMEOUT 60)

    add_custom_target(bench-long
        COMMAND zz_bench_scrollback --tier=100k
        COMMAND zz_bench_scrollback --tier=1m
        COMMAND zz_bench_feed --profile=ascii --tier=100k
        COMMAND zz_bench_feed --profile=mixed --tier=100k
        COMMAND zz_bench_feed --profile=ascii --tier=1m
        COMMAND zz_bench_feed --profile=mixed --tier=1m
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "M8 长跑 benchmark（100k/1M，JSON 落盘 ${CMAKE_CURRENT_BINARY_DIR}）")
endif()
