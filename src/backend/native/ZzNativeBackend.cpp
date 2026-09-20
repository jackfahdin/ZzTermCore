#include "ZzNativeBackend.h"

#include "ZzTerm/UnicodeWidth.h"

// ZzNativeBackend 实现：Parser 已接入，feed 的真实链路为
//   bytes -> ZzVtParser（语法） -> Sink -> Terminal 语义 -> ZzScreen。
// print 通道字节经 ZzUtf8Decoder 解码为码点后由 putChar 落格。

/// 嵌套私有类：把解析事件转发为 Terminal 的语义方法调用。
/// DCS 不覆盖（基类默认空实现 = 安全忽略）。
struct ZzNativeBackend::Sink : ZzParserSink {
    explicit Sink(ZzNativeBackend& backend) : backend_(backend) {}

    void onPrint(char byte) override
    {
        backend_.utf8_.feed(std::string_view(&byte, 1),
                            [this](char32_t cp) { backend_.putChar(cp); });
    }
    void onExecute(std::uint8_t control) override { backend_.executeControl(control); }
    void onCsiDispatch(const ZzParamSequence& seq) override { backend_.dispatchCsi(seq); }
    void onEscDispatch(std::string_view intermediates, char final) override
    {
        backend_.dispatchEsc(intermediates, final);
    }
    void onOscDispatch(std::string_view payload) override { backend_.dispatchOsc(payload); }

    ZzNativeBackend& backend_;
};

ZzNativeBackend::ZzNativeBackend(int cols, int rows, std::size_t scrollbackMaxLines)
    : screen_(cols, rows)
    , scrollback_(zzCreateChunkedScrollback(scrollbackMaxLines))
    , renderView_(screen_)
    , sink_(std::make_unique<Sink>(*this))
    , parser_(std::make_unique<ZzVtParser>(sink_.get()))
{
    // Screen 不知道历史后端：滚出行经回调上移到 Terminal，由 Terminal 入栈。
    screen_.setScrollOutCallback([this](std::vector<ZzLine> lines) {
        scrolledOutPending_ += lines.size();
        scrollback_->append(std::move(lines));
    });
}

ZzNativeBackend::~ZzNativeBackend() = default;

ZzTermChanges ZzNativeBackend::feed(std::span<const std::byte> data)
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
    }
    return changes;
}

bool ZzNativeBackend::resize(int cols, int rows)
{
    if (cols <= 0 || rows <= 0)
        return false;
    if (screen_.size() == ZzSize{cols, rows})
        return false;
    // M0：网格级 resize，不做 reflow（见 Terminal.h 注释）。
    screen_.resize(cols, rows);
    return true;
}

const ZzRenderView& ZzNativeBackend::renderView() const noexcept { return renderView_; }
ZzSize ZzNativeBackend::size() const noexcept { return screen_.size(); }
ZzCursorState ZzNativeBackend::cursor() const noexcept { return screen_.cursor(); }
bool ZzNativeBackend::isAlternateScreen() const noexcept
{
    return screen_.activeBuffer() == ZzScreenBuffer::Alternate;
}
const std::string& ZzNativeBackend::title() const noexcept { return title_; }
void ZzNativeBackend::clearDirty() noexcept { screen_.clearDirty(); }

void ZzNativeBackend::setOutputHandler(std::function<void(std::string_view)>)
{
    // M3 输入编码后启用（DA 响应、光标上报等回传）。
}

void ZzNativeBackend::setAmbiguousWidthMode(bool wide) noexcept
{
    ambiguousWide_ = wide;
}

void ZzNativeBackend::noteScreenDirty() noexcept
{
    if (activeChanges_)
        activeChanges_->screenDirty = true;
}

ZzCell ZzNativeBackend::eraseFill() const noexcept
{
    // 擦除/滚动填充：携带当前画笔背景（bce 语义），无文本无属性。
    ZzCell fill;
    fill.setBackground(penBg_);
    return fill;
}

void ZzNativeBackend::clearWidePairAt(ZzPosition pos) noexcept
{
    const ZzSize sz = screen_.size();
    const ZzLine& line = screen_.lineAt(pos.row);
    const ZzCell& c = line.cellAt(pos.col);
    if (c.width() == ZzCellWidth::WideLead && pos.col + 1 < sz.cols) {
        const ZzCell& right = line.cellAt(pos.col + 1);
        if (right.width() == ZzCellWidth::WideContinuation) {
            ZzCell blank;
            blank.setBackground(right.background());
            screen_.putCell(ZzPosition{pos.row, pos.col + 1}, blank);
        }
    } else if (c.width() == ZzCellWidth::WideContinuation && pos.col > 0) {
        const ZzCell& left = line.cellAt(pos.col - 1);
        if (left.width() == ZzCellWidth::WideLead) {
            ZzCell blank;
            blank.setBackground(left.background());
            screen_.putCell(ZzPosition{pos.row, pos.col - 1}, blank);
        }
    }
}

void ZzNativeBackend::putChar(char32_t cp)
{
    const ZzSize sz = screen_.size();
    const ZzCellRange region = screen_.scrollRegionRows(); // [top, bottom+1)
    ZzPosition cur = screen_.cursor().position;
    const int width = zzCellWidthOf(cp, ambiguousWide_);

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

    // 宽字符在最后一列放不下：当前格留空（画笔背景），立即换行到新行行首。
    // 与 DECAWM 无关：xterm 宽字符不可截半显示，总是换行（若实测 Contour
    // 行为不同，compat 用例注释钉住分歧）。
    if (width == 2 && cur.col == sz.cols - 1) {
        ZzCell blank;
        blank.setBackground(penBg_);
        clearWidePairAt(cur);
        screen_.putCell(cur, blank);
        screen_.setLineWrapped(cur.row, true);
        if (cur.row == region.endCol - 1)
            screen_.scrollUp(1, eraseFill());
        else
            ++cur.row;
        cur.col = 0;
    }

    ZzCell cell;
    cell.setWidth(width == 2 ? ZzCellWidth::WideLead : ZzCellWidth::Narrow);
    cell.setCodePoint(cp);
    cell.setForeground(penFg_);
    cell.setBackground(penBg_);
    cell.setAttributes(penAttrs_);
    clearWidePairAt(cur);
    screen_.putCell(cur, cell);
    if (width == 2) {
        ZzCell cont;
        cont.setWidth(ZzCellWidth::WideContinuation);
        cont.setForeground(penFg_);
        cont.setBackground(penBg_);
        const ZzPosition contPos{cur.row, cur.col + 1};
        clearWidePairAt(contPos);
        screen_.putCell(contPos, cont);
    }
    noteScreenDirty();

    if (width == 2) {
        // 宽字符占满行尾两格：光标停最后一列，置 wrap-pending（下一字符换行）。
        if (cur.col + 1 == sz.cols - 1) {
            screen_.setCursorPosition(ZzPosition{cur.row, sz.cols - 1});
            screen_.setWrapPending(true);
        } else {
            screen_.setCursorPosition(ZzPosition{cur.row, cur.col + 2});
        }
    } else if (cur.col < sz.cols - 1) {
        screen_.setCursorPosition(ZzPosition{cur.row, cur.col + 1});
    } else {
        // 最后一列：光标不动，置 wrap-pending（下一个可打印字符才换行）。
        screen_.setWrapPending(true);
    }
    // wrap-pending 无论 DECAWM 开关都置位（对齐 xterm charproc.c：
    // 写满右边距即置 do_wrap，仅消费时按当时 WRAPAROUND 决定是否换行）。
    // DECAWM 关闭时后续字符在入口处消费该标志但不换行，仍覆盖最后一格。
}

void ZzNativeBackend::executeControl(std::uint8_t control)
{
    const ZzCellRange region = screen_.scrollRegionRows(); // [top, bottom+1)
    const ZzPosition cur = screen_.cursor().position;

    switch (control) {
    case 0x07: // BEL
        if (activeChanges_)
            activeChanges_->bell = true;
        break;
    case 0x08: // BS：左移一格（不越行首）
        screen_.setCursorPosition(ZzPosition{cur.row, cur.col > 0 ? cur.col - 1 : 0});
        noteScreenDirty();
        break;
    case 0x09: // HT：下一个 Tab Stop
        screen_.setCursorPosition(ZzPosition{cur.row, screen_.nextTabStop(cur.col)});
        noteScreenDirty();
        break;
    case 0x0A: // LF
    case 0x0B: // VT
    case 0x0C: // FF：index——滚动区下沿上滚，否则下移一行
        if (cur.row == region.endCol - 1)
            screen_.scrollUp(1, eraseFill());
        else
            screen_.setCursorPosition(ZzPosition{cur.row + 1, cur.col});
        noteScreenDirty();
        break;
    case 0x0D: // CR：回列首（经 setCursorPosition 连带清除 wrap-pending）
        screen_.setCursorPosition(ZzPosition{cur.row, 0});
        noteScreenDirty();
        break;
    default:
        break; // 其余 C0 安全忽略
    }
}

void ZzNativeBackend::dispatchEsc(std::string_view intermediates, char final)
{
    if (!intermediates.empty())
        return; // charset 选择（ESC ( X 等）随 M2 字符集设计实现

    const ZzCellRange region = screen_.scrollRegionRows(); // [top, bottom+1)
    const ZzPosition cur = screen_.cursor().position;

    switch (final) {
    case '7': // DECSC
        screen_.saveCursor();
        break;
    case '8': // DECRC
        screen_.restoreCursor();
        noteScreenDirty();
        break;
    case 'D': // IND：同 LF
        if (cur.row == region.endCol - 1)
            screen_.scrollUp(1, eraseFill());
        else
            screen_.setCursorPosition(ZzPosition{cur.row + 1, cur.col});
        noteScreenDirty();
        break;
    case 'M': // RI：滚动区上沿下滚，否则上移一行
        if (cur.row == region.startCol)
            screen_.scrollDown(1, eraseFill());
        else if (cur.row > 0)
            screen_.setCursorPosition(ZzPosition{cur.row - 1, cur.col});
        noteScreenDirty();
        break;
    case 'E': // NEL：CR + IND；CR 无条件生效（ECMA-48）
        if (cur.row == region.endCol - 1) {
            screen_.scrollUp(1, eraseFill());
            screen_.setCursorPosition(ZzPosition{cur.row, 0}); // 滚动后行号不变，仍在下沿
        } else {
            screen_.setCursorPosition(ZzPosition{cur.row + 1, 0});
        }
        noteScreenDirty();
        break;
    case 'H': // HTS：当前列设 Tab Stop
        screen_.setTabStop(cur.col);
        break;
    default:
        break; // 其余 ESC 序列安全忽略
    }
}

void ZzNativeBackend::dispatchOsc(std::string_view payload)
{
    const std::size_t sep = payload.find(';');
    if (sep == std::string_view::npos)
        return;
    const std::string_view code = payload.substr(0, sep);
    if (code != "0" && code != "1" && code != "2")
        return; // 仅窗口/图标标题（OSC 0/1/2），其余安全忽略
    title_ = std::string(payload.substr(sep + 1));
    if (activeChanges_)
        activeChanges_->titleChanged = true;
}
