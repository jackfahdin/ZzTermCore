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

int main()
{
    // 任务 4 补全 OSC 8 端到端用例。
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
