#pragma once

// 内部头（不安装）：ZzTerminal 的 PImpl 实现细节。
// 重构规则：本头内容机械搬运自原 include/ZzTerm/Terminal.h 私有区，
// 成员语义与注释保持逐字一致。

#include "ZzTerm/Terminal.h"

#include "ZzTerm/Parser.h"
#include "ZzTerm/Utf8.h"

#include "backend/native/ZzNativeRenderView.h"

class ZzTerminal::Impl {
public:
    Impl(int cols, int rows, std::size_t scrollbackMaxLines);

    // ---- 语义方法（原 ZzTerminal 私有方法；实现分布在
    //      Terminal.cpp / CsiDispatch.cpp / Sgr.cpp） ----
    ZzTermChanges feed(std::span<const std::byte> data);
    void putChar(char32_t cp);
    void executeControl(std::uint8_t control);
    void dispatchCsi(const ZzParamSequence& seq);
    void dispatchEsc(std::string_view intermediates, char final);
    void dispatchOsc(std::string_view payload);
    void sgr(const ZzParamSequence& seq);
    [[nodiscard]] ZzCell eraseFill() const noexcept;
    void noteScreenDirty() noexcept;

    struct Sink; // 嵌套类：ZzParserSink 实现，定义在 Terminal.cpp。

    ZzScreen                     screen_;     ///< 工作区（内含 Primary/Alternate）。
    std::unique_ptr<ZzScrollback> scrollback_; ///< 历史后端（接口指针，实现可替换）。
    ZzNativeRenderView           renderView_; ///< 渲染边界（借用 screen_）。
    std::string                  title_;      ///< OSC 标题（UTF-8）。
    std::size_t                  scrolledOutPending_ = 0; ///< feed 内滚出行计数（回调聚合用）。
    std::unique_ptr<Sink>       sink_;    ///< 先于 parser_ 声明：析构逆序保证 parser 先销毁。
    std::unique_ptr<ZzVtParser> parser_;  ///< VT 解析器（语法 dispatch）。
    ZzUtf8Decoder               utf8_;    ///< print 通道 UTF-8 增量解码。
    ZzCellAttributes            penAttrs_; ///< 当前画笔属性（SGR）。
    ZzColor penFg_ = ZzColor::Default();  ///< 当前画笔前景色。
    ZzColor penBg_ = ZzColor::Default();  ///< 当前画笔背景色。
    ZzTermChanges* activeChanges_ = nullptr; ///< feed 期间的变化聚合目标。
};
