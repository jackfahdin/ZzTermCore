// M13 下游消费验证：经 find_package(ZzTermCore) 安装态消费 ZzTermCore 与
// ZzTerm::Pty——验证打包形态（导出目标/头文件/链接）与基础调用通路，
// 行为正确性归主仓测试族，本工程不做行为断言扩展。
#include <ZzTerm/Terminal.h>

#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>

#if defined(ZZ_HAVE_PTY_TARGET)
#include "ZzPty.h"
#endif

int main()
{
    int failures = 0;

    // ZzTerm::ZzTermCore：构造 -> feed -> renderView 读回 marker。
    {
        ZzTerminal term(80, 24, ZzBackendKind::Native, 1000);
        const std::string_view vt = "echo zz-downstream-ok\r\n";
        term.feed(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(vt.data()), vt.size()));
        const auto  line = term.renderView().lineAt(0);
        std::string line0;
        for (int c = 0; c < 80; ++c)
            line0 += line.cellAt(c).text;
        if (line0.find("echo zz-downstream-ok") == std::string::npos) {
            std::fprintf(stderr, "FAIL core: line0=[%s]\n", line0.c_str());
            ++failures;
        }
    }

#if defined(ZZ_HAVE_PTY_TARGET)
    // ZzTerm::Pty：spawn cat -> 写 -> 5s 内读回 marker（平台无关路径；
    // Windows 下 cmd 语义差异不归本工程校验，PTY 用例归主仓 test_pty）。
    {
        ZzPtyConfig cfg;
        cfg.argv = {"/bin/cat"};
        auto pty = ZzPty::spawn(cfg);
        if (!pty) {
            std::fprintf(stderr, "FAIL pty: spawn cat\n");
            ++failures;
        } else {
            const std::string_view msg = "zz-pty-ok\n";
            std::string        got;
            std::byte          buf[256];
            pty->writeAll(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(msg.data()), msg.size()));
            for (int i = 0; i < 500 && got.find("zz-pty-ok") == std::string::npos; ++i) {
                const std::ptrdiff_t n = pty->read(buf);
                if (n > 0)
                    got.append(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
            }
            if (got.find("zz-pty-ok") == std::string::npos) {
                std::fprintf(stderr, "FAIL pty: roundtrip got=[%s]\n", got.c_str());
                ++failures;
            }
        }
    }
#endif

    if (failures == 0)
        std::puts("downstream check OK");
    return failures == 0 ? 0 : 1;
}
