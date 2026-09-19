#pragma once

#include "ZzContourEvents.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// \brief 单元格颜色，保留颜色身份（不预转 RGB）。
struct ZzColor
{
    enum class Tag : std::uint8_t
    {
        Undefined, ///< 未设置
        Default,   ///< 终端默认色
        Indexed,   ///< 调色板索引（含亮色，亮 n 号色映射为索引 8+n）
        RGB        ///< 真彩色，value 为 0xRRGGBB
    };
    Tag tag = Tag::Default;
    std::uint32_t value = 0;
};

inline bool operator==(ZzColor const& a, ZzColor const& b)
{
    return a.tag == b.tag && a.value == b.value;
}
inline bool operator!=(ZzColor const& a, ZzColor const& b)
{
    return !(a == b);
}

/// \brief 单元格标志位掩码。
struct ZzCellFlag
{
    enum : std::uint32_t
    {
        None = 0,
        Bold = 1u << 0,
        Faint = 1u << 1,
        Italic = 1u << 2,
        Underline = 1u << 3,
        Blinking = 1u << 4,
        Inverse = 1u << 5,
        Hidden = 1u << 6,
        CrossedOut = 1u << 7,
        WideCharContinuation = 1u << 8 ///< 宽字符续格（本格无独立内容）
    };
};

/// \brief 快照中的单个单元格（拷贝语义，不引用 Terminal 内部）。
struct ZzContourCell
{
    std::u32string codepoints;         ///< 簇内全部 codepoint；续格与空格为空
    ZzColor foreground;
    ZzColor background;
    std::uint32_t flags = ZzCellFlag::None;
    int width = 1;                     ///< 1 或 2（宽字符首格为 2）
};

/// \brief 光标位置（0 起行列）。
struct ZzContourCursor
{
    int line = 0;
    int column = 0;
};

/// \brief 拷贝式屏幕快照（仅主屏/当前屏可见区域，不含 scrollback）。
struct ZzContourSnapshot
{
    int columns = 0;
    int rows = 0;
    bool alternateScreen = false;
    std::vector<ZzContourCell> cells;  ///< rows * columns，行主序
    std::optional<ZzContourCursor> cursor; ///< 光标不可见时为 nullopt

    ZzContourCell const& at(int line, int column) const
    {
        return cells[static_cast<size_t>(line * columns + column)];
    }
};

/// \brief Contour vtbackend 的 headless 核心封装（M1a）。
/// 同步直驱：feed 即解析；不启动 Terminal 内部线程；拷贝式快照。
class ZzContourBackend
{
public:
    /// \param columns 列数；\param rows 行数；\param events 事件接收方（寿命须包住本对象）；
    /// \param scrollbackLines scrollback 行数上限。
    ZzContourBackend(int columns, int rows, ZzContourEvents& events, int scrollbackLines = 1000);
    ~ZzContourBackend();
    ZzContourBackend(ZzContourBackend const&) = delete;
    ZzContourBackend& operator=(ZzContourBackend const&) = delete;

    /// \brief 同步喂入终端字节流（可多次、可跨任意边界拆分）。
    void feed(std::string_view data);
    /// \brief 调整屏幕行列；cell 像素按固定 8x17 同步。
    void resize(int columns, int rows);

    [[nodiscard]] std::pair<int, int> size() const;
    [[nodiscard]] bool isAlternateScreen() const;
    [[nodiscard]] std::string title() const;
    [[nodiscard]] int historyLineCount() const;
    /// \brief 第 row 行（主屏 0 起）的内容是否自动续到下一行（即该行是自动换行逻辑行的首行）。
    [[nodiscard]] bool lineWrapped(int row) const;
    /// \brief 把缓冲中的终端回传字节（DA 响应等）经 onWriteToTransport 发出。
    void flushReplies();

    /// \brief 取当前屏拷贝式快照（非 const：内部需刷新 RenderBuffer 取光标）。
    ZzContourSnapshot snapshot();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
