// Contour 后端 smoke：验证 vtparser 的 include 路径 / C++23 / 静态链接三件套。
// 仅构建集成验证，不断言完整终端语义（那是 M1 ZzContourBackend 与兼容性测试的职责）。
#include <vtparser/Parser.hpp>
#include <vtparser/ParserEvents.hpp>

#include <gsl/span>

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

class SmokeEvents : public vtparser::NullParserEvents {
public:
    void print(char32_t cp) override { printed += static_cast<char>(cp); }
    std::size_t print(std::string_view chars, std::size_t cellCount) override
    {
        // bulk 通道：parser 忽略返回值（不做逐字符回退），在此直接累积文本。
        printed += chars;
        return cellCount;
    }
    void execute(char controlCode) override
    {
        if (controlCode == '\r') ++crCount;
        if (controlCode == '\n') ++lfCount;
    }
    void dispatchCSI(char finalChar) override
    {
        if (finalChar == 'm') ++sgrCount;
    }

    std::string printed;
    int crCount = 0;
    int lfCount = 0;
    int sgrCount = 0;
};

} // namespace

int main()
{
    SmokeEvents events;
    vtparser::Parser<SmokeEvents> parser { events };

    const std::string_view bytes = "\x1b[1;31mZZ\x1b[0m ok\r\n";
    parser.parseFragment(gsl::span<const char> { bytes.data(), bytes.size() });

    assert(events.printed == "ZZ ok");
    assert(events.sgrCount == 2);
    assert(events.crCount == 1);
    assert(events.lfCount == 1);
    return 0;
}
