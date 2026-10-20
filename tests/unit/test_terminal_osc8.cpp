// OSC 8 超链接：Screen 原语与端到端解析测试（M22）。
// 本任务（任务 3）只建骨架；OSC 端到端断言由任务 4 补全。
// 约定：每个 tests/unit/*.cpp 含 main()，失败返回非零。

#include <cstdio>
#include <span>
#include <string>

#include "ZzTerm/Terminal.h"

static int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                                  \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void feedStr(ZzTerminal& term, std::string_view s)
{
    term.feed(std::as_bytes(std::span{s.data(), s.size()}));
}

// 任务 6：契约默认值路径——无 OSC 8 时 hyperlinkId 恒 0、hyperlinkUri(0) 为空串。
static void testRenderViewHyperlinkDefaults()
{
    ZzTerminal term(20, 5, ZzBackendKind::Native, 100);
    feedStr(term, "ab");
    ZZ_TEST_EXPECT(term.renderView().lineAt(0).cellAt(0).hyperlinkId == 0);
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(0).empty());
}

int main()
{
    testRenderViewHyperlinkDefaults();
    // 任务 4 补全 OSC 8 端到端用例。
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
