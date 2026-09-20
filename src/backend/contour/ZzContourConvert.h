#pragma once

// vtbackend ↔ ZzTerm 公开类型的转换共享头（inline）：ZzContourBackend（snapshot）
// 与 ZzContourRenderView（零拷贝 thunk）共用同一份映射逻辑，避免两端漂移。
// 输出方向（vtbackend → ZzTerm）在上半部；输入方向（ZzTerm → vtbackend，M3a）在末尾。
// 仅限 contour 库内部使用（C++23）；不暴露给 ZzTermCore 主库。

#include <ZzTerm/Cell.h>

#include <vtbackend/core/CellFlags.hpp>
#include <vtbackend/core/Color.hpp>
#include <vtbackend/grid/CellProxy.hpp>

#include <cstdint>
#include <string>

/// \brief vtbackend::Color → ZzColor；亮 n 号色统一映射为索引 8+n，保留颜色身份。
inline ZzColor zzColor(vtbackend::Color color)
{
    switch (color.type())
    {
        case vtbackend::ColorType::RGB: {
            auto const rgb = color.rgb();
            return ZzColor::Rgb(rgb.red, rgb.green, rgb.blue);
        }
        case vtbackend::ColorType::Indexed:
            return ZzColor::Indexed(color.index());
        case vtbackend::ColorType::Bright:
            return ZzColor::Indexed(static_cast<std::uint8_t>(8 + color.index()));
        case vtbackend::ColorType::Default:
        case vtbackend::ColorType::Undefined: // 无实际消费，统一按默认色
        default:
            return ZzColor::Default();
    }
}

/// \brief vtbackend::CellFlags → ZzCellAttributes（含点线/虚线下划线 SGR 4:4/4:5）。
inline ZzCellAttributes zzAttributes(vtbackend::CellFlags flags)
{
    ZzCellAttributes out;
    if (flags.contains(vtbackend::CellFlag::Bold)) out.setBold(true);
    if (flags.contains(vtbackend::CellFlag::Faint)) out.setFaint(true);
    if (flags.contains(vtbackend::CellFlag::Italic)) out.setItalic(true);
    if (flags.contains(vtbackend::CellFlag::Underline)) out.setUnderline(ZzUnderlineStyle::Single);
    if (flags.contains(vtbackend::CellFlag::DoublyUnderlined)) out.setUnderline(ZzUnderlineStyle::Double);
    if (flags.contains(vtbackend::CellFlag::CurlyUnderlined)) out.setUnderline(ZzUnderlineStyle::Curly);
    if (flags.contains(vtbackend::CellFlag::DottedUnderline)) out.setUnderline(ZzUnderlineStyle::Dotted);
    if (flags.contains(vtbackend::CellFlag::DashedUnderline)) out.setUnderline(ZzUnderlineStyle::Dashed);
    if (flags.contains(vtbackend::CellFlag::Blinking)) out.setBlink(ZzBlinkStyle::Slow);
    if (flags.contains(vtbackend::CellFlag::RapidBlinking)) out.setBlink(ZzBlinkStyle::Rapid);
    if (flags.contains(vtbackend::CellFlag::Inverse)) out.setInverse(true);
    if (flags.contains(vtbackend::CellFlag::Hidden)) out.setInvisible(true);
    if (flags.contains(vtbackend::CellFlag::CrossedOut)) out.setStrikethrough(true);
    return out;
}

/// \brief const 版 CellProxy → ZzCellWidth（WideCharContinuation 优先于宽度判定）。
inline ZzCellWidth zzWidth(const vtbackend::BasicCellProxy<true>& cell)
{
    if (cell.flags().contains(vtbackend::CellFlag::WideCharContinuation))
        return ZzCellWidth::WideContinuation;
    return cell.width() == 2 ? ZzCellWidth::WideLead : ZzCellWidth::Narrow;
}

/// \brief 单个 codepoint 编码为 UTF-8 追加到 out。
/// 与 ZzNativeRenderView.cpp 的 appendUtf8 同款逻辑各持一份属有意为之：
/// native 在主库 C++20、contour 在独立库 C++23，跨库共享无合适落点。
inline void zzAppendUtf8(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// ---- ZzTerm → vtbackend（输入方向，M3a）----

#include <vtbackend/input/InputGenerator.hpp>

#include <ZzTerm/Input.h>

#include <optional>

/// \brief ZzKeyModifier → vtbackend::Modifiers（Shift/Alt/Ctrl/Super 一一对应）。
inline vtbackend::Modifiers zzModifiers(ZzKeyModifier mods)
{
    vtbackend::Modifiers out;
    if (zzHasModifier(mods, ZzKeyModifier::Shift))
        out.enable(vtbackend::Modifier::Shift);
    if (zzHasModifier(mods, ZzKeyModifier::Alt))
        out.enable(vtbackend::Modifier::Alt);
    if (zzHasModifier(mods, ZzKeyModifier::Ctrl))
        out.enable(vtbackend::Modifier::Control);
    if (zzHasModifier(mods, ZzKeyModifier::Super))
        out.enable(vtbackend::Modifier::Super);
    return out;
}

/// \brief ZzKeyEvent::Key → vtbackend::Key；Character 与未覆盖键返回 nullopt
///（Character 由调用方走 sendCharEvent 路径）。
inline std::optional<vtbackend::Key> zzKey(ZzKeyEvent::Key key)
{
    using ZK = ZzKeyEvent::Key;
    using CK = vtbackend::Key;
    switch (key) {
    case ZK::Enter:     return CK::Enter;
    case ZK::Tab:       return CK::Tab;
    case ZK::Backspace: return CK::Backspace;
    case ZK::Escape:    return CK::Escape;
    case ZK::Up:        return CK::UpArrow;
    case ZK::Down:      return CK::DownArrow;
    case ZK::Left:      return CK::LeftArrow;
    case ZK::Right:     return CK::RightArrow;
    case ZK::Home:      return CK::Home;
    case ZK::End:       return CK::End;
    case ZK::Insert:    return CK::Insert;
    case ZK::Delete:    return CK::Delete;
    case ZK::PageUp:    return CK::PageUp;
    case ZK::PageDown:  return CK::PageDown;
    case ZK::F1:  return CK::F1;
    case ZK::F2:  return CK::F2;
    case ZK::F3:  return CK::F3;
    case ZK::F4:  return CK::F4;
    case ZK::F5:  return CK::F5;
    case ZK::F6:  return CK::F6;
    case ZK::F7:  return CK::F7;
    case ZK::F8:  return CK::F8;
    case ZK::F9:  return CK::F9;
    case ZK::F10: return CK::F10;
    case ZK::F11: return CK::F11;
    case ZK::F12: return CK::F12;
    case ZK::Character:
    default:            return std::nullopt;
    }
}
