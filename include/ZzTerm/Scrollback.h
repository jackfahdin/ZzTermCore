#pragma once

#include <cstddef>
#include <cstdint>
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
 *   append / lineAt / lineCount / setCapacity / reflow；
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
    std::uint64_t totalAppended = 0; ///< 累计入库行数（含随后被裁的）；为 M5 绝对行号选区坐标铺路。
    std::uint64_t totalDropped  = 0; ///< 累计因容量裁剪丢弃的行数；clear 不复位。
};

/**
 * @brief 滚动历史抽象接口。
 *
 * ownership：实现类独占拥有行数据；append 以值移交所有权，
 * lineAt 返回的引用在下一次 append/clear/setCapacity/reflow/takeNewest 后可能失效。
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
     *       logical line 重组由 reflow() 承担。
     * @note 不变量：历史行宽度必须等于终端当前列宽；resize 路径由
     *       reflow() 重组维持该不变量（调用方保证以当前列宽的行入库）。
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
     * @warning 返回引用在下一次 append/clear/setCapacity/reflow 后可能失效，
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
     * @brief 列向 soft-wrap reflow：全部历史行按新列宽重组（M4）。
     * @param newCols 新列宽（> 0；等于当前行宽或历史为空时为空操作）。
     * @note 不变量：历史行宽度与终端当前列宽一致（resize 先历史后屏幕，
     *       屏幕溢出行以新宽度入库）；重组算法与屏幕区共用 zzReflowLines；
     *       重组后超容量仍从最旧一端裁剪并计入 totalDropped。
     * @note 固有边界：历史与屏幕分域重组，横跨两域的逻辑行会在接缝处
     *       被拆成两条独立链（内容零丢失），与 Contour 统一重组的折行
     *       位置可能不同（M5 选区工作前加 compat 钉住）。
     */
    virtual void reflow(int newCols) = 0;

    /**
     * @brief 从最新端取走最多 n 行并删除（Core 内部使用；M15 行变回抽原语）。
     * @param n 最多取走行数。
     * @return 取走的行（旧到新顺序、以值移交所有权），不足 n 行时全部返回。
     * @note 调用后既有 lineAt 引用失效（同 append 的失效规则）。
     * @note stats 语义：本操作不是容量裁剪——totalDropped 不变；totalAppended
     *       只增不改（绝对行号产生回退空洞，与 contour rotateBuffersRight 的
     *       stableBase 回退同构，选区锚点按不透明行号处理）。
     */
    [[nodiscard]] virtual std::vector<ZzLine> takeNewest(std::size_t n) = 0;

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
