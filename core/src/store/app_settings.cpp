#include "fastsm/store/app_settings.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

#include "fastsm/store/paths.hpp"
#include "settings_serde.hpp"

using nlohmann::json;
using namespace fastsm::present;

namespace fastsm::store {
namespace {

template <class Field>
json items_to_json(const std::vector<SpeechItem<Field>>& items) {
    json arr = json::array();
    for (const auto& item : items)
        arr.push_back({{"field", field_key(item.field)}, {"enabled", item.enabled}});
    return arr;
}

template <class Field, class FromKey>
std::vector<SpeechItem<Field>> items_from_json(const json& arr, FromKey from_key) {
    std::vector<SpeechItem<Field>> out;
    if (!arr.is_array())
        return out;
    for (const auto& e : arr) {
        Field f{};
        if (from_key(e.value("field", std::string{}), f))
            out.push_back({f, e.value("enabled", true)});
    }
    return out;
}

} // namespace

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

SettingsStore SettingsStore::default_store() {
    return SettingsStore(config_dir() / "settings.json");
}

AppSettings settings_from_json(const json& root) {
    AppSettings settings;
    settings.sounds_enabled = root.value("sounds_enabled", true);
    settings.enter_to_send = root.value("enter_to_send", false);
    settings.soundpack = root.value("soundpack", std::string("Default"));
    settings.fetch_pages = root.value("fetch_pages", 3);
    settings.cache_limit = root.value("cache_limit", 200);
    settings.confirm_boost = root.value("confirm_boost", false);
    settings.confirm_favorite = root.value("confirm_favorite", false);
    settings.confirm_clear_timeline = root.value("confirm_clear_timeline", true);

    SpeechSettings speech;
    if (auto it = root.find("speech"); it != root.end() && it->is_object()) {
        if (auto s = it->find("status"); s != it->end())
            speech.status = items_from_json<StatusSpeechField>(*s, status_field_from_key);
        if (auto u = it->find("user"); u != it->end())
            speech.user = items_from_json<UserSpeechField>(*u, user_field_from_key);
    }
    settings.speech = speech.normalized();
    return settings;
}

json settings_to_json(const AppSettings& settings) {
    json root;
    root["sounds_enabled"] = settings.sounds_enabled;
    root["enter_to_send"] = settings.enter_to_send;
    root["soundpack"] = settings.soundpack;
    root["fetch_pages"] = settings.fetch_pages;
    root["cache_limit"] = settings.cache_limit;
    root["confirm_boost"] = settings.confirm_boost;
    root["confirm_favorite"] = settings.confirm_favorite;
    root["confirm_clear_timeline"] = settings.confirm_clear_timeline;
    root["speech"]["status"] = items_to_json(settings.speech.status);
    root["speech"]["user"] = items_to_json(settings.speech.user);
    return root;
}

// Legacy reader: kept so an old settings.json (pre-unification) is still picked
// up by AppConfigStore. The app no longer writes this file.
AppSettings SettingsStore::load() const {
    std::ifstream in(path_, std::ios::binary);
    if (!in)
        return {}; // defaults
    json root;
    try {
        in >> root;
    } catch (...) {
        return {};
    }
    return settings_from_json(root);
}

bool SettingsStore::save(const AppSettings& settings) const {
    try {
        const std::filesystem::path tmp = path_.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out << settings_to_json(settings).dump(2);
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path_, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace fastsm::store
