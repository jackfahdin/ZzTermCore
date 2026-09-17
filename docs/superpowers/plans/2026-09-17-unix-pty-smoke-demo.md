# Unix PTY + 控制台冒烟 Demo 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 实现 Unix PTY 模块 `ZzTermPty` 与控制台冒烟 Demo `ZzTermSmoke`，让 Core 第一次驱动真实 shell（bash/vim/less），并以脚本化冒烟测试接入 CTest。

**架构：** `ZzTermPty` 为纯 OS 薄 RAII 封装（openpty + fork + execvp + 错误回报管道），不依赖 ZzTermCore；`ZzTermSmoke` 用 poll 循环桥接 PTY ↔ `ZzTerminal` ↔ stdout（ANSI SGR 全屏重绘），stdin 字节透传，SIGWINCH 经自管道同步尺寸，子进程退出后 demo 以其退出码退出。

**技术栈：** C++20、POSIX PTY（`<pty.h>` openpty/TIOCSWINSZ/waitpid）、poll 事件循环、CMake（BUILD_SHARED_LIBS）、CTest。

**对应规格：** `docs/superpowers/specs/2026-09-17-unix-pty-smoke-demo-design.md`（已批准）。

**工作分支：** `pty-smoke-demo`（从 master 新建）。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `pty/unix/ZzPtyExport.h` | 新建。ZzTermPty 导出宏（仿 `include/ZzTerm/Export.h` 约定，宏名 `ZZTERM_PTY_*`）。 |
| `pty/unix/ZzPty.h` | 新建。ZzPtyConfig / ZzPty 公开头，全中文 Doxygen。 |
| `pty/unix/ZzPty.cpp` | 新建。openpty + fork + execvp 实现，含 exec 失败回报管道。 |
| `pty/CMakeLists.txt` | 新建。`ZzTermPty` target（别名 `ZzTerm::Pty`），不链接 ZzTermCore。 |
| `CMakeLists.txt` | 修改。`if(UNIX)` 下 `add_subdirectory(pty)` / `add_subdirectory(examples)`。 |
| `tests/CMakeLists.txt` | 修改。单元测试在 `TARGET ZzTermPty` 存在时同时链接 ZzTermPty；新增 `ZzTermSmokeEcho` 冒烟测试。 |
| `tests/unit/test_pty.cpp` | 新建。PTY 四项测试（cat round-trip / resize ioctl / 退出码 42 / spawn 失败）。 |
| `examples/CMakeLists.txt` | 新建。`add_subdirectory(ZzTermSmoke)`。 |
| `examples/ZzTermSmoke/CMakeLists.txt` | 新建。`ZzTermSmoke` 可执行 target。 |
| `examples/ZzTermSmoke/main.cpp` | 新建。demo 全部逻辑（参数、termios guard、poll 主循环、SGR 渲染、SIGWINCH 自管道）。 |
| `src/screen/Screen.cpp` | 修改（130-140）。restoreCursor 钳制被覆盖的顺手修复（M0 遗留 backlog）。 |
| `tests/unit/test_terminal_core.cpp` | 修改。追加 resize 后 DECRC 钳制测试。 |
| `docs/VT-Xterm-Checklist.md` | 修改。勾选 Linux PTY、bash/zsh。 |
| `docs/API.md` | 修改。新增 PTY 小节；顺手修正第 21 行失效引用 `ZzTerm/zzterm_export.h` → `ZzTerm/Export.h`。 |
| `docs/Architecture.md` | 修改。第 3 节目录树补 `examples/ZzTermSmoke/`（注明调试冒烟工具，正式 Demo 仍为 ZzTermDemo）。 |
| `Doxyfile` | 修改。`INPUT` 增加 `pty` 目录。 |

## 关键设计细节（实现前必读）

1. **exec 失败回报**：fork 之后 execvp 失败发生在子进程，父进程无法直接感知。spawn 在 fork 前建一根写端带 `FD_CLOEXEC` 的管道：子进程 exec 失败时把 errno 写入管道后 `_exit(127)`；exec 成功时 CLOEXEC 关闭写端，父进程阻塞读立即得到 EOF（0 字节）。父进程读到完整 errno → 回收子进程、关闭 master、返回 nullptr。
2. **EIO 归 EOF**：Linux 上子进程退出且 slave 引用全部关闭后，读 master 返回 -1/EIO（而非 0）。`ZzPty::read` 把 EIO 与 read==0 统一归一为 EOF（返回 0）。
3. **非 tty 场景（CTest 管道）**：demo 必须能在 stdin/stdout 为管道时工作——`isatty(STDIN_FILENO)` 为假时 TermiosGuard 不生效；TIOCGWINSZ 失败回退 80x24；stdin 读到 EOF 后停止监听（否则 poll 忙转）。冒烟测试 `ZzTermSmoke -- bash -c 'echo zz-smoke-ok'` 即在此场景下运行。
4. **demo 读 master 必须非阻塞**：poll 报告可读后要循环读到 EAGAIN 为止（一次 POLLIN 可能对应多段数据；若只读一次，剩余数据要等下一事件，EOF 场景会卡住）。spawn 后用 `fcntl(O_NONBLOCK)` 设置 masterFd。因此 `ZzPty::read` 返回 -1 时 errno 可能是 EAGAIN（暂不可读），demo 按"回到 poll"处理。
5. **退出顺序**：子进程退出（tryWait 命中）后立即非阻塞排空 master 残余输出 → 最后渲染一次 → 退出。不等待 slave 引用全部关闭（子进程的子进程可能持有 slave，等 EOF 会挂死）。
6. **raw 语义**：demo 对外层 stdin 终端 `cfmakeraw`（关 OPOST），因此渲染输出换行必须显式 `\r\n`；PTY slave 由 ZzPty 默认置 raw，交互程序（bash readline/vim/less）会自行重设 termios，不受影响。
7. **构建门槛**：PTY 与 demo 是 Unix 专属，根 CMake 用 `if(UNIX)` 包裹，保证 Windows/macOS configure 不炸。openpty 在部分平台位于 libutil（glibc ≥ 2.34 已并入 libc），用 `find_library(util)` 找到才链接。

---

### 任务 1：ZzTermPty 模块 + 单元测试

**文件：**
- 创建：`pty/unix/ZzPtyExport.h`
- 创建：`pty/unix/ZzPty.h`
- 创建：`pty/unix/ZzPty.cpp`
- 创建：`pty/CMakeLists.txt`
- 创建：`tests/unit/test_pty.cpp`
- 修改：`CMakeLists.txt`（测试块之后追加）
- 修改：`tests/CMakeLists.txt:12`

- [ ] **步骤 1：编写失败的测试**

创建 `tests/unit/test_pty.cpp`（完整内容）：

```cpp
// ZzTermPty（Unix PTY 封装）测试。
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ZzPty.h"

static int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// 带超时读取：timeoutMs 内读到数据返回字节数；超时/错误返回 -1，EOF 返回 0。
static std::ptrdiff_t readWithTimeout(ZzPty& pty, std::span<std::byte> buf,
                                      int timeoutMs = 5000)
{
    struct pollfd pfd { pty.masterFd(), POLLIN, 0 };
    const int r = ::poll(&pfd, 1, timeoutMs);
    if (r <= 0) return -1;
    return pty.read(buf);
}

// 聚合读取直到 EOF 或超时。
static std::string readAll(ZzPty& pty)
{
    std::string out;
    std::byte buf[4096];
    for (;;) {
        const std::ptrdiff_t n = readWithTimeout(pty, buf);
        if (n <= 0) break;
        out.append(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
    }
    return out;
}

static void testCatRoundTrip()
{
    ZzPtyConfig cfg;
    cfg.argv = {"/bin/cat"};
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;

    const std::string msg = "zz-pty-roundtrip\n";
    ZZ_TEST_EXPECT(pty->writeAll(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(msg.data()), msg.size())));

    std::string got;
    std::byte buf[256];
    while (got.size() < msg.size()) {
        const std::ptrdiff_t n = readWithTimeout(*pty, buf);
        ZZ_TEST_EXPECT(n > 0);
        if (n <= 0) break;
        got.append(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
    }
    ZZ_TEST_EXPECT(got == msg); // raw 模式：写入什么读回什么（无 echo/规范模式干扰）
}

static void testResize()
{
    ZzPtyConfig cfg;
    cfg.argv = {"/bin/cat"};
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;

    ZZ_TEST_EXPECT(pty->resize(100, 40));
    struct winsize ws {};
    ZZ_TEST_EXPECT(::ioctl(pty->masterFd(), TIOCGWINSZ, &ws) == 0);
    ZZ_TEST_EXPECT(ws.ws_col == 100);
    ZZ_TEST_EXPECT(ws.ws_row == 40);
}

static void testExitCode()
{
    ZzPtyConfig cfg;
    cfg.argv = {"/bin/sh", "-c", "exit 42"};
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;

    (void)readAll(*pty); // 排空输出直到 EOF（EIO 归 EOF）

    std::optional<int> code;
    for (int i = 0; i < 100 && !code; ++i) { // 最多等约 5 秒
        code = pty->tryWait();
        if (!code) ::usleep(50 * 1000);
    }
    ZZ_TEST_EXPECT(code.has_value());
    ZZ_TEST_EXPECT(code.value_or(-1) == 42);
}

static void testSpawnFailure()
{
    ZzPtyConfig cfg;
    cfg.argv = {"/nonexistent/zz-definitely-missing-binary"};
    errno = 0;
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty == nullptr);   // 返回 nullptr、不崩溃
    ZZ_TEST_EXPECT(errno != 0);       // errno 保留供诊断
}

int main()
{
    testCatRoundTrip();
    testResize();
    testExitCode();
    testSpawnFailure();
    if (g_failures == 0) std::printf("test_pty: all tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
```

- [ ] **步骤 2：搭建 CMake 目标与头文件骨架，运行测试确认失败**

创建 `pty/unix/ZzPtyExport.h`：

```cpp
#pragma once

/**
 * @file ZzPtyExport.h
 * @brief ZzTermPty 动态库导出宏定义。
 *
 * 与 include/ZzTerm/Export.h 同一约定：
 * - 静态构建（BUILD_SHARED_LIBS=OFF）定义 ZZTERM_PTY_STATIC，宏为空；
 * - 动态构建编译库本体定义 ZZTERM_PTY_BUILDING_LIBRARY（导出），使用者不定义（导入）。
 */

#if defined(ZZTERM_PTY_STATIC)
#  define ZZTERM_PTY_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#  if defined(ZZTERM_PTY_BUILDING_LIBRARY)
#    define ZZTERM_PTY_API __declspec(dllexport)
#  else
#    define ZZTERM_PTY_API __declspec(dllimport)
#  endif
#else
#  if defined(ZZTERM_PTY_BUILDING_LIBRARY)
#    define ZZTERM_PTY_API __attribute__((visibility("default")))
#  else
#    define ZZTERM_PTY_API
#  endif
#endif
```

创建 `pty/unix/ZzPty.h`（骨架：声明齐全，`spawn` 尚未实现——此时只有声明没有定义，测试链接失败即"预期的失败"）：

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ZzPtyExport.h"

#include <sys/types.h> // pid_t

/**
 * @file ZzPty.h
 * @brief ZzPty：Unix PTY 薄 RAII 封装（openpty/fork/exec/winsize/waitpid）。
 *
 * 职责（Architecture.md 第 2/3 节）：
 * - 纯 OS 封装，不依赖 ZzTermCore；事件驱动由调用方决定（demo 用 poll，
 *   未来 Qt Widget 把 masterFd() 接 QSocketNotifier，本 API 不变）；
 * - 无线程、无回调；所有方法必须在同一线程调用（非线程安全）。
 *
 * ownership：ZzPty 独占拥有 master fd 与子进程 pid；析构时 SIGHUP 子进程、
 * 关闭 fd 并回收僵尸。
 */

/**
 * @brief PTY 启动配置。
 */
struct ZzPtyConfig {
    std::vector<std::string> argv; ///< 程序与参数，argv[0] 为程序路径或名称（execvp 搜索 PATH）。
    int  cols    = 80;             ///< 初始列数（TIOCSWINSZ）。
    int  rows    = 24;             ///< 初始行数（TIOCSWINSZ）。
    bool rawMode = true;           ///< slave 置 raw（cfmakeraw），避免 PTY echo/规范模式干扰 Core 验证。
};

/**
 * @brief Unix PTY 会话（master 侧）。
 */
class ZZTERM_PTY_API ZzPty {
public:
    /**
     * @brief 启动子进程并挂到新的 PTY。
     * @param cfg 启动配置（argv 非空、cols/rows > 0，否则失败 errno = EINVAL）。
     * @return 成功返回 PTY 会话；失败返回 nullptr（含子进程 exec 失败），errno 保留供诊断。
     */
    static std::unique_ptr<ZzPty> spawn(const ZzPtyConfig& cfg);

    /// @brief 析构：SIGHUP 子进程（不退出则短暂等待后 SIGKILL）、关闭 master fd、回收僵尸。
    ~ZzPty();

    ZzPty(const ZzPty&)            = delete;
    ZzPty& operator=(const ZzPty&) = delete;

    /**
     * @brief master 侧文件描述符（供 poll/select/QSocketNotifier 使用）。
     * @return master fd（本对象存续期间有效）。
     */
    [[nodiscard]] int masterFd() const noexcept;

    /**
     * @brief 从 master 读取子进程输出。
     * @param buf 读取缓冲。
     * @return > 0 读取字节数；0 = EOF（子进程退出后 Linux 返回 EIO，归一为 EOF）；
     *         -1 = 错误，errno 保留（fd 非阻塞时可能为 EAGAIN，表示暂不可读）。
     */
    std::ptrdiff_t read(std::span<std::byte> buf) noexcept;

    /**
     * @brief 循环写入直到全部写完或出错（处理部分写入与 EINTR）。
     * @param data 待写入数据。
     * @return true 全部写完；false 出错（errno 保留）。
     */
    bool writeAll(std::span<const std::byte> data) noexcept;

    /**
     * @brief 调整 PTY 窗口尺寸（TIOCSWINSZ，触发子进程 SIGWINCH）。
     * @param cols 新列数（> 0）。
     * @param rows 新行数（> 0）。
     * @return true 成功。
     */
    bool resize(int cols, int rows) noexcept;

    /**
     * @brief 非阻塞收集子进程退出码（WNOHANG waitpid）。
     * @return 子进程仍在运行返回 std::nullopt；已退出返回退出码
     *         （被信号杀死为 128 + 信号号）。已回收后重复调用返回同一退出码。
     */
    std::optional<int> tryWait() noexcept;

private:
    ZzPty(int masterFd, pid_t childPid) noexcept;

    int   masterFd_ = -1;   ///< master fd。
    pid_t childPid_ = -1;   ///< 子进程 pid。
    bool  reaped_   = false; ///< 是否已 waitpid 回收。
    int   exitCode_ = 0;    ///< 已回收时的退出码（供重复 tryWait 返回）。
};
```

创建 `pty/CMakeLists.txt`：

```cmake
# ZzTermPty：Unix PTY 薄封装（纯 OS 模块，不依赖 ZzTermCore）。
add_library(ZzTermPty unix/ZzPty.cpp unix/ZzPty.h unix/ZzPtyExport.h)
add_library(ZzTerm::Pty ALIAS ZzTermPty)

target_compile_features(ZzTermPty PUBLIC cxx_std_20)

# 导出宏约定与 ZzTermCore 一致（见 unix/ZzPtyExport.h）。
target_compile_definitions(ZzTermPty PRIVATE ZZTERM_PTY_BUILDING_LIBRARY)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(ZzTermPty PUBLIC ZZTERM_PTY_STATIC)
endif()

set_target_properties(ZzTermPty PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# 公开头以 "ZzPty.h" 形式引用，include 根即 unix/ 目录。
target_include_directories(ZzTermPty
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/unix>)

# openpty 在部分平台位于 libutil（glibc >= 2.34 已并入 libc），找到才链接。
find_library(ZZTERM_UTIL_LIBRARY util)
if(ZZTERM_UTIL_LIBRARY)
    target_link_libraries(ZzTermPty PRIVATE ${ZZTERM_UTIL_LIBRARY})
endif()
```

修改根 `CMakeLists.txt`，在测试块（第 72-75 行 `if(ZZTERM_BUILD_TESTS) ... endif()`）之后追加：

```cmake
# ---------------------------------------------------------------------------
# PTY 与示例（仅 Unix；macOS 头文件差异 M5 处理，Windows ConPTY 里程碑靠后）
# ---------------------------------------------------------------------------
if(UNIX)
    add_subdirectory(pty)
    add_subdirectory(examples)
endif()
```

修改 `tests/CMakeLists.txt` 第 12 行，让单元测试在 PTY 目标存在时同时链接它：

```cmake
    add_executable(${test_name} "${test_source}")
    target_link_libraries(${test_name} PRIVATE ZzTermCore)
    if(TARGET ZzTermPty)
        target_link_libraries(${test_name} PRIVATE ZzTermPty)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
```

创建占位 `pty/unix/ZzPty.cpp`（仅有 include，无函数定义）：

```cpp
#include "ZzPty.h"
```

再创建最小 `examples/CMakeLists.txt`（暂为空注释，任务 2 填充）：

```cmake
# 示例程序目录（任务：ZzTermSmoke 控制台冒烟 Demo）。
```

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug 2>&1 | tail -20`
预期：构建失败，链接错误 `undefined reference to 'ZzPty::spawn(...)'`（测试存在但实现缺失，即"预期的失败"）。

- [ ] **步骤 3：实现 ZzPty.cpp**

用以下完整实现替换 `pty/unix/ZzPty.cpp`：

```cpp
#include "ZzPty.h"

#include <cerrno>
#include <csignal>
#include <vector>

#include <fcntl.h>
#include <pty.h> // openpty（Linux；macOS 为 <util.h>，M5 处理）
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace {

/// 忽略 EINTR 的 close。
void closeNoIntr(int fd) noexcept
{
    if (fd >= 0) {
        while (::close(fd) != 0 && errno == EINTR) {
        }
    }
}

} // namespace

ZzPty::ZzPty(int masterFd, pid_t childPid) noexcept
    : masterFd_(masterFd), childPid_(childPid)
{
}

std::unique_ptr<ZzPty> ZzPty::spawn(const ZzPtyConfig& cfg)
{
    if (cfg.argv.empty() || cfg.argv.front().empty() || cfg.cols <= 0 || cfg.rows <= 0) {
        errno = EINVAL;
        return nullptr;
    }

    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(cfg.cols);
    ws.ws_row = static_cast<unsigned short>(cfg.rows);

    int master = -1;
    int slave  = -1;
    if (::openpty(&master, &slave, nullptr, nullptr, &ws) != 0) {
        return nullptr; // errno 由 openpty 保留
    }

    if (cfg.rawMode) {
        struct termios tio {};
        if (::tcgetattr(slave, &tio) == 0) {
            ::cfmakeraw(&tio);
            (void)::tcsetattr(slave, TCSANOW, &tio);
        }
    }

    // exec 参数在 fork 前构建：子进程只做 async-signal-safe 操作。
    std::vector<char*> args;
    args.reserve(cfg.argv.size() + 1);
    for (const std::string& s : cfg.argv) {
        args.push_back(const_cast<char*>(s.c_str()));
    }
    args.push_back(nullptr);

    // exec 失败回报管道：写端 CLOEXEC——exec 成功时自动关闭，父进程读到 EOF；
    // exec 失败时子进程写入 errno，父进程读到后按失败处理。
    int errPipe[2] = {-1, -1};
    if (::pipe(errPipe) != 0) {
        const int saved = errno;
        closeNoIntr(master);
        closeNoIntr(slave);
        errno = saved;
        return nullptr;
    }
    {
        const int flags = ::fcntl(errPipe[1], F_GETFD);
        (void)::fcntl(errPipe[1], F_SETFD, flags | FD_CLOEXEC);
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const int saved = errno;
        closeNoIntr(master);
        closeNoIntr(slave);
        closeNoIntr(errPipe[0]);
        closeNoIntr(errPipe[1]);
        errno = saved;
        return nullptr;
    }

    if (pid == 0) {
        // ---- 子进程：仅 async-signal-safe 调用 ----
        ::close(master);
        ::close(errPipe[0]);
        (void)::setsid();
        (void)::ioctl(slave, TIOCSCTTY, 0); // 成为控制终端
        (void)::dup2(slave, STDIN_FILENO);
        (void)::dup2(slave, STDOUT_FILENO);
        (void)::dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) {
            ::close(slave);
        }
        ::execvp(args[0], args.data());
        const int execErrno = errno;
        (void)!::write(errPipe[1], &execErrno, sizeof(execErrno));
        ::_exit(127);
    }

    // ---- 父进程 ----
    ::close(slave);
    ::close(errPipe[1]);

    int childErrno = 0;
    // 阻塞读：exec 成功时写端 CLOEXEC 关闭，立即返回 0（EOF）；
    // 失败时读到子进程写入的完整 errno（<= PIPE_BUF 单次原子写）。
    const ssize_t n = ::read(errPipe[0], &childErrno, sizeof(childErrno));
    ::close(errPipe[0]);
    if (n == static_cast<ssize_t>(sizeof(childErrno))) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        ::close(master);
        errno = childErrno;
        return nullptr;
    }

    return std::unique_ptr<ZzPty>(new ZzPty(master, pid));
}

ZzPty::~ZzPty()
{
    if (childPid_ > 0 && !reaped_) {
        ::kill(childPid_, SIGHUP);
        // 短暂等待子进程响应 SIGHUP；不退出则 SIGKILL，保证不遗留僵尸。
        for (int i = 0; i < 10; ++i) {
            int status = 0;
            if (::waitpid(childPid_, &status, WNOHANG) == childPid_) {
                break;
            }
            if (i == 9) {
                ::kill(childPid_, SIGKILL);
                while (::waitpid(childPid_, &status, 0) < 0 && errno == EINTR) {
                }
                break;
            }
            ::usleep(5 * 1000);
        }
    }
    closeNoIntr(masterFd_);
}

int ZzPty::masterFd() const noexcept
{
    return masterFd_;
}

std::ptrdiff_t ZzPty::read(std::span<std::byte> buf) noexcept
{
    for (;;) {
        const ssize_t n = ::read(masterFd_, buf.data(), buf.size());
        if (n > 0) return static_cast<std::ptrdiff_t>(n);
        if (n == 0) return 0;       // EOF
        if (errno == EINTR) continue;
        if (errno == EIO) return 0; // Linux：子进程退出且 slave 引用全关，归一为 EOF
        return -1;
    }
}

bool ZzPty::writeAll(std::span<const std::byte> data) noexcept
{
    std::size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::write(masterFd_, data.data() + off, data.size() - off);
        if (n > 0) {
            off += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
    return true;
}

bool ZzPty::resize(int cols, int rows) noexcept
{
    if (cols <= 0 || rows <= 0) return false;
    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    return ::ioctl(masterFd_, TIOCSWINSZ, &ws) == 0;
}

std::optional<int> ZzPty::tryWait() noexcept
{
    if (reaped_) return exitCode_;
    int status = 0;
    const pid_t r = ::waitpid(childPid_, &status, WNOHANG);
    if (r == 0) return std::nullopt;          // 仍在运行
    if (r < 0) {
        if (errno == ECHILD) {                // 已被外部回收：按已知退出码返回
            reaped_ = true;
            return exitCode_;
        }
        return std::nullopt;
    }
    reaped_ = true;
    if (WIFEXITED(status)) {
        exitCode_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exitCode_ = 128 + WTERMSIG(status);
    } else {
        exitCode_ = 1;
    }
    return exitCode_;
}
```

- [ ] **步骤 4：运行测试确认通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_pty --output-on-failure`
预期：PASS（1/1）。再运行 `ctest --preset linux-gcc-debug` 确认全部 10 个测试通过（无回归）。

- [ ] **步骤 5：Commit**

```bash
git add pty/ CMakeLists.txt tests/CMakeLists.txt tests/unit/test_pty.cpp
git commit -m "feat(pty): Unix PTY 薄封装 ZzTermPty（openpty/exec 回报/winsize/waitpid）"
```

---

### 任务 2：ZzTermSmoke 控制台冒烟 Demo + 冒烟 CTest

**文件：**
- 创建：`examples/ZzTermSmoke/main.cpp`
- 创建：`examples/ZzTermSmoke/CMakeLists.txt`
- 修改：`examples/CMakeLists.txt`
- 修改：`tests/CMakeLists.txt`（文件末尾追加冒烟测试）

- [ ] **步骤 1：实现 demo 主程序**

创建 `examples/ZzTermSmoke/main.cpp`（完整内容）：

```cpp
// ZzTermSmoke：控制台冒烟 Demo（调试工具，正式 GUI Demo 为未来的 ZzTermDemo）。
// 用法：ZzTermSmoke [-- 命令...]，默认 bash。子进程退出后以子进程退出码退出。
//
// 结构：PTY 输出 -> ZzTerminal -> RenderView 全屏重绘到 stdout（ANSI SGR）；
//       stdin 字节透传 -> PTY；SIGWINCH 自管道同步 PTY 与 Terminal 尺寸。
// 非 tty 场景（CTest 管道）：termios guard 不生效、尺寸回退 80x24、
// stdin EOF 后停止监听，仍可完整跑通（脚本化冒烟依赖此行为）。
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "ZzTerm/Terminal.h"
#include "ZzPty.h"

namespace {

/// SIGWINCH 自管道：信号处理器只写字节，poll 循环消费（async-signal-safe）。
int g_winchPipe[2] = {-1, -1};

void onSigWinch(int)
{
    if (g_winchPipe[1] >= 0) {
        const char b = 1;
        (void)!::write(g_winchPipe[1], &b, 1);
    }
}

/// RAII：构造时把 stdin 终端置 raw，析构恢复原始终端属性；stdin 非 tty 时不生效。
class TermiosGuard {
public:
    TermiosGuard() noexcept
    {
        if (::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &saved_) == 0) {
            struct termios raw = saved_;
            ::cfmakeraw(&raw);
            if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                active_ = true;
            }
        }
    }
    ~TermiosGuard()
    {
        if (active_) {
            (void)::tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        }
    }
    TermiosGuard(const TermiosGuard&)            = delete;
    TermiosGuard& operator=(const TermiosGuard&) = delete;

private:
    struct termios saved_ {};
    bool active_ = false;
};

/// 查询当前终端尺寸；失败回退 80x24（含 stdin/stdout 为管道的 CTest 场景）。
ZzSize queryTerminalSize() noexcept
{
    struct winsize ws {};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return ZzSize{static_cast<int>(ws.ws_col), static_cast<int>(ws.ws_row)};
    }
    if (::ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return ZzSize{static_cast<int>(ws.ws_col), static_cast<int>(ws.ws_row)};
    }
    return ZzSize{80, 24};
}

/// 尽力写满 fd（EINTR 重试，错误静默丢弃——渲染输出不可失败退出）。
void writeFd(int fd, std::string_view data) noexcept
{
    while (!data.empty()) {
        const ssize_t n = ::write(fd, data.data(), data.size());
        if (n > 0) {
            data.remove_prefix(static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
}

/// char32_t -> UTF-8 追加编码（渲染侧最小实现；码位合法性由 Core 保证）。
void appendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/// ZzColor -> SGR 参数片段（不含前后缀），isFg 区分前景/背景。
void appendColorSgr(std::string& out, ZzColor color, bool isFg)
{
    if (color.isDefault()) {
        out += isFg ? ";39" : ";49";
        return;
    }
    if (color.kind() == ZzColor::Kind::Indexed) {
        const int idx = color.index();
        if (idx < 8) {                    // ANSI 8 色：30-37 / 40-47
            out += isFg ? ";3" : ";4";
            out += static_cast<char>('0' + idx);
        } else if (idx < 16) {            // bright：90-97 / 100-107
            out += isFg ? ";9" : ";10";
            out += static_cast<char>('0' + (idx - 8));
        } else {                          // 256 色：38;5;n / 48;5;n
            out += isFg ? ";38;5;" : ";48;5;";
            out += std::to_string(idx);
        }
        return;
    }
    // RGB TrueColor：38;2;r;g;b / 48;2;r;g;b
    out += isFg ? ";38;2;" : ";48;2;";
    out += std::to_string(color.red());
    out += ';';
    out += std::to_string(color.green());
    out += ';';
    out += std::to_string(color.blue());
}

/// 当前渲染画笔（用于只在样式变化时发 SGR）。
struct PenStyle {
    ZzColor           fg = ZzColor::Default();
    ZzColor           bg = ZzColor::Default();
    ZzCellAttributes  attrs;

    friend bool operator==(const PenStyle&, const PenStyle&) = default;
};

/// 完整 SGR 序列：先复位再按需置位（只在样式变化时调用，代价可接受）。
void appendStyleSgr(std::string& out, const PenStyle& s)
{
    out += "\x1b[0";
    if (s.attrs.bold())                              out += ";1";
    if (s.attrs.faint())                             out += ";2";
    if (s.attrs.italic())                            out += ";3";
    if (s.attrs.underline() != ZzUnderlineStyle::None) out += ";4";
    if (s.attrs.blink() != ZzBlinkStyle::None)       out += ";5";
    if (s.attrs.inverse())                           out += ";7";
    if (s.attrs.invisible())                         out += ";8";
    if (s.attrs.strikethrough())                     out += ";9";
    appendColorSgr(out, s.fg, true);
    appendColorSgr(out, s.bg, false);
    out += 'm';
}

/// 全屏重绘当前屏幕（不渲染 scrollback；外层终端已 raw，换行显式 \r\n）。
void renderScreen(const ZzTerminal& term, std::string& out)
{
    const ZzRenderView& view = term.renderView();
    const ZzSize size = view.size();
    out += "\x1b[?25l"; // 重绘期间隐藏光标，减少闪烁
    out += "\x1b[H";    // CUP 回原点，全屏覆盖
    PenStyle pen;
    for (int row = 0; row < size.rows; ++row) {
        const ZzLine& line = view.lineAt(row);
        for (int col = 0; col < size.cols; ++col) {
            const ZzCell& cell = line.cellAt(col);
            if (cell.width() == ZzCellWidth::WideContinuation) {
                continue; // 宽字符续格不输出（首格已占两列）
            }
            const PenStyle want{cell.foreground(), cell.background(), cell.attributes()};
            if (!(want == pen)) {
                appendStyleSgr(out, want);
                pen = want;
            }
            if (cell.isCluster()) {
                out += line.clusterText(cell.clusterIndex());
            } else if (cell.codePoint() != 0) {
                appendUtf8(out, cell.codePoint());
            } else {
                out += ' ';
            }
        }
        if (row + 1 < size.rows) {
            out += "\r\n";
        }
    }
    const ZzCursorState cursor = view.cursor();
    if (cursor.visible) {
        char cup[32];
        std::snprintf(cup, sizeof(cup), "\x1b[%d;%dH",
                      cursor.position.row + 1, cursor.position.col + 1);
        out += "\x1b[?25h";
        out += cup;
    }
}

int run(const std::vector<std::string>& command)
{
    const ZzSize termSize = queryTerminalSize();

    TermiosGuard termiosGuard; // 此后所有退出路径经 RAII 恢复原始终端

    ZzPtyConfig cfg;
    cfg.argv = command;
    cfg.cols = termSize.cols;
    cfg.rows = termSize.rows;
    auto pty = ZzPty::spawn(cfg);
    if (!pty) {
        std::fprintf(stderr, "ZzTermSmoke: spawn 失败：%s\n", std::strerror(errno));
        return 1;
    }

    // 主循环排干式读取：master 置非阻塞，poll 命中后读到 EAGAIN 为止。
    {
        const int flags = ::fcntl(pty->masterFd(), F_GETFL);
        (void)::fcntl(pty->masterFd(), F_SETFL, flags | O_NONBLOCK);
    }

    ZzTerminal term(termSize.cols, termSize.rows, 1000);

    if (::pipe(g_winchPipe) != 0) {
        std::fprintf(stderr, "ZzTermSmoke: pipe 失败：%s\n", std::strerror(errno));
        return 1;
    }
    struct sigaction sa {};
    sa.sa_handler = &onSigWinch;
    ::sigemptyset(&sa.sa_mask);
    (void)::sigaction(SIGWINCH, &sa, nullptr);

    bool watchStdin = true; // stdin EOF（管道场景）后停止监听，避免 poll 忙转
    std::optional<int> childExit;

    std::string renderBuf;
    renderBuf.reserve(64 * 1024);
    std::byte ioBuf[16 * 1024];

    for (;;) {
        struct pollfd fds[3];
        fds[0] = { pty->masterFd(), POLLIN, 0 };
        fds[1] = { STDIN_FILENO, static_cast<short>(watchStdin ? POLLIN : 0), 0 };
        fds[2] = { g_winchPipe[0], POLLIN, 0 };
        const int ready = ::poll(fds, 3, -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break; // poll 永久错误：走退出路径
        }

        // stdin -> PTY 字节透传
        if (fds[1].revents & POLLIN) {
            const ssize_t n = ::read(STDIN_FILENO, ioBuf, sizeof(ioBuf));
            if (n > 0) {
                (void)pty->writeAll(std::span<const std::byte>(
                    ioBuf, static_cast<std::size_t>(n)));
            } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
                watchStdin = false;
            }
        }
        if (fds[1].revents & (POLLHUP | POLLERR)) {
            watchStdin = false;
        }

        // SIGWINCH -> 重读尺寸，同步 PTY 与 Terminal
        if (fds[2].revents & POLLIN) {
            char drain[64];
            while (::read(g_winchPipe[0], drain, sizeof(drain)) > 0) {
            }
            const ZzSize s = queryTerminalSize();
            (void)pty->resize(s.cols, s.rows);
            (void)term.resize(s.cols, s.rows);
        }

        // PTY -> Core -> 渲染
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            for (;;) {
                const std::ptrdiff_t n = pty->read(ioBuf);
                if (n > 0) {
                    (void)term.feed(std::span<const std::byte>(
                        ioBuf, static_cast<std::size_t>(n)));
                    continue;
                }
                break; // 0 = EOF 或 -1（EAGAIN 暂不可读）：回到 poll
            }
            renderBuf.clear();
            renderScreen(term, renderBuf);
            writeFd(STDOUT_FILENO, renderBuf);
        }

        // 子进程退出收集
        if (!childExit) {
            childExit = pty->tryWait();
        }
        if (childExit) {
            // 子进程已退出：非阻塞排空残余输出后退出
            //（不等待 slave 引用全部关闭——子进程的子进程可能持有 slave）。
            for (;;) {
                const std::ptrdiff_t n = pty->read(ioBuf);
                if (n <= 0) break;
                (void)term.feed(std::span<const std::byte>(
                    ioBuf, static_cast<std::size_t>(n)));
            }
            renderBuf.clear();
            renderScreen(term, renderBuf);
            writeFd(STDOUT_FILENO, renderBuf);
            break;
        }
    }

    if (!childExit) {
        childExit = pty->tryWait();
    }
    ::close(g_winchPipe[0]);
    ::close(g_winchPipe[1]);
    g_winchPipe[0] = g_winchPipe[1] = -1;
    return childExit.value_or(1);
}

} // namespace

int main(int argc, char* argv[])
{
    std::vector<std::string> command;
    if (argc > 1) {
        if (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h") {
            std::fprintf(stderr, "用法：ZzTermSmoke [-- 命令...]（默认 bash）\n");
            return 0;
        }
        if (std::string_view(argv[1]) != "--") {
            std::fprintf(stderr, "ZzTermSmoke: 未知参数 '%s'（用法：ZzTermSmoke [-- 命令...]）\n",
                         argv[1]);
            return 2;
        }
        for (int i = 2; i < argc; ++i) {
            command.emplace_back(argv[i]);
        }
    }
    if (command.empty()) {
        command.emplace_back("bash");
    }
    return run(command);
}
```

创建 `examples/ZzTermSmoke/CMakeLists.txt`：

```cmake
# ZzTermSmoke：控制台冒烟 Demo（调试工具；正式 GUI Demo 为 ZzTermDemo）。
add_executable(ZzTermSmoke main.cpp)
target_link_libraries(ZzTermSmoke PRIVATE ZzTermCore ZzTermPty)
target_compile_features(ZzTermSmoke PRIVATE cxx_std_20)
```

修改 `examples/CMakeLists.txt` 为：

```cmake
# 示例程序目录。
add_subdirectory(ZzTermSmoke)
```

- [ ] **步骤 2：接入冒烟 CTest**

在 `tests/CMakeLists.txt` 文件末尾追加：

```cmake
# 脚本化冒烟：PTY 驱动 Core 全链路——真实 bash 输出经 ZzTerminal 渲染回 stdout，
# 断言标记串出现且以子进程退出码 0 退出（仅 Unix 构建存在 demo 目标时）。
if(TARGET ZzTermSmoke)
    add_test(NAME ZzTermSmokeEcho
        COMMAND ZzTermSmoke -- bash -c "echo zz-smoke-ok")
    set_tests_properties(ZzTermSmokeEcho PROPERTIES
        PASS_REGULAR_EXPRESSION "zz-smoke-ok"
        TIMEOUT 30)
endif()
```

- [ ] **步骤 3：运行冒烟测试确认通过**

运行：`cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R ZzTermSmokeEcho --output-on-failure`
预期：PASS，输出含 `zz-smoke-ok`，退出码 0。

- [ ] **步骤 4：全量测试无回归 + 人工验证**

运行：`ctest --preset linux-gcc-debug`
预期：11/11 全部通过。

人工验证（由用户在本机终端执行，记录结果）：

```bash
./build/linux-gcc-debug/examples/ZzTermSmoke/ZzTermSmoke
# 在 demo 内依次验证：ls --color 着色正确；vim 打开/编辑/退出显示正常；
# less 翻页正常；Ctrl+C 中断当前命令回到提示符；exit 后 demo 干净退出。
```

- [ ] **步骤 5：Commit**

```bash
git add examples/ tests/CMakeLists.txt
git commit -m "feat(examples): ZzTermSmoke 控制台冒烟 Demo（PTY 驱动 Core 全链路 + CTest 冒烟）"
```

---

### 任务 3：顺手修复 restoreCursor 钳制被覆盖（M0 遗留 backlog）

**文件：**
- 修改：`src/screen/Screen.cpp:130-140`
- 修改：`tests/unit/test_terminal_core.cpp`（main 前追加测试函数 + main 内登记调用）

背景：`ZzScreen::restoreCursor` 先 `setCursorPosition(savedCursor_.position)`（钳制到当前网格），随后 `buf.cursor = savedCursor_` 又用未钳制的保存值整体覆盖——resize 缩小后恢复的光标可能越界。

- [ ] **步骤 1：编写失败的测试**

在 `tests/unit/test_terminal_core.cpp` 的 `main()` 之前追加：

```cpp
static void testRestoreCursorClampedAfterResize()
{
    ZzTerminal term(10, 6, 100);
    feedStr(term, "\x1b[6;8H"); // CUP：row 5、col 7（0 起始）
    feedStr(term, "\x1b" "7");  // DECSC 保存光标
    term.resize(4, 4);          // 缩小网格
    feedStr(term, "\x1b" "8");  // DECRC 恢复：位置必须钳制到新网格内
    ZZ_TEST_EXPECT(term.cursor().position.row == 3);
    ZZ_TEST_EXPECT(term.cursor().position.col == 3);
}
```

并在 `main()` 中（其他测试调用旁）登记 `testRestoreCursorClampedAfterResize();`。

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug -R test_terminal_core --output-on-failure`
预期：FAIL，`cursor().position.row == 3` 不成立（实际为越界的 5）。

- [ ] **步骤 2：修复 restoreCursor**

修改 `src/screen/Screen.cpp` 的 `ZzScreen::restoreCursor()`——把整体赋值移到钳制之前：

```cpp
void ZzScreen::restoreCursor() noexcept
{
    if (!hasSavedCursor_) {
        setCursorPosition(ZzPosition{0, 0});
        return;
    }
    Buffer& buf = active_ == ZzScreenBuffer::Primary ? primary_ : alternate_;
    buf.cursor = savedCursor_;
    setCursorPosition(savedCursor_.position); // 钳制，防止 resize 后越界
    buf.wrapPending = savedWrapPending_;      // 最后恢复（setCursorPosition 会清 wrapPending）
}
```

注：执行时发现 `setCursorPosition` 无条件清 `wrapPending`，若先恢复
`savedWrapPending_` 再钳制会导致 `test_wrap_pending` 的 `testSavedWithCursor`
回归——以上顺序（赋值 → 钳制 → 恢复 wrapPending）为已实施并验证的正确版本。

- [ ] **步骤 3：运行测试确认通过**

运行：`cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug --output-on-failure`
预期：全部 11 个测试 PASS（含新测试，且无回归）。

- [ ] **步骤 4：Commit**

```bash
git add src/screen/Screen.cpp tests/unit/test_terminal_core.cpp
git commit -m "fix(screen): 修复 restoreCursor 钳制被整体赋值覆盖（resize 后光标越界）"
```

---

### 任务 4：文档同步 + 全量验证

**文件：**
- 修改：`docs/VT-Xterm-Checklist.md:144,147`
- 修改：`docs/API.md`（新增 PTY 小节 + 修正第 21 行失效引用）
- 修改：`docs/Architecture.md`（第 3 节目录树）
- 修改：`Doxyfile:9`

- [ ] **步骤 1：勾选 Checklist**

修改 `docs/VT-Xterm-Checklist.md` 的 PTY / Demo 小节：`-   [ ] Linux PTY` → `-   [x] Linux PTY`；`-   [ ] bash/zsh` → `-   [x] bash/zsh`。

- [ ] **步骤 2：更新 API.md**

在 `docs/API.md` 的 `### Input Encoder` 小节之后插入：

```markdown
### PTY（ZzTermPty，Unix）

- 头文件 `ZzPty.h`（target `ZzTermPty`，纯 OS 封装，不依赖 ZzTermCore；
  仅 Unix 构建，macOS 头文件差异 M5 处理，Windows ConPTY 里程碑靠后）。
- `ZzPty::spawn(ZzPtyConfig)` 失败返回 nullptr，errno 保留（含子进程 exec
  失败）；`masterFd()` 供 poll/select/QSocketNotifier 事件驱动。
- `read` 返回 0 表示 EOF（Linux EIO 归一）；`writeAll` 循环写完或出错；
  `resize` 即 TIOCSWINSZ；`tryWait` 非阻塞收集退出码（信号杀死为
  128 + 信号号）。析构 SIGHUP（必要时 SIGKILL）子进程并回收僵尸。
- 调试工具 `ZzTermSmoke`（`examples/ZzTermSmoke`）：PTY -> ZzTerminal ->
  stdout 全屏重绘的控制台冒烟 Demo，子进程退出即以其退出码退出。
```

顺手修正第 21 行失效引用：`ZzTerm/zzterm_export.h` → `ZzTerm/Export.h`。

- [ ] **步骤 3：更新 Architecture.md 目录树**

修改 `docs/Architecture.md` 第 3 节目录树：在 `examples/ZzTermDemo/` 行上方插入 `├── examples/ZzTermSmoke/  # 控制台冒烟 Demo（调试工具）`，保持树形注释对齐（正式 GUI Demo 仍为 ZzTermDemo，Targets 行不变）。

- [ ] **步骤 4：Doxyfile 覆盖 pty 目录**

修改 `Doxyfile` 第 9 行：`INPUT = include docs` → `INPUT = include docs pty`。

- [ ] **步骤 5：全量验证（三件套全绿）**

依次运行：

```bash
cmake --preset linux-gcc-debug && cmake --build --preset linux-gcc-debug && ctest --preset linux-gcc-debug
cmake -S . -B build/shared-check -G Ninja -DBUILD_SHARED_LIBS=ON && cmake --build build/shared-check && (cd build/shared-check && ctest --output-on-failure)
doxygen Doxyfile
```

预期：静态构建 11/11 测试通过；动态构建 11/11 测试通过；doxygen exit 0 且零 warning（新增公开头 Doxygen 必须完整）。

- [ ] **步骤 6：Commit**

```bash
git add docs/ Doxyfile
git commit -m "docs(pty): Checklist 勾选 Linux PTY/bash、API.md 补 PTY 章节、Doxygen 覆盖 pty/"
```

---

## 自检记录

- **规格覆盖度**：规格 §2（ZzPty API、模块划分）→ 任务 1；§3（demo 结构、参数、主循环、渲染、退出）→ 任务 2；§4（错误处理）→ 任务 1（EIO/EINTR/部分写入/spawn 失败）+ 任务 2（RAII guard、spawn 失败退出码 1）；§5（四项单测、冒烟 CTest、人工验证、Checklist、API.md、三件套全绿）→ 任务 1/2/4。backlog 顺手项（restoreCursor）→ 任务 3。
- **占位符扫描**：所有代码步骤含完整代码块，无 TODO/待定。
- **类型一致性**：`ZzPty::read` 返回 `std::ptrdiff_t`（0=EOF / -1=错误）在任务 1 头文件、实现、测试与任务 2 demo 中一致；`PenStyle`/`renderScreen`/`queryTerminalSize` 等 demo 内符号仅在任务 2 单文件内定义与使用；Core API（`ZzTerminal(cols,rows,scrollback)`、`feed`、`renderView().lineAt(row).cellAt(col)`、`ZzCell::codePoint/foreground/background/attributes/isCluster/clusterIndex/width`、`ZzColor::isDefault/kind/index/red/green/blue`、`ZzCellAttributes::bold/faint/italic/underline/blink/inverse/invisible/strikethrough`、`ZzCursorState::position/visible`、`ZzSize{cols,rows}`）均已对照 `include/ZzTerm/*.h` 核实。
