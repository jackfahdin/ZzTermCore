// M7b T2：contour 聚簇行为探针（开发期工具，不注册 ctest）。
// 只读 contour 后端行为：每个探针脚本分别以"整喂"与"逐码点切 feed"两式
// 喂 ZzTerminal(80, 24, Contour, 1000)，逐格打印前两行非空格
// （col、text 十六进制转储、width 枚举值）与光标位置，输出人工抄入
// docs/superpowers/specs/2026-09-21-m7b-parity-probe.md 裁定表。
#include <ZzTerm/Terminal.h>

#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

void feedBytes(ZzTerminal& t, std::string_view bytes)
{
    t.feed(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
}

// 按 UTF-8 码点边界切分（探针脚本均为合法 UTF-8）。
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

void printColor(const ZzColor& c)
{
    switch (c.kind()) {
    case ZzColor::Kind::Default: std::printf("D"); break;
    case ZzColor::Kind::Indexed: std::printf("I%u", static_cast<unsigned>(c.index())); break;
    case ZzColor::Kind::Rgb:
        std::printf("RGB%02X%02X%02X", static_cast<unsigned>(c.red()),
                    static_cast<unsigned>(c.green()), static_cast<unsigned>(c.blue()));
        break;
    }
}

void dumpTerminal(const ZzTerminal& t, const char* mode)
{
    std::printf("  [%s] cursor=(%d,%d)\n", mode, t.cursor().position.row, t.cursor().position.col);
    for (int row = 0; row < 2; ++row) {
        const ZzLineView line = t.renderView().lineAt(row);
        bool any = false;
        for (int col = 0; col < 80; ++col) {
            const ZzCellView cell = line.cellAt(col);
            if (cell.text.empty() && cell.width != ZzCellWidth::WideContinuation)
                continue;
            if (!any) {
                std::printf("    row %d:", row);
                any = true;
            }
            std::printf("  c%d[w%d", col, static_cast<int>(cell.width));
            // 画笔归属观察：属性位原始值 + 前景色（SGR 探针关键）。
            if (cell.attributes.raw() != 0 || !cell.foreground.isDefault()) {
                std::printf("|a%04X|", static_cast<unsigned>(cell.attributes.raw()));
                printColor(cell.foreground);
            }
            std::printf("]");
            if (!cell.text.empty()) {
                std::printf("=");
                for (const unsigned char b : cell.text)
                    std::printf("%02X", b);
            }
        }
        if (any)
            std::printf("\n");
    }
}

// 每例同时跑"整喂"与"逐码点切 feed"两式（覆盖跨 feed 续接）。
void probe(const char* name, std::string_view script)
{
    std::printf("== %s ==\n", name);
    ZzTerminal whole { 80, 24, ZzBackendKind::Contour, 1000 };
    feedBytes(whole, script);
    dumpTerminal(whole, "整喂");
    ZzTerminal split { 80, 24, ZzBackendKind::Contour, 1000 };
    for (const std::string_view cp : splitCodePoints(script))
        feedBytes(split, cp);
    dumpTerminal(split, "切 feed");
}

} // namespace

int main()
{
    // 1. 单组合符        a + U+0301
    probe("01 单组合符 a+U+0301", "a\xCC\x81");
    // 2. 双组合符        e + U+0301 U+0327
    probe("02 双组合符 e+U+0301+U+0327", "e\xCC\x81\xCC\xA7");
    // 3. Emoji ZWJ 对    👨‍👩
    probe("03 Emoji ZWJ 对", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9");
    // 4. 家庭 ZWJ 四连   👨‍👩‍👧
    probe("04 家庭 ZWJ 四连", "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
    // 5. 区旗单发        U+1F1FA
    probe("05 区旗单发 U+1F1FA", "\xF0\x9F\x87\xBA");
    // 6. 区旗成对        US
    probe("06 区旗成对 US", "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8");
    // 7. 区旗三连
    probe("07 区旗三连", "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xBA");
    // 8. VS16 变宽       U+261D U+FE0F
    probe("08 VS16 变宽 U+261D+U+FE0F", "\xE2\x98\x9D\xEF\xB8\x8F");
    // 9. VS16 红心       U+2764 U+FE0F
    probe("09 VS16 红心 U+2764+U+FE0F", "\xE2\x9D\xA4\xEF\xB8\x8F");
    // 10. VS15 变窄      U+2764 U+FE0E
    probe("10 VS15 变窄 U+2764+U+FE0E", "\xE2\x9D\xA4\xEF\xB8\x8E");
    // 11. keycap         1 + U+FE0F U+20E3
    probe("11 keycap 1+U+FE0F+U+20E3", "1\xEF\xB8\x8F\xE2\x83\xA3");
    // 12. InCB 连字      क + ् + ष
    probe("12 InCB 连字", "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7");
    // 13. CJK+组合       中 + U+0301
    probe("13 CJK+组合 中+U+0301", "\xE4\xB8\xAD\xCC\x81");
    // 14. a+ZWJ+emoji    （ZWJ 前非 ExtPic，预期断）
    probe("14 a+ZWJ+emoji 预期断", "a\xE2\x80\x8D\xF0\x9F\x91\xA9");
    // 15a. Prepend 单发  U+0600
    probe("15a Prepend 单发 U+0600", "\xD8\x80");
    // 15b. Prepend+字母  U+0600 + a
    probe("15b Prepend+字母 U+0600+a", "\xD8\x80"
                                       "a");

    // 边界 1：行填满 79 个 a 后再喂组合符（软换行边界续接：
    // 前格在物理上行尾，组合符落同行末格还是新行首格）。
    {
        std::string script(79, 'a');
        script += "\xCC\x81"; // U+0301
        probe("B1 79a+U+0301 软换行边界续接", script);
    }
    // 边界 2：尾列宽字符 + 组合符（宽格对上的续接）：
    // 78 个 a + 中（占 c78-c79）+ U+0301。
    {
        std::string script(78, 'a');
        script += "\xE4\xB8\xAD\xCC\x81"; // 中 + U+0301
        probe("B2 78a+中+U+0301 宽格对续接", script);
    }
    // 边界 3：VS16 窄变宽位移语义深挖——基字符已窄格落格，VS16 使其变宽后，
    // 后续内容是否右移（b 应落 c3 则证明插入了续格并右移）。
    probe("B3 a+U+261D+U+FE0F+bc 变宽位移", "a\xE2\x98\x9D\xEF\xB8\x8F"
                                           "bc");
    // 边界 4：VS16 变宽发生在行末列——79 个 a 填到 c78，U+261D 落 c79
    // （wrap-pending），再喂 U+FE0F 观察尾列窄变宽行为。
    {
        std::string script(79, 'a');
        script += "\xE2\x98\x9D\xEF\xB8\x8F"; // U+261D U+FE0F
        probe("B4 79a+U+261D+U+FE0F 尾列变宽", script);
    }

    // SGR 画笔属性语义补探（T2 修复波 Major 1：计划显式委托 T2 实测的决策点）。
    // SGR1：红画笔落基字符 a，换绿画笔后喂组合符——续接格取基格原画笔还是新画笔。
    probe("SGR1 红a+换绿+U+0301", "\x1b[31ma\x1b[32m\xCC\x81");
    // SGR2：SGR + B3 形态——红 a/☝，换绿喂 VS16（原位变宽），换黄喂 bc，
    // 记录被右移格与插入续格的画笔归属。
    probe("SGR2 红a☝+绿VS16+黄bc", "\x1b[31ma\xE2\x98\x9D\x1b[32m\xEF\xB8\x8F\x1b[33mbc");
    // SGR3：SGR + emoji ZWJ——红 👨，换绿喂 ZWJ+👩，记录整格属性。
    probe("SGR3 红👨+绿ZWJ👩", "\x1b[31m\xF0\x9F\x91\xA8\x1b[32m\xE2\x80\x8D\xF0\x9F\x91\xA9");

    // 组合盲区补探四例（T2 修复波 Nit 1 授权，只探不判）。
    probe("N1 RI+组合符 U+1F1FA+U+0301", "\xF0\x9F\x87\xBA\xCC\x81");
    probe("N2 宽基+VS16 中+U+FE0F", "\xE4\xB8\xAD\xEF\xB8\x8F");
    probe("N3 裸 keycap 1+U+20E3", "1\xE2\x83\xA3");
    probe("N4 ExtPic+肤色 👍🏽", "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD");
    // Hangul GB6/7/8 续接补探两例（T3 修复波 Major 1 授权）：
    // H1 连用 Jamo L+V（U+1100+U+1161）、H2 预组音节+Jamo T（가+U+11A8）。
    probe("H1 Hangul L+V U+1100+U+1161", "\xE1\x84\x80\xE1\x85\xA1");
    probe("H2 Hangul LV+T U+AC00+U+11A8", "\xEA\xB0\x80\xE1\x86\xA8");
    return 0;
}
