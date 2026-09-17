/**
 * @file Utf8.cpp
 * @brief ZzUtf8Decoder 实现，见 include/ZzTerm/Utf8.h 的标准引用与语义说明。
 *
 * 第二个 continuation 字节的合法区间按 Unicode Standard Table 3-7
 * 逐 lead byte 收紧，从而在序列的最早位置拒绝超长编码、代理区和
 * 超出 U+10FFFF 的码点；这也是 maximal subpart 替换粒度正确的前提。
 */

#include "ZzTerm/Utf8.h"

void ZzUtf8Decoder::reset() noexcept {
    accumulator_ = 0;
    remaining_ = 0;
    lowerBound_ = 0x80;
    upperBound_ = 0xBF;
}

bool ZzUtf8Decoder::decodeByte(std::uint8_t byte, char32_t& codePoint,
                               bool& produced) noexcept {
    produced = false;

    if (remaining_ == 0) {
        if (byte < 0x80) { // ASCII
            codePoint = byte;
            produced = true;
            return true;
        }
        if (byte >= 0xC2 && byte <= 0xDF) { // 2 字节序列
            accumulator_ = byte & 0x1F;
            remaining_ = 1;
            lowerBound_ = 0x80;
            upperBound_ = 0xBF;
            return true;
        }
        if (byte >= 0xE0 && byte <= 0xEF) { // 3 字节序列
            accumulator_ = byte & 0x0F;
            remaining_ = 2;
            switch (byte) {
            case 0xE0: // E0 的第二字节须为 A0..BF，否则是超长编码
                lowerBound_ = 0xA0;
                upperBound_ = 0xBF;
                break;
            case 0xED: // ED 的第二字节须为 80..9F，排除代理区 D800..DFFF
                lowerBound_ = 0x80;
                upperBound_ = 0x9F;
                break;
            default:
                lowerBound_ = 0x80;
                upperBound_ = 0xBF;
                break;
            }
            return true;
        }
        if (byte >= 0xF0 && byte <= 0xF4) { // 4 字节序列
            accumulator_ = byte & 0x07;
            remaining_ = 3;
            switch (byte) {
            case 0xF0: // F0 的第二字节须为 90..BF，否则是超长编码
                lowerBound_ = 0x90;
                upperBound_ = 0xBF;
                break;
            case 0xF4: // F4 的第二字节须为 80..8F，限制码点 <= U+10FFFF
                lowerBound_ = 0x80;
                upperBound_ = 0x8F;
                break;
            default:
                lowerBound_ = 0x80;
                upperBound_ = 0xBF;
                break;
            }
            return true;
        }
        // 裸 continuation（80..BF）、0xC0/0xC1（必为超长编码）、
        // 0xF5..0xFF（RFC 3629 范围之外，包括历史上的 5/6 字节序列头）
        codePoint = ZzReplacementChar;
        produced = true;
        return true;
    }

    if (byte >= lowerBound_ && byte <= upperBound_) {
        accumulator_ = (accumulator_ << 6) | (byte & 0x3F);
        // 区间收紧只作用于第二个字节，其余 continuation 一律 80..BF
        lowerBound_ = 0x80;
        upperBound_ = 0xBF;
        if (--remaining_ == 0) {
            codePoint = static_cast<char32_t>(accumulator_);
            produced = true;
        }
        return true;
    }

    // continuation 非法：已缓存的前缀是非法子序列的 maximal subpart，
    // 由调用方输出一个 U+FFFD；当前字节不被消费，将作为新序列起点重试。
    reset();
    return false;
}

std::vector<char32_t> ZzUtf8Decoder::decodeAll(std::string_view data) {
    std::vector<char32_t> result;
    result.reserve(data.size());
    feed(data, [&result](char32_t codePoint) { result.push_back(codePoint); });
    finish([&result](char32_t codePoint) { result.push_back(codePoint); });
    return result;
}

