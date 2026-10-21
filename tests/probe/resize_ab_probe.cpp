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

    for (int pass = 0; pass < 2; ++pass) {
        const bool withResize = pass == 1;
        const fs::path outDir = workdir / (withResize ? "resized" : "ref");
        fs::create_directories(outDir);

        ZzPtyConfig cfg;
        cfg.argv = { "/bin/bash", "--noprofile", "--norc", "-i" };
        cfg.cols = kCols;
        cfg.rows = kRows;
        cfg.rawMode = false; // 交互 bash 需要正常 termios（readline 自管输入，ONLCR 输出换行回车）
        cfg.cwd = filler.string();
        cfg.env = { "PS1=Jackfahdin@zz:~/Jackfahdin/github/ZzClawTerm$ ",
                    "TERM=xterm-256color", "LC_ALL=C.UTF-8" };

        Session s;
        s.pty = ZzPty::spawn(cfg);
        if (!s.pty) {
            std::fprintf(stderr, "PTY spawn 失败: %s\n", std::strerror(errno));
            return 1;
        }
        s.rawLog.open(outDir / "raw.bin", std::ios::binary);
        runPass(s, outDir, withResize);
    }

    std::printf("完成。判读：\n");
    std::printf("  diff ref/native-ls.txt resized/native-final-c200-r50.txt\n");
    std::printf("  diff ref/contour-ls.txt resized/contour-final-c200-r50.txt\n");
    return 0;
}
