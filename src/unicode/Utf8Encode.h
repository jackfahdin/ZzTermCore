// zzEncodeUtf8 / zzAppendCodePoint：码点 → UTF-8 编码的 canonical 实现
// （unicode 模块；M5b 自 ZzSelectionText.cpp 抽出，M7c 自 src/terminal 迁入）。
// 解码器在 ZzTerm/Utf8.h（ZzUtf8Decoder），编码/解码同属 unicode 模块。
// header-only inline、C++20 写法（C++23 兼容），供主库与 contour 后端库
// 共享——contour 库经 PRIVATE include "${CMAKE_SOURCE_DIR}/src" 引用。
#pragma once

#include <cstddef>
#include <string>

// 把 cp 编码进 buf（至多 4 字节），返回字节数。调用方保证 cp 为合法标量值。
inline int zzEncodeUtf8(char (&buf)[4], char32_t cp)
{
    if (cp < 0x80) {
        buf[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = static_cast<char>(0xC0 | (cp >> 6));
        buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = static_cast<char>(0xE0 | (cp >> 12));
        buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    buf[0] = static_cast<char>(0xF0 | (cp >> 18));
    buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

// 便捷封装：编码并追加到 out。
inline void zzAppendCodePoint(std::string& out, char32_t cp)
{
    char buf[4];
    const int n = zzEncodeUtf8(buf, cp);
    out.append(buf, static_cast<std::size_t>(n));
}
