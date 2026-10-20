#pragma once

// 内部头（不安装）：OSC 8 超链接 id→URI 池（M22）。
// 贴 contour 上游 HyperlinkStorage 模型：LRU 容量 1024，id 0 保留为"无链接"。
// intern 命中规则 = (uri, userId) 二元组相同——同二元组必同 id，
// OSC 8 `id=` 参数的跨段关联由此成立（关联键即 hyperlinkId 本身）。

#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ZzHyperlinkEntry {
    std::string uri;
    std::string userId;
};

class ZzHyperlinkPool {
public:
    static constexpr std::size_t kCapacity = 1024;

    /// @brief 登记 (uri, userId)，返回 hyperlinkId（!= 0）。
    ///        同二元组复用既有 id 并触新；满时淘汰最久未用条目。
    std::uint16_t intern(std::string_view uri, std::string_view userId);

    /// @brief 按 id 查条目；0 或未登记/已淘汰返回 nullptr。
    [[nodiscard]] const ZzHyperlinkEntry* lookup(std::uint16_t id) const noexcept;

private:
    // id 空间 1..65535 循环分配；entries_ 以 id 索引（vector，空 = 未占用）。
    struct Slot {
        ZzHyperlinkEntry entry;
        bool occupied = false;
    };
    std::vector<Slot> entries_;                              // 下标即 id
    std::list<std::uint16_t> lru_;                           // 前新后旧
    std::unordered_map<std::string, std::uint16_t> byKey_;   // "uri\x1fuserId" → id
    std::uint16_t nextId_ = 1;
};
