#include "ZzSearch.h"

#include "../backend/ZzLineSource.h"
#include "ZzUtf8Encode.h"

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
    std::string text;                        // 行尾空白已修剪
    std::string folded;                      // ASCII 折叠副本（仅不敏感模式构建）
    std::vector<std::int32_t> byteToCell;    // text.size()+1 项：字节位置 → 格偏移
    std::vector<std::int32_t> byteToCellEnd; // text.size() 项：字节位置 → 所在单元末格之后一格
};

// 组建一条逻辑行的纯文本与位置回映表到 m（规则同 ZzSelectionText 提取：
// 宽字符整字续格跳过、cluster 整串、空单元格输出空格、行尾空白修剪）。
// m 由调用方持有跨行复用：进入时四表同步清空（vector clear 保容量，
// 分配从"每行 3-4 次"降为"全程常数次"，M6 债 1）。
void buildLineText(const ZzIPhysicalLineSource& src,
                   std::size_t firstRow, std::size_t rowCount, bool needFolded,
                   LineTextMap& m)
{
    const int cols = src.cols();
    m.text.clear();
    m.folded.clear();
    m.byteToCell.clear();
    m.byteToCellEnd.clear();
    std::int32_t cell = 0;
    auto emit = [&](std::string_view bytes, std::int32_t cellCount) {
        for (char c : bytes) {
            m.text.push_back(c);
            m.byteToCell.push_back(cell);
            m.byteToCellEnd.push_back(cell + cellCount);
            if (needFolded)
                m.folded.push_back(foldByte(c));
        }
        cell += cellCount;
    };
    ZzLine line;
    for (std::size_t r = 0; r < rowCount; ++r) {
        src.lineAt(firstRow + r, line);
        for (int c = 0; c < cols; ++c) {
            const ZzCell& zc = line.cellAt(c);
            switch (zc.width()) {
            case ZzCellWidth::WideContinuation:
                continue; // 续格无文本（lead 已取整字）
            case ZzCellWidth::Empty:
                emit(" ", 1); // 行内空白占位；行尾统一修剪
                continue;
            default:
                break;
            }
            if (zc.isCluster()) {
                emit(line.clusterText(zc.clusterIndex()), 1);
            } else if (zc.codePoint() != 0) {
                char buf[4];
                const int n = zzEncodeUtf8(buf, zc.codePoint());
                emit(std::string_view(buf, static_cast<std::size_t>(n)),
                     zc.width() == ZzCellWidth::WideLead ? 2 : 1);
            } else {
                emit(" ", 1);
            }
        }
    }
    // 末尾哨兵先入表（text.size() → 行总长（格）），再行尾空白修剪
    //（text/folded 同步截断；byteToCell 裁到 text.size()+1，哨兵随之落为
    // 首个被修剪空白的格偏移=末字符之后一格，而非含尾随空白的行长；
    // byteToCellEnd 无哨兵，逐字节同步 pop，长度恒为 text.size()）。
    m.byteToCell.push_back(cell);
    while (!m.text.empty() && m.text.back() == ' ') {
        m.text.pop_back();
        m.byteToCellEnd.pop_back();
        if (needFolded)
            m.folded.pop_back();
    }
    m.byteToCell.resize(m.text.size() + 1);
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
            const auto cellStart = m.byteToCell[pos];
            // 末字节所在单元的末格之后一格：命中尾落在单元字节中段时
            // 同样归并整格（规格 5.2），不产零宽区间；match 末尾恰好
            // 对齐单元边界时等价于 byteToCell[pos + needle.size()]。
            const auto cellEnd = m.byteToCellEnd[pos + needle.size() - 1];
            out.push_back(ZzLogicalRange{{logicalLine, cellStart}, {logicalLine, cellEnd}});
            pos += needle.size(); // 命中不重叠：从 match 末尾继续
        }
        row += count;
        ++logicalLine;
    }
    return out;
}
