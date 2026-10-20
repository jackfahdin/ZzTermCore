#include "HyperlinkPool.h"

namespace {

std::string zzKey(std::string_view uri, std::string_view userId)
{
    std::string key(uri);
    key.push_back('\x1f');
    key.append(userId);
    return key;
}

} // namespace

std::uint16_t ZzHyperlinkPool::intern(std::string_view uri, std::string_view userId)
{
    const std::string key = zzKey(uri, userId);
    if (const auto it = byKey_.find(key); it != byKey_.end()) {
        lru_.remove(it->second);
        lru_.push_front(it->second);
        return it->second;
    }

    if (lru_.size() >= kCapacity) {
        // 满：淘汰最久未用条目并释放其 id（不复用——屏幕上可能仍残留
        // 引用旧 id 的 cell，复用会把残留链接错指到新 URI）。
        const std::uint16_t oldId = lru_.back();
        lru_.pop_back();
        Slot& oldSlot = entries_[oldId];
        byKey_.erase(zzKey(oldSlot.entry.uri, oldSlot.entry.userId));
        oldSlot.occupied = false;
    }

    if (static_cast<std::size_t>(nextId_) >= entries_.size() && entries_.size() < 65536)
        entries_.resize(static_cast<std::size_t>(nextId_) + 1); // 按需增长，保证下标合法

    // 取下一个空槽（nextId_ 回绕为 0 时从 1 重扫；容量 1024 远小于 65535，必有空槽）。
    while (nextId_ == 0 || entries_[nextId_].occupied)
        ++nextId_;
    const std::uint16_t id = nextId_++;

    Slot& slot = entries_[id];
    slot.entry.uri = std::string(uri);
    slot.entry.userId = std::string(userId);
    slot.occupied = true;
    lru_.push_front(id);
    byKey_.emplace(std::move(key), id);
    return id;
}

const ZzHyperlinkEntry* ZzHyperlinkPool::lookup(std::uint16_t id) const noexcept
{
    if (id == 0 || id >= entries_.size() || !entries_[id].occupied)
        return nullptr;
    return &entries_[id].entry;
}
