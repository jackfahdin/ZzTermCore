// zzReflowLines 纯函数测试矩阵（M4 resize reflow）。
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/screen/Reflow.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// 造一行：text 为 ASCII 串，按 cols 补空；wrapped 置链标记。
static ZzLine makeLine(int cols, const std::string& text, bool wrapped)
{
    ZzLine line(cols);
    for (int i = 0; i < (int)text.size() && i < cols; ++i) {
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint((char32_t)text[(std::size_t)i]);
        line.setCell(i, c);
    }
    line.setWrapped(wrapped);
    return line;
}

// 提取行的可见文本（逐格码位，空格调出空格）。
static std::string lineText(const ZzLine& line)
{
    std::string s;
    for (int i = 0; i < line.cellCount(); ++i) {
        const ZzCell& c = line.cellAt(i);
        s += (c.width() == ZzCellWidth::WideContinuation) ? '\0' // 续格占位，调用方注意
             : (char)(c.isEmpty() ? ' ' : c.codePoint());
    }
    return s;
}

// 1. 变宽合并：80 列 3 行链 -> 120 列 2 行
static void testWidenMergesChain()
{
    std::vector<ZzLine> lines;
    std::string a(80, 'a'), b(80, 'b'), c(40, 'c');
    lines.push_back(makeLine(80, a, true));
    lines.push_back(makeLine(80, b, true));
    lines.push_back(makeLine(80, c, false));
    auto out = zzReflowLines(std::move(lines), 80, 120);
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(!out[1].wrapped());
    ZZ_TEST_EXPECT(out[0].cellCount() == 120);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 80) == a);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(80, 40) == b.substr(0, 40));
    ZZ_TEST_EXPECT(lineText(out[1]).substr(40, 40) == c);
}

// 2. 变窄重切：80 列 1 行硬行 -> 40 列截断（硬行不多行化）
static void testNarrowTruncatesHardLine()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, std::string(80, 'a'), false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 1);
    ZZ_TEST_EXPECT(!out[0].wrapped());
    ZZ_TEST_EXPECT(lineText(out[0]) == std::string(40, 'a'));
}

// 3. 变窄重切：wrapped 链 80x2 -> 40 列 4 行
static void testNarrowResplitsChain()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, std::string(80, 'a'), true));
    lines.push_back(makeLine(80, std::string(50, 'b'), false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 4);
    for (std::size_t i = 0; i + 1 < out.size(); ++i)
        ZZ_TEST_EXPECT(out[i].wrapped());
    ZZ_TEST_EXPECT(!out.back().wrapped());
    ZZ_TEST_EXPECT(lineText(out[3]).substr(0, 10) == std::string(10, 'b'));
}

// 4. 短行变窄不产生幽灵行（尾部默认空白被裁）
static void testNoPhantomRows()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, "hello", false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 1);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 5) == "hello");
}

// 5. 宽字符边界钳制：宽字符不拆半，落边界时前移一格
static void testWideCharBoundaryClamp()
{
    // 10 列行：4 个窄字符 + 3 个宽字符（占 6 列），链两行。
    std::vector<ZzLine> lines;
    ZzLine l0(10);
    for (int i = 0; i < 4; ++i) {
        ZzCell c; c.setWidth(ZzCellWidth::Narrow); c.setCodePoint(U'a' + i);
        l0.setCell(i, c);
    }
    for (int i = 0; i < 3; ++i) {
        ZzCell lead; lead.setWidth(ZzCellWidth::WideLead); lead.setCodePoint(char32_t(0x4E2D + i));
        ZzCell cont; cont.setWidth(ZzCellWidth::WideContinuation);
        l0.setCell(4 + i * 2, lead);
        l0.setCell(5 + i * 2, cont);
    }
    l0.setWrapped(true);
    ZzLine l1(10); // 链末行：2 个窄字符
    for (int i = 0; i < 2; ++i) {
        ZzCell c; c.setWidth(ZzCellWidth::Narrow); c.setCodePoint(U'x' + i);
        l1.setCell(i, c);
    }
    lines.push_back(std::move(l0));
    lines.push_back(std::move(l1));
    // 重切到 5 列：'abcd' + 宽字符1(2列) => 第一行 abcd+空白? 宽字符1 不落第5列(剩1列不足) -> 前移
    auto out = zzReflowLines(std::move(lines), 10, 5);
    // 内容流：abcd 中 中 中 xy（宽字符各2列）=> 行0: abcd + 空白(宽字符前移)
    // 行1: 中1 中2 空白(中3前移)  行2: 中3 xy
    ZZ_TEST_EXPECT(out.size() == 3);
    ZZ_TEST_EXPECT(out[0].cellCount() == 5);
    ZZ_TEST_EXPECT(out[0].cellAt(4).isEmpty()); // 边界补空白
    ZZ_TEST_EXPECT(out[1].cellAt(0).width() == ZzCellWidth::WideLead);
    ZZ_TEST_EXPECT(out[1].cellAt(0).codePoint() == 0x4E2D);
    ZZ_TEST_EXPECT(out[1].cellAt(1).width() == ZzCellWidth::WideContinuation);
    ZZ_TEST_EXPECT(out[1].cellAt(4).isEmpty()); // 第二个边界补空白
    ZZ_TEST_EXPECT(out[2].cellAt(0).codePoint() == 0x4E2F);
    ZZ_TEST_EXPECT(out[2].cellAt(2).codePoint() == U'x');
    ZZ_TEST_EXPECT(!out[2].wrapped());
}

// 6. 光标跟踪：链内偏移映射
static void testCursorTracking()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, std::string(80, 'a'), true));
    lines.push_back(makeLine(80, std::string(80, 'b'), false));
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 100; // 第二物理行第 20 列
    auto out = zzReflowLines(std::move(lines), 80, 40, &cur);
    ZZ_TEST_EXPECT(out.size() == 4);
    ZZ_TEST_EXPECT(cur.row == 2);  // 偏移 100 -> 40 列下第 2 行
    ZZ_TEST_EXPECT(cur.col == 20); // 100 % 40 = 20
}

// 7. 光标偏移指向续格时记到 lead 位置
static void testCursorOnContinuation()
{
    std::vector<ZzLine> lines;
    ZzLine l0(4);
    ZzCell lead; lead.setWidth(ZzCellWidth::WideLead); lead.setCodePoint(0x4E2D);
    ZzCell cont; cont.setWidth(ZzCellWidth::WideContinuation);
    l0.setCell(0, lead);
    l0.setCell(1, cont);
    lines.push_back(std::move(l0));
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 1; // 续格偏移
    auto out = zzReflowLines(std::move(lines), 4, 8, &cur);
    ZZ_TEST_EXPECT(cur.row == 0 && cur.col == 0); // 归到 lead
}

// 8. 空行与全空白行
static void testBlankLines()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, "", false));
    lines.push_back(makeLine(80, "tail", false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(lineText(out[1]).substr(0, 4) == "tail");
}

// 9. cluster 跨行搬运重新登记
static void testClusterReintern()
{
    std::vector<ZzLine> lines;
    ZzLine l0(4);
    ZzCell c;
    c.setWidth(ZzCellWidth::Narrow);
    c.setCluster(l0.internCluster("e\xCC\x81")); // e + 组合重音符
    l0.setCell(3, c); // 放行尾，变窄后应被挤到下一行
    lines.push_back(std::move(l0));
    ZzLine l1(4);
    ZzCell x; x.setWidth(ZzCellWidth::Narrow); x.setCodePoint(U'z');
    l1.setCell(0, x);
    lines.push_back(std::move(l1));
    lines[0].setWrapped(true);
    auto out = zzReflowLines(std::move(lines), 4, 2);
    // 链内容：空空空cluster z -> 2 列重切：行0 空空 / 行1 空+cluster / 行2 z
    ZZ_TEST_EXPECT(out.size() == 3);
    ZZ_TEST_EXPECT(out[1].cellAt(1).isCluster());
    ZZ_TEST_EXPECT(out[1].clusterText(out[1].cellAt(1).clusterIndex()) == "e\xCC\x81");
}

// 10. 光标落在被裁空白区的兜底
static void testCursorInTrimmedBlanks()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, "hi", false));
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 79; // 尾部空白区，变窄后被裁
    auto out = zzReflowLines(std::move(lines), 80, 40, &cur);
    ZZ_TEST_EXPECT(out.size() == 1);
    ZZ_TEST_EXPECT(cur.row == 0);
    ZZ_TEST_EXPECT(cur.col >= 0 && cur.col < 40); // 兜底到链内容尾/clamp
}

// 11. 硬行宽字符落新列宽边界：整体截断丢弃，不多行化、无续格泄漏
static void testHardLineWideCharAtBoundary()
{
    // 10 列硬行：8 个窄字符 + 第 8 列 WideLead（占 8-9 两列），reflow 到 9 列。
    std::vector<ZzLine> lines;
    ZzLine l0(10);
    for (int i = 0; i < 8; ++i) {
        ZzCell c; c.setWidth(ZzCellWidth::Narrow); c.setCodePoint(U'a' + i);
        l0.setCell(i, c);
    }
    ZzCell lead; lead.setWidth(ZzCellWidth::WideLead); lead.setCodePoint(0x4E2D);
    ZzCell cont; cont.setWidth(ZzCellWidth::WideContinuation);
    l0.setCell(8, lead);
    l0.setCell(9, cont);
    lines.push_back(std::move(l0));
    auto out = zzReflowLines(std::move(lines), 10, 9);
    ZZ_TEST_EXPECT(out.size() == 1);        // 硬行永不多行化
    ZZ_TEST_EXPECT(!out[0].wrapped());
    ZZ_TEST_EXPECT(out[0].cellCount() == 9);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 8) == "abcdefgh");
    ZZ_TEST_EXPECT(out[0].cellAt(8).isEmpty()); // 宽字符整体丢弃，边界补空白
    for (int i = 0; i < out[0].cellCount(); ++i)
        ZZ_TEST_EXPECT(out[0].cellAt(i).width() != ZzCellWidth::WideContinuation); // 无续格泄漏
}

int main()
{
    testWidenMergesChain();
    testNarrowTruncatesHardLine();
    testNarrowResplitsChain();
    testNoPhantomRows();
    testWideCharBoundaryClamp();
    testCursorTracking();
    testCursorOnContinuation();
    testBlankLines();
    testClusterReintern();
    testCursorInTrimmedBlanks();
    testHardLineWideCharAtBoundary();
    if (g_failures == 0)
        std::printf("test_reflow: all passed\n");
    return g_failures;
}
