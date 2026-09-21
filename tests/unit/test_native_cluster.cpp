// M7b T3：native putChar 聚簇续接集成格级断言（公开 API）。
// 用例集与 T2 探针 26 例同源，期望值直接引 T2 裁定表 contour 实测值
// （docs/superpowers/specs/2026-09-21-m7b-parity-probe.md）；b 类分歧例
// （I-1 B1 整喂 / I-2 Prepend+a 宽度）按控制者裁定取 native 目标值。
// 每例同时跑"整喂"与"逐码点切 feed"两式（native 跨 feed 续接应一致）。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;
#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

void feedBytes(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
}

// 按 UTF-8 码点边界切分（用例脚本均为合法 UTF-8）。
std::vector<std::string_view> splitCodePoints(std::string_view bytes)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char lead = static_cast<unsigned char>(bytes[i]);
        std::size_t len = 1;
        if (lead >= 0xF0)
            len = 4;
        else if (lead >= 0xE0)
            len = 3;
        else if (lead >= 0xC0)
            len = 2;
        out.push_back(bytes.substr(i, len));
        i += len;
    }
    return out;
}

void checkCell(const ZzTerminal& t, int row, int col, std::string_view text, ZzCellWidth w,
               const char* what)
{
    const ZzCellView c = t.renderView().lineAt(row).cellAt(col);
    if (c.text != text || c.width != w) {
        ++g_failures;
        std::fprintf(stderr, "FAIL cell(%d,%d) %s: got{text=%s,w=%d} want{w=%d}\n", row, col, what,
                     c.text.c_str(), static_cast<int>(c.width), static_cast<int>(w));
    }
}

void checkCursor(const ZzTerminal& t, int row, int col, const char* what)
{
    const ZzPosition p = t.cursor().position;
    if (p.row != row || p.col != col) {
        ++g_failures;
        std::fprintf(stderr, "FAIL cursor %s: got(%d,%d) want(%d,%d)\n", what, p.row, p.col, row,
                     col);
    }
}

void checkFg(const ZzTerminal& t, int row, int col, const ZzColor& want, const char* what)
{
    const ZzCellView c = t.renderView().lineAt(row).cellAt(col);
    if (!(c.foreground == want)) {
        ++g_failures;
        std::fprintf(stderr, "FAIL fg(%d,%d) %s\n", row, col, what);
    }
}

constexpr ZzCellWidth N = ZzCellWidth::Narrow;
constexpr ZzCellWidth L = ZzCellWidth::WideLead;
constexpr ZzCellWidth W = ZzCellWidth::WideContinuation;

// 每例对整喂/切 feed 两个终端跑同一断言块（native 跨 feed 续接应一致）。
template <typename Check>
void bothModes(std::string_view script, Check&& check)
{
    ZzTerminal whole { 80, 24, ZzBackendKind::Native, 1000 };
    feedBytes(whole, script);
    check(whole, "整喂");
    ZzTerminal split { 80, 24, ZzBackendKind::Native, 1000 };
    for (const std::string_view cp : splitCodePoints(script))
        feedBytes(split, cp);
    check(split, "切feed");
}

// 01 单组合符 a + U+0301：1 格窄聚簇，光标不推进。
void test01SingleCombining()
{
    bothModes("a\xCC\x81", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "a\xCC\x81", N, m);
        checkCursor(t, 0, 1, m);
    });
}

// 02 双组合符 e + U+0301 + U+0327。
void test02DoubleCombining()
{
    bothModes("e\xCC\x81\xCC\xA7", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "e\xCC\x81\xCC\xA7", N, m);
        checkCursor(t, 0, 1, m);
    });
}

// 03 Emoji ZWJ 对：宽格对，整串一格。
void test03EmojiZwj()
{
    bothModes("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// 04 家庭 ZWJ 四连：宽格对。
void test04FamilyZwj()
{
    bothModes("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",
              [](const ZzTerminal& t, const char* m) {
                  checkCell(t, 0, 0,
                            "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7",
                            L, m);
                  checkCell(t, 0, 1, "", W, m);
                  checkCursor(t, 0, 2, m);
              });
}

// 05 区旗单发：RI 单发即宽 2（T2 I-3，native 初始落格宽度裁定覆盖）。
void test05RiSingle()
{
    bothModes("\xF0\x9F\x87\xBA", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xF0\x9F\x87\xBA", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// 06 区旗成对：两 RI 同格宽 2。
void test06RiPair()
{
    bothModes("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// 07 区旗三连：RI 对 + 落单 RI，各宽 2。
void test07RiTriple()
{
    bothModes("\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xBA",
              [](const ZzTerminal& t, const char* m) {
                  checkCell(t, 0, 0, "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8", L, m);
                  checkCell(t, 0, 1, "", W, m);
                  checkCell(t, 0, 2, "\xF0\x9F\x87\xBA", L, m);
                  checkCell(t, 0, 3, "", W, m);
                  checkCursor(t, 0, 4, m);
              });
}

// 08 VS16 变宽 U+261D + U+FE0F：窄基原位变宽。
void test08Vs16Wide()
{
    bothModes("\xE2\x98\x9D\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE2\x98\x9D\xEF\xB8\x8F", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// 09 VS16 红心 U+2764 + U+FE0F。
void test09Vs16Heart()
{
    bothModes("\xE2\x9D\xA4\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE2\x9D\xA4\xEF\xB8\x8F", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// 10 VS15 变窄 U+2764 + U+FE0E：保持窄。
void test10Vs15Narrow()
{
    bothModes("\xE2\x9D\xA4\xEF\xB8\x8E", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE2\x9D\xA4\xEF\xB8\x8E", N, m);
        checkCursor(t, 0, 1, m);
    });
}

// 11 keycap 1 + U+FE0F + U+20E3：带 VS16 判宽。
void test11Keycap()
{
    bothModes("1\xEF\xB8\x8F\xE2\x83\xA3", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "1\xEF\xB8\x8F\xE2\x83\xA3", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// 12 InCB 连字：基窄聚簇判宽 2（T2 I-4）。
void test12IncbConjunct()
{
    bothModes("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// 13 CJK + 组合符：宽格对上续接，宽度不变。
void test13CjkCombining()
{
    bothModes("\xE4\xB8\xAD\xCC\x81", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE4\xB8\xAD\xCC\x81", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// 14 a + ZWJ + 👩：a+ZWJ 窄聚簇，emoji 断开独立宽格。
void test14ZwjBreak()
{
    bothModes("a\xE2\x80\x8D\xF0\x9F\x91\xA9", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "a\xE2\x80\x8D", N, m);
        checkCell(t, 0, 1, "\xF0\x9F\x91\xA9", L, m);
        checkCell(t, 0, 2, "", W, m);
        checkCursor(t, 0, 3, m);
    });
}

// 15a Prepend 单发 U+0600：窄格。
void test15aPrependSingle()
{
    bothModes("\xD8\x80", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xD8\x80", N, m);
        checkCursor(t, 0, 1, m);
    });
}

// 15b Prepend + a：聚簇成功，native 目标宽 1（控制者裁定 I-2：
// 无 emoji 表现一律基宽；两式一致——native 无跨 feed 缺失）。
void test15bPrependLetter()
{
    bothModes("\xD8\x80"
              "a",
              [](const ZzTerminal& t, const char* m) {
                  checkCell(t, 0, 0, "\xD8\x80"
                                     "a",
                            N, m);
                  checkCursor(t, 0, 1, m);
              });
}

// B1 79a + U+0301 软换行边界：续到光标左邻格 c78，不换行（I-1 控制者
// 裁定：native 两式都附当前行尾格）。
void testB1SoftWrapBoundary()
{
    std::string script(79, 'a');
    script += "\xCC\x81";
    bothModes(script, [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 77, "a", N, m);
        checkCell(t, 0, 78, "a\xCC\x81", N, m);
        // 空白格 native 报 Empty（值 0，M4 已钉的表示约定）。
        checkCell(t, 0, 79, "", ZzCellWidth::Empty, m);
        checkCell(t, 1, 0, "", ZzCellWidth::Empty, m);
        checkCursor(t, 0, 79, m);
    });
}

// B2 78a + 中 + U+0301：wrap-pending 下续到宽格对首格，光标换行 (1,0)。
void testB2WidePairBoundary()
{
    std::string script(78, 'a');
    script += "\xE4\xB8\xAD\xCC\x81";
    bothModes(script, [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 78, "\xE4\xB8\xAD\xCC\x81", L, m);
        checkCell(t, 0, 79, "", W, m);
        checkCursor(t, 1, 0, m);
    });
}

// B3 VS16 窄变宽位移：原位转 WideLead + 插续格 + 后续输入右移一格。
void testB3Vs16Shift()
{
    bothModes("a\xE2\x98\x9D\xEF\xB8\x8F"
              "bc",
              [](const ZzTerminal& t, const char* m) {
                  checkCell(t, 0, 0, "a", N, m);
                  checkCell(t, 0, 1, "\xE2\x98\x9D\xEF\xB8\x8F", L, m);
                  checkCell(t, 0, 2, "", W, m);
                  checkCell(t, 0, 3, "b", N, m);
                  checkCell(t, 0, 4, "c", N, m);
                  checkCursor(t, 0, 5, m);
              });
}

// B4 尾列变宽抑制：79a + ☝ + VS16，聚簇保持窄落 c79，光标换行。
void testB4TailColumnSuppress()
{
    std::string script(79, 'a');
    script += "\xE2\x98\x9D\xEF\xB8\x8F";
    bothModes(script, [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 79, "\xE2\x98\x9D\xEF\xB8\x8F", N, m);
        checkCursor(t, 1, 0, m);
    });
}

// SGR1 画笔保留：红 a + 换绿 + U+0301 → 聚簇格保持红（T2 §5b）。
void testSgr1PenKept()
{
    bothModes("\x1b[31ma\x1b[32m\xCC\x81", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "a\xCC\x81", N, m);
        checkFg(t, 0, 0, ZzColor::Indexed(1), m);
    });
}

// SGR2 原位变宽画笔归属：变宽格与插入续格取基格画笔，后续格各自画笔。
void testSgr2WidenPen()
{
    bothModes("\x1b[31ma\xE2\x98\x9D\x1b[32m\xEF\xB8\x8F\x1b[33mbc",
              [](const ZzTerminal& t, const char* m) {
                  checkCell(t, 0, 1, "\xE2\x98\x9D\xEF\xB8\x8F", L, m);
                  checkFg(t, 0, 1, ZzColor::Indexed(1), m);
                  checkFg(t, 0, 2, ZzColor::Indexed(1), m); // 插入续格同基格
                  checkFg(t, 0, 3, ZzColor::Indexed(3), m);
                  checkFg(t, 0, 4, ZzColor::Indexed(3), m);
              });
}

// SGR3 emoji ZWJ 整格保留基字符画笔。
void testSgr3ZwjPen()
{
    bothModes("\x1b[31m\xF0\x9F\x91\xA8\x1b[32m\xE2\x80\x8D\xF0\x9F\x91\xA9",
              [](const ZzTerminal& t, const char* m) {
                  checkCell(t, 0, 0, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", L, m);
                  checkFg(t, 0, 0, ZzColor::Indexed(1), m);
              });
}

// N1 RI + 组合符：宽格聚簇。
void testN1RiCombining()
{
    bothModes("\xF0\x9F\x87\xBA\xCC\x81", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xF0\x9F\x87\xBA\xCC\x81", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// N2 宽基 + VS16：保持宽。
void testN2WideBaseVs16()
{
    bothModes("\xE4\xB8\xAD\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE4\xB8\xAD\xEF\xB8\x8F", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// N3 裸 keycap（无 VS16）：窄。
void testN3BareKeycap()
{
    bothModes("1\xE2\x83\xA3", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "1\xE2\x83\xA3", N, m);
        checkCursor(t, 0, 1, m);
    });
}

// N4 ExtPic + 肤色：宽格聚簇。
void testN4SkinTone()
{
    bothModes("\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// X1 跨 feed 续接：基字符与组合符分两次 feed 与整喂一致（bothModes 已
// 逐例覆盖；本例显式钉 ZWJ 序列逐码点分 feed）。
void testX1CrossFeed()
{
    ZzTerminal a { 80, 24, ZzBackendKind::Native, 1000 };
    feedBytes(a, "a");
    feedBytes(a, "\xCC\x81");
    checkCell(a, 0, 0, "a\xCC\x81", N, "跨feed组合符");
    checkCursor(a, 0, 1, "跨feed组合符");

    ZzTerminal b { 80, 24, ZzBackendKind::Native, 1000 };
    feedBytes(b, "\xF0\x9F\x91\xA8");
    feedBytes(b, "\xE2\x80\x8D");
    feedBytes(b, "\xF0\x9F\x91\xA9");
    checkCell(b, 0, 0, "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9", L, "跨feedZWJ");
    checkCursor(b, 0, 2, "跨feedZWJ");
}

// X2 光标行为：续接后再喂 b，b 落在聚簇格之后。
void testX2CursorAfterContinue()
{
    ZzTerminal t { 80, 24, ZzBackendKind::Native, 1000 };
    feedBytes(t, "a\xCC\x81"
                 "b");
    checkCell(t, 0, 0, "a\xCC\x81", N, "聚簇格");
    checkCell(t, 0, 1, "b", N, "续接后b落格");
    checkCursor(t, 0, 2, "续接后光标");
}

// X3 空白格不续：组合符落在行首（无前格）独占一格（简报前格守卫：
// 前格须含文本才判续，空白格不续；裁定表末节同述）。
void testX3BlankNoContinue()
{
    ZzTerminal t { 80, 24, ZzBackendKind::Native, 1000 };
    feedBytes(t, "\xCC\x81"); // 行首无前格：U+0301 独立落格
    checkCell(t, 0, 0, "\xCC\x81", N, "行首组合符");
    checkCursor(t, 0, 1, "行首组合符");
}

// H1 Hangul 连用 Jamo L+V（U+1100+U+1161）：GB6 续接，宽基保持宽 2
// （探针实测 contour w2 聚簇，快路径 prev 侧须放 Hangul 五类进慢路径）。
void testH1HangulLV()
{
    bothModes("\xE1\x84\x80\xE1\x85\xA1", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE1\x84\x80\xE1\x85\xA1", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// H2 预组音节 + Jamo T（가 U+AC00 + U+11A8）：GB8 续接，宽 2 聚簇。
void testH2HangulLvT()
{
    bothModes("\xEA\xB0\x80\xE1\x86\xA8", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xEA\xB0\x80\xE1\x86\xA8", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// V 系（M7c T3 实测）：Emoji 属性基字符 + VS16 判宽 2；非 Emoji 基（a）
// + VS16 保窄——判别属性是 Emoji 而非 Emoji_Presentation。
void testV1DigitVs16()
{
    bothModes("0\xEF\xB8\x8F"
              "5\xEF\xB8\x8F"
              "9\xEF\xB8\x8F",
              [](const ZzTerminal& t, const char* m) {
                  checkCell(t, 0, 0, "0\xEF\xB8\x8F", L, m);
                  checkCell(t, 0, 1, "", W, m);
                  checkCell(t, 0, 2, "5\xEF\xB8\x8F", L, m);
                  checkCell(t, 0, 3, "", W, m);
                  checkCell(t, 0, 4, "9\xEF\xB8\x8F", L, m);
                  checkCell(t, 0, 5, "", W, m);
                  checkCursor(t, 0, 6, m);
              });
}

void testV2HashVs16()
{
    bothModes("#\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "#\xEF\xB8\x8F", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

void testV3StarVs16()
{
    bothModes("*\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "*\xEF\xB8\x8F", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

void testV4CopyrightVs16()
{
    bothModes("\xC2\xA9\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xC2\xA9\xEF\xB8\x8F", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

void testV5DoubleExclVs16()
{
    bothModes("\xE2\x80\xBC\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE2\x80\xBC\xEF\xB8\x8F", L, m);
        checkCell(t, 0, 1, "", W, m);
        checkCursor(t, 0, 2, m);
    });
}

// V6 判别例：a + VS16 保窄（非 Emoji 基字符不适用 VS16 变宽）。
void testV6AsciiVs16Narrow()
{
    bothModes("a\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "a\xEF\xB8\x8F", N, m);
        checkCursor(t, 0, 1, m);
    });
}

// V7-V9 方向 2 反例（M7c T3 修复波）：ExtPic 且 Emoji 但非 variation
// base——libunicode 真规则下 contour 保窄，native 按 emojiVariationBase
// 对齐（证伪"Emoji 属性"泛化）。
void testV7StarVs16Narrow()
{
    bothModes("\xE2\x98\x85\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE2\x98\x85\xEF\xB8\x8F", N, m);
        checkCursor(t, 0, 1, m);
    });
}

void testV8ChessVs16Narrow()
{
    bothModes("\xE2\x99\x94\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE2\x99\x94\xEF\xB8\x8F", N, m);
        checkCursor(t, 0, 1, m);
    });
}

void testV9NoteVs16Narrow()
{
    bothModes("\xE2\x99\xA9\xEF\xB8\x8F", [](const ZzTerminal& t, const char* m) {
        checkCell(t, 0, 0, "\xE2\x99\xA9\xEF\xB8\x8F", N, m);
        checkCursor(t, 0, 1, m);
    });
}

} // namespace

int main()
{
    test01SingleCombining();
    test02DoubleCombining();
    test03EmojiZwj();
    test04FamilyZwj();
    test05RiSingle();
    test06RiPair();
    test07RiTriple();
    test08Vs16Wide();
    test09Vs16Heart();
    test10Vs15Narrow();
    test11Keycap();
    test12IncbConjunct();
    test13CjkCombining();
    test14ZwjBreak();
    test15aPrependSingle();
    test15bPrependLetter();
    testB1SoftWrapBoundary();
    testB2WidePairBoundary();
    testB3Vs16Shift();
    testB4TailColumnSuppress();
    testSgr1PenKept();
    testSgr2WidenPen();
    testSgr3ZwjPen();
    testN1RiCombining();
    testN2WideBaseVs16();
    testN3BareKeycap();
    testN4SkinTone();
    testX1CrossFeed();
    testX2CursorAfterContinue();
    testX3BlankNoContinue();
    testH1HangulLV();
    testH2HangulLvT();
    testV1DigitVs16();
    testV2HashVs16();
    testV3StarVs16();
    testV4CopyrightVs16();
    testV5DoubleExclVs16();
    testV6AsciiVs16Narrow();
    testV7StarVs16Narrow();
    testV8ChessVs16Narrow();
    testV9NoteVs16Narrow();
    if (g_failures == 0)
        std::printf("test_native_cluster: 全部通过\n");
    return g_failures == 0 ? 0 : 1;
}
