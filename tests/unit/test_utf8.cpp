/**
 * @file test_utf8.cpp
 * @brief ZzUtf8Decoder 单元测试。
 *
 * 测试约定：main() 驱动，ZZ_TEST_EXPECT 断言失败打印位置并计数，
 * 进程以失败数作为退出码（0 为全部通过）。
 *
 * 非法序列的期望输出遵循 Unicode Standard §3.9 的 maximal subpart
 * 替换语义（每个非法子序列的最长格式良好前缀替换为单个 U+FFFD）。
 */

#include "ZzTerm/Utf8.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                              \
    do {                                                                  \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,  \
                         #cond);                                          \
        }                                                                 \
    } while (0)

/// 一次性解码（含 finish 收尾）。
std::vector<char32_t> decode(std::string_view input) {
    ZzUtf8Decoder decoder;
    return decoder.decodeAll(input);
}

bool expectDecoded(std::string_view input,
                   std::initializer_list<char32_t> expected) {
    const std::vector<char32_t> actual = decode(input);
    if (actual == std::vector<char32_t>(expected)) {
        return true;
    }
    std::fprintf(stderr, "mismatch for input (%zu bytes): got", input.size());
    for (char32_t cp : actual) {
        std::fprintf(stderr, " U+%04X", static_cast<unsigned>(cp));
    }
    std::fprintf(stderr, "\n");
    return false;
}

void testAscii() {
    ZZ_TEST_EXPECT(expectDecoded("Hello, ZzTerm!", {'H', 'e', 'l', 'l', 'o',
                                                    ',', ' ', 'Z', 'z', 'T',
                                                    'e', 'r', 'm', '!'}));
    // 内嵌 NUL 与控制字符按原样通过
    ZZ_TEST_EXPECT(expectDecoded(std::string_view("a\0b\t\n", 5),
                                 {'a', 0, 'b', '\t', '\n'}));
}

void testMultiByte() {
    // 2 字节：é U+00E9、ñ U+00F1
    ZZ_TEST_EXPECT(expectDecoded("\xC3\xA9\xC3\xB1", {0x00E9, 0x00F1}));
    // 2 字节下界/上界：U+0080、U+07FF
    ZZ_TEST_EXPECT(expectDecoded("\xC2\x80\xDF\xBF", {0x0080, 0x07FF}));
    // 3 字节：CJK '中' U+4E2D、'文' U+6587
    ZZ_TEST_EXPECT(expectDecoded("中文", {0x4E2D, 0x6587}));
    // 3 字节边界：E0 A0 80 = U+0800（3 字节最小值）、U+FFFF
    ZZ_TEST_EXPECT(expectDecoded("\xE0\xA0\x80\xEF\xBF\xBF", {0x0800, 0xFFFF}));
    // 代理区相邻的合法码点：U+D7FF、U+E000
    ZZ_TEST_EXPECT(expectDecoded("\xED\x9F\xBF\xEE\x80\x80", {0xD7FF, 0xE000}));
    // 4 字节：emoji 😀 U+1F600，以及 U+10000 / U+10FFFF 边界
    ZZ_TEST_EXPECT(expectDecoded("\xF0\x9F\x98\x80", {0x1F600}));
    ZZ_TEST_EXPECT(expectDecoded("\xF0\x90\x80\x80\xF4\x8F\xBF\xBF",
                                 {0x10000, 0x10FFFF}));
}

void testEmojiZwj() {
    // 👩‍💻 = U+1F469 ZWJ U+1F4BB：ZWJ 序列按独立码点流输出，
    // grapheme 聚簇不属于本模块职责
    ZZ_TEST_EXPECT(expectDecoded("👩\xE2\x80\x8D💻",
                                 {0x1F469, 0x200D, 0x1F4BB}));
    // 旗帜 regional indicator 对：U+1F1E8 U+1F1F3
    ZZ_TEST_EXPECT(expectDecoded("\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3",
                                 {0x1F1E8, 0x1F1F3}));
}

void testSplitEquivalence() {
    // 混合内容：ASCII + CJK + 4 字节 emoji + 非法字节
    const std::string full =
        "ab\xE4\xB8\xAD\xF0\x9F\x98\x80z\xE1\x80\x41\xC3\xA9";
    const std::vector<char32_t> reference = decode(full);

    // 逐字节喂入必须与一次性喂入结果一致
    {
        ZzUtf8Decoder decoder;
        std::vector<char32_t> actual;
        for (char c : full) {
            decoder.feed(std::string_view(&c, 1),
                         [&actual](char32_t cp) { actual.push_back(cp); });
        }
        decoder.finish([&actual](char32_t cp) { actual.push_back(cp); });
        ZZ_TEST_EXPECT(actual == reference);
    }
    // 遍历所有切分点：两段喂入与一次性喂入一致
    for (std::size_t cut = 0; cut <= full.size(); ++cut) {
        ZzUtf8Decoder decoder;
        std::vector<char32_t> actual;
        decoder.feed(std::string_view(full).substr(0, cut),
                     [&actual](char32_t cp) { actual.push_back(cp); });
        decoder.feed(std::string_view(full).substr(cut),
                     [&actual](char32_t cp) { actual.push_back(cp); });
        decoder.finish([&actual](char32_t cp) { actual.push_back(cp); });
        if (actual != reference) {
            ZZ_TEST_EXPECT(false && "split point mismatch");
            std::fprintf(stderr, "  at cut=%zu\n", cut);
        }
    }
}

void testPendingAndFinish() {
    ZzUtf8Decoder decoder;
    std::vector<char32_t> out;
    decoder.feed("\xE4\xB8", [&out](char32_t cp) { out.push_back(cp); });
    ZZ_TEST_EXPECT(out.empty());
    ZZ_TEST_EXPECT(decoder.pending());
    // 续传最后一个字节后输出完整码点 '中'
    decoder.feed("\xAD", [&out](char32_t cp) { out.push_back(cp); });
    ZZ_TEST_EXPECT(out == std::vector<char32_t>{0x4E2D});
    ZZ_TEST_EXPECT(!decoder.pending());

    // 流结束时序列不完整：finish() 输出一个 U+FFFD 并复位
    decoder.feed("\xF0\x9F\x98", [&out](char32_t cp) { out.push_back(cp); });
    ZZ_TEST_EXPECT(decoder.pending());
    decoder.finish([&out](char32_t cp) { out.push_back(cp); });
    ZZ_TEST_EXPECT(!decoder.pending());
    ZZ_TEST_EXPECT(out == std::vector<char32_t>({0x4E2D, ZzReplacementChar}));
    // reset() 丢弃未完成序列时不输出 U+FFFD
    decoder.feed("\xE4", [&out](char32_t cp) { out.push_back(cp); });
    decoder.reset();
    ZZ_TEST_EXPECT(!decoder.pending());
    ZZ_TEST_EXPECT(out == std::vector<char32_t>({0x4E2D, ZzReplacementChar}));
}

void testInvalidSequences() {
    // 裸 continuation
    ZZ_TEST_EXPECT(expectDecoded("\x80", {ZzReplacementChar}));
    ZZ_TEST_EXPECT(expectDecoded("\x80\xBF", {ZzReplacementChar,
                                              ZzReplacementChar}));
    // 0xC0/0xC1 必为超长编码：lead 与 continuation 各自一个 U+FFFD
    ZZ_TEST_EXPECT(expectDecoded("\xC0\xAF", {ZzReplacementChar,
                                              ZzReplacementChar}));
    ZZ_TEST_EXPECT(expectDecoded("\xC1\xBF", {ZzReplacementChar,
                                              ZzReplacementChar}));
    // 超长 3 字节：E0 80 80 -> FFFD FFFD FFFD
    ZZ_TEST_EXPECT(expectDecoded("\xE0\x80\x80", {ZzReplacementChar,
                                                  ZzReplacementChar,
                                                  ZzReplacementChar}));
    // 代理区：ED A0 80 (U+D800) / ED BF BF (U+DFFF) -> 各 3 个 U+FFFD
    ZZ_TEST_EXPECT(expectDecoded("\xED\xA0\x80", {ZzReplacementChar,
                                                  ZzReplacementChar,
                                                  ZzReplacementChar}));
    ZZ_TEST_EXPECT(expectDecoded("\xED\xBF\xBF", {ZzReplacementChar,
                                                  ZzReplacementChar,
                                                  ZzReplacementChar}));
    // 超出 U+10FFFF：F4 90 80 80 -> 4 个 U+FFFD
    ZZ_TEST_EXPECT(expectDecoded("\xF4\x90\x80\x80",
                                 {ZzReplacementChar, ZzReplacementChar,
                                  ZzReplacementChar, ZzReplacementChar}));
    // 0xF5..0xFF（含历史 5/6 字节序列头）被拒绝
    ZZ_TEST_EXPECT(expectDecoded("\xF5\x80\x80\x80",
                                 {ZzReplacementChar, ZzReplacementChar,
                                  ZzReplacementChar, ZzReplacementChar}));
    ZZ_TEST_EXPECT(expectDecoded("\xFF", {ZzReplacementChar}));
    ZZ_TEST_EXPECT(expectDecoded("\xFE\xAF", {ZzReplacementChar,
                                              ZzReplacementChar}));

    // maximal subpart：E1 80 已是格式良好前缀，整体一个 U+FFFD，
    // 随后 0x41('A') 作为新序列正常解码（Unicode §3.9 示例）
    ZZ_TEST_EXPECT(expectDecoded("\xE1\x80\x41", {ZzReplacementChar, 'A'}));
    // 同理 F0 9F 前缀 + 'A'
    ZZ_TEST_EXPECT(expectDecoded("\xF0\x9F\x41", {ZzReplacementChar, 'A'}));
    // 截断的多字节序列后跟合法字符：FFFD + 正常继续，解码器不锁定
    ZZ_TEST_EXPECT(expectDecoded("\xE4\xB8\x41\x42",
                                 {ZzReplacementChar, 'A', 'B'}));
    // 非法字节夹杂在合法文本中间
    ZZ_TEST_EXPECT(expectDecoded("a\xFF\xE4\xB8\xADz",
                                 {'a', ZzReplacementChar, 0x4E2D, 'z'}));
}

void testTruncatedAtEnd() {
    // decodeAll 内含 finish()：流尾不完整序列替换为单个 U+FFFD
    ZZ_TEST_EXPECT(expectDecoded("\xE4\xB8", {ZzReplacementChar}));
    ZZ_TEST_EXPECT(expectDecoded("\xF0\x9F\x98\x80\xE4",
                                 {0x1F600, ZzReplacementChar}));
    ZZ_TEST_EXPECT(expectDecoded("\xC3", {ZzReplacementChar}));
}

} // namespace

int main() {
    testAscii();
    testMultiByte();
    testEmojiZwj();
    testSplitEquivalence();
    testPendingAndFinish();
    testInvalidSequences();
    testTruncatedAtEnd();

    if (g_failures == 0) {
        std::printf("test_utf8: all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_utf8: %d failure(s)\n", g_failures);
    return 1;
}
