#include "ZzTerm/Scrollback.h"

#include <algorithm>
#include <deque>

// chunked RAM 历史后端（M0 实现骨架）。
//
// 分块策略：std::deque 按 chunk 管理行（每块 kChunkLines 行），追加只在
// 尾部块上进行，裁剪从头部整块释放，避免单行分配与整体搬迁。
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
        for (auto& line : lines) {
            if (capacity_ == 0)
                return;
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
        const std::size_t chunk = index / kChunkLines;
        const std::size_t inner = index % kChunkLines;
        return chunks_[chunk][inner];
    }

    void setCapacity(std::size_t maxLines) override
    {
        capacity_ = maxLines;
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
    }

    [[nodiscard]] ZzScrollbackStats stats() const override
    {
        ZzScrollbackStats s;
        s.lineCount = totalLines_;
        s.approxBytes = approxBytes_;
        s.hotLines = totalLines_; // 当前全部位于 Hot(RAM) 层。
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
            if (head.empty())
                chunks_.pop_front();
        }
    }

    std::deque<std::vector<ZzLine>> chunks_; ///< 块链：旧 -> 新。
    std::size_t totalLines_  = 0;
    std::size_t capacity_    = 0;
    std::size_t approxBytes_ = 0;
};

} // namespace

std::unique_ptr<ZzScrollback> zzCreateChunkedScrollback(std::size_t maxLines)
{
    return std::make_unique<ZzChunkedScrollback>(maxLines);
}
