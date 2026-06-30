#pragma once

#include <string>
#include <string_view>
#include <vector>

// User-configurable ordering + on/off for the pieces of a post (and a user row)
// that the screen reader reads — the Swift analogue is SpeechSettings.swift.
// All composition lives in the core; the front end only edits this config and
// renders what StatusPresenter/UserPresenter produce.
namespace fastsm::present {

enum class StatusSpeechField {
    BoostedBy,
    Author,
    Handle,
    ContentWarning,
    Text,
    Quote,
    Media,
    Poll,
    Time,
    Stats,
    Favorited,
    Boosted,
    Visibility,
    ReplyIndicator,
    Source,
};

enum class UserSpeechField {
    Author,
    Handle,
    Bot,
    Locked,
    Bio,
    Followers,
    Following,
    Posts,
};

enum class NotificationSpeechField {
    Actor,  // who acted (display name)
    Action, // what they did ("followed you", "boosted your post", ...)
    Handle, // @user
    Text,   // the related post's text
    Time,
};

// Stable string id (for settings.json) and human label (for the settings UI).
const char* field_key(StatusSpeechField f);
const char* field_display_name(StatusSpeechField f);
bool status_field_from_key(std::string_view key, StatusSpeechField& out);

const char* field_key(UserSpeechField f);
const char* field_display_name(UserSpeechField f);
bool user_field_from_key(std::string_view key, UserSpeechField& out);

const char* field_key(NotificationSpeechField f);
const char* field_display_name(NotificationSpeechField f);
bool notification_field_from_key(std::string_view key, NotificationSpeechField& out);

// One orderable, toggleable field.
template <class Field> struct SpeechItem {
    Field field{};
    bool enabled = true;

    SpeechItem() = default;
    SpeechItem(Field f, bool e = true) : field(f), enabled(e) {}

    bool operator==(const SpeechItem& o) const { return field == o.field && enabled == o.enabled; }
    bool operator!=(const SpeechItem& o) const { return !(*this == o); }
};

struct SpeechSettings {
    std::vector<SpeechItem<StatusSpeechField>> status;
    std::vector<SpeechItem<UserSpeechField>> user;
    std::vector<SpeechItem<NotificationSpeechField>> notification;

    // The Mac default order/visibility.
    static SpeechSettings defaults();

    // Guarantee every field appears exactly once: keep saved order, drop
    // dupes/unknowns, append fields added in a newer version (using the
    // default's enabled state) so old settings files keep working.
    SpeechSettings normalized() const;

    bool operator==(const SpeechSettings& o) const {
        return status == o.status && user == o.user && notification == o.notification;
    }
};

// The current speech configuration, read by the presenters. Set by the settings
// store on load/change. UI-thread read-mostly.
class SpeechConfig {
public:
    static const SpeechSettings& current();
    static void set_current(SpeechSettings settings);

private:
    static SpeechSettings current_;
};

} // namespace fastsm::present
