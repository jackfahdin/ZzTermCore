// ZzContourBackend（M1a）headless 回归测试。
// 仅 ZZTERM_WITH_CONTOUR=ON 时构建（见 tests/CMakeLists.txt 条件注册）。
#include "ZzContourPtyBridge.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

int g_failures = 0;

#define ZZ_CHECK(cond)                                                                              \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            ++g_failures;                                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
        }                                                                                           \
    } while (0)

// 桥：write 转发回调、read 恒无数据、pageSize 记录、close 状态。
void testBridge()
{
    std::string written;
    auto const initialSize = vtpty::PageSize { vtpty::LineCount(24), vtpty::ColumnCount(80) };
    ZzContourPtyBridge bridge(initialSize,
                              [&written](std::string_view data) { written.append(data); });

    ZZ_CHECK(!bridge.isClosed());
    ZZ_CHECK(bridge.write("hello") == 5);
    ZZ_CHECK(written == "hello");
    ZZ_CHECK(bridge.pageSize().columns.value == 80);
    ZZ_CHECK(bridge.pageSize().lines.value == 24);

    bridge.resizeScreen(vtpty::PageSize { vtpty::LineCount(30), vtpty::ColumnCount(100) },
                        std::nullopt);
    ZZ_CHECK(bridge.pageSize().columns.value == 100);
    ZZ_CHECK(bridge.pageSize().lines.value == 30);

    ZZ_CHECK(bridge.start().has_value());
    (void) bridge.slave();
    bridge.wakeupReader();
    bridge.close();
    ZZ_CHECK(bridge.isClosed());
    bridge.waitForClosed();
}

} // namespace

int main()
{
    testBridge();
    if (g_failures != 0)
        std::fprintf(stderr, "test_contour_backend: %d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
