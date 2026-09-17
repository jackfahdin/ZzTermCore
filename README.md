# ZzTermCore

独立、跨平台、自研的终端模拟器 Core 组件（C++20）。不依赖任何特定上层
应用进行开发、测试或功能验证。

## 特性

-   平台无关的纯 C++20 Terminal Engine：不依赖 Qt、PTY、网络或 GUI API
-   增量 UTF-8 解码与 VT/ANSI/xterm 解析器，支持任意 chunk 边界，可独立
    测试与 Fuzz
-   Cell 级 Unicode 表达：wide cell、grapheme cluster、256 色与
    RGB TrueColor
-   稳定只读的 `ZzRenderView` 渲染边界，Renderer 不接触 Core 私有容器
-   静态库与动态库均可构建、安装和链接（`BUILD_SHARED_LIBS`）
-   规划中的外层模块：Qt 6 QWidget 组件、关键词/正则高亮、
    Unix PTY / Windows ConPTY、独立 ZzTermDemo

``` text
bytes -> UTF-8/VT/xterm Parser -> Terminal State
      -> Cell/Line/Screen -> Scrollback -> RenderView

Frontend semantic events -> InputEncoder -> bytes
```

## 构建

要求：CMake >= 3.19（使用 CMakePresets 需 >= 3.21）、支持 C++20 的
编译器（Clang / GCC / MSVC）、推荐 Ninja。

``` bash
# 配置 + 构建 + 测试（更多 preset 见 CMakePresets.json）
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug

# 动态库构建
cmake -S . -B build/shared -G Ninja -DBUILD_SHARED_LIBS=ON
cmake --build build/shared

# 安装后下游可通过 find_package 使用
cmake --install build/shared --prefix <prefix>
# find_package(ZzTermCore) -> target_link_libraries(app PRIVATE ZzTerm::ZzTermCore)
```

生成 API 文档：

``` bash
doxygen Doxyfile   # 输出到 docs/api-html/
```

## 仓库布局

``` text
include/ZzTerm/                         公开头文件（Core API）
src/{parser,unicode,screen,history,input,terminal}/
tests/unit/                             单元测试（CTest）
docs/                                   设计文档与进度追踪
.github/workflows/                      CI（Clang / GCC / MSVC + Doxygen）
```

## 文档

-   [docs/Architecture.md](docs/Architecture.md)：技术设计与开发规范
-   [docs/VT-Xterm-Checklist.md](docs/VT-Xterm-Checklist.md)：功能实现与测试追踪
-   [docs/API.md](docs/API.md)：高层 API 说明
-   [docs/README.md](docs/README.md)：完整文档导航

## 开发状态

当前处于 M0（仓库、构建、Core API 骨架、测试与文档流水线）之后的
早期阶段。里程碑与 Definition of Done 见 Architecture.md 第 19、20 节，
逐项进度以 VT-xterm-Checklist.md 为准。

## License

[MIT](LICENSE)
