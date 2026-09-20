// native 终端回传（M3a）：DA1、DSR 5n、CPR 6n 经 output 通道应答。仅公开 API。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

ZzTermChanges feed(ZzTerminal& t, std::string_view bytes)
{
    return t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                             bytes.size()));
}

// DA1（CSI c）：应答 VT102 级最小集；回传不标脏。
void testDa1()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    const ZzTermChanges ch = feed(t, "\x1B[c");
    ZZ_CHECK(out == "\x1B[?1;2c");
    ZZ_CHECK(!ch.screenDirty); // 回传不标脏
}

// DSR 5n：就绪应答。
void testDsr5()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "\x1B[5n");
    ZZ_CHECK(out == "\x1B[0n");
}

// CPR 6n：应答真实光标位置（1 起始）；写入移动光标后应答跟随。
void testCpr()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "AB");
    feed(t, "\x1B[6n");
    ZZ_CHECK(out == "\x1B[1;3R"); // 行 1 列 3（0 起始 (0,2) 换算）
    out.clear();
    feed(t, "\x1B[3;5H");         // 光标到行 3 列 5（1 起始）
    feed(t, "\x1B[6n");
    ZZ_CHECK(out == "\x1B[3;5R");
}

// 未知 DSR 参数安全忽略（无应答、不崩）。
void testUnknownDsrIgnored()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "\x1B[7n");
    ZZ_CHECK(out.empty());
}

} // namespace

int main()
{
    testDa1();
    testDsr5();
    testCpr();
    testUnknownDsrIgnored();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_replies: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
