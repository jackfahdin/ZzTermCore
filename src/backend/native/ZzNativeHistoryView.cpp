#include "ZzNativeHistoryView.h"

#include <ZzTerm/Line.h>
#include <ZzTerm/Screen.h>
#include <ZzTerm/Scrollback.h>

#include "unicode/Utf8Encode.h"
#include "screen/HyperlinkPool.h"

namespace {

// ZzLineView 内联存储的是 const ZzLine*（指针值），thunk 需先取指针再解引用。
const ZzLine& lineFrom(const void* storage)
{
    return **static_cast<const ZzLine* const*>(storage);
}

} // namespace

ZzNativeHistoryView::ZzNativeHistoryView(const ZzScreen& screen,
                                         const ZzScrollback& scrollback,
                                         const std::uint64_t& generation,
                                         const std::uint64_t& clearCount) noexcept
    : screen_(&screen), scrollback_(&scrollback), generation_(&generation),
      clearCount_(&clearCount)
{
}

std::size_t ZzNativeHistoryView::lineCount() const noexcept
{
    if (screen_->activeBuffer() == ZzScreenBuffer::Alternate)
        return 0; // Alternate 无历史（与内部 LineSource 同一规则）
    return scrollback_->lineCount();
}

std::size_t ZzNativeHistoryView::primaryLineCount() const noexcept
{
    return scrollback_->lineCount(); // buffer 无关：Alternate 期间仍真实行数
}

ZzLineView ZzNativeHistoryView::lineAt(std::size_t index) const
{
    return ZzLineView(&scrollback_->lineAt(index), &cellAtThunk, &cellCountThunk,
                      &wrappedThunk);
}

std::uint64_t ZzNativeHistoryView::droppedLineCount() const noexcept
{
    // 绝对行号基线（M19 减法口径，E-2）：累计裁掉 − 累计注入归还；
    // 注入归还时基线回退，既有行绝对行号不变。
    return scrollback_->stats().totalDropped - scrollback_->stats().totalPrepended;
}

std::uint64_t ZzNativeHistoryView::generation() const noexcept
{
    return *generation_;
}

std::uint64_t ZzNativeHistoryView::clearCount() const noexcept
{
    return *clearCount_; // M20：ED3 清史累计计数（buffer 无关，命令执行口径）
}

std::string ZzNativeHistoryView::hyperlinkUri(std::uint16_t hyperlinkId) const
{
    if (const auto* entry = screen_->hyperlinkPool().lookup(hyperlinkId))
        return entry->uri;
    return {};
}

ZzCellView ZzNativeHistoryView::cellAtThunk(const void* storage, int col)
{
    const ZzLine& line = lineFrom(storage);
    const ZzCell& cell = line.cellAt(col);
    ZzCellView view;
    if (cell.isCluster()) {
        view.text = line.clusterText(cell.clusterIndex());
    } else if (cell.codePoint() != 0) {
        zzAppendCodePoint(view.text, cell.codePoint());
    }
    view.foreground = cell.foreground();
    view.background = cell.background();
    view.attributes = cell.attributes();
    view.width      = cell.width();
    view.hyperlinkId = line.hyperlinkIdAt(col);
    return view;
}

int ZzNativeHistoryView::cellCountThunk(const void* storage) noexcept
{
    return lineFrom(storage).cellCount();
}

bool ZzNativeHistoryView::wrappedThunk(const void* storage) noexcept
{
    return lineFrom(storage).wrapped();
}
