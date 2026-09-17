#include "ZzTerm/Terminal.h"

#include "ZzTerm/Parser.h"
#include "ZzTerm/UnicodeWidth.h"

// ZzTerminal 实现：Parser 已接入，feed 的真实链路为
//   bytes -> ZzVtParser（语法） -> Sink -> Terminal 语义 -> ZzScreen。
// print 通道字节经 ZzUtf8Decoder 解码为码点后由 putChar 落格。

/// 嵌套私有类：把解析事件转发为 Terminal 的语义方法调用。
/// DCS 不覆盖（基类默认空实现 = 安全忽略）。
struct ZzTerminal::Sink : ZzParserSink {
    explicit Sink(ZzTerminal& term) : term_(term) {}

    void onPrint(char byte) override
    {
        term_.utf8_.feed(std::string_view(&byte, 1),
                         [this](char32_t cp) { term_.putChar(cp); });
    }
    void onExecute(std::uint8_t control) override { term_.executeControl(control); }
    void onCsiDispatch(const ZzParamSequence& seq) override { term_.dispatchCsi(seq); }
    void onEscDispatch(std::string_view intermediates, char final) override
    {
        term_.dispatchEsc(intermediates, final);
    }
    void onOscDispatch(std::string_view payload) override { term_.dispatchOsc(payload); }

    ZzTerminal& term_;
};

ZzTerminal::ZzTerminal(int cols, int rows, std::size_t scrollbackMaxLines)
    : screen_(cols, rows)
    , scrollback_(zzCreateChunkedScrollback(scrollbackMaxLines))
    , renderView_(&screen_, scrollback_.get())
    , sink_(std::make_unique<Sink>(*this))
    , parser_(std::make_unique<ZzVtParser>(sink_.get()))
{
    // Screen 不知道历史后端：滚出行经回调上移到 Terminal，由 Terminal 入栈。
    screen_.setScrollOutCallback([this](std::vector<ZzLine> lines) {
        scrolledOutPending_ += lines.size();
        scrollback_->append(std::move(lines));
    });
}

ZzTerminal::~ZzTerminal() = default;

ZzTermChanges ZzTerminal::feed(std::span<const std::byte> data)
{
    ZzTermChanges changes;
    scrolledOutPending_ = 0;
    activeChanges_ = &changes;

    const auto* chars = reinterpret_cast<const char*>(data.data());
    parser_->feed(std::string_view(chars, data.size()));

    activeChanges_ = nullptr;
    if (scrolledOutPending_ > 0) {
        changes.scrollbackChanged = true;
        changes.scrolledOutLines = scrolledOutPending_;
        scrolledOutPending_ = 0;
    }
    return changes;
}

void ZzTerminal::noteScreenDirty() noexcept
{
    if (activeChanges_)
        activeChanges_->screenDirty = true;
}

ZzCell ZzTerminal::eraseFill() const noexcept
{
    // 擦除/滚动填充：携带当前画笔背景（bce 语义），无文本无属性。
    ZzCell fill;
    fill.setBackground(penBg_);
    return fill;
}

void ZzTerminal::putChar(char32_t cp)
{
    const ZzSize sz = screen_.size();
    const ZzCellRange region = screen_.scrollRegionRows(); // [top, bottom+1)
    ZzPosition cur = screen_.cursor().position;

    // xterm pending-wrap：上一字符写在最后一列时，先换行再落格。
    if (screen_.wrapPending()) {
        screen_.setWrapPending(false);
        if (screen_.autoWrapMode()) {
            screen_.setLineWrapped(cur.row, true);
            if (cur.row == region.endCol - 1)
                screen_.scrollUp(1, eraseFill());
            else
                ++cur.row;
            cur.col = 0;
        }
    }

    ZzCell cell;
    // zzCellWidthOf 为 M1 占位（恒窄）；M2 接入真实 EAW 表后，
    // 宽字符需在此处补写 WideContinuation 续格（M2 任务，非本次范围）。
    cell.setWidth(zzCellWidthOf(cp) == 2 ? ZzCellWidth::WideLead : ZzCellWidth::Narrow);
    cell.setCodePoint(cp);
    cell.setForeground(penFg_);
    cell.setBackground(penBg_);
    cell.setAttributes(penAttrs_);
    screen_.putCell(cur, cell);
    noteScreenDirty();

    if (cur.col < sz.cols - 1) {
        screen_.setCursorPosition(ZzPosition{cur.row, cur.col + 1});
    } else if (screen_.autoWrapMode()) {
        // 最后一列：光标不动，置 wrap-pending（下一个可打印字符才换行）。
        screen_.setWrapPending(true);
    }
    // DECAWM 关闭时在最后一列：光标不动、不置标志，后续字符覆盖该格。
}

void ZzTerminal::executeControl(std::uint8_t control)
{
    // C0 控制的完整语义（LF/BS/HT/BEL 等）归任务 3；此处仅实现 CR，
    // 因为 pending-wrap 语义要求 CR 立即清除 wrap-pending 并回列首。
    if (control == '\r')
        screen_.setCursorPosition(ZzPosition{screen_.cursor().position.row, 0});
}

void ZzTerminal::dispatchCsi(const ZzParamSequence&)
{
    // 空实现：CSI 语义分发归任务 3。
}

void ZzTerminal::dispatchEsc(std::string_view, char)
{
    // 空实现：ESC 语义分发归任务 3。
}

void ZzTerminal::dispatchOsc(std::string_view)
{
    // 空实现：OSC 语义（标题等）归任务 4。
}

void ZzTerminal::sgr(const ZzParamSequence&)
{
    // 空实现：SGR 画笔属性归任务 3。
}

bool ZzTerminal::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return false;
    if (screen_.size() == ZzSize{cols, rows})
        return false;
    // M0：网格级 resize，不做 reflow（见 Terminal.h 注释）。
    screen_.resize(cols, rows);
    return true;
}

const ZzRenderView& ZzTerminal::renderView() const noexcept
{
    return renderView_;
}

ZzSize ZzTerminal::size() const noexcept
{
    return screen_.size();
}

ZzCursorState ZzTerminal::cursor() const noexcept
{
    return screen_.cursor();
}

bool ZzTerminal::isAlternateScreen() const noexcept
{
    return screen_.activeBuffer() == ZzScreenBuffer::Alternate;
}

const std::string& ZzTerminal::title() const noexcept
{
    return title_;
}

void ZzTerminal::clearDirty() noexcept
{
    screen_.clearDirty();
}

ZzScreen& ZzTerminal::screen() noexcept
{
    return screen_;
}

ZzScrollback& ZzTerminal::scrollback() noexcept
{
    return *scrollback_;
}
