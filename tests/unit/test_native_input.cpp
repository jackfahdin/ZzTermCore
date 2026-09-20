// native 输入链路集成测试（M3a）：facade send → encoder → output 通道；
// DECCKM(?1) 模式经 feed 同步到编码；handler 未设不崩。仅公开 API。
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

void feed(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()),
                                      bytes.size()));
}

ZzKeyEvent keyEvent(ZzKeyEvent::Key key)
{
    ZzKeyEvent ev;
    ev.key = key;
    return ev;
}

// sendText/sendKey 经 output handler 发出；功能键编码正确。
void testSendThroughOutputHandler()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    t.sendText("hi");
    ZZ_CHECK(out == "hi");
    out.clear();
    t.sendKey(keyEvent(ZzKeyEvent::Key::Up));
    ZZ_CHECK(out == "\x1B[A");
    out.clear();
    t.sendKey(keyEvent(ZzKeyEvent::Key::F1));
    ZZ_CHECK(out == "\x1BOP");
}

// DECCKM：feed ?1h 后方向键切 SS3，?1l 切回（模式位经 CSI 同步到 encoder）。
void testDecckmSync()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string out;
    t.setOutputHandler([&](std::string_view b) { out.append(b); });
    feed(t, "\x1B[?1h");
    t.sendKey(keyEvent(ZzKeyEvent::Key::Up));
    ZZ_CHECK(out == "\x1BOA");
    out.clear();
    feed(t, "\x1B[?1l");
    t.sendKey(keyEvent(ZzKeyEvent::Key::Up));
    ZZ_CHECK(out == "\x1B[A");
}

// handler 未设置：send 与回传静默丢弃，不崩。
void testNoHandlerSafe()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    t.sendText("x");
    t.sendKey(keyEvent(ZzKeyEvent::Key::Up));
    feed(t, "\x1B[?1h"); // 模式切换也不依赖 handler
    ZZ_CHECK(true);      // 到达此处即未崩
}

// handler 可替换：新 handler 生效，旧 handler 不再收到。
void testHandlerReplaceable()
{
    ZzTerminal t(10, 4, ZzBackendKind::Native, 0);
    std::string a, b;
    t.setOutputHandler([&](std::string_view s) { a.append(s); });
    t.sendText("1");
    t.setOutputHandler([&](std::string_view s) { b.append(s); });
    t.sendText("2");
    ZZ_CHECK(a == "1");
    ZZ_CHECK(b == "2");
}

} // namespace

int main()
{
    testSendThroughOutputHandler();
    testDecckmSync();
    testNoHandlerSafe();
    testHandlerReplaceable();
    if (g_failures != 0)
        std::fprintf(stderr, "test_native_input: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
