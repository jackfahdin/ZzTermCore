#pragma once

#include "ZzContourEvents.h"

#include <ZzTerm/Cell.h>
#include <ZzTerm/Input.h>
#include <ZzTerm/Line.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// \brief 快照中的单个单元格（拷贝语义，不引用 Terminal 内部）。
struct ZzContourCell
{
    std::u32string  codepoints;   ///< 簇内全部 codepoint；续格与空格为空
    ZzColor         foreground = ZzColor::Default();
    ZzColor         background = ZzColor::Default();
    ZzCellAttributes attributes;
    ZzCellWidth     width      = ZzCellWidth::Narrow; ///< WideLead=宽字符首格；WideContinuation=续格
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
    /// 流以残缺 UTF-8 结束时尾部字节暂不下发，待后续 feed 拼回（内部 pendingUtf8 跨 chunk 缓冲）。
    void feed(std::string_view data);
    /// \brief 调整屏幕行列；cell 像素按固定 8x17 同步。
    void resize(int columns, int rows);

    [[nodiscard]] std::pair<int, int> size() const;
    [[nodiscard]] bool isAlternateScreen() const;
    [[nodiscard]] std::string title() const;
    [[nodiscard]] int historyLineCount() const;
    /// \brief 第 row 行（主屏 0 起）的内容是否自动续到下一行（即该行是自动换行逻辑行的首行）。
    [[nodiscard]] bool lineWrapped(int row) const;

    /// \brief 历史第 i 行内容快照（i ∈ [0, historyLineCount())，0 = 最旧）。
    /// 返回 ZzLine 值快照（含 wrapped 标记，ZzLine 语义：续到下一行为 true）。
    [[nodiscard]] ZzLine historyLineSnapshot(int historyIndex) const;
    /// \brief 主屏/当前屏第 row 行内容快照（同上）。
    [[nodiscard]] ZzLine screenLineSnapshot(int row) const;
    /// \brief 历史第 i 行是否续到下一行（等价快照内 wrapped，轻量路径）。
    [[nodiscard]] bool historyLineWrapped(int historyIndex) const;
    /// \brief Grid stable id 下限（容量裁剪丢弃探测；单调，zero-history 分支可回退）。
    [[nodiscard]] std::int64_t stableFloor() const;
    /// \brief 把缓冲中的终端回传字节（DA 响应等）经 onWriteToTransport 发出。
    void flushReplies();

    /// \brief 透传按键事件到 contour Terminal（编码字节经 output 钩子上行）。
    /// Character 键走 sendCharEvent；未覆盖键忽略。
    void sendKeyEvent(const ZzKeyEvent& event);
    /// \brief 普通文本输入。encodeText 恒等语义，经 contour sendRawInput 原样上行
    ///（KAM 开启时按 contour 语义阻断，与 sendKeyEvent 路径一致，注释钉住）。
    void sendText(std::string_view utf8);

    /// \brief 透传鼠标事件到 contour Terminal（网格坐标 → CellLocation；像素坐标缺省）。
    void sendMouseEvent(const ZzMouseEvent& event);
    /// \brief 透传粘贴文本（contour 按自身 bracketed 模式包裹）。
    void sendPasteText(std::string_view utf8);
    /// \brief 透传焦点事件。
    void sendFocusEvent(bool focused);

    /// \brief 取当前屏拷贝式快照（非 const：内部需刷新 RenderBuffer 取光标）。
    ZzContourSnapshot snapshot();

    /// \brief 光标位置与可见性（经 RenderBuffer 路径；不可见时返回 nullopt）。
    /// 内部会 refreshRenderBuffer，非 const。
    std::optional<std::pair<int, int>> cursorPosition();

    /// \brief 内部：仅供 ZzContourRenderView——当前屏指针（void* 保持公开头无 Contour 类型）。
    /// 返回指针随 feed/resize 失效；消费方（thunk 内）static_cast 回 const vtbackend::Screen*。
    [[nodiscard]] const void* screenForView() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
