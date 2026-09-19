#include "ZzContourRenderView.h"

#include "ZzContourBackend.h"
#include "ZzContourConvert.h"

#include <vtbackend/core/LineFlags.hpp>
#include <vtbackend/screen/Screen.hpp>

#include <type_traits>

namespace {

// ZzLineView 内联存储的行状态（按值；≤24B、trivially copyable）。
// screen 为 const vtbackend::Screen*（经 ZzContourBackend::screenForView 以 void* 出入公开头）。
struct RowRef {
    const void* screen;
    int         row;
    ZzColor     blankBg;  ///< blank 行的 fillAttrs 背景（BCE 擦除场景）
    bool        blank;
};
static_assert(sizeof(RowRef) <= 24, "RowRef 超过 ZzLineView 内联存储");
static_assert(std::is_trivially_copyable_v<RowRef>, "RowRef 须 trivially copyable");

const vtbackend::Screen& screenFrom(const void* storage)
{
    return *static_cast<const vtbackend::Screen*>(static_cast<const RowRef*>(storage)->screen);
}

} // namespace

ZzContourRenderView::ZzContourRenderView(ZzContourBackend& backend, const State& state) noexcept
    : backend_(&backend)
    , state_(&state)
{
}

ZzSize ZzContourRenderView::size() const noexcept
{
    const auto [cols, rows] = backend_->size();
    return ZzSize { cols, rows };
}

bool ZzContourRenderView::isAlternateScreen() const noexcept
{
    return backend_->isAlternateScreen();
}

ZzLineView ZzContourRenderView::lineAt(int row) const
{
    const auto* screen = static_cast<const vtbackend::Screen*>(backend_->screenForView());
    auto const& gridLine = screen->grid().lineAt(vtbackend::LineOffset(row));
    RowRef ref { screen, row, ZzColor::Default(), gridLine.isBlank() };
    if (ref.blank)
        // 空行可携带非默认 fillAttrs 背景（BCE 擦除场景）；与 snapshot() 同款还原。
        ref.blankBg = zzColor(gridLine.storage().fillAttrs.backgroundColor);
    return ZzLineView(ref, &cellAtThunk, &cellCountThunk, &wrappedThunk);
}

ZzCursorState ZzContourRenderView::cursor() const
{
    // cursorPosition 内部 refreshRenderBuffer（非 const 路径），此处 const_cast 收口：
    // 视图为借用式、与 feed 同线程使用，刷新只读渲染缓冲不改变终端语义状态。
    ZzCursorState state;
    if (auto const pos = const_cast<ZzContourBackend*>(backend_)->cursorPosition()) {
        state.position = ZzPosition { pos->first, pos->second };
        state.visible = true;
    } else {
        state.visible = false;
    }
    return state;
}

ZzCellView ZzContourRenderView::cellAtThunk(const void* storage, int col)
{
    const auto& ref = *static_cast<const RowRef*>(storage);
    const auto& screen = screenFrom(storage);
    ZzCellView view;
    if (ref.blank) {
        // 空行产 fillAttrs 背景格，前景保持默认（与 snapshot() 同款）。
        view.background = ref.blankBg;
        return view;
    }
    auto const& gridLine = screen.grid().lineAt(vtbackend::LineOffset(ref.row));
    // 列钳制：行列同时增长时新并入页面的行仍持旧宽度的未物化存储（Grid::resize），
    // 与 snapshot() 同款防御，越界列产默认格。
    if (col < gridLine.size().value) {
        auto const cell = screen.at(vtbackend::LineOffset(ref.row), vtbackend::ColumnOffset(col));
        for (const char32_t cp : cell.codepoints())
            zzAppendUtf8(view.text, cp);
        view.width = zzWidth(cell);
        view.foreground = zzColor(cell.foregroundColor());
        view.background = zzColor(cell.backgroundColor());
        view.attributes = zzAttributes(cell.flags());
    }
    return view;
}

int ZzContourRenderView::cellCountThunk(const void* storage) noexcept
{
    return screenFrom(storage).grid().pageSize().columns.value;
}

bool ZzContourRenderView::wrappedThunk(const void* storage) noexcept
{
    const auto& ref = *static_cast<const RowRef*>(storage);
    const auto& screen = screenFrom(storage);
    // 与 ZzContourBackend::lineWrapped 同语义：看下一行是否带 Wrapped；末行恒 false。
    if (ref.row + 1 >= screen.grid().pageSize().lines.value)
        return false;
    return screen.lineFlags(vtbackend::LineOffset(ref.row + 1)).contains(vtbackend::LineFlag::Wrapped);
}
