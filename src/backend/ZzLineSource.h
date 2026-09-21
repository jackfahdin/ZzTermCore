// ZzIPhysicalLineSource：统一物理行只读数据源（M5a，内部接口）。
// 统一坐标：历史区物理行 [0, historyLineCount()) 在前，屏幕区物理行紧跟其后。
// ZzLine.wrapped() 语义：本行内容续到下一物理行（软换行链，最后一行为 false）。
// 接缝规则（规格 5.3，收口 M4 观察项①）：历史末行 wrapped()==true 即与屏幕
// 首行续接为一条逻辑行；逻辑行拼接与文本提取由 src/terminal/ZzSelectionText
// 统一实现，双后端共用。
// 借用口语义：lineAt(unifiedRow, out) 把行完整覆写到 out，调用方复用缓冲
// 消除逐行分配；Alternate 屏时
// historyLineCount() 须返回 0（Alternate 无历史，规格 5.1）。
#pragma once

#include <ZzTerm/Line.h>

#include <cstddef>
#include <cstdint>

class ZzIPhysicalLineSource {
public:
    virtual ~ZzIPhysicalLineSource() = default;

    [[nodiscard]] virtual std::size_t historyLineCount() const = 0;
    [[nodiscard]] virtual int screenRowCount() const = 0;
    [[nodiscard]] virtual int cols() const = 0;
    // 统一物理行借用口：把第 unifiedRow 行完整覆写到 out（含 cluster 侧表，
    // 两侧均经赋值语义整体替换，无残留）。out 进入时可为任意状态；
    // unifiedRow ∈ [0, historyLineCount()+screenRowCount())。
    // 复用 out 可消除逐行分配（native copy-assign 复用容量；contour
    // move-assign 与值返回开销持平——ZzLine cluster 侧表无公开清理口，
    // contour 容量复用待后续 ZzLine 增补清理口后启用，规格 M6 5.1）。
    virtual void lineAt(std::size_t unifiedRow, ZzLine& out) const = 0;
    // 轻量 wrapped 查询（不得触发整行拷贝；供逻辑行链扫描使用）
    [[nodiscard]] virtual bool lineWrapped(std::size_t unifiedRow) const = 0;
    // 历史头部累计丢弃物理行数（单调不减；供选区锚点平移）
    [[nodiscard]] virtual std::uint64_t droppedLineCount() const = 0;
};
