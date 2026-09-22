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

/// \brief ZzTerm 侧码点经 unicode 模块 canonical（unicode/Utf8Encode.h，
/// M7c 统一；本库经 PRIVATE include "${CMAKE_SOURCE_DIR}/src" 引用）编码。
#include "unicode/Utf8Encode.h"

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

/// \brief ZzMouseButton → vtbackend::MouseButton。
/// 注意枚举顺序差：Zz 为 None/Left/Middle/Right，vtbackend 为 Left/Right/Middle；
/// None/Release 统一映射为 vtbackend::MouseButton::Release。
inline vtbackend::MouseButton zzMouseButton(ZzMouseButton button)
{
    switch (button) {
    case ZzMouseButton::Left:       return vtbackend::MouseButton::Left;
    case ZzMouseButton::Middle:     return vtbackend::MouseButton::Middle;
    case ZzMouseButton::Right:      return vtbackend::MouseButton::Right;
    case ZzMouseButton::WheelUp:    return vtbackend::MouseButton::WheelUp;
    case ZzMouseButton::WheelDown:  return vtbackend::MouseButton::WheelDown;
    case ZzMouseButton::WheelLeft:  return vtbackend::MouseButton::WheelLeft;
    case ZzMouseButton::WheelRight: return vtbackend::MouseButton::WheelRight;
    case ZzMouseButton::None:
    default:                        return vtbackend::MouseButton::Release;
    }
}
