#include "ZzNativeBackend.h"

#include "ZzTerm/UnicodeWidth.h"
#include "ZzTerm/Utf8.h"

#include "unicode/GraphemeBreak.h"
#include "unicode/Utf8Encode.h"

#include <string>
#include <vector>

// ZzNativeBackend 实现：Parser 已接入，feed 的真实链路为
//   bytes -> ZzVtParser（语法） -> Sink -> Terminal 语义 -> ZzScreen。
// print 通道字节经 ZzUtf8Decoder 解码为码点后由 putChar 落格。

namespace {

// 聚簇宽度裁定（T2 表 §1 实测归约 + M7c T3 V 系实测与修复波）：true = 聚簇
// 应占 2 格——RI（单发即宽）、emoji variation base+VS16（libunicode
// width.cpp 真规则数据源 emoji-variation-sequences.txt 的 emoji style base
// 集合；ExtPic 非 variation base+VS16 保窄——★/♔/♩ 反例实测）、keycap
// （须带 VS16，裸 keycap 窄）、InCB 连字（基窄也宽）、emoji ZWJ 序列；
// 窄基+组合符 / VS15 / Prepend+a / 非 variation base+VS16 保持基宽。
bool zzClusterWantsWide(const std::u32string& cps)
{
    bool hasVs16 = false;
    bool hasKeycap = false;
    bool hasZwj = false;
    int extPicCount = 0;
    int consonants = 0;
    int linkers = 0;
    for (const char32_t c : cps) {
        const ZzGraphemeProps p = zzGraphemePropsOf(c);
        if (p.gcb == ZzGcb::RegionalIndicator)
            return true; // I-3：RI 单发即宽 2
        if (c == 0xFE0F)
            hasVs16 = true;
        if (c == 0x20E3)
            hasKeycap = true;
        if (p.gcb == ZzGcb::ZWJ)
            hasZwj = true;
        if (p.extPic)
            ++extPicCount;
        if (p.incb == ZzIncb::Consonant)
            ++consonants;
        if (p.incb == ZzIncb::Linker)
            ++linkers;
    }
    const ZzGraphemeProps first = zzGraphemePropsOf(cps.front());
    if (hasVs16 && (first.emojiVariationBase || hasKeycap))
        return true; // variation base+VS16（例 08/09 与 M7c V1-V5）/ keycap（例 11）
    if (consonants >= 2 && linkers >= 1)
        return true; // I-4：InCB 连字（例 12）
    if (hasZwj && extPicCount >= 2)
        return true; // emoji ZWJ 序列（例 03/04）
    return false;
}

// M7b 聚簇续接（无状态回望，T2 裁定表为最终语义）：cp 与前格 cluster 判续，
// 续则并入前格。返回 true 表示已续接落格（调用方 noteScreenDirty 后返回）。
// wrappedThis：本次 putChar 入口消费 wrap-pending 且 DECAWM 开已换行；
// wasWrapPending：入口处 wrap-pending 原值（DECAWM 关覆盖语义判定用）。
bool zzTryClusterContinue(ZzScreen& screen, char32_t cp, ZzPosition cur,
                          bool wrappedThis, bool wasWrapPending)
{
    const ZzSize sz = screen.size();

    // 前格定位（T2 §3 软换行边界裁定 + A4 实测）：DECAWM 关且 wrap-pending
    // 未换行时取覆盖目标格本身（组合符并入末格，与 contour A4 实测一致）；
    // 普通情形取光标同行左邻格；本次 wrap-pending 换行后（cur 在新行首列）
    // 取上一行行尾格（B2 形态）；其余行首无前格即断。
    ZzPosition prev{-1, -1};
    if (wasWrapPending && !wrappedThis) // DECAWM 关：覆盖目标即最后一格
        prev = cur;
    else if (cur.col > 0)
        prev = ZzPosition{cur.row, cur.col - 1};
    else if (wrappedThis && cur.row > 0)
        prev = ZzPosition{cur.row - 1, sz.cols - 1};
    else
        return false;
    // 左邻是 WideContinuation 时退到其 WideLead（宽格对上的续接落首格）。
    if (screen.lineAt(prev.row).cellAt(prev.col).width() == ZzCellWidth::WideContinuation) {
        if (prev.col == 0)
            return false;
        --prev.col;
    }

    const ZzCell prevCell = screen.lineAt(prev.row).cellAt(prev.col);
    // 前格须含文本才判续（T2 裁定 5：空白格不续）。
    if (!prevCell.isCluster() && prevCell.codePoint() == 0)
        return false;

    // 快路径：三点全满足走原路零回望——前格非 cluster、前格码点 GCB 非
    // Prepend/RegionalIndicator 且非 Hangul 五类（L/V/LV/LVT/T，GB6/7/8
    // 续接候选——prev 侧排除后，新码点 Jamo/音节块跟在非 Hangul 后按
    // GB999 断开是正确快路径，现代韩文预组音节热路径不受影响）、新码点
    // GCB 非 Extend/ZWJ/SpacingMark/RegionalIndicator 且非 ExtPic。
    // 纯 ASCII 双方先短路（GCB 恒 Other，免两次表查找，append 热路径
    // 零附加成本）。
    if (!prevCell.isCluster() && prevCell.codePoint() < 0x80 && cp < 0x80)
        return false;
    const ZzGraphemeProps nextProps = zzGraphemePropsOf(cp);
    if (!prevCell.isCluster()) {
        const ZzGraphemeProps prevProps = zzGraphemePropsOf(prevCell.codePoint());
        const bool prevMayContinue =
            prevProps.gcb == ZzGcb::Prepend || prevProps.gcb == ZzGcb::RegionalIndicator
            || prevProps.gcb == ZzGcb::L || prevProps.gcb == ZzGcb::V
            || prevProps.gcb == ZzGcb::LV || prevProps.gcb == ZzGcb::LVT
            || prevProps.gcb == ZzGcb::T;
        const bool nextMayContinue =
            nextProps.gcb == ZzGcb::Extend || nextProps.gcb == ZzGcb::ZWJ
            || nextProps.gcb == ZzGcb::SpacingMark
            || nextProps.gcb == ZzGcb::RegionalIndicator || nextProps.extPic;
        if (!prevMayContinue && !nextMayContinue)
            return false;
    }

    // 取前格码点串并判续（T1 单一求值核，含 64 硬上限——超了即断）。
    std::u32string prevCps;
    std::string newText;
    if (prevCell.isCluster()) {
        newText = std::string(screen.lineAt(prev.row).clusterText(prevCell.clusterIndex()));
        const std::vector<char32_t> decoded = ZzUtf8Decoder().decodeAll(newText);
        prevCps.assign(decoded.begin(), decoded.end());
    } else {
        prevCps.push_back(prevCell.codePoint());
        zzAppendCodePoint(newText, prevCell.codePoint());
    }
    if (!zzGraphemeContinues(prevCps, cp))
        return false;

    // 续接落格（T2 §5b 画笔裁定：只更新文本，前景/背景/属性保持前格原值；
    // 旧 cluster 侧表条目弃置不管——internCluster 不查重约定）。
    prevCps.push_back(cp);
    zzAppendCodePoint(newText, cp);
    ZzCell merged = prevCell;
    merged.setCluster(screen.internClusterAt(prev.row, newText));

    // 窄变宽（VS16/keycap/InCB 连字续接使窄基聚簇变宽，T2 §1/§2 裁定）。
    const bool narrowToWide =
        prevCell.width() == ZzCellWidth::Narrow && zzClusterWantsWide(prevCps);
    // 上一行行尾的续接（wrap-pending 已换行，B4 形态）：尾列无续格空间，
    // 变宽抑制保持窄，光标不动。
    const bool canWiden = narrowToWide && prev.row == cur.row && prev.col + 1 < sz.cols;
    if (canWiden) {
        // 行内有空间（B3/SGR2 形态）：原位转 WideLead，右侧插入
        // WideContinuation 续格（画笔同基格），光标右移一格；右移到行外
        // 则停末列并置 wrap-pending（同宽字符行尾既有逻辑）。
        merged.setWidth(ZzCellWidth::WideLead);
        screen.putCell(prev, merged);
        ZzCell cont;
        cont.setWidth(ZzCellWidth::WideContinuation);
        cont.setForeground(prevCell.foreground());
        cont.setBackground(prevCell.background());
        cont.setAttributes(prevCell.attributes());
        screen.putCell(ZzPosition{prev.row, prev.col + 1}, cont);
        if (prev.col + 2 >= sz.cols) {
            screen.setCursorPosition(ZzPosition{cur.row, sz.cols - 1});
            screen.setWrapPending(true);
        } else {
            screen.setCursorPosition(ZzPosition{cur.row, prev.col + 2});
        }
    } else {
        screen.putCell(prev, merged);
        // 本次 wrap-pending 已换行（B2/B4 形态）：cur 是局部变量，须显式
        // 提交换行后的光标位置；未换行时光标本就正确，不动。
        if (wrappedThis)
            screen.setCursorPosition(cur);
    }
    return true;
}

} // namespace

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
    , lineSource_(screen_, *scrollback_)
    , renderView_(screen_)
    , historyView_(screen_, *scrollback_, historyGeneration_)
    , sink_(std::make_unique<Sink>(*this))
    , parser_(std::make_unique<ZzVtParser>(sink_.get()))
{
    // Screen 不知道历史后端：滚出行经回调上移到 Terminal，由 Terminal 入栈。
    screen_.setScrollOutCallback([this](std::vector<ZzLine> lines) {
        scrolledOutPending_ += lines.size();
        scrollback_->append(std::move(lines));
        ++historyGeneration_; // M14：历史 append（含容量裁剪）代计数递增
    });
    // M15：扩行回抽回调——Screen 经此从 scrollback 取最新行注入屏幕顶部；
    // 实取非空时历史可见行数减少，代计数递增（M14「不得漏增」）。
    screen_.setHistoryPullCallback([this](std::size_t maxLines) {
        auto pulled = scrollback_->takeNewest(maxLines);
        if (!pulled.empty())
            ++historyGeneration_;
        return pulled;
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
    const ZzSize old = screen_.size();
    if (old == ZzSize{cols, rows})
        return false;
    // M4：列变化触发 soft-wrap reflow，先历史后屏幕
    //（屏幕溢出行以新宽度经 ScrollOutCallback 回流到已重组的历史，宽度不变量自洽）。
    if (cols != old.cols) {
        scrollback_->reflow(cols);
        ++historyGeneration_; // M14：历史 reflow 代计数递增（屏幕回流 append 经回调另计）
        screen_.reflow(cols);
    }
    if (rows != old.rows)
        screen_.resize(cols, rows); // M15：行向条件语义（裁光标下方/压历史/回抽），见 Screen.h
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

void ZzNativeBackend::setOutputHandler(std::function<void(std::string_view)> handler)
{
    outputHandler_ = std::move(handler);
}

void ZzNativeBackend::emit(std::string_view bytes)
{
    if (outputHandler_ && !bytes.empty())
        outputHandler_(bytes);
}

void ZzNativeBackend::sendText(std::string_view utf8)
{
    emit(encoder_.encodeText(utf8));
}

void ZzNativeBackend::sendKey(const ZzKeyEvent& event)
{
    emit(encoder_.encodeKey(event));
}

void ZzNativeBackend::sendMouse(const ZzMouseEvent& event)
{
    emit(encoder_.encodeMouse(event));
}

void ZzNativeBackend::sendPaste(std::string_view utf8)
{
    emit(encoder_.encodePaste(utf8));
}

void ZzNativeBackend::sendFocus(bool focused)
{
    emit(encoder_.encodeFocus(focused));
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
    int width = zzCellWidthOf(cp, ambiguousWide_);
    // M7b T2 裁定 §1（I-3）：RI 单发即宽 2——EAW 对 RI 报 Neutral（窄），
    // contour 实测宽格，初始落格宽度按裁定表对齐。
    if (width != 2 && zzGraphemePropsOf(cp).gcb == ZzGcb::RegionalIndicator)
        width = 2;

    // xterm pending-wrap：上一字符写在最后一列时，先换行再落格。
    const bool wasWrapPending = screen_.wrapPending();
    bool didWrap = false;
    if (wasWrapPending) {
        screen_.setWrapPending(false);
        if (screen_.autoWrapMode()) {
            didWrap = true;
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

    // M7b 聚簇续接（无状态回望，T2 裁定表）：新码点与前格 cluster 判续，
    // 续则并入前格不推进光标（宽度/画笔/软换行边界语义逐条按裁定表）。
    if (zzTryClusterContinue(screen_, cp, cur, didWrap, wasWrapPending)) {
        noteScreenDirty();
        return;
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
    case '=': // DECKPAM：application keypad（encoder 暂无 numpad 键消费方，
              // 状态同步为未来 keypad 编码保持正确）
        encoder_.setApplicationKeypad(true);
        break;
    case '>': // DECPNM：numeric keypad
        encoder_.setApplicationKeypad(false);
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
