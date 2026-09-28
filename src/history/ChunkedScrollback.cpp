#include "ZzTerm/Scrollback.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <deque>

#include "../screen/Reflow.h"

// chunked RAM 历史后端（M0 实现骨架）。
//
// 分块策略：std::deque 按 chunk 管理行（每块 kChunkLines 行），追加只在
// 尾部块上进行，裁剪从头部释放（可部分擦除头块，由 headOffset_ 维持定长寻址），避免单行分配与整体搬迁。
// 未来 Hot/Warm/Cold 分层时，本实现对应 Hot 层，Warm(LZ4)/Cold(mmap)
// 以新实现类接入同一 ZzScrollback 接口。

ZzScrollback::~ZzScrollback() = default;

namespace {

class ZzChunkedScrollback final : public ZzScrollback {
public:
    explicit ZzChunkedScrollback(std::size_t maxLines)
        : capacity_(maxLines)
    {
    }

    void append(std::vector<ZzLine> lines) override
    {
        totalAppended_ += lines.size();
        if (capacity_ == 0) {
            totalDropped_ += lines.size();
            return;
        }
        for (auto& line : lines) {
            // 偏移头块同时也是尾块（单块）时可用槽位只有 kChunkLines - headOffset_；
            // 多块时尾块恒为完整 kChunkLines 槽位。
            if (chunks_.empty() ||
                chunks_.back().size() >= kChunkLines - (chunks_.size() == 1 ? headOffset_ : 0))
                chunks_.emplace_back();
            approxBytes_ += sizeof(ZzLine) +
                            static_cast<std::size_t>(line.cellCount()) * sizeof(ZzCell);
            chunks_.back().push_back(std::move(line));
            ++totalLines_;
        }
        trimToCapacity();
    }

    [[nodiscard]] std::size_t lineCount() const noexcept override
    {
        return totalLines_;
    }

    [[nodiscard]] const ZzLine& lineAt(std::size_t index) const override
    {
        // index 0 为最旧一行。调用方保证 index < lineCount()。
        // headOffset_（M8b）：部分裁剪后块 0 不再对齐 256 槽位边界，
        // 物理槽位 = headOffset_ + index；块 0 的向量下标需再减 headOffset_。
        const std::size_t phys  = headOffset_ + index;
        const std::size_t chunk = phys / kChunkLines;
        const std::size_t inner = (phys % kChunkLines) - (chunk == 0 ? headOffset_ : 0);
        return chunks_[chunk][inner];
    }

    void setCapacity(std::size_t maxLines) override
    {
        capacity_ = maxLines;
        trimToCapacity();
    }

    void reflow(int newCols) override
    {
        if (newCols <= 0 || totalLines_ == 0)
            return;
        const int oldCols = chunks_.front().front().cellCount(); // 不变量：全历史同宽
        if (oldCols == newCols)
            return;
        // 流式重组（M8b）：逐块喂入、旧块即时释放，峰值 O(全历史 + 链长)
        // 而非全量 vector 进/出的约 2 倍峰值。
        ZzReflowStreamer streamer(oldCols, newCols);
        std::deque<std::vector<ZzLine>> rebuilt;
        std::vector<ZzLine> produced;
        produced.reserve(kChunkLines);
        // 精确 256 对齐切块（审查修复）：单次 feed/finish 产出可跨多个 256，
        // 必须 while 循环逐块切出，维持"除尾块外每块恰 256 行"的
        // 定长槽位不变量（lineAt/trimToCapacity 依赖）。
        auto cutFullChunks = [&] {
            while (produced.size() >= kChunkLines) {
                rebuilt.emplace_back(produced.begin(),
                                     produced.begin() + static_cast<std::ptrdiff_t>(kChunkLines));
                produced.erase(produced.begin(),
                               produced.begin() + static_cast<std::ptrdiff_t>(kChunkLines));
            }
        };
        while (!chunks_.empty()) {
            std::vector<ZzLine> chunk = std::move(chunks_.front());
            chunks_.pop_front(); // 旧块即时释放，峰值不叠加
            streamer.feed(chunk, produced);
            cutFullChunks();
        }
        streamer.finish(produced);
        cutFullChunks(); // dangling 尾链冲刷产出同样按 256 精确切块（修复轮 2）
        if (!produced.empty())
            rebuilt.push_back(std::move(produced));
        chunks_ = std::move(rebuilt);
        headOffset_ = 0;
        totalLines_ = 0;
        approxBytes_ = 0;
        for (const auto& chunk : chunks_)
            for (const auto& line : chunk) {
                ++totalLines_;
                approxBytes_ += sizeof(ZzLine) +
                                static_cast<std::size_t>(line.cellCount()) * sizeof(ZzCell);
            }
        trimToCapacity();
    }

    [[nodiscard]] std::size_t capacity() const noexcept override
    {
        return capacity_;
    }

    void clear() noexcept override
    {
        chunks_.clear();
        totalLines_ = 0;
        approxBytes_ = 0;
        headOffset_ = 0;
    }

    [[nodiscard]] ZzScrollbackStats stats() const override
    {
        ZzScrollbackStats s;
        s.lineCount = totalLines_;
        s.approxBytes = approxBytes_;
        s.hotLines = totalLines_; // 当前全部位于 Hot(RAM) 层。
        s.totalAppended = totalAppended_;
        s.totalDropped = totalDropped_;
        return s;
    }

private:
    static constexpr std::size_t kChunkLines = 256; ///< 每块行数（M0 经验值，待 benchmark 调优）。

    /// @brief 从最旧一端裁剪至容量内（可部分擦除头块，由 headOffset_ 维持定长寻址）。
    void trimToCapacity()
    {
        while (totalLines_ > capacity_ && !chunks_.empty()) {
            auto& head = chunks_.front();
            const std::size_t removable = std::min(totalLines_ - capacity_, head.size());
            for (std::size_t i = 0; i < removable; ++i)
                approxBytes_ -= sizeof(ZzLine) +
                                static_cast<std::size_t>(head[i].cellCount()) * sizeof(ZzCell);
            head.erase(head.begin(), head.begin() + static_cast<std::ptrdiff_t>(removable));
            totalLines_ -= removable;
            totalDropped_ += removable;
            if (head.empty()) {
                chunks_.pop_front();
                headOffset_ = 0; // 整块释放：新头块从槽位 0 起
            } else {
                headOffset_ += removable; // 部分擦除：块 0 向量位置 0 前移
            }
        }
    }

    std::deque<std::vector<ZzLine>> chunks_; ///< 块链：旧 -> 新。
    std::size_t totalLines_  = 0;
    std::size_t capacity_    = 0;
    std::size_t approxBytes_ = 0;
    std::size_t headOffset_ = 0; ///< 头部块被部分裁剪的槽位数（块 0 向量位置 0 对应定长槽位 headOffset_）。
    std::uint64_t totalAppended_ = 0; ///< 累计入库行数（clear 不复位）。
    std::uint64_t totalDropped_  = 0; ///< 累计裁剪丢弃行数（clear 不复位）。
};

} // namespace

std::unique_ptr<ZzScrollback> zzCreateChunkedScrollback(std::size_t maxLines)
{
    return std::make_unique<ZzChunkedScrollback>(maxLines);
}
