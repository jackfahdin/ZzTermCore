#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "ZzTerm/Export.h"
#include "ZzTerm/Line.h"

/**
 * @file Scrollback.h
 * @brief ZzScrollback：滚动历史抽象接口（M0 为 chunked RAM 实现）。
 *
 * 分层演进路线（Architecture.md 第 6/14 节）：
 * - 第一阶段（当前）：chunked RAM history，按固定大小块分配行存储，
 *   避免单行一个 allocation，也避免单一巨大 vector 的扩容拷贝；
 * - 未来：Hot RAM / Warm LZ4 压缩 / Cold mmap-file 三层。分层信息通过
 *   ZzScrollbackStats 暴露，接口本身不关心层的存在——调用方只依赖
 *   append / lineAt / lineCount / setCapacity；
 * - 百万行为扩展目标；禁止退化为 historyLines × columns × sizeof(Cell)
 *   的单一朴素矩阵（chunked 分配 + 未来压缩即为此约束的回应）。
 *
 * 行序约定：index 0 为最旧一行，index lineCount()-1 为最新一行
 * （紧邻屏幕上方）。logical line 归属规则见 Line.h 注释。
 */

/// @brief 历史后端统计信息（为分层/监控/Inspector 预留）。
struct ZzScrollbackStats {
    std::size_t lineCount   = 0; ///< 当前总行数。
    std::size_t approxBytes = 0; ///< 近似内存占用（字节）。
    // 以下为分层统计，当前 chunked RAM 实现全部计入 hot；warm/cold 恒为 0。
    std::size_t hotLines  = 0;   ///< Hot 层（RAM 明文）行数。
    std::size_t warmLines = 0;   ///< Warm 层（LZ4 压缩）行数，预留。
    std::size_t coldLines = 0;   ///< Cold 层（mmap 文件）行数，预留。
};

/**
 * @brief 滚动历史抽象接口。
 *
 * ownership：实现类独占拥有行数据；append 以值移交所有权，
 * lineAt 返回的引用在下一次 append/clear/setCapacity 后可能失效。
 *
 * 线程安全：非线程安全，与 ZzScreen 同线程使用。
 */
class ZZTERM_API ZzScrollback {
public:
    virtual ~ZzScrollback();

    /**
     * @brief 追加一批从屏幕顶部滚出的行（保持原有先后顺序）。
     * @param lines 滚出行（以值移交所有权，追加后参数处于移后状态）。
     * @note 超出容量时从最旧一端裁掉。实现应保持行的 wrapped 标记，
     *       不做 logical line 合并/拆分。
     */
    virtual void append(std::vector<ZzLine> lines) = 0;

    /**
     * @brief 当前总行数。
     * @return 已保留的历史行数。
     */
    [[nodiscard]] virtual std::size_t lineCount() const noexcept = 0;

    /**
     * @brief 只读访问一行。
     * @param index 行索引，0 为最旧一行。
     * @return 行的常量引用。
     * @warning 返回引用在下一次 append/clear/setCapacity 后可能失效，
     *          不得长期持有。
     */
    [[nodiscard]] virtual const ZzLine& lineAt(std::size_t index) const = 0;

    /**
     * @brief 设置容量上限（行）。
     * @param maxLines 最大保留行数；0 表示不保留历史。
     * @note 缩容立即从最旧一端裁剪。
     */
    virtual void setCapacity(std::size_t maxLines) = 0;

    /**
     * @brief 当前容量上限（行）。
     * @return 容量上限；0 表示不保留历史。
     */
    [[nodiscard]] virtual std::size_t capacity() const noexcept = 0;

    /// @brief 清空全部历史（对应 ED 3）。
    virtual void clear() noexcept = 0;

    /**
     * @brief 统计信息（含分层计数，供 Terminal Inspector 使用）。
     * @return 统计信息快照。
     */
    [[nodiscard]] virtual ZzScrollbackStats stats() const = 0;
};

/**
 * @brief 创建 chunked RAM 历史后端（当前唯一实现）。
 * @param maxLines 容量上限（行）。
 * @return 历史实例，调用方独占拥有。
 * @note 未来出现 Warm/Cold 层实现时，由工厂或组合层选择后端，
 *       ZzTerminal 只依赖 ZzScrollback 接口。
 */
ZZTERM_API std::unique_ptr<ZzScrollback> zzCreateChunkedScrollback(std::size_t maxLines);
