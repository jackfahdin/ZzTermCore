#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ZzTerm/Cell.h"
#include "ZzTerm/Export.h"

/**
 * @file Line.h
 * @brief ZzLine：终端的一行（physical row），Cell 容器与 grapheme cluster 侧表。
 *
 * 术语约定（Architecture.md 第 6/13 节）：
 * - physical row：屏幕上的一行，即本类，固定 cols 个单元格；
 * - logical line：逻辑行，由若干连续 physical row 组成——除最后一行外，
 *   每个组成行的 wrapped() == true（soft wrap），最后一行 wrapped() == false
 *   表示以 hard newline 结束。Copy/Search/Reflow 一律基于 logical line；
 * - ZzLine 自身不感知它属于哪条 logical line，归属关系由容器
 *   （ZzScreen / ZzScrollback）按上述规则推导。
 *
 * 公共 API 不暴露底层 std::vector（Architecture.md 第 6 节要求）。
 */

/**
 * @brief 终端屏幕上的一行（physical row）：固定列数的 ZzCell 容器，
 *        附带 grapheme cluster 侧表与 soft-wrap 标记。
 *
 * 多码位 grapheme cluster 不内嵌在 Cell 中，而是经 internCluster()
 * 登记到本行侧表，Cell 只持有 24 位索引（见 Cell.h 的取舍说明）。
 */
class ZZTERM_API ZzLine {
public:
    /// @brief 构造空行（0 列）。
    ZzLine();

    /**
     * @brief 构造指定列数的行，全部填充空白单元格。
     * @param cols 列数（> 0）。
     */
    explicit ZzLine(int cols);

    /**
     * @brief 返回列数（单元格数）。
     * @return 本行列数。
     */
    [[nodiscard]] int cellCount() const noexcept;

    /**
     * @brief 只读访问指定列的单元格。
     * @param col 列号，0 <= col < cellCount()。
     * @return 单元格常量引用。
     */
    [[nodiscard]] const ZzCell& cellAt(int col) const noexcept;

    /**
     * @brief 写入指定列的单元格（Core 内部使用）。
     * @param col 列号，0 <= col < cellCount()。
     * @param cell 新单元格内容。
     * @note 若 cell 引用了 cluster（isCluster()），调用方必须保证索引
     *       对当前行有效（先用 internCluster 注册）。
     */
    void setCell(int col, const ZzCell& cell) noexcept;

    /**
     * @brief 调整列数。
     * @param cols 目标列数。
     * @param fill 增长时用于填充新列的单元格（通常为空白）。
     * @note 缩短时直接截断。本方法不做 soft-wrap reflow；
     *       reflow 由 Screen/Terminal 在 resize 流程中负责。
     */
    void resize(int cols, const ZzCell& fill = ZzCell{});

    /**
     * @brief 清空整行为指定单元格（默认空白）。
     * @param fill 填充单元格（通常携带当前画笔背景属性）。
     */
    void clear(const ZzCell& fill = ZzCell{});

    /**
     * @brief 在 col 处插入 count 个单元格（右移原有内容，行尾溢出截断）。
     * @param col 插入位置，0 <= col < cellCount()。
     * @param count 插入数量。
     * @param fill 填充单元格。
     */
    void insertCells(int col, int count, const ZzCell& fill = ZzCell{});

    /**
     * @brief 删除 col 处 count 个单元格（左移，行尾用 fill 补齐）。
     * @param col 删除位置，0 <= col < cellCount()。
     * @param count 删除数量。
     * @param fill 行尾补齐用的单元格。
     */
    void eraseCells(int col, int count, const ZzCell& fill = ZzCell{});

    /**
     * @brief 本行末尾是否为 soft wrap（软换行续接到下一 physical row）。
     * @return true 表示本行以软换行结束。
     */
    [[nodiscard]] bool wrapped() const noexcept { return wrapped_; }

    /**
     * @brief 设置 soft wrap 标记。
     * @param wrapped true 表示本行末尾软换行续接下一 physical row。
     */
    void setWrapped(bool wrapped) noexcept { wrapped_ = wrapped; }

    /**
     * @brief 将一个 grapheme cluster（UTF-8 字节串）注册到本行侧表。
     * @param utf8 cluster 的 UTF-8 编码（必须非空）。
     * @return 侧表索引，用于 ZzCell::setCluster()。
     * @note 相同内容不查重（M0 简化），重复注册会产生多个表项；
     *       表项随行存续，不得跨行使用索引。
     */
    std::uint32_t internCluster(std::string_view utf8);

    /**
     * @brief 按索引读取 cluster 的 UTF-8 文本。
     * @param index internCluster 返回的索引。
     * @return UTF-8 字节串视图；索引越界返回空视图。
     * @warning 返回的 string_view 指向行内存储，行对象被修改
     *          （resize/internCluster 等）后可能失效，不得长期持有。
     */
    [[nodiscard]] std::string_view clusterText(std::uint32_t index) const noexcept;

private:
    std::vector<ZzCell>      cells_;    ///< 单元格数组（不对外暴露）。
    std::vector<std::string> clusters_; ///< grapheme cluster 侧表（见 Cell.h 取舍说明）。
    bool                     wrapped_ = false; ///< soft wrap 标记。
};
