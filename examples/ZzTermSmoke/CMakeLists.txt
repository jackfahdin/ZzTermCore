# ZzTermSmoke：控制台冒烟 Demo（调试工具；正式 GUI Demo 为 ZzTermDemo）。
add_executable(ZzTermSmoke main.cpp)
target_link_libraries(ZzTermSmoke PRIVATE ZzTermCore ZzTermPty)
target_compile_features(ZzTermSmoke PRIVATE cxx_std_20)
