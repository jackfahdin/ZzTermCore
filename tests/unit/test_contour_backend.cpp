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

} // namespace

int main()
{
    testBridge();
    testAsciiSnapshot();
    testResizeBasic();
    if (g_failures != 0)
        std::fprintf(stderr, "test_contour_backend: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
