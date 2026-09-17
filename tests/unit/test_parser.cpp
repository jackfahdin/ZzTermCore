/**
 * @file test_parser.cpp
 * @brief ZzVtParser 单元测试：用 mock sink 记录回调事件序列并断言。
 *
 * 覆盖：纯文本 print、C0 控制符、CSI 光标/SGR 序列、OSC 的 BEL/ST
 * 两种终止、任意 chunk 边界截断续传一致性、超长 OSC 上限保护、
 * 畸形序列恢复、ESC/DCS 序列、参数省略与钳位。
 *
 * 测试约定（仓库无测试框架）：自建 ZZ_TEST_EXPECT 宏断言，
 * 全部通过返回 0，任一失败返回非零。
 */
#include "ZzTerm/Parser.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_failures = 0;

#define ZZ_TEST_EXPECT(cond)                                                     \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

namespace {

// 记录型 sink：把每次回调格式化成一行文本事件，便于整体比较。
struct MockSink : ZzParserSink {
    std::vector<std::string> events;

    static std::string formatSeq(const char* tag, const ZzParamSequence& seq) {
        std::string s = tag;
        s.push_back(':');
        if (seq.privateMarker)
            s.push_back(seq.privateMarker);
        for (std::size_t i = 0; i < seq.params.size(); ++i) {
            if (i)
                s.push_back(';');
            if (seq.params[i] == ZzParamSequence::kOmitted)
                s += "<omit>";
            else
                s += std::to_string(seq.params[i]);
        }
        s.append(seq.intermediates);
        s.push_back(seq.final);
        return s;
    }

    void onPrint(char byte) override {
        // 相邻 print 合并为一个事件，模拟上层按 run 消费。
        if (!events.empty() && events.back().rfind("P:", 0) == 0)
            events.back().push_back(byte);
        else
            events.push_back(std::string("P:") + byte);
    }
    void onExecute(std::uint8_t control) override {
        char buf[16];
        std::snprintf(buf, sizeof buf, "E:%02X", control);
        events.push_back(buf);
    }
    void onCsiDispatch(const ZzParamSequence& seq) override {
        events.push_back(formatSeq("CSI", seq));
    }
    void onEscDispatch(std::string_view intermediates, char final) override {
        events.push_back("ESC:" + std::string(intermediates) + final);
    }
    void onOscDispatch(std::string_view payload) override {
        events.push_back("OSC:" + std::string(payload));
    }
    void onDcsHook(const ZzParamSequence& header) override {
        events.push_back(formatSeq("DCS", header));
    }
    void onDcsPut(std::string_view data) override {
        if (!events.empty() && events.back().rfind("PUT:", 0) == 0)
            events.back().append(data);
        else
            events.push_back("PUT:" + std::string(data));
    }
    void onDcsUnhook() override { events.push_back("UNHOOK"); }
};

std::string join(const std::vector<std::string>& events) {
    std::string out;
    for (const auto& e : events) {
        out += e;
        out.push_back('\n');
    }
    return out;
}

// 把输入按固定块大小分段喂入，返回事件日志。
std::string feedInChunks(std::string_view data, std::size_t chunkSize,
                         ZzParserLimits limits = {}) {
    MockSink sink;
    ZzVtParser parser(&sink, limits);
    for (std::size_t i = 0; i < data.size(); i += chunkSize)
        parser.feed(data.substr(i, chunkSize));
    return join(sink.events);
}

void testPlainText() {
    ZZ_TEST_EXPECT(feedInChunks("hello, world", 1024) == "P:hello, world\n");
}

void testC0Controls() {
    // BEL BS HT LF CR 全部走 execute；DEL 忽略。
    std::string in;
    in.push_back('\a');
    in.push_back('\b');
    in.push_back('\t');
    in.push_back('\n');
    in.push_back('\r');
    in.push_back('\x7F');
    ZZ_TEST_EXPECT(feedInChunks(in, 1024) ==
                   "E:07\nE:08\nE:09\nE:0A\nE:0D\n");
}

void testCsiCursor() {
    // CUP：ESC[1;2H
    ZZ_TEST_EXPECT(feedInChunks("\x1b[1;2H", 1024) == "CSI:1;2H\n");
    // 省略参数：ESC[;5H -> 首槽 kOmitted
    ZZ_TEST_EXPECT(feedInChunks("\x1b[;5H", 1024) == "CSI:<omit>;5H\n");
    // 无参数：ESC[C
    ZZ_TEST_EXPECT(feedInChunks("\x1b[C", 1024) == "CSI:C\n");
    // DEC 私有模式：ESC[?25h
    ZZ_TEST_EXPECT(feedInChunks("\x1b[?25h", 1024) == "CSI:?25h\n");
    // intermediate：ESC[!p（DECSCL）
    ZZ_TEST_EXPECT(feedInChunks("\x1b[!p", 1024) == "CSI:!p\n");
    // 序列中途的 C0 立即执行且不中断 CSI
    ZZ_TEST_EXPECT(feedInChunks("\x1b[1\x07"";2H", 1024) == "E:07\nCSI:1;2H\n");
}

void testSgr() {
    ZZ_TEST_EXPECT(feedInChunks("\x1b[1;31m", 1024) == "CSI:1;31m\n");
    ZZ_TEST_EXPECT(feedInChunks("\x1b[0;1;4;38;5;196m", 1024) ==
                   "CSI:0;1;4;38;5;196m\n");
}

void testOscTermination() {
    // BEL 终止
    ZZ_TEST_EXPECT(feedInChunks("\x1b]0;title\x07", 1024) == "OSC:0;title\n");
    // ST（ESC \）终止
    ZZ_TEST_EXPECT(feedInChunks("\x1b]0;title\x1b\\", 1024) == "OSC:0;title\n");
    // 终止后紧跟普通文本
    ZZ_TEST_EXPECT(feedInChunks("\x1b]8;;http://x\x07link", 1024) ==
                   "OSC:8;;http://x\nP:link\n");
}

void testEscDispatch() {
    ZZ_TEST_EXPECT(feedInChunks("\x1b""7", 1024) == "ESC:7\n");   // DECSC
    ZZ_TEST_EXPECT(feedInChunks("\x1b(B", 1024) == "ESC:(B\n");   // charset
    ZZ_TEST_EXPECT(feedInChunks("\x1b#8", 1024) == "ESC:#8\n");   // DECALN
    ZZ_TEST_EXPECT(feedInChunks("\x1b=", 1024) == "ESC:=\n");     // DECKPAM
}

void testDcs() {
    // DECRQSS 风格：ESC P 1;2 $ q payload ST
    ZZ_TEST_EXPECT(feedInChunks("\x1bP1;2$qABC\x1b\\", 1024) ==
                   "DCS:1;2$q\nPUT:ABC\nUNHOOK\n");
    // 空 payload
    ZZ_TEST_EXPECT(feedInChunks("\x1bPq\x1b\\", 1024) == "DCS:q\nUNHOOK\n");
}

void testChunkBoundaryEquivalence() {
    // 复合输入覆盖 print/C0/CSI/SGR/OSC(BEL)/OSC(ST)/DCS/ESC。
    const std::string data =
        std::string("ab\x07") + "\x1b[1;2H" + "cd" + "\x1b[0;1;31m" +
        "\x1b]0;ti\x07" "le" + "\x1b]8;;u\x1b\\" + "\x1bP1$qXY\x1b\\" +
        "\x1b(B" + "\x1b[?1049h" + "zz";
    const std::string reference = feedInChunks(data, data.size());
    // 逐字节喂入（最极端的 chunk 边界）。
    ZZ_TEST_EXPECT(feedInChunks(data, 1) == reference);
    // 所有两点切分位置逐一验证。
    for (std::size_t cut = 0; cut <= data.size(); ++cut) {
        MockSink sink;
        ZzVtParser parser(&sink);
        parser.feed(std::string_view(data).substr(0, cut));
        parser.feed(std::string_view(data).substr(cut));
        if (join(sink.events) != reference) {
            ++g_failures;
            std::fprintf(stderr, "FAIL chunk split at %zu\n", cut);
        }
    }
}

void testOscLengthLimit() {
    ZzParserLimits limits;
    limits.maxOscLength = 8;
    // payload 20 字节，上限 8：dispatch 截断结果，之后状态恢复正常。
    std::string in = "\x1b]2;" + std::string(20, 'x') + "\x07" "OK";
    ZZ_TEST_EXPECT(feedInChunks(in, 1024, limits) ==
                   "OSC:2;" + std::string(6, 'x') + "\nP:OK\n");
    // 超限发生在 chunk 中间也不影响后续同步。
    ZZ_TEST_EXPECT(feedInChunks(in, 1, limits) ==
                   feedInChunks(in, 1024, limits));
}

void testParamLimits() {
    // 数值钳位（默认上限 65535），防整数溢出。
    ZZ_TEST_EXPECT(feedInChunks("\x1b[999999999999H", 1024) == "CSI:65535H\n");
    // 参数个数上限：超出部分并入最后一槽。
    ZzParserLimits limits;
    limits.maxParams = 4;
    ZZ_TEST_EXPECT(feedInChunks("\x1b[1;2;3;4;5;6m", 1024, limits) ==
                   "CSI:1;2;3;456m\n");
}

void testMalformedRecovery() {
    // CAN 中止 CSI，随后文本正常 print，不 dispatch 半条序列。
    std::string in = "\x1b[1;2";
    in.push_back('\x18');
    in += "abc";
    ZZ_TEST_EXPECT(feedInChunks(in, 1024) == "E:18\nP:abc\n");
    // 参数区出现第二个 private marker -> CsiIgnore，消耗到 final，
    // 不 dispatch；之后文本不受影响。
    ZZ_TEST_EXPECT(feedInChunks("\x1b[?1?h" "z", 1024) == "P:z\n");
    // CSI intermediate 后再出现参数字节 -> CsiIgnore。
    ZZ_TEST_EXPECT(feedInChunks("\x1b[!1p" "q", 1024) == "P:q\n");
    // OSC 内 ESC 后非 '\\'：中止 OSC（不 dispatch），字节按新序列处理。
    ZZ_TEST_EXPECT(feedInChunks("\x1b]0;t\x1b[32mX", 1024) ==
                   "CSI:32m\nP:X\n");
    // ESC 后跟不可识别字节：安全丢弃回 Ground，不死循环。
    ZZ_TEST_EXPECT(feedInChunks("\x1b\x80ok", 1024) == "P:\x80ok\n");
    // SOS/PM/APC 忽略到 ST。
    ZZ_TEST_EXPECT(feedInChunks("\x1b_kkk\x1b\\hi", 1024) == "P:hi\n");
    // DCS 畸形（参数区出现 ':'）-> DcsIgnore 到 ST。
    ZZ_TEST_EXPECT(feedInChunks("\x1bP1:2qDATA\x1b\\ok", 1024) == "P:ok\n");
}

void testEightBitC1NotRecognized() {
    // 不识别 8-bit C1：0x9B 应作为普通字节进 print 通道（UTF-8 安全）。
    std::string in = "\x9B"
                     "31m";
    ZZ_TEST_EXPECT(feedInChunks(in, 1024) == "P:\x9B"
                                             "31m\n");
}

void testStateAccessorAndReset() {
    MockSink sink;
    ZzVtParser parser(&sink);
    parser.feed("\x1b[1;");
    ZZ_TEST_EXPECT(parser.state() == ZzParserState::CsiParam);
    parser.reset();
    ZZ_TEST_EXPECT(parser.state() == ZzParserState::Ground);
    parser.feed("H"); // reset 丢弃了未完成 CSI，'H' 按文本处理
    ZZ_TEST_EXPECT(join(sink.events) == "P:H\n");
}

} // namespace

int main() {
    testPlainText();
    testC0Controls();
    testCsiCursor();
    testSgr();
    testOscTermination();
    testEscDispatch();
    testDcs();
    testChunkBoundaryEquivalence();
    testOscLengthLimit();
    testParamLimits();
    testMalformedRecovery();
    testEightBitC1NotRecognized();
    testStateAccessorAndReset();

    if (g_failures == 0) {
        std::printf("test_parser: all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_parser: %d failure(s)\n", g_failures);
    return 1;
}
