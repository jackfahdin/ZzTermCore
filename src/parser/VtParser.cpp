/**
 * @file VtParser.cpp
 * @brief ZzVtParser 状态机实现。
 *
 * 状态划分与转移规则参考 Paul Williams 的 DEC-compatible parser
 * （https://vt500.net/decparser），语义依据：
 * - ECMA-48 (ISO/IEC 6429)：C0/C1、CSI、控制序列语法；
 * - xterm ctlseqs.txt（Thomas E. Dickey）：OSC BEL/ST 终止、
 *   私有序列等 xterm 方言。
 *
 * 与 Williams 模型的主要偏差（均有意为之）：
 * - 不识别 8-bit C1（0x80-0x9F），与 xterm UTF-8 模式一致，
 *   避免与 UTF-8 continuation byte 冲突；
 * - OSC 额外接受 BEL 作为终止符（xterm 行为，ECMA-48 仅认 ST）；
 * - 任何序列状态下收到 CAN/SUB 立即中止序列并执行该控制符；
 * - OSC 内 ESC 后非 '\\' 时中止 OSC 并将该字节按新 Escape
 *   序列重新处理（安全恢复，不进入死循环）。
 */
#include "ZzTerm/Parser.h"

#include <algorithm>

namespace {

// C0 控制字符（含 BEL/BS/HT/LF/VT/FF/CR），ESC/CAN/SUB 单独处理。
constexpr bool isC0(unsigned char b) { return b <= 0x1F; }
// ESC sequence final byte 范围（ECMA-48 5.3.2）。
constexpr bool isEscFinal(unsigned char b) { return b >= 0x30 && b <= 0x7E; }
// CSI/DCS final byte 范围（ECMA-48 5.4）。
constexpr bool isCsiFinal(unsigned char b) { return b >= 0x40 && b <= 0x7E; }
constexpr bool isParamByte(unsigned char b) { return b >= 0x30 && b <= 0x3F; }
constexpr bool isDigit(unsigned char b) { return b >= 0x30 && b <= 0x39; }
constexpr bool isIntermediate(unsigned char b) { return b >= 0x20 && b <= 0x2F; }
// CSI private marker：'<''=''>''?'（ECMA-48 private parameter 前缀）。
// CSI private marker：'<''=''>''?'（ECMA-48 private parameter 前缀）。
constexpr bool isPrivateMarker(unsigned char b) { return b >= 0x3C && b <= 0x3F; }

constexpr unsigned char kEsc = 0x1B;
constexpr unsigned char kCan = 0x18;
constexpr unsigned char kSub = 0x1A;
constexpr unsigned char kBel = 0x07;
constexpr unsigned char kDel = 0x7F;

} // namespace

// ZzParserSink 默认空实现：子类只覆盖关心的事件。
void ZzParserSink::onPrint(char) {}
void ZzParserSink::onExecute(std::uint8_t) {}
void ZzParserSink::onCsiDispatch(const ZzParamSequence&) {}
void ZzParserSink::onEscDispatch(std::string_view, char) {}
void ZzParserSink::onOscDispatch(std::string_view) {}
void ZzParserSink::onDcsHook(const ZzParamSequence&) {}
void ZzParserSink::onDcsPut(std::string_view) {}
void ZzParserSink::onDcsUnhook() {}

ZzVtParser::ZzVtParser(ZzParserSink* sink, ZzParserLimits limits)
    : sink_(sink), limits_(limits) {
    // 内部缓冲为定长数组，钳制上限防止配置越界写。
    limits_.maxParams = std::min<std::uint32_t>(limits_.maxParams, 64);
    limits_.maxIntermediates = std::min<std::uint32_t>(limits_.maxIntermediates, 16);
    if (limits_.maxParams == 0)
        limits_.maxParams = 1;
    if (limits_.maxParamValue <= 0)
        limits_.maxParamValue = 65535;
    if (limits_.maxDcsChunk == 0)
        limits_.maxDcsChunk = 4096;
    oscBuffer_.reserve(std::min<std::size_t>(limits_.maxOscLength, 1024));
    dcsChunk_.reserve(limits_.maxDcsChunk);
}

void ZzVtParser::reset() {
    // 语义复位：丢弃未完成状态，不再向 sink 补发任何回调。
    state_ = ZzParserState::Ground;
    clearSequence();
    oscBuffer_.clear();
    oscTruncated_ = false;
    oscEscPending_ = false;
    dcsChunk_.clear();
    dcsEscPending_ = false;
}

void ZzVtParser::clearSequence() {
    paramCount_ = 0;
    paramActive_ = false;
    privateMarker_ = 0;
    intermediateCount_ = 0;
}

void ZzVtParser::feed(std::string_view data) {
    for (char c : data)
        processByte(static_cast<unsigned char>(c));
    // 输入边界处把攒下的 DCS 块递交出去，保证 onDcsPut 及时。
    flushDcsChunk();
}

void ZzVtParser::enterState(ZzParserState next) {
    state_ = next;
    if (next == ZzParserState::Escape || next == ZzParserState::CsiEntry ||
        next == ZzParserState::DcsEntry || next == ZzParserState::OscString)
        clearSequence();
    if (next == ZzParserState::OscString) {
        oscBuffer_.clear();
        oscTruncated_ = false;
        oscEscPending_ = false;
    }
}

void ZzVtParser::collectParamDigit(unsigned char byte) {
    std::uint32_t index;
    if (!paramActive_) {
        if (paramCount_ < limits_.maxParams) {
            index = paramCount_; // 开启新槽
            params_[index] = 0;
        } else {
            // 槽数达上限：数字并入最后一槽，参数个数有界。
            index = paramCount_ - 1;
        }
        paramActive_ = true;
    } else {
        index = paramCount_ < limits_.maxParams ? paramCount_ : paramCount_ - 1;
    }
    // 十进制累积并钳位，防整数溢出。
    std::int64_t v = static_cast<std::int64_t>(params_[index]) * 10 + (byte - '0');
    params_[index] = v > limits_.maxParamValue ? limits_.maxParamValue
                                               : static_cast<std::int32_t>(v);
}

void ZzVtParser::finishParamSlot() {
    // 只关闭已打开（有数字）的槽；空槽由 ';' 分支显式记录为省略。
    if (!paramActive_)
        return;
    // 吸收模式下最后一槽已计数，不得重复增加。
    if (paramCount_ < limits_.maxParams)
        ++paramCount_;
    paramActive_ = false;
}

ZzParamSequence ZzVtParser::makeSequence(char final) const {
    ZzParamSequence seq;
    seq.params = std::span<const std::int32_t>(params_, paramCount_);
    seq.privateMarker = privateMarker_;
    seq.intermediates = std::string_view(intermediates_, intermediateCount_);
    seq.final = final;
    return seq;
}

void ZzVtParser::dispatchCsi(char final) {
    finishParamSlot();
    sink_->onCsiDispatch(makeSequence(final));
}

void ZzVtParser::dispatchEsc(char final) {
    sink_->onEscDispatch(std::string_view(intermediates_, intermediateCount_), final);
}

void ZzVtParser::dispatchOsc() {
    // 超长时递交截断内容；调用方应把 OSC 视为不可信输入再解析。
    sink_->onOscDispatch(oscBuffer_);
}

void ZzVtParser::flushDcsChunk() {
    if (!dcsChunk_.empty()) {
        sink_->onDcsPut(dcsChunk_);
        dcsChunk_.clear();
    }
}

void ZzVtParser::processByte(unsigned char byte) {
    // 全局规则一：CAN/SUB 在任何序列状态下中止序列并执行该控制符
    // （ECMA-48；也是 Williams 状态机的 anywhere transition）。
    if (byte == kCan || byte == kSub) {
        if (state_ == ZzParserState::DcsPassthrough) {
            flushDcsChunk();
            sink_->onDcsUnhook();
        }
        sink_->onExecute(byte);
        enterState(ZzParserState::Ground);
        return;
    }

    // 全局规则二：ESC 中止当前序列并开启新 Escape 序列。
    // OSC/DCS 的 ST（ESC \）例外，先在对应状态内消化。
    if (byte == kEsc && state_ != ZzParserState::OscString &&
        state_ != ZzParserState::DcsPassthrough &&
        state_ != ZzParserState::DcsIgnore &&
        state_ != ZzParserState::SosPmApcString) {
        enterState(ZzParserState::Escape);
        return;
    }

    // 重处理循环：OSC/DCS 中 ESC 后非 '\\' 的字节需要回炉到
    // Escape 状态再走一遍。每次迭代要么消费字节、要么完成一次
    // 状态切换且下一迭代必然消费，循环必然终止。
    for (;;) {
        switch (state_) {
        case ZzParserState::Ground:
            if (isC0(byte)) {
                sink_->onExecute(byte);
            } else if (byte == kDel) {
                // DEL 按 ECMA-48 忽略。
            } else {
                // 0x20-0x7E 与 0x80-0xFF（UTF-8 字节）都走 print 通道。
                sink_->onPrint(static_cast<char>(byte));
            }
            return;

        case ZzParserState::Escape:
            if (isC0(byte)) {
                sink_->onExecute(byte); // C0 立即执行，序列不中断
                return;
            }
            if (byte == kDel)
                return; // DEL 在序列中忽略（Williams anywhere 规则）
            if (isIntermediate(byte)) {
                if (intermediateCount_ < limits_.maxIntermediates)
                    intermediates_[intermediateCount_++] = static_cast<char>(byte);
                enterState(ZzParserState::EscapeIntermediate);
                return;
            }
            switch (byte) {
            case '[': enterState(ZzParserState::CsiEntry); return;        // CSI
            case 'P': enterState(ZzParserState::DcsEntry); return;        // DCS
            case ']': enterState(ZzParserState::OscString); return;       // OSC
            case 'X':                                                    // SOS
            case '^':                                                    // PM
            case '_': enterState(ZzParserState::SosPmApcString); return; // APC
            default: break;
            }
            if (isEscFinal(byte)) {
                dispatchEsc(static_cast<char>(byte));
                enterState(ZzParserState::Ground);
                return;
            }
            // 仅剩 0x80-0xFF：不识别 8-bit C1，中止 Escape 并把字节
            // 回炉到 Ground（作为 UTF-8 字节走 print 通道）。
            enterState(ZzParserState::Ground);
            continue;

        case ZzParserState::EscapeIntermediate:
            if (isC0(byte)) {
                sink_->onExecute(byte);
                return;
            }
            if (byte == kDel)
                return;
            if (isIntermediate(byte)) {
                if (intermediateCount_ < limits_.maxIntermediates)
                    intermediates_[intermediateCount_++] = static_cast<char>(byte);
                return;
            }
            if (isEscFinal(byte)) {
                dispatchEsc(static_cast<char>(byte));
                enterState(ZzParserState::Ground);
                return;
            }
            // 仅剩 0x80-0xFF：同 Escape 状态，回炉到 Ground。
            enterState(ZzParserState::Ground);
            continue;

        case ZzParserState::CsiEntry:
        case ZzParserState::CsiParam:
            if (isC0(byte)) {
                sink_->onExecute(byte);
                return;
            }
            if (byte == kDel)
                return; // CSI 内 DEL 忽略，不判畸形
            if (isDigit(byte)) {
                collectParamDigit(byte);
                state_ = ZzParserState::CsiParam;
                return;
            }
            if (byte == ';') {
                if (paramActive_) {
                    finishParamSlot();
                } else if (paramCount_ < limits_.maxParams) {
                    // 空槽（如 ESC[;5H 首参数）：记录为省略哨兵。
                    params_[paramCount_++] = ZzParamSequence::kOmitted;
                }
                state_ = ZzParserState::CsiParam;
                return;
            }
            if (byte == ':') {
                // M0 不支持子参数（SGR 4:3 等），按畸形处理。
                enterState(ZzParserState::CsiIgnore);
                return;
            }
            if (isPrivateMarker(byte)) {
                if (state_ == ZzParserState::CsiEntry && paramCount_ == 0 &&
                    !paramActive_ && privateMarker_ == 0) {
                    privateMarker_ = static_cast<char>(byte);
                    state_ = ZzParserState::CsiParam;
                } else {
                    // private marker 只允许出现在参数区开头。
                    enterState(ZzParserState::CsiIgnore);
                }
                return;
            }
            if (isIntermediate(byte)) {
                finishParamSlot();
                if (intermediateCount_ < limits_.maxIntermediates)
                    intermediates_[intermediateCount_++] = static_cast<char>(byte);
                enterState(ZzParserState::CsiIntermediate);
                return;
            }
            if (isCsiFinal(byte)) {
                dispatchCsi(static_cast<char>(byte));
                enterState(ZzParserState::Ground);
                return;
            }
            enterState(ZzParserState::CsiIgnore); // 0x7F 等：畸形
            return;

        case ZzParserState::CsiIntermediate:
            if (isC0(byte)) {
                sink_->onExecute(byte);
                return;
            }
            if (byte == kDel)
                return;
            if (isIntermediate(byte)) {
                if (intermediateCount_ < limits_.maxIntermediates)
                    intermediates_[intermediateCount_++] = static_cast<char>(byte);
                return;
            }
            if (isCsiFinal(byte)) {
                dispatchCsi(static_cast<char>(byte));
                enterState(ZzParserState::Ground);
                return;
            }
            enterState(ZzParserState::CsiIgnore);
            return;

        case ZzParserState::CsiIgnore:
            if (isC0(byte)) {
                sink_->onExecute(byte);
                return;
            }
            if (isCsiFinal(byte))
                enterState(ZzParserState::Ground);
            return; // 其余字节全部丢弃

        case ZzParserState::OscString:
            if (oscEscPending_) {
                oscEscPending_ = false;
                if (byte == '\\') { // ST 到达（ESC + 反斜杠）
                    dispatchOsc();
                    enterState(ZzParserState::Ground);
                    return;
                }
                if (byte == kEsc) { // 连续 ESC：重新等待
                    oscEscPending_ = true;
                    return;
                }
                // ESC 后非 ST：中止 OSC（不 dispatch），该字节按新
                // Escape 序列重新处理。
                enterState(ZzParserState::Escape);
                continue;
            }
            if (byte == kEsc) {
                oscEscPending_ = true;
                return;
            }
            if (byte == kBel) { // xterm 扩展：BEL 终止 OSC
                dispatchOsc();
                enterState(ZzParserState::Ground);
                return;
            }
            if (isC0(byte) || byte == kDel)
                return; // OSC 内 C0/DEL 忽略（xterm 行为）
            if (oscBuffer_.size() < limits_.maxOscLength)
                oscBuffer_.push_back(static_cast<char>(byte));
            else
                oscTruncated_ = true;
            return;

        case ZzParserState::DcsEntry:
        case ZzParserState::DcsParam:
            if (isC0(byte) || byte == kDel)
                return; // DCS 头部忽略 C0/DEL（不执行，区别于 CSI）
            if (isDigit(byte)) {
                collectParamDigit(byte);
                state_ = ZzParserState::DcsParam;
                return;
            }
            if (byte == ';') {
                if (paramActive_) {
                    finishParamSlot();
                } else if (paramCount_ < limits_.maxParams) {
                    params_[paramCount_++] = ZzParamSequence::kOmitted;
                }
                state_ = ZzParserState::DcsParam;
                return;
            }
            if (byte == ':') {
                enterState(ZzParserState::DcsIgnore);
                return;
            }
            if (isPrivateMarker(byte)) {
                if (state_ == ZzParserState::DcsEntry && paramCount_ == 0 &&
                    !paramActive_ && privateMarker_ == 0) {
                    privateMarker_ = static_cast<char>(byte);
                    state_ = ZzParserState::DcsParam;
                } else {
                    enterState(ZzParserState::DcsIgnore);
                }
                return;
            }
            if (isIntermediate(byte)) {
                finishParamSlot();
                if (intermediateCount_ < limits_.maxIntermediates)
                    intermediates_[intermediateCount_++] = static_cast<char>(byte);
                enterState(ZzParserState::DcsIntermediate);
                return;
            }
            if (isCsiFinal(byte)) {
                finishParamSlot();
                sink_->onDcsHook(makeSequence(static_cast<char>(byte)));
                enterState(ZzParserState::DcsPassthrough);
                dcsChunk_.clear();
                dcsEscPending_ = false;
                return;
            }
            enterState(ZzParserState::DcsIgnore);
            return;

        case ZzParserState::DcsIntermediate:
            if (isC0(byte) || byte == kDel)
                return;
            if (isIntermediate(byte)) {
                if (intermediateCount_ < limits_.maxIntermediates)
                    intermediates_[intermediateCount_++] = static_cast<char>(byte);
                return;
            }
            if (isCsiFinal(byte)) {
                finishParamSlot();
                sink_->onDcsHook(makeSequence(static_cast<char>(byte)));
                enterState(ZzParserState::DcsPassthrough);
                dcsChunk_.clear();
                dcsEscPending_ = false;
                return;
            }
            enterState(ZzParserState::DcsIgnore);
            return;

        case ZzParserState::DcsPassthrough:
            if (dcsEscPending_) {
                dcsEscPending_ = false;
                if (byte == '\\') { // ST：正常结束
                    flushDcsChunk();
                    sink_->onDcsUnhook();
                    enterState(ZzParserState::Ground);
                    return;
                }
                if (byte == kEsc) {
                    dcsEscPending_ = true;
                    return;
                }
                // ESC 后非 ST：中止 DCS，该字节按新 Escape 序列处理。
                flushDcsChunk();
                sink_->onDcsUnhook();
                enterState(ZzParserState::Escape);
                continue;
            }
            if (byte == kEsc) {
                dcsEscPending_ = true;
                return;
            }
            if (isC0(byte) || byte == kDel)
                return; // 透传通道忽略 C0/DEL
            dcsChunk_.push_back(static_cast<char>(byte));
            if (dcsChunk_.size() >= limits_.maxDcsChunk)
                flushDcsChunk();
            return;

        case ZzParserState::DcsIgnore:
            if (byte == kEsc) {
                // 等待 ST 的 ESC \；其他 ESC 序列直接中止。
                dcsEscPending_ = true;
                return;
            }
            if (dcsEscPending_) {
                dcsEscPending_ = false;
                if (byte == '\\')
                    enterState(ZzParserState::Ground);
                else if (byte == kEsc)
                    dcsEscPending_ = true;
                else
                    enterState(ZzParserState::Escape); // 中止并重启 Escape
                return;
            }
            return; // 丢弃到 ST 为止

        case ZzParserState::SosPmApcString:
            // SOS/PM/APC 不被本终端支持，整体忽略到 ST（ESC \）。
            // 与 OSC 不同，BEL 不构成终止符（ECMA-48）。
            if (byte == kEsc) {
                oscEscPending_ = true; // 复用 pending 标志
                return;
            }
            if (oscEscPending_) {
                oscEscPending_ = false;
                if (byte == '\\')
                    enterState(ZzParserState::Ground);
                else if (byte == kEsc)
                    oscEscPending_ = true;
                else
                    enterState(ZzParserState::Escape);
            }
            return;
        }
    }
}
