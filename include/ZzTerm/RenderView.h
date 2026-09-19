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
    ZzColor          foreground = ZzColor::Default();
    ZzColor          background = ZzColor::Default();
    ZzCellAttributes attributes;
    ZzCellWidth      width      = ZzCellWidth::Narrow;
};

/// \brief 行只读访问句柄：类型擦除值语义（内联存储后端行状态，无堆分配、无寿命问题）。
/// 拷贝廉价；后端行失效（feed/resize 后）则句柄失效。
class ZZTERM_API ZzLineView final {
public:
    using CellAtFn    = ZzCellView (*)(const void* storage, int col);
    using CellCountFn = int (*)(const void* storage) noexcept;
    using WrappedFn   = bool (*)(const void* storage) noexcept;

    /// \param state 后端行状态（按值存入内联缓冲，须 trivially copyable 且不超过 24 字节）
    template <typename T>
    ZzLineView(const T& state, CellAtFn cellAt, CellCountFn cellCount, WrappedFn wrapped) noexcept
        : cellAtFn_(cellAt), cellCountFn_(cellCount), wrappedFn_(wrapped)
    {
        static_assert(sizeof(T) <= kStorageSize, "ZzLineView 行状态超过内联存储");
        static_assert(std::is_trivially_copyable_v<T>, "ZzLineView 行状态须 trivially copyable");
        new (storage_) T(state);
    }

    [[nodiscard]] int cellCount() const noexcept { return cellCountFn_(storage_); }
    [[nodiscard]] ZzCellView cellAt(int col) const { return cellAtFn_(storage_, col); }
    [[nodiscard]] bool wrapped() const noexcept { return wrappedFn_(storage_); }

private:
    static constexpr std::size_t kStorageSize = 24;
    alignas(void*) unsigned char storage_[kStorageSize] = {};
    CellAtFn    cellAtFn_;
    CellCountFn cellCountFn_;
    WrappedFn   wrappedFn_;
};

/// \brief 后端无关渲染视图接口。dirty 行级形式保留；
/// 粗粒度后端（Contour）实现为「有脏时 rowDirty 恒 true、dirtyRange 恒全行」。
class ZZTERM_API ZzRenderView {
public:
    virtual ~ZzRenderView() = default;
    [[nodiscard]] virtual ZzSize size() const noexcept = 0;
    [[nodiscard]] virtual bool isAlternateScreen() const noexcept = 0;
    [[nodiscard]] virtual ZzLineView lineAt(int row) const = 0;
    [[nodiscard]] virtual ZzCursorState cursor() const = 0;
    [[nodiscard]] virtual std::uint64_t dirtyGeneration() const noexcept = 0;
    [[nodiscard]] virtual bool rowDirty(int row) const noexcept = 0;
    [[nodiscard]] virtual ZzCellRange dirtyRange(int row) const noexcept = 0;
};
