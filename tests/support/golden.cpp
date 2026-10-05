#include "golden.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace lg::test {

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    throw std::runtime_error(std::string("invalid hex digit: ") + c);
}

}  // namespace

std::vector<std::uint8_t> golden_frame(const std::string& name) {
    std::ifstream file(LG_GOLDEN_FILE);
    if (!file) {
        throw std::runtime_error("cannot open " LG_GOLDEN_FILE);
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string case_name;
        std::string hex;
        fields >> case_name >> hex;
        if (case_name != name) continue;
        if (hex.size() % 2 != 0) throw std::runtime_error("odd hex length in case " + name);
        std::vector<std::uint8_t> bytes;
        bytes.reserve(hex.size() / 2);
        for (std::size_t i = 0; i < hex.size(); i += 2) {
            bytes.push_back(static_cast<std::uint8_t>(hex_digit(hex[i]) * 16 + hex_digit(hex[i + 1])));
        }
        return bytes;
    }
    throw std::runtime_error("golden case not found: " + name);
}

}  // namespace lg::test
