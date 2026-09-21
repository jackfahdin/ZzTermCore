# M7a Fuzz target 约定：
#   - 本目录仅 ZZTERM_FUZZ=ON 时收录（tests/CMakeLists.txt 尾部）；
#   - 每个 fuzz_*.cpp 使用 libFuzzer 提供的 main（-fsanitize=fuzzer），
#     独立于 tests/unit 的 GLOB 体系，命名空间不冲突；
#   - ASan 由根 CMakeLists 全局注入，本文件只追加 fuzzer。
foreach(fuzz_target IN ITEMS fuzz_parser fuzz_feed)
    add_executable(${fuzz_target} ${fuzz_target}.cpp)
    target_link_libraries(${fuzz_target} PRIVATE ZzTermCore)
    target_compile_options(${fuzz_target} PRIVATE -fsanitize=fuzzer)
    target_link_options(${fuzz_target} PRIVATE -fsanitize=fuzzer)
endforeach()

# 崩溃产物目录（位于 build/ 下，既被 gitignore 的 build/ 规则覆盖）。
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fuzz-artifacts")

# corpus 工作副本（build/ 下，gitignore 天然覆盖）：libFuzzer 只向第一个
# corpus 参数目录写入新变异单元，因此 smoke 的第一参数用可写工作副本、
# 第二参数用源码种子目录（只读输入），杜绝向 git 源目录原地写入 SHA1 文件。
# 注意：file(COPY) 是 configure 期动作——T2/T3 新增种子后需重新 configure
# 或手动 cmake --preset 才会同步到工作副本（CONFIGURE_DEPENDS 自动重
# configure 只跟踪 GLOB 源文件，不覆盖 corpus 文件场景）。
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/corpus/parser"
    DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/corpus-work")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/corpus/feed"
    DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/corpus-work")

# ctest smoke：seed corpus 起步各跑 30 秒，TIMEOUT 60。
# 撞出崩溃时的处置按规格 M7a 4.5 崩溃预案裁定。
add_test(NAME fuzz_parser_smoke
    COMMAND fuzz_parser "${CMAKE_CURRENT_BINARY_DIR}/corpus-work/parser"
            "${CMAKE_CURRENT_SOURCE_DIR}/corpus/parser"
            -max_total_time=30 -print_final_stats=1
            "-artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/fuzz-artifacts/")
set_tests_properties(fuzz_parser_smoke PROPERTIES TIMEOUT 60)
add_test(NAME fuzz_feed_smoke
    COMMAND fuzz_feed "${CMAKE_CURRENT_BINARY_DIR}/corpus-work/feed"
            "${CMAKE_CURRENT_SOURCE_DIR}/corpus/feed"
            -max_total_time=30 -print_final_stats=1
            "-artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/fuzz-artifacts/")
set_tests_properties(fuzz_feed_smoke PROPERTIES TIMEOUT 60)
