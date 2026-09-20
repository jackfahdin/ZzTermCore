// ZzTermSmoke：控制台冒烟 Demo（调试工具，正式 GUI Demo 为未来的 ZzTermDemo）。
// 用法：ZzTermSmoke [--backend=native|contour] [-- 命令...]，默认 contour + bash。
// 子进程退出后以子进程退出码退出。
//
// 结构：PTY 输出 -> ZzTerminal -> RenderView 全屏重绘到 stdout（ANSI SGR）；
//       stdin 经 Core 输入链路（sendText/sendKey）-> output 通道 -> PTY；
//       SIGWINCH 自管道同步 PTY 与 Terminal 尺寸。
// 非 tty 场景（CTest 管道）：termios guard 不生效、尺寸回退 80x24、
// stdin EOF 后停止监听，仍可完整跑通（脚本化冒烟依赖此行为）。
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
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

/// 输入翻译器（demo 本地）：stdin 字节流 → sendText / sendKey / sendMouse。
/// 功能键按 xterm 编码识别（CSI/SS3），鼠标按经典 X10 与 SGR 1006 两种
/// 形态识别，其余字节（含 UTF-8 多字节）走 sendText。
/// 不完整转义序列在缓冲中等待后续字节；无法识别的序列丢弃。
/// 已知限制：裸 ESC 无超时消歧，会延迟到下一个输入字节连带发出
/// （经典终端输入二义性；demo 可接受，生产前端应自带超时）。
class InputTranslator {
public:
    explicit InputTranslator(ZzTerminal& term) : term_(term) {}

    void feed(std::string_view data)
    {
        buf_ += data;
        std::size_t i = 0;
        while (i < buf_.size()) {
            if (buf_[i] == '\x1B') {
                const std::size_t eaten = tryEscape(std::string_view(buf_).substr(i));
                if (eaten == 0)
                    break; // 序列不完整：等更多字节
                i += eaten;
                continue;
            }
            // 普通文本段：累积到下一个 ESC 为止一次性 sendText
            const std::size_t next = buf_.find('\x1B', i);
            const std::size_t end = next == std::string::npos ? buf_.size() : next;
            term_.sendText(std::string_view(buf_).substr(i, end - i));
            i = end;
        }
        buf_.erase(0, i);
    }

private:
    void sendKey(ZzKeyEvent::Key k)
    {
        ZzKeyEvent ev;
        ev.key = k;
        term_.sendKey(ev);
    }

    // 鼠标码（xterm 位布局）→ ZzMouseEvent → sendMouse。sgrRelease 仅 SGR 编码
    // 用（m 结尾）；经典编码释放由码 3 表达。坐标入参为协议 1 起始。
    void sendMouseFromCode(int code, int x, int y, bool sgrRelease)
    {
        ZzMouseEvent ev;
        ev.col = x - 1;
        ev.row = y - 1;
        if (code & 4)
            ev.modifiers = ev.modifiers | ZzKeyModifier::Shift;
        if (code & 8)
            ev.modifiers = ev.modifiers | ZzKeyModifier::Alt;
        if (code & 16)
            ev.modifiers = ev.modifiers | ZzKeyModifier::Ctrl;
        if (code & 64) {
            // 滚轮只有按下；横向滚轮（66/67）demo 忽略。
            if ((code & 1) != 0 || (code & 2) != 0)
                return;
            ev.action = ZzMouseAction::Press;
            ev.button = ZzMouseButton::WheelUp;
        } else if ((code & 3) == 3 || sgrRelease) {
            ev.action = ZzMouseAction::Release;
            ev.button = ZzMouseButton::None;
        } else if (code & 32) {
            ev.action = ZzMouseAction::Move;
            ev.button = ZzMouseButton::None;
        } else {
            ev.action = ZzMouseAction::Press;
            ev.button = (code & 3) == 0 ? ZzMouseButton::Left
                      : (code & 3) == 1 ? ZzMouseButton::Middle
                                        : ZzMouseButton::Right;
        }
        term_.sendMouse(ev);
    }

    // 返回消费字节数；0 = 序列不完整需等待。
    std::size_t tryEscape(std::string_view s)
    {
        if (s.size() < 2)
            return 0;
        if (s[1] == 'O') { // SS3：方向/Home/End/F1-F4
            if (s.size() < 3)
                return 0;
            switch (s[2]) {
            case 'A': sendKey(ZzKeyEvent::Key::Up); return 3;
            case 'B': sendKey(ZzKeyEvent::Key::Down); return 3;
            case 'C': sendKey(ZzKeyEvent::Key::Right); return 3;
            case 'D': sendKey(ZzKeyEvent::Key::Left); return 3;
            case 'H': sendKey(ZzKeyEvent::Key::Home); return 3;
            case 'F': sendKey(ZzKeyEvent::Key::End); return 3;
            case 'P': sendKey(ZzKeyEvent::Key::F1); return 3;
            case 'Q': sendKey(ZzKeyEvent::Key::F2); return 3;
            case 'R': sendKey(ZzKeyEvent::Key::F3); return 3;
            case 'S': sendKey(ZzKeyEvent::Key::F4); return 3;
            default: return 2; // 未知 SS3：丢弃 ESC O
            }
        }
        if (s[1] != '[') {
            sendKey(ZzKeyEvent::Key::Escape);
            return 1; // 裸 ESC
        }
        if (s.size() >= 3 && s[2] == 'M') {
            // 经典 X10 鼠标：ESC [ M Cb Cx Cy（各 -32）
            if (s.size() < 6)
                return 0;
            const int code = static_cast<unsigned char>(s[3]) - 32;
            const int x = static_cast<unsigned char>(s[4]) - 32;
            const int y = static_cast<unsigned char>(s[5]) - 32;
            sendMouseFromCode(code, x, y, false);
            return 6;
        }
        if (s.size() >= 3 && s[2] == '<') {
            // SGR 1006 鼠标：ESC [ < code ; x ; y M/m
            std::size_t j = 3;
            while (j < s.size()
                   && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == ';'))
                ++j;
            if (j >= s.size())
                return 0; // 不完整
            if (s[j] != 'M' && s[j] != 'm')
                return j; // 非鼠标 < 序列：丢弃已扫描部分
            int code = 0, x = 0, y = 0;
            if (std::sscanf(std::string(s.substr(3, j - 3)).c_str(), "%d;%d;%d", &code, &x, &y)
                == 3)
                sendMouseFromCode(code, x, y, s[j] == 'm');
            return j + 1;
        }
        // CSI：ESC [ 参数 final
        std::size_t j = 2;
        while (j < s.size()
               && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == ';'))
            ++j;
        if (j >= s.size())
            return 0; // 不完整
        const char fin = s[j];
        switch (fin) {
        case 'A': sendKey(ZzKeyEvent::Key::Up); return j + 1;
        case 'B': sendKey(ZzKeyEvent::Key::Down); return j + 1;
        case 'C': sendKey(ZzKeyEvent::Key::Right); return j + 1;
        case 'D': sendKey(ZzKeyEvent::Key::Left); return j + 1;
        case 'H': sendKey(ZzKeyEvent::Key::Home); return j + 1;
        case 'F': sendKey(ZzKeyEvent::Key::End); return j + 1;
        case '~': {
            const std::string param(s.substr(2, j - 2));
            const int n = param.empty() ? 0 : std::atoi(param.c_str());
            switch (n) {
            case 2:  sendKey(ZzKeyEvent::Key::Insert); break;
            case 3:  sendKey(ZzKeyEvent::Key::Delete); break;
            case 5:  sendKey(ZzKeyEvent::Key::PageUp); break;
            case 6:  sendKey(ZzKeyEvent::Key::PageDown); break;
            case 15: sendKey(ZzKeyEvent::Key::F5); break;
            case 17: sendKey(ZzKeyEvent::Key::F6); break;
            case 18: sendKey(ZzKeyEvent::Key::F7); break;
            case 19: sendKey(ZzKeyEvent::Key::F8); break;
            case 20: sendKey(ZzKeyEvent::Key::F9); break;
            case 21: sendKey(ZzKeyEvent::Key::F10); break;
            case 23: sendKey(ZzKeyEvent::Key::F11); break;
            case 24: sendKey(ZzKeyEvent::Key::F12); break;
            default: break; // 未知 CSI ~：丢弃
            }
            return j + 1;
        }
        default: return j + 1; // 未知 CSI：丢弃
        }
    }

    ZzTerminal& term_;
    std::string  buf_;
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

    InputTranslator translator(term);

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

        // stdin -> InputTranslator -> Core 输入链路 -> output 通道 -> PTY
        if (fds[1].revents & POLLIN) {
            const ssize_t n = ::read(STDIN_FILENO, ioBuf, sizeof(ioBuf));
            if (n > 0) {
                translator.feed(std::string_view(
                    reinterpret_cast<const char*>(ioBuf), static_cast<std::size_t>(n)));
            } else if (n == 0 || (n < 0 && errno != EINTR && errno != EAGAIN)) {
                watchStdin = false; // stdin EOF（管道场景）后停止监听
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
