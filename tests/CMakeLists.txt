# 单元测试约定（M0）：
#   - tests/unit/*.cpp 每个文件编译为一个独立测试可执行文件；
#   - 每个测试自带 main()，用 assert 或简单宏断言，失败返回非零；
#   - 文件名即 CTest 测试名，需保证在目标命名空间内唯一。
# 新增测试文件无需修改本文件，GLOB + CONFIGURE_DEPENDS 自动收编。
file(GLOB ZZTERM_UNIT_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp")

foreach(test_source IN LISTS ZZTERM_UNIT_TEST_SOURCES)
    get_filename_component(test_name "${test_source}" NAME_WE)
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE ZzTermCore)
    if(TARGET ZzTermPty)
        target_link_libraries(${test_name} PRIVATE ZzTermPty)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# 脚本化冒烟：PTY 驱动 Core 全链路——真实 bash 输出经 ZzTerminal 渲染回 stdout，
# 断言标记串出现且以子进程退出码 0 退出（仅 Unix 构建存在 demo 目标时）。
if(TARGET ZzTermSmoke)
    add_test(NAME ZzTermSmokeEcho
        COMMAND ZzTermSmoke -- bash -c "echo zz-smoke-ok")
    set_tests_properties(ZzTermSmokeEcho PROPERTIES
        PASS_REGULAR_EXPRESSION "zz-smoke-ok"
        TIMEOUT 30)
endif()
