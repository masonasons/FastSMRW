#pragma once

#include <optional>
#include <string>

namespace fastsm {

// Describes what a timeline shows. M1 implements the standing home feed; the
// other kinds are defined here so the enum is ready for M2 without churn.
struct TimelineSource {
    enum class Kind {
        Home,
        Notifications,
        Mentions,
        Local,
        Federated,
        Bookmarks,
        Favorites,
    };

    Kind kind = Kind::Home;

    std::string title() const {
        switch (kind) {
        case Kind::Home:
            return "Home";
        case Kind::Notifications:
            return "Notifications";
        case Kind::Mentions:
            return "Mentions";
        case Kind::Local:
            return "Local";
        case Kind::Federated:
            return "Federated";
        case Kind::Bookmarks:
            return "Bookmarks";
        case Kind::Favorites:
            return "Favorites";
        }
        return "Timeline";
    }

    // Stable namespace for the on-disk cache.
    std::string cache_key() const {
        switch (kind) {
        case Kind::Home:
            return "home";
        case Kind::Notifications:
            return "notifications";
        case Kind::Mentions:
            return "mentions";
        case Kind::Local:
            return "local";
        case Kind::Federated:
            return "federated";
        case Kind::Bookmarks:
            return "bookmarks";
        case Kind::Favorites:
            return "favourites";
        }
        return "timeline";
    }

    bool is_cacheable() const { return true; }
    bool is_notification_timeline() const {
        return kind == Kind::Notifications || kind == Kind::Mentions;
    }
    // Standing feeds (home/notifications) can't be closed; spawned ones
    // (local/federated/mentions/bookmarks/favorites/...) can (Delete key).
    bool is_dismissable() const {
        return kind != Kind::Home && kind != Kind::Notifications;
    }

    // Soundpack base name chimed when this timeline receives new posts on
    // refresh (matches the Mac TimelineSource.newItemsSoundName). nullopt = no
    // chime.
    std::optional<std::string> new_items_sound_name() const {
        switch (kind) {
        case Kind::Home:
        case Kind::Local:
        case Kind::Federated:
            return "home";
        case Kind::Notifications:
            return "notification";
        case Kind::Mentions:
            return "mentions";
        case Kind::Bookmarks:
        case Kind::Favorites:
            return std::nullopt; // not a streaming feed; no new-items chime
        }
        return std::nullopt;
    }
    bool operator==(const TimelineSource& o) const { return kind == o.kind; }

    static TimelineSource home() { return {Kind::Home}; }
    static TimelineSource notifications() { return {Kind::Notifications}; }
    static TimelineSource mentions() { return {Kind::Mentions}; }
    static TimelineSource local() { return {Kind::Local}; }
    static TimelineSource federated() { return {Kind::Federated}; }
    static TimelineSource bookmarks() { return {Kind::Bookmarks}; }
    static TimelineSource favorites() { return {Kind::Favorites}; }
};

} // namespace fastsm
