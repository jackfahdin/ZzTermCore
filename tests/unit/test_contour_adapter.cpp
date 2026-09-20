// ZzContourBackendAdapter 经 ZzTerminal facade 的端到端测试（Contour 后端）。
#include <ZzTerm/Terminal.h>

#include <cassert>
#include <cstdio>
#include <string>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

// feed 聚合 ZzTermChanges + 统一视图读回 + title/output 通道。
void testFeedAndView()
{
    ZzTerminal term(80, 24, ZzBackendKind::Contour, 1000);
    std::string titleSeen;
    bool outputSeen = false;
    // title 经 ZzTermChanges 上报；output 经 setOutputHandler
    term.setOutputHandler([&outputSeen](std::string_view) { outputSeen = true; });

    auto changes = term.feed(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>("\x1b]0;T\x07" "Hi"), 8));
    ZZ_CHECK(changes.titleChanged);
    ZZ_CHECK(term.title() == "T");

    const ZzRenderView& view = term.renderView();
    ZZ_CHECK(view.size() == (ZzSize { 80, 24 }));
    ZZ_CHECK(!view.isAlternateScreen());
    const ZzLineView line = view.lineAt(0);
    ZZ_CHECK(line.cellAt(0).text == "H");
    ZZ_CHECK(line.cellAt(1).text == "i");
    ZZ_CHECK(line.cellAt(0).foreground == ZzColor::Default());

    // DA 回写经 output 通道（adapter 在 feed 末尾 flushReplies）。
    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x1b[c"), 3));
    ZZ_CHECK(outputSeen);
}

// 事件标志：bell、alt buffer、scrollback 差值；dirty 语义（有脏恒全行）。
void testChangesAndDirty()
{
    ZzTerminal term(80, 24, ZzBackendKind::Contour, 1000);
    auto c1 = term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x07"), 1));
    ZZ_CHECK(c1.bell);

    auto c2 = term.feed(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x1b[?1049h"), 8));
    ZZ_CHECK(c2.activeBufferChanged);
    ZZ_CHECK(term.isAlternateScreen());
    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x1b[?1049l"), 8));
    ZZ_CHECK(!term.isAlternateScreen());

    // Contour 逐行滚屏：scrolledOutLines 为单次 feed 差值（与 native 同语义），
    // 30 行进 24 行屏累计滚出 >= 6 行。
    std::size_t totalScrolled = 0;
    for (int i = 0; i < 30; ++i) {
        const std::string line = "L" + std::to_string(i) + "\r\n";
        auto c = term.feed(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(line.data()), line.size()));
        if (c.scrollbackChanged)
            totalScrolled += c.scrolledOutLines;
    }
    ZZ_CHECK(totalScrolled >= 6);

    const ZzRenderView& view = term.renderView();
    ZZ_CHECK(view.dirtyGeneration() > 0);
    ZZ_CHECK(view.rowDirty(0));                    // Contour：有脏恒全行脏
    ZZ_CHECK(view.dirtyRange(0).endCol == 80);     // dirtyRange 恒全行
    term.clearDirty();
    ZZ_CHECK(!view.rowDirty(0));                   // clearDirty 后无脏
    ZZ_CHECK(view.dirtyGeneration() > 0);          // 代际不清零（与 native 语义一致）
}

// resize 返回值语义（非正/相同 false）+ 光标可见性。
void testResizeAndCursor()
{
    ZzTerminal term(80, 24, ZzBackendKind::Contour, 1000);
    ZZ_CHECK(!term.resize(0, 24));
    ZZ_CHECK(!term.resize(80, 24));
    ZZ_CHECK(term.resize(100, 30));
    ZZ_CHECK(term.size() == (ZzSize { 100, 30 }));

    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("AB"), 2));
    ZZ_CHECK(term.cursor().visible);
    ZZ_CHECK(term.cursor().position.row == 0);
    ZZ_CHECK(term.cursor().position.col == 2);
    term.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>("\x1b[?25l"), 6));
    ZZ_CHECK(!term.cursor().visible);
}

// scrollback 饱和语义钉住（M1b 现状，契约见 ZzTermChanges::scrolledOutLines 注释）：
// 5 行屏 + scrollback 容量 5，喂 10 行实际滚出 6 行；Contour 以历史行数差值计数，
// 饱和后差值恒 0——累计停在 5、末次 feed 的 scrollbackChanged 不再置位。
//（native 同场景累计为 6，语义差已知悉，待后续统一。）
void testScrollbackSaturation()
{
    ZzTerminal term(80, 5, ZzBackendKind::Contour, 5);
    std::size_t totalScrolled = 0;
    bool lastScrollbackChanged = true;
    for (int i = 0; i < 10; ++i) {
        const std::string line = "L" + std::to_string(i) + "\r\n";
        auto c = term.feed(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(line.data()), line.size()));
        if (c.scrollbackChanged)
            totalScrolled += c.scrolledOutLines;
        lastScrollbackChanged = c.scrollbackChanged;
    }
    ZZ_CHECK(totalScrolled == 5);      // 饱和前的 5 行差值；第 6 行滚出不再计入
    ZZ_CHECK(!lastScrollbackChanged);  // 饱和后 scrollbackChanged 停报
}

} // namespace

int main()
{
    testFeedAndView();
    testChangesAndDirty();
    testResizeAndCursor();
    testScrollbackSaturation();
    if (g_failures != 0)
        std::fprintf(stderr, "test_contour_adapter: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
