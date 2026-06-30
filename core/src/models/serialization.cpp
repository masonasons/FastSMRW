#include "fastsm/models/serialization.hpp"

#include <cstdint>

using nlohmann::json;

// Only User is JSON-serialized (it's embedded in the accounts config.json).
// All timeline rows use the compact binary timeline_codec, never JSON.
namespace fastsm {

void to_json(json& j, const User& v) {
    j = json{{"id", v.id},
             {"acct", v.acct},
             {"username", v.username},
             {"display_name", v.display_name},
             {"note", v.note},
             {"avatar_url", v.avatar_url},
             {"header_url", v.header_url},
             {"url", v.url},
             {"followers_count", v.followers_count},
             {"following_count", v.following_count},
             {"statuses_count", v.statuses_count},
             {"created_at", v.created_at},
             {"bot", v.bot},
             {"locked", v.locked},
             {"platform", static_cast<int>(v.platform)}};
}

void from_json(const json& j, User& v) {
    v.id = j.value("id", "");
    v.acct = j.value("acct", "");
    v.username = j.value("username", "");
    v.display_name = j.value("display_name", "");
    v.note = j.value("note", "");
    v.avatar_url = j.value("avatar_url", "");
    v.header_url = j.value("header_url", "");
    v.url = j.value("url", "");
    v.followers_count = j.value("followers_count", 0);
    v.following_count = j.value("following_count", 0);
    v.statuses_count = j.value("statuses_count", 0);
    v.created_at = j.value("created_at", std::int64_t{0});
    v.bot = j.value("bot", false);
    v.locked = j.value("locked", false);
    v.platform = static_cast<Platform>(j.value("platform", 0));
}

} // namespace fastsm
