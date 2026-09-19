// ZzContourBackend（M1a）headless 回归测试。
// 仅 ZZTERM_WITH_CONTOUR=ON 时构建（见 tests/CMakeLists.txt 条件注册）。
#include "ZzContourBackend.h"
#include "ZzContourPtyBridge.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

// 桥：write 转发回调、read 恒无数据、pageSize 记录、close 状态。
void testBridge()
{
    std::string written;
    auto const initialSize = vtpty::PageSize { vtpty::LineCount(24), vtpty::ColumnCount(80) };
    ZzContourPtyBridge bridge(initialSize,
                              [&written](std::string_view data) { written.append(data); });

    ZZ_CHECK(!bridge.isClosed());
    ZZ_CHECK(bridge.write("hello") == 5);
    ZZ_CHECK(written == "hello");
    ZZ_CHECK(bridge.pageSize().columns.value == 80);
    ZZ_CHECK(bridge.pageSize().lines.value == 24);

    bridge.resizeScreen(vtpty::PageSize { vtpty::LineCount(30), vtpty::ColumnCount(100) },
                        std::nullopt);
    ZZ_CHECK(bridge.pageSize().columns.value == 100);
    ZZ_CHECK(bridge.pageSize().lines.value == 30);

    ZZ_CHECK(bridge.start().has_value());
    (void) bridge.slave();
    bridge.wakeupReader();
    bridge.close();
    ZZ_CHECK(bridge.isClosed());
    bridge.waitForClosed();
}

// 事件记录器：实现 ZzContourEvents 全部纯虚，记录各事件。
class RecordingEvents : public ZzContourEvents
{
public:
    void onTitleChanged(std::string title) override { this->title = std::move(title); ++titleCount; }
    void onBell() override { ++bellCount; }
    void onScreenDirty() override { ++dirtyCount; }
    void onActiveBufferChanged(bool alternate) override { altChanges.push_back(alternate); }
    void onWriteToTransport(std::string bytes) override { written += bytes; }

    std::string title;
    int titleCount = 0;
    int bellCount = 0;
    int dirtyCount = 0;
    std::vector<bool> altChanges;
    std::string written;
};

// 快照辅助：把第 line 行前 n 列的 codepoints 拼成 u32string。
std::u32string rowText(ZzContourSnapshot const& snap, int line, int n)
{
    std::u32string out;
    for (int col = 0; col < n; ++col)
        out += snap.at(line, col).codepoints;
    return out;
}

// ASCII 写入 → 快照读回一致；尺寸与默认状态正确。
void testAsciiSnapshot()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);

    ZZ_CHECK(backend.size() == std::make_pair(80, 24));
    ZZ_CHECK(!backend.isAlternateScreen());
    ZZ_CHECK(backend.title().empty());
    ZZ_CHECK(backend.historyLineCount() == 0);

    backend.feed("Hello");
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.columns == 80);
    ZZ_CHECK(snap.rows == 24);
    ZZ_CHECK(!snap.alternateScreen);
    ZZ_CHECK(snap.cells.size() == static_cast<size_t>(80 * 24));
    ZZ_CHECK(rowText(snap, 0, 5) == U"Hello");
    ZZ_CHECK(snap.at(1, 0).codepoints.empty());
}

// resize 后尺寸与既有内容保持。
void testResizeBasic()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("Keep");
    backend.resize(100, 30);
    ZZ_CHECK(backend.size() == std::make_pair(100, 30));
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.columns == 100);
    ZZ_CHECK(snap.rows == 30);
    ZZ_CHECK(rowText(snap, 0, 4) == U"Keep");
}

// SGR 索引色前景在快照中保留颜色身份（不预转 RGB）。
void testSgrColors()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[31mR\x1b[0m");
    auto snap = backend.snapshot();
    auto const& cell = snap.at(0, 0);
    ZZ_CHECK(cell.codepoints == U"R");
    ZZ_CHECK(cell.foreground == (ZzColor { ZzColor::Tag::Indexed, 1 }));
    // 复位后写入的格子回到默认色。
    ZZ_CHECK(snap.at(0, 1).foreground == (ZzColor { ZzColor::Tag::Default, 0 }));
}

// SGR 亮红色映射为索引 8+1。
void testBrightColor()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[91mB");
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.at(0, 0).foreground == (ZzColor { ZzColor::Tag::Indexed, 9 }));
}

// RGB 真彩色前景与背景。
void testRgbColor()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[38;2;10;20;30m\x1b[48;2;200;100;50mX");
    auto snap = backend.snapshot();
    auto const& cell = snap.at(0, 0);
    ZZ_CHECK(cell.foreground
             == (ZzColor { ZzColor::Tag::RGB, (10u << 16) | (20u << 8) | 30u }));
    ZZ_CHECK(cell.background
             == (ZzColor { ZzColor::Tag::RGB, (200u << 16) | (100u << 8) | 50u }));
}

// 粗体/斜体/下划线 flags。
void testStyleFlags()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    // 各 SGR 样式间显式复位：SGR 属性是累加的（ECMA-48），不复位则粗体会带进斜体格。
    backend.feed("\x1b[1mB\x1b[0m\x1b[3mI\x1b[0m\x1b[4mU");
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.at(0, 0).flags & ZzCellFlag::Bold);
    ZZ_CHECK(snap.at(0, 1).flags & ZzCellFlag::Italic);
    ZZ_CHECK(snap.at(0, 2).flags & ZzCellFlag::Underline);
    // 斜体格不带粗体位。
    ZZ_CHECK(!(snap.at(0, 1).flags & ZzCellFlag::Bold));
}

// BCE 擦除场景：空行（isBlank 分支）须从 fillAttrs 还原背景色，前景保持默认。
void testBlankLineFillAttrs()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b[41m\x1b[2J");
    auto snap = backend.snapshot();
    auto const& cell = snap.at(1, 0);
    ZZ_CHECK(cell.codepoints.empty());
    ZZ_CHECK(cell.background == (ZzColor { ZzColor::Tag::Indexed, 1 }));
    ZZ_CHECK(cell.foreground == (ZzColor { ZzColor::Tag::Default, 0 }));
}

// 光标随写入推进；DECTCEM（CSI ?25l/h）控制可见性。
void testCursor()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("AB");
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(snap.cursor.has_value());
        ZZ_CHECK(snap.cursor->line == 0);
        ZZ_CHECK(snap.cursor->column == 2);
    }
    backend.feed("\x1b[?25l"); // 隐藏光标
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(!snap.cursor.has_value());
    }
    backend.feed("\x1b[?25h"); // 恢复显示
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(snap.cursor.has_value());
    }
}

// OSC title 与 BEL 事件；feed 触发 dirty 信号。
void testTitleBellDirty()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("\x1b]0;My Title\x07");
    ZZ_CHECK(events.title == "My Title");
    ZZ_CHECK(events.titleCount == 1);
    ZZ_CHECK(backend.title() == "My Title");

    backend.feed("\x07");
    ZZ_CHECK(events.bellCount == 1);

    ZZ_CHECK(events.dirtyCount > 0);
}

// alt screen：进入/退出/主屏内容恢复，事件按序上报。
void testAltScreen()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed("MAIN");
    backend.feed("\x1b[?1049h"); // 进备用屏
    ZZ_CHECK(backend.isAlternateScreen());
    ZZ_CHECK(events.altChanges.size() == 1 && events.altChanges[0]);
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(snap.alternateScreen);
    }
    backend.feed("ALT");
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(rowText(snap, 0, 3) == U"ALT");
    }
    backend.feed("\x1b[?1049l"); // 回主屏
    ZZ_CHECK(!backend.isAlternateScreen());
    ZZ_CHECK(events.altChanges.size() == 2 && !events.altChanges[1]);
    {
        auto snap = backend.snapshot();
        ZZ_CHECK(!snap.alternateScreen);
        ZZ_CHECK(rowText(snap, 0, 4) == U"MAIN"); // 主屏内容恢复
    }
}

// scrollback 随滚屏增长；自动换行行带 Wrapped 标记。
void testScrollback()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events, 1000);
    for (int i = 0; i < 30; ++i)
        backend.feed("L" + std::to_string(i) + "\r\n");
    ZZ_CHECK(backend.historyLineCount() >= 6); // 30 行内容至少滚出 6 行进 scrollback
}

void testLineWrapped()
{
    RecordingEvents events;
    ZzContourBackend backend(80, 24, events);
    backend.feed(std::string(100, 'a')); // 超过 80 列自动换行
    ZZ_CHECK(backend.lineWrapped(0));
    ZZ_CHECK(!backend.lineWrapped(1));
    auto snap = backend.snapshot();
    ZZ_CHECK(snap.at(0, 0).codepoints == U"a");
    ZZ_CHECK(snap.at(1, 0).codepoints == U"a"); // 第 81 个字符绕到第 1 行
}

} // namespace

int main()
{
    testBridge();
    testAsciiSnapshot();
    testResizeBasic();
    testSgrColors();
    testBrightColor();
    testRgbColor();
    testStyleFlags();
    testBlankLineFillAttrs();
    testCursor();
    testTitleBellDirty();
    testAltScreen();
    testScrollback();
    testLineWrapped();
    if (g_failures != 0)
        std::fprintf(stderr, "test_contour_backend: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
