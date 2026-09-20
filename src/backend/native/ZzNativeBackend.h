#pragma once

// 内部头（不安装）：native 终端引擎后端（自研引擎，一等后端）。
// M1b 自原 src/terminal/TerminalImpl.h（ZzTerminal::Impl）整体迁入，
// 成员语义与注释逐字保留；类改为实现 ZzTerminalBackend 接口。

#include "../ZzTerminalBackend.h"

#include "ZzTerm/Parser.h"
#include "ZzTerm/Utf8.h"

#include "ZzNativeRenderView.h"

class ZzNativeBackend final : public ZzTerminalBackend {
public:
    ZzNativeBackend(int cols, int rows, std::size_t scrollbackMaxLines);
    ~ZzNativeBackend() override;

    // ---- ZzTerminalBackend 接口 ----
    ZzTermChanges feed(std::span<const std::byte> data) override;
    bool resize(int cols, int rows) override;
    [[nodiscard]] const ZzRenderView& renderView() const noexcept override;
    [[nodiscard]] ZzSize size() const noexcept override;
    [[nodiscard]] ZzCursorState cursor() const noexcept override;
    [[nodiscard]] bool isAlternateScreen() const noexcept override;
    [[nodiscard]] const std::string& title() const noexcept override;
    void clearDirty() noexcept override;
    void setOutputHandler(std::function<void(std::string_view)> handler) override;

    // ---- facade 的 screen()/scrollback() 委托用（Native 限定访问） ----
    [[nodiscard]] ZzScreen& screen() noexcept { return screen_; }
    [[nodiscard]] ZzScrollback& scrollback() noexcept { return *scrollback_; }

    // ---- 语义方法（原 ZzTerminal 私有方法；实现分布在
    //      ZzNativeBackend.cpp / NativeCsiDispatch.cpp / NativeSgr.cpp） ----
    void putChar(char32_t cp);
    void executeControl(std::uint8_t control);
    void dispatchCsi(const ZzParamSequence& seq);
    void dispatchEsc(std::string_view intermediates, char final);
    void dispatchOsc(std::string_view payload);
    void sgr(const ZzParamSequence& seq);
    void dispatchDecPrivate(const ZzParamSequence& seq); // DEC 私有 CSI（? 前缀）
    void switchToAlternate(bool saveCursor); // 1049h/1047h 进入备用屏幕
    void switchToPrimary(bool restoreCursor); // 1049l/1047l 退回主屏幕
    [[nodiscard]] ZzCell eraseFill() const noexcept;
    /// 覆写一致性：pos 覆盖既有宽字符任一半时，另一半清为空格（保留被清格背景）。
    void clearWidePairAt(ZzPosition pos) noexcept;
    void noteScreenDirty() noexcept;

    struct Sink; // 嵌套类：ZzParserSink 实现，定义在 ZzNativeBackend.cpp。

    ZzScreen                     screen_;     ///< 工作区（内含 Primary/Alternate）。
    std::unique_ptr<ZzScrollback> scrollback_; ///< 历史后端（接口指针，实现可替换）。
    ZzNativeRenderView           renderView_; ///< 渲染边界（借用 screen_）。
    std::string                  title_;      ///< OSC 标题（UTF-8）。
    std::size_t                  scrolledOutPending_ = 0; ///< feed 内滚出行计数（回调聚合用）。
    int  savedScrollTop_ = 0;    ///< 切 Alternate 时保存的主屏滚动区上沿（0 起始）。
    int  savedScrollBottom_ = 0; ///< 切 Alternate 时保存的主屏滚动区下沿（0 起始，含）。
    bool hasSavedScrollRegion_ = false; ///< 是否有待恢复的滚动区。
    std::unique_ptr<Sink>       sink_;    ///< 先于 parser_ 声明：析构逆序保证 parser 先销毁。
    std::unique_ptr<ZzVtParser> parser_;  ///< VT 解析器（语法 dispatch）。
    ZzUtf8Decoder               utf8_;    ///< print 通道 UTF-8 增量解码。
    ZzCellAttributes            penAttrs_; ///< 当前画笔属性（SGR）。
    ZzColor penFg_ = ZzColor::Default();  ///< 当前画笔前景色。
    ZzColor penBg_ = ZzColor::Default();  ///< 当前画笔背景色。
    ZzTermChanges* activeChanges_ = nullptr; ///< feed 期间的变化聚合目标。
};
