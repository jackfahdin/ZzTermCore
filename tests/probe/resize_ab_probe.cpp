// 残行/不满屏归属定位探针（2026-10-21 开放缺陷，docs/Scrollback-and-Reflow.md §9 状态更正）。
// 真实 bash PTY + 双后端 tee：同一字节流喂 native/contour 两个引擎，跑两遍剧本——
//   pass 0 = 参照（不 resize），pass 1 = 拖拽式连续 resize 扫掠（长提示符窄窗折行）。
// 每步 dump 两引擎的 屏幕+历史+光标 与原始字节流到输出目录，供 diff/判读。
// 用法: resize_ab_probe [输出目录]（默认 /tmp/resize-ab；需本机有 bash）。
// 判读: diff <out>/ref/<eng>-ls.txt <out>/resized/<eng>-final-c200-r50.txt 应为空。
#include <ZzTerm/Terminal.h>

#include "ZzPty.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <algorithm>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kCols = 200;
constexpr int kRows = 50;

std::string rowText(const ZzLineView& line)
{
    std::string out;
    for (int c = 0; c < line.cellCount(); ++c)
        out += line.cellAt(c).text;
    // 裁尾空白，但保留行存在性
    const auto pos = out.find_last_not_of(' ');
    if (pos == std::string::npos)
        out.clear();
    else
        out.resize(pos + 1);
    return out;
}

void dumpState(const ZzTerminal& term, const fs::path& file)
{
    std::ofstream os(file);
    const ZzHistoryView& hv = term.historyView();
    os << "== history (" << hv.lineCount() << ") ==\n";
    for (std::size_t i = 0; i < hv.lineCount(); ++i)
        os << "H" << i << "|" << rowText(hv.lineAt(i)) << "\n";
    const ZzRenderView& rv = term.renderView();
    os << "== screen ==\n";
    for (int r = 0; r < rv.size().rows; ++r)
        os << "S" << r << "|" << rowText(rv.lineAt(r)) << "\n";
    const ZzCursorState cur = rv.cursor();
    os << "== cursor row=" << cur.position.row << " col=" << cur.position.col
       << " visible=" << cur.visible << " ==\n";
}

struct Session {
    std::unique_ptr<ZzPty> pty;
    ZzTerminal native { kCols, kRows, ZzBackendKind::Native, 10000 };
    ZzTerminal contour { kCols, kRows, ZzBackendKind::Contour, 10000 };

    void feedBoth(std::span<const std::byte> data)
    {
        native.feed(data);
        contour.feed(data);
    }

    // 抽干 PTY 输出直到安静 quietMs 毫秒；rawLog 非空时把原始字节一并落盘
    std::ofstream rawLog;

    void drain(int quietMs = 400, int maxMs = 15000)
    {
        int waited = 0;
        int quiet = 0;
        std::byte buf[16384];
        while (quiet < quietMs && waited < maxMs) {
            pollfd pfd { .fd = pty->masterFd(),
                         .events = POLLIN, .revents = 0 };
            const int rc = ::poll(&pfd, 1, 50);
            waited += 50;
            if (rc > 0 && (pfd.revents & POLLIN)) {
                const auto n = pty->read(std::span<std::byte>(buf, sizeof(buf)));
                if (n > 0) {
                    if (rawLog)
                        rawLog.write(reinterpret_cast<const char*>(buf), n);
                    feedBoth(std::span<const std::byte>(buf, static_cast<std::size_t>(n)));
                    quiet = 0;
                    continue;
                }
            }
            quiet += 50;
        }
    }

    void resizeBoth(int cols, int rows)
    {
        native.resize(cols, rows);
        contour.resize(cols, rows);
        pty->resize(cols, rows); // 触发 SIGWINCH → readline 重绘
    }
};

void runPass(Session& s, const fs::path& outDir, bool withResize)
{
    s.drain(); // bash 启动横幅+首个提示符
    dumpState(s.native, outDir / "native-boot.txt");
    dumpState(s.contour, outDir / "contour-boot.txt");

    s.pty->writeAll(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("ls -la\n"), 7));
    s.drain();
    dumpState(s.native, outDir / "native-ls.txt");
    dumpState(s.contour, outDir / "contour-ls.txt");

    if (withResize) {
        // 模拟真实拖拽：连续细粒度扫掠，短停顿（不给完全稳定窗口）；
        // 长提示符（48 字符）在窄窗必然折行，触发 readline 折行重绘
        int step = 0;
        const auto dumpBoth = [&](const char* tag, int cols, int rows) {
            char name[80];
            std::snprintf(name, sizeof(name), "native-%s-c%d-r%d.txt", tag, cols, rows);
            dumpState(s.native, outDir / name);
            std::snprintf(name, sizeof(name), "contour-%s-c%d-r%d.txt", tag, cols, rows);
            dumpState(s.contour, outDir / name);
        };
        for (int c = 196; c >= 44; c -= 4) {          // 快速拖窄 200→44
            int rows = 50 - (200 - c) * 22 / 156;      // 行同步减到 ~28
            s.resizeBoth(c, rows);
            s.drain(60, 250);
            if (++step % 10 == 0)
                dumpBoth("down", c, rows);
        }
        s.drain();                                      // 窄窗稳定
        dumpBoth("narrow-settle", 44, 28);
        for (int c = 48; c <= 200; c += 4) {           // 快速拉宽 44→200
            int rows = 28 + (c - 44) * 22 / 156;
            s.resizeBoth(c, rows);
            s.drain(60, 250);
            if (++step % 10 == 0)
                dumpBoth("up", c, rows);
        }
        s.resizeBoth(200, 50);
        s.drain();                                      // 最终稳定
        dumpBoth("final", 200, 50);
    }

    s.pty->writeAll(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("exit\n"), 5));
    s.drain(300, 3000);
}

// 第三轮剧本（2026-10-22 用户实测症状）：最大化满屏 ls 后反复缩扩几轮。
// 与 resized pass 的差异：中途不 drain——GUI 连续拖拽时 bash 的 SIGWINCH 重绘
// 滞后于一连串 resize，字节在全部 resize 之后才喂进引擎（交错顺序不同）。
// 判读：每轮稳定后 cursor row 应等于 rows-1（提示符贴底），底部无空行。
void runCyclesPass(Session& s, const fs::path& outDir)
{
    const auto dumpBoth = [&](const char* tag) {
        char name[80];
        std::snprintf(name, sizeof(name), "native-%s.txt", tag);
        dumpState(s.native, outDir / name);
        std::snprintf(name, sizeof(name), "contour-%s.txt", tag);
        dumpState(s.contour, outDir / name);
    };
    s.drain(); // 启动横幅+首个提示符
    s.pty->writeAll(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("ls -la\n"), 7));
    s.drain();
    dumpBoth("ls");

    for (int round = 0; round < 3; ++round) {
        for (int c = 196; c >= 60; c -= 8) {           // 拖窄 200→60，不等待
            int rows = 50 - (200 - c) * 30 / 140;      // 行同步减到 ~20
            s.resizeBoth(c, rows);
        }
        for (int c = 68; c <= 200; c += 8) {           // 拉宽 60→200，不等待
            int rows = 20 + (c - 60) * 30 / 140;
            s.resizeBoth(c, rows);
        }
        s.resizeBoth(200, 50);
        s.drain();                                     // 本轮稳定
        char tag[40];
        std::snprintf(tag, sizeof(tag), "cycle-%d", round);
        dumpBoth(tag);
    }

    for (int round = 0; round < 3; ++round) {
        for (int c = 192; c >= 44; c -= 8)             // 纯列变扫掠 200→44
            s.resizeBoth(c, 50);
        for (int c = 52; c <= 200; c += 8)             // 纯列变扫掠 44→200
            s.resizeBoth(c, 50);
        s.resizeBoth(200, 50);
        s.drain();
        char tag[40];
        std::snprintf(tag, sizeof(tag), "colsonly-%d", round);
        dumpBoth(tag);
    }

    for (int round = 0; round < 3; ++round) {
        for (int r = 48; r >= 20; r -= 2)              // 纯行变扫掠 50→20
            s.resizeBoth(200, r);
        for (int r = 22; r <= 50; r += 2)              // 纯行变扫掠 20→50
            s.resizeBoth(200, r);
        s.resizeBoth(200, 50);
        s.drain();
        char tag[40];
        std::snprintf(tag, sizeof(tag), "rowsonly-%d", round);
        dumpBoth(tag);
    }

    // 变体 D（GUI 真实链路）：ZzTerminalView 对 transport->resize 做 150ms 尾随
    // 去抖——拖拽期间引擎逐事件 resize 而 PTY 不动，停手才一次送达。这里模拟：
    // 引擎单独扫掠（PTY 保持 200x50，bash 收不到 SIGWINCH、全程无重绘字节），
    // 扫完把 PTY 调到终点尺寸（与起点相同则无 SIGWINCH，纯引擎语义考验）。
    for (int round = 0; round < 3; ++round) {
        for (int c = 196; c >= 60; c -= 8) {
            int rows = 50 - (200 - c) * 30 / 140;
            s.native.resize(c, rows);
            s.contour.resize(c, rows);
        }
        for (int c = 68; c <= 200; c += 8) {
            int rows = 20 + (c - 60) * 30 / 140;
            s.native.resize(c, rows);
            s.contour.resize(c, rows);
        }
        s.native.resize(200, 50);
        s.contour.resize(200, 50);
        s.drain(200, 800); // 可能到来的残留输出抽干
        char tag[40];
        std::snprintf(tag, sizeof(tag), "debounce-%d", round);
        dumpBoth(tag);
    }
    // 变体 E：去抖后终点与起点不同（bash 在终点重绘一次），再从终点继续扫。
    for (int round = 0; round < 3; ++round) {
        const int endCols = 150 - round * 20;          // 150, 130, 110
        const int endRows = 40 - round * 5;            // 40, 35, 30
        for (int c = 196; c >= 60; c -= 8) {
            int rows = 50 - (200 - c) * 30 / 140;
            s.native.resize(c, rows);
            s.contour.resize(c, rows);
        }
        for (int c = 68; c <= endCols; c += 8) {
            int rows = 20 + (c - 60) * (endRows - 20) / (endCols - 60);
            s.native.resize(c, rows);
            s.contour.resize(c, rows);
        }
        s.native.resize(endCols, endRows);
        s.contour.resize(endCols, endRows);
        s.pty->resize(endCols, endRows);               // 去抖 settled：bash 重绘
        s.drain();
        char tag[40];
        std::snprintf(tag, sizeof(tag), "debounce-end-c%d-r%d", endCols, endRows);
        dumpBoth(tag);
        // 回到 200x50（同样引擎先行、PTY 尾随）
        s.native.resize(200, 50);
        s.contour.resize(200, 50);
        s.pty->resize(200, 50);
        s.drain();
        std::snprintf(tag, sizeof(tag), "debounce-back-%d", round);
        dumpBoth(tag);
    }

    // 变体 F（GUI 全链路高保真）：引擎逐事件 resize + PTY 150ms 尾随去抖
    // （拖拽中途停顿 >150ms 时 PTY 送达中间尺寸，bash 在引擎继续 resize 的
    // 同时按中间尺寸重绘——feed/resize 任意交错）。固定种子随机游走可复现。
    {
        std::mt19937 rng(20261022);
        int cols = 200, rows = 50;
        auto sincePty = 0; // 距上次 PTY 送达的毫秒数
        for (int step = 0; step < 60; ++step) {
            const int dCols = static_cast<int>(rng() % 13) - 6;   // ±6
            const int dRows = static_cast<int>(rng() % 7) - 3;    // ±3
            cols = std::clamp(cols + dCols, 24, 200);
            rows = std::clamp(rows + dRows, 6, 50);
            s.native.resize(cols, rows);
            s.contour.resize(cols, rows);
            const int pause = 30 + static_cast<int>(rng() % 120); // 30-150ms
            sincePty += pause;
            s.drain(pause, pause + 10);                           // 边拖边喂
            if (sincePty >= 150) {                                // 去抖 settled
                s.pty->resize(cols, rows);
                sincePty = 0;
            }
        }
        s.native.resize(200, 50);
        s.contour.resize(200, 50);
        s.pty->resize(200, 50);
        s.drain();
        dumpBoth("fuzz-final");
    }

    s.pty->writeAll(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("exit\n"), 5));
    s.drain(300, 3000);
}

// 第四轮剧本（2026-10-22 用户实测症状定因）：94+ 字符长提示符——窗口窄于
// 提示符可见长度时 bash/readline 进入水平滚动模式、整行不再回写提示符
// （每步重绘退化为 `\r<空格>\r`），拉宽越过阈值后才在「其模型锚定的旧行」
// 重印提示符。逐步扫掠穿过阈值，判读：cursor row 应=rows-1 且底部无空行。
void runLongPs1Pass(Session& s, const fs::path& outDir)
{
    const auto dumpBoth = [&](const char* tag, int cols, int rows) {
        char name[80];
        std::snprintf(name, sizeof(name), "native-%s-c%d-r%d.txt", tag, cols, rows);
        dumpState(s.native, outDir / name);
        std::snprintf(name, sizeof(name), "contour-%s-c%d-r%d.txt", tag, cols, rows);
        dumpState(s.contour, outDir / name);
    };
    s.drain(); // 启动横幅+首个提示符
    s.pty->writeAll(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("ls -la\n"), 7));
    s.drain();
    dumpBoth("ls", kCols, kRows);

    for (int c = 196; c >= 44; c -= 4) {           // 下行慢扫掠（穿过 ~94 阈值到 44）
        int rows = 50 - (200 - c) * 21 / 156;
        s.resizeBoth(c, rows);
        s.drain(60, 250);
    }
    s.drain();
    dumpBoth("narrow-settle", 44, 29);
    for (int c = 48; c <= 200; c += 4) {           // 上行慢扫掠拉回
        int rows = 29 + (c - 44) * 21 / 156;
        s.resizeBoth(c, rows);
        s.drain(60, 250);
        if (c % 20 == 0)
            dumpBoth("up", c, rows);
    }
    s.resizeBoth(200, 50);
    s.drain();
    dumpBoth("final", 200, 50);

    s.pty->writeAll(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("exit\n"), 5));
    s.drain(300, 3000);
}

} // namespace

int main(int argc, char** argv)
{
    const fs::path workdir = argc > 1 ? argv[1] : "/tmp/resize-ab";
    const fs::path filler = workdir / "filler";
    fs::create_directories(filler);
    // 预先建好输出目录，避免 pass 之间 `..` 目录项变化污染对比
    fs::create_directories(workdir / "ref");
    fs::create_directories(workdir / "resized");
    // 120 个文件保证 ls -la 在 50 行屏溢出进历史
    for (int i = 0; i < 120; ++i) {
        std::ofstream(filler / ("file-" + std::to_string(i) + ".txt")) << "x";
    }

    for (int pass = 0; pass < 4; ++pass) {
        const bool withResize = pass == 1;
        const bool withCycles = pass == 2;
        const bool withLongPs1 = pass == 3;
        const fs::path outDir = workdir / (withLongPs1 ? "longps1" : withCycles ? "cycles"
                                           : withResize             ? "resized"
                                                                    : "ref");
        fs::create_directories(outDir);

        ZzPtyConfig cfg;
        cfg.argv = { "/bin/bash", "--noprofile", "--norc", "-i" };
        cfg.cols = kCols;
        cfg.rows = kRows;
        cfg.rawMode = false; // 交互 bash 需要正常 termios（readline 自管输入，ONLCR 输出换行回车）
        cfg.cwd = filler.string();
        if (withLongPs1)
            // 94 可见字符 + 颜色转义（用户真实 PS1 同构）：窄于 ~94 列触发水平滚动
            cfg.env = { "PS1=\\[\\e[00;35m\\]Jackfahdin\\[\\e[00m\\]\\[\\e[00;33m\\]@\\[\\e[00m\\]\\[\\e[00;32m\\]zz\\[\\e[00m\\]: \\[\\e[01;34m\\]/tmp/Jackfahdin/github/ZzClawTerm/long/path/filler\\[\\e[00m\\] $ ",
                        "TERM=xterm-256color", "LC_ALL=C.UTF-8" };
        else
            cfg.env = { "PS1=Jackfahdin@zz:~/Jackfahdin/github/ZzClawTerm$ ",
                        "TERM=xterm-256color", "LC_ALL=C.UTF-8" };

        Session s;
        s.pty = ZzPty::spawn(cfg);
        if (!s.pty) {
            std::fprintf(stderr, "PTY spawn 失败: %s\n", std::strerror(errno));
            return 1;
        }
        s.rawLog.open(outDir / "raw.bin", std::ios::binary);
        if (withCycles)
            runCyclesPass(s, outDir);
        else if (withLongPs1)
            runLongPs1Pass(s, outDir);
        else
            runPass(s, outDir, withResize);
    }

    std::printf("完成。判读：\n");
    std::printf("  diff ref/native-ls.txt resized/native-final-c200-r50.txt\n");
    std::printf("  diff ref/contour-ls.txt resized/contour-final-c200-r50.txt\n");
    std::printf("  各 cycles/cycle-N 末行 cursor row 应=49 且 S49 非空\n");
    return 0;
}
