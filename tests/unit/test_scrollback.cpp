// ZzScrollback（chunked 实现）行为测试：append/裁剪/reflow/stats 记账（M4）。
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ZzTerm/Scrollback.h"

static int g_failures = 0;
#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

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

static std::string lineText(const ZzLine& line, int n)
{
    std::string s;
    for (int i = 0; i < n && i < line.cellCount(); ++i) {
        const ZzCell& c = line.cellAt(i);
        s += (char)(c.isEmpty() ? ' ' : c.codePoint());
    }
    return s;
}

// 1. 存量行为：append + 容量裁剪 + lineAt 顺序
static void testAppendAndTrim()
{
    auto sb = zzCreateChunkedScrollback(3);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "l1", false));
    batch.push_back(makeLine(4, "l2", false));
    batch.push_back(makeLine(4, "l3", false));
    batch.push_back(makeLine(4, "l4", false));
    sb->append(std::move(batch));
    ZZ_TEST_EXPECT(sb->lineCount() == 3);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "l2"); // 最旧的 l1 被裁
    ZZ_TEST_EXPECT(lineText(sb->lineAt(2), 2) == "l4");
    const ZzScrollbackStats st = sb->stats();
    ZZ_TEST_EXPECT(st.totalAppended == 4);
    ZZ_TEST_EXPECT(st.totalDropped == 1);
    ZZ_TEST_EXPECT(st.hotLines == 3);
    ZZ_TEST_EXPECT(st.approxBytes > 0);
}

// 2. 容量 0 不保留历史，记账照常
static void testZeroCapacity()
{
    auto sb = zzCreateChunkedScrollback(0);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "l1", false));
    sb->append(std::move(batch));
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    sb->reflow(2); // 空历史 reflow 为空操作，不崩
}

// 3. reflow 变窄重切历史行
static void testReflowNarrow()
{
    auto sb = zzCreateChunkedScrollback(100);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "abcd", true));
    batch.push_back(makeLine(4, "efgh", false));
    batch.push_back(makeLine(4, "tail", false));
    sb->append(std::move(batch));
    sb->reflow(2);
    // 链 abcd/efgh -> ab/cd/ef/gh（4 行）；tail -> ta/il（M16：硬行多行化 2 行链）
    ZZ_TEST_EXPECT(sb->lineCount() == 6);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "ab");
    ZZ_TEST_EXPECT(sb->lineAt(0).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(3), 2) == "gh");
    ZZ_TEST_EXPECT(!sb->lineAt(3).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(4), 2) == "ta");
    ZZ_TEST_EXPECT(sb->lineAt(4).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(5), 2) == "il");
    ZZ_TEST_EXPECT(!sb->lineAt(5).wrapped());
}

// 4. reflow 变宽合并 + 重组后超容量仍从最旧端裁
static void testReflowWidenAndTrim()
{
    auto sb = zzCreateChunkedScrollback(3); // 容量 3：append 不裁剪，保住完整 abcd 链
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(2, "ab", true));
    batch.push_back(makeLine(2, "cd", false));
    batch.push_back(makeLine(2, "xy", false));
    sb->append(std::move(batch));
    sb->reflow(4); // 链 abcd 合并 1 行 + xy 1 行 = 2 行，不超容量
    ZZ_TEST_EXPECT(sb->lineCount() == 2);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 4) == "abcd");
    ZZ_TEST_EXPECT(!sb->lineAt(0).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(1), 4) == "xy  ");
    const ZzScrollbackStats st = sb->stats();
    ZZ_TEST_EXPECT(st.totalAppended == 3);

    // 变窄重组后行数超容量：仍从最旧一端裁剪并计入 totalDropped
    auto sb2 = zzCreateChunkedScrollback(2);
    std::vector<ZzLine> batch2;
    batch2.push_back(makeLine(4, "abcd", true));
    batch2.push_back(makeLine(4, "efgh", false));
    sb2->append(std::move(batch2));
    sb2->reflow(2); // abcd/efgh -> ab/cd/ef/gh 4 行，超容量 2，裁最旧的 ab/cd
    ZZ_TEST_EXPECT(sb2->lineCount() == 2);
    ZZ_TEST_EXPECT(lineText(sb2->lineAt(0), 2) == "ef");
    ZZ_TEST_EXPECT(lineText(sb2->lineAt(1), 2) == "gh");
    ZZ_TEST_EXPECT(sb2->stats().totalDropped == 2);
}

// 5. clear 后累计记账不复位
static void testClearKeepsCounters()
{
    auto sb = zzCreateChunkedScrollback(10);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "l1", false));
    sb->append(std::move(batch));
    sb->clear();
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    ZZ_TEST_EXPECT(sb->stats().totalAppended == 1);
}

// 6. 部分裁剪后 lineAt 一致性（M8b 回归：trimToCapacity 头部块部分擦除曾破坏
// "除尾块外每块恰 256 行"的定长寻址不变量——块对齐破坏导致越界/错位读）。
static void testPartialTrimConsistency()
{
    char buf[8];
    auto sb = zzCreateChunkedScrollback(300); // 容量非 256 整数倍
    std::vector<ZzLine> batch;
    for (int i = 0; i < 600; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        batch.push_back(makeLine(8, buf, false));
    }
    sb->append(std::move(batch)); // 600 进 300：裁整块 256 + 部分 44
    ZZ_TEST_EXPECT(sb->lineCount() == 300);
    ZZ_TEST_EXPECT(sb->stats().totalDropped == 300);
    for (int i = 0; i < 300; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", 300 + i);
        ZZ_TEST_EXPECT(lineText(sb->lineAt((std::size_t)i), 5) == buf);
    }

    // 部分裁剪后继续 append 再触发一次部分裁剪，校验偏移记账持续正确
    std::vector<ZzLine> more;
    for (int i = 600; i < 700; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        more.push_back(makeLine(8, buf, false));
    }
    sb->append(std::move(more)); // 再裁 100（head 块 212 -> 112）
    ZZ_TEST_EXPECT(sb->lineCount() == 300);
    for (int i = 0; i < 300; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", 400 + i);
        ZZ_TEST_EXPECT(lineText(sb->lineAt((std::size_t)i), 5) == buf);
    }
}

// 7. 单块再填充回归（M8b T1 审查修复：容量 < 256 时头块同时是尾块，
// 部分裁剪后继续 append 会"再填充"同一块，开块条件须计入 headOffset_，
// 否则 lineAt 物理槽位超出唯一块而越界）。
static void testSingleChunkRefillAfterTrim()
{
    char buf[8];
    auto sb = zzCreateChunkedScrollback(100); // 容量 < 256：全程单块
    std::vector<ZzLine> batch;
    for (int i = 0; i < 200; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        batch.push_back(makeLine(8, buf, false));
    }
    sb->append(std::move(batch)); // 200 进 100：部分裁 100，headOffset_=100
    std::vector<ZzLine> more;
    for (int i = 200; i < 300; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        more.push_back(makeLine(8, buf, false));
    }
    sb->append(std::move(more)); // 再填充同一块，再裁 100
    ZZ_TEST_EXPECT(sb->lineCount() == 100);
    for (int i = 0; i < 100; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", 200 + i);
        ZZ_TEST_EXPECT(lineText(sb->lineAt((std::size_t)i), 5) == buf);
    }
}

// 8. reflow 重建切块 256 对齐回归（M8b T3 审查修复：单次 feed 产出可跨多个
// 256 槽位块，整块推送会破坏"除尾块外每块恰 256 行"的定长寻址不变量，
// lineAt 越过首块边界即错位/越界）。600 行编号历史含跨块 wrapped 链，
// 变窄/变宽往返后逐行校验精确期望文本与 wrapped 标记。
static void testReflowChunkedAlignment()
{
    char buf[8];
    auto sb = zzCreateChunkedScrollback(4096); // 容量充足：reflow 不触发裁剪
    std::vector<ZzLine> batch;
    for (int i = 0; i < 600; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        // 行 250-260 为一条跨 256 块边界的 wrapped 链（11 物理行），其余硬行。
        // M17a-4b：wrapped 行尾部空单元格恒为填充被逐行裁尾——链身行改用
        // 真实空格（0x20 非填充格）填满 8 列，85 格链流期望不变。
        const bool inChain = (i >= 250 && i <= 260);
        std::string text = buf;
        if (inChain && i < 260)
            text += "   ";
        batch.push_back(makeLine(8, text, inChain && i < 260));
    }
    sb->append(std::move(batch));
    ZZ_TEST_EXPECT(sb->lineCount() == 600);

    // 链内容流（85 格）：编号 5 字符 + 3 空白，末编号无尾空白；链跨块重组不丢格。
    std::string chainStream;
    for (int i = 250; i <= 260; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        chainStream += buf;
        if (i < 260)
            chainStream += "   ";
    }

    // 期望产出（M16 多行化）：8->4 时每条硬行（编号 5 字符有效内容）变 2 行链
    //（"L%04" wrapped + "d" 非 wrapped）；4->8 沿链合并恢复 "L%04d" 单行硬行。
    // 链流按 newCols 顺序切块（编号序列严格递增无缺无重），除链末行外 wrapped 全 true。
    auto expectedRows = [&](int newCols) {
        std::vector<std::pair<std::string, bool>> rows;
        for (int i = 0; i < 600; ++i) {
            if (i >= 250 && i <= 260) {
                if (i == 250)
                    for (std::size_t off = 0; off < chainStream.size();
                         off += (std::size_t)newCols)
                        rows.emplace_back(chainStream.substr(off, (std::size_t)newCols),
                                          off + (std::size_t)newCols < chainStream.size());
                continue;
            }
            std::snprintf(buf, sizeof(buf), "L%04d", i);
            const std::string t(buf); // "L%04d"，5 字符
            if (newCols == 4) {
                rows.emplace_back(t.substr(0, 4), true);  // "L%04" wrapped
                rows.emplace_back(t.substr(4, 1), false); // "d" 链末行
            } else {
                rows.emplace_back(t, false); // 往返恢复完整编号硬行
            }
        }
        return rows;
    };

    auto verifyAll = [&](const char* what, int newCols) {
        const auto rows = expectedRows(newCols);
        ZZ_TEST_EXPECT(sb->lineCount() == rows.size());
        const std::size_t n =
            sb->lineCount() < rows.size() ? sb->lineCount() : rows.size();
        for (std::size_t i = 0; i < n; ++i) {
            const ZzLine& line = sb->lineAt(i);
            ZZ_TEST_EXPECT(line.cellCount() == newCols); // 全历史同宽不变量
            if (lineText(line, newCols).substr(0, rows[i].first.size()) != rows[i].first
                || line.wrapped() != rows[i].second) {
                std::fprintf(stderr, "FAIL %s:%d: %s row %zu mismatch\n",
                             __FILE__, __LINE__, what, i);
                ++g_failures;
            }
        }
    };

    sb->reflow(4); // 变窄：单块 256 入 -> 远超 256 行出，产出跨多个 256 槽位块
    verifyAll("narrow 8->4", 4);
    sb->reflow(8); // 变宽往返：跨块链拼接 + 重建切块对齐
    verifyAll("widen 4->8", 8);
    ZZ_TEST_EXPECT(sb->stats().lineCount == sb->lineCount());
}

// 9. finish 冲刷的 dangling 尾链产出同样按 256 精确切块回归（M8b T3 修复轮 2：
// finish 产出原直接整块 push_back，超长 dangling 尾链 reflow 到窄列可产出
// 超 256 行尾块，破坏定长槽位不变量）。末尾 dangling wrapped 链 300 物理行
// 无后继，reflow 8->2 后链产出约 4 倍行数，lineAt 全扫精确期望校验。
static void testReflowDanglingTailChain()
{
    char buf[8];
    auto sb = zzCreateChunkedScrollback(4096); // 容量充足：reflow 不触发裁剪
    std::vector<ZzLine> batch;
    for (int i = 0; i < 300; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        batch.push_back(makeLine(8, buf, false)); // 历史主体：300 行编号硬行
    }
    for (int i = 300; i < 600; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        // 末尾 300 物理行为一条 dangling wrapped 链：最后一行 wrapped=true 无后继。
        // M17a-4b：同 testReflowChunkedAlignment——链身行用真实空格填满。
        std::string text = buf;
        if (i < 599)
            text += "   ";
        batch.push_back(makeLine(8, text, true));
    }
    sb->append(std::move(batch));
    ZZ_TEST_EXPECT(sb->lineCount() == 600);

    // 链内容流（2397 格）：编号 5 字符 + 3 空白，末编号无尾空白（链尾裁空白）。
    std::string chainStream;
    for (int i = 300; i < 600; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        chainStream += buf;
        if (i < 599)
            chainStream += "   ";
    }

    // 期望产出（8->2，M16 多行化）：每条编号硬行（5 字符有效内容）变 3 行链
    //（"L%" wrapped、"04" wrapped、"d" 非 wrapped）；链流按 2 列顺序切块
    //（编号序列严格递增无缺无重），除链末行外 wrapped 全 true。
    std::vector<std::pair<std::string, bool>> rows;
    for (int i = 0; i < 300; ++i) {
        std::snprintf(buf, sizeof(buf), "L%04d", i);
        const std::string t(buf);
        rows.emplace_back(t.substr(0, 2), true);
        rows.emplace_back(t.substr(2, 2), true);
        rows.emplace_back(t.substr(4, 1), false);
    }
    for (std::size_t off = 0; off < chainStream.size(); off += 2)
        rows.emplace_back(chainStream.substr(off, 2),
                          off + 2 < chainStream.size());

    sb->reflow(2);
    ZZ_TEST_EXPECT(sb->lineCount() == rows.size());
    const std::size_t n = sb->lineCount() < rows.size() ? sb->lineCount() : rows.size();
    for (std::size_t i = 0; i < n; ++i) {
        const ZzLine& line = sb->lineAt(i);
        ZZ_TEST_EXPECT(line.cellCount() == 2); // 全历史同宽不变量
        if (lineText(line, 2).substr(0, rows[i].first.size()) != rows[i].first
            || line.wrapped() != rows[i].second) {
            std::fprintf(stderr, "FAIL %s:%d: dangling-tail row %zu mismatch\n",
                         __FILE__, __LINE__, i);
            ++g_failures;
        }
    }
    ZZ_TEST_EXPECT(sb->stats().lineCount == sb->lineCount());
}

// 10. severNewestWrapped：斩断最新历史行出链（M17c）
static void testSeverNewestWrapped()
{
    auto sb = zzCreateChunkedScrollback(10);
    std::vector<ZzLine> batch;
    batch.push_back(makeLine(4, "l1", false));
    batch.push_back(makeLine(4, "l2", true)); // 末行带链标（接缝粘连场景）
    sb->append(std::move(batch));
    ZZ_TEST_EXPECT(sb->lineAt(1).wrapped());
    sb->severNewestWrapped();
    ZZ_TEST_EXPECT(!sb->lineAt(1).wrapped());     // 链标被斩
    ZZ_TEST_EXPECT(sb->lineCount() == 2);         // 行数/内容不动
    ZZ_TEST_EXPECT(lineText(sb->lineAt(1), 2) == "l2");
    const ZzScrollbackStats st = sb->stats();     // 统计记账不受影响
    ZZ_TEST_EXPECT(st.totalAppended == 2);
    ZZ_TEST_EXPECT(st.totalDropped == 0);
    sb->severNewestWrapped();                     // 幂等：已无链标再斩不崩
    ZZ_TEST_EXPECT(sb->lineCount() == 2);
}

static void testSeverNewestWrappedEmpty()
{
    auto sb = zzCreateChunkedScrollback(10);
    sb->severNewestWrapped(); // 空历史空操作，不崩
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
}

int main()
{
    testAppendAndTrim();
    testZeroCapacity();
    testReflowNarrow();
    testReflowWidenAndTrim();
    testClearKeepsCounters();
    testPartialTrimConsistency();
    testSingleChunkRefillAfterTrim();
    testReflowChunkedAlignment();
    testReflowDanglingTailChain();
    testSeverNewestWrapped();
    testSeverNewestWrappedEmpty();
    if (g_failures == 0)
        std::printf("test_scrollback: all passed\n");
    return g_failures;
}
