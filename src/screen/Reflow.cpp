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
    // 逐行裁尾（M17a-4b）：每行尾部完全默认空白格恒为填充而非内容——
    // autowrap 只在行满触发（末格必有码位）、reflow 拆分各行恒满，
    // wrapped 行尾部 plain-blank 只可能来自填充（M17a Preserve 扩宽 /
    // 宽字符边界 / EL 擦尾），裁掉均正确；码位 0x20 的真空格不受影响。
    // 链流 = 各行裁尾后有效段顺接（链末行裁尾即旧 trimEnd 语义）。
    // 光标跟踪：链内物理坐标（片段号 = chainOffset/oldCols、片段内列 =
    // chainOffset%oldCols）换算为新流偏移 Σ used[0..f-1] + min(col, used[f])，
    // 落在被裁补白区时锚到该片段内容尾。
    const int cursorFrag = trackThis ? cursor->chainOffset / oldCols : -1;
    const int cursorFragCol = trackThis ? cursor->chainOffset % oldCols : 0;

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

    std::size_t streamBase = 0; // 本行首格在新流中的偏移（逐行累加 used）
    for (std::size_t i = 0; i < chainLen; ++i) {
        const ZzLine& srcLine = chainLines[i]; // cluster 文本取自行内
        int used = oldCols;
        while (used > 0 && zzIsPlainBlank(srcLine.cellAt(used - 1)))
            --used;
        const std::size_t cursorTarget =
            streamBase + static_cast<std::size_t>(
                             cursorFrag == static_cast<int>(i)
                                 ? std::min(cursorFragCol, used)
                                 : 0);
        for (int srcCol = 0; srcCol < used; ++srcCol) {
            const ZzCell& cell = srcLine.cellAt(srcCol);
            const std::size_t s = streamBase + static_cast<std::size_t>(srcCol);
            if (cell.width() == ZzCellWidth::WideContinuation)
                continue; // 续格随 lead 再生

            const int w = (cell.width() == ZzCellWidth::WideLead) ? 2 : 1;
            if (outCol + w > newCols) {
                // 宽字符落边界：本行以默认空白收尾，提前换行。
                flushRow(true);
            }

            if (trackThis && !tracked && cursorFrag == static_cast<int>(i)
                && (cursorTarget == s || (w == 2 && cursorTarget == s + 1))) {
                cursor->row = (int)out.size();
                cursor->col = outCol;
                tracked = true;
            }

            ZzCell placed = cell;
            if (placed.isCluster())
                placed.setCluster(row.internCluster(srcLine.clusterText(cell.clusterIndex())));
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
        streamBase += static_cast<std::size_t>(used);
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
                                  ZzReflowCursor* cursor, ZzReflowCursorChain cursorChain)
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
        // M17a：扩列且光标在本链（多行链）时豁免收链——旧宽度布局原样
        // 保留（仅扩宽行存储），光标行位 = 链起点 + 偏移/旧宽、列 = 偏移%旧宽。
        if (cursorChain == ZzReflowCursorChain::Preserve && cursor
            && cursor->chainIndex == chainIndex && newCols > oldCols
            && chainEnd - chainStart > 1) {
            cursor->row = static_cast<int>(out.size()) + cursor->chainOffset / oldCols;
            cursor->col = cursor->chainOffset % oldCols;
            for (std::size_t i = chainStart; i < chainEnd; ++i) {
                ZzLine row = std::move(lines[i]);
                row.resize(newCols); // 扩宽，右侧补默认格；wrapped 旗标随 move 保留
                out.push_back(std::move(row));
            }
        } else {
            const bool trackThis = cursor && cursor->chainIndex == chainIndex;
            zzReflowChain(&lines[chainStart], chainEnd - chainStart, oldCols, newCols, out,
                          cursor, trackThis);
        }
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
