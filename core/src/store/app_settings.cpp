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
    for (const auto& item : items) {
        json j = {{"field", field_key(item.field)}, {"enabled", item.enabled}};
        if (!item.before.empty())
            j["before"] = item.before;
        if (!item.after.empty())
            j["after"] = item.after;
        if (item.no_separator_after)
            j["no_separator_after"] = true;
        arr.push_back(std::move(j));
    }
    return arr;
}

template <class Field, class FromKey>
std::vector<SpeechItem<Field>> items_from_json(const json& arr, FromKey from_key) {
    std::vector<SpeechItem<Field>> out;
    if (!arr.is_array())
        return out;
    for (const auto& e : arr) {
        Field f{};
        if (from_key(e.value("field", std::string{}), f)) {
            SpeechItem<Field> item(f, e.value("enabled", true));
            item.before = e.value("before", std::string{});
            item.after = e.value("after", std::string{});
            item.no_separator_after = e.value("no_separator_after", false);
            out.push_back(std::move(item));
        }
    }
    return out;
}

} // namespace

AppSettings settings_from_json(const json& root) {
    AppSettings settings;
    settings.sounds_enabled = root.value("sounds_enabled", true);
    settings.sound_volume = root.value("sound_volume", 100);
    settings.boundary_sound = root.value("boundary_sound", true);
    settings.earcon_image = root.value("earcon_image", true);
    settings.earcon_media = root.value("earcon_media", true);
    settings.earcon_mention = root.value("earcon_mention", true);
    settings.earcon_pinned = root.value("earcon_pinned", true);
    settings.earcon_poll = root.value("earcon_poll", true);
    settings.enter_to_send = root.value("enter_to_send", false);
    settings.soundpack = root.value("soundpack", std::string("Default"));
    if (auto it = root.find("account_soundpacks"); it != root.end() && it->is_object())
        for (const auto& [key, pack] : it->items())
            if (pack.is_string())
                settings.account_soundpacks[key] = pack.get<std::string>();
    settings.fetch_pages = root.value("fetch_pages", 3);
    settings.cache_limit = root.value("cache_limit", 200);
    settings.confirm_boost = root.value("confirm_boost", false);
    settings.confirm_unboost = root.value("confirm_unboost", false);
    settings.confirm_favorite = root.value("confirm_favorite", false);
    settings.confirm_unfavorite = root.value("confirm_unfavorite", false);
    settings.confirm_clear_timeline = root.value("confirm_clear_timeline", true);
    settings.confirm_block = root.value("confirm_block", true);
    settings.confirm_unblock = root.value("confirm_unblock", false);
    settings.confirm_delete_post = root.value("confirm_delete_post", true);
    settings.auto_refresh_seconds = root.value("auto_refresh_seconds", 60);
    settings.streaming_enabled = root.value("streaming_enabled", true);
    settings.show_mentions_in_notifications = root.value("show_mentions_in_notifications", true);
    settings.reverse_timelines = root.value("reverse_timelines", false);
    settings.auto_load_older = root.value("auto_load_older", true);
    settings.enter_post_action = root.value("enter_post_action", std::string("post_info"));
    settings.enter_user_action = root.value("enter_user_action", std::string("actions"));
    settings.secondary_post_action =
        root.value("secondary_post_action", std::string("play_media"));
    settings.media_background = root.value("media_background", false);
    settings.reply_mentions_at_end = root.value("reply_mentions_at_end", false);
    settings.invisible_mode = root.value("invisible_mode", std::string("off"));
    settings.invisible_keymap = root.value("invisible_keymap", std::string("default"));
    settings.invisible_layer_key =
        root.value("invisible_layer_key", std::string("control+win+space"));
    settings.invisible_repeat_at_edge = root.value("invisible_repeat_at_edge", true);
    settings.window_shown = root.value("window_shown", true);
    settings.update_branch = root.value("update_branch", std::string("stable"));
    settings.check_updates_on_startup = root.value("check_updates_on_startup", true);

    if (CwMode cw; cw_mode_from_key(root.value("cw_mode", std::string("hide")), cw))
        settings.text.cw = cw;
    if (EmojiRemoval e; emoji_removal_from_key(root.value("post_emoji_removal", std::string("none")), e))
        settings.text.post_emoji = e;
    if (EmojiRemoval e; emoji_removal_from_key(root.value("name_emoji_removal", std::string("none")), e))
        settings.text.name_emoji = e;
    settings.text.max_mentions = root.value("max_usernames_in_post", 0);
    settings.text.absolute_time = root.value("absolute_time", false);

    SpeechSettings speech;
    if (auto it = root.find("speech"); it != root.end() && it->is_object()) {
        if (auto s = it->find("status"); s != it->end())
            speech.status = items_from_json<StatusSpeechField>(*s, status_field_from_key);
        if (auto u = it->find("user"); u != it->end())
            speech.user = items_from_json<UserSpeechField>(*u, user_field_from_key);
        if (auto nf = it->find("notification"); nf != it->end())
            speech.notification =
                items_from_json<NotificationSpeechField>(*nf, notification_field_from_key);
        if (auto a = it->find("autoread"); a != it->end())
            speech.autoread = items_from_json<StatusSpeechField>(*a, status_field_from_key);
        if (auto c = it->find("copy_status"); c != it->end())
            speech.copy_status = items_from_json<StatusSpeechField>(*c, status_field_from_key);
        if (auto c = it->find("copy_user"); c != it->end())
            speech.copy_user = items_from_json<UserSpeechField>(*c, user_field_from_key);
        if (auto c = it->find("copy_notification"); c != it->end())
            speech.copy_notification =
                items_from_json<NotificationSpeechField>(*c, notification_field_from_key);
        speech.separator = it->value("separator", std::string(", "));
    }
    settings.speech = speech.normalized();
    return settings;
}

json settings_to_json(const AppSettings& settings) {
    json root;
    root["sounds_enabled"] = settings.sounds_enabled;
    root["sound_volume"] = settings.sound_volume;
    root["boundary_sound"] = settings.boundary_sound;
    root["earcon_image"] = settings.earcon_image;
    root["earcon_media"] = settings.earcon_media;
    root["earcon_mention"] = settings.earcon_mention;
    root["earcon_pinned"] = settings.earcon_pinned;
    root["earcon_poll"] = settings.earcon_poll;
    root["enter_to_send"] = settings.enter_to_send;
    root["soundpack"] = settings.soundpack;
    root["account_soundpacks"] = json::object();
    for (const auto& [key, pack] : settings.account_soundpacks)
        root["account_soundpacks"][key] = pack;
    root["fetch_pages"] = settings.fetch_pages;
    root["cache_limit"] = settings.cache_limit;
    root["confirm_boost"] = settings.confirm_boost;
    root["confirm_unboost"] = settings.confirm_unboost;
    root["confirm_favorite"] = settings.confirm_favorite;
    root["confirm_unfavorite"] = settings.confirm_unfavorite;
    root["confirm_clear_timeline"] = settings.confirm_clear_timeline;
    root["confirm_block"] = settings.confirm_block;
    root["confirm_unblock"] = settings.confirm_unblock;
    root["confirm_delete_post"] = settings.confirm_delete_post;
    root["auto_refresh_seconds"] = settings.auto_refresh_seconds;
    root["streaming_enabled"] = settings.streaming_enabled;
    root["show_mentions_in_notifications"] = settings.show_mentions_in_notifications;
    root["reverse_timelines"] = settings.reverse_timelines;
    root["auto_load_older"] = settings.auto_load_older;
    root["enter_post_action"] = settings.enter_post_action;
    root["enter_user_action"] = settings.enter_user_action;
    root["secondary_post_action"] = settings.secondary_post_action;
    root["media_background"] = settings.media_background;
    root["reply_mentions_at_end"] = settings.reply_mentions_at_end;
    root["invisible_mode"] = settings.invisible_mode;
    root["invisible_keymap"] = settings.invisible_keymap;
    root["invisible_layer_key"] = settings.invisible_layer_key;
    root["invisible_repeat_at_edge"] = settings.invisible_repeat_at_edge;
    root["window_shown"] = settings.window_shown;
    root["update_branch"] = settings.update_branch;
    root["check_updates_on_startup"] = settings.check_updates_on_startup;
    root["cw_mode"] = cw_mode_key(settings.text.cw);
    root["post_emoji_removal"] = emoji_removal_key(settings.text.post_emoji);
    root["name_emoji_removal"] = emoji_removal_key(settings.text.name_emoji);
    root["max_usernames_in_post"] = settings.text.max_mentions;
    root["absolute_time"] = settings.text.absolute_time;
    root["speech"]["status"] = items_to_json(settings.speech.status);
    root["speech"]["user"] = items_to_json(settings.speech.user);
    root["speech"]["notification"] = items_to_json(settings.speech.notification);
    root["speech"]["autoread"] = items_to_json(settings.speech.autoread);
    root["speech"]["copy_status"] = items_to_json(settings.speech.copy_status);
    root["speech"]["copy_user"] = items_to_json(settings.speech.copy_user);
    root["speech"]["copy_notification"] = items_to_json(settings.speech.copy_notification);
    root["speech"]["separator"] = settings.speech.separator;
    return root;
}

} // namespace fastsm::store
