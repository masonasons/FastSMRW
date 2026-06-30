#include "fastsm/presentation/speech_settings.hpp"

#include <unordered_set>

namespace fastsm::present {

const char* field_key(StatusSpeechField f) {
    switch (f) {
    case StatusSpeechField::BoostedBy:
        return "boostedBy";
    case StatusSpeechField::Author:
        return "author";
    case StatusSpeechField::Handle:
        return "handle";
    case StatusSpeechField::ContentWarning:
        return "contentWarning";
    case StatusSpeechField::Text:
        return "text";
    case StatusSpeechField::Quote:
        return "quote";
    case StatusSpeechField::Media:
        return "media";
    case StatusSpeechField::Poll:
        return "poll";
    case StatusSpeechField::Time:
        return "time";
    case StatusSpeechField::Stats:
        return "stats";
    case StatusSpeechField::Favorited:
        return "favorited";
    case StatusSpeechField::Boosted:
        return "boosted";
    case StatusSpeechField::Visibility:
        return "visibility";
    case StatusSpeechField::ReplyIndicator:
        return "replyIndicator";
    case StatusSpeechField::Source:
        return "source";
    }
    return "";
}

const char* field_display_name(StatusSpeechField f) {
    switch (f) {
    case StatusSpeechField::BoostedBy:
        return "Boosted by";
    case StatusSpeechField::Author:
        return "Author name";
    case StatusSpeechField::Handle:
        return "Handle (@user)";
    case StatusSpeechField::ContentWarning:
        return "Content warning";
    case StatusSpeechField::Text:
        return "Post text";
    case StatusSpeechField::Quote:
        return "Quoted post";
    case StatusSpeechField::Media:
        return "Media / attachments";
    case StatusSpeechField::Poll:
        return "Poll";
    case StatusSpeechField::Time:
        return "Time";
    case StatusSpeechField::Stats:
        return "Reply / boost / favorite counts";
    case StatusSpeechField::Favorited:
        return "Favorited state";
    case StatusSpeechField::Boosted:
        return "Boosted state";
    case StatusSpeechField::Visibility:
        return "Visibility";
    case StatusSpeechField::ReplyIndicator:
        return "Reply indicator";
    case StatusSpeechField::Source:
        return "Posting app / source";
    }
    return "";
}

bool status_field_from_key(std::string_view key, StatusSpeechField& out) {
    for (int i = 0; i <= static_cast<int>(StatusSpeechField::Source); ++i) {
        const auto f = static_cast<StatusSpeechField>(i);
        if (key == field_key(f)) {
            out = f;
            return true;
        }
    }
    return false;
}

const char* field_key(UserSpeechField f) {
    switch (f) {
    case UserSpeechField::Author:
        return "author";
    case UserSpeechField::Handle:
        return "handle";
    case UserSpeechField::Bot:
        return "bot";
    case UserSpeechField::Locked:
        return "locked";
    case UserSpeechField::Bio:
        return "bio";
    case UserSpeechField::Followers:
        return "followers";
    case UserSpeechField::Following:
        return "following";
    case UserSpeechField::Posts:
        return "posts";
    }
    return "";
}

const char* field_display_name(UserSpeechField f) {
    switch (f) {
    case UserSpeechField::Author:
        return "Display name";
    case UserSpeechField::Handle:
        return "Handle (@user)";
    case UserSpeechField::Bot:
        return "Bot indicator";
    case UserSpeechField::Locked:
        return "Locked indicator";
    case UserSpeechField::Bio:
        return "Bio";
    case UserSpeechField::Followers:
        return "Followers count";
    case UserSpeechField::Following:
        return "Following count";
    case UserSpeechField::Posts:
        return "Posts count";
    }
    return "";
}

bool user_field_from_key(std::string_view key, UserSpeechField& out) {
    for (int i = 0; i <= static_cast<int>(UserSpeechField::Posts); ++i) {
        const auto f = static_cast<UserSpeechField>(i);
        if (key == field_key(f)) {
            out = f;
            return true;
        }
    }
    return false;
}

namespace {
template <class Field>
std::vector<SpeechItem<Field>> merge(const std::vector<SpeechItem<Field>>& items,
                                     const std::vector<SpeechItem<Field>>& defaults) {
    std::vector<SpeechItem<Field>> result;
    std::unordered_set<int> seen;
    for (const auto& item : items) {
        if (seen.insert(static_cast<int>(item.field)).second)
            result.push_back(item);
    }
    for (const auto& item : defaults) {
        if (seen.insert(static_cast<int>(item.field)).second)
            result.push_back(item);
    }
    return result;
}
} // namespace

SpeechSettings SpeechSettings::defaults() {
    using S = StatusSpeechField;
    using U = UserSpeechField;
    SpeechSettings s;
    s.status = {
        {S::BoostedBy}, {S::Author},     {S::Handle, false}, {S::ContentWarning},
        {S::Text},      {S::Quote},      {S::Media},         {S::Poll},
        {S::ReplyIndicator, false},      {S::Time},          {S::Stats},
        {S::Favorited}, {S::Boosted},    {S::Visibility, false}, {S::Source, false},
    };
    s.user = {
        {U::Author}, {U::Handle},  {U::Bot},            {U::Locked},
        {U::Bio},    {U::Followers}, {U::Following, false}, {U::Posts, false},
    };
    return s;
}

SpeechSettings SpeechSettings::normalized() const {
    SpeechSettings def = defaults();
    SpeechSettings out;
    out.status = merge(status, def.status);
    out.user = merge(user, def.user);
    return out;
}

SpeechSettings SpeechConfig::current_ = SpeechSettings::defaults();

const SpeechSettings& SpeechConfig::current() { return current_; }
void SpeechConfig::set_current(SpeechSettings settings) { current_ = std::move(settings); }

} // namespace fastsm::present
