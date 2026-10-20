#include "ZzTerm/Line.h"

#include <algorithm>

ZzLine::ZzLine() = default;

ZzLine::ZzLine(int cols)
{
    resize(cols);
}

int ZzLine::cellCount() const noexcept
{
    return static_cast<int>(cells_.size());
}

const ZzCell& ZzLine::cellAt(int col) const noexcept
{
    static const ZzCell kEmpty{};
    if (col < 0 || col >= cellCount())
        return kEmpty;
    return cells_[static_cast<std::size_t>(col)];
}

void ZzLine::setCell(int col, const ZzCell& cell) noexcept
{
    if (col < 0 || col >= cellCount())
        return;
    cells_[static_cast<std::size_t>(col)] = cell;
}

void ZzLine::resize(int cols, const ZzCell& fill)
{
    if (cols < 0)
        cols = 0;
    cells_.resize(static_cast<std::size_t>(cols), fill);
    if (!hyperlinks_.empty())
        hyperlinks_.resize(static_cast<std::size_t>(cols), 0);
}

void ZzLine::clear(const ZzCell& fill)
{
    std::fill(cells_.begin(), cells_.end(), fill);
    std::fill(hyperlinks_.begin(), hyperlinks_.end(), 0);
    setWrapped(false);
}

void ZzLine::insertCells(int col, int count, const ZzCell& fill)
{
    const int n = cellCount();
    if (col < 0 || col >= n || count <= 0)
        return;
    count = std::min(count, n - col);
    // 右移 [col, n - count) 到 [col + count, n)，腾出 [col, col + count)。
    std::move_backward(cells_.begin() + col, cells_.begin() + (n - count), cells_.end());
    std::fill(cells_.begin() + col, cells_.begin() + col + count, fill);
    if (!hyperlinks_.empty()) {
        // 侧表与 cells_ 按列对齐：同位置插入 0，末尾溢出截断。
        hyperlinks_.insert(hyperlinks_.begin() + col, static_cast<std::size_t>(count), 0);
        hyperlinks_.resize(cells_.size());
    }
}

void ZzLine::eraseCells(int col, int count, const ZzCell& fill)
{
    const int n = cellCount();
    if (col < 0 || col >= n || count <= 0)
        return;
    count = std::min(count, n - col);
    // 左移 [col + count, n) 到 [col, ...)，行尾用 fill 补齐。
    std::move(cells_.begin() + col + count, cells_.end(), cells_.begin() + col);
    std::fill(cells_.end() - count, cells_.end(), fill);
    if (!hyperlinks_.empty()) {
        // 侧表同步左移，末尾补 0 保持与 cells_ 等长。
        hyperlinks_.erase(hyperlinks_.begin() + col, hyperlinks_.begin() + col + count);
        hyperlinks_.resize(cells_.size(), 0);
    }
}

void ZzLine::setCellHyperlink(int col, std::uint16_t id)
{
    if (col < 0 || col >= static_cast<int>(cells_.size()))
        return;
    if (id != 0) {
        if (hyperlinks_.empty())
            hyperlinks_.assign(cells_.size(), 0);
        hyperlinks_[static_cast<std::size_t>(col)] = id;
        cells_[static_cast<std::size_t>(col)].setHyperlink(true);
    } else {
        if (!hyperlinks_.empty())
            hyperlinks_[static_cast<std::size_t>(col)] = 0;
        cells_[static_cast<std::size_t>(col)].setHyperlink(false);
    }
}

std::uint16_t ZzLine::hyperlinkIdAt(int col) const noexcept
{
    if (col < 0 || col >= static_cast<int>(cells_.size()))
        return 0;
    if (!cells_[static_cast<std::size_t>(col)].hasHyperlink())
        return 0; // bit26 为准：侧表残留条目不可达
    if (hyperlinks_.empty())
        return 0; // 防御：bit 置位但无侧表（不应发生）
    return hyperlinks_[static_cast<std::size_t>(col)];
}

std::uint32_t ZzLine::internCluster(std::string_view utf8)
{
    // M0 简化：不查重，直接追加。索引空间 2^24（见 Cell.h），单行不会耗尽。
    clusters_.emplace_back(utf8);
    return static_cast<std::uint32_t>(clusters_.size() - 1);
}

std::string_view ZzLine::clusterText(std::uint32_t index) const noexcept
{
    if (index >= clusters_.size())
        return {};
    return clusters_[index];
}
