// zzReflowLines 纯函数测试矩阵（M4 resize reflow）。
#include <algorithm>
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

// 2. 变窄重切：80 列 1 行硬行 -> 40 列 2 行链（M16：硬行多行化，内容不丢，往返恢复）
static void testNarrowHardLineMultiLines()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(80, std::string(80, 'a'), false));
    auto out = zzReflowLines(std::move(lines), 80, 40);
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(!out[1].wrapped());
    ZZ_TEST_EXPECT(lineText(out[0]) == std::string(40, 'a'));
    ZZ_TEST_EXPECT(lineText(out[1]) == std::string(40, 'a'));
    // 拉大往返：沿 wrapped 链合并，恢复单行硬行。
    auto back = zzReflowLines(std::move(out), 40, 80);
    ZZ_TEST_EXPECT(back.size() == 1);
    ZZ_TEST_EXPECT(!back[0].wrapped());
    ZZ_TEST_EXPECT(lineText(back[0]) == std::string(80, 'a'));
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

// 11. 硬行宽字符落新列宽边界：前移落下行（M16：多行化），内容完整、无续格泄漏
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
    // M16：宽字符不落边界（9 列行末仅剩 1 列）→ 前移落第 2 行，两行成链。
    ZZ_TEST_EXPECT(out.size() == 2);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(!out[1].wrapped());
    ZZ_TEST_EXPECT(out[0].cellCount() == 9);
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 8) == "abcdefgh");
    ZZ_TEST_EXPECT(out[0].cellAt(8).isEmpty()); // 边界补空白
    ZZ_TEST_EXPECT(out[1].cellAt(0).width() == ZzCellWidth::WideLead);
    ZZ_TEST_EXPECT(out[1].cellAt(0).codePoint() == 0x4E2D);
    ZZ_TEST_EXPECT(out[1].cellAt(1).width() == ZzCellWidth::WideContinuation);
    for (int i = 2; i < out[1].cellCount(); ++i)
        ZZ_TEST_EXPECT(out[1].cellAt(i).width() != ZzCellWidth::WideContinuation); // 无续格泄漏
}

// 12. 流式与一次性等价（M8b）：同一输入经 zzReflowLines 与 ZzReflowStreamer
//（整批 / 逐行 / 不规则分批三种喂法）产出必须逐行逐格一致。
// 覆盖：硬行、多长度 wrapped 链、宽字符跨边界、空链尾、dangling wrapped 尾链。

// 构造等价性用例输入：单行硬行 + 两条 2-3 物理行 wrapped 链（其一含宽字符
// 与颜色/属性格）+ dangling wrapped 尾链。所有行均为 cols 列。
static std::vector<ZzLine> makeStreamInput(int cols)
{
    std::vector<ZzLine> lines;
    // 单行硬行。
    lines.push_back(makeLine(cols, "hi", false));
    // wrapped 链 A：3 物理行，中间行含前景/背景/粗体格（非默认空白，不可裁）。
    lines.push_back(makeLine(cols, "ab", true));
    {
        ZzLine mid(cols);
        ZzCell c;
        c.setWidth(ZzCellWidth::Narrow);
        c.setCodePoint(U'Q');
        c.setForeground(ZzColor::Indexed(5));
        c.setBackground(ZzColor::Rgb(10, 20, 30));
        ZzCellAttributes attr;
        attr.setBold(true);
        c.setAttributes(attr);
        mid.setCell(0, c);
        mid.setWrapped(true);
        lines.push_back(std::move(mid));
    }
    lines.push_back(makeLine(cols, "cd", false));
    // wrapped 链 B：宽字符（WideLead 带颜色，续格随 lead 再生）+ 窄字符收尾。
    {
        ZzLine w0(cols);
        ZzCell lead;
        lead.setWidth(ZzCellWidth::WideLead);
        lead.setCodePoint(0x4E2D);
        lead.setForeground(ZzColor::Indexed(2));
        lead.setBackground(ZzColor::Indexed(7));
        w0.setCell(0, lead);
        if (cols >= 2) {
            ZzCell cont;
            cont.setWidth(ZzCellWidth::WideContinuation);
            cont.setForeground(ZzColor::Indexed(2));
            cont.setBackground(ZzColor::Indexed(7));
            w0.setCell(1, cont);
        }
        w0.setWrapped(true);
        lines.push_back(std::move(w0));
        lines.push_back(makeLine(cols, "z", false));
    }
    // dangling wrapped 尾链（最后一行 wrapped=true，无链尾）。
    lines.push_back(makeLine(cols, "xy", true));
    lines.push_back(makeLine(cols, "pq", true));
    return lines;
}

// 逐行逐格比较两组产出行：cellCount/wrapped/逐格 codePoint/width/
// foreground/background/attributes().raw()。
static void expectSameLines(const std::vector<ZzLine>& expected,
                            const std::vector<ZzLine>& actual, const char* what)
{
    ZZ_TEST_EXPECT(expected.size() == actual.size());
    const std::size_t n = std::min(expected.size(), actual.size());
    for (std::size_t i = 0; i < n; ++i) {
        const ZzLine& e = expected[i];
        const ZzLine& a = actual[i];
        ZZ_TEST_EXPECT(a.cellCount() == e.cellCount());
        ZZ_TEST_EXPECT(a.wrapped() == e.wrapped());
        const int cols = std::min(e.cellCount(), a.cellCount());
        for (int j = 0; j < cols; ++j) {
            const ZzCell& ec = e.cellAt(j);
            const ZzCell& ac = a.cellAt(j);
            if (ac.codePoint() != ec.codePoint() || ac.width() != ec.width()
                || !(ac.foreground() == ec.foreground())
                || !(ac.background() == ec.background())
                || ac.attributes().raw() != ec.attributes().raw()) {
                std::fprintf(stderr, "FAIL %s:%d: %s row %zu col %d mismatch\n",
                             __FILE__, __LINE__, what, i, j);
                ++g_failures;
            }
        }
    }
}

static void testStreamEquivalence()
{
    const int pairs[][2] = {{4, 2}, {2, 4}, {5, 3}};
    for (const auto& p : pairs) {
        const int oldCols = p[0];
        const int newCols = p[1];
        const std::vector<ZzLine> expected =
            zzReflowLines(makeStreamInput(oldCols), oldCols, newCols);
        // 喂法 1：整批一次喂入。
        {
            ZzReflowStreamer streamer(oldCols, newCols);
            std::vector<ZzLine> in = makeStreamInput(oldCols);
            std::vector<ZzLine> out;
            streamer.feed(in, out);
            streamer.finish(out);
            expectSameLines(expected, out, "whole-batch");
        }
        // 喂法 2：逐行喂入。
        {
            ZzReflowStreamer streamer(oldCols, newCols);
            std::vector<ZzLine> in = makeStreamInput(oldCols);
            std::vector<ZzLine> out;
            for (auto& line : in) {
                std::vector<ZzLine> one;
                one.push_back(std::move(line));
                streamer.feed(one, out);
            }
            streamer.finish(out);
            expectSameLines(expected, out, "line-by-line");
        }
        // 喂法 3：不规则分批（2/1/3 循环切片）。
        {
            ZzReflowStreamer streamer(oldCols, newCols);
            std::vector<ZzLine> in = makeStreamInput(oldCols);
            std::vector<ZzLine> out;
            const std::size_t pat[] = {2, 1, 3};
            std::size_t pos = 0;
            std::size_t pi = 0;
            while (pos < in.size()) {
                const std::size_t n = std::min(pat[pi++ % 3], in.size() - pos);
                std::vector<ZzLine> batch;
                for (std::size_t k = 0; k < n; ++k)
                    batch.push_back(std::move(in[pos + k]));
                pos += n;
                streamer.feed(batch, out);
            }
            streamer.finish(out);
            expectSameLines(expected, out, "irregular-batches");
        }
    }
}

// M17a-1：Preserve 扩列——光标链豁免收链，旧布局/旗标/光标行位保持
static void testPreserveCursorChainOnWiden()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(10, "ABCDEFGHIJ", true)); // 链 0 行 0
    lines.push_back(makeLine(10, "klm", false));       // 链 0 行 1（13 格）
    lines.push_back(makeLine(10, "xy", false));        // 链 1
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 11; // 片段 1 列 1（'l'）
    auto out = zzReflowLines(std::move(lines), 10, 20, &cur,
                             ZzReflowCursorChain::Preserve);
    ZZ_TEST_EXPECT(out.size() == 3); // 链豁免：2 行原样 + 链 1 一行
    ZZ_TEST_EXPECT(out[0].cellCount() == 20);
    ZZ_TEST_EXPECT(out[0].wrapped());
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 10) == "ABCDEFGHIJ");
    ZZ_TEST_EXPECT(!out[1].wrapped());
    ZZ_TEST_EXPECT(lineText(out[1]).substr(0, 3) == "klm");
    ZZ_TEST_EXPECT(lineText(out[2]).substr(0, 2) == "xy");
    ZZ_TEST_EXPECT(cur.row == 1); // 行位 = 链起点 0 + 偏移 11 / 10
    ZZ_TEST_EXPECT(cur.col == 1); // 列 = 偏移 11 % 10
}

// M17a-2：Preserve 缩列不豁免——照常重切（与 Reflow 逐点一致）
static void testPreserveIgnoredOnShrink()
{
    auto buildLines = [] {
        std::vector<ZzLine> lines;
        lines.push_back(makeLine(10, "ABCDEFGHIJ", true));
        lines.push_back(makeLine(10, "klm", false));
        lines.push_back(makeLine(10, "xy", false));
        return lines;
    };
    ZzReflowCursor curA; curA.chainIndex = 0; curA.chainOffset = 11;
    ZzReflowCursor curB = curA;
    auto outPreserve = zzReflowLines(buildLines(), 10, 5, &curA,
                                     ZzReflowCursorChain::Preserve);
    auto outReflow = zzReflowLines(buildLines(), 10, 5, &curB,
                                   ZzReflowCursorChain::Reflow);
    ZZ_TEST_EXPECT(outPreserve.size() == 4); // "ABCDE"(w) "FGHIJ"(w) "klm" "xy"
    ZZ_TEST_EXPECT(outPreserve.size() == outReflow.size());
    for (std::size_t i = 0; i < outPreserve.size(); ++i) {
        ZZ_TEST_EXPECT(lineText(outPreserve[i]) == lineText(outReflow[i]));
        ZZ_TEST_EXPECT(outPreserve[i].wrapped() == outReflow[i].wrapped());
    }
    ZZ_TEST_EXPECT(curA.row == 2 && curA.col == 1);
    ZZ_TEST_EXPECT(curA.row == curB.row && curA.col == curB.col);
}

// M17a-3：Preserve 无光标跟踪——豁免无对象，等同 Reflow，不崩
static void testPreserveWithoutCursor()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(10, "ABCDEFGHIJ", true));
    lines.push_back(makeLine(10, "klm", false));
    auto out = zzReflowLines(std::move(lines), 10, 20, nullptr,
                             ZzReflowCursorChain::Preserve);
    ZZ_TEST_EXPECT(out.size() == 1); // 13 格合 1 行（与 Reflow 一致）
    ZZ_TEST_EXPECT(lineText(out[0]).substr(0, 13) == "ABCDEFGHIJklm");
}

// M17a-7：Preserve 扩列后再缩列——补白不得污染逻辑链（往返守恒）
static void testPreservePaddingRoundTrip()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(5, "abcde", true));
    lines.push_back(makeLine(5, "fgh", false));
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 7; // 片段 1 列 2（'g'）
    auto grown = zzReflowLines(std::move(lines), 5, 10, &cur,
                               ZzReflowCursorChain::Preserve);
    ZZ_TEST_EXPECT(grown.size() == 2); // 豁免保持 2 行
    ZZ_TEST_EXPECT(cur.row == 1 && cur.col == 2);
    // 再缩回 5 列（Reflow 模式）：逻辑链必须仍是 "abcdefgh" 8 格
    // 勘误 E-4：chainOffset 以当前列宽为单位，调用方（Screen::reflowBuffer）
    // 每次 reflow 按 cols_ 重算——豁免后光标 (1,2)，10 列单位下应为 1*10+2=12；
    // 简报沿用 5 列单位的 7，会被解读为片段 0 列 7（补白区）而锚到片段 0 内容尾。
    cur.chainOffset = 12;
    auto shrunk = zzReflowLines(std::move(grown), 10, 5, &cur,
                                ZzReflowCursorChain::Reflow);
    ZZ_TEST_EXPECT(shrunk.size() == 2);
    ZZ_TEST_EXPECT(lineText(shrunk[0]).substr(0, 5) == "abcde");
    ZZ_TEST_EXPECT(shrunk[0].wrapped());
    ZZ_TEST_EXPECT(lineText(shrunk[1]).substr(0, 3) == "fgh");
    ZZ_TEST_EXPECT(!shrunk[1].wrapped());
    ZZ_TEST_EXPECT(cur.row == 1 && cur.col == 2); // 'g' 行位不变
}

// M17a-8：Preserve 扩列后再次扩列（光标已离链）——合并结果无补白
static void testPreserveThenMergeNoPadding()
{
    std::vector<ZzLine> lines;
    lines.push_back(makeLine(5, "abcde", true));
    lines.push_back(makeLine(5, "fgh", false));
    ZzReflowCursor cur;
    cur.chainIndex = 0;
    cur.chainOffset = 7;
    auto grown = zzReflowLines(std::move(lines), 5, 10, &cur,
                               ZzReflowCursorChain::Preserve);
    // 光标链变成另一链（模拟用户换了输入行）：本链走 Reflow 合并
    cur.chainIndex = 99; // 不在任何链上（不跟踪本链）
    auto merged = zzReflowLines(std::move(grown), 10, 20, &cur,
                                ZzReflowCursorChain::Reflow);
    ZZ_TEST_EXPECT(merged.size() == 1);
    ZZ_TEST_EXPECT(lineText(merged[0]).substr(0, 8) == "abcdefgh"); // 无补白洞
}

int main()
{
    testWidenMergesChain();
    testNarrowHardLineMultiLines();
    testNarrowResplitsChain();
    testNoPhantomRows();
    testWideCharBoundaryClamp();
    testCursorTracking();
    testCursorOnContinuation();
    testBlankLines();
    testClusterReintern();
    testCursorInTrimmedBlanks();
    testHardLineWideCharAtBoundary();
    testStreamEquivalence();
    testPreserveCursorChainOnWiden();
    testPreserveIgnoredOnShrink();
    testPreserveWithoutCursor();
    testPreservePaddingRoundTrip();
    testPreserveThenMergeNoPadding();
    if (g_failures == 0)
        std::printf("test_reflow: all passed\n");
    return g_failures;
}
