#pragma once

// JSON (de)serialization for User only — used by the accounts config (config.json).
// The timeline cache does NOT use JSON; it uses the compact binary timeline_codec.
// Declared here so nlohmann/json's ADL finds them where User is serialized.

#include <nlohmann/json.hpp>

#include "fastsm/models/user.hpp"

namespace fastsm {

void to_json(nlohmann::json& j, const User& v);
void from_json(const nlohmann::json& j, User& v);

} // namespace fastsm
