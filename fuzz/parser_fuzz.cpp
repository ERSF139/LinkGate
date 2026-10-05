// libFuzzer 目标：任意字节流按任意切块喂给 StreamParser，检查 parser_check 里的全部不变式。
// 构建与运行见 README 的"模糊测试"一节。
#include <cstddef>
#include <cstdint>

#include "parser_check.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) {
        return 0;
    }
    // 第一个字节决定切块大小，让 fuzzer 也能探索"一帧被切在哪里"这个维度。
    const std::size_t chunk = static_cast<std::size_t>(data[0] % 64) + 1;
    lg::test::ParseRun run;
    const char* reason = "";
    if (!lg::test::run_parser_checked(data + 1, size - 1, chunk, &run, &reason)) {
        __builtin_trap();
    }
    return 0;
}
