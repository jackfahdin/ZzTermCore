// ZzTermPty（PTY 薄封装）测试：Unix openpty / Windows ConPTY 双平台。
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

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
#if defined(_WIN32)
    // Windows：ZzPty::read 内部 PeekNamedPipe 判定——无数据且子进程存活返回 -1
    //（语义等价 Unix EAGAIN），子进程退出且排空归一 EOF；故直接轮询 read + Sleep。
    const DWORD deadline = ::GetTickCount() + static_cast<DWORD>(timeoutMs);
    for (;;) {
        const std::ptrdiff_t n = pty.read(buf);
        if (n >= 0) return n;
        if (static_cast<int>(::GetTickCount() - deadline) >= 0) return -1;
        ::Sleep(10);
    }
#else
    struct pollfd pfd { pty.masterFd(), POLLIN, 0 };
    const int r = ::poll(&pfd, 1, timeoutMs);
    if (r <= 0) return -1;
    return pty.read(buf);
#endif
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
#if defined(_WIN32)
    // Windows：cmd 交互会话——写入 echo 与 exit 复合命令，读回 marker 且
    // 退出码 43 即完成 写入->执行->输出->读取 全链路往返（退出码同时证明
    // 输入确实送达 cmd）。findstr 方案三轮实证不可行：cmd 元字符层吞裸
    // caret（R1 FINDSTR: Bad command line）、强制引用后 ^ 转义反斜杠失真
    //（R2）、findstr 在父进程重定向句柄泄漏下读管道 stdin 即 EOF 退出（R3）。
    cfg.argv = {"cmd.exe"};
    const std::string msg = "echo zz-pty-roundtrip & exit 43\r";
#else
    cfg.argv = {"/bin/cat"};
    const std::string msg = "zz-pty-roundtrip\n";
#endif
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;

    std::string got;
    std::byte buf[256];
    ZZ_TEST_EXPECT(pty->writeAll(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(msg.data()), msg.size())));

#if defined(_WIN32)
    // 读取终止条件只有"子进程退出且管道排空"（ZzPty::read 归一的 EOF）——
    // 就绪标记（标题依赖）与固定静默窗口均已移除：输入在 spawn 后即写入
    //（管道缓冲持有，cmd 就绪后自然消费，无启动竞争）；静默超时在 runner
    // 负载下会误判流结束（flake 根因，根治）。30s 全局截止仅作兜底，
    // 异常形态走断言失败而非挂死。
    const DWORD deadline = ::GetTickCount() + 30000;
    std::optional<int> code;
    bool              eof = false;
    while (!eof || !code) {
        const std::ptrdiff_t n = readWithTimeout(*pty, buf, 1000);
        if (n > 0) {
            got.append(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
        } else if (n == 0) {
            eof = true;
        }
        if (!code)
            code = pty->tryWait();
        if (static_cast<int>(::GetTickCount() - deadline) >= 0)
            break; // 兜底
    }
    ZZ_TEST_EXPECT(got.find("zz-pty-roundtrip") != std::string::npos);
    ZZ_TEST_EXPECT(code.value_or(-1) == 43);
#else
    // 退出条件：读够写入长度（raw 模式无横幅干扰）。
    while (got.size() < msg.size()) {
        const std::ptrdiff_t n = readWithTimeout(*pty, buf);
        ZZ_TEST_EXPECT(n > 0);
        if (n <= 0) break;
        got.append(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
    }
    ZZ_TEST_EXPECT(got == msg); // raw 模式：写入什么读回什么（无 echo/规范模式干扰）
#endif
}

static void testResize()
{
    ZzPtyConfig cfg;
#if defined(_WIN32)
    cfg.argv = {"cmd.exe"};
#else
    cfg.argv = {"/bin/cat"};
#endif
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;

    ZZ_TEST_EXPECT(pty->resize(100, 40));
#if defined(_WIN32)
    // ConPTY 无尺寸回读 API：仅断言 resize 返回 true。
#else
    struct winsize ws {};
    ZZ_TEST_EXPECT(::ioctl(pty->masterFd(), TIOCGWINSZ, &ws) == 0);
    ZZ_TEST_EXPECT(ws.ws_col == 100);
    ZZ_TEST_EXPECT(ws.ws_row == 40);
#endif
}

static void testExitCode()
{
    ZzPtyConfig cfg;
#if defined(_WIN32)
    cfg.argv = {"cmd.exe", "/c", "exit 42"};
#else
    cfg.argv = {"/bin/sh", "-c", "exit 42"};
#endif
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;

    (void)readAll(*pty); // 排空输出直到 EOF（EIO 归 EOF）

    std::optional<int> code;
    for (int i = 0; i < 100 && !code; ++i) { // 最多等约 5 秒
        code = pty->tryWait();
#if defined(_WIN32)
        if (!code) ::Sleep(50);
#else
        if (!code) ::usleep(50 * 1000);
#endif
    }
    ZZ_TEST_EXPECT(code.has_value());
    ZZ_TEST_EXPECT(code.value_or(-1) == 42);
}

static void testSpawnFailure()
{
    ZzPtyConfig cfg;
    cfg.argv = {"/nonexistent/zz-definitely-missing-binary"};
#if defined(_WIN32)
    ::SetLastError(0);
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty == nullptr);        // 返回 nullptr、不崩溃
    ZZ_TEST_EXPECT(::GetLastError() != 0); // GetLastError 保留供诊断
#else
    errno = 0;
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty == nullptr);   // 返回 nullptr、不崩溃
    ZZ_TEST_EXPECT(errno != 0);       // errno 保留供诊断
#endif
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
