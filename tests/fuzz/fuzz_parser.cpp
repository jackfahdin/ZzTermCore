// M7a 占位 harness（T1）：验证 ZZTERM_FUZZ 编译链；T2/T3 实装替换。
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t)
{
    return 0;
}
