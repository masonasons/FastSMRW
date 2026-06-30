#include "fastsm/store/app_settings.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <system_error>

#include "fastsm/store/paths.hpp"

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

AppSettings SettingsStore::load() const {
    AppSettings settings;
    std::ifstream in(path_, std::ios::binary);
    if (!in)
        return settings; // defaults
    json root;
    try {
        in >> root;
    } catch (...) {
        return settings;
    }

    settings.sounds_enabled = root.value("sounds_enabled", true);
    settings.soundpack = root.value("soundpack", std::string("Default"));

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

bool SettingsStore::save(const AppSettings& settings) const {
    json root;
    root["sounds_enabled"] = settings.sounds_enabled;
    root["soundpack"] = settings.soundpack;
    root["speech"]["status"] = items_to_json(settings.speech.status);
    root["speech"]["user"] = items_to_json(settings.speech.user);

    try {
        const std::filesystem::path tmp = path_.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out)
                return false;
            out << root.dump(2);
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
