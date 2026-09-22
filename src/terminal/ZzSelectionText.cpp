#include "ZzSelectionText.h"

#include "../backend/ZzLineSource.h"
#include "../unicode/Utf8Encode.h"

#include <ZzTerm/Cell.h>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace {

std::size_t totalRows(const ZzIPhysicalLineSource& src)
{
    return src.historyLineCount() + static_cast<std::size_t>(src.screenRowCount());
}

// 提取一条逻辑行 [colStart, colEnd) 半开列区间的文本（colEnd < 0 表示到行末）。
// span 为该逻辑行的物理行区间 [first, first+count)，由调用方单次扫描提供。
// 宽字符边界归一在此完成。行尾空白（空单元格与 U+0020）修剪。
std::string extractLogicalLine(const ZzIPhysicalLineSource& src,
                               std::pair<std::size_t, std::size_t> span,
                               std::int64_t colStart, std::int64_t colEnd)
{
    const int cols = src.cols();
    std::vector<ZzLine> snapshots;
    snapshots.reserve(span.second);
    for (std::size_t i = 0; i < span.second; ++i) {
        snapshots.emplace_back();
        src.lineAt(span.first + i, snapshots.back());
    }

    const std::int64_t lineLen = static_cast<std::int64_t>(cols) * static_cast<std::int64_t>(span.second);
    std::int64_t begin = std::clamp<std::int64_t>(colStart, 0, lineLen);
    std::int64_t end = colEnd < 0 ? lineLen : std::clamp<std::int64_t>(colEnd, begin, lineLen);
    if (begin >= end)
        return {};

    auto cellAt = [&](std::int64_t offset) -> const ZzCell& {
        return snapshots[static_cast<std::size_t>(offset / cols)]
            .cellAt(static_cast<int>(offset % cols));
    };
    // 边界归一：start 落续格退到 lead；end 落续格进到其后（不拆半字）
    if (begin > 0 && cellAt(begin).width() == ZzCellWidth::WideContinuation)
        --begin;
    if (end < lineLen && cellAt(end).width() == ZzCellWidth::WideContinuation)
        ++end;

    std::string out;
    for (std::int64_t i = begin; i < end; ++i) {
        const ZzCell& cell = cellAt(i);
        switch (cell.width()) {
        case ZzCellWidth::WideContinuation:
            continue; // 续格不输出（lead 已取整字）
        case ZzCellWidth::Empty:
            out.push_back(' '); // 行内空白占位；行尾统一修剪
            continue;
        default:
            break;
        }
        if (cell.isCluster()) {
            const ZzLine& owner = snapshots[static_cast<std::size_t>(i / cols)];
            out += owner.clusterText(cell.clusterIndex());
        } else if (cell.codePoint() != 0) {
            zzAppendCodePoint(out, cell.codePoint());
        } else {
            out.push_back(' ');
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

} // namespace

std::int64_t zzLogicalLineCount(const ZzIPhysicalLineSource& src)
{
    const std::size_t total = totalRows(src);
    std::int64_t count = 0;
    std::size_t row = 0;
    while (row < total) {
        ++count;
        while (row + 1 < total && src.lineWrapped(row))
            ++row;
        ++row;
    }
    return count;
}

std::string zzExtractSelectionText(const ZzIPhysicalLineSource& src,
                                   ZzLogicalPos start,
                                   ZzLogicalPos end)
{
    if (start.line == end.line && start.col == end.col)
        return {};
    // 调用方（ZzSelection::range）已保证 start <= end；防御性交换
    if (end.line < start.line || (end.line == start.line && end.col < start.col))
        std::swap(start, end);
    const std::int64_t logicalCount = zzLogicalLineCount(src);
    if (logicalCount == 0 || start.line >= logicalCount)
        return {};
    start.line = std::max<std::int64_t>(start.line, 0);
    if (end.line >= logicalCount) {
        // 终点行号越界：clamp 到末条逻辑行并视作选到内容末尾
        // （end.col 在 extractLogicalLine 内再 clamp 到行长）。
        end.line = logicalCount - 1;
        end.col = std::numeric_limits<std::int32_t>::max();
    }

    // 单次物理行扫描：先快进跳过 start.line 之前的逻辑行（O(start 前物理行数）），
    // 随后逐条定位 wrapped 链并提取，整体 O(R + 输出大小）。
    const std::size_t total = totalRows(src);
    std::size_t row = 0;
    for (std::int64_t skipped = 0; skipped < start.line; ++skipped) {
        while (row + 1 < total && src.lineWrapped(row))
            ++row;
        ++row;
    }

    std::string out;
    for (std::int64_t line = start.line; line <= end.line; ++line) {
        // row 当前指向本条逻辑行首物理行；顺链得出物理区间 [row, row+count)
        std::size_t count = 1;
        while (row + count < total && src.lineWrapped(row + count - 1))
            ++count;
        if (line != start.line)
            out.push_back('\n'); // 上一条逻辑行结束且后面还有选中行
        const std::int64_t colStart = line == start.line ? start.col : 0;
        const std::int64_t colEnd = line == end.line ? end.col : -1;
        out += extractLogicalLine(src, {row, count}, colStart, colEnd);
        row += count;
    }
    return out;
}
