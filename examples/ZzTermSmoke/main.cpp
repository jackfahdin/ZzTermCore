// ZzTermSmoke：控制台冒烟 Demo（调试工具，正式 GUI Demo 为未来的 ZzTermDemo）。
// 用法：ZzTermSmoke [--backend=native|contour] [-- 命令...]，默认 contour + bash。
// 子进程退出后以子进程退出码退出。
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
        const ZzLineView line = view.lineAt(row);
        for (int col = 0; col < size.cols; ++col) {
            const ZzCellView cell = line.cellAt(col);
            if (cell.width == ZzCellWidth::WideContinuation) {
                continue; // 宽字符续格不输出（首格已占两列）
            }
            const PenStyle want{cell.foreground, cell.background, cell.attributes};
            if (!(want == pen)) {
                appendStyleSgr(out, want);
                pen = want;
            }
            out += cell.text.empty() ? " " : cell.text;
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

int run(ZzBackendKind backend, const std::vector<std::string>& command)
{
    const ZzSize termSize = queryTerminalSize();

    TermiosGuard termiosGuard; // 此后所有退出路径经 RAII 恢复原始终端

    ZzPtyConfig cfg;
    cfg.argv = command;
    cfg.cols = termSize.cols;
    cfg.rows = termSize.rows;
    // 真实终端语义：slave 保留默认 termios（ISIG/ICANON/ECHO/OPOST），
    // Ctrl+C 等作业控制信号才生效；交互程序（bash/vim/less）会自行重设。
    // rawMode=true 仅服务于 Core 自动化验证（test_pty 字节级 round-trip）。
    cfg.rawMode = false;
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

    ZzTerminal term(termSize.cols, termSize.rows, backend, 1000);
    // output 通道回传字节（DA 响应等）写回 PTY，供子进程读取应答。
    term.setOutputHandler([&pty](std::string_view bytes) {
        pty->writeAll(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
    });

    // O_NONBLOCK 两端都要：读端供主循环排空（否则排空循环在管道读空后阻塞，
    // demo 在首个 SIGWINCH 后永久挂起）；写端防止信号处理器在管道写满时阻塞。
    if (::pipe2(g_winchPipe, O_NONBLOCK | O_CLOEXEC) != 0) {
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
