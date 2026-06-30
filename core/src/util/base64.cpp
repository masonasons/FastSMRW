#include "fastsm/util/base64.hpp"

#include <array>

namespace fastsm::util {
namespace {
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::array<int, 256> make_reverse() {
    std::array<int, 256> r{};
    r.fill(-1);
    for (int i = 0; i < 64; ++i)
        r[static_cast<unsigned char>(kAlphabet[i])] = i;
    return r;
}
} // namespace

std::string base64_encode(std::string_view data) {
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    const size_t n = data.size();
    while (i + 3 <= n) {
        const unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8) |
                           static_cast<unsigned char>(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
        i += 3;
    }
    if (i + 1 == n) {
        const unsigned v = static_cast<unsigned char>(data[i]) << 16;
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == n) {
        const unsigned v = (static_cast<unsigned char>(data[i]) << 16) |
                           (static_cast<unsigned char>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

std::string base64_decode(std::string_view text) {
    static const std::array<int, 256> rev = make_reverse();
    std::string out;
    out.reserve(text.size() / 4 * 3);
    int buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=' || c == '\r' || c == '\n' || c == ' ')
            continue;
        const int val = rev[static_cast<unsigned char>(c)];
        if (val < 0)
            return {}; // malformed
        buffer = (buffer << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace fastsm::util
