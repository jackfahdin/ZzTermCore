// zzExtractSelectionText 提取矩阵（M5a）：四规则 + 接缝续接 + clamp。
#include "../../src/backend/ZzLineSource.h"
#include "../../src/terminal/ZzSelectionText.h"

#include <cstdio>
#include <string>
#include <utility>
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

ZzCell narrowCell(char c)
{
    ZzCell cell;
    cell.setWidth(ZzCellWidth::Narrow);
    cell.setCodePoint(static_cast<char32_t>(c));
    return cell;
}

// 用 ASCII 文本构造一行；cols 定宽，尾部为空单元格（isEmpty）。
ZzLine makeLine(int cols, std::string_view text, bool wrapped)
{
    ZzLine line;
    line.resize(cols);
    int col = 0;
    for (char c : text)
        line.setCell(col++, narrowCell(c));
    line.setWrapped(wrapped);
    return line;
}

// fake 数据源：rows 前半截为历史（historyRows 条），其余为屏幕。
class FakeSource final : public ZzIPhysicalLineSource {
public:
    FakeSource(int cols, int historyRows, std::vector<ZzLine> rows)
        : cols_(cols), historyRows_(historyRows), rows_(std::move(rows))
    {
    }
    std::size_t historyLineCount() const override { return static_cast<std::size_t>(historyRows_); }
    int screenRowCount() const override { return static_cast<int>(rows_.size()) - historyRows_; }
    int cols() const override { return cols_; }
    ZzLine lineAt(std::size_t unifiedRow) const override { return rows_.at(unifiedRow); }
    bool lineWrapped(std::size_t unifiedRow) const override { return rows_.at(unifiedRow).wrapped(); }
    std::uint64_t droppedLineCount() const override { return 0; }

private:
    int cols_;
    int historyRows_;
    std::vector<ZzLine> rows_;
};

} // namespace

static void testPlainSingleLogicalLine()
{
    FakeSource src(10, 0, {makeLine(10, "hello", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 5}) == "hello");
}

static void testSoftWrapChainJoinsWithoutNewline()
{
    // "hello" + "world" 一条逻辑行（首行 wrapped），共 10 格
    FakeSource src(5, 0, {makeLine(5, "hello", true), makeLine(5, "world", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 10}) == "helloworld");
}

static void testCrossLogicalLinesGetNewline()
{
    FakeSource src(10, 0, {makeLine(10, "ab", false), makeLine(10, "cd", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {1, 2}) == "ab\ncd");
}

static void testNoTrailingNewline()
{
    FakeSource src(10, 0, {makeLine(10, "ab", false)});
    const std::string text = zzExtractSelectionText(src, {0, 0}, {0, 2});
    ZZ_TEST_EXPECT(text == "ab");
}

static void testTrailingBlankTrimmed()
{
    // "ab" 后 8 个空单元格；选到行尾，输出不含尾部空白
    FakeSource src(10, 0, {makeLine(10, "ab", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 10}) == "ab");
}

static void testInteriorBlankKeptAsSpace()
{
    // "a" + 空单元格 + "b"：行内空白保留为一个空格
    ZzLine line;
    line.resize(5);
    line.setCell(0, narrowCell('a'));
    line.setCell(2, narrowCell('b'));
    FakeSource src(5, 0, {std::move(line)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 3}) == "a b");
}

static void testSeamStitchHistoryToScreen()
{
    // 历史末行 wrapped（续到屏幕首行）：跨域按一条逻辑行拼接，不插换行
    FakeSource src(5, 1, {makeLine(5, "hello", true), makeLine(5, "world", false)});
    ZZ_TEST_EXPECT(zzLogicalLineCount(src) == 1);
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 10}) == "helloworld");
}

static void testNoSeamStitchWhenHardBoundary()
{
    FakeSource src(5, 1, {makeLine(5, "hello", false), makeLine(5, "world", false)});
    ZZ_TEST_EXPECT(zzLogicalLineCount(src) == 2);
}

static void testWideCharBoundaryNormalization()
{
    // 格序列：'x' + 宽字符"界"(lead+续格) + 'y'
    ZzLine line;
    line.resize(6);
    line.setCell(0, narrowCell('x'));
    ZzCell lead;
    lead.setWidth(ZzCellWidth::WideLead);
    lead.setCodePoint(U'界');
    line.setCell(1, lead);
    ZzCell cont;
    cont.setWidth(ZzCellWidth::WideContinuation);
    line.setCell(2, cont);
    line.setCell(3, narrowCell('y'));
    FakeSource src(6, 0, {std::move(line)});
    // start 落在续格 → 归一到 lead，含整字
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 2}, {0, 4})
                   == zzExtractSelectionText(src, {0, 1}, {0, 4}));
    // end 落在续格（半开区间切在半字中间）→ 前扩含整字
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 2})
                   == zzExtractSelectionText(src, {0, 0}, {0, 3}));
}

static void testClusterTakenAsWhole()
{
    ZzLine line;
    line.resize(4);
    ZzCell cluster;
    cluster.setWidth(ZzCellWidth::Narrow);
    cluster.setCluster(line.internCluster("a\u0301")); // a + 组合重音符
    line.setCell(0, cluster);
    line.setCell(1, narrowCell('b'));
    FakeSource src(4, 0, {std::move(line)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 2}) == "a\u0301b");
}

static void testEmptySelectionReturnsEmpty()
{
    FakeSource src(10, 0, {makeLine(10, "hello", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 3}, {0, 3}).empty());
}

static void testOutOfRangeClamped()
{
    FakeSource src(10, 0, {makeLine(10, "hello", false)});
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {0, 999}) == "hello");
    ZZ_TEST_EXPECT(zzExtractSelectionText(src, {0, 0}, {99, 0}) == "hello");
}

int main()
{
    testPlainSingleLogicalLine();
    testSoftWrapChainJoinsWithoutNewline();
    testCrossLogicalLinesGetNewline();
    testNoTrailingNewline();
    testTrailingBlankTrimmed();
    testInteriorBlankKeptAsSpace();
    testSeamStitchHistoryToScreen();
    testNoSeamStitchWhenHardBoundary();
    testWideCharBoundaryNormalization();
    testClusterTakenAsWhole();
    testEmptySelectionReturnsEmpty();
    testOutOfRangeClamped();
    if (g_failures == 0)
        std::printf("test_selection_text: all passed\n");
    return g_failures;
}
