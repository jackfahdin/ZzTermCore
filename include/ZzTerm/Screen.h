#pragma once

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "ZzTerm/Cell.h"
#include "ZzTerm/Export.h"
#include "ZzTerm/Line.h"
#include "ZzTerm/Types.h"

/**
 * @file Screen.h
 * @brief ZzScreen：终端工作区（Primary/Alternate 双缓冲区、光标、滚动区、
 *        Tab Stops、模式位、Dirty Tracking）。
 *
 * 职责边界（Architecture.md 第 6/21 节）：
 * - ZzScreen 只知道工作区网格，不知道历史后端类型；滚出屏幕顶部的行通过
 *   ScrollOutCallback 上移给 ZzTerminal，由 Terminal 决定是否进入
 *   ZzScrollback（Alternate Screen 永不产生历史）；
 * - ZzScreen 不做语义解释（无 SGR 画笔状态、无模式到行为的映射），
 *   语义由 ZzTerminal 负责，Screen 提供原语操作；
 * - resize 列变化时的 soft-wrap reflow 由 reflow() 原语承担（M4 落地），
 *   ZzNativeBackend::resize 负责屏幕与历史的协调顺序。
 */

/// @brief 屏幕缓冲区选择。
enum class ZzScreenBuffer : std::uint8_t {
    Primary,   ///< 主屏幕（滚动会进入历史）。
    Alternate  ///< 备用屏幕（vim/htop 等全屏程序使用，不产生历史）。
};

/// @brief 擦除范围模式（ED/EL 的参数语义，0/1/2）。
enum class ZzEraseMode : std::uint8_t {
    ToEnd    = 0, ///< 从光标（含）擦到末尾。
    FromStart = 1, ///< 从开头擦到光标（含）。
    All      = 2  ///< 全部擦除。
};

/**
 * @brief 终端工作区。
 *
 * 内部持有 Primary/Alternate 两套网格，同一时刻只有一套处于活动状态。
 *
 * ownership：ZzScreen 独占拥有其行数据；ScrollOutCallback 以值传递
 * 移交滚出行的所有权。
 *
 * 线程安全：非线程安全。所有方法必须在 Terminal 所在的单一工作线程
 * 调用；Renderer 线程只能经由 ZzRenderView 只读访问。
 */
class ZZTERM_API ZzScreen {
public:
    /**
     * @brief 滚出行回调。仅在 Primary 缓冲区、滚动区为全屏高度时，
     *        行滚出顶部滚动区上沿会触发；参数以值移交行所有权。
     *        Alternate 缓冲区滚动永不触发。
     */
    using ScrollOutCallback = std::function<void(std::vector<ZzLine> lines)>;

    /**
     * @brief 历史回抽回调（M15）。行变扩行且光标贴末行时，ZzScreen 经本回调
     *        向历史后端索取最多 maxLines 行最新历史（旧到新顺序、以值移交
     *        所有权）注入屏幕顶部；无历史可取时返回空向量。
     *        仅为 Primary 缓冲区调用；Alternate 扩行永不触发。
     *        回调返回行宽度须与当前网格列宽一致：行变回抽时列宽不变；
     *        列变 reflow 顶补（M16c）时历史已先完成重组（backend 协调顺序：
     *        先历史后屏幕），返回行为新列宽。
     */
    using HistoryPullCallback = std::function<std::vector<ZzLine>(std::size_t maxLines)>;

    /// @brief 屏幕首行整行擦除时斩断历史末行链标的通知（M17c）。
    ///        仅 Primary 缓冲触发；Alternate 无历史不触发。由持有方接线到
    ///        ZzScrollback::severNewestWrapped。
    ///        回调不得抛异常（severRowLinks 为 noexcept，抛出即 terminate）。
    using SeverSeamLinkCallback = std::function<void()>;

    /**
     * @brief 构造指定尺寸的工作区（Primary 活动）。
     * @param cols 列数（> 0）。
     * @param rows 行数（> 0）。
     */
    ZzScreen(int cols, int rows);

    /**
     * @brief 返回网格尺寸。
     * @return 当前工作区尺寸。
     */
    [[nodiscard]] ZzSize size() const noexcept;

    /**
     * @brief 网格级 resize 原语：行变按条件语义搬行（M15），列向逐行截断/填充。
     * @param cols 新列数（> 0）。
     * @param rows 新行数（> 0）。
     * @note 行变语义（对齐 contour shrinkLines/growLines）：缩行先裁光标下方
     *       行（不入历史），不够裁时把 Primary 顶部行经 ScrollOutCallback
     *       压入历史（无回调则丢弃，同 reflow 溢出语义），光标随内容平移；
     *       扩行仅当光标贴末行时经 HistoryPullCallback 从最新历史回抽注入
     *       顶部，不足部分底部补空。Alternate 缓冲无回调路径：尾部截断/补空。
     * @note 不做列向 reflow；列变化的 soft-wrap reflow 由 reflow() 原语承担，
     *       ZzNativeBackend::resize 协调顺序（先历史后屏幕，M4 已落地）。全屏标脏。
     */
    void resize(int cols, int rows);

    /**
     * @brief 列向 soft-wrap reflow 原语：屏幕区行按新列宽重组（M4）。
     * @param newCols 新列宽（> 0；等于当前列宽或非法时为空操作）。
     * @note Primary/Alternate 两套网格各自重组；重组导致行数超出时，
     *       Primary 顶部溢出行经 ScrollOutCallback 上移（Alternate 溢出
     *       直接丢弃，备用屏无历史）；行数不足时先经 HistoryPullCallback
     *       从最新历史顶补填满（M16c，仅 Primary 且装有回调，内容贴底
     *       锚定；Alternate 永不顶补），余量底部补空行。
     *       光标按"逻辑行链 + 链内偏移"跟随内容映射并 clamp；
     *       wrapPending 清除；滚动区复位全屏；tab stops 按新列宽重建；
     *       全屏标脏。行数不变（行向调整由 resize 负责）。
     * @note M17a：扩列时 Primary 光标所在折链豁免收链（readline 陈旧帧
     *       擦除兼容），详见 docs/Scrollback-and-Reflow.md「光标活动链
     *       保护」。
     */
    void reflow(int newCols);

    /**
     * @brief 把若干行插入 Primary 缓冲区顶部（Core 内部使用；M16b 接缝链归还）。
     * @param lines 待插入行（以值移交所有权，旧到新顺序）。
     * @note 插入后行数可瞬时超过 rows_——由随后的 reflow() 溢出分支裁回
     *       （reflowBuffer 出口恒 rows_）；cursor.position.row 随插入数平移；
     *       wrapPending 清除；dirty 状态由随后的 reflow 全屏标脏自洽。
     * @note 仅作用于 primary_（与当前活动缓冲无关）；Alternate 永不插入。
     *       插入后到 reflow() 之间不得穿插写入路径调用（单线程约定）。
     */
    void prependPrimaryLines(std::vector<ZzLine> lines);

    // ---- 缓冲区 ----

    /**
     * @brief 当前活动缓冲区。
     * @return Primary 或 Alternate。
     */
    [[nodiscard]] ZzScreenBuffer activeBuffer() const noexcept;

    /**
     * @brief 切换活动缓冲区。
     * @param buffer 目标缓冲区。
     * @note 仅切换存储；DECSET 1049 的“保存光标/清屏”等语义由
     *       ZzTerminal 在调用前后完成。切换后全屏标脏。
     */
    void setActiveBuffer(ZzScreenBuffer buffer);

    // ---- 行/单元格访问 ----

    /**
     * @brief 只读访问活动缓冲区的一行。
     * @param row 行号，0 <= row < size().rows。
     * @return 行的常量引用。
     * @warning 返回引用在下一次修改 Screen 的调用后可能失效，不得长期持有。
     */
    [[nodiscard]] const ZzLine& lineAt(int row) const noexcept;

    /**
     * @brief 在活动缓冲区写入一个单元格（最低层原语）。
     * @param pos 目标位置（越界则忽略）。
     * @param cell 单元格内容。
     * @note 不做光标移动、不做 wide/continuation 一致性修复；
     *       一致性由 ZzTerminal 的写入流程负责。
     */
    void putCell(ZzPosition pos, const ZzCell& cell) noexcept;

    /**
     * @brief 向第 row 行的 grapheme cluster 侧表注册 cluster 文本，返回索引。
     *
     * 配合 Cell::setCluster 使用：聚簇续接等场景把新 cluster 串注册进
     * 行侧表后，以索引更新目标格。遵循 putCell/setLineWrapped 的靶向
     * mutator 先例——只开侧表注册一条通道，不开通用可变行口。
     * @param row 物理行号（0 <= row < size().rows，越界忽略并返回 0——
     *        0 是合法侧表索引，越界返回值不得用于 setCluster）。
     * @param utf8 cluster 的 UTF-8 编码（必须非空）。
     * @return 侧表索引（ZzLine::clusterText 可取回文本）。
     */
    std::uint32_t internClusterAt(int row, std::string_view utf8);

    /**
     * @brief 设置活动缓冲区指定行的 soft wrap 标记（DECAWM 换行时使用）。
     * @param row 行号，0 <= row < size().rows（越界忽略）。
     * @param wrapped true 表示本行末尾软换行续接下一 physical row。
     */
    void setLineWrapped(int row, bool wrapped) noexcept;

    // ---- 光标 ----

    /**
     * @brief 活动缓冲区光标状态。
     * @return 光标完整状态（位置/形状/可见性/闪烁）。
     */
    [[nodiscard]] ZzCursorState cursor() const noexcept;

    /**
     * @brief 设置光标位置（clamp 到屏幕边界）。
     * @param pos 目标位置（0 起始绝对屏幕坐标）。
     * @note DECOM（origin mode）下相对滚动区的换算由 ZzTerminal 完成，
     *       本方法只接受绝对屏幕坐标。
     */
    void setCursorPosition(ZzPosition pos) noexcept;

    /**
     * @brief 设置光标形状/可见性/闪烁。
     * @param shape 光标形状（DECSCUSR）。
     * @param visible 是否可见（DECTCEM）。
     * @param blinking 是否闪烁（DECSCUSR 闪烁位）。
     */
    void setCursorStyle(ZzCursorShape shape, bool visible, bool blinking) noexcept;

    /// @brief 保存光标（DECSC / SCOSC 语义的状态副本）。
    void saveCursor() noexcept;

    /// @brief 恢复光标（DECRC / SCORC）；若无已保存状态则复位到左上角。
    void restoreCursor() noexcept;

    /**
     * @brief wrap-pending 标志（xterm 行尾延迟换行语义）。
     * @return true 表示光标停在最后一列且下一个可打印字符将触发换行。
     * @note 本状态按缓冲区独立保存；Save/Restore Cursor 连带保存恢复；
     *       setCursorPosition、erase/insert/delete/scroll 各原语会清除它。
     *       置位逻辑由 ZzTerminal 的写入流程负责，Screen 不自动置位。
     */
    [[nodiscard]] bool wrapPending() const noexcept;

    /**
     * @brief 设置 wrap-pending 标志。
     * @param pending true 置位，false 清除。
     */
    void setWrapPending(bool pending) noexcept;

    // ---- 滚动区 / Tab Stops / 模式位 ----

    /**
     * @brief 设置滚动区（DECSTBM）。
     * @param topRow 上沿（0 起始，含）。
     * @param bottomRow 下沿（0 起始，含）。
     * @note 非法参数（top >= bottom、越界）被忽略并复位为全屏。
     */
    void setScrollRegion(int topRow, int bottomRow) noexcept;

    /// @brief 复位滚动区为全屏。
    void resetScrollRegion() noexcept;

    /**
     * @brief 当前滚动区行范围（0 起始）。
     * @return 滚动区行范围；复用 ZzCellRange 的半开区间约定：startCol 为
     *         上沿（含），endCol 为下沿 + 1（不含）。
     */
    [[nodiscard]] ZzCellRange scrollRegionRows() const noexcept;

    /**
     * @brief 在指定列设置 Tab Stop（HTS）。
     * @param col 列号（0 起始，越界忽略）。
     */
    void setTabStop(int col);

    /**
     * @brief 清除指定列 Tab Stop；col < 0 时清除全部（TBC 3）。
     * @param col 列号（0 起始）；负数表示清除全部 Tab Stop。
     */
    void clearTabStop(int col);

    /**
     * @brief 返回 col 之后（不含）的下一个 Tab Stop 列号。
     * @param col 起始列（不含）。
     * @return 下一个 Tab Stop；不存在则返回最后一列。
     */
    [[nodiscard]] int nextTabStop(int col) const noexcept;

    /**
     * @brief 设置 Origin Mode（DECOM）标志；语义解释在 ZzTerminal。
     * @param on true 开启 origin mode。
     */
    void setOriginMode(bool on) noexcept;
    /**
     * @brief Origin Mode（DECOM）当前状态。
     * @return 当前模式状态。
     */
    [[nodiscard]] bool originMode() const noexcept;

    /**
     * @brief 设置 Insert Mode（IRM）标志；语义解释在 ZzTerminal。
     * @param on true 开启插入模式。
     */
    void setInsertMode(bool on) noexcept;
    /**
     * @brief Insert Mode（IRM）当前状态。
     * @return 当前模式状态。
     */
    [[nodiscard]] bool insertMode() const noexcept;

    /**
     * @brief 设置 Auto-wrap Mode（DECAWM）标志；语义解释在 ZzTerminal。
     * @param on true 开启自动换行。
     */
    void setAutoWrapMode(bool on) noexcept;
    /**
     * @brief Auto-wrap Mode（DECAWM）当前状态。
     * @return 当前模式状态。
     */
    [[nodiscard]] bool autoWrapMode() const noexcept;

    // ---- 擦除 / 插入 / 删除 / 滚动 ----

    /**
     * @brief 行内擦除（EL 0/1/2），基于当前光标列。
     * @param mode 擦除范围。
     * @param fill 填充单元格（通常携带当前画笔的背景属性）。
     * @note M17c 斩链：擦除范围覆盖整行（ToEnd 起点列 0 / All /
     *       FromStart 终点末列）时，本行出链与前驱入链一并斩断——整行
     *       擦除 = 内容死亡 = 从折链摘除。部分擦除视为编辑，不动链标。
     */
    void eraseInLine(ZzEraseMode mode, const ZzCell& fill) noexcept;

    /**
     * @brief 屏幕擦除（ED 0/1/2），基于当前光标位置。
     * @param mode 擦除范围。
     * @param fill 填充单元格。
     * @note ED 3（清滚动历史）不由本方法处理；历史属于 ZzTerminal/
     *       ZzScrollback 职责。
     * @note M17c 斩链：被整行覆盖的行按 eraseInLine 同规则斩链；row 0
     *       被整行覆盖时经 SeverSeamLinkCallback 通知斩断历史末行链标。
     */
    void eraseInDisplay(ZzEraseMode mode, const ZzCell& fill) noexcept;

    /**
     * @brief 光标处向右插入 count 个空白单元格（ICH）。
     * @param count 插入数量。
     * @param fill 填充单元格（通常携带当前画笔背景属性）。
     */
    void insertCells(int count, const ZzCell& fill) noexcept;

    /**
     * @brief 光标处删除 count 个单元格（DCH）。
     * @param count 删除数量。
     * @param fill 行尾补齐用的填充单元格。
     */
    void deleteCells(int count, const ZzCell& fill) noexcept;

    /**
     * @brief 光标行处在滚动区内插入 count 行（IL），底部行丢弃。
     * @param count 插入行数。
     * @param fill 新行填充单元格。
     */
    void insertLines(int count, const ZzCell& fill);

    /**
     * @brief 光标行处在滚动区内删除 count 行（DL），底部补空行。
     * @param count 删除行数。
     * @param fill 底部补空行的填充单元格。
     * @note 满足 ScrollOutCallback 触发条件时，被删除的行经回调上移。
     */
    void deleteLines(int count, const ZzCell& fill);

    /**
     * @brief 滚动区内向上滚动 count 行（SU / 行末 LF 滚动）。
     * @param count 滚动行数。
     * @param fill 底部补空行的填充单元格。
     * @note 满足 ScrollOutCallback 触发条件时，滚出上沿的行经回调上移。
     */
    void scrollUp(int count, const ZzCell& fill);

    /**
     * @brief 滚动区内向下滚动 count 行（SD / RI 滚动）。
     * @param count 滚动行数。
     * @param fill 顶部补空行的填充单元格。
     */
    void scrollDown(int count, const ZzCell& fill);

    // ---- Dirty Tracking ----

    /**
     * @brief Dirty 代际号。任何可见内容变化都会递增。
     * @return 代际号（单调递增，clearDirty 不复位）。
     * @note Renderer 比较代际号即可判断是否需要任何重绘。
     */
    [[nodiscard]] std::uint64_t dirtyGeneration() const noexcept;

    /**
     * @brief 指定行是否标脏。
     * @param row 行号，0 <= row < size().rows。
     * @return true 表示该行标脏。
     */
    [[nodiscard]] bool rowDirty(int row) const noexcept;

    /**
     * @brief 指定行的脏单元格合并范围（半开区间）。
     * @param row 行号，0 <= row < size().rows。
     * @return 脏列范围；行未脏时返回空范围。
     * @note M0 为每行单个合并区间；未来如需多区间细粒度，可在
     *       ZzRenderView 层扩展而不破坏本接口。
     */
    [[nodiscard]] ZzCellRange dirtyRange(int row) const noexcept;

    /**
     * @brief 收集当前所有脏行行号（升序）。
     * @return 脏行行号列表。
     */
    [[nodiscard]] std::vector<int> dirtyRows() const;

    /// @brief 清除 Dirty 状态（代际号不复位，继续递增）。
    /// @note 由 ZzTerminal::clearDirty 在一帧渲染完成后调用。
    void clearDirty() noexcept;

    // ---- ScrollOut 回调 ----

    /**
     * @brief 设置滚出行回调（由 ZzTerminal 安装，接管历史入栈）。
     * @param callback 回调；传空表示丢弃滚出行。
     */
    void setScrollOutCallback(ScrollOutCallback callback);

    /**
     * @brief 设置历史回抽回调（由 ZzTerminal/backend 安装，M15）。
     * @param callback 回调；传空表示扩行不回抽（底部补空）。
     */
    void setHistoryPullCallback(HistoryPullCallback callback);

    /**
     * @brief 设置接缝斩链回调（M17c，由 backend 安装）；空回调时跳过跨界斩。
     * @param callback 回调；擦屏幕首行时通知持有方斩断历史末行链标。
     */
    void setSeverSeamLinkCallback(SeverSeamLinkCallback callback);

private:
    /// @brief 单缓冲区的完整状态。
    struct Buffer {
        std::vector<ZzLine> lines;                          ///< 行数组。
        ZzCursorState       cursor;                         ///< 光标。
        std::vector<char>   dirtyRows;                      ///< 行脏标记（0/1）。
        std::vector<ZzCellRange> dirtyRanges;               ///< 行内脏列合并区间。
        bool wrapPending = false; ///< wrap-pending 标志（见 wrapPending()）。

        void resize(int cols, int rows);
    };

    void markDirty(int row, int col) noexcept;
    void markRowDirty(int row) noexcept;
    void markAllDirty() noexcept;
    /// @brief 重组单套缓冲区到新列宽（reflow 的 per-buffer 实现）。
    void reflowBuffer(Buffer& buf, int newCols, bool mayScrollOut);
    /// @brief 单套缓冲区的 resize 实现（M15 行变条件语义 + 列向截断/填充）。
    void resizeBuffer(Buffer& buf, int cols, int rows, bool mayUseHistory);
    /// @brief 滚动区是否覆盖全屏高度（滚出上沿的行才可进入历史）。
    [[nodiscard]] bool regionIsFullHeight() const noexcept;
    /// @brief 向上滚动滚动区（内部实现，含滚出回调）。
    void scrollRegionUp(int top, int bottom, int count, const ZzCell& fill);
    /// @brief 向下滚动滚动区（内部实现）。
    void scrollRegionDown(int top, int bottom, int count, const ZzCell& fill);
    /// @brief M17c 整行擦除斩链：清 row 出链与前驱入链；row==0 且
    ///        Primary 时经 severSeamLinkCallback_ 跨界斩历史末行。
    void severRowLinks(Buffer& buf, int row) noexcept;

    Buffer              primary_;
    Buffer              alternate_;
    ZzScreenBuffer      active_       = ZzScreenBuffer::Primary;
    ZzCursorState       savedCursor_;           ///< DECSC 保存的光标。
    bool                hasSavedCursor_ = false;
    bool savedWrapPending_ = false; ///< saveCursor 保存的 wrap-pending。
    int                 cols_ = 0;
    int                 rows_ = 0;
    int                 scrollTop_    = 0;      ///< 滚动区上沿（含）。
    int                 scrollBottom_ = 0;      ///< 滚动区下沿（含）。
    std::vector<char>   tabStops_;              ///< 每列 Tab Stop 标记。
    bool                originMode_   = false;
    bool                insertMode_   = false;
    bool                autoWrapMode_ = true;   ///< DECAWM 默认开。
    std::uint64_t       dirtyGeneration_ = 0;
    ScrollOutCallback   scrollOutCallback_;
    HistoryPullCallback historyPullCallback_;
    SeverSeamLinkCallback severSeamLinkCallback_;
};
