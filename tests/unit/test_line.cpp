// ZzLine 基础行为测试。
// 约定：每个 tests/unit/*.cpp 含 main()，失败返回非零。

#include <cstdio>

#include "ZzTerm/Line.h"

static int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void testLine()
{
    ZzLine line(10);
    ZZ_TEST_EXPECT(line.cellCount() == 10);
    ZZ_TEST_EXPECT(!line.wrapped());
    ZZ_TEST_EXPECT(line.cellAt(0).isEmpty());
    ZZ_TEST_EXPECT(line.cellAt(-1).isEmpty());   // 越界返回空单元格
    ZZ_TEST_EXPECT(line.cellAt(10).isEmpty());

    ZzCell a;
    a.setWidth(ZzCellWidth::Narrow);
    a.setCodePoint(U'a');
    line.setCell(3, a);
    ZZ_TEST_EXPECT(line.cellAt(3).codePoint() == U'a');
    line.setCell(-5, a); // 越界写入被忽略
    ZZ_TEST_EXPECT(line.cellAt(0).isEmpty());

    // 插入/删除单元格。
    line.insertCells(0, 2);
    // insertCells(0,2) 将 [0, 8) 右移到 [2, 10)，'a' 从 col 3 移到 col 5。
    ZZ_TEST_EXPECT(line.cellAt(2).isEmpty());
    ZZ_TEST_EXPECT(line.cellAt(5).codePoint() == U'a');
    line.eraseCells(0, 2);
    ZZ_TEST_EXPECT(line.cellAt(3).codePoint() == U'a');

    // soft wrap 标记与 cluster 侧表。
    line.setWrapped(true);
    ZZ_TEST_EXPECT(line.wrapped());
    const std::uint32_t idx = line.internCluster("中"); // 示例：多字节 UTF-8
    ZZ_TEST_EXPECT(line.clusterText(idx) == "中");
    ZZ_TEST_EXPECT(line.clusterText(999).empty());

    line.clear();
    ZZ_TEST_EXPECT(!line.wrapped());
    ZZ_TEST_EXPECT(line.cellAt(3).isEmpty());
}

int main()
{
    testLine();

    if (g_failures == 0) {
        std::fprintf(stderr, "test_line: PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_line: %d failure(s)\n", g_failures);
    return 1;
}
