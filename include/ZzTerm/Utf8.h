#pragma once

/**
 * @file Utf8.h
 * @brief 增量 UTF-8 解码器：unicode 模块的第一块基石。
 *
 * 遵循标准：
 * - RFC 3629（UTF-8, a transformation format of ISO 10646）；
 * - Unicode Standard §3.9 "Unicode Encoding Forms"：
 *   - 格式良好的 UTF-8 字节序列表（Well-Formed UTF-8 Byte Sequences，
 *     即 Table 3-7），据此拒绝超长编码（overlong）、代理区编码、
 *     超出 U+10FFFF 的码点以及 5/6 字节序列；
 *   - 非法序列按 maximal subpart 语义处理：非法子序列中属于某个
 *     格式良好序列前缀的最长部分，整体替换为单个 U+FFFD；导致非法的
 *     那个字节随后作为新序列的起点重新处理（D93b 推荐实践）。
 *
 * 本模块平台无关、零第三方依赖，不 include 任何平台头文件
 * （Architecture.md §2）。Architecture.md 未规定命名空间，
 * 统一使用 zz（§18 仅约束标识符英文与 Zz 前缀）。
 */

#include "ZzTerm/Export.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

/**
 * @brief 非法 UTF-8 的替换字符 U+FFFD REPLACEMENT CHARACTER。
 */
inline constexpr char32_t ZzReplacementChar = U'\ufffd';

/**
 * @brief 增量 UTF-8 解码器。
 *
 * 远端字节流可在任意字节边界截断；未完成的序列状态保留在解码器内，
 * 后续 feed() 继续解码。解码器按字节处理，每次 feed() 输出 0..N 个
 * 码点（char32_t），非法输入按 maximal subpart 语义输出 U+FFFD。
 *
 * 解码器只做 UTF-8 -> code point 流的转换：不做 grapheme 聚簇
 * （emoji/ZWJ 序列按独立码点输出），不做 East Asian Width 计算，
 * 这些是 screen/unicode 上层模块的职责（Architecture.md §6、§8）。
 *
 * 线程安全：非线程安全，单个解码器实例应由单一线程驱动。
 */
class ZZTERM_API ZzUtf8Decoder {
public:
    ZzUtf8Decoder() = default;

    /**
     * @brief 重置到初始状态，丢弃未完成的序列。
     * @note 丢弃的未完成序列不会输出 U+FFFD；若需要替换语义，先调 finish()。
     */
    void reset() noexcept;

    /**
     * @brief 是否存在跨 feed() 边界的未完成多字节序列。
     * @return true 表示内部缓存了 1..3 个等待 continuation 的字节。
     */
    [[nodiscard]] bool pending() const noexcept { return remaining_ > 0; }

    /**
     * @brief 增量喂入字节流，对每个解码出的码点调用 emit。
     *
     * 对 feed() 的多次任意切块调用与一次性喂入全量字节，输出的码点流
     * 完全一致。发生非法序列时，先输出 maximal subpart 对应的 U+FFFD，
     * 再把触发非法的字节按新序列起点重新解释，解码器不会进入错误锁定
     * 状态。
     *
     * @param data 原始字节流，长度可为 0。
     * @param emit 回调，签名 void(char32_t codePoint)；非法输入以
     *             ZzReplacementChar（U+FFFD）输出。
     */
    template <typename Emit>
    void feed(std::span<const std::byte> data, Emit&& emit) {
        for (const std::byte raw : data) {
            const std::uint8_t byte = static_cast<std::uint8_t>(raw);
            // 一个字节最多触发两轮 decodeByte()：第一轮把已缓存的非法前缀
            // 作为 maximal subpart 输出 U+FFFD 且不消费该字节，
            // 第二轮以全新状态重新解释它。
            for (;;) {
                char32_t codePoint = 0;
                bool produced = false;
                if (decodeByte(byte, codePoint, produced)) {
                    if (produced) {
                        emit(codePoint);
                    }
                    break;
                }
                emit(ZzReplacementChar);
            }
        }
    }

    /**
     * @brief feed() 的 std::string_view 便捷重载。
     * @param data 原始字节流（允许内嵌 NUL）。
     * @param emit 回调，签名 void(char32_t)。
     */
    template <typename Emit>
    void feed(std::string_view data, Emit&& emit) {
        const auto* bytes = reinterpret_cast<const std::byte*>(data.data());
        feed(std::span<const std::byte>(bytes, data.size()),
             std::forward<Emit>(emit));
    }

    /**
     * @brief 输入流结束时收尾：若仍有未完成序列，按 maximal subpart
     *        语义输出一个 U+FFFD 并复位。
     * @param emit 回调，签名 void(char32_t)。
     */
    template <typename Emit>
    void finish(Emit&& emit) {
        if (remaining_ > 0) {
            reset();
            emit(ZzReplacementChar);
        }
    }

    /**
     * @brief 便捷接口：一次性解码完整输入并收集全部码点。
     * @param data 完整输入字节流（可含非法 UTF-8）。
     * @return 解码出的码点序列；非法输入处为 ZzReplacementChar（U+FFFD）。
     * @note 会调用 finish() 收尾并复位解码器。面向测试与小数据，
     *       热路径请使用 feed() + 回调以避免 vector 分配。
     */
    [[nodiscard]] std::vector<char32_t> decodeAll(std::string_view data);

private:
    /**
     * @brief 按状态机处理单字节。
     * @param byte 输入字节。
     * @param[out] codePoint 产生的码点（produced 为 true 时有效）。
     * @param[out] produced 本次是否产生了码点。
     * @return true 表示字节已被消费；false 表示字节未被消费，
     *         已缓存的前缀被判定为非法 maximal subpart（调用方负责
     *         输出 U+FFFD 并重试该字节）。
     */
    bool decodeByte(std::uint8_t byte, char32_t& codePoint,
                    bool& produced) noexcept;

    std::uint32_t accumulator_ = 0;   ///< 已累积的码点位
    std::uint8_t remaining_ = 0;      ///< 还需要的 continuation 字节数
    std::uint8_t lowerBound_ = 0x80;  ///< 下一个 continuation 的合法下界（Table 3-7）
    std::uint8_t upperBound_ = 0xBF;  ///< 下一个 continuation 的合法上界（Table 3-7）
};

