// zzSearchLines 引擎矩阵（M5b）：子串多命中、大小写两态、宽字符/cluster
// 位置回映、软换行链内命中、跨逻辑行不命中、空 pattern、selectedText 自洽。
#include "../../src/backend/ZzLineSource.h"
#include "../../src/terminal/ZzSearch.h"
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

void expectMatch(const ZzLogicalRange& m, std::int64_t line, std::int32_t cs, std::int32_t ce)
{
    ZZ_TEST_EXPECT(m.start.line == line && m.start.col == cs);
    ZZ_TEST_EXPECT(m.end.line == line && m.end.col == ce);
}

// 命中坐标经 zzExtractSelectionText 提取必须等于 pattern（自洽不变量，大小写敏感）。
void expectSelfConsistent(const ZzIPhysicalLineSource& src, const std::vector<ZzLogicalRange>& matches,
                          std::string_view pattern)
{
    for (const ZzLogicalRange& m : matches) {
        const std::string text = zzExtractSelectionText(src, m.start, m.end);
        ZZ_TEST_EXPECT(text == pattern);
    }
}

} // namespace

static void testSingleMatch()
{
    FakeSource src(16, 0, {makeLine(16, "hello world", false)});
    const auto m = zzSearchLines(src, "world", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 6, 11);
    expectSelfConsistent(src, m, "world");
}

static void testMultipleMatchesNonOverlapping()
{
    FakeSource src(16, 0, {makeLine(16, "ababab", false)});
    const auto m = zzSearchLines(src, "abab", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1); // 命中后从 match 末尾继续：0-4 后 "ab" 不再命中
    expectMatch(m[0], 0, 0, 4);
}

static void testCaseInsensitiveAscii()
{
    FakeSource src(16, 0, {makeLine(16, "hello World", false)});
    ZzSearchOptions insensitive;
    insensitive.caseSensitive = false;
    const auto m = zzSearchLines(src, "world", insensitive);
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 6, 11);
    const auto none = zzSearchLines(src, "world", ZzSearchOptions{});
    ZZ_TEST_EXPECT(none.empty()); // 敏感模式不命中
}

static void testWideCharRemap()
{
    // 格序列：x 界(lead+续) y → "界" 命中应占格 1..3
    ZzLine line;
    line.resize(8);
    line.setCell(0, narrowCell('x'));
    ZzCell lead;
    lead.setWidth(ZzCellWidth::WideLead);
    lead.setCodePoint(U'界');
    line.setCell(1, lead);
    ZzCell cont;
    cont.setWidth(ZzCellWidth::WideContinuation);
    line.setCell(2, cont);
    line.setCell(3, narrowCell('y'));
    FakeSource src(8, 0, {std::move(line)});
    const auto m = zzSearchLines(src, "界", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 1, 3); // 宽字符完整覆盖，不拆半字
    expectSelfConsistent(src, m, "界");
    const auto my = zzSearchLines(src, "y", ZzSearchOptions{});
    ZZ_TEST_EXPECT(my.size() == 1);
    expectMatch(my[0], 0, 3, 4);
}

static void testClusterRemap()
{
    ZzLine line;
    line.resize(8);
    line.setCell(0, narrowCell('z'));
    ZzCell cluster;
    cluster.setWidth(ZzCellWidth::Narrow);
    cluster.setCluster(line.internCluster("á")); // a + 组合重音符，占 1 格
    line.setCell(1, cluster);
    FakeSource src(8, 0, {std::move(line)});
    const auto m = zzSearchLines(src, "á", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 1, 2);
    expectSelfConsistent(src, m, "á");
}

static void testMatchInsideSoftWrapChain()
{
    // "hello"+"world" 一条逻辑行；跨物理行边界的 "owo" 命中
    FakeSource src(5, 0, {makeLine(5, "hello", true), makeLine(5, "world", false)});
    const auto m = zzSearchLines(src, "owo", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 4, 7);
    expectSelfConsistent(src, m, "owo");
}

static void testNoCrossLogicalLineMatch()
{
    FakeSource src(8, 0, {makeLine(8, "ab", false), makeLine(8, "cd", false)});
    ZZ_TEST_EXPECT(zzSearchLines(src, "bc", ZzSearchOptions{}).empty());
}

static void testPatternWithNewlineNeverMatches()
{
    FakeSource src(8, 0, {makeLine(8, "ab", false), makeLine(8, "cd", false)});
    ZZ_TEST_EXPECT(zzSearchLines(src, "ab\ncd", ZzSearchOptions{}).empty());
}

static void testSeamChainMatch()
{
    // 历史末行 wrapped 续到屏幕首行（接缝链）：跨域命中
    FakeSource src(5, 1, {makeLine(5, "hello", true), makeLine(5, "world", false)});
    const auto m = zzSearchLines(src, "lowo", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 1);
    expectMatch(m[0], 0, 3, 7);
}

static void testEmptyPatternReturnsEmpty()
{
    FakeSource src(8, 0, {makeLine(8, "hello", false)});
    ZZ_TEST_EXPECT(zzSearchLines(src, "", ZzSearchOptions{}).empty());
}

static void testMatchesAcrossLogicalLinesSorted()
{
    FakeSource src(8, 0, {makeLine(8, "xax", false), makeLine(8, "yay", false)});
    const auto m = zzSearchLines(src, "a", ZzSearchOptions{});
    ZZ_TEST_EXPECT(m.size() == 2);
    expectMatch(m[0], 0, 1, 2);
    expectMatch(m[1], 1, 1, 2);
}

int main()
{
    testSingleMatch();
    testMultipleMatchesNonOverlapping();
    testCaseInsensitiveAscii();
    testWideCharRemap();
    testClusterRemap();
    testMatchInsideSoftWrapChain();
    testNoCrossLogicalLineMatch();
    testPatternWithNewlineNeverMatches();
    testSeamChainMatch();
    testEmptyPatternReturnsEmpty();
    testMatchesAcrossLogicalLinesSorted();
    if (g_failures == 0)
        std::printf("test_search: all passed\n");
    return g_failures;
}
