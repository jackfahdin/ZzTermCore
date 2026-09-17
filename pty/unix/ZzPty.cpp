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
