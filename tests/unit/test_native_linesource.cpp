// ZzNativeLineSource 单测（M5a）：历史+屏幕统一坐标、Alternate 历史归零、
// 丢弃计数透传（stats().totalDropped）。
#include "../../src/backend/native/ZzNativeLineSource.h"

#include <ZzTerm/Screen.h>
#include <ZzTerm/Scrollback.h>

#include <cstdio>
#include <vector>

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

namespace {

ZzLine makeLine(int cols, char c, bool wrapped)
{
    ZzLine line;
    line.resize(cols);
    ZzCell cell;
    cell.setWidth(ZzCellWidth::Narrow);
    cell.setCodePoint(static_cast<char32_t>(c));
    line.setCell(0, cell);
    line.setWrapped(wrapped);
    return line;
}

} // namespace

static void testUnifiedRowMapping()
{
    ZzScreen screen(10, 3);
    auto scrollback = zzCreateChunkedScrollback(100);
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(10, 'a', false));
    lines.push_back(makeLine(10, 'b', true));
    scrollback->append(std::move(lines));

    ZzNativeLineSource src(screen, *scrollback);
    ZZ_TEST_EXPECT(src.historyLineCount() == 2);
    ZZ_TEST_EXPECT(src.screenRowCount() == 3);
    ZZ_TEST_EXPECT(src.cols() == 10);
    // 统一坐标：0/1 为历史（0 最旧），2 起为屏幕（空白行）
    ZZ_TEST_EXPECT(src.lineAt(0).cellAt(0).codePoint() == U'a');
    ZZ_TEST_EXPECT(src.lineAt(1).cellAt(0).codePoint() == U'b');
    ZZ_TEST_EXPECT(src.lineAt(1).wrapped());
    ZZ_TEST_EXPECT(src.lineWrapped(1) && !src.lineWrapped(0));
    ZZ_TEST_EXPECT(src.lineAt(2).cellAt(0).isEmpty()); // 屏幕空白行
}

static void testDroppedCountPassthrough()
{
    ZzScreen screen(10, 3);
    auto scrollback = zzCreateChunkedScrollback(2); // 容量 2，触发裁剪
    ZzNativeLineSource src(screen, *scrollback);
    std::vector<ZzLine> batch;
    for (int i = 0; i < 5; ++i)
        batch.push_back(makeLine(10, static_cast<char>('a' + i), false));
    scrollback->append(std::move(batch));
    ZZ_TEST_EXPECT(src.historyLineCount() == 2);
    ZZ_TEST_EXPECT(src.droppedLineCount() == 3);
    ZZ_TEST_EXPECT(src.lineAt(0).cellAt(0).codePoint() == U'd'); // 最旧留存
}

static void testAlternateScreenHidesHistory()
{
    ZzScreen screen(10, 3);
    auto scrollback = zzCreateChunkedScrollback(100);
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(10, 'a', false));
    scrollback->append(std::move(lines));
    ZzNativeLineSource src(screen, *scrollback);
    ZZ_TEST_EXPECT(src.historyLineCount() == 1);
    screen.setActiveBuffer(ZzScreenBuffer::Alternate);
    ZZ_TEST_EXPECT(src.historyLineCount() == 0); // Alternate 无历史
    ZZ_TEST_EXPECT(src.screenRowCount() == 3);
}

int main()
{
    testUnifiedRowMapping();
    testDroppedCountPassthrough();
    testAlternateScreenHidesHistory();
    if (g_failures == 0)
        std::printf("test_native_linesource: all passed\n");
    return g_failures;
}
