// M7a：ZzVtParser Fuzz target——状态机在任意字节流与任意 chunk 边界下
// 不崩、不断言、不 UB（ASan + libFuzzer）。sink 基类默认回调全 no-op
//（src/parser/Parser.cpp:47-54），语义不在本 target 校验范围。
#include "ZzTerm/Parser.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size == 0)
        return 0;
    const std::string_view input(reinterpret_cast<const char*>(data), size);
    ZzParserSink sink; // 基类默认回调全 no-op
    // 路径一：整喂一次（完整序列路径）。
    {
        ZzVtParser parser(&sink);
        parser.feed(input);
    }
    // 路径二：首字节对输入长度取模定切点，切两段先后喂——跨 chunk
    // 续接路径（OSC 字符串与多字节序列在边界中断续接是最脆的接缝）。
    {
        ZzVtParser parser(&sink);
        const std::size_t cut = data[0] % size; // [0, size-1]，第二段恒非空
        parser.feed(input.substr(0, cut));
        parser.feed(input.substr(cut));
    }
    return 0;
}
