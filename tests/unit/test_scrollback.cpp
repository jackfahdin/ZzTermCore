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
    // 链 abcd/efgh -> ab/cd/ef/gh（4 行），tail -> ta（硬行截断）
    ZZ_TEST_EXPECT(sb->lineCount() == 5);
    ZZ_TEST_EXPECT(lineText(sb->lineAt(0), 2) == "ab");
    ZZ_TEST_EXPECT(sb->lineAt(0).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(3), 2) == "gh");
    ZZ_TEST_EXPECT(!sb->lineAt(3).wrapped());
    ZZ_TEST_EXPECT(lineText(sb->lineAt(4), 2) == "ta");
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
        const bool inChain = (i >= 250 && i <= 260);
        batch.push_back(makeLine(8, buf, inChain && i < 260));
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

    // 期望产出：硬行文本 = 编号前 4 字符（8->4 截断，4->8 往返后内容不变），
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
            std::string t(buf);
            t.resize(4); // 变窄截断后内容，往返两阶段一致
            rows.emplace_back(std::move(t), false);
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
    if (g_failures == 0)
        std::printf("test_scrollback: all passed\n");
    return g_failures;
}
