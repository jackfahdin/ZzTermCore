// M11：ZzUtf8Decoder 独立 Fuzz target——增量解码器在任意字节流与任意分块下
// 不崩、不断言、不 UB（ASan + libFuzzer），并校验三条不变量（见 checkCodePoints
// 与续接一致性断言）。decoder 语义准绳为 include/ZzTerm/Utf8.h 文档（RFC 3629
// Table 3-7 拒绝 overlong/代理区/超 U+10FFFF/5-6 字节序列；非法按 maximal
// subpart 输出 U+FFFD）。断言用 __builtin_trap——fuzz preset 为 RelWithDebInfo，
// assert 会被 NDEBUG 编译掉。
#include "ZzTerm/Utf8.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

void checkCodePoints(const std::vector<char32_t>& cps, std::size_t inputSize)
{
    // 不变量一：输出码点数不超过输入字节数加一（每字节至多产一码点，
    // finish 收尾至多追加一个 U+FFFD）。
    if (cps.size() > inputSize + 1)
        __builtin_trap();
    for (char32_t cp : cps) {
        // 不变量二/三：不落在代理区 U+D800-U+DFFF、不超过 U+10FFFF。
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            __builtin_trap();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size == 0)
        return 0;
    const std::string_view input(reinterpret_cast<const char*>(data), size);

    // 路径一：整喂 decodeAll（内部调 finish 收尾并复位）。
    ZzUtf8Decoder whole;
    const std::vector<char32_t> expect = whole.decodeAll(input);
    checkCodePoints(expect, size);

    // 路径二：首字节定切点分两段 feed + finish——续接不变量：与整喂一致
    //（Utf8.h feed 文档承诺：任意切块与一次性喂入输出完全一致）。
    ZzUtf8Decoder chunked;
    std::vector<char32_t> got;
    auto emit = [&got](char32_t cp) { got.push_back(cp); };
    const std::size_t cut = data[0] % size; // [0, size-1]，第二段恒非空
    chunked.feed(input.substr(0, cut), emit);
    chunked.feed(input.substr(cut), emit);
    chunked.finish(emit);
    if (got != expect)
        __builtin_trap();

    // 路径三：逐字节 feed + finish——最细分块下的续接不变量。
    ZzUtf8Decoder bytewise;
    std::vector<char32_t> gotByte;
    auto emitByte = [&gotByte](char32_t cp) { gotByte.push_back(cp); };
    for (const std::uint8_t b : input) {
        const char ch = static_cast<char>(b);
        bytewise.feed(std::string_view(&ch, 1), emitByte);
    }
    bytewise.finish(emitByte);
    if (gotByte != expect)
        __builtin_trap();

    return 0;
}
