// M18：ZzPtyConfig cwd/env 扩展测试（双平台）。
// Unix：pwd/printenv；Windows：cmd.exe /c cd 与 echo %VAR%。
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <limits.h>
#include <poll.h>
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

// 聚合读取直到 EOF（含超时兜底）：与 test_pty.cpp 同型小工具。
static std::string readAll(ZzPty& pty)
{
    std::string out;
    std::byte buf[4096];
#if defined(_WIN32)
    const DWORD deadline = ::GetTickCount() + 5000;
    for (;;) {
        const std::ptrdiff_t n = pty.read(std::span<std::byte>(buf, sizeof(buf)));
        if (n > 0) { out.append(reinterpret_cast<const char*>(buf), (std::size_t)n); continue; }
        if (n == 0) break;
        if (static_cast<int>(::GetTickCount() - deadline) >= 0) break;
        ::Sleep(10);
    }
#else
    for (;;) {
        struct pollfd pfd { pty.masterFd(), POLLIN, 0 };
        if (::poll(&pfd, 1, 5000) <= 0) break;
        const std::ptrdiff_t n = pty.read(std::span<std::byte>(buf, sizeof(buf)));
        if (n > 0) { out.append(reinterpret_cast<const char*>(buf), (std::size_t)n); continue; }
        break; // 0=EOF / -1=错误
    }
#endif
    return out;
}

// 去掉行尾空白（\r\n/空格），便于跨平台比对。
static std::string trimRight(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    return s;
}

#if defined(_WIN32)
static std::string zzGetCwd()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetCurrentDirectoryW(MAX_PATH, buf);
    std::wstring w(buf, n);
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                          nullptr, 0, nullptr, nullptr);
    std::string out((std::size_t)len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), len,
                          nullptr, nullptr);
    return out;
}
static std::string zzTempDir()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetTempPathW(MAX_PATH, buf);
    std::wstring w(buf, n);
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(),
                                          nullptr, 0, nullptr, nullptr);
    std::string out((std::size_t)len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), out.data(), len,
                          nullptr, nullptr);
    while (!out.empty() && (out.back() == '\\' || out.back() == '/')) out.pop_back();
    return out;
}
#else
static std::string zzGetCwd()
{
    char buf[PATH_MAX];
    return ::getcwd(buf, sizeof(buf)) ? std::string(buf) : std::string();
}
static std::string zzTempDir() { return "/tmp"; }
#endif

// 1. cwd 注入：子进程 pwd == 指定目录
static void testCwdInjected()
{
    const std::string dir = zzTempDir();
    ZzPtyConfig cfg;
#if defined(_WIN32)
    cfg.argv = {"cmd.exe", "/c", "cd"};
#else
    cfg.argv = {"pwd"};
#endif
    cfg.cwd = dir;
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;
    const std::string out = trimRight(readAll(*pty));
    ZZ_TEST_EXPECT(out == dir); // /tmp 无符号链接歧义（Linux TMPDIR 注意：
                                // 若失败先核 realpath 口径再改）
}

// 2. cwd 缺省：子进程 pwd == 父进程 cwd（旧行为不变）
static void testCwdDefault()
{
    ZzPtyConfig cfg;
#if defined(_WIN32)
    cfg.argv = {"cmd.exe", "/c", "cd"};
#else
    cfg.argv = {"pwd"};
#endif
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;
    ZZ_TEST_EXPECT(trimRight(readAll(*pty)) == zzGetCwd());
}

// 3. cwd 非法路径：spawn 失败（Unix errno 通道；Windows ERROR_DIRECTORY）
static void testCwdInvalid()
{
    ZzPtyConfig cfg;
#if defined(_WIN32)
    cfg.argv = {"cmd.exe", "/c", "cd"};
#else
    cfg.argv = {"pwd"};
#endif
    cfg.cwd = "/nonexistent/zz-m18-no-such-dir";
    ZZ_TEST_EXPECT(ZzPty::spawn(cfg) == nullptr);
}

// 4. env 注入：子进程读到注入值
static void testEnvInjected()
{
    ZzPtyConfig cfg;
#if defined(_WIN32)
    cfg.argv = {"cmd.exe", "/c", "echo %ZZ_M18_PROBE%"};
#else
    cfg.argv = {"printenv", "ZZ_M18_PROBE"};
#endif
    cfg.env = {"ZZ_M18_PROBE=marker42"};
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;
    ZZ_TEST_EXPECT(trimRight(readAll(*pty)) == "marker42");
}

// 5. env 同名覆盖：后写覆盖先写
static void testEnvOverride()
{
    ZzPtyConfig cfg;
#if defined(_WIN32)
    cfg.argv = {"cmd.exe", "/c", "echo %ZZ_M18_PROBE%"};
#else
    cfg.argv = {"printenv", "ZZ_M18_PROBE"};
#endif
    cfg.env = {"ZZ_M18_PROBE=a", "ZZ_M18_PROBE=b"};
    auto pty = ZzPty::spawn(cfg);
    ZZ_TEST_EXPECT(pty != nullptr);
    if (!pty) return;
    ZZ_TEST_EXPECT(trimRight(readAll(*pty)) == "b");
}

int main()
{
    testCwdInjected();
    testCwdDefault();
    testCwdInvalid();
    testEnvInjected();
    testEnvOverride();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
