# M1a：ZzContourBackend 核心封装 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 在 `contour` 分支上实现 `ZzContourBackend`——headless 驱动 Contour `vtbackend::Terminal` 的核心封装（构造/feed/resize/事件回写/拷贝式快照），配套 headless 回归测试。

**架构：** 新 STATIC target `ZzTermContourBackend`（仅 `ZZTERM_WITH_CONTOUR=ON` 构建，PRIVATE 链接 `vtbackend`、PRIVATE cxx_std_23）。公开头 `ZzContourBackend.h` 不暴露任何 Contour 类型（PImpl），消费者无需 C++23。内部三件套：`ZzContourPtyBridge`（vtpty::Pty 回写桥）、`ZzContourEvents`（事件抽象接口）、PImpl 内的 EventsImpl（继承 `Terminal::NullEvents` 转发事件）。`ZzTermCore` 库不链接本 target（收口留 M1b）。

**技术栈：** C++（公开头 C++17 兼容、实现 cxx_std_23）、Contour vtbackend/vtpty（submodule @6777ff0）、CMake + CTest。

**对应规格：** `docs/superpowers/specs/2026-09-19-contour-m1a-backend-design.md`

**通用约束（每个任务都必须遵守）：**

- 分支：`contour`。构建：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug`；测试：`ctest --preset linux-gcc-debug`。
- 公开头内禁止出现 Contour 类型与 include；doxygen 注释内禁止尖括号（写成「索引」「RGB 值」等中文描述）；markdown 行内 code span 内禁止尖括号、内容禁止以点开头（如 `.value` 会触发 doxygen tt 标签不匹配，已实测）、code span 后禁止紧跟顿号。
- Contour target 一律 PRIVATE 链接（tests/CMakeLists.txt 已有此注释约定）。
- commit 规范：`type(scope): 中文描述`。
- 关键 Contour 事实（已实测钉死，直接照用）：
  - PageSize 与 ImageSize 字段顺序 lines 在前、columns 在后；LineCount(24)、ColumnCount(80)、Width、Height 为 boxed 包装，用 value 成员解包。
  - `Settings`（`vtbackend/screen/Settings.hpp`）：`pageSize`、`historyLimits`（类型 `HistoryLimits`，用 `HistoryLimits::plain(LineCount(n))`）、`ptyReadBufferSize`（size_t，4096）、`goodImageProtocol`（bool，false 即关闭）。
  - `Terminal` 构造（`vtbackend/screen/Terminal.hpp:465`）五参：Events 引用、crispy Environment 常量引用、unique_ptr 包装的 vtpty::Pty、Settings、steady_clock 时间点；`crispy::defaultEnvironment()` 在 `crispy/Environment.hpp`。
  - `Terminal::NullEvents` 存在（同头文件），可直接继承，唯一纯虚 `openDocument` 它已空实现。
  - 读路径：`terminal.currentScreen()` → `Screen`；`screen.at(LineOffset, ColumnOffset)` 得 CellProxy（`codepoints()`/`width()`/`flags()`/`foregroundColor()`/`backgroundColor()`）；`screen.historyLineCount()`；`screen.lineFlags(LineOffset)` 判定 `LineFlag::Wrapped`。
  - Color：`color.type()` 得 `ColorType::{Undefined,Default,Bright,Indexed,RGB}`；`color.index()` 读索引（Bright 同用，0-7）；`color.rgb()` 得 `RGBColor{red,green,blue}`（uint8 字段）。
  - 光标：`terminal.refreshRenderBuffer()` 后 `terminal.renderBuffer()` 得 RAII 句柄，`ref.get().cursor` 为 optional 包装的 RenderCursor（nullopt = 不可见），`RenderCursor::position` 为 `CellLocation{line,column}`（boxed，用 value 成员解包）。
  - 宽字符续格：首格 `width()==2`；续格 `width()==1`、`codepointCount()==0`、`flags().contains(CellFlag::WideCharContinuation)`。
  - DA 响应（`\x1b[c`）先入缓冲，必须 `terminal.flushInput()` 才经 Pty::write 发出；默认 VT525 → 前缀 `\x1b[?65;`、结尾 `c`。
  - `ScreenType::{Primary, Alternate}`；alt screen 进出序列为 `\x1b[?1049h` / `\x1b[?1049l`。
  - **绝不调** `Terminal::start()`（无实现）；锁内回调（cursorPositionChanged 等）内禁止回读 Terminal。

---

### 任务 1：ZzContourPtyBridge 回写桥 + target 骨架

**文件：**
- 创建：`src/backend/contour/ZzContourPtyBridge.h`
- 创建：`src/backend/contour/ZzContourPtyBridge.cpp`
- 创建：`src/backend/contour/CMakeLists.txt`
- 修改：`cmake/ContourBackend.cmake`（末尾追加 add_subdirectory）
- 修改：`tests/CMakeLists.txt`（REMOVE_ITEM + 条件注册块）
- 测试：`tests/unit/test_contour_backend.cpp`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_contour_backend.cpp`：

```cpp
// ZzContourBackend（M1a）headless 回归测试。
// 仅 ZZTERM_WITH_CONTOUR=ON 时构建（见 tests/CMakeLists.txt 条件注册）。
#include "ZzContourPtyBridge.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;

#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

// 桥：write 转发回调、read 恒无数据、pageSize 记录、close 状态。
void testBridge()
{
    std::string written;
    auto const initialSize = vtpty::PageSize { vtpty::LineCount(24), vtpty::ColumnCount(80) };
    ZzContourPtyBridge bridge(initialSize,
                              [&written](std::string_view data) { written.append(data); });

    ZZ_CHECK(!bridge.isClosed());
    ZZ_CHECK(bridge.write("hello") == 5);
    ZZ_CHECK(written == "hello");
    ZZ_CHECK(bridge.pageSize().columns.value == 80);
    ZZ_CHECK(bridge.pageSize().lines.value == 24);

    bridge.resizeScreen(vtpty::PageSize { vtpty::LineCount(30), vtpty::ColumnCount(100) },
                        std::nullopt);
    ZZ_CHECK(bridge.pageSize().columns.value == 100);
    ZZ_CHECK(bridge.pageSize().lines.value == 30);

    ZZ_CHECK(bridge.start().has_value());
    (void) bridge.slave();
    bridge.wakeupReader();
    bridge.close();
    ZZ_CHECK(bridge.isClosed());
    bridge.waitForClosed();
}

} // namespace

int main()
{
    testBridge();
    if (g_failures != 0)
        std::fprintf(stderr, "test_contour_backend: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：运行构建验证失败**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug`
预期：当前无需变化即可通过（测试尚未注册）；直接运行 `ctest --preset linux-gcc-debug` 确认列表中**没有** test_contour_backend——失败形态是「测试不存在」，由步骤 3-4 引入。

- [ ] **步骤 3：实现桥 + target 骨架**

创建 `src/backend/contour/ZzContourPtyBridge.h`：

```cpp
#pragma once

#include <vtpty/Pty.hpp>

#include <functional>
#include <optional>
#include <string_view>

/// \brief headless 场景下的 vtpty::Pty 实现：把终端回传字节（DA 响应等）转发到注入回调。
/// read 恒无数据；slave 使用 PtySlaveDummy；pageSize 内部记录。
class ZzContourPtyBridge : public vtpty::Pty
{
public:
    using WriteCallback = std::function<void(std::string_view)>;

    ZzContourPtyBridge(vtpty::PageSize initialPageSize, WriteCallback onWrite);

    vtpty::StartResult start() override;
    vtpty::PtySlave& slave() noexcept override;
    void close() override;
    void waitForClosed() override;
    [[nodiscard]] bool isClosed() const noexcept override;
    std::optional<ReadResult> read(crispy::BufferObject<char>& storage,
                                   std::optional<std::chrono::milliseconds> timeout,
                                   size_t size) override;
    void wakeupReader() override;
    int write(std::string_view buf) override;
    [[nodiscard]] vtpty::PageSize pageSize() const noexcept override;
    void resizeScreen(vtpty::PageSize cells, std::optional<vtpty::ImageSize> pixels) override;

private:
    vtpty::PtySlaveDummy slave_;
    vtpty::PageSize pageSize_;
    WriteCallback onWrite_;
    bool closed_ = false;
};
```

创建 `src/backend/contour/ZzContourPtyBridge.cpp`：

```cpp
#include "ZzContourPtyBridge.h"

ZzContourPtyBridge::ZzContourPtyBridge(vtpty::PageSize initialPageSize, WriteCallback onWrite)
    : pageSize_(initialPageSize)
    , onWrite_(std::move(onWrite))
{
}

vtpty::StartResult ZzContourPtyBridge::start()
{
    return vtpty::StartOutcome {};
}

vtpty::PtySlave& ZzContourPtyBridge::slave() noexcept
{
    return slave_;
}

void ZzContourPtyBridge::close()
{
    closed_ = true;
}

void ZzContourPtyBridge::waitForClosed()
{
}

bool ZzContourPtyBridge::isClosed() const noexcept
{
    return closed_;
}

std::optional<vtpty::Pty::ReadResult> ZzContourPtyBridge::read(
    crispy::BufferObject<char>& /*storage*/,
    std::optional<std::chrono::milliseconds> /*timeout*/,
    size_t /*size*/)
{
    return std::nullopt;
}

void ZzContourPtyBridge::wakeupReader()
{
}

int ZzContourPtyBridge::write(std::string_view buf)
{
    if (onWrite_)
        onWrite_(buf);
    return static_cast<int>(buf.size());
}

vtpty::PageSize ZzContourPtyBridge::pageSize() const noexcept
{
    return pageSize_;
}

void ZzContourPtyBridge::resizeScreen(vtpty::PageSize cells,
                                      std::optional<vtpty::ImageSize> /*pixels*/)
{
    pageSize_ = cells;
}
```

创建 `src/backend/contour/CMakeLists.txt`：

```cmake
# ZzTermContourBackend：Contour vtbackend 的 headless 核心封装（M1a）。
# 仅 ZZTERM_WITH_CONTOUR=ON 时由 cmake/ContourBackend.cmake 聚合进来。
# 公开头不暴露 Contour 类型；Contour target 一律 PRIVATE 链接。
add_library(ZzTermContourBackend STATIC
    ZzContourPtyBridge.cpp
)
target_include_directories(ZzTermContourBackend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ZzTermContourBackend PRIVATE vtbackend)
target_compile_features(ZzTermContourBackend PRIVATE cxx_std_23)
```

修改 `cmake/ContourBackend.cmake`：在文件**末尾**（四个 Contour 子目录 add_subdirectory 之后）追加：

```cmake
# M1a：ZzContourBackend 核心封装 target（依赖上面聚合好的 vtbackend）。
add_subdirectory("${CMAKE_SOURCE_DIR}/src/backend/contour" "${CMAKE_BINARY_DIR}/src/backend/contour")
```

修改 `tests/CMakeLists.txt`：
1. 在现有 `list(REMOVE_ITEM ...)` 块中追加一行（与 test_contour_smoke.cpp 并列）：

```cmake
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_contour_backend.cpp")
```

2. 在 `if(TARGET vtparser) ... endif()` 块之后追加：

```cmake
# M1a：ZzContourBackend headless 回归测试，仅 Contour 后端启用时构建。
if(TARGET ZzTermContourBackend)
    add_executable(test_contour_backend unit/test_contour_backend.cpp)
    target_link_libraries(test_contour_backend PRIVATE ZzTermContourBackend vtpty) # Contour target 一律 PRIVATE
    target_compile_features(test_contour_backend PRIVATE cxx_std_23)
    add_test(NAME test_contour_backend COMMAND test_contour_backend)
endif()
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：全绿，测试总数 15（14 基线 + test_contour_backend），新测试 PASS。
若编译报 `vtpty::StartResult`/`StartOutcome` 名字错误，到 `third_party/contour/src/vtpty/Pty.hpp:82-104` 核对实际类型名（StartResult 是 expected 包装，StartOutcome 是成功值类型），以头文件为准修正。

- [ ] **步骤 5：Commit**

```bash
git add src/backend/contour/ cmake/ContourBackend.cmake tests/CMakeLists.txt tests/unit/test_contour_backend.cpp
git commit -m "feat(contour): ZzContourPtyBridge 回写桥与 ZzTermContourBackend target 骨架"
```

---

### 任务 2：ZzContourEvents 接口 + ZzContourBackend 骨架（feed/resize/ASCII 快照）

**文件：**
- 创建：`src/backend/contour/ZzContourEvents.h`
- 创建：`src/backend/contour/ZzContourBackend.h`
- 创建：`src/backend/contour/ZzContourBackend.cpp`
- 修改：`src/backend/contour/CMakeLists.txt`（源文件列表加 ZzContourBackend.cpp）
- 测试：`tests/unit/test_contour_backend.cpp`（追加）

- [ ] **步骤 1：编写失败的测试**

在 `tests/unit/test_contour_backend.cpp` 的匿名命名空间内、`testBridge` 之后追加（include 区追加 `"ZzContourBackend.h"` 头与 utility、vector 两个标准头）：

```cpp
// 事件记录器：实现 ZzContourEvents 全部纯虚，记录各事件。
class RecordingEvents : public ZzContourEvents
{
public:
    void onTitleChanged(std::string title) override { this->title = std::move(title); ++titleCount; }
    void onBell() override { ++bellCount; }
    void onScreenDirty() override { ++dirtyCount; }
    void onActiveBufferChanged(bool alternate) override { altChanges.push_back(alternate); }
    void onWriteToTransport(std::string bytes) override { written += bytes; }

    std::string title;
    int titleCount = 0;
    int bellCount = 0;
    int dirtyCount = 0;
    std::vector<bool> altChanges;
    std::string written;
};

// 快照辅助：把第 line 行前 n 列的 codepoints 拼成 u32string。
std::u32string rowText(ZzContourSnapshot const& snap, int line, int n)
{
    std::u32string out;
    for (int col = 0; col < n; ++col)
        out += snap.at(line, col).codepoints;
    return out;
}

// ASCII 写入 → 快照读回一致；尺寸与默认状态正确。
void testAsciiSnapshot()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);

    ZZ_CHECK(backend.size() == std::make_pair(80, 24));
    ZZ_CHECK(!backend.isAlternateScreen());
    ZZ_CHECK(backend.title().empty());
    ZZ_CHECK(backend.historyLineCount() == 0);

    backend.feed("Hello");
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.columns == 80);
    ZZ_CHECK(snap.rows == 24);
    ZZ_CHECK(!snap.alternateScreen);
    ZZ_CHECK(snap.cells.size() == static_cast<size_t>(80 * 24));
    ZZ_CHECK(rowText(snap, 0, 5) == U"Hello");
    ZZ_CHECK(snap.at(1, 0).codepoints.empty());
}

// resize 后尺寸与既有内容保持。
void testResizeBasic()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("Keep");
    backend.resize(100, 30);
    ZZ_CHECK(backend.size() == std::make_pair(100, 30));
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.columns == 100);
    ZZ_CHECK(snap.rows == 30);
    ZZ_CHECK(rowText(snap, 0, 4) == U"Keep");
}
```

`main()` 中追加调用 `testAsciiSnapshot(); testResizeBasic();`。

- [ ] **步骤 2：运行构建验证失败**

运行：`cmake --build --preset linux-gcc-debug`
预期：FAIL——编译错误，`ZzContourBackend.h` 不存在。

- [ ] **步骤 3：实现 ZzContourEvents 与 ZzContourBackend 骨架**

创建 `src/backend/contour/ZzContourEvents.h`（纯项目类型，无 Contour 依赖）：

```cpp
#pragma once

#include <string>

/// \brief ZzContourBackend 的事件抽象接口。库使用方实现该接口接收终端事件。
/// 所有回调在 feed/resize 调用线程内同步触发；实现方不得在回调内回读 backend
/// 快照以外的 Terminal 内部状态（部分回调在 Terminal 锁内触发）。
class ZzContourEvents
{
public:
    virtual ~ZzContourEvents() = default;
    /// \brief 窗口标题变化（OSC 0/2）。
    virtual void onTitleChanged(std::string title) = 0;
    /// \brief BEL 字符。
    virtual void onBell() = 0;
    /// \brief 屏幕内容脏（需要重绘）信号。
    virtual void onScreenDirty() = 0;
    /// \brief 主屏/备用屏切换；alternate 为 true 表示进入备用屏。
    virtual void onActiveBufferChanged(bool alternate) = 0;
    /// \brief 终端回传给传输层的字节（DA 响应、光标上报等）。
    virtual void onWriteToTransport(std::string bytes) = 0;
};
```

创建 `src/backend/contour/ZzContourBackend.h`（不暴露任何 Contour 类型）：

```cpp
#pragma once

#include "ZzContourEvents.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// \brief 单元格颜色，保留颜色身份（不预转 RGB）。
struct ZzColor
{
    enum class Tag : std::uint8_t
    {
        Undefined, ///< 未设置
        Default,   ///< 终端默认色
        Indexed,   ///< 调色板索引（含亮色，亮 n 号色映射为索引 8+n）
        RGB        ///< 真彩色，value 为 0xRRGGBB
    };
    Tag tag = Tag::Default;
    std::uint32_t value = 0;
};

inline bool operator==(ZzColor const& a, ZzColor const& b)
{
    return a.tag == b.tag && a.value == b.value;
}
inline bool operator!=(ZzColor const& a, ZzColor const& b)
{
    return !(a == b);
}

/// \brief 单元格标志位掩码。
struct ZzCellFlag
{
    enum : std::uint32_t
    {
        None = 0,
        Bold = 1u << 0,
        Faint = 1u << 1,
        Italic = 1u << 2,
        Underline = 1u << 3,
        Blinking = 1u << 4,
        Inverse = 1u << 5,
        Hidden = 1u << 6,
        CrossedOut = 1u << 7,
        WideCharContinuation = 1u << 8 ///< 宽字符续格（本格无独立内容）
    };
};

/// \brief 快照中的单个单元格（拷贝语义，不引用 Terminal 内部）。
struct ZzContourCell
{
    std::u32string codepoints;         ///< 簇内全部 codepoint；续格与空格为空
    ZzColor foreground;
    ZzColor background;
    std::uint32_t flags = ZzCellFlag::None;
    int width = 1;                     ///< 1 或 2（宽字符首格为 2）
};

/// \brief 光标位置（0 起行列）。
struct ZzContourCursor
{
    int line = 0;
    int column = 0;
};

/// \brief 拷贝式屏幕快照（仅主屏/当前屏可见区域，不含 scrollback）。
struct ZzContourSnapshot
{
    int columns = 0;
    int rows = 0;
    bool alternateScreen = false;
    std::vector<ZzContourCell> cells;  ///< rows * columns，行主序
    std::optional<ZzContourCursor> cursor; ///< 光标不可见时为 nullopt

    ZzContourCell const& at(int line, int column) const
    {
        return cells[static_cast<size_t>(line * columns + column)];
    }
};

/// \brief Contour vtbackend 的 headless 核心封装（M1a）。
/// 同步直驱：feed 即解析；不启动 Terminal 内部线程；拷贝式快照。
class ZzContourBackend
{
public:
    /// \param columns 列数；\param rows 行数；\param events 事件接收方（寿命须包住本对象）；
    /// \param scrollbackLines scrollback 行数上限。
    ZzContourBackend(int columns, int rows, ZzContourEvents& events, int scrollbackLines = 1000);
    ~ZzContourBackend();
    ZzContourBackend(ZzContourBackend const&) = delete;
    ZzContourBackend& operator=(ZzContourBackend const&) = delete;

    /// \brief 同步喂入终端字节流（可多次、可跨任意边界拆分）。
    void feed(std::string_view data);
    /// \brief 调整屏幕行列；cell 像素按固定 8x17 同步。
    void resize(int columns, int rows);

    [[nodiscard]] std::pair<int, int> size() const;
    [[nodiscard]] bool isAlternateScreen() const;
    [[nodiscard]] std::string title() const;
    [[nodiscard]] int historyLineCount() const;
    /// \brief 第 row 行（主屏 0 起）是否为自动换行的续行起点。
    [[nodiscard]] bool lineWrapped(int row) const;
    /// \brief 把缓冲中的终端回传字节（DA 响应等）经 onWriteToTransport 发出。
    void flushReplies();

    /// \brief 取当前屏拷贝式快照（非 const：内部需刷新 RenderBuffer 取光标）。
    ZzContourSnapshot snapshot();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

创建 `src/backend/contour/ZzContourBackend.cpp`：

```cpp
#include "ZzContourBackend.h"

#include "ZzContourPtyBridge.h"

#include <vtbackend/screen/Terminal.hpp>

#include <crispy/Environment.hpp>

#include <chrono>
#include <utility>

namespace {

vtbackend::PageSize makePageSize(int columns, int rows)
{
    return vtbackend::PageSize { vtbackend::LineCount(rows), vtbackend::ColumnCount(columns) };
}

} // namespace

struct ZzContourBackend::Impl
{
    // EventsImpl 先于 terminal 声明：Terminal 构造需要 Events&，且 EventsImpl 寿命须包住 Terminal。
    class EventsImpl : public vtbackend::Terminal::NullEvents
    {
    public:
        explicit EventsImpl(Impl& owner) : owner_(owner) {}
        // 事件转发在任务 4/5 逐个接线；本任务先保证构造/feed/resize/快照可用。
    private:
        Impl& owner_;
    };

    ZzContourEvents& listener;
    std::string title;
    EventsImpl eventsImpl;
    vtbackend::PageSize pageSize;
    ZzContourPtyBridge* bridge = nullptr; // 所有权在 terminal
    std::unique_ptr<vtbackend::Terminal> terminal;

    Impl(int columns, int rows, ZzContourEvents& events, int scrollbackLines)
        : listener(events)
        , eventsImpl(*this)
        , pageSize(makePageSize(columns, rows))
    {
        auto settings = vtbackend::Settings {};
        settings.pageSize = pageSize;
        settings.historyLimits = vtbackend::HistoryLimits::plain(vtbackend::LineCount(scrollbackLines));
        settings.ptyReadBufferSize = 4096;
        settings.goodImageProtocol = false;

        auto bridgePtr = std::make_unique<ZzContourPtyBridge>(
            pageSize, [this](std::string_view data) { listener.onWriteToTransport(std::string(data)); });
        bridge = bridgePtr.get();

        terminal = std::make_unique<vtbackend::Terminal>(eventsImpl,
                                                         crispy::defaultEnvironment(),
                                                         std::move(bridgePtr),
                                                         settings,
                                                         std::chrono::steady_clock::now());
    }
};

ZzContourBackend::ZzContourBackend(int columns, int rows, ZzContourEvents& events, int scrollbackLines)
    : impl_(std::make_unique<Impl>(columns, rows, events, scrollbackLines))
{
}

ZzContourBackend::~ZzContourBackend() = default;

void ZzContourBackend::feed(std::string_view data)
{
    impl_->terminal->writeToScreen(data);
}

void ZzContourBackend::resize(int columns, int rows)
{
    impl_->pageSize = makePageSize(columns, rows);
    impl_->terminal->resizeScreen(
        impl_->pageSize,
        vtbackend::ImageSize { vtbackend::Width(static_cast<unsigned>(columns) * 8u),
                               vtbackend::Height(static_cast<unsigned>(rows) * 17u) });
}

std::pair<int, int> ZzContourBackend::size() const
{
    return { impl_->pageSize.columns.value, impl_->pageSize.lines.value };
}

bool ZzContourBackend::isAlternateScreen() const
{
    return impl_->terminal->isAlternateScreen();
}

std::string ZzContourBackend::title() const
{
    return impl_->title;
}

int ZzContourBackend::historyLineCount() const
{
    return impl_->terminal->currentScreen().historyLineCount().value;
}

bool ZzContourBackend::lineWrapped(int row) const
{
    return impl_->terminal->currentScreen()
        .lineFlags(vtbackend::LineOffset(row))
        .contains(vtbackend::LineFlag::Wrapped);
}

void ZzContourBackend::flushReplies()
{
    impl_->terminal->flushInput();
}

ZzContourSnapshot ZzContourBackend::snapshot()
{
    auto const& screen = impl_->terminal->currentScreen();
    ZzContourSnapshot snap;
    snap.columns = impl_->pageSize.columns.value;
    snap.rows = impl_->pageSize.lines.value;
    snap.alternateScreen = impl_->terminal->isAlternateScreen();
    snap.cells.reserve(static_cast<size_t>(snap.columns * snap.rows));
    for (int line = 0; line < snap.rows; ++line)
    {
        for (int col = 0; col < snap.columns; ++col)
        {
            auto const cell = screen.at(vtbackend::LineOffset(line), vtbackend::ColumnOffset(col));
            ZzContourCell out;
            out.codepoints = cell.codepoints();
            out.width = static_cast<int>(cell.width());
            // 颜色与 flags 转换在任务 3 接线；光标在任务 4 接线。
            snap.cells.push_back(std::move(out));
        }
    }
    return snap;
}
```

修改 `src/backend/contour/CMakeLists.txt`，把源文件列表改为：

```cmake
add_library(ZzTermContourBackend STATIC
    ZzContourPtyBridge.cpp
    ZzContourBackend.cpp
)
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_contour_backend -V`
预期：PASS。若编译报类型名错误（如 `HistoryLimits`、`LineFlag`、`CellProxy` 方法名），到 `third_party/contour/src/vtbackend/` 对应头核对：`core/Primitives.hpp`（HistoryLimits/PageSize/LineOffset）、`core/LineFlags.hpp`（LineFlag）、`grid/CellProxy.hpp`（codepoints/width）、`screen/Screen.hpp`（at/historyLineCount/lineFlags）。
随后跑全量 `ctest --preset linux-gcc-debug`，预期 15/15 全绿。

- [ ] **步骤 5：Commit**

```bash
git add src/backend/contour/ tests/unit/test_contour_backend.cpp
git commit -m "feat(contour): ZzContourEvents 接口与 ZzContourBackend 骨架（feed/resize/ASCII 快照）"
```

---

### 任务 3：快照颜色与 CellFlags 转换（SGR 索引色 / RGB 真彩色 / 粗体）

**文件：**
- 修改：`src/backend/contour/ZzContourBackend.cpp`（加 zzColor/zzFlags 转换并接入 snapshot）
- 测试：`tests/unit/test_contour_backend.cpp`（追加）

- [ ] **步骤 1：编写失败的测试**

在匿名命名空间内追加：

```cpp
// SGR 索引色前景在快照中保留颜色身份（不预转 RGB）。
void testSgrColors()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[31mR\x1b[0m");
    auto snap = backend.snapshot();
    auto const& cell = snap.at(0, 0);
    ZZ_CHECK(cell.codepoints == U"R");
    ZZ_CHECK(cell.foreground == (ZzColor { ZzColor::Tag::Indexed, 1 }));
    // 复位后写入的格子回到默认色。
    ZZ_CHECK(snap.at(0, 1).foreground == (ZzColor { ZzColor::Tag::Default, 0 }));
}

// SGR 亮红色映射为索引 8+1。
void testBrightColor()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[91mB");
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.at(0, 0).foreground == (ZzColor { ZzColor::Tag::Indexed, 9 }));
}

// RGB 真彩色前景与背景。
void testRgbColor()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[38;2;10;20;30m\x1b[48;2;200;100;50mX");
    auto snap = backend.snapshot();
    auto const& cell = snap.at(0, 0);
    ZZ_CHECK(cell.foreground
             == (ZzColor { ZzColor::Tag::RGB, (10u << 16) | (20u << 8) | 30u }));
    ZZ_CHECK(cell.background
             == (ZzColor { ZzColor::Tag::RGB, (200u << 16) | (100u << 8) | 50u }));
}

// 粗体/斜体/下划线 flags。
void testStyleFlags()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[1mB\x1b[3mI\x1b[4mU");
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.at(0, 0).flags & ZzCellFlag::Bold);
    ZZ_CHECK(snap.at(0, 1).flags & ZzCellFlag::Italic);
    ZZ_CHECK(snap.at(0, 2).flags & ZzCellFlag::Underline);
    // 斜体格不带粗体位。
    ZZ_CHECK(!(snap.at(0, 1).flags & ZzCellFlag::Bold));
}
```

`main()` 中追加调用 `testSgrColors(); testBrightColor(); testRgbColor(); testStyleFlags();`。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_contour_backend`
预期：FAIL——四个用例中断言失败（快照当前填默认色与 None flags）。

- [ ] **步骤 3：实现颜色与 flags 转换**

在 `src/backend/contour/ZzContourBackend.cpp` 的匿名命名空间内追加（include 区追加 `vtbackend/core/CellFlags.hpp` 与 `vtbackend/core/Color.hpp` 两个头）：

```cpp
ZzColor zzColor(vtbackend::Color color)
{
    switch (color.type())
    {
        case vtbackend::ColorType::RGB: {
            auto const rgb = color.rgb();
            return { ZzColor::Tag::RGB,
                     (static_cast<std::uint32_t>(rgb.red) << 16)
                         | (static_cast<std::uint32_t>(rgb.green) << 8)
                         | static_cast<std::uint32_t>(rgb.blue) };
        }
        case vtbackend::ColorType::Indexed:
            return { ZzColor::Tag::Indexed, color.index() };
        case vtbackend::ColorType::Bright:
            // 亮 n 号色统一映射为索引 8+n，保留颜色身份。
            return { ZzColor::Tag::Indexed, static_cast<std::uint32_t>(8 + color.index()) };
        case vtbackend::ColorType::Default:
            return { ZzColor::Tag::Default, 0 };
        case vtbackend::ColorType::Undefined:
        default:
            return { ZzColor::Tag::Undefined, 0 };
    }
}

std::uint32_t zzFlags(vtbackend::CellFlags flags)
{
    std::uint32_t out = ZzCellFlag::None;
    if (flags.contains(vtbackend::CellFlag::Bold)) out |= ZzCellFlag::Bold;
    if (flags.contains(vtbackend::CellFlag::Faint)) out |= ZzCellFlag::Faint;
    if (flags.contains(vtbackend::CellFlag::Italic)) out |= ZzCellFlag::Italic;
    if (flags.contains(vtbackend::CellFlag::Underline)) out |= ZzCellFlag::Underline;
    if (flags.contains(vtbackend::CellFlag::Blinking)) out |= ZzCellFlag::Blinking;
    if (flags.contains(vtbackend::CellFlag::Inverse)) out |= ZzCellFlag::Inverse;
    if (flags.contains(vtbackend::CellFlag::Hidden)) out |= ZzCellFlag::Hidden;
    if (flags.contains(vtbackend::CellFlag::CrossedOut)) out |= ZzCellFlag::CrossedOut;
    if (flags.contains(vtbackend::CellFlag::WideCharContinuation)) out |= ZzCellFlag::WideCharContinuation;
    return out;
}
```

在 `snapshot()` 的单元格循环内（替换「颜色与 flags 转换在任务 3 接线」注释行）接入：

```cpp
            out.foreground = zzColor(cell.foregroundColor());
            out.background = zzColor(cell.backgroundColor());
            out.flags = zzFlags(cell.flags());
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_contour_backend`
预期：PASS。若 `RGBColor` 字段名不对，核对 `third_party/contour/src/vtbackend/core/Color.hpp`（约 :47-54，`red/green/blue` 三个 uint8 字段）。

- [ ] **步骤 5：Commit**

```bash
git add src/backend/contour/ZzContourBackend.cpp tests/unit/test_contour_backend.cpp
git commit -m "feat(contour): 快照颜色身份与 CellFlags 转换（SGR/亮色/RGB/样式位）"
```

---

### 任务 4：快照光标（RenderBuffer 路径）+ title/bell/dirty 事件接线

**文件：**
- 修改：`src/backend/contour/ZzContourBackend.cpp`（EventsImpl 加转发、snapshot 加光标、include RenderBuffer 头）
- 测试：`tests/unit/test_contour_backend.cpp`（追加）

- [ ] **步骤 1：编写失败的测试**

在匿名命名空间内追加：

```cpp
// 光标随写入推进；DECTCEM（CSI ?25l/h）控制可见性。
void testCursor()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("AB");
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(snap.cursor.has_value());
        ZZ_CHECK(snap.cursor->line == 0);
        ZZ_CHECK(snap.cursor->column == 2);
    }
    backend.feed("\x1b[?25l"); // 隐藏光标
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(!snap.cursor.has_value());
    }
    backend.feed("\x1b[?25h"); // 恢复显示
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(snap.cursor.has_value());
    }
}

// OSC title 与 BEL 事件；feed 触发 dirty 信号。
void testTitleBellDirty()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b]0;My Title\x07");
    ZZ_CHECK(events.title == "My Title");
    ZZ_CHECK(events.titleCount == 1);
    ZZ_CHECK(backend.title() == "My Title");

    backend.feed("\x07");
    ZZ_CHECK(events.bellCount == 1);

    ZZ_CHECK(events.dirtyCount > 0);
}
```

`main()` 中追加调用 `testCursor(); testTitleBellDirty();`。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_contour_backend`
预期：FAIL——光标断言失败（cursor 恒 nullopt）、title/bell/dirty 计数为 0。

- [ ] **步骤 3：实现光标读取与事件转发**

修改 `src/backend/contour/ZzContourBackend.cpp`：

1. include 区追加 `vtbackend/render/RenderBuffer.hpp` 头。
2. `EventsImpl` 类体内（`explicit EventsImpl(...)` 之后）追加转发实现：

```cpp
        void setWindowTitle(std::string_view newTitle) override
        {
            owner_.title = std::string(newTitle);
            owner_.listener.onTitleChanged(std::string(newTitle));
        }
        void bell() override { owner_.listener.onBell(); }
        void screenUpdated() override { owner_.listener.onScreenDirty(); }
        // 锁内回调（cursorPositionChanged 等）不接线：M1a 无实时渲染方，
        // 且锁内禁止回读 Terminal；需要时只置标志 defer。
```

（EventsImpl 的私有成员 `owner_` 已存在；把类体注释「事件转发在任务 4/5 逐个接线……」更新为「bufferChanged 在任务 5 接线」。）

3. `snapshot()` 尾部（`return snap;` 之前）接入光标：

```cpp
    impl_->terminal->refreshRenderBuffer();
    {
        auto ref = impl_->terminal->renderBuffer(); // RAII 读锁句柄
        auto const& renderBuffer = ref.get();
        if (renderBuffer.cursor)
        {
            snap.cursor = ZzContourCursor { renderBuffer.cursor->position.line.value,
                                            renderBuffer.cursor->position.column.value };
        }
    }
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_contour_backend -V`
预期：PASS。若 `refreshRenderBuffer`/`RenderCursor` 名字不符，核对 `third_party/contour/src/vtbackend/screen/Terminal.hpp:1310-1331` 与 `render/RenderBuffer.hpp:104-149`。

- [ ] **步骤 5：Commit**

```bash
git add src/backend/contour/ZzContourBackend.cpp tests/unit/test_contour_backend.cpp
git commit -m "feat(contour): 快照光标可见性（RenderBuffer 路径）与 title/bell/dirty 事件接线"
```

---

### 任务 5：alt screen 进出/恢复 + scrollback 增长 + lineWrapped

**文件：**
- 修改：`src/backend/contour/ZzContourBackend.cpp`（EventsImpl 加 bufferChanged 转发）
- 测试：`tests/unit/test_contour_backend.cpp`（追加）

- [ ] **步骤 1：编写失败的测试**

在匿名命名空间内追加：

```cpp
// alt screen：进入/退出/主屏内容恢复，事件按序上报。
void testAltScreen()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("MAIN");
    backend.feed("\x1b[?1049h"); // 进备用屏
    ZZ_CHECK(backend.isAlternateScreen());
    ZZ_CHECK(events.altChanges.size() == 1 && events.altChanges[0]);
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(snap.alternateScreen);
    }
    backend.feed("ALT");
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(rowText(snap, 0, 3) == U"ALT");
    }
    backend.feed("\x1b[?1049l"); // 回主屏
    ZZ_CHECK(!backend.isAlternateScreen());
    ZZ_CHECK(events.altChanges.size() == 2 && !events.altChanges[1]);
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(!snap.alternateScreen);
        ZZ_CHECK(rowText(snap, 0, 4) == U"MAIN"); // 主屏内容恢复
    }
}

// scrollback 随滚屏增长；自动换行行带 Wrapped 标记。
void testScrollback()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events, 1000);
    for (int i = 0; i < 30; ++i)
        backend.feed("L" + std::to_string(i) + "\r\n");
    ZZ_CHECK(backend.historyLineCount() >= 6); // 30 行内容至少滚出 6 行进 scrollback
}

void testLineWrapped()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed(std::string(100, 'a')); // 超过 80 列自动换行
    ZZ_CHECK(backend.lineWrapped(0));
    ZZ_CHECK(!backend.lineWrapped(1));
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.at(0, 0).codepoints == U"a");
    ZZ_CHECK(snap.at(1, 0).codepoints == U"a"); // 第 81 个字符绕到第 1 行
}
```

`main()` 中追加调用 `testAltScreen(); testScrollback(); testLineWrapped();`。

- [ ] **步骤 2：运行测试验证失败**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_contour_backend`
预期：FAIL——`events.altChanges` 为空（bufferChanged 未接线）；scrollback/lineWrapped 用例应已通过（任务 2 已实现对应 API，若意外失败先修 API 再继续）。

- [ ] **步骤 3：接线 bufferChanged**

修改 `src/backend/contour/ZzContourBackend.cpp` 的 `EventsImpl`，追加：

```cpp
        void bufferChanged(vtbackend::ScreenType type) override
        {
            owner_.listener.onActiveBufferChanged(type == vtbackend::ScreenType::Alternate);
        }
```

并把 EventsImpl 类体内残留的「bufferChanged 在任务 5 接线」注释删除（已全部接线完毕）。

- [ ] **步骤 4：运行测试验证通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug`
预期：15/15 全绿。

- [ ] **步骤 5：Commit**

```bash
git add src/backend/contour/ZzContourBackend.cpp tests/unit/test_contour_backend.cpp
git commit -m "feat(contour): alt screen 切换事件接线与 scrollback/自动换行回归测试"
```

---

### 任务 6：DA 回写 + 多 chunk 边界 + CJK 宽字符 + M1a 全量验收

**文件：**
- 测试：`tests/unit/test_contour_backend.cpp`（追加；无实现改动预期——feed/flushReplies 已在任务 2 就绪，本任务用测试锁定行为；若失败则修实现）

- [ ] **步骤 1：编写测试**

在匿名命名空间内追加：

```cpp
// DA 请求（CSI c）→ flushReplies 后经桥收到 VT525 风格响应。
void testDeviceAttributes()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[c");
    ZZ_CHECK(events.written.empty()); // 响应先入缓冲，未 flush 不发出
    backend.flushReplies();
    ZZ_CHECK(events.written.rfind("\x1b[?65;", 0) == 0); // 前缀匹配（attrs 列表随 Settings 变）
    ZZ_CHECK(!events.written.empty() && events.written.back() == 'c');
}

// UTF-8 序列与 CSI 序列跨 feed 拆分，结果与不拆分一致。
void testChunkBoundaries()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\xe4\xb8");   // 「中」的前两个字节
    backend.feed("\xad");       // 第三个字节
    backend.feed("\x1b[3");     // CSI 拆分
    backend.feed("1m");
    backend.feed("R");
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.at(0, 0).codepoints == U"中");
    ZZ_CHECK(snap.at(0, 2).codepoints == U"R");
    ZZ_CHECK(snap.at(0, 2).foreground == (ZzColor { ZzColor::Tag::Indexed, 1 }));
}

// CJK 宽字符：首格 width=2，续格带 WideCharContinuation 标志且无内容。
void testCjkWideCell()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\xe4\xb8\xad"); // 「中」
    auto snap = backend.snapshot();
    auto const& head = snap.at(0, 0);
    ZZ_CHECK(head.codepoints == U"中");
    ZZ_CHECK(head.width == 2);
    auto const& cont = snap.at(0, 1);
    ZZ_CHECK(cont.codepoints.empty());
    ZZ_CHECK(cont.flags & ZzCellFlag::WideCharContinuation);
    // 光标停在宽字符之后（第 2 列）。
    ZZ_CHECK(snap.cursor.has_value() && snap.cursor->column == 2);
}
```

`main()` 中追加调用 `testDeviceAttributes(); testChunkBoundaries(); testCjkWideCell();`。

- [ ] **步骤 2：运行测试验证**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_contour_backend -V`
预期：PASS。若 DA 响应前缀不是 `\x1b[?65;`，到 `third_party/contour/src/vtbackend/screen/Screen.cpp:1372-1412`（sendDeviceAttributes）核对当前 Settings 下的 id（默认 VTType::VT525 对应 65），以实际为准修断言并在此记录原因。

- [ ] **步骤 3：M1a 全量验收（ON/OFF/doxygen）**

依次运行并核对：

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
```

预期：15/15 全绿（14 基线 + test_contour_backend）。

```bash
cmake -S . -B build/m1a-off-check -G Ninja -DZZTERM_WITH_CONTOUR=OFF && cmake --build build/m1a-off-check && ctest --test-dir build/m1a-off-check
```

预期：构建无 ZzTermContourBackend/test_contour_backend，ctest 13/13 全绿。

```bash
doxygen Doxyfile
```

预期：exit 0 且零 warning（新头文件注释若引入 warning，修到零为止；注意注释内禁止尖括号）。

- [ ] **步骤 4：Commit**

```bash
git add tests/unit/test_contour_backend.cpp
git commit -m "test(contour): DA 回写、多 chunk 边界与 CJK 宽字符回归测试，M1a 验收完成"
```

---

## 自检记录

- **规格覆盖度**：规格 4.1 仓库变更 → 任务 1/2；4.2 核心类 → 任务 2；4.3 PtyBridge → 任务 1；4.4 Events → 任务 2（接口）+ 4（title/bell/dirty）+ 5（bufferChanged）；4.5 快照 → 任务 2（结构）+ 3（颜色/flags）+ 4（光标）；4.6 测试 12 项 → 任务 2（ASCII/resize 基础）、3（SGR/亮色/RGB/样式）、4（光标/title/bell）、5（alt screen/scrollback/lineWrapped）、6（DA/多 chunk/CJK 宽字符）；第 6 节 DoD → 任务 6 步骤 3。规格中「OSC title」「bell」「resize 80x24→100x30」均有对应用例。
- **ZzContourEvents.cpp**：规格 4.1 标注「如需」——接口全纯虚无实现代码，不创建该文件。
- **类型一致性**：`ZzColor::Tag::{Undefined,Default,Indexed,RGB}`、`ZzCellFlag::*` 位值、`ZzContourSnapshot::at(line, column)`、`RecordingEvents` 字段名（title/titleCount/bellCount/dirtyCount/altChanges/written）在全部任务间一致；backend API（feed/resize/size/isAlternateScreen/title/historyLineCount/lineWrapped/flushReplies/snapshot）任务 2 定义、任务 4-6 仅消费。
- **延后项**：PIC/shared、ZzTerminal 接入、零拷贝视图、ExternalTransportAdapter 公开形态、输入编码——均属 M1b/M3，不在本计划。
