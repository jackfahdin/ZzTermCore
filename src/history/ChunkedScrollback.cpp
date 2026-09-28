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
            if (chunks_.empty() || chunks_.back().size() >= kChunkLines)
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
        std::vector<ZzLine> all;
        all.reserve(totalLines_);
        for (auto& chunk : chunks_)
            for (auto& line : chunk) {
                assert(line.cellCount() == oldCols); // debug 断言：全历史同宽不变量
                all.push_back(std::move(line));
            }
        all = zzReflowLines(std::move(all), oldCols, newCols);
        chunks_.clear();
        totalLines_ = 0;
        approxBytes_ = 0;
        for (auto& line : all) {
            if (chunks_.empty() || chunks_.back().size() >= kChunkLines)
                chunks_.emplace_back();
            approxBytes_ += sizeof(ZzLine) +
                            static_cast<std::size_t>(line.cellCount()) * sizeof(ZzCell);
            chunks_.back().push_back(std::move(line));
            ++totalLines_;
        }
        headOffset_ = 0; // 重建后块 0 重新对齐槽位 0（T3 重写 reflow 时随函数体一并调整）
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

    /// @brief 从最旧一端裁剪至容量内（整块释放头部块）。
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
