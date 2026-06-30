#pragma once

#include <string>
#include <string_view>

namespace fastsm::util {

std::string base64_encode(std::string_view data);
std::string base64_decode(std::string_view text); // returns "" on malformed input

} // namespace fastsm::util
