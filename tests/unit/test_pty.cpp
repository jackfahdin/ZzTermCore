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
