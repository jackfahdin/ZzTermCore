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

static std::uint16_t linkAt(const ZzTerminal& term, int row, int col)
{
    return term.renderView().lineAt(row).cellAt(col).hyperlinkId;
}

static void testOsc8Basic()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;;https://example.com\x1b\\link\x1b]8;;\x1b\\ tail");
    // "link" 4 格带链接，id != 0 且互相相等
    const std::uint16_t id = linkAt(term, 0, 0);
    ZZ_TEST_EXPECT(id != 0);
    for (int c = 1; c < 4; ++c)
        ZZ_TEST_EXPECT(linkAt(term, 0, c) == id);
    // 关闭后空格与 tail 无链接
    ZZ_TEST_EXPECT(linkAt(term, 0, 4) == 0);
    ZZ_TEST_EXPECT(linkAt(term, 0, 6) == 0);
    // URI 可查
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(id) == "https://example.com");
}

static void testOsc8BelTerminated()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;;https://bel.example\x07x\x1b]8;;\x07");
    ZZ_TEST_EXPECT(linkAt(term, 0, 0) != 0);
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(linkAt(term, 0, 0)) == "https://bel.example");
}

static void testOsc8IdAssociation()
{
    ZzTerminal term(80, 5, ZzBackendKind::Native, 100);
    // 同 uri+id 两段（模拟折行链接的两段）：必须同 hyperlinkId
    feedStr(term, "\x1b]8;id=u1;https://a.example\x1b\\part1 \x1b]8;;\x1b\\");
    feedStr(term, "\x1b]8;id=u1;https://a.example\x1b\\part2\x1b]8;;\x1b\\");
    const std::uint16_t id1 = linkAt(term, 0, 0);
    const std::uint16_t id2 = linkAt(term, 0, 6);
    ZZ_TEST_EXPECT(id1 != 0 && id1 == id2);
    // 同 uri 不同 id：不同 hyperlinkId
    feedStr(term, "\x1b]8;id=u2;https://a.example\x1b\\part3\x1b]8;;\x1b\\");
    ZZ_TEST_EXPECT(linkAt(term, 0, 12) != id1);
}

static void testOsc8UnknownParamsIgnored()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;foo=bar:id=z:zz=1;https://p.example\x1b\\ab\x1b]8;;\x1b\\");
    ZZ_TEST_EXPECT(linkAt(term, 0, 0) != 0);
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(linkAt(term, 0, 0)) == "https://p.example");
}

static void testOsc8WideChar()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;;https://w.example\x1b\\中\x1b]8;;\x1b\\");
    const std::uint16_t id = linkAt(term, 0, 0);
    ZZ_TEST_EXPECT(id != 0);
    ZZ_TEST_EXPECT(linkAt(term, 0, 1) == id); // 宽字符续格同链接
}

static void testOsc8Malformed()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    // URI 含控制字符：parser 层按 xterm/contour 钉住的行为剥除 OSC 内 C0/DEL，
    // 语义层收到的已是净 URI（https://bad.example），链接成立；
    // dispatchOsc8Hyperlink 的 <0x20 整拒为纵深防御（feed 路径不可达）。
    feedStr(term, "\x1b]8;;https://bad\x01.example\x1b\\zz\x1b]8;;\x1b\\");
    ZZ_TEST_EXPECT(linkAt(term, 0, 0) != 0);
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(linkAt(term, 0, 0)) == "https://bad.example");
    // 缺第二个分号：忽略
    feedStr(term, "\x1b]8;noSecondSep\x1b\\q\x1b]8;;\x1b\\");
    ZZ_TEST_EXPECT(linkAt(term, 0, 3) == 0);
}

// 历史路径：链接行滚出屏幕后，ZzNativeHistoryView 的 hyperlinkId/hyperlinkUri 仍可查。
static void testOsc8HistoryScroll()
{
    ZzTerminal term(40, 5, ZzBackendKind::Native, 100);
    feedStr(term, "\x1b]8;;https://hist.example\x1b\\hist\x1b]8;;\x1b\\");
    // 6 个换行把首行（含链接）滚出 5 行屏幕、进入历史
    for (int i = 0; i < 6; ++i)
        feedStr(term, "\r\n");
    ZZ_TEST_EXPECT(term.historyView().lineCount() >= 1);
    const std::uint16_t id = term.historyView().lineAt(0).cellAt(0).hyperlinkId;
    ZZ_TEST_EXPECT(id != 0);
    ZZ_TEST_EXPECT(term.historyView().hyperlinkUri(id) == "https://hist.example");
}

// M22-5e：端到端——折行链接经 resize 缩列触发 reflow 后 id 非 0 且 URI 稳定。
static void testOsc8ReflowSurvivesResize()
{
    ZzTerminal term(10, 5, ZzBackendKind::Native, 100);
    // 12 字符链接文本，10 列下折成 2 行（wrapped 链）
    feedStr(term, "\x1b]8;;https://reflow.example\x1b\\abcdefghijkl\x1b]8;;\x1b\\");
    const std::uint16_t idBefore = linkAt(term, 0, 0);
    ZZ_TEST_EXPECT(idBefore != 0);
    ZZ_TEST_EXPECT(linkAt(term, 1, 0) == idBefore); // 折行第二段同链接
    // 缩列触发 reflow：12 格重切为 3 行；屏幕 5 行容纳不下，首片段溢出入史。
    // 不钉死具体行号（行迁移语义属 M15/M20），扫描历史+屏幕全部链接格：
    // 链接格总数守恒（12）、id 跨 reflow 稳定、URI 仍是原值。
    term.resize(5, 5);
    int linkedCells = 0;
    auto scan = [&](auto& view, int lines, int cols) {
        for (int r = 0; r < lines; ++r)
            for (int c = 0; c < cols; ++c) {
                const std::uint16_t id = view.lineAt(r).cellAt(c).hyperlinkId;
                if (id != 0) {
                    ZZ_TEST_EXPECT(id == idBefore);
                    ++linkedCells;
                }
            }
    };
    scan(term.historyView(), (int)term.historyView().lineCount(), 5);
    scan(term.renderView(), 5, 5);
    ZZ_TEST_EXPECT(linkedCells == 12);
    ZZ_TEST_EXPECT(term.renderView().hyperlinkUri(idBefore) == "https://reflow.example");
}

int main()
{
    testRenderViewHyperlinkDefaults();
    testOsc8Basic();
    testOsc8BelTerminated();
    testOsc8IdAssociation();
    testOsc8UnknownParamsIgnored();
    testOsc8WideChar();
    testOsc8Malformed();
    testOsc8HistoryScroll();
    testOsc8ReflowSurvivesResize();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    return 0;
}
