#include <ZzTerm/UnwrapView.h>

#include <algorithm>
#include <utility>
#include <vector>

// M17b：拼接索引——统一空间（历史+屏幕）按 wrapped 链分段的派生缓存。
// 双代计数（RenderView.dirtyGeneration + HistoryView.generation）任一
// 变化即失效，下次查询重建。重建 O(物理行数 × 列数)（裁尾逐格扫），
// M6 量级下可接受；不行再优化（YAGNI）。
class ZzUnwrapView::Impl {
public:
    Impl(const ZzRenderView& rv, const ZzHistoryView& hv) noexcept : rv_(rv), hv_(hv) {}

    struct Entry {
        std::int64_t startLine = 0; // 链头统一行号
        int          rows  = 0;     // 链行数
        int          cells = 0;     // 有效全长（裁尾累加）
    };

    // 统一行号 u 的行句柄：u < hist 为历史行，否则屏幕行。
    [[nodiscard]] ZzLineView unifiedLine(std::int64_t u) const
    {
        const auto hist = static_cast<std::int64_t>(hv_.lineCount());
        if (u < hist)
            return hv_.lineAt(static_cast<std::size_t>(u));
        return rv_.lineAt(static_cast<int>(u - hist));
    }

    // 物理行有效格数：裁掉尾部无效格（text 空且非宽字符续格）——
    // 与 reflow 链流逐行裁尾同口径（Reflow.cpp：行尾默认空白恒为填充，
    // 码位 0x20 真空格 text 非空不受影响）。
    static int effectiveCells(const ZzLineView& line)
    {
        int n = line.cellCount();
        while (n > 0) {
            const ZzCellView c = line.cellAt(n - 1);
            if (!c.text.empty() || c.width == ZzCellWidth::WideContinuation)
                break;
            --n;
        }
        return n;
    }

    void ensureFresh()
    {
        const std::uint64_t rg = rv_.dirtyGeneration();
        const std::uint64_t hg = hv_.generation();
        // fresh_ 显式标记首次重建：真代计数首值为 0（Screen.dirtyGeneration_
        // 与历史 generation 均 0 起），不能用 seen 初值错开兜底首次构建。
        if (fresh_ && rg == seenRenderGen_ && hg == seenHistoryGen_)
            return;
        entries_.clear();
        maxCells_ = 0;
        const auto hist = static_cast<std::int64_t>(hv_.lineCount());
        const std::int64_t total = hist + rv_.size().rows;
        std::int64_t u = 0;
        while (u < total) {
            Entry e;
            e.startLine = u;
            std::int64_t v = u;
            for (;;) {
                const ZzLineView line = unifiedLine(v);
                e.cells += effectiveCells(line);
                ++e.rows;
                if (!line.wrapped() || v + 1 >= total)
                    break;
                ++v;
            }
            maxCells_ = std::max(maxCells_, e.cells);
            entries_.push_back(e);
            u = v + 1;
        }
        seenRenderGen_ = rg;
        seenHistoryGen_ = hg;
        fresh_ = true;
    }

    [[nodiscard]] const std::vector<Entry>& entries()
    {
        ensureFresh();
        return entries_;
    }
    // 不触发重建的缓存访问器：stitchedSourceLine/stitchedSourceLineCount
    // 标 noexcept，不得经 entries() 触发可能分配的重建（句柄调用契约
    // 本就要求先经 lineAt/lineCount 建立索引，同 ZzLineView 句柄口径）。
    [[nodiscard]] const std::vector<Entry>& entriesCached() const noexcept { return entries_; }
    [[nodiscard]] int maxCellCount()
    {
        ensureFresh();
        return maxCells_;
    }

    // 拼接行 index 的拼接列 col → 单格（跨链寻址；调用方保证界内）
    [[nodiscard]] ZzCellView cellAt(std::size_t index, int col) const
    {
        const Entry& e = entries_[index];
        std::int64_t u = e.startLine;
        for (;;) {
            const ZzLineView line = unifiedLine(u);
            const int eff = effectiveCells(line);
            if (col < eff)
                return line.cellAt(col);
            col -= eff;
            ++u;
        }
    }

    [[nodiscard]] ZzStitchedPos toStitched(ZzLogicalPos pos)
    {
        ensureFresh();
        if (entries_.empty())
            return {0, 0};
        if (pos.line < 0)
            return {0, 0};
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const Entry& e = entries_[i];
            if (pos.line >= e.startLine && pos.line < e.startLine + e.rows) {
                int col = 0;
                for (std::int64_t u = e.startLine; u < pos.line; ++u)
                    col += effectiveCells(unifiedLine(u));
                col += std::min<int>(pos.col, effectiveCells(unifiedLine(pos.line)));
                return {static_cast<std::int64_t>(i), col};
            }
        }
        // 越界（pos.line 超末行）：钳到末拼接行行尾
        const std::size_t last = entries_.size() - 1;
        return {static_cast<std::int64_t>(last),
                entries_[last].cells > 0 ? entries_[last].cells - 1 : 0};
    }

    [[nodiscard]] ZzLogicalPos fromStitched(std::int64_t line, int col)
    {
        ensureFresh();
        if (entries_.empty())
            return {0, 0};
        line = std::clamp<std::int64_t>(line, 0,
                                        static_cast<std::int64_t>(entries_.size()) - 1);
        const Entry& e = entries_[static_cast<std::size_t>(line)];
        col = std::clamp(col, 0, e.cells > 0 ? e.cells - 1 : 0);
        int acc = 0;
        for (std::int64_t u = e.startLine; u < e.startLine + e.rows; ++u) {
            const int eff = effectiveCells(unifiedLine(u));
            if (col < acc + eff || u == e.startLine + e.rows - 1)
                return {u, eff > 0 ? std::min(col - acc, eff - 1) : 0};
            acc += eff;
        }
        return {e.startLine, 0}; // 不可达（上行末行兜底已返回）
    }

private:
    const ZzRenderView&  rv_;
    const ZzHistoryView& hv_;
    std::vector<Entry>   entries_;
    int                  maxCells_ = 0;
    std::uint64_t        seenRenderGen_ = 0;
    std::uint64_t        seenHistoryGen_ = 0;
    bool                 fresh_ = false; // false = 尚未构建过，首查恒重建
                                         //（代计数 0 起，不能靠初值错开）
};

ZzUnwrapView::ZzUnwrapView(const ZzRenderView& renderView, const ZzHistoryView& historyView)
    : impl_(std::make_unique<Impl>(renderView, historyView))
{
}
ZzUnwrapView::~ZzUnwrapView() = default;

std::size_t ZzUnwrapView::lineCount() const { return impl_->entries().size(); }

ZzStitchedLineView ZzUnwrapView::lineAt(std::size_t index) const noexcept
{
    return ZzStitchedLineView(this, index);
}

int ZzUnwrapView::maxCellCount() const { return impl_->maxCellCount(); }

ZzStitchedPos ZzUnwrapView::toStitched(ZzLogicalPos pos) const { return impl_->toStitched(pos); }

ZzLogicalPos ZzUnwrapView::fromStitched(std::int64_t line, int col) const
{
    return impl_->fromStitched(line, col);
}

int ZzUnwrapView::stitchedCellCount(std::size_t index) const
{
    return impl_->entries()[index].cells;
}

ZzCellView ZzUnwrapView::stitchedCellAt(std::size_t index, int col) const
{
    (void)impl_->entries(); // 确保索引新鲜
    return impl_->cellAt(index, col);
}

std::int64_t ZzUnwrapView::stitchedSourceLine(std::size_t index) const noexcept
{
    return impl_->entriesCached()[index].startLine;
}

int ZzUnwrapView::stitchedSourceLineCount(std::size_t index) const noexcept
{
    return impl_->entriesCached()[index].rows;
}

int ZzStitchedLineView::cellCount() const { return owner_->stitchedCellCount(index_); }
ZzCellView ZzStitchedLineView::cellAt(int col) const { return owner_->stitchedCellAt(index_, col); }
std::int64_t ZzStitchedLineView::sourceLine() const noexcept
{
    return owner_->stitchedSourceLine(index_);
}
int ZzStitchedLineView::sourceLineCount() const noexcept
{
    return owner_->stitchedSourceLineCount(index_);
}
