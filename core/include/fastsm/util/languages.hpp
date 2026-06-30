#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fastsm::util {

// (ISO code, English name) pairs for the compose language popup.
const std::vector<std::pair<std::string, std::string>>& languages();

} // namespace fastsm::util
