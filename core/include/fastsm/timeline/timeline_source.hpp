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
        Thread,    // a post's conversation (param = focused status id)
        UserPosts, // an author's posts (param = account id)
        Followers, // an account's followers (param = account id); rows are users
        Following, // who an account follows (param = account id); rows are users
        Hashtag,     // posts tagged #param
        SearchPosts, // full-text post search for param
        SearchPeople, // people search for param; rows are users
    };

    Kind kind = Kind::Home;
    std::string param;      // parameter for parameterized kinds (Thread/UserPosts: id)
    std::string title_text; // display title for parameterized kinds (e.g. "Thread: @x")

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
        case Kind::Thread:
            return title_text.empty() ? "Thread" : title_text;
        case Kind::UserPosts:
            return title_text.empty() ? "User" : title_text;
        case Kind::Followers:
            return title_text.empty() ? "Followers" : title_text;
        case Kind::Following:
            return title_text.empty() ? "Following" : title_text;
        case Kind::Hashtag:
            return title_text.empty() ? ("#" + param) : title_text;
        case Kind::SearchPosts:
            return title_text.empty() ? ("Search: " + param) : title_text;
        case Kind::SearchPeople:
            return title_text.empty() ? ("People: " + param) : title_text;
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
        case Kind::Thread:
            return "thread:" + param;
        case Kind::UserPosts:
            return "userPosts:" + param;
        case Kind::Followers:
            return "followers:" + param;
        case Kind::Following:
            return "following:" + param;
        case Kind::Hashtag:
            return "hashtag:" + param;
        case Kind::SearchPosts:
            return "search:posts:" + param;
        case Kind::SearchPeople:
            return "search:people:" + param;
        }
        return "timeline";
    }

    // Standing feeds are cached for instant startup; spawned/ephemeral ones
    // (threads, author timelines, user lists, hashtags, searches) are not.
    bool is_cacheable() const {
        switch (kind) {
        case Kind::Thread:
        case Kind::UserPosts:
        case Kind::Followers:
        case Kind::Following:
        case Kind::Hashtag:
        case Kind::SearchPosts:
        case Kind::SearchPeople:
            return false;
        default:
            return true;
        }
    }
    // Time-ordered feeds re-sort newest-first on merge; threads keep conversation
    // order, user lists keep server order, and searches keep relevance order.
    bool is_time_ordered() const {
        return kind != Kind::Thread && kind != Kind::SearchPosts && !is_user_list();
    }
    // Rows are users (not statuses), so the UI offers multi-select + batch actions.
    bool is_user_list() const {
        return kind == Kind::Followers || kind == Kind::Following || kind == Kind::SearchPeople;
    }
    // Mastodon paginates these by item id (max_id), so scrollback can be re-seeded
    // from the oldest loaded row after a cache load.
    bool paginates_by_item_id() const {
        switch (kind) {
        case Kind::Home:
        case Kind::Local:
        case Kind::Federated:
        case Kind::Notifications:
        case Kind::Mentions:
        case Kind::UserPosts:
        case Kind::Hashtag:
            return true;
        default:
            return false;
        }
    }
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
        case Kind::Hashtag:
            return "home";
        case Kind::Notifications:
            return "notification";
        case Kind::Mentions:
            return "mentions";
        case Kind::Bookmarks:
        case Kind::Favorites:
        case Kind::Thread:
        case Kind::UserPosts:
        case Kind::Followers:
        case Kind::Following:
        case Kind::SearchPosts:
        case Kind::SearchPeople:
            return std::nullopt; // not a streaming feed; no new-items chime
        }
        return std::nullopt;
    }
    bool operator==(const TimelineSource& o) const { return kind == o.kind && param == o.param; }

    static TimelineSource home() { return {Kind::Home}; }
    static TimelineSource notifications() { return {Kind::Notifications}; }
    static TimelineSource mentions() { return {Kind::Mentions}; }
    static TimelineSource local() { return {Kind::Local}; }
    static TimelineSource federated() { return {Kind::Federated}; }
    static TimelineSource bookmarks() { return {Kind::Bookmarks}; }
    static TimelineSource favorites() { return {Kind::Favorites}; }
    static TimelineSource thread(std::string status_id, std::string title = {}) {
        return {Kind::Thread, std::move(status_id), std::move(title)};
    }
    static TimelineSource user_posts(std::string account_id, std::string title = {}) {
        return {Kind::UserPosts, std::move(account_id), std::move(title)};
    }
    static TimelineSource followers(std::string account_id, std::string title = {}) {
        return {Kind::Followers, std::move(account_id), std::move(title)};
    }
    static TimelineSource following(std::string account_id, std::string title = {}) {
        return {Kind::Following, std::move(account_id), std::move(title)};
    }
    static TimelineSource hashtag(std::string tag) { return {Kind::Hashtag, std::move(tag)}; }
    static TimelineSource search_posts(std::string query) {
        return {Kind::SearchPosts, std::move(query)};
    }
    static TimelineSource search_people(std::string query) {
        return {Kind::SearchPeople, std::move(query)};
    }
};

} // namespace fastsm
