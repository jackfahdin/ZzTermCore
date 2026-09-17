#pragma once

#include <cstdint>

#include "ZzTerm/Export.h"
#include "ZzTerm/Types.h"

/**
 * @file Cell.h
 * @brief 终端核心数据模型：ZzCell（单元格）、ZzColor（颜色）、ZzCellAttributes（显示属性）。
 *
 * 设计约束（Architecture.md 第 6 节）：
 * - 禁止假设 1 code point == 1 cell：ZzCell 必须能表达 Empty、Narrow、Wide、
 *   WideContinuation 以及多码位 grapheme cluster；
 * - 颜色模型覆盖 default、ANSI 16（含 bright）、256 色、RGB TrueColor；
 * - 属性位覆盖 bold、faint、italic、underline variants、blink、inverse、
 *   invisible、strikethrough、protected；
 * - 持续关注 sizeof(ZzCell)。
 *
 * 当前布局：sizeof(ZzCell) == 16 字节，alignof(ZzCell) == 4，
 * 由文件末尾 static_assert 保证。任何字段调整都必须同步更新此处记录并评估
 * 对 scrollback 内存占用的影响（10 万行 × 80 列 × 16 B ≈ 128 MB 上限量级）。
 */

/**
 * @brief 单元格颜色（32 位紧凑编码）。
 *
 * 位布局（value_，自高位到低位）：
 * - [31:30] 类别：0 = Default（跟随调色板默认前景/背景），1 = Indexed，2 = Rgb；
 * - Indexed：[7:0] 为调色板索引 0-255。ANSI 16 色与 bright 色即索引 0-15，
 *   与 256 色共用同一索引空间，不再单独区分（语义等价，索引 < 16 即 ANSI 16）；
 * - Rgb：[23:16] R、[15:8] G、[7:0] B（RGB TrueColor）。
 *
 * 取舍说明：用 4 字节标记联合（tagged union）而非 std::variant，
 * 保证 sizeof(ZzColor) == 4 且 constexpr 可构造，避免 Cell 体积膨胀。
 * 调色板解析（索引 -> 实际 RGB）由 Renderer 负责，Core 不做主题假设。
 */
class ZzColor {
public:
    /// @brief 颜色类别。
    enum class Kind : std::uint8_t {
        Default = 0, ///< 默认前景/背景（由调色板决定）。
        Indexed = 1, ///< 调色板索引（0-15 为 ANSI 16/bright，16-255 为 256 色扩展）。
        Rgb     = 2  ///< RGB TrueColor。
    };

    /**
     * @brief 构造默认颜色（Default）。
     * @return 默认颜色，kind() == Kind::Default。
     */
    [[nodiscard]] static constexpr ZzColor Default() noexcept { return ZzColor{kKindDefault}; }

    /**
     * @brief 构造调色板索引颜色。
     * @param index 调色板索引，0-15 为 ANSI 16（含 bright），16-255 为 256 色。
     * @return 对应索引的调色板颜色。
     */
    [[nodiscard]] static constexpr ZzColor Indexed(std::uint8_t index) noexcept
    {
        return ZzColor{kKindIndexed | index};
    }

    /**
     * @brief 构造 RGB TrueColor。
     * @param r 红色分量（0-255）。
     * @param g 绿色分量（0-255）。
     * @param b 蓝色分量（0-255）。
     * @return RGB TrueColor。
     */
    [[nodiscard]] static constexpr ZzColor Rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
    {
        return ZzColor{kKindRgb | (std::uint32_t{r} << 16) | (std::uint32_t{g} << 8) | b};
    }

    /**
     * @brief 返回颜色类别。
     * @return 颜色类别（Default / Indexed / Rgb）。
     */
    [[nodiscard]] constexpr Kind kind() const noexcept
    {
        return static_cast<Kind>(value_ >> 30);
    }

    /**
     * @brief 是否为默认颜色。
     * @return true 表示 kind() == Kind::Default。
     */
    [[nodiscard]] constexpr bool isDefault() const noexcept { return kind() == Kind::Default; }

    /**
     * @brief 返回调色板索引。
     * @return 仅当 kind() == Kind::Indexed 时有意义，否则返回 0。
     */
    [[nodiscard]] constexpr std::uint8_t index() const noexcept
    {
        return static_cast<std::uint8_t>(value_ & 0xFFu);
    }

    /**
     * @brief RGB 红色分量。
     * @return 红色分量（0-255）；仅 kind() == Kind::Rgb 时有意义。
     */
    [[nodiscard]] constexpr std::uint8_t red() const noexcept
    {
        return static_cast<std::uint8_t>((value_ >> 16) & 0xFFu);
    }

    /**
     * @brief RGB 绿色分量。
     * @return 绿色分量（0-255）；仅 kind() == Kind::Rgb 时有意义。
     */
    [[nodiscard]] constexpr std::uint8_t green() const noexcept
    {
        return static_cast<std::uint8_t>((value_ >> 8) & 0xFFu);
    }

    /**
     * @brief RGB 蓝色分量。
     * @return 蓝色分量（0-255）；仅 kind() == Kind::Rgb 时有意义。
     */
    [[nodiscard]] constexpr std::uint8_t blue() const noexcept
    {
        return static_cast<std::uint8_t>(value_ & 0xFFu);
    }

    /// @brief 相等比较（比较 32 位编码值）。
    friend constexpr bool operator==(ZzColor, ZzColor) noexcept = default;

private:
    static constexpr std::uint32_t kKindDefault = 0u << 30;
    static constexpr std::uint32_t kKindIndexed = 1u << 30;
    static constexpr std::uint32_t kKindRgb     = 2u << 30;

    explicit constexpr ZzColor(std::uint32_t value) noexcept : value_(value) {}

    std::uint32_t value_;
};

static_assert(sizeof(ZzColor) == 4, "ZzColor 必须保持 4 字节");

/// @brief 下划线样式（对应 SGR 4:x 系列）。
enum class ZzUnderlineStyle : std::uint8_t {
    None   = 0, ///< 无下划线。
    Single = 1, ///< 单下划线（SGR 4 / 4:1）。
    Double = 2, ///< 双下划线（SGR 4:2）。
    Curly  = 3, ///< 波浪下划线（SGR 4:3）。
    Dotted = 4, ///< 点线下划线（SGR 4:4）。
    Dashed = 5  ///< 虚线下划线（SGR 4:5）。
};

/// @brief 闪烁样式（对应 SGR 5/6）。
enum class ZzBlinkStyle : std::uint8_t {
    None  = 0, ///< 不闪烁。
    Slow  = 1, ///< 慢速闪烁（SGR 5）。
    Rapid = 2  ///< 快速闪烁（SGR 6）。
};

/**
 * @brief 单元格显示属性位集（16 位）。
 *
 * 位布局（自低位起）：
 * - bit 0     bold（SGR 1）
 * - bit 1     faint（SGR 2）
 * - bit 2     italic（SGR 3）
 * - bit 3-5   underline（ZzUnderlineStyle，0-5）
 * - bit 6-7   blink（ZzBlinkStyle，0-2）
 * - bit 8     inverse（SGR 7）
 * - bit 9     invisible（SGR 8）
 * - bit 10    strikethrough（SGR 9）
 * - bit 11    protected（DEC 保护属性）
 * - bit 12-15 预留（未来可用于 underline color 存在位等）
 *
 * 注：xterm 的 underline color（SGR 58/59）暂不内嵌到 Cell（会显著增大
* sizeof(ZzCell)），计划采用按行稀疏侧表存储，预留位为其扩展点。
 */
class ZzCellAttributes {
public:
    /// @brief 构造全零属性（普通文本）。
    constexpr ZzCellAttributes() noexcept = default;

    /// @brief 是否加粗（SGR 1）。
    /// @return true 表示 bold 位置位。
    [[nodiscard]] constexpr bool bold() const noexcept { return bits_ & kBold; }
    /// @brief 是否淡显（SGR 2）。
    /// @return true 表示 faint 位置位。
    [[nodiscard]] constexpr bool faint() const noexcept { return bits_ & kFaint; }
    /// @brief 是否斜体（SGR 3）。
    /// @return true 表示 italic 位置位。
    [[nodiscard]] constexpr bool italic() const noexcept { return bits_ & kItalic; }
    /// @brief 下划线样式（SGR 4:x）。
    /// @return 当前下划线样式。
    [[nodiscard]] constexpr ZzUnderlineStyle underline() const noexcept
    {
        return static_cast<ZzUnderlineStyle>((bits_ >> kUnderlineShift) & kUnderlineMask);
    }
    /// @brief 闪烁样式（SGR 5/6）。
    /// @return 当前闪烁样式。
    [[nodiscard]] constexpr ZzBlinkStyle blink() const noexcept
    {
        return static_cast<ZzBlinkStyle>((bits_ >> kBlinkShift) & kBlinkMask);
    }
    /// @brief 是否反显（SGR 7）。
    /// @return true 表示 inverse 位置位。
    [[nodiscard]] constexpr bool inverse() const noexcept { return bits_ & kInverse; }
    /// @brief 是否隐藏（SGR 8）。
    /// @return true 表示 invisible 位置位。
    [[nodiscard]] constexpr bool invisible() const noexcept { return bits_ & kInvisible; }
    /// @brief 是否删除线（SGR 9）。
    /// @return true 表示 strikethrough 位置位。
    [[nodiscard]] constexpr bool strikethrough() const noexcept { return bits_ & kStrikethrough; }
    /// @brief 是否受保护（DEC 保护属性，擦除操作跳过）。
    /// @return true 表示 protected 位置位。
    [[nodiscard]] constexpr bool isProtected() const noexcept { return bits_ & kProtected; }

    /// @brief 设置加粗（SGR 1）。
    /// @param v true 置位，false 清位。
    constexpr void setBold(bool v) noexcept { setFlag(kBold, v); }
    /// @brief 设置淡显（SGR 2）。
    /// @param v true 置位，false 清位。
    constexpr void setFaint(bool v) noexcept { setFlag(kFaint, v); }
    /// @brief 设置斜体（SGR 3）。
    /// @param v true 置位，false 清位。
    constexpr void setItalic(bool v) noexcept { setFlag(kItalic, v); }
    /// @brief 设置下划线样式（SGR 4:x）。
    /// @param v 下划线样式。
    constexpr void setUnderline(ZzUnderlineStyle v) noexcept
    {
        bits_ = static_cast<std::uint16_t>(
            (bits_ & ~(kUnderlineMask << kUnderlineShift)) |
            (static_cast<std::uint16_t>(v) << kUnderlineShift));
    }
    /// @brief 设置闪烁样式（SGR 5/6）。
    /// @param v 闪烁样式。
    constexpr void setBlink(ZzBlinkStyle v) noexcept
    {
        bits_ = static_cast<std::uint16_t>(
            (bits_ & ~(kBlinkMask << kBlinkShift)) |
            (static_cast<std::uint16_t>(v) << kBlinkShift));
    }
    /// @brief 设置反显（SGR 7）。
    /// @param v true 置位，false 清位。
    constexpr void setInverse(bool v) noexcept { setFlag(kInverse, v); }
    /// @brief 设置隐藏（SGR 8）。
    /// @param v true 置位，false 清位。
    constexpr void setInvisible(bool v) noexcept { setFlag(kInvisible, v); }
    /// @brief 设置删除线（SGR 9）。
    /// @param v true 置位，false 清位。
    constexpr void setStrikethrough(bool v) noexcept { setFlag(kStrikethrough, v); }
    /// @brief 设置 DEC 保护属性。
    /// @param v true 置位，false 清位。
    constexpr void setProtected(bool v) noexcept { setFlag(kProtected, v); }

    /// @brief 复位为普通属性。
    constexpr void reset() noexcept { bits_ = 0; }

    /**
     * @brief 原始位值（调试用）。
     * @return 16 位属性位原始值。
     */
    [[nodiscard]] constexpr std::uint16_t raw() const noexcept { return bits_; }

    /// @brief 相等比较（位值相等）。
    friend constexpr bool operator==(ZzCellAttributes, ZzCellAttributes) noexcept = default;

private:
    static constexpr std::uint16_t kBold          = 1u << 0;
    static constexpr std::uint16_t kFaint         = 1u << 1;
    static constexpr std::uint16_t kItalic        = 1u << 2;
    static constexpr int           kUnderlineShift = 3;
    static constexpr std::uint16_t kUnderlineMask = 0x7u;
    static constexpr int           kBlinkShift    = 6;
    static constexpr std::uint16_t kBlinkMask     = 0x3u;
    static constexpr std::uint16_t kInverse       = 1u << 8;
    static constexpr std::uint16_t kInvisible     = 1u << 9;
    static constexpr std::uint16_t kStrikethrough = 1u << 10;
    static constexpr std::uint16_t kProtected     = 1u << 11;

    constexpr void setFlag(std::uint16_t flag, bool v) noexcept
    {
        bits_ = v ? static_cast<std::uint16_t>(bits_ | flag)
                  : static_cast<std::uint16_t>(bits_ & ~flag);
    }

    std::uint16_t bits_ = 0;
};

static_assert(sizeof(ZzCellAttributes) == 2, "ZzCellAttributes 必须保持 2 字节");

/// @brief 单元格宽度类别。
enum class ZzCellWidth : std::uint8_t {
    Empty            = 0, ///< 空单元格（无文本，仅背景/属性）。
    Narrow           = 1, ///< 窄字符，占 1 列。
    WideLead         = 2, ///< 宽字符（CJK/emoji 等）首格，占 2 列。
    WideContinuation = 3  ///< 宽字符续格（占位，本身无文本，渲染依赖左侧 WideLead）。
};

/**
 * @brief 终端单元格：文本载荷 + 颜色 + 显示属性 + 宽度类别。
 *
 * 文本载荷（text_，32 位）编码：
 * - bit 30 = 0：bits [20:0] 为单个 Unicode 码位（0 表示无文本，
 *   足以覆盖全部合法码位 <= 0x10FFFF）。单码位情形直接内联存储，
 *   覆盖绝大多数 ASCII/CJK 单元格；
 * - bit 30 = 1：bits [23:0] 为 grapheme cluster 索引，指向所属 ZzLine
 *   的 cluster 侧表（ZzLine::internCluster / ZzLine::clusterText），
 *   用于多码位 cluster（combining marks、variation selector、emoji ZWJ
 *   序列等）。
 *
 * 取舍说明（Architecture.md 允许的两种方案权衡）：
 * - 未采用 per-cell SSO 小字符串：即使 8 字节 SSO 也会让 sizeof(ZzCell)
 *   从 16 涨到 24 字节，scrollback 内存放大 50%，而多码位 cluster
 *   在实际输出中占比极低；
 * - 采用 per-line interning 侧表：Cell 固定 16 字节，cluster 只付出
 *   一次间接索引；代价是跨行移动/复制 Cell 时必须随带所属行的 cluster
 *   侧表（由 ZzLine/ZzScreen 内部保证一致性），且索引空间 2^24 足够
 *   单行使用。
 *
 * 内存布局记录（见文件头注释）：text_(4) + fg_(4) + bg_(4) + attrs_(2)
 * + width_(1) + reserved_(1) = 16 字节，对齐 4。
 * reserved_ 预留给未来的 hyperlink id / decoration 标记等稀疏扩展，
 * 当前必须为 0。
 */
struct ZzCell {
    /**
     * @brief 返回宽度类别。
     * @return 单元格宽度类别。
     */
    [[nodiscard]] constexpr ZzCellWidth width() const noexcept { return width_; }

    /**
     * @brief 是否为空单元格（无文本）。Empty 与 Narrow(码位 0) 均视为无文本。
     * @return true 表示无文本。
     */
    [[nodiscard]] constexpr bool isEmpty() const noexcept
    {
        return width_ == ZzCellWidth::Empty ||
               (!isCluster() && codePoint() == 0);
    }

    /**
     * @brief 是否为 grapheme cluster 引用（多码位）。
     * @return true 表示文本载荷为 cluster 索引而非内联码位。
     */
    [[nodiscard]] constexpr bool isCluster() const noexcept
    {
        return (text_ & kClusterFlag) != 0;
    }

    /**
     * @brief 返回单码位值（仅 !isCluster() 时有意义）。
     * @return Unicode 码位，0 表示无文本。
     */
    [[nodiscard]] constexpr char32_t codePoint() const noexcept
    {
        return static_cast<char32_t>(text_ & kCodePointMask);
    }

    /**
     * @brief 返回 cluster 索引（仅 isCluster() 时有意义，配合 ZzLine::clusterText）。
     * @return 所属 ZzLine cluster 侧表中的索引。
     */
    [[nodiscard]] constexpr std::uint32_t clusterIndex() const noexcept
    {
        return text_ & kClusterIndexMask;
    }

    /**
     * @brief 设置宽度类别。
     * @param w 新的宽度类别。
     */
    constexpr void setWidth(ZzCellWidth w) noexcept { width_ = w; }

    /**
     * @brief 设置为单码位窄/宽字符文本。
     * @param cp Unicode 码位（<= 0x10FFFF）。
     * @note 调用方负责保证码位合法性；非法 UTF-8 的替换字符（U+FFFD）
     *       由 UTF-8 decoder 产生，Cell 不做校验。
     */
    constexpr void setCodePoint(char32_t cp) noexcept
    {
        text_ = static_cast<std::uint32_t>(cp) & kCodePointMask;
    }

    /**
     * @brief 设置为 grapheme cluster 引用。
     * @param index 所属 ZzLine cluster 侧表中的索引（< 2^24）。
     */
    constexpr void setCluster(std::uint32_t index) noexcept
    {
        text_ = kClusterFlag | (index & kClusterIndexMask);
    }

    /// @brief 清除文本载荷（不改动颜色/属性/宽度）。
    constexpr void clearText() noexcept { text_ = 0; }

    /// @brief 前景色。
    /// @return 当前前景色。
    [[nodiscard]] constexpr ZzColor foreground() const noexcept { return fg_; }
    /// @brief 背景色。
    /// @return 当前背景色。
    [[nodiscard]] constexpr ZzColor background() const noexcept { return bg_; }
    /// @brief 显示属性。
    /// @return 当前显示属性位集。
    [[nodiscard]] constexpr ZzCellAttributes attributes() const noexcept { return attrs_; }

    /// @brief 设置前景色。
    /// @param c 新前景色。
    constexpr void setForeground(ZzColor c) noexcept { fg_ = c; }
    /// @brief 设置背景色。
    /// @param c 新背景色。
    constexpr void setBackground(ZzColor c) noexcept { bg_ = c; }
    /// @brief 设置显示属性。
    /// @param a 新显示属性位集。
    constexpr void setAttributes(ZzCellAttributes a) noexcept { attrs_ = a; }

    /// @brief 复位为空白单元格（默认色、无属性、Empty、无文本）。
    constexpr void reset() noexcept { *this = ZzCell{}; }

    /// @brief 相等比较（全部字段逐项比较，含 reserved_）。
    friend constexpr bool operator==(const ZzCell&, const ZzCell&) noexcept = default;

private:
    static constexpr std::uint32_t kClusterFlag      = 1u << 30;
    static constexpr std::uint32_t kCodePointMask    = 0x1FFFFFu;   ///< 21 位，覆盖 U+10FFFF
    static constexpr std::uint32_t kClusterIndexMask = 0xFFFFFFu;   ///< 24 位索引空间

    std::uint32_t     text_ = 0;
    ZzColor           fg_   = ZzColor::Default();
    ZzColor           bg_   = ZzColor::Default();
    ZzCellAttributes  attrs_;
    ZzCellWidth       width_    = ZzCellWidth::Empty;
    std::uint8_t      reserved_ = 0; ///< 预留字段，必须为 0（见类注释）。
};

static_assert(sizeof(ZzCell) == 16,
              "sizeof(ZzCell) 应为 16 字节；若失败请重新评估字段布局并更新文件头注释");
static_assert(alignof(ZzCell) == 4, "ZzCell 对齐应为 4 字节");
