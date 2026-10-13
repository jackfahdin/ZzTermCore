#include "ZzPty.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <thread>
#include <vector>

#include <fcntl.h>
#if defined(__APPLE__)
#include <util.h> // openpty（macOS）
#else
#include <pty.h> // openpty（Linux 等 glibc 平台）
#endif
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

/// 幂等置 O_NONBLOCK（收割排空用；调用方可能已设置）。
void setNonblocking(int fd) noexcept
{
    if (fd >= 0) {
        const int flags = ::fcntl(fd, F_GETFL);
        if (flags >= 0 && !(flags & O_NONBLOCK)) {
            (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
    }
}

/// 非阻塞读空 fd 上当前可读的数据（子进程临终输出排空）。
void drainFd(int fd) noexcept
{
    if (fd >= 0) {
        char buf[4096];
        while (::read(fd, buf, sizeof(buf)) > 0) {
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

    // M18：环境合并在 fork 前完成（子进程保持纯 async-signal-safe）——
    // env 为空时 nullptr 走 execvp 纯继承（与旧行为逐字节一致）。
    extern char** environ; // POSIX 全局
    std::vector<std::string> mergedEnv;
    std::vector<char*> envp;
    if (!cfg.env.empty()) {
        for (char** e = environ; *e != nullptr; ++e)
            mergedEnv.emplace_back(*e);
        for (const std::string& kv : cfg.env) {
            const std::string key = kv.substr(0, kv.find('='));
            std::erase_if(mergedEnv, [&](const std::string& e) {
                return e.size() > key.size() && e.compare(0, key.size(), key) == 0
                       && e[key.size()] == '=';
            });
            mergedEnv.push_back(kv); // 后写覆盖先写（同名已删）
        }
        envp.reserve(mergedEnv.size() + 1);
        for (std::string& e : mergedEnv)
            envp.push_back(e.data());
        envp.push_back(nullptr);
    }

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
        // M18：工作目录（chdir 失败走与 exec 失败相同的 errPipe 通道）
        if (!cfg.cwd.empty() && ::chdir(cfg.cwd.c_str()) != 0) {
            const int chdirErrno = errno;
            (void)!::write(errPipe[1], &chdirErrno, sizeof(chdirErrno));
            ::_exit(127);
        }
        if (cfg.env.empty()) {
            ::execvp(args[0], args.data());
        } else {
            // execve 不搜 PATH：改用 environ 指针替换 + execvp（指针赋值
            // 为 async-signal-safe，且 macOS 无 execvpe 可移植）。
            environ = envp.data();
            ::execvp(args[0], args.data());
        }
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
        // macOS/BSD 病理：tty 输出队列满时写入方陷入不可中断睡眠，SIGKILL
        // 也无法即时收尸（CI 实证：阻塞 waitpid 挂住 300s）。收割窗口内必须
        // 持续排空 master，让子进程临终输出（SIGWINCH 重绘、exit trap 序列）
        // 始终可写通；Linux 无此病理，同一路径同样安全。
        setNonblocking(masterFd_);
        ::kill(childPid_, SIGHUP);
        bool alive = true;
        for (int round = 0; round < 2 && alive; ++round) {
            if (round == 1) {
                ::kill(childPid_, SIGKILL);
            }
            const int budgetMs = round == 0 ? 1000 : 2000; // SIGHUP 宽限 1s，SIGKILL 后 2s
            for (int elapsed = 0; elapsed < budgetMs; elapsed += 5) {
                int status = 0;
                const pid_t r = ::waitpid(childPid_, &status, WNOHANG);
                if (r == childPid_) {
                    reaped_ = true;
                    if (WIFEXITED(status)) {
                        exitCode_ = WEXITSTATUS(status);
                    } else if (WIFSIGNALED(status)) {
                        exitCode_ = 128 + WTERMSIG(status);
                    } else {
                        exitCode_ = 1;
                    }
                    alive = false;
                    break;
                }
                if (r < 0 && errno == ECHILD) { // 已被外部回收
                    reaped_ = true;
                    alive = false;
                    break;
                }
                drainFd(masterFd_);
                ::usleep(5 * 1000);
            }
        }
        if (alive) {
            // 兜底：子进程仍卡在不可中断内核态——分离线程后台收割，析构
            //（可能在 GUI 线程）永不阻塞。fd 照关：子进程已判死刑。
            std::fprintf(stderr,
                         "ZzPty: 子进程 %d SIGKILL 后仍不可收割，转后台收尸\n",
                         int(childPid_));
            std::thread([pid = childPid_]() {
                int status = 0;
                while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
            }).detach();
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
