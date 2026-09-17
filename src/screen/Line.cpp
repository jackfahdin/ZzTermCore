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
}

void ZzLine::clear(const ZzCell& fill)
{
    std::fill(cells_.begin(), cells_.end(), fill);
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
