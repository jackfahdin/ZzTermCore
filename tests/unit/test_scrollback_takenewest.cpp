// ZzScrollback::takeNewest 单测（M15）：基本取行顺序、跨块、超取与取空、
// stats 语义（回抽非裁剪）、headOffset 复位。
#include <cstdio>
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

namespace {

// 构造 cols 列、首格码位为 cp 的行（cp 须非 0，0x100 + i 避让）。
ZzLine makeLine(int cols, char32_t cp)
{
    ZzLine line;
    line.resize(cols);
    ZzCell cell;
    cell.setWidth(ZzCellWidth::Narrow);
    cell.setCodePoint(cp);
    line.setCell(0, cell);
    return line;
}

std::vector<ZzLine> makeBatch(int cols, int count, char32_t base)
{
    std::vector<ZzLine> batch;
    for (int i = 0; i < count; ++i)
        batch.push_back(makeLine(cols, base + static_cast<char32_t>(i)));
    return batch;
}

} // namespace

static void testBasicOrderAndCount()
{
    auto sb = zzCreateChunkedScrollback(100);
    sb->append(makeBatch(10, 5, 0x100)); // a..e（码位 0x100..0x104）
    auto taken = sb->takeNewest(2);
    ZZ_TEST_EXPECT(taken.size() == 2);
    ZZ_TEST_EXPECT(taken[0].cellAt(0).codePoint() == 0x103); // 旧到新：d 在前
    ZZ_TEST_EXPECT(taken[1].cellAt(0).codePoint() == 0x104); // e 在后
    ZZ_TEST_EXPECT(sb->lineCount() == 3);
    ZZ_TEST_EXPECT(sb->lineAt(2).cellAt(0).codePoint() == 0x102); // 留存最新为 c
}

static void testCrossChunk()
{
    auto sb = zzCreateChunkedScrollback(1000);
    sb->append(makeBatch(10, 300, 0x100)); // 跨块（256 + 44）
    auto taken = sb->takeNewest(50);       // 尾块 44 行 + 前块 6 行
    ZZ_TEST_EXPECT(taken.size() == 50);
    ZZ_TEST_EXPECT(taken[0].cellAt(0).codePoint() == 0x100 + 250); // 全局旧到新
    ZZ_TEST_EXPECT(taken[49].cellAt(0).codePoint() == 0x100 + 299);
    ZZ_TEST_EXPECT(sb->lineCount() == 250);
    ZZ_TEST_EXPECT(sb->lineAt(0).cellAt(0).codePoint() == 0x100);
    ZZ_TEST_EXPECT(sb->lineAt(249).cellAt(0).codePoint() == 0x100 + 249);
}

static void testTakeMoreThanAvailableAndEmpty()
{
    auto sb = zzCreateChunkedScrollback(100);
    sb->append(makeBatch(10, 5, 0x100));
    auto taken = sb->takeNewest(10); // 超取：全部返回
    ZZ_TEST_EXPECT(taken.size() == 5);
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    ZZ_TEST_EXPECT(sb->takeNewest(3).empty()); // 取空
    // 取空后再 append：寻址正常（headOffset_ 复位路径）
    sb->append(makeBatch(10, 1, 0x1FF));
    ZZ_TEST_EXPECT(sb->lineCount() == 1);
    ZZ_TEST_EXPECT(sb->lineAt(0).cellAt(0).codePoint() == 0x1FF);
}

static void testStatsUnchanged()
{
    auto sb = zzCreateChunkedScrollback(100);
    sb->append(makeBatch(10, 5, 0x100));
    const auto before = sb->stats();
    sb->takeNewest(2);
    const auto after = sb->stats();
    ZZ_TEST_EXPECT(after.totalAppended == before.totalAppended); // 只增计数不动
    ZZ_TEST_EXPECT(after.totalDropped == before.totalDropped);   // 回抽非裁剪
    ZZ_TEST_EXPECT(after.lineCount == 3);
    ZZ_TEST_EXPECT(after.approxBytes < before.approxBytes);      // 字节数按行扣减
}

static void testHeadOffsetReset()
{
    auto sb = zzCreateChunkedScrollback(3); // 容量 3，触发部分裁剪产生 headOffset_
    sb->append(makeBatch(10, 5, 0x100));    // 留存 c,d,e；headOffset_=2
    ZZ_TEST_EXPECT(sb->stats().totalDropped == 2);
    auto taken = sb->takeNewest(3); // 全取空（单块且带 headOffset_）
    ZZ_TEST_EXPECT(taken.size() == 3);
    ZZ_TEST_EXPECT(taken[0].cellAt(0).codePoint() == 0x102); // c
    ZZ_TEST_EXPECT(sb->lineCount() == 0);
    sb->append(makeBatch(10, 2, 0x200)); // 复位后再 append：寻址正常
    ZZ_TEST_EXPECT(sb->lineCount() == 2);
    ZZ_TEST_EXPECT(sb->lineAt(0).cellAt(0).codePoint() == 0x200);
    ZZ_TEST_EXPECT(sb->lineAt(1).cellAt(0).codePoint() == 0x201);
}

int main()
{
    testBasicOrderAndCount();
    testCrossChunk();
    testTakeMoreThanAvailableAndEmpty();
    testStatsUnchanged();
    testHeadOffsetReset();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
