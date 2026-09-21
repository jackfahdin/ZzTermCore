// ZzContourLineSource 单测（M5a）：历史行快照（格+wrapped）、屏幕行快照、
// stableFloor 前移累计为丢弃计数。仅 Contour 后端启用时构建。
#include "../../src/backend/contour/ZzContourLineSource.h"
#include "../../src/backend/contour/ZzContourBackend.h"
#include "../../src/backend/contour/ZzContourEvents.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

// 事件 stub：照 test_contour_backend.cpp 的既有写法（全部默认空实现即可）。
// 方法签名以 src/backend/contour/ZzContourEvents.h 实际声明为准（std::string 按值）。
struct NullEvents : ZzContourEvents {
    void onTitleChanged(std::string) override {}
    void onBell() override {}
    void onScreenDirty() override {}
    void onActiveBufferChanged(bool) override {}
    void onWriteToTransport(std::string) override {}
};

std::string lineText(const ZzLine& line, int n)
{
    std::string out;
    for (int i = 0; i < n; ++i) {
        const ZzCell& c = line.cellAt(i);
        if (c.isEmpty())
            break;
        out.push_back(static_cast<char>(c.codePoint()));
    }
    return out;
}

} // namespace

static void testHistorySnapshot()
{
    NullEvents events;
    ZzContourBackend backend(10, 2, events, 100); // 10 列 2 行小屏
    // 造 3 条硬行历史 + 屏幕可见行：每行 "r0\r\n" 等
    backend.feed("r0\r\nr1\r\nr2\r\nr3");
    ZzContourLineSource src(backend);
    ZZ_TEST_EXPECT(src.historyLineCount() == 2);      // 4 行内容 2 行屏 → 2 行历史
    ZZ_TEST_EXPECT(lineText(src.lineAt(0), 2) == "r0"); // 0 = 最旧
    ZZ_TEST_EXPECT(!src.lineWrapped(0));                // 硬行
    ZZ_TEST_EXPECT(src.screenRowCount() == 2);
    ZZ_TEST_EXPECT(lineText(src.lineAt(src.historyLineCount()), 2) == "r2"); // 屏幕首行
}

static void testSoftWrapChainWrappedFlag()
{
    NullEvents events;
    ZzContourBackend backend(5, 2, events, 100);
    backend.feed("abcdefgh"); // 5 列：满宽 "abcde" 续行 + "fgh"
    ZzContourLineSource src(backend);
    // 屏幕首行（统一坐标的 historyLineCount() 行）wrapped=true（续到下一行）
    const auto screenFirst = src.historyLineCount();
    ZZ_TEST_EXPECT(src.lineWrapped(screenFirst));
    ZZ_TEST_EXPECT(!src.lineWrapped(screenFirst + 1));
    ZZ_TEST_EXPECT(lineText(src.lineAt(screenFirst), 5) == "abcde");
}

static void testDroppedCountAccumulates()
{
    NullEvents events;
    ZzContourBackend backend(10, 2, events, 4); // 历史容量 4 行
    ZzContourLineSource src(backend);
    std::string script;
    for (int i = 0; i < 10; ++i)
        script += "line" + std::to_string(i) + "\r\n";
    backend.feed(script);
    src.noteFloor(); // 适配层在 feed 后调用；测试直调验证语义
    ZZ_TEST_EXPECT(src.historyLineCount() == 4);
    ZZ_TEST_EXPECT(src.droppedLineCount() > 0); // 容量溢出产生丢弃
    // 幂等：无新 feed 时再次 noteFloor 不增加
    const auto dropped = src.droppedLineCount();
    src.noteFloor();
    ZZ_TEST_EXPECT(src.droppedLineCount() == dropped);
}

int main()
{
    testHistorySnapshot();
    testSoftWrapChainWrappedFlag();
    testDroppedCountAccumulates();
    if (g_failures == 0)
        std::printf("test_contour_linesource: all passed\n");
    return g_failures;
}
