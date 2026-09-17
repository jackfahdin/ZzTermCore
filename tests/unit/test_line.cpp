// ZzLine / ZzTerminal(M0 占位 feed) 基础行为测试。
// 约定：每个 tests/unit/*.cpp 含 main()，失败返回非零。

#include <cstdio>
#include <cstring>

#include "ZzTerm/Line.h"
#include "ZzTerm/Terminal.h"

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

static void testTerminalFeed()
{
    ZzTerminal term(10, 4, 100);
    const char* text = "hi";
    ZzTermChanges changes = term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text), std::strlen(text)));
    ZZ_TEST_EXPECT(changes.screenDirty);

    const ZzRenderView& view = term.renderView();
    ZZ_TEST_EXPECT((view.size() == ZzSize{10, 4}));
    ZZ_TEST_EXPECT(view.cellAt(0, 0).codePoint() == U'h');
    ZZ_TEST_EXPECT(view.cellAt(0, 1).codePoint() == U'i');
    ZZ_TEST_EXPECT((view.cursor().position == ZzPosition{0, 2}));
    ZZ_TEST_EXPECT(view.dirtyGeneration() > 0);

    // 行末 soft wrap：写满 10 列后第 11 个字符换到下一行。
    const char* fill = "0123456789AB";
    term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(fill), std::strlen(fill)));
    // 光标此时在第 2 行（"hi01234567" 填满第 0 行后 wrap，"89AB" 落在第 1 行）。
    ZZ_TEST_EXPECT(term.screen().lineAt(0).wrapped());
    ZZ_TEST_EXPECT(term.cursor().position.row == 1);
    ZZ_TEST_EXPECT(term.cursor().position.col == 4);

    // 滚动：填满 4 行后继续换行应滚入历史。
    const char* scroll = "\n\n\n\n\n\n";
    term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(scroll), std::strlen(scroll)));
    ZZ_TEST_EXPECT(term.scrollback().lineCount() > 0);

    // Dirty 复位。
    term.clearDirty();
    ZZ_TEST_EXPECT(view.dirtyRows().empty());

    // resize 合法性与非法值拒绝。
    ZZ_TEST_EXPECT(term.resize(20, 6));
    ZZ_TEST_EXPECT((term.size() == ZzSize{20, 6}));
    ZZ_TEST_EXPECT(!term.resize(0, -1));
    ZZ_TEST_EXPECT((term.size() == ZzSize{20, 6}));
}

int main()
{
    testLine();
    testTerminalFeed();

    if (g_failures == 0) {
        std::fprintf(stderr, "test_line_terminal: PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_line_terminal: %d failure(s)\n", g_failures);
    return 1;
}
