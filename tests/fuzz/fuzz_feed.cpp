// M7a：ZzTerminal feed+resize 交织 Fuzz target——有态终端在任意输入
// 分片与尺寸抖动下不崩、不断言、不 UB（Architecture §204 第三类
// target；reflow 历史 bug 集中于 feed+resize 交织路径）。
#include "ZzTerm/Terminal.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    if (size < 2)
        return 0;
    // 跨输入复用：终端是有态对象，复用才能触达长程状态路径；跨输入的
    // 状态污染本身属于 fuzz 面（规格 M7a 4.3；若实证崩溃难复现，再
    // 裁定为按输入重建并记录）。
    static ZzTerminal term(80, 24, ZzBackendKind::Native, 1000);

    const std::size_t chunk = 1 + data[0] % 17;   // 分片粒度 1-17 字节
    const std::uint8_t knob = data[size / 2];      // resize 判定字节
    const bool doResize = (knob & 1) != 0;
    const int resizeCols = 40 + knob % 41;         // 列数 40-80 抖动

    const std::byte* bytes = reinterpret_cast<const std::byte*>(data);
    const std::size_t mid = size / 2;
    bool resized = false;
    for (std::size_t off = 0; off < size; off += chunk) {
        const std::size_t n = (off + chunk <= size) ? chunk : size - off;
        term.feed(std::span<const std::byte>(bytes + off, n));
        if (doResize && !resized && mid < off + n) {
            term.resize(resizeCols, 24); // feed 中段穿插 resize（恰好一次）
            resized = true;
        }
    }
    return 0;
}
