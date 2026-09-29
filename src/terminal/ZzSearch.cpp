#include "ZzSearch.h"

#include "../backend/ZzLineSource.h"
#include "../unicode/Utf8Encode.h"

#include <ZzTerm/Cell.h>

#include <cstdint>
#include <string>

namespace {

char foldByte(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

std::string foldString(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (char c : in)
        out.push_back(foldByte(c));
    return out;
}

struct LineTextMap {
    std::string text;   // 行尾空白已修剪
    std::string folded; // ASCII 折叠副本（仅不敏感模式构建）
};

// 组建一条逻辑行的纯文本到 m（规则同 ZzSelectionText 提取：
// 宽字符整字续格跳过、cluster 整串、空单元格输出空格、行尾空白修剪）。
// 单一路径（M10 方案 A）：满足段条件（Empty，或 Narrow 非聚簇且码点不超过
// 127）的连续 cell 走段批量内循环（text/folded 直接下标写入），其余 cell
// （宽格/聚簇/多字节码点）落 emit 逐字处理；不再构建字节→格回映表，
// 命中格偏移由 zzCellRangeForMatch 惰性遍历换算。
// m 由调用方持有跨行复用：进入时两串同步清空（clear 保容量，
// 分配从"每行 3-4 次"降为"全程常数次"，M6 债 1）。
void buildLineText(const ZzIPhysicalLineSource& src,
                   std::size_t firstRow, std::size_t rowCount, bool needFolded,
                   LineTextMap& m)
{
    const int cols = src.cols();
    m.text.clear();
    m.folded.clear();

    // 工作容量一次到位：非聚簇 cell 每格至多 4 字节，4×格数即安全上界，
    // 段批量内循环因此免逐字节越界检查；聚簇整串长度无上界，由 emit 按需
    // 扩容（罕见路径）。结束统一截断到实际长度。
    const std::size_t totalCells = static_cast<std::size_t>(cols) * rowCount;
    m.text.resize(4 * totalCells);
    if (needFolded)
        m.folded.resize(4 * totalCells);
    std::size_t w = 0;

    ZzLine line;
    // emit：非段条件 cell 的字节串逐字写入（宽格/聚簇/多字节码点）。
    auto emit = [&](std::string_view bytes) {
        if (w + bytes.size() > m.text.size()) {
            m.text.resize(w + bytes.size());
            if (needFolded)
                m.folded.resize(w + bytes.size());
        }
        for (char c : bytes) {
            m.text[w] = c;
            if (needFolded)
                m.folded[w] = foldByte(c);
            ++w;
        }
    };
    for (std::size_t r = 0; r < rowCount; ++r) {
        src.lineAt(firstRow + r, line);
        int c = 0;
        while (c < cols) {
            const ZzCell& zc = line.cellAt(c);
            if (zc.width() == ZzCellWidth::Empty ||
                (zc.width() == ZzCellWidth::Narrow && !zc.isCluster() &&
                 zc.codePoint() <= 127)) {
                // 段批量内循环：连续消费满足段条件的 cell；每格恰产 1 字节，
                // Empty 与码点 0 窄格写空格（与 emit 分支同规则）。
                for (;;) {
                    const ZzCell& bc = line.cellAt(c);
                    const char ch = (bc.width() == ZzCellWidth::Empty || bc.codePoint() == 0)
                                        ? ' '
                                        : static_cast<char>(bc.codePoint());
                    m.text[w] = ch;
                    if (needFolded)
                        m.folded[w] = foldByte(ch);
                    ++w;
                    ++c;
                    if (c >= cols)
                        break;
                    const ZzCell& nc = line.cellAt(c);
                    if (nc.width() != ZzCellWidth::Empty &&
                        (nc.width() != ZzCellWidth::Narrow || nc.isCluster() ||
                         nc.codePoint() > 127))
                        break;
                }
                continue;
            }
            if (zc.width() == ZzCellWidth::WideContinuation) {
                ++c;
                continue; // 续格无文本（lead 已取整字）
            }
            if (zc.isCluster()) {
                emit(line.clusterText(zc.clusterIndex()));
            } else if (zc.codePoint() != 0) {
                char buf[4];
                const int n = zzEncodeUtf8(buf, zc.codePoint());
                emit(std::string_view(buf, static_cast<std::size_t>(n)));
            } else {
                emit(" "); // 防御：码点 0 宽格（窄格码点 0 已入段批量）
            }
            ++c;
        }
    }
    // 结束截断 + 行尾空白修剪（text/folded 同步，规则与原实现一致）。
    while (w > 0 && m.text[w - 1] == ' ')
        --w;
    m.text.resize(w);
    if (needFolded)
        m.folded.resize(w);
}

// 惰性回映（M10 方案 A）：命中后遍历该逻辑行的 cells 同步累计字节数，
// 定位 pos 所在单元的格偏移（cellStart）与末字节所在单元末格之后一格（cellEnd）。
// 格步进规则与文本构建完全一致：续格跳过（无字节）、聚簇整串 1 格、
// WideLead 2 格、其余 1 格；Empty 与码点 0 窄格贡献 1 字节空格。
void zzCellRangeForMatch(const ZzIPhysicalLineSource& src,
                         std::size_t firstRow, std::size_t rowCount,
                         std::size_t pos, std::size_t needleLen,
                         std::int32_t& cellStart, std::int32_t& cellEnd)
{
    const int cols = src.cols();
    const std::size_t lastByte = pos + needleLen - 1;
    std::size_t bytePos = 0;   // 当前 cell 首字节的字节偏移
    std::int32_t cell = 0;     // 当前 cell 的格偏移（与文本构建的格步进同步）
    bool startFound = false;
    ZzLine line;
    for (std::size_t r = 0; r < rowCount; ++r) {
        src.lineAt(firstRow + r, line);
        for (int c = 0; c < cols; ++c) {
            const ZzCell& zc = line.cellAt(c);
            if (zc.width() == ZzCellWidth::WideContinuation)
                continue; // 续格无字节（lead 已计 2 格）
            std::size_t byteCount;
            std::int32_t cellCount;
            if (zc.width() == ZzCellWidth::Empty) {
                byteCount = 1; // 空单元格输出空格
                cellCount = 1;
            } else if (zc.isCluster()) {
                byteCount = line.clusterText(zc.clusterIndex()).size();
                cellCount = 1; // 聚簇整串 1 格
            } else if (zc.codePoint() != 0) {
                char buf[4];
                byteCount = static_cast<std::size_t>(zzEncodeUtf8(buf, zc.codePoint()));
                cellCount = zc.width() == ZzCellWidth::WideLead ? 2 : 1;
            } else {
                byteCount = 1; // 码点 0 窄格输出空格
                cellCount = 1;
            }
            if (!startFound && pos < bytePos + byteCount) {
                cellStart = cell; // 多字节码点的中段字节归属所在单元
                startFound = true;
            }
            if (lastByte < bytePos + byteCount) {
                cellEnd = cell + cellCount; // 末字节所在单元末格之后一格
                return;
            }
            bytePos += byteCount;
            cell += cellCount;
        }
    }
    // 不可达（命中必落在文本字节内）；防御兜底，保持非零宽区间。
    if (!startFound)
        cellStart = cell;
    cellEnd = cell > cellStart ? cell : cellStart + 1;
}

} // namespace

std::vector<ZzLogicalRange> zzSearchLines(const ZzIPhysicalLineSource& src,
                                          std::string_view pattern,
                                          ZzSearchOptions options)
{
    std::vector<ZzLogicalRange> out;
    if (pattern.empty() || pattern.find('\n') != std::string_view::npos)
        return out; // 空 pattern；不跨逻辑行匹配（v1 钉死）

    std::string foldedPattern;
    if (!options.caseSensitive)
        foldedPattern = foldString(pattern);
    const std::string_view needle = options.caseSensitive ? pattern : std::string_view(foldedPattern);

    const std::size_t total = src.historyLineCount() + static_cast<std::size_t>(src.screenRowCount());
    std::int64_t logicalLine = 0;
    std::size_t row = 0;
    LineTextMap m;
    while (row < total) {
        std::size_t count = 1;
        while (row + count < total && src.lineWrapped(row + count - 1))
            ++count;
        buildLineText(src, row, count, !options.caseSensitive, m);
        const std::string& hay = options.caseSensitive ? m.text : m.folded;
        std::size_t pos = 0;
        while ((pos = hay.find(needle, pos)) != std::string::npos) {
            std::int32_t cellStart = 0;
            std::int32_t cellEnd   = 0;
            zzCellRangeForMatch(src, row, count, pos, needle.size(), cellStart, cellEnd);
            out.push_back(ZzLogicalRange{{logicalLine, cellStart}, {logicalLine, cellEnd}});
            pos += needle.size(); // 命中不重叠：从 match 末尾继续
        }
        row += count;
        ++logicalLine;
    }
    return out;
}
