#include "ZzSelectionText.h"

#include "../backend/ZzLineSource.h"

#include <ZzTerm/Cell.h>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace {

void appendCodePoint(std::string& out, char32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::size_t totalRows(const ZzIPhysicalLineSource& src)
{
    return src.historyLineCount() + static_cast<std::size_t>(src.screenRowCount());
}

// 逻辑行 lineIndex 的物理行区间 [first, first+count)；越界返回 {total, 0}。
std::pair<std::size_t, std::size_t> logicalSpan(const ZzIPhysicalLineSource& src,
                                                std::int64_t lineIndex)
{
    const std::size_t total = totalRows(src);
    std::int64_t current = 0;
    std::size_t row = 0;
    while (row < total) {
        if (current == lineIndex) {
            std::size_t count = 1;
            while (row + count < total && src.lineWrapped(row + count - 1))
                ++count;
            return {row, count};
        }
        while (row + 1 < total && src.lineWrapped(row))
            ++row;
        ++row;
        ++current;
    }
    return {total, 0};
}

// 提取一条逻辑行 [colStart, colEnd) 半开列区间的文本（colEnd < 0 表示到行末）。
// 宽字符边界归一在此完成。行尾空白（空单元格与 U+0020）修剪。
std::string extractLogicalLine(const ZzIPhysicalLineSource& src,
                               std::pair<std::size_t, std::size_t> span,
                               std::int64_t colStart, std::int64_t colEnd)
{
    const int cols = src.cols();
    std::vector<const ZzLine*> lines; // 指向下方 snapshots 的存活期
    std::vector<ZzLine> snapshots;
    snapshots.reserve(span.second);
    for (std::size_t i = 0; i < span.second; ++i)
        snapshots.push_back(src.lineAt(span.first + i));
    for (const ZzLine& l : snapshots)
        lines.push_back(&l);

    const std::int64_t lineLen = static_cast<std::int64_t>(cols) * static_cast<std::int64_t>(span.second);
    std::int64_t begin = std::clamp<std::int64_t>(colStart, 0, lineLen);
    std::int64_t end = colEnd < 0 ? lineLen : std::clamp<std::int64_t>(colEnd, begin, lineLen);
    if (begin >= end)
        return {};

    auto cellAt = [&](std::int64_t offset) -> const ZzCell& {
        return lines[static_cast<std::size_t>(offset / cols)]
            ->cellAt(static_cast<int>(offset % cols));
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
            const ZzLine& owner = *lines[static_cast<std::size_t>(i / cols)];
            out += owner.clusterText(cell.clusterIndex());
        } else if (cell.codePoint() != 0) {
            appendCodePoint(out, cell.codePoint());
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

    std::string out;
    for (std::int64_t line = start.line; line <= end.line; ++line) {
        if (line != start.line)
            out.push_back('\n');
        const auto span = logicalSpan(src, line);
        const std::int64_t colStart = line == start.line ? start.col : 0;
        const std::int64_t colEnd = line == end.line ? end.col : -1;
        out += extractLogicalLine(src, span, colStart, colEnd);
    }
    return out;
}
