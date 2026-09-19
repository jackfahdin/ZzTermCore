#pragma once

/// \file
/// \brief 后端无关的只读渲染视图契约（M1b 重写）。
/// Renderer 只能通过本契约读取终端内容；视图为借用式，须与 feed 同线程使用，
/// feed/resize 后既有视图与 ZzLineView 句柄全部失效。非线程安全。

#include <ZzTerm/Cell.h>
#include <ZzTerm/Types.h>

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <type_traits>

/// \brief 值语义单格视图（单格拷贝，非全量复制）。
struct ZzCellView {
    std::string      text;                              ///< UTF-8 文本；空（Empty/续格）时为空串
    ZzColor          foreground = ZzColor::Default();   ///< 前景色（默认 Default）
    ZzColor          background = ZzColor::Default();   ///< 背景色（默认 Default）
    ZzCellAttributes attributes;                        ///< 样式属性位（粗体/斜体/下划线等）
    ZzCellWidth      width      = ZzCellWidth::Narrow;  ///< 单元格宽度（Narrow/Wide）
};

/// \brief 行只读访问句柄：类型擦除值语义（内联存储后端行状态，无堆分配、无寿命问题）。
/// 拷贝廉价；后端行失效（feed/resize 后）则句柄失效。
class ZZTERM_API ZzLineView final {
public:
    using CellAtFn    = ZzCellView (*)(const void* storage, int col);  ///< 取格回调
    using CellCountFn = int (*)(const void* storage) noexcept;         ///< 格数回调
    using WrappedFn   = bool (*)(const void* storage) noexcept;        ///< 自动换行回调

    /**
     * @brief 构造行只读访问句柄。
     * @param state 后端行状态（按值存入内联缓冲，须 trivially copyable 且不超过 24 字节）
     * @param cellAt 取格回调
     * @param cellCount 格数回调
     * @param wrapped 自动换行回调
     */
    template <typename T>
    ZzLineView(const T& state, CellAtFn cellAt, CellCountFn cellCount, WrappedFn wrapped) noexcept
        : cellAtFn_(cellAt), cellCountFn_(cellCount), wrappedFn_(wrapped)
    {
        static_assert(sizeof(T) <= kStorageSize, "ZzLineView 行状态超过内联存储");
        static_assert(std::is_trivially_copyable_v<T>, "ZzLineView 行状态须 trivially copyable");
        new (storage_) T(state);
    }

    /**
     * @brief 行内格数。
     * @return 该行单元格数量。
     */
    [[nodiscard]] int cellCount() const noexcept { return cellCountFn_(storage_); }
    /**
     * @brief 取第 col 格视图。
     * @param col 列号（0 起）。
     * @return 该格的值语义单格视图。
     */
    [[nodiscard]] ZzCellView cellAt(int col) const { return cellAtFn_(storage_, col); }
    /**
     * @brief 行是否因自动换行产生。
     * @return 是则为 true。
     */
    [[nodiscard]] bool wrapped() const noexcept { return wrappedFn_(storage_); }

private:
    static constexpr std::size_t kStorageSize = 24;              ///< 内联存储字节数上限
    alignas(void*) unsigned char storage_[kStorageSize] = {};    ///< 行状态内联缓冲
    CellAtFn    cellAtFn_;                                       ///< 取格回调指针
    CellCountFn cellCountFn_;                                    ///< 格数回调指针
    WrappedFn   wrappedFn_;                                      ///< 自动换行回调指针
};

/// \brief 后端无关渲染视图接口。dirty 行级形式保留；
/// 粗粒度后端（Contour）实现为「有脏时 rowDirty 恒 true、dirtyRange 恒全行」。
class ZZTERM_API ZzRenderView {
public:
    virtual ~ZzRenderView() = default;
    /**
     * @brief 屏幕尺寸。
     * @return 当前工作区尺寸（行/列）。
     */
    [[nodiscard]] virtual ZzSize size() const noexcept = 0;
    /**
     * @brief 是否处于备用屏幕。
     * @return 处于备用屏幕则为 true。
     */
    [[nodiscard]] virtual bool isAlternateScreen() const noexcept = 0;
    /**
     * @brief 取第 row 行句柄。
     * @param row 行号（0 起）。
     * @return 该行只读访问句柄。
     */
    [[nodiscard]] virtual ZzLineView lineAt(int row) const = 0;
    /**
     * @brief 当前光标状态。
     * @return 光标完整状态（位置/形状/可见性/闪烁）。
     * @note 形状/闪烁后端可暂以默认值上报（Contour 当前 shape 恒 Block、
     *       blinking 恒 true）；位置/可见性为完整状态。
     */
    [[nodiscard]] virtual ZzCursorState cursor() const = 0;
    /**
     * @brief 脏代计数。
     * @return 内容每次变更递增的代计数。
     */
    [[nodiscard]] virtual std::uint64_t dirtyGeneration() const noexcept = 0;
    /**
     * @brief 第 row 行是否脏。
     * @param row 行号（0 起）。
     * @return 脏则为 true。
     */
    [[nodiscard]] virtual bool rowDirty(int row) const noexcept = 0;
    /**
     * @brief 第 row 行脏格范围。
     * @param row 行号（0 起）。
     * @return 该行的脏格区间。
     */
    [[nodiscard]] virtual ZzCellRange dirtyRange(int row) const noexcept = 0;
};
