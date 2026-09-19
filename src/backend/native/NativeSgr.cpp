#include <cstdint>

#include "ZzNativeBackend.h"

// SGR（CSI m）到画笔状态的映射（ECMA-48 §8.3.117 + xterm 扩展）。
// 只改画笔，不修改已有 Cell；print 落格时套用当前画笔。

namespace {

/// 钳位到 0-255 的字节分量（Parser 已钳到 65535，这里再收一层）。
std::uint8_t clampByte(std::int32_t v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<std::uint8_t>(v);
}

} // namespace

void ZzNativeBackend::sgr(const ZzParamSequence& seq)
{
    if (seq.params.empty()) { // CSI m 无参数 = reset
        penAttrs_.reset();
        penFg_ = ZzColor::Default();
        penBg_ = ZzColor::Default();
        return;
    }

    for (std::size_t i = 0; i < seq.params.size(); ++i) {
        const int p = seq.params[i] == ZzParamSequence::kOmitted ? 0
                                                                 : static_cast<int>(seq.params[i]);
        switch (p) {
        case 0: // reset
            penAttrs_.reset();
            penFg_ = ZzColor::Default();
            penBg_ = ZzColor::Default();
            break;
        case 1: penAttrs_.setBold(true); break;
        case 2: penAttrs_.setFaint(true); break;
        case 3: penAttrs_.setItalic(true); break;
        case 4: penAttrs_.setUnderline(ZzUnderlineStyle::Single); break; // 4:x 子参数待 Parser 支持 ':'
        case 5: penAttrs_.setBlink(ZzBlinkStyle::Slow); break;
        case 6: penAttrs_.setBlink(ZzBlinkStyle::Rapid); break;
        case 7: penAttrs_.setInverse(true); break;
        case 8: penAttrs_.setInvisible(true); break;
        case 9: penAttrs_.setStrikethrough(true); break;
        case 22: penAttrs_.setBold(false); penAttrs_.setFaint(false); break;
        case 23: penAttrs_.setItalic(false); break;
        case 24: penAttrs_.setUnderline(ZzUnderlineStyle::None); break;
        case 25: penAttrs_.setBlink(ZzBlinkStyle::None); break;
        case 27: penAttrs_.setInverse(false); break;
        case 28: penAttrs_.setInvisible(false); break;
        case 29: penAttrs_.setStrikethrough(false); break;
        case 38: // 扩展前景色
        case 48: { // 扩展背景色
            ZzColor* target = p == 38 ? &penFg_ : &penBg_;
            const std::int32_t mode =
                i + 1 < seq.params.size() ? seq.params[i + 1] : ZzParamSequence::kOmitted;
            if (mode == 5 && i + 2 < seq.params.size()) { // 256 色：;5;n
                const std::int32_t idx = seq.params[i + 2];
                if (idx >= 0 && idx <= 255)
                    *target = ZzColor::Indexed(static_cast<std::uint8_t>(idx));
                i += 2;
            } else if (mode == 2 && i + 4 < seq.params.size()) { // RGB：;2;r;g;b
                *target = ZzColor::Rgb(clampByte(seq.params[i + 2]),
                                       clampByte(seq.params[i + 3]),
                                       clampByte(seq.params[i + 4]));
                i += 4;
            }
            // 参数不足：静默忽略（不消费后续参数）
            break;
        }
        case 39: penFg_ = ZzColor::Default(); break;
        case 49: penBg_ = ZzColor::Default(); break;
        default:
            if (p >= 30 && p <= 37)
                penFg_ = ZzColor::Indexed(static_cast<std::uint8_t>(p - 30));
            else if (p >= 40 && p <= 47)
                penBg_ = ZzColor::Indexed(static_cast<std::uint8_t>(p - 40));
            else if (p >= 90 && p <= 97)
                penFg_ = ZzColor::Indexed(static_cast<std::uint8_t>(p - 90 + 8));
            else if (p >= 100 && p <= 107)
                penBg_ = ZzColor::Indexed(static_cast<std::uint8_t>(p - 100 + 8));
            // 其余未知参数安全忽略
            break;
        }
    }
}
