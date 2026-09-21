// ZzSelection：选区模型（M5a）。纯值逻辑，不依赖任何后端类型。
// 半开区间语义：range() 返回 [start, end)；anchor == extent 为空选区。
#pragma once

#include <ZzTerm/Types.h>

#include <cstdint>

class ZzSelection {
public:
    void set(ZzLogicalPos anchor, ZzLogicalPos extent) noexcept;
    void extend(ZzLogicalPos extent) noexcept;
    void clear() noexcept;
    [[nodiscard]] bool empty() const noexcept;
    // 规范化区间 [start, end)；空选区返回 false（start/end 不动）。
    [[nodiscard]] bool range(ZzLogicalPos& start, ZzLogicalPos& end) const noexcept;
    // 历史头部丢弃 delta 个物理行后平移锚点（近似语义：物理计数平移逻辑
    // 序号，规格 5.1；计划头部精化 2）。start 平移为负 clamp 到 {0,0}；
    // 两端都平移为负（内容全部丢弃）时选区清空。
    void onLinesDropped(std::uint64_t delta) noexcept;
    // line clamp 到 [0, lineCount)；空选区为空操作。col 不在此 clamp
    //（逻辑行长度由提取层按行 clamp）。
    void clampTo(std::int64_t lineCount) noexcept;

private:
    ZzLogicalPos anchor_{};
    ZzLogicalPos extent_{};
};
