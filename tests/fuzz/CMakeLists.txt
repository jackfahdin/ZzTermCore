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

# ctest smoke：seed corpus 起步各跑 30 秒，TIMEOUT 60。
# 撞出崩溃时的处置按规格 M7a 4.5 崩溃预案裁定。
add_test(NAME fuzz_parser_smoke
    COMMAND fuzz_parser "${CMAKE_CURRENT_SOURCE_DIR}/corpus/parser"
            -max_total_time=30 -print_final_stats=1
            "-artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/fuzz-artifacts/")
set_tests_properties(fuzz_parser_smoke PROPERTIES TIMEOUT 60)
add_test(NAME fuzz_feed_smoke
    COMMAND fuzz_feed "${CMAKE_CURRENT_SOURCE_DIR}/corpus/feed"
            -max_total_time=30 -print_final_stats=1
            "-artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/fuzz-artifacts/")
set_tests_properties(fuzz_feed_smoke PROPERTIES TIMEOUT 60)
