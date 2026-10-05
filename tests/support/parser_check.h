#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "devproto/frame.h"
#include "devproto/parser.h"

namespace lg::test {

// 把 input 按 chunk 字节一段段喂给 StreamParser，模拟 TCP 的任意切分。
// 同时被单元测试和 libFuzzer 目标使用。违反不变式时返回 false，并在 *reason 写明哪一条。
struct ParseRun {
    std::vector<std::vector<std::uint8_t>> frames;
    devproto::ParserStats stats;
    std::size_t leftover = 0;
};

bool run_parser_checked(const std::uint8_t* input, std::size_t size, std::size_t chunk, ParseRun* run,
                        const char** reason);

}  // namespace lg::test
