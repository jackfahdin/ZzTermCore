/**
 * @file Parser.h
 * @brief VT/ANSI/xterm 转义序列增量解析器（语法层）。
 *
 * 设计参考 Paul Williams 的 DEC-compatible parser 状态机
 * （https://vt500.net），语义对齐 ECMA-48 (ISO/IEC 6429) 与
 * xterm 控制序列文档 ctlseqs.txt（Thomas E. Dickey）。
 *
 * Parser 只负责"语法 dispatch"：把字节流解析成 print / C0 执行 /
 * CSI / OSC / DCS / ESC 等事件，通过 ZzParserSink 回调交给上层；
 * 语义（光标移动、SGR 样式、Screen 修改等）由 Terminal 层负责，
 * Parser 绝不直接触碰 Screen。
 *
 * UTF-8 组合方式：本解析器是字节级状态机。在 UTF-8 模式下
 * 0x80-0xFF 不属于任何 C1 控制字符（不识别 8-bit C1，与 xterm
 * UTF-8 模式一致），因此多字节 UTF-8 序列的 continuation byte
 * 会原样进入 onPrint。典型接线方式为：
 *
 *   bytes -> ZzVtParser::feed -> ZzParserSink::onPrint(byte)
 *                              -> ZzUtf8Decoder::feed(byte)
 *                              -> Terminal::putChar(code point)
 *
 * 即 UTF-8 解码器只消费 print 通道的字节，控制字节在进入解码器
 * 之前已被状态机分流，二者可独立测试、独立 Fuzz。
 */
#ifndef ZZTERM_PARSER_H
#define ZZTERM_PARSER_H

#include "ZzTerm/Export.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Parser 状态机状态枚举。
 *
 * 覆盖 Architecture.md 第 7 节要求的最小状态集：
 * Ground、Escape（含 Intermediate）、CSI Entry/Param/Intermediate
 * （外加 Ignore）、OSC String、DCS Entry/Param/Intermediate/
 * Passthrough（外加 Ignore），以及 SOS/PM/APC 串的忽略状态。
 * 仅用于调试与 Terminal Inspector 展示，不应用于语义判断。
 */
enum class ZzParserState : std::uint8_t {
    Ground,             ///< 普通文本状态
    Escape,             ///< ESC 已进入，等待 intermediate/final
    EscapeIntermediate, ///< ESC 后已收集至少一个 intermediate
    CsiEntry,           ///< CSI 入口（ESC [）
    CsiParam,           ///< CSI 参数收集（含 private marker 与 ';'）
    CsiIntermediate,    ///< CSI intermediate 收集
    CsiIgnore,          ///< CSI 畸形，忽略到 final byte 为止
    OscString,          ///< OSC 字符串收集（ESC ] ... BEL/ST）
    DcsEntry,           ///< DCS 入口（ESC P）
    DcsParam,           ///< DCS 参数收集
    DcsIntermediate,    ///< DCS intermediate 收集
    DcsPassthrough,     ///< DCS 数据透传（流式回调，不整段缓存）
    DcsIgnore,          ///< DCS 畸形，忽略到 ST 为止
    SosPmApcString,     ///< SOS/PM/APC 字符串，整体忽略到 ST 为止
};

/**
 * @brief 解析器安全上限配置。
 *
 * 远端字节流视为不可信（Architecture.md 第 15 节），所有缓冲
 * 必须有界。所有上限均可由调用方按场景调整。
 */
struct ZzParserLimits {
    /**
     * @brief OSC payload 最大缓存字节数。
     *
     * 超限后停止追加（payload 被截断），状态机继续消费直到
     * 终止符，随后照常 dispatch 截断结果，保证内存有界且
     * 不丢失后续同步。
     */
    std::size_t maxOscLength = 64 * 1024;

    /**
     * @brief CSI/DCS 单条序列允许的最大参数个数。
     *
     * 超出部分不再新增参数槽，最后一个参数槽继续吸收数字，
     * 防止参数向量无界增长。
     */
    std::uint32_t maxParams = 32;

    /**
     * @brief 单个参数数值上限（钳位，防整数溢出）。
     *
     * 超过上限的十进制累积值被钳到该值。
     */
    std::int32_t maxParamValue = 65535;

    /**
     * @brief intermediate 字节最大个数（ESC/CSI/DCS 共用）。
     */
    std::uint32_t maxIntermediates = 4;

    /**
     * @brief DCS 透传回调单次递交的最大字节数。
     *
     * DCS payload 不做整段缓存，按此块大小流式递交，
     * 内存占用与 payload 长度无关。
     */
    std::size_t maxDcsChunk = 4096;
};

/**
 * @brief 一次 CSI 序列或 DCS 头部的解析结果。
 *
 * 所有视图字段只在回调执行期间有效，指向 Parser 内部缓冲，
 * 回调返回后即失效，需要保留时必须拷贝。
 */
struct ZzParamSequence {
    /// 参数槽中表示"省略"（如 ESC[;5H 的第一个参数）的哨兵值。
    static constexpr std::int32_t kOmitted = -1;

    /**
     * @brief 参数列表，省略的参数槽填 kOmitted。
     *
     * 个数不超过 ZzParserLimits::maxParams。
     * 注意：M0 暂不支持 ':' 子参数（SGR 4:3 等），出现 ':'
     * 视为畸形进入 ignore，后续里程碑再扩展。
     */
    std::span<const std::int32_t> params;

    /**
     * @brief 参数区首字节的 private marker。
     *
     * 取值 0（无）或 '<' / '=' / '>' / '?' 之一（如 DEC 私有
     * 模式 ESC[?25h 的 '?'）。
     */
    char privateMarker = 0;

    /// intermediate 字节（如 ESC[!p 的 '!'），长度有上限。
    std::string_view intermediates;

    /// final byte，决定序列语义（如 'H' = CUP、'm' = SGR）。
    char final = 0;
};

/**
 * @brief Parser 事件接收接口（visitor/sink）。
 *
 * Terminal 层实现该接口接收语法事件并执行语义。所有方法
 * 默认空实现，子类按需覆盖。回调内允许再次修改自身状态，
 * 但不得在同一个 feed() 调用中递归调用 Parser::feed。
 *
 * 线程安全：Parser 与 Sink 均非线程安全，字节流必须串行 feed。
 */
class ZZTERM_API ZzParserSink {
public:
    virtual ~ZzParserSink() = default;

    /**
     * @brief 可打印字节（Ground 状态下 0x20-0x7F 与 0x80-0xFF）。
     *
     * 字节级输出，一个 code point 可能由多个连续 onPrint 字节
     * 组成；请与 ZzUtf8Decoder 串联后再做宽度/grapheme 处理
     * （禁止假设 1 byte 或 1 code point == 1 cell）。
     *
     * @param byte 可打印字节（可能是多字节 UTF-8 序列的一部分）。
     */
    virtual void onPrint(char byte);

    /**
     * @brief C0 控制字符执行（0x00-0x1F，如 BEL/BS/HT/LF/CR）。
     *
     * 按 ECMA-48，C0 在序列中途出现时立即执行且不中断当前序列
     * （CAN/SUB/ESC 除外，它们会中止序列，见实现注释）。
     *
     * @param control C0 控制字符码值（0x00-0x1F）。
     */
    virtual void onExecute(std::uint8_t control);

    /**
     * @brief CSI 序列分发（ESC [ params intermediates final）。
     * @param seq CSI 参数、private marker、intermediate 与 final byte
     *            的解析结果；视图仅在回调期间有效。
     */
    virtual void onCsiDispatch(const ZzParamSequence& seq);

    /**
     * @brief ESC 序列分发（ESC intermediates final）。
     *
     * 含 charset 选择（ESC ( B）、DECSC（ESC 7）等。
     *
     * @param intermediates intermediate 字节序列（可为空）。
     * @param final final byte，决定序列语义。
     */
    virtual void onEscDispatch(std::string_view intermediates, char final);

    /**
     * @brief OSC 字符串分发（ESC ] payload BEL/ST）。
     *
     * payload 语义切分（如 OSC 0;title 的 ';'）由上层负责。
     * 超长 payload 递交的是按上限截断后的内容。
     *
     * @param payload OSC 负载（不含终止符，超长部分已截断）。
     */
    virtual void onOscDispatch(std::string_view payload);

    /**
     * @brief DCS 序列开始（ESC P params intermediates final）。
     *
     * 之后 payload 通过 onDcsPut 流式递交，onDcsUnhook 结束。
     *
     * @param header DCS 头部（参数/intermediate/final）解析结果。
     */
    virtual void onDcsHook(const ZzParamSequence& header);

    /**
     * @brief DCS payload 数据块（多次调用，块大小有上限）。
     * @param data 本次递交的 payload 数据块（长度不超过
     *             ZzParserLimits::maxDcsChunk）。
     */
    virtual void onDcsPut(std::string_view data);

    /**
     * @brief DCS 序列结束（ST 到达或被中止）。
     */
    virtual void onDcsUnhook();
};

/**
 * @brief VT/ANSI/xterm 增量解析器。
 *
 * 支持任意 chunk 边界：所有状态（含 OSC/DCS 缓冲与参数收集）
 * 保存在对象内，可在任意字节处截断续传，结果与一次性输入一致。
 *
 * 健壮性（Architecture.md 第 15 节）：
 * - OSC 缓冲有上限，超限截断并安全恢复；
 * - DCS payload 流式递交，无整段缓存；
 * - CSI/DCS 参数个数与数值范围有限制，防整数溢出；
 * - CAN(0x18)/SUB(0x1A) 在任何序列状态中止序列回到 Ground，
 *   畸形序列进入 Ignore 状态消耗到 final byte，不会死循环；
 * - 不识别 8-bit C1（0x80-0x9F），UTF-8 安全。
 */
class ZZTERM_API ZzVtParser {
public:
    /**
     * @brief 构造解析器。
     * @param sink 事件接收方，必须非空，生命周期须长于本对象。
     * @param limits 安全上限配置，默认即可满足常规终端场景。
     */
    explicit ZzVtParser(ZzParserSink* sink, ZzParserLimits limits = {});

    ZzVtParser(const ZzVtParser&) = delete;
    ZzVtParser& operator=(const ZzVtParser&) = delete;

    /**
     * @brief 输入一段原始字节流。
     *
     * 可在 UTF-8 字符或控制序列中间截断；未完成状态保留在
     * 对象内，后续 feed() 自动续接。
     *
     * @param data 原始字节（远端输入一律视为不可信）。
     */
    void feed(std::string_view data);

    /**
     * @brief 当前状态机状态（用于调试与 Terminal Inspector）。
     * @return 当前状态枚举值。
     */
    ZzParserState state() const { return state_; }

    /**
     * @brief 复位到 Ground 并丢弃全部未完成状态与缓冲。
     *
     * 用于 RIS（ESC c）等需要整端复位的语义场景，由上层调用。
     */
    void reset();

private:
    void processByte(unsigned char byte);
    void enterState(ZzParserState next);
    void clearSequence();
    void collectParamDigit(unsigned char byte);
    void finishParamSlot();
    void dispatchCsi(char final);
    void dispatchEsc(char final);
    void dispatchOsc();
    void flushDcsChunk();
    ZzParamSequence makeSequence(char final) const;

    ZzParserSink* sink_;
    ZzParserLimits limits_;
    ZzParserState state_ = ZzParserState::Ground;

    // 参数收集（CSI/DCS 共用）；paramActive_ 标记当前槽是否已有数字。
    std::int32_t params_[64]{}; // 容量冗余，实际槽数受 limits_.maxParams 约束
    std::uint32_t paramCount_ = 0;
    bool paramActive_ = false;
    char privateMarker_ = 0;
    char intermediates_[16]{};
    std::uint32_t intermediateCount_ = 0;

    std::string oscBuffer_;
    bool oscTruncated_ = false;
    bool oscEscPending_ = false; // OSC 内已见到 ESC，等待判断是否为 ST

    // DCS 透传小缓冲，攒块后通过 onDcsPut 递交，防逐字节回调开销。
    std::string dcsChunk_;
    bool dcsEscPending_ = false;
};

#endif // ZZTERM_PARSER_H
