#include "Reflow.h"

#include <algorithm>
#include <cassert>

namespace {

/// @brief 完全默认空白格（可裁）：无文本、默认前景背景、无属性。
bool zzIsPlainBlank(const ZzCell& c)
{
    return c.isEmpty() && c.foreground().isDefault() && c.background().isDefault()
           && c.attributes().raw() == 0;
}

/// @brief 链处理核（M8b 从 zzReflowLines 抽取）：处理一条完整逻辑行链，
/// 重组产出追加到 out。输入只读；trackThis/cursor 仅 vector API 的 screen
/// 路径使用（streamer 传 nullptr/false）。语义与 M4 原实现逐字节一致。
void zzReflowChain(const ZzLine* chainLines, std::size_t chainLen, int oldCols, int newCols,
                   std::vector<ZzLine>& out, ZzReflowCursor* cursor, bool trackThis)
{
    const bool isHardLine = (chainLen == 1) && !chainLines[0].wrapped();

    // 链内容的有效末尾（流偏移，不含）：裁掉末尾完全默认空白格。
    std::size_t trimEnd = chainLen * static_cast<std::size_t>(oldCols);
    while (trimEnd > 0) {
        const std::size_t s = trimEnd - 1;
        const ZzCell& c = chainLines[s / oldCols].cellAt((int)(s % oldCols));
        if (!zzIsPlainBlank(c))
            break;
        --trimEnd;
    }

    bool tracked = false;

    ZzLine row(newCols);
    int outCol = 0;
    int lastContentCol = 0; // 本行最后一个内容格之后的列（兜底用）

    auto flushRow = [&](bool wrapped) {
        row.setWrapped(wrapped);
        out.push_back(std::move(row));
        row = ZzLine(newCols);
        outCol = 0;
        lastContentCol = 0;
    };

    const std::size_t limit = isHardLine
        ? std::min(trimEnd, static_cast<std::size_t>(newCols)) // 硬行截断
        : trimEnd;

    // 热循环：cells_ 连续存储，取行首指针顺序推进源位置。
    std::size_t srcIdx = 0;
    int srcCol = 0;
    const ZzLine* nextLine = &chainLines[srcIdx];
    const ZzCell* nextCells = &nextLine->cellAt(0);
    for (std::size_t s = 0; s < limit; ++s) {
        const ZzLine* srcLine = nextLine;   // 本格所属行（cluster 文本取自此行）
        const ZzCell* srcCells = nextCells;
        const ZzCell& cell = srcCells[srcCol];
        if (++srcCol == oldCols) {
            srcCol = 0;
            if (s + 1 < limit) { // 链尾最后一格之后不再推进，避免越界
                nextLine = &chainLines[++srcIdx];
                nextCells = &nextLine->cellAt(0);
            }
        }
        if (cell.width() == ZzCellWidth::WideContinuation)
            continue; // 续格随 lead 再生

        const int w = (cell.width() == ZzCellWidth::WideLead) ? 2 : 1;
        if (outCol + w > newCols) {
            if (isHardLine)
                break; // 硬行永不多行化：宽字符落边界时直接截断
            // 宽字符落边界：本行以默认空白收尾，提前换行。
            flushRow(true);
        }

        if (trackThis && !tracked
            && (cursor->chainOffset == (int)s
                || (w == 2 && cursor->chainOffset == (int)s + 1))) {
            cursor->row = (int)out.size();
            cursor->col = outCol;
            tracked = true;
        }

        ZzCell placed = cell;
        if (placed.isCluster())
            placed.setCluster(row.internCluster(srcLine->clusterText(cell.clusterIndex())));
        row.setCell(outCol, placed);
        if (w == 2) {
            // 续格再生规则与 ZzNativeBackend::putChar 一致：
            // 仅 width + 前景/背景，不带属性。
            ZzCell cont;
            cont.setWidth(ZzCellWidth::WideContinuation);
            cont.setForeground(cell.foreground());
            cont.setBackground(cell.background());
            row.setCell(outCol + 1, cont);
        }
        outCol += w;
        lastContentCol = outCol;
    }

    // 链末行收尾（硬行也在此收尾）；兜底需在 flushRow 重置 lastContentCol 前取值。
    const int tailContentCol = lastContentCol;
    flushRow(false);

    if (trackThis && !tracked) {
        // 光标落在被裁空白区：兜底到链末行内容尾。
        cursor->row = (int)out.size() - 1;
        cursor->col = std::min(tailContentCol, newCols - 1);
    }
}

} // namespace

std::vector<ZzLine> zzReflowLines(std::vector<ZzLine> lines, int oldCols, int newCols,
                                  ZzReflowCursor* cursor)
{
    std::vector<ZzLine> out;
    if (oldCols <= 0 || newCols <= 0 || oldCols == newCols) {
        // 列宽未变：原样返回（wrapped 标记不动）。
        if (cursor) {
            // 恒等映射：行 = 链起点 + 偏移 / newCols 的近似由调用方保证不触发本分支。
            cursor->row = 0;
            cursor->col = 0;
        }
        return lines;
    }

    std::size_t chainIndex = 0;
    std::size_t chainStart = 0;
    while (chainStart < lines.size()) {
        // 链尾：lines[j].wrapped() 为 true 则 j+1 同链。
        std::size_t chainEnd = chainStart + 1;
        while (chainEnd < lines.size() && lines[chainEnd - 1].wrapped())
            ++chainEnd;
        const bool trackThis = cursor && cursor->chainIndex == chainIndex;
        zzReflowChain(&lines[chainStart], chainEnd - chainStart, oldCols, newCols, out, cursor,
                      trackThis);
        ++chainIndex;
        chainStart = chainEnd;
    }
    return out;
}

ZzReflowStreamer::ZzReflowStreamer(int oldCols, int newCols)
    : oldCols_(oldCols)
    , newCols_(newCols)
{
    // 调用方保证合法且列宽有变化（恒等/非法路径由调用方前置过滤，
    // 语义与 zzReflowLines 早退分支对齐）。
    assert(oldCols_ > 0 && newCols_ > 0 && oldCols_ != newCols_);
}

void ZzReflowStreamer::feed(std::vector<ZzLine>& lines, std::vector<ZzLine>& out)
{
    for (auto& line : lines) {
        assert(line.cellCount() == oldCols_); // 全历史同宽不变量
        pending_.push_back(std::move(line));
        if (!pending_.back().wrapped()) {
            // 链完成（当前行是链尾）：处理并清空暂存（缓冲复用，不逐链分配）。
            zzReflowChain(pending_.data(), pending_.size(), oldCols_, newCols_, out,
                          nullptr, false);
            pending_.clear();
        }
    }
}

void ZzReflowStreamer::finish(std::vector<ZzLine>& out)
{
    // 尾链 dangling wrapped 也按完整链处理（与 zzReflowLines 收尾语义一致）。
    if (!pending_.empty()) {
        zzReflowChain(pending_.data(), pending_.size(), oldCols_, newCols_, out, nullptr, false);
        pending_.clear();
    }
}
