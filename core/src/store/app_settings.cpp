#include "fastsm/store/app_settings.hpp"

#include <nlohmann/json.hpp>

#include "fastsm/store/settings_json.hpp"

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
    settings.confirm_block = root.value("confirm_block", true);
    settings.auto_refresh_seconds = root.value("auto_refresh_seconds", 0);
    settings.streaming_enabled = root.value("streaming_enabled", false);
    settings.show_mentions_in_notifications = root.value("show_mentions_in_notifications", true);
    settings.invisible_mode = root.value("invisible_mode", std::string("off"));
    settings.invisible_keymap = root.value("invisible_keymap", std::string("default"));
    settings.invisible_layer_key =
        root.value("invisible_layer_key", std::string("control+win+space"));
    settings.window_shown = root.value("window_shown", true);

    if (CwMode cw; cw_mode_from_key(root.value("cw_mode", std::string("hide")), cw))
        settings.text.cw = cw;
    if (EmojiRemoval e; emoji_removal_from_key(root.value("post_emoji_removal", std::string("none")), e))
        settings.text.post_emoji = e;
    if (EmojiRemoval e; emoji_removal_from_key(root.value("name_emoji_removal", std::string("none")), e))
        settings.text.name_emoji = e;
    settings.text.max_mentions = root.value("max_usernames_in_post", 0);

    SpeechSettings speech;
    if (auto it = root.find("speech"); it != root.end() && it->is_object()) {
        if (auto s = it->find("status"); s != it->end())
            speech.status = items_from_json<StatusSpeechField>(*s, status_field_from_key);
        if (auto u = it->find("user"); u != it->end())
            speech.user = items_from_json<UserSpeechField>(*u, user_field_from_key);
        if (auto nf = it->find("notification"); nf != it->end())
            speech.notification =
                items_from_json<NotificationSpeechField>(*nf, notification_field_from_key);
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
    root["confirm_block"] = settings.confirm_block;
    root["auto_refresh_seconds"] = settings.auto_refresh_seconds;
    root["streaming_enabled"] = settings.streaming_enabled;
    root["show_mentions_in_notifications"] = settings.show_mentions_in_notifications;
    root["invisible_mode"] = settings.invisible_mode;
    root["invisible_keymap"] = settings.invisible_keymap;
    root["invisible_layer_key"] = settings.invisible_layer_key;
    root["window_shown"] = settings.window_shown;
    root["cw_mode"] = cw_mode_key(settings.text.cw);
    root["post_emoji_removal"] = emoji_removal_key(settings.text.post_emoji);
    root["name_emoji_removal"] = emoji_removal_key(settings.text.name_emoji);
    root["max_usernames_in_post"] = settings.text.max_mentions;
    root["speech"]["status"] = items_to_json(settings.speech.status);
    root["speech"]["user"] = items_to_json(settings.speech.user);
    root["speech"]["notification"] = items_to_json(settings.speech.notification);
    return root;
}

} // namespace fastsm::store
