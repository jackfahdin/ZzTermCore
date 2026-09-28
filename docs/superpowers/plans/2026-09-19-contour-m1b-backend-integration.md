# M1b：Contour 后端收口（视图统一 + 双后端接入）实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 把公开渲染视图升级为后端无关契约，ZzTerminal 瘦身为双后端 facade（运行期 ZzBackendKind 选择），Contour 经 ZzContourBackendAdapter 接入，demo 双后端兼容性验证。

**架构：** `include/ZzTerm/RenderView.h` 重写为抽象契约（ZzCellView 值语义单格 / ZzLineView 类型擦除值句柄 / ZzRenderView 抽象接口）。native 引擎从 ZzTerminal::Impl 整体迁入 `src/backend/native/ZzNativeBackend`（机械移动、行为零变化）并配 ZzNativeRenderView 零拷贝适配；Contour 侧新增 ZzContourBackendAdapter（事件推→ZzTermChanges 拉聚合 + output 通道）与 ZzContourRenderView（零拷贝包 CellProxy）。`ZzTerminal::Impl` 瘦身为接口指针委托。

**技术栈：** C++20（主库与公开头）/ C++23（contour 后端库）、CMake + CTest、python3 + pexpect + pyte（交互验证）。

**对应规格：** `docs/superpowers/specs/2026-09-19-contour-m1b-backend-integration-design.md`

**规格勘误/细化（计划阶段核实后的落地，优先级高于规格文字）：**

1. **不新建 flags 类型**：规格的「ZzCellFlag 上移为公开定义」改为直接复用现有公开 `ZzCellAttributes`（include/ZzTerm/Cell.h:176-287，16 位位集，含 underline 三态样式、blink 两态、protected）——Contour CellFlags 可无损映射（Bold→bold、Faint→faint、Italic→italic、Underline→underline(Single)、DoublyUnderlined→Double、CurlyUnderlined→Curly、Blinking→blink(Slow)、Inverse→inverse、Hidden→invisible、CrossedOut→strikethrough）。M1a 私有 `ZzCellFlag` 与私有 `struct ZzColor` 一并废除。
2. **公开 ZzColor 不扩展 Undefined**：公开 ZzColor（Cell.h:38-137）Kind 只有 Default/Indexed/Rgb，无 Undefined；vtbackend 的 Undefined 颜色无实际消费（M1a 测试零断言），统一映射为 `ZzColor::Default()`。
3. **ZzCellView.text 用 UTF-8 std::string**（非 u32string）：native cluster 本就 UTF-8 interned，demo 消费 UTF-8，双后端比对天然一致；Contour 侧 u32→UTF-8 在转换层编码。
4. **ZzCursorState 从 Screen.h 移至 Types.h**（Screen.h 继续经 Types.h 可见，源兼容），使重写后的 RenderView.h 不必 include native 的 Screen.h。
5. **统一视图契约砍掉旧 ZzRenderView 的 scrollback 访问**（scrollbackOffset/scrollbackLineCount/dirtyRows/cellAt(row,col) 便捷形式）——scrollback 按规格不进统一契约；demo 与测试均未使用这些入口。
6. **ZzTerminal 的 screen()/scrollback() 保留但限定 Native**（全仓库仅 3 处测试消费 screen() 设模式位，scrollback() 零消费）：facade 内经 dynamic_cast 到 ZzNativeBackend 实现，Contour 后端调用抛 std::logic_error。

**通用约束（每个任务都必须遵守）：**

- 分支：`contour`。构建：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug`；测试：`ctest --preset linux-gcc-debug`。
- 公开头（include/）保持 C++20 兼容、不暴露任何 Contour/vtbackend 类型；doxygen 注释内禁止尖括号；markdown 行内 code span 内禁止尖括号、内容禁止以点开头、后禁止紧跟顿号。
- Contour target 一律 PRIVATE 链接；ZzTermCore 链接 ZzTermContourBackend 为 PRIVATE。
- commit 规范：`type(scope): 中文描述`。
- native 引擎迁移（任务 2）为机械移动：12 个 native 单测（4 个构造 ZzTerminal 的文件 + 其余引擎单测）断言一律不改；任何需要改断言才能过的情况视为迁移缺陷。
- 当前基线：ON 15/15 全绿（12 个 C++ 单测可执行 + test_contour_smoke + test_contour_backend + 2 个 demo 脚本测试）；OFF 13/13。
- 关键现状事实（已钉死，直接照用）：
  - 公开 `ZzColor`：`ZzColor::Default()`/`ZzColor::Indexed(uint8_t)`/`ZzColor::Rgb(r,g,b)`，`kind()/index()/red()/green()/blue()`，`operator==` 有、无 `!=`；亮 n 号色即 `Indexed(8+n)`。
  - 公开 `ZzCellAttributes`：`setBold/setFaint/setItalic/setUnderline(ZzUnderlineStyle)/setBlink(ZzBlinkStyle)/setInverse/setInvisible/setStrikethrough`，`ZzUnderlineStyle{None,Single,Double,Curly,Dotted,Dashed}`、`ZzBlinkStyle{None,Slow,Rapid}`，`operator==` 有。
  - `ZzCellWidth{Empty,Narrow,WideLead,WideContinuation}`（Cell.h:290-295）。
  - native `ZzCell` 访问器命名是 `foreground()/background()/attributes()/width()/isCluster()/codePoint()/clusterIndex()`（不是 text/fg/bg/attrs）；cluster 文本经 `ZzLine::clusterText(index)` 取 UTF-8 string_view。
  - native `ZzScreen`：size/activeBuffer/lineAt/cursor/dirtyGeneration/rowDirty/dirtyRange/clearDirty/resize 齐备（Screen.h）；`ZzCursorState` 在 Screen.h:41-49（任务 1 移至 Types.h）。
  - `ZzTerminal::Impl` 引擎：成员与方法在 src/terminal/TerminalImpl.h:12-42；feed 聚合逻辑 Terminal.cpp:44-59（activeChanges_ 机制）；resize 实质逻辑在 ZzTerminal::resize Terminal.cpp:221-230（非正/相同返回 false，网格级不 reflow）——迁移时随引擎走，facade 变纯转发。
  - native 测试构造点 34 处同一形态 `ZzTerminal term(cols, rows, 100);`：test_terminal_e2e.cpp 6 处、test_terminal_core.cpp 13 处、test_terminal_csi.cpp 11 处、test_terminal_sgr.cpp 4 处；另有 examples/ZzTermSmoke/main.cpp:238 一处。
  - 库源文件：根 CMakeLists.txt:39-40 `file(GLOB ... src/*/*.cpp)` 自动收编，src/backend/native/*.cpp 无需改 CMake。
  - ZzPty 写回 API：`pty->writeAll(std::span<const std::byte>)`（pty/unix/ZzPty.h:74）。
  - Contour 侧：zzColor/zzFlags 转换在 src/backend/contour/ZzContourBackend.cpp:22-59；isBlank 守卫与列钳制在同文件 snapshot() 内（约 :220-260）；锁契约：onTitleChanged/onBell/onActiveBufferChanged 锁内（禁调 backend 任何方法）、onScreenDirty/onWriteToTransport 锁外。
  - Contour CellProxy 读接口：codepoints()/width()/flags()/foregroundColor()/backgroundColor()；宽字符首格 width()==2、续格 flags 含 WideCharContinuation。

---

### 任务 1：统一视图契约（RenderView.h 重写）+ native 适配 + 全部消费点适配

**文件：**
- 重写：`include/ZzTerm/RenderView.h`
- 修改：`include/ZzTerm/Types.h`（+ZzCursorState）
- 修改：`include/ZzTerm/Screen.h`（移除 ZzCursorState 定义，改由 Types.h 提供）
- 创建：`src/backend/native/ZzNativeRenderView.h`
- 创建：`src/backend/native/ZzNativeRenderView.cpp`
- 修改：`src/terminal/TerminalImpl.h`（renderView_ 换型为 ZzNativeRenderView）
- 修改：`src/terminal/Terminal.cpp`（renderView_ 构造与 renderView() 返回适配）
- 修改：`examples/ZzTermSmoke/main.cpp`（renderScreen 适配新契约）
- 修改：`tests/unit/test_terminal_e2e.cpp`、`test_terminal_core.cpp`、`test_terminal_csi.cpp`、`test_terminal_sgr.cpp`（视图访问适配）
- 修改：`tests/unit/test_backend_interface.cpp`（FakeBackend 适配抽象 ZzRenderView）

- [ ] **步骤 1：重写公开契约头**

`include/ZzTerm/Types.h` 追加（ZzCursorShape 已在 :70-74，ZzCursorState 移自 Screen.h:41-49，逐字移动）：

```cpp
/// \brief 光标状态（位置 + 形状 + 可见性）。
struct ZzCursorState {
    ZzPosition    position;
    ZzCursorShape shape    = ZzCursorShape::Block;
    bool          visible  = true;
    bool          blinking = true;
    friend constexpr bool operator==(const ZzCursorState&, const ZzCursorState&) noexcept = default;
};
```

`include/ZzTerm/Screen.h`：删除其 ZzCursorState 定义（:41-49），保留其余（ZzCursorShape 本就只在 Types.h）；确认 Screen.h 已 include Types.h（ZzCursorState 经此可见，源兼容）。

`include/ZzTerm/RenderView.h` 全文重写：

```cpp
#pragma once

/// \file
/// \brief 后端无关的只读渲染视图契约（M1b 重写）。
/// Renderer 只能通过本契约读取终端内容；视图为借用式，须与 feed 同线程使用，
/// feed/resize 后既有视图与 ZzLineView 句柄全部失效。非线程安全。

#include <ZzTerm/Cell.h>
#include <ZzTerm/Types.h>

#include <cstdint>
#include <string>

/// \brief 值语义单格视图（单格拷贝，非全量复制）。
struct ZzCellView {
    std::string      text;                              ///< UTF-8 文本；空（Empty/续格）时为空串
    ZzColor          foreground = ZzColor::Default();
    ZzColor          background = ZzColor::Default();
    ZzCellAttributes attributes;
    ZzCellWidth      width      = ZzCellWidth::Narrow;
};

/// \brief 行只读访问句柄：类型擦除值语义（内联存储后端行状态，无堆分配、无寿命问题）。
/// 拷贝廉价；后端行失效（feed/resize 后）则句柄失效。
class ZZTERM_API ZzLineView final {
public:
    using CellAtFn    = ZzCellView (*)(const void* storage, int col);
    using CellCountFn = int (*)(const void* storage) noexcept;
    using WrappedFn   = bool (*)(const void* storage) noexcept;

    /// \param state 后端行状态（按值存入内联缓冲，须 trivially copyable 且不超过 24 字节）
    template <typename T>
    ZzLineView(const T& state, CellAtFn cellAt, CellCountFn cellCount, WrappedFn wrapped) noexcept
        : cellAtFn_(cellAt), cellCountFn_(cellCount), wrappedFn_(wrapped)
    {
        static_assert(sizeof(T) <= kStorageSize, "ZzLineView 行状态超过内联存储");
        static_assert(std::is_trivially_copyable_v<T>, "ZzLineView 行状态须 trivially copyable");
        new (storage_) T(state);
    }

    [[nodiscard]] int cellCount() const noexcept { return cellCountFn_(storage_); }
    [[nodiscard]] ZzCellView cellAt(int col) const { return cellAtFn_(storage_, col); }
    [[nodiscard]] bool wrapped() const noexcept { return wrappedFn_(storage_); }

private:
    static constexpr std::size_t kStorageSize = 24;
    alignas(void*) unsigned char storage_[kStorageSize] = {};
    CellAtFn    cellAtFn_;
    CellCountFn cellCountFn_;
    WrappedFn   wrappedFn_;
};

/// \brief 后端无关渲染视图接口。dirty 行级形式保留；
/// 粗粒度后端（Contour）实现为「有脏时 rowDirty 恒 true、dirtyRange 恒全行」。
class ZZTERM_API ZzRenderView {
public:
    virtual ~ZzRenderView() = default;
    [[nodiscard]] virtual ZzSize size() const noexcept = 0;
    [[nodiscard]] virtual bool isAlternateScreen() const noexcept = 0;
    [[nodiscard]] virtual ZzLineView lineAt(int row) const = 0;
    [[nodiscard]] virtual ZzCursorState cursor() const = 0;
    [[nodiscard]] virtual std::uint64_t dirtyGeneration() const noexcept = 0;
    [[nodiscard]] virtual bool rowDirty(int row) const noexcept = 0;
    [[nodiscard]] virtual ZzCellRange dirtyRange(int row) const noexcept = 0;
};
```

（`RenderView.h` 需要追加 include `<new>`、`<type_traits>`、`<cstddef>`；旧内容——具体类 ZzRenderView 持有 ZzScreen/ZzScrollback 指针——全部移除。）

- [ ] **步骤 2：运行构建验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：FAIL——大量编译错误（TerminalImpl 的 renderView_ 类型、demo 与各测试对旧 lineAt/cellAt 的调用、test_backend_interface 的 `view_(nullptr, nullptr)` 构造）。

- [ ] **步骤 3：实现 ZzNativeRenderView 并适配全部消费点**

创建 `src/backend/native/ZzNativeRenderView.h`：

```cpp
#pragma once

#include <ZzTerm/RenderView.h>

class ZzScreen;

/// \brief native ZzScreen 的零拷贝渲染视图适配（M1b）。
/// 借用 screen（寿命须包住本对象）；feed/resize 后经 lineAt 重新取行句柄即可。
class ZzNativeRenderView final : public ZzRenderView {
public:
    explicit ZzNativeRenderView(const ZzScreen& screen) noexcept;
    [[nodiscard]] ZzSize size() const noexcept override;
    [[nodiscard]] bool isAlternateScreen() const noexcept override;
    [[nodiscard]] ZzLineView lineAt(int row) const override;
    [[nodiscard]] ZzCursorState cursor() const override;
    [[nodiscard]] std::uint64_t dirtyGeneration() const noexcept override;
    [[nodiscard]] bool rowDirty(int row) const noexcept override;
    [[nodiscard]] ZzCellRange dirtyRange(int row) const noexcept override;

private:
    static ZzCellView cellAtThunk(const void* storage, int col);
    static int cellCountThunk(const void* storage) noexcept;
    static bool wrappedThunk(const void* storage) noexcept;
    const ZzScreen* screen_;
};
```

创建 `src/backend/native/ZzNativeRenderView.cpp`：

```cpp
#include "ZzNativeRenderView.h"

#include <ZzTerm/Line.h>
#include <ZzTerm/Screen.h>

namespace {

void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace

ZzNativeRenderView::ZzNativeRenderView(const ZzScreen& screen) noexcept : screen_(&screen) {}

ZzSize ZzNativeRenderView::size() const noexcept { return screen_->size(); }

bool ZzNativeRenderView::isAlternateScreen() const noexcept
{
    return screen_->activeBuffer() == ZzScreenBuffer::Alternate;
}

ZzLineView ZzNativeRenderView::lineAt(int row) const
{
    return ZzLineView(&screen_->lineAt(row), &cellAtThunk, &cellCountThunk, &wrappedThunk);
}

ZzCursorState ZzNativeRenderView::cursor() const { return screen_->cursor(); }

std::uint64_t ZzNativeRenderView::dirtyGeneration() const noexcept { return screen_->dirtyGeneration(); }

bool ZzNativeRenderView::rowDirty(int row) const noexcept { return screen_->rowDirty(row); }

ZzCellRange ZzNativeRenderView::dirtyRange(int row) const noexcept { return screen_->dirtyRange(row); }

ZzCellView ZzNativeRenderView::cellAtThunk(const void* storage, int col)
{
    const auto& line = *static_cast<const ZzLine*>(storage);
    const ZzCell& cell = line.cellAt(col);
    ZzCellView view;
    if (cell.isCluster()) {
        view.text = line.clusterText(cell.clusterIndex());
    } else if (cell.codePoint() != 0) {
        appendUtf8(view.text, cell.codePoint());
    }
    view.foreground = cell.foreground();
    view.background = cell.background();
    view.attributes = cell.attributes();
    view.width      = cell.width();
    return view;
}

int ZzNativeRenderView::cellCountThunk(const void* storage) noexcept
{
    return static_cast<const ZzLine*>(storage)->cellCount();
}

bool ZzNativeRenderView::wrappedThunk(const void* storage) noexcept
{
    return static_cast<const ZzLine*>(storage)->wrapped();
}
```

适配 `src/terminal/TerminalImpl.h`：成员 `ZzRenderView renderView_;` 改为 `ZzNativeRenderView renderView_;`（include 换为 `backend/native/ZzNativeRenderView.h`）。

适配 `src/terminal/Terminal.cpp`：`renderView_(&screen_, scrollback_.get())` 改为 `renderView_(screen_)`；`ZzTerminal::renderView()` 返回类型不变（`const ZzRenderView&`，绑定到基类引用）。

适配 `examples/ZzTermSmoke/main.cpp` 的 renderScreen（:171-210）：`view.lineAt(row).cellAt(col)` 返回的不再是 `const ZzCell&`，改为：

```cpp
    const ZzRenderView& view = term.renderView();
    // ……光标隐藏/CUP 前缀不变……
    for (int row = 0; row < size.rows; ++row) {
        const ZzLineView line = view.lineAt(row);
        for (int col = 0; col < size.cols; ++col) {
            const ZzCellView cell = line.cellAt(col);
            if (cell.width == ZzCellWidth::WideContinuation)
                continue;
            const PenStyle style { cell.foreground, cell.background, cell.attributes };
            if (style != current)
                appendStyleSgr(out, style, current);
            out += cell.text.empty() ? " " : cell.text;
        }
        // ……行间 \r\n 与结尾光标恢复不变……
    }
```

适配 4 个 native 测试文件的视图访问（同一模式，逐处改写）：`view.lineAt(row).cellAt(col)` 返回值改用新访问器——`cell.foreground()`→`cell.foreground`、`cell.attributes()`→`cell.attributes`、`cell.width()`→`cell.width`、`cell.codePoint()`→ 经 `cell.text` 判断（单码位场景比对 `cell.text == "X"` 形态，cluster 场景比对 UTF-8 串）；断言语义一律不变，只改访问形态。scrollback 相关旧入口（若有 scrollbackLineCount 调用）按编译错误逐个就地适配（scrollback 不进统一契约：改走 `term.scrollback()`，该公开方法保留）。

适配 `tests/unit/test_backend_interface.cpp`：`view_(nullptr, nullptr)` 构造改为实现一个最小 FakeRenderView：

```cpp
class FakeRenderView final : public ZzRenderView {
public:
    ZzSize size() const noexcept override { return {}; }
    bool isAlternateScreen() const noexcept override { return false; }
    ZzLineView lineAt(int) const override
    {
        return ZzLineView(0, [](const void*, int) { return ZzCellView {}; },
                          [](const void*) noexcept { return 0; },
                          [](const void*) noexcept { return false; });
    }
    ZzCursorState cursor() const override { return {}; }
    std::uint64_t dirtyGeneration() const noexcept override { return 0; }
    bool rowDirty(int) const noexcept override { return false; }
    ZzCellRange dirtyRange(int) const noexcept override { return {}; }
};
// FakeBackend 成员：FakeRenderView view_; renderView() 返回 view_;
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：15/15 全绿（所有断言语义未动，仅访问形态适配）。
若 `ZzLineView` 模板构造报错（lambda 转函数指针）：确认 lambda 无捕获（无捕获 lambda 可隐式转函数指针）；若 ZzCursorState 报重定义/未定义：检查 Screen.h 是否已删定义且 include Types.h。

- [ ] **步骤 5：Commit**

```bash
git add include/ZzTerm/RenderView.h include/ZzTerm/Types.h include/ZzTerm/Screen.h \
    src/backend/native/ src/terminal/ examples/ZzTermSmoke/main.cpp tests/unit/
git commit -m "feat(render): 渲染视图升级为后端无关契约（ZzCellView/ZzLineView/ZzRenderView）+ native 零拷贝适配"
```

---

### 任务 2：ZzNativeBackend 抽取 + ZzTerminal 双后端 facade（ZzBackendKind）

**文件：**
- 创建：`src/backend/native/ZzNativeBackend.h`
- 创建：`src/backend/native/ZzNativeBackend.cpp`（Terminal.cpp 的 Sink/Impl 方法迁入）
- 创建：`src/backend/native/NativeCsiDispatch.cpp`（CsiDispatch.cpp 迁入，改名避免混淆）
- 创建：`src/backend/native/NativeSgr.cpp`（Sgr.cpp 迁入）
- 删除：`src/terminal/TerminalImpl.h`、`src/terminal/CsiDispatch.cpp`、`src/terminal/Sgr.cpp`
- 重写：`src/terminal/Terminal.cpp`（瘦身为 facade）
- 修改：`src/backend/ZzTerminalBackend.h`（+setOutputHandler、注释更新）
- 修改：`include/ZzTerm/Terminal.h`（+ZzBackendKind、构造签名、+setOutputHandler、screen()/scrollback() 限定说明）
- 修改：`tests/unit/test_terminal_e2e.cpp`、`test_terminal_core.cpp`、`test_terminal_csi.cpp`、`test_terminal_sgr.cpp`（34 处构造点显式 Native）
- 修改：`tests/unit/test_backend_interface.cpp`（+setOutputHandler 覆盖）
- 修改：`examples/ZzTermSmoke/main.cpp`（构造点，暂硬编 Native，任务 6 加参数）

- [ ] **步骤 1：编写失败的测试**

修改 `tests/unit/test_backend_interface.cpp`：FakeBackend 增加第 9 个 override——

```cpp
    void setOutputHandler(std::function<void(std::string_view)> handler) override
    {
        outputHandlerSet = static_cast<bool>(handler);
    }
    bool outputHandlerSet = false;
```

main() 追加断言：

```cpp
    backend.setOutputHandler([](std::string_view) {});
    assert(backend.outputHandlerSet);
```

并在 `include/ZzTerm/Terminal.h` 期望的新构造签名处加一处编译期用法（在本测试或任一单测中）：

```cpp
    ZzTerminal term(10, 4, ZzBackendKind::Native, 100);
    assert(term.size() == (ZzSize { 10, 4 }));
```

- [ ] **步骤 2：运行构建验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：FAIL——`ZzBackendKind` 未定义、接口无 setOutputHandler。

- [ ] **步骤 3：接口与公开头修订**

`src/backend/ZzTerminalBackend.h`：接口追加第 9 个纯虚（include 追加 `<functional>`、`<string_view>`）：

```cpp
    /// \brief 设置终端回传字节（DA 响应、光标上报等）的输出通道；native 暂为空实现。
    virtual void setOutputHandler(std::function<void(std::string_view)> handler) = 0;
```

`include/ZzTerm/Terminal.h` 修订：

```cpp
/// \brief 终端引擎后端选择（运行期）。
enum class ZzBackendKind {
    Native,  ///< 自研引擎（一等后端，兼容性对照基准）
    Contour  ///< Contour vtbackend（默认方向；需 ZZTERM_WITH_CONTOUR=ON 构建）
};

class ZZTERM_API ZzTerminal {
public:
    /// \param backend 后端选择（显式，无默认值）。OFF 构建传 Contour 抛 std::logic_error。
    ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines = 10000);
    // ……其余方法不变，追加：
    /// \brief 设置终端回传字节的输出通道（DA 响应等）；Contour 后端有效，native 暂不回传。
    void setOutputHandler(std::function<void(std::string_view)> handler);
    // screen()/scrollback() 注释补充：仅 Native 后端可用，Contour 后端调用抛 std::logic_error。
};
```

（include 追加 `<functional>`、`<string_view>`。）

- [ ] **步骤 4：native 引擎机械迁移**

1. `src/terminal/TerminalImpl.h` 内容整体改写到 `src/backend/native/ZzNativeBackend.h`：`class ZzTerminal::Impl` 改名 `class ZzNativeBackend final : public ZzTerminalBackend`，追加实现接口的 9 个方法声明（feed/resize/renderView/size/cursor/isAlternateScreen/title/clearDirty/setOutputHandler）+ 公开 `ZzScreen& screen() noexcept`、`ZzScrollback& scrollback() noexcept`（facade 的 screen()/scrollback() 委托用）；成员与 struct Sink 前置声明逐字搬移；构造签名 `ZzNativeBackend(int cols, int rows, std::size_t scrollbackMaxLines)`。
2. `src/terminal/Terminal.cpp` 中 Impl 相关实现（:11-208 的 Sink 定义、构造、feed、noteScreenDirty、eraseFill、putChar、executeControl、dispatchEsc、dispatchOsc）整体搬入 `src/backend/native/ZzNativeBackend.cpp`，`ZzTerminal::Impl::` 限定改 `ZzNativeBackend::`；resize 实质逻辑（Terminal.cpp:221-230：非正尺寸/相同尺寸返回 false、screen_.resize、注释保留）搬入 `ZzNativeBackend::resize`。追加接口方法实现——size/cursor/isAlternateScreen/title/clearDirty/renderView（返回 renderView_）逐行对应原 ZzTerminal 转发体的实质（:232-240），setOutputHandler 空实现 `{}`（存下 handler 亦可，注释「M3 输入编码后启用」）。
3. `src/terminal/CsiDispatch.cpp`、`src/terminal/Sgr.cpp` 内容分别搬入 `src/backend/native/NativeCsiDispatch.cpp`、`NativeSgr.cpp`（限定名替换）；删除 src/terminal/ 下三个旧文件（GLOB 自动收编新文件，无需改 CMake）。
4. 重写 `src/terminal/Terminal.cpp` 为 facade：

```cpp
#include <ZzTerm/Terminal.h>

#include "../backend/ZzTerminalBackend.h"
#include "../backend/native/ZzNativeBackend.h"
#ifdef ZZTERM_WITH_CONTOUR
#include "../backend/contour/ZzContourBackendAdapter.h" // 任务 4 创建；本任务先 #ifdef 占位
#endif

#include <stdexcept>
#include <utility>

class ZzTerminal::Impl {
public:
    Impl(int cols, int rows, ZzBackendKind kind, std::size_t scrollbackMaxLines)
    {
        switch (kind) {
        case ZzBackendKind::Native:
            backend = std::make_unique<ZzNativeBackend>(cols, rows, scrollbackMaxLines);
            break;
        case ZzBackendKind::Contour:
#ifdef ZZTERM_WITH_CONTOUR
            backend = zzCreateContourBackendAdapter(cols, rows, scrollbackMaxLines);
#else
            throw std::logic_error("ZzBackendKind::Contour 需要 ZZTERM_WITH_CONTOUR=ON 构建");
#endif
            break;
        }
    }
    std::unique_ptr<ZzTerminalBackend> backend;
};

ZzTerminal::ZzTerminal(int cols, int rows, ZzBackendKind backend, std::size_t scrollbackMaxLines)
    : impl_(std::make_unique<Impl>(cols, rows, backend, scrollbackMaxLines)) {}
ZzTerminal::~ZzTerminal() = default;

ZzTermChanges ZzTerminal::feed(std::span<const std::byte> data) { return impl_->backend->feed(data); }
bool ZzTerminal::resize(int cols, int rows) { return impl_->backend->resize(cols, rows); }
const ZzRenderView& ZzTerminal::renderView() const noexcept { return impl_->backend->renderView(); }
ZzSize ZzTerminal::size() const noexcept { return impl_->backend->size(); }
ZzCursorState ZzTerminal::cursor() const noexcept { return impl_->backend->cursor(); }
bool ZzTerminal::isAlternateScreen() const noexcept { return impl_->backend->isAlternateScreen(); }
const std::string& ZzTerminal::title() const noexcept { return impl_->backend->title(); }
void ZzTerminal::clearDirty() noexcept { impl_->backend->clearDirty(); }
void ZzTerminal::setOutputHandler(std::function<void(std::string_view)> handler)
{
    impl_->backend->setOutputHandler(std::move(handler));
}

ZzScreen& ZzTerminal::screen() noexcept
{
    auto* native = dynamic_cast<ZzNativeBackend*>(impl_->backend.get());
    if (!native)
        throw std::logic_error("ZzTerminal::screen() 仅 Native 后端可用");
    return native->screen();
}

ZzScrollback& ZzTerminal::scrollback() noexcept
{
    auto* native = dynamic_cast<ZzNativeBackend*>(impl_->backend.get());
    if (!native)
        throw std::logic_error("ZzTerminal::scrollback() 仅 Native 后端可用");
    return native->scrollback();
}
```

（注：`zzCreateContourBackendAdapter` 为任务 4 在 contour 库提供的工厂函数；本任务在 ZZTERM_WITH_CONTOUR 宏分支内暂写 `throw std::logic_error("Contour 后端接入中（任务 4）");` 或直接保留 OFF 同款抛错——任务 4 替换。noexcept 方法内抛异常会 terminate：`screen()`/`scrollback()` 原声明带 noexcept——从公开头移除这两个方法的 noexcept（行为变更：仅 Contour 下才抛，Native 路径不抛）。）

5. 34 处测试构造点机械替换（4 个文件）：`ZzTerminal term(10, 4, 100);` 形态统一改为 `ZzTerminal term(10, 4, ZzBackendKind::Native, 100);`（行列值逐处保持原样）；examples/ZzTermSmoke/main.cpp:238 改为 `ZzTerminal term(termSize.cols, termSize.rows, ZzBackendKind::Native, 1000);`（任务 6 再参数化）。

- [ ] **步骤 5：运行测试验证通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：15/15 全绿（含步骤 1 修订的 test_backend_interface；12 个 native 单测断言零改动）。重新 configure（步骤含文件增删，GLOB CONFIGURE_DEPENDS 应自动感知，若未感知则删 build 目录重配）。
另验证 OFF：`cmake -S . -B build/m1b-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m1b-off-check && ctest --test-dir build/m1b-off-check`——预期 13/13（Contour 抛错路径本任务不测，任务 4 补）。

- [ ] **步骤 6：Commit**

```bash
git add -A src/ include/ZzTerm/Terminal.h tests/unit/ examples/ZzTermSmoke/main.cpp
git commit -m "refactor(backend): native 引擎迁入 ZzNativeBackend，ZzTerminal 瘦身双后端 facade（ZzBackendKind 显式选择）"
```

---

### 任务 3：M1a contour 后端颜色/属性统一到公开类型

**文件：**
- 修改：`src/backend/contour/ZzContourBackend.h`（废私有 ZzColor/ZzCellFlag，ZzContourCell 换用公开类型）
- 修改：`src/backend/contour/ZzContourBackend.cpp`（zzColor 改产出公开 ZzColor、zzFlags 改产出 ZzCellAttributes、width 映射 ZzCellWidth）
- 测试：`tests/unit/test_contour_backend.cpp`（断言形态适配）

- [ ] **步骤 1：改写失败的测试**

`tests/unit/test_contour_backend.cpp` 中颜色断言形态统一替换（7 处典型）：

```cpp
// 原：ZZ_CHECK(cell.foreground == (ZzColor { ZzColor::Tag::Indexed, 1 }));
ZZ_CHECK(cell.foreground == ZzColor::Indexed(1));
// 原：ZZ_CHECK(snap.at(0, 1).foreground == (ZzColor { ZzColor::Tag::Default, 0 }));
ZZ_CHECK(snap.at(0, 1).foreground == ZzColor::Default());
// 亮红：ZzColor::Indexed(9)；RGB：ZzColor::Rgb(10, 20, 30)、ZzColor::Rgb(200, 100, 50)
```

flags 断言形态替换：

```cpp
// 原：ZZ_CHECK(snap.at(0, 0).flags & ZzCellFlag::Bold);
ZZ_CHECK(snap.at(0, 0).attributes.bold());
// Italic→.italic()；Underline→.underline() == ZzUnderlineStyle::Single
// 续格：原 flags & ZzCellFlag::WideCharContinuation → cell.width == ZzCellWidth::WideContinuation
// 宽字符首格：原 width == 2 → cell.width == ZzCellWidth::WideLead
```

- [ ] **步骤 2：运行构建验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：FAIL——ZzColor::Tag/ZzCellFlag 不再存在（步骤 3 删除后）。

- [ ] **步骤 3：统一实现**

`src/backend/contour/ZzContourBackend.h`：删除私有 `struct ZzColor`（:15-35）与 `struct ZzCellFlag`（:38-53）；include 追加 `<ZzTerm/Cell.h>`（ZzTermCore 公开头，C++20 兼容）；`ZzContourCell` 改为：

```cpp
/// \brief 快照中的单个单元格（拷贝语义，不引用 Terminal 内部）。
struct ZzContourCell
{
    std::u32string  codepoints;   ///< 簇内全部 codepoint；续格与空格为空
    ZzColor         foreground = ZzColor::Default();
    ZzColor         background = ZzColor::Default();
    ZzCellAttributes attributes;
    ZzCellWidth     width      = ZzCellWidth::Narrow; ///< WideLead=宽字符首格；WideContinuation=续格
};
```

`src/backend/contour/ZzContourBackend.cpp`：zzColor 改产出公开 ZzColor——

```cpp
ZzColor zzColor(vtbackend::Color color)
{
    switch (color.type())
    {
        case vtbackend::ColorType::RGB: {
            auto const rgb = color.rgb();
            return ZzColor::Rgb(rgb.red, rgb.green, rgb.blue);
        }
        case vtbackend::ColorType::Indexed:
            return ZzColor::Indexed(color.index());
        case vtbackend::ColorType::Bright:
            return ZzColor::Indexed(static_cast<std::uint8_t>(8 + color.index()));
        case vtbackend::ColorType::Default:
        case vtbackend::ColorType::Undefined: // 无实际消费，统一按默认色
        default:
            return ZzColor::Default();
    }
}
```

zzFlags 改名 zzAttributes 并改产出 ZzCellAttributes：

```cpp
ZzCellAttributes zzAttributes(vtbackend::CellFlags flags)
{
    ZzCellAttributes out;
    if (flags.contains(vtbackend::CellFlag::Bold)) out.setBold(true);
    if (flags.contains(vtbackend::CellFlag::Faint)) out.setFaint(true);
    if (flags.contains(vtbackend::CellFlag::Italic)) out.setItalic(true);
    if (flags.contains(vtbackend::CellFlag::Underline)) out.setUnderline(ZzUnderlineStyle::Single);
    if (flags.contains(vtbackend::CellFlag::DoublyUnderlined)) out.setUnderline(ZzUnderlineStyle::Double);
    if (flags.contains(vtbackend::CellFlag::CurlyUnderlined)) out.setUnderline(ZzUnderlineStyle::Curly);
    if (flags.contains(vtbackend::CellFlag::Blinking)) out.setBlink(ZzBlinkStyle::Slow);
    if (flags.contains(vtbackend::CellFlag::Inverse)) out.setInverse(true);
    if (flags.contains(vtbackend::CellFlag::Hidden)) out.setInvisible(true);
    if (flags.contains(vtbackend::CellFlag::CrossedOut)) out.setStrikethrough(true);
    return out;
}

ZzCellWidth zzWidth(const vtbackend::BasicCellProxy<true>& cell) // const 版 CellProxy
{
    if (cell.flags().contains(vtbackend::CellFlag::WideCharContinuation))
        return ZzCellWidth::WideContinuation;
    return cell.width() == 2 ? ZzCellWidth::WideLead : ZzCellWidth::Narrow;
}
```

snapshot() 填充处（约 :250-253）：`out.foreground = zzColor(...)`、`out.background = zzColor(...)`、`out.attributes = zzAttributes(cell.flags())`、`out.width = zzWidth(cell)`；blank 行分支的 fillAttrs 背景同样走 zzColor（fillAttrs 的 flags 也经 zzAttributes 透出——沿用任务 3 审查认可的既有行为）。

（若 `BasicCellProxy<true>` 模板名不符，以 third_party/contour/src/vtbackend/grid/CellProxy.hpp:34 的实际模板声明为准；也可让 zzWidth 直接收 width()+flags() 两个参数规避模板名。）

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：15/15 全绿。

- [ ] **步骤 5：Commit**

```bash
git add src/backend/contour/ tests/unit/test_contour_backend.cpp
git commit -m "refactor(contour): M1a 私有 ZzColor/ZzCellFlag 废除，统一公开 ZzColor/ZzCellAttributes/ZzCellWidth"
```

---

### 任务 4：ZzContourRenderView + ZzContourBackendAdapter + 工厂接线

**文件：**
- 创建：`src/backend/contour/ZzContourConvert.h`（zzColor/zzAttributes/zzWidth/UTF-8 编码共享给视图与 adapter——从 ZzContourBackend.cpp 提取为内联函数）
- 修改：`src/backend/contour/ZzContourBackend.cpp`（转换函数改为 include ZzContourConvert.h，去重）
- 修改：`src/backend/contour/ZzContourBackend.h` + `.cpp`（+cursorState 轻量方法）
- 创建：`src/backend/contour/ZzContourRenderView.h/.cpp`
- 创建：`src/backend/contour/ZzContourBackendAdapter.h/.cpp`
- 修改：`src/terminal/Terminal.cpp`（宏分支接入真实工厂）
- 修改：`CMakeLists.txt`（根：ZzTermCore PRIVATE 链 ZzTermContourBackend + compile definition）
- 测试：`tests/unit/test_contour_adapter.cpp`（新增，REMOVE_ITEM + if(TARGET) 注册）

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_contour_adapter.cpp`（自带 main、ZZ_CHECK 宏风格参照 test_contour_backend.cpp）：

```cpp
// ZzContourBackendAdapter 经 ZzTerminal facade 的端到端测试（Contour 后端）。
#include <ZzTerm/Terminal.h>

#include <cassert>
#include <cstdio>
#include <string>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

// feed 聚合 ZzTermChanges + 统一视图读回 + title/output 通道。
void testFeedAndView()
{
    ZzTerminal term(80, 24, ZzBackendKind::Contour, 1000);
    std::string titleSeen;
    bool outputSeen = false;
    // title 经 ZzTermChanges 上报；output 经 setOutputHandler
    term.setOutputHandler([&outputSeen](std::string_view) { outputSeen = true; });

    auto changes = term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("\x1b]0;T\x07" "Hi"), 8));
    ZZ_CHECK(changes.titleChanged);
    ZZ_CHECK(term.title() == "T");

    const ZzRenderView& view = term.renderView();
    ZZ_CHECK(view.size() == (ZzSize { 80, 24 }));
    ZZ_CHECK(!view.isAlternateScreen());
    const ZzLineView line = view.lineAt(0);
    ZZ_CHECK(line.cellAt(0).text == "H");
    ZZ_CHECK(line.cellAt(1).text == "i");
    ZZ_CHECK(line.cellAt(0).foreground == ZzColor::Default());

    // DA 回写经 output 通道（adapter 在 feed 末尾 flushReplies）。
    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x1b[c"), 3));
    ZZ_CHECK(outputSeen);
}

// 事件标志：bell、alt buffer、scrollback 差值；dirty 语义（有脏恒全行）。
void testChangesAndDirty()
{
    ZzTerminal term(80, 24, ZzBackendKind::Contour, 1000);
    auto c1 = term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x07"), 1));
    ZZ_CHECK(c1.bell);

    auto c2 = term.feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x1b[?1049h"), 8));
    ZZ_CHECK(c2.activeBufferChanged);
    ZZ_CHECK(term.isAlternateScreen());
    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x1b[?1049l"), 8));
    ZZ_CHECK(!term.isAlternateScreen());

    for (int i = 0; i < 30; ++i) {
        const std::string line = "L" + std::to_string(i) + "\r\n";
        auto c = term.feed(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(line.data()), line.size()));
        if (i == 29) {
            ZZ_CHECK(c.scrollbackChanged);
            ZZ_CHECK(c.scrolledOutLines >= 6);
        }
    }

    const ZzRenderView& view = term.renderView();
    ZZ_CHECK(view.dirtyGeneration() > 0);
    ZZ_CHECK(view.rowDirty(0));                    // Contour：有脏恒全行脏
    ZZ_CHECK(view.dirtyRange(0).endCol == 80);     // dirtyRange 恒全行
    term.clearDirty();
    ZZ_CHECK(!view.rowDirty(0));                   // clearDirty 后无脏
    ZZ_CHECK(view.dirtyGeneration() > 0);          // 代际不清零（与 native 语义一致）
}

// resize 返回值语义（非正/相同 false）+ 光标可见性。
void testResizeAndCursor()
{
    ZzTerminal term(80, 24, ZzBackendKind::Contour, 1000);
    ZZ_CHECK(!term.resize(0, 24));
    ZZ_CHECK(!term.resize(80, 24));
    ZZ_CHECK(term.resize(100, 30));
    ZZ_CHECK(term.size() == (ZzSize { 100, 30 }));

    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("AB"), 2));
    ZZ_CHECK(term.cursor().visible);
    ZZ_CHECK(term.cursor().position.row == 0);
    ZZ_CHECK(term.cursor().position.col == 2);
    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x1b[?25l"), 6));
    ZZ_CHECK(!term.cursor().visible);
}

} // namespace

int main()
{
    testFeedAndView();
    testChangesAndDirty();
    testResizeAndCursor();
    if (g_failures != 0)
        std::fprintf(stderr, "test_contour_adapter: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

`tests/CMakeLists.txt`：REMOVE_ITEM 列表追加 `unit/test_contour_adapter.cpp`，并在 test_contour_backend 条件块后追加同款注册块（`if(TARGET ZzTermContourBackend)`，链 `ZzTermContourBackend ZzTermCore`，cxx_std_23 不需要——本测试只碰公开头，C++20 即可，链接 ZzTermContourBackend 仅为保障 target 依赖关系）。

- [ ] **步骤 2：运行构建验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：FAIL——`ZzContourBackendAdapter.h`/`zzCreateContourBackendAdapter` 不存在，且 Contour kind 构造抛 logic_error。

- [ ] **步骤 3：实现转换共享头 + cursorState + 视图 + adapter + 工厂**

1. `src/backend/contour/ZzContourConvert.h`：把 ZzContourBackend.cpp 的 zzColor/zzAttributes/zzWidth（任务 3 版本）与 appendUtf8（从 ZzNativeRenderView.cpp 同款逻辑复制为 `inline void zzAppendUtf8(std::string&, char32_t)`——两端各一份属有意为之：native 在主库 C++20、contour 在独立库 C++23，跨库共享无合适落点）提取为 `inline` 函数；ZzContourBackend.cpp 改为 include 本头并删除本地副本。
2. `ZzContourBackend` 追加轻量光标方法（头 + 实现）：

```cpp
    /// \brief 光标位置与可见性（经 RenderBuffer 路径；不可见时返回 nullopt）。
    /// 内部会 refreshRenderBuffer，非 const。
    std::optional<std::pair<int, int>> cursorPosition();
```

实现即 snapshot() 尾部光标段的重构复用（refreshRenderBuffer + renderBuffer RAII + position 解包），snapshot() 改为内部调用它。
3. `src/backend/contour/ZzContourRenderView.h/.cpp`：

```cpp
class ZzContourRenderView final : public ZzRenderView {
public:
    // shared state 由 adapter 持有（寿命包住本视图）。
    struct State {
        std::uint64_t dirtyGeneration = 0;
        bool          dirtySinceClear = false;
    };
    ZzContourRenderView(ZzContourBackend& backend, const State& state) noexcept;
    ZzSize size() const noexcept override;                 // backend.size() → ZzSize
    bool isAlternateScreen() const noexcept override;      // backend.isAlternateScreen()
    ZzLineView lineAt(int row) const override;             // RowRef 值状态 + thunk
    ZzCursorState cursor() const override;                 // backend.cursorPosition()（const_cast 收口，注释说明）
    std::uint64_t dirtyGeneration() const noexcept override { return state_->dirtyGeneration; }
    bool rowDirty(int) const noexcept override { return state_->dirtySinceClear; }
    ZzCellRange dirtyRange(int row) const noexcept override
    {
        return state_->dirtySinceClear ? ZzCellRange { 0, size().cols } : ZzCellRange {};
    }
private:
    // RowRef：{ const void* screen; int row; ZzColor blankBg; bool blank; }（≤24B，trivially copyable）
    // cellAtThunk 复用 M1a 逻辑：blank 行产 fillAttrs 背景格；非空行走 CellProxy + zzColor/zzAttributes/zzWidth + zzAppendUtf8；
    // 列钳制（col < 行实际列数，Grid::resize 未物化防御）照 snapshot() 同款。
    ZzContourBackend* backend_;
    const State*      state_;
};
```

（RowRef 持有 `const vtbackend::Screen*`——经 `backend.currentScreen()` 暴露？M1a ZzContourBackend 未暴露 Screen。给 ZzContourBackend 追加一个仅供本库内部使用的访问器（注释「内部：仅供 ZzContourRenderView」）：`const void* screenForView() const` 返回 `&terminal->currentScreen()`——用 void* 保持公开头无 Contour 类型；thunk 内 static_cast 回 `const vtbackend::Screen*`。）
4. `src/backend/contour/ZzContourBackendAdapter.h`（C++20 干净、无 Contour include）：

```cpp
#pragma once

#include "../../../src/backend/ZzTerminalBackend.h" // 实际用相对 include 由 CMake include 路径决定

#include <memory>

/// \brief 工厂：创建 Contour 后端的 ZzTerminalBackend 实现（M1b）。
/// scrollbackLines 透传 ZzContourBackend。
std::unique_ptr<ZzTerminalBackend> zzCreateContourBackendAdapter(int cols, int rows,
                                                                 std::size_t scrollbackLines);
```

`ZzContourBackendAdapter.cpp`：

```cpp
class ZzContourBackendAdapter final : public ZzTerminalBackend {
public:
    ZzContourBackendAdapter(int cols, int rows, std::size_t scrollbackLines)
        : events_(std::make_unique<EventsImpl>(*this))
        , backend_(std::make_unique<ZzContourBackend>(cols, rows, *events_,
                                                      static_cast<int>(scrollbackLines)))
        , renderView_(*backend_, state_)
    {}

    ZzTermChanges feed(std::span<const std::byte> data) override
    {
        const int historyBefore = backend_->historyLineCount();
        screenDirty_ = activeBufferChanged_ = titleChanged_ = bell_ = false;
        backend_->feed(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
        backend_->flushReplies(); // 回传字节经 onWriteToTransport（锁外）进 output handler
        const int historyAfter = backend_->historyLineCount();

        ZzTermChanges changes;
        changes.screenDirty = screenDirty_;
        changes.activeBufferChanged = activeBufferChanged_;
        changes.titleChanged = titleChanged_;
        changes.bell = bell_;
        if (historyAfter > historyBefore) {
            changes.scrollbackChanged = true;
            changes.scrolledOutLines = static_cast<std::size_t>(historyAfter - historyBefore);
        }
        if (changes.screenDirty) {
            state_.dirtySinceClear = true;
            ++state_.dirtyGeneration;
        }
        return changes;
    }

    bool resize(int cols, int rows) override
    {
        if (cols <= 0 || rows <= 0) return false;
        if (backend_->size() == std::make_pair(cols, rows)) return false;
        backend_->resize(cols, rows);
        state_.dirtySinceClear = true;
        ++state_.dirtyGeneration;
        return true;
    }

    const ZzRenderView& renderView() const noexcept override { return renderView_; }
    ZzSize size() const noexcept override
    {
        const auto [cols, rows] = backend_->size();
        return ZzSize { cols, rows };
    }
    ZzCursorState cursor() const noexcept override { return renderView_.cursor(); }
    bool isAlternateScreen() const noexcept override { return backend_->isAlternateScreen(); }
    const std::string& title() const noexcept override { return title_; }
    void clearDirty() noexcept override { state_.dirtySinceClear = false; }
    void setOutputHandler(std::function<void(std::string_view)> handler) override
    {
        outputHandler_ = std::move(handler);
    }

private:
    // ZzContourEvents 实现：锁内回调（title/bell/altBuffer）只写 adapter 自有状态
    //（不触碰 backend，遵守锁契约）；锁外回调（dirty/output）直接转发。
    class EventsImpl final : public ZzContourEvents { /* onTitleChanged: title_=move(t), titleChanged_=true；
        onBell: bell_=true；onActiveBufferChanged: activeBufferChanged_=true；
        onScreenDirty: screenDirty_=true；onWriteToTransport: 若 outputHandler_ 则调用 */
    };

    std::unique_ptr<EventsImpl>       events_;
    std::unique_ptr<ZzContourBackend> backend_;
    ZzContourRenderView::State        state_;
    ZzContourRenderView               renderView_;
    std::string                       title_;
    bool screenDirty_ = false, activeBufferChanged_ = false, titleChanged_ = false, bell_ = false;
    std::function<void(std::string_view)> outputHandler_;
};

std::unique_ptr<ZzTerminalBackend> zzCreateContourBackendAdapter(int cols, int rows,
                                                                 std::size_t scrollbackLines)
{
    return std::make_unique<ZzContourBackendAdapter>(cols, rows, scrollbackLines);
}
```

5. 根 `CMakeLists.txt`（在 include(cmake/ContourBackend.cmake) 之后）追加：

```cmake
# M1b：ZzTermCore 双后端接入——PRIVATE 链接 Contour 后端库并开通编译宏。
if(ZZTERM_WITH_CONTOUR)
    target_link_libraries(ZzTermCore PRIVATE ZzTermContourBackend)
    target_compile_definitions(ZzTermCore PRIVATE ZZTERM_WITH_CONTOUR=1)
endif()
```

`src/terminal/Terminal.cpp` 的 ZZTERM_WITH_CONTOUR 宏分支替换为真实工厂调用 `zzCreateContourBackendAdapter(cols, rows, scrollbackMaxLines)`（include `../backend/contour/ZzContourBackendAdapter.h`）。

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：16/16 全绿（15 + test_contour_adapter）。若链接报 undefined `zzCreateContourBackendAdapter`：检查 ZzTermCore 的 PRIVATE 链接是否生效、contour 库 target 名拼写。

- [ ] **步骤 5：OFF 路径验证**

运行：`cmake -S . -B build/m1b-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m1b-off-check && ctest --test-dir build/m1b-off-check`
预期：13/13 全绿（test_contour_adapter 不注册）。并用一个临时小程序或现有 test_backend_interface 扩展断言 OFF 下 `ZzTerminal(10, 4, ZzBackendKind::Contour, 100)` 抛 std::logic_error——以在 test_backend_interface.cpp 中追加（该测试 ON/OFF 都构建）：

```cpp
#ifndef ZZTERM_WITH_CONTOUR
    bool thrown = false;
    try { ZzTerminal t(10, 4, ZzBackendKind::Contour, 100); (void)t; }
    catch (const std::logic_error&) { thrown = true; }
    assert(thrown);
#endif
```

（test_backend_interface 需编译定义透传：tests/CMakeLists.txt 给该 target 追加 `target_compile_definitions(test_backend_interface PRIVATE $<$<BOOL:${ZZTERM_WITH_CONTOUR}>:ZZTERM_WITH_CONTOUR=1>)`；并链接 ZzTermCore 已有。）

- [ ] **步骤 6：Commit**

```bash
git add src/backend/contour/ src/terminal/ CMakeLists.txt tests/
git commit -m "feat(contour): ZzContourBackendAdapter 接入 ZzTerminal 双后端（视图/事件聚合/output 通道/工厂）"
```

---

### 任务 5：双后端对照测试 test_backend_compat

**文件：**
- 测试：`tests/unit/test_backend_compat.cpp`（新增，REMOVE_ITEM + if(TARGET ZzTermContourBackend) 注册，链 ZzTermCore）

- [ ] **步骤 1：编写测试**

创建 `tests/unit/test_backend_compat.cpp`（ZZ_CHECK 宏风格同其他测试）。核心辅助：

```cpp
// 同一字节序列喂两个后端，逐格比对统一视图。
struct Dual {
    ZzTerminal native { 80, 24, ZzBackendKind::Native, 1000 };
    ZzTerminal contour { 80, 24, ZzBackendKind::Contour, 1000 };
    void feedBoth(std::string_view bytes)
    {
        native.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
        contour.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
    }
};

// 比对 (row, col) 一格：文本/前景/背景/属性/宽度。
void checkCellEqual(const ZzTerminal& a, const ZzTerminal& b, int row, int col, const char* what)
{
    const ZzCellView ca = a.renderView().lineAt(row).cellAt(col);
    const ZzCellView cb = b.renderView().lineAt(row).cellAt(col);
    if (ca.text != cb.text || ca.foreground != cb.foreground || ca.background != cb.background
        || ca.attributes != cb.attributes || ca.width != cb.width) {
        ++g_failures;
        std::fprintf(stderr, "FAIL cell(%d,%d) %s: native{text=%s,w=%d} vs contour{text=%s,w=%d}\n",
                     row, col, what, ca.text.c_str(), (int)ca.width, cb.text.c_str(), (int)cb.width);
    }
}

// 比对整行前 n 格（宽度不同的宽字符场景逐格比对仍成立：两后端对续格都给 WideContinuation）。
void checkRowEqual(const ZzTerminal& a, const ZzTerminal& b, int row, int n, const char* what)
{
    for (int col = 0; col < n; ++col)
        checkCellEqual(a, b, row, col, what);
}
```

用例（每个独立函数，main 逐个调用）：

1. `testAscii`：feedBoth("Hello") → checkRowEqual(0, 5)；光标 position/visible 两侧一致。
2. `testSgrIndexed`：feedBoth("\x1b[31mR\x1b[0mN") → 格(0,0)/(0,1) 双后端一致（Indexed(1) / Default）。
3. `testRgb`：feedBoth("\x1b[38;2;10;20;30m\x1b[48;2;1;2;3mX") → 格(0,0) 一致。
4. `testStyles`：feedBoth("\x1b[1mB\x1b[0m\x1b[3mI\x1b[0m\x1b[4mU") → 三格 attributes 一致（bold/italic/underline(Single)）。
5. `testCjkWide`：feedBoth("中A") → 格(0,0) WideLead、格(0,1) WideContinuation、格(0,2) 'A'，双后端一致。
6. `testAltScreen`：feedBoth("MAIN\x1b[?1049h") → isAlternateScreen 两侧 true；feedBoth("ALT") → 行 0 前 3 格一致；feedBoth("\x1b[?1049l") → false 且行 0 恢复 "MAIN"（两侧一致）。
7. `testChangesFlags`：feedBoth("\x07") → 两侧 changes.bell 均 true；feedBoth("\x1b]0;X\x07") → titleChanged 且 title()=="X" 两侧一致。
8. `testScrollback`：双后端各 feed 30 行（"L0\r\n"…"L29\r\n"，同一循环分别 feed 并各自聚合 scrolledOutLines 累计值）→ 两侧累计 scrollback 行数相等（若实测两侧滚动边界语义差 1，以实测为准统一断言容差并注释原因）；视图尺寸不变。
9. `testResize`：feedBoth("Keep") → 两侧 resize(100,30) 均 true → size 一致、(0,0..3) 文本一致（native 网格级不 reflow 与 Contour reflow 对行首短文本行为一致；更复杂 reflow 差异不属本测试）。
10. `testCursorVisibility`：feedBoth("AB\x1b[?25l") → cursor().visible 两侧 false；feedBoth("\x1b[?25h") → true。

- [ ] **步骤 2：运行测试验证失败/通过并修复差异**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_backend_compat -V`
预期：首轮可能 FAIL——真实语义差异（如 native 与 Contour 对某 SGR 序列的属性映射差、scrollback 差 1、空文本格 text 是否为空串）。**逐一研判**：差异属 (a) 转换层 bug → 修 adapter/视图；(b) 两后端真实语义分歧（可接受的实现差）→ 在该用例注释说明并调整为断言各自语义（不得放宽成恒真）。全部研判记录进报告。

- [ ] **步骤 3：全量回归 + Commit**

运行：`ctest --preset linux-gcc-debug`（预期 17/17）。

```bash
git add tests/unit/test_backend_compat.cpp tests/CMakeLists.txt
git commit -m "test(backend): 双后端逐格对照测试（ASCII/SGR/RGB/样式/CJK/alt/scrollback/resize/光标）"
```

---

### 任务 6：demo 双后端化（ZzTermSmoke --backend + verify_smoke.py 参数化 + 双 ctest）

**文件：**
- 修改：`examples/ZzTermSmoke/main.cpp`（参数解析重写、output handler 接 PTY 写回）
- 修改：`tests/interactive/verify_smoke.py`（argv[3] backend 参数）
- 修改：`tests/CMakeLists.txt`（ZzTermSmokeEcho/ZzTermSmokeInteractive 按 backend 循环注册）

- [ ] **步骤 1：重写 demo 参数解析 + output 接线**

`examples/ZzTermSmoke/main.cpp` 的 main（:340-361）重写为：

```cpp
int main(int argc, char** argv)
{
    ZzBackendKind backend = ZzBackendKind::Contour; // v2.1 默认后端方向
    std::vector<std::string> command;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::printf("usage: ZzTermSmoke [--backend=native|contour] [-- command...]\n");
            return 0;
        }
        if (arg.rfind("--backend=", 0) == 0) {
            const std::string_view value = arg.substr(10);
            if (value == "native") backend = ZzBackendKind::Native;
            else if (value == "contour") backend = ZzBackendKind::Contour;
            else { std::fprintf(stderr, "unknown backend: %.*s\n", (int)value.size(), value.data()); return 2; }
            continue;
        }
        if (arg == "--") {
            for (++i; i < argc; ++i) command.emplace_back(argv[i]);
            break;
        }
        std::fprintf(stderr, "unknown argument: %.*s\n", (int)arg.size(), arg.data());
        return 2;
    }
    if (command.empty()) command.emplace_back("bash");
    return run(backend, command);
}
```

`run` 签名改为 `int run(ZzBackendKind backend, const std::vector<std::string>& command)`；终端构造（原 :238）改为 `ZzTerminal term(termSize.cols, termSize.rows, backend, 1000);`；构造后追加 output 接线（DA 响应等回传字节写回 PTY——pty->writeAll 签名 `bool writeAll(std::span<const std::byte>)`，pty/unix/ZzPty.h:74）：

```cpp
    term.setOutputHandler([&pty](std::string_view bytes) {
        pty->writeAll(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
    });
```

（renderScreen 已在任务 1 适配新视图契约，本任务不再动。）

- [ ] **步骤 2：verify_smoke.py 参数化**

`tests/interactive/verify_smoke.py`：

- 参数区（:17-18 附近）追加：`BACKEND = sys.argv[3] if len(sys.argv) > 3 else "contour"`。
- spawn 调用（:84-85）的 argv 列表从 `[]` 改为 `[f"--backend={BACKEND}"]`。
- 文件头注释与 check 输出标签可选加注 backend 名（便于日志区分），断言逻辑零改动。

- [ ] **步骤 3：tests/CMakeLists.txt 双后端注册**

把现有 `ZzTermSmokeEcho`（:48-52）与 `ZzTermSmokeInteractive`（:57-72）块改为按后端循环：

```cmake
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
```

（旧的 ZzTermSmokeEcho/ZzTermSmokeInteractive 两个测试名随之消失，由 _native/_contour 变体取代——全量验收计数按新名字计。）

- [ ] **步骤 4：运行双后端 demo 测试验证**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R "ZzTermSmoke" -V`
预期：4 个 demo 测试（Echo_native/Echo_contour/Interactive_native/Interactive_contour）全 PASS。若 Interactive_contour 失败：pyte 断言的哪一步挂（bash 提示符/ls 着色/SIGWINCH/less/vim）即为 adapter 真实缺口，按 SDD 流程上报，不得放宽 pyte 断言。

- [ ] **步骤 5：Commit**

```bash
git add examples/ZzTermSmoke/main.cpp tests/interactive/verify_smoke.py tests/CMakeLists.txt
git commit -m "feat(demo): ZzTermSmoke --backend 双后端化，output 通道接 PTY 写回，双 ctest 注册"
```

---

### 任务 7：PIC/shared 构建 + M1b 全量验收

**文件：**
- 修改：`cmake/ContourBackend.cmake`（Contour 聚合层统一 PIC）

- [ ] **步骤 1：Contour 聚合层开 PIC**

`cmake/ContourBackend.cmake` 在选项定义之后、各 add_subdirectory 之前追加：

```cmake
# M1b：ZzTermContourBackend 将 PRIVATE 链入 ZzTermCore；
# BUILD_SHARED_LIBS=ON 时静态库进动态库必须全员 PIC。
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
```

（目录作用域，被本文件聚合的全部 add_subdirectory——crispy-core/vtpty/vtparser/vtbackend/src/backend/contour——继承。）

- [ ] **步骤 2：shared 构建验证**

运行：

```bash
cmake -S . -B build/m1b-shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/m1b-shared-check && ctest --test-dir build/m1b-shared-check
```

预期：构建无 PIC 链接错误（relocation R_X86_64_PC32 之类），ctest 全绿（与 ON 静态构建同计数）。若报 PIC 错误：定位缺 PIC 的 target，确认步骤 1 的 set 在 add_subdirectory 之前；CPM 拉取的 header-only 依赖（GSL/boxed/reflection）无编译产物不受影响，libunicode 若有编译产物需在其 CPMAddPackage 后补 `set_target_properties(... PROPERTIES POSITION_INDEPENDENT_CODE ON)`。

- [ ] **步骤 3：ON/OFF 全量回归**

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake -S . -B build/m1b-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m1b-off-check && ctest --test-dir build/m1b-off-check
```

预期：ON 全绿（18：12 个 C++ 单测可执行含 test_backend_interface/test_backend_compat/test_contour_adapter/test_contour_backend 等 + test_contour_smoke + Echo×2 + Interactive×2，按实际注册数核）；OFF 全绿（Contour 相关 target/测试不出现，demo 仅 native 变体）。

- [ ] **步骤 4：doxygen 验收**

```bash
doxygen Doxyfile
```

预期：exit 0 零 warning（新增/修改的公开头注释遵守约束：无尖括号、code span 内容不以点开头）。

- [ ] **步骤 5：Commit**

```bash
git add cmake/ContourBackend.cmake
git commit -m "build(contour): Contour 聚合层统一 PIC，shared 构建纳入验收"
```

---

## 自检记录

- **规格覆盖度**：规格 4.2 视图契约 → 任务 1（+任务 3 颜色/属性统一）；4.3 事件桥接与 output 通道 → 任务 4；4.4 双后端接入（ZzBackendKind/facade/native 迁移/PIC）→ 任务 2 + 任务 7；4.5 测试（native 显式 Native/compat/M1a 适配/interface 修订/demo 双跑）→ 任务 2/5/3/4/6；4.6 DoD（ON/OFF/shared/doxygen）→ 任务 7 + 各任务内嵌验证。规格第 6 节风险（ZzColor 表达力/迁移回归/视图性能/光标 const）→ 计划头部勘误 2、任务 2 通用约束、计划头部勘误 3、任务 4 cursor const_cast 收口，均有对应。
- **规格勘误**（头部 6 条）：ZzCellFlag 不上移（复用 ZzCellAttributes）、ZzColor 不扩展 Undefined、ZzCellView.text 用 UTF-8 std::string、ZzCursorState 移 Types.h、统一视图砍 scrollback 入口、screen()/scrollback() 限定 Native——均已写清理由与落地任务。
- **占位符扫描**：无 TODO/待定；所有代码步骤含完整代码；任务 4 的 EventsImpl 注释内含全部 5 个回调的处置（非占位，是实现要点摘要，分派时由任务简报给出完整代码）。
- **类型一致性**：ZzCellView/ZzLineView/ZzRenderView（任务 1 定义）→ 任务 4 Contour 实现、任务 5 比对、任务 6 demo 消费，签名一致；ZzBackendKind/构造签名（任务 2）→ 任务 4 测试、任务 6 demo 一致；zzColor/zzAttributes/zzWidth（任务 3 定义于 ZzContourBackend.cpp，任务 4 提入 ZzContourConvert.h）名字一致；`zzCreateContourBackendAdapter`（任务 2 宏分支占位、任务 4 实现）名字一致；ZzContourRenderView::State 字段名 dirtyGeneration/dirtySinceClear 全计划一致。
- **计数基线**：ON 15（任务 1-3 保持）→ 16（任务 4 +adapter）→ 17（任务 5 +compat）→ 任务 6 后 Echo/Interactive 改名 ×2（-2 +4 = 19 上限，python 依赖缺失时 17）；OFF 13/13 全程。任务 7 步骤 3 按实际注册数核对。
