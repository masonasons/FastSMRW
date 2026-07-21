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
        RemoteLocal, // a remote instance's Local timeline (param = instance domain)
        RemoteUser,  // a remote user's posts (param = "user@instance"), fetched abroad
        List,        // a Mastodon list timeline (param = list id); on Bluesky a
                     // curation list feed (param = list at-uri)
        Feed,        // a Bluesky custom feed / feed generator (param = feed at-uri)
        Mutes,       // the account's muted users; rows are users
        Blocks,      // the account's blocked users; rows are users
        FollowRequests, // accounts requesting to follow you; rows are users
        PostUsers,   // the users referenced in one post (author + mentions); rows are
                     // users, seeded from the post (not fetched). param = status id.
        AnalyzedUsers, // result of a User Analysis (e.g. "don't follow you back");
                       // rows are users, seeded at spawn (not fetched). param = the
                       // analysis category id.
        Trends,        // the instance's trending posts (Mastodon /trends/statuses),
                       // ordered by trend score rather than time.
        Conversations, // the account's direct-message conversations (Mastodon
                       // /api/v1/conversations); one row per conversation, newest first.
        FavoritedBy,   // accounts that favorited a post (param = status id); rows are users.
        BoostedBy,     // accounts that boosted a post (param = status id); rows are users.
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
        case Kind::RemoteLocal:
            return title_text.empty() ? (param + " (Local)") : title_text;
        case Kind::RemoteUser:
            return title_text.empty() ? ("@" + param) : title_text;
        case Kind::List:
            return title_text.empty() ? "List" : title_text;
        case Kind::Feed:
            return title_text.empty() ? "Feed" : title_text;
        case Kind::Mutes:
            return "Muted Users";
        case Kind::Blocks:
            return "Blocked Users";
        case Kind::FollowRequests:
            return "Follow Requests";
        case Kind::PostUsers:
            return title_text.empty() ? "Users in post" : title_text;
        case Kind::AnalyzedUsers:
            return title_text.empty() ? "User Analysis" : title_text;
        case Kind::Trends:
            return "Trending Posts";
        case Kind::Conversations:
            return "Conversations";
        case Kind::FavoritedBy:
            return title_text.empty() ? "Favorited by" : title_text;
        case Kind::BoostedBy:
            return title_text.empty() ? "Boosted by" : title_text;
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
        case Kind::RemoteLocal:
            return "remoteLocal:" + param;
        case Kind::RemoteUser:
            return "remoteUser:" + param;
        case Kind::List:
            return "list:" + param;
        case Kind::Feed:
            return "feed:" + param;
        case Kind::Mutes:
            return "mutes";
        case Kind::Blocks:
            return "blocks";
        case Kind::FollowRequests:
            return "followRequests";
        case Kind::PostUsers:
            return "postUsers:" + param;
        case Kind::AnalyzedUsers:
            return "analyzedUsers:" + param;
        case Kind::Trends:
            return "trends";
        case Kind::Conversations:
            return "conversations";
        case Kind::FavoritedBy:
            return "favoritedBy:" + param;
        case Kind::BoostedBy:
            return "boostedBy:" + param;
        }
        return "timeline";
    }

    // Every timeline caches its rows to disk (keyed by account + this cache_key,
    // so they never collide) and reloads instantly on restart. Spawned timelines
    // (threads, author timelines, hashtags, searches, lists, remote feeds, people
    // lists) drop their cache when closed.
    bool is_cacheable() const { return !is_static(); }
    // A synthetic timeline whose rows are provided at spawn (not fetched, refreshed,
    // paged, cached, or restored on restart): the post-users list and the User
    // Analysis results.
    bool is_static() const {
        return kind == Kind::PostUsers || kind == Kind::AnalyzedUsers;
    }
    // Time-ordered feeds re-sort newest-first on merge; threads keep conversation
    // order, user lists keep server order, and searches keep relevance order.
    bool is_time_ordered() const {
        return kind != Kind::Thread && kind != Kind::SearchPosts && kind != Kind::Trends &&
               kind != Kind::Feed && !is_user_list();
    }
    // Rows are users (not statuses), so the UI offers multi-select + batch actions.
    bool is_user_list() const {
        return kind == Kind::Followers || kind == Kind::Following || kind == Kind::SearchPeople ||
               kind == Kind::Mutes || kind == Kind::Blocks || kind == Kind::FollowRequests ||
               kind == Kind::PostUsers || kind == Kind::AnalyzedUsers ||
               kind == Kind::FavoritedBy || kind == Kind::BoostedBy;
    }
    // Mastodon paginates these by item id (max_id), so scrollback can be re-seeded
    // from the oldest loaded row after a cache load.
    //
    // Deliberately NOT listed: Notifications and Mentions. Both page by *notification*
    // id, but their rows carry a group key (Notifications) or the mention's own post
    // (Mentions) — different id spaces entirely, and a status id is orders of magnitude
    // larger than a notification id, so re-seeding from a row would ask the server for
    // the *newest* notifications and then quietly backfill the timeline with ancient
    // ones. Better to leave scrollback unseeded until a real cursor arrives.
    bool paginates_by_item_id() const {
        switch (kind) {
        case Kind::Home:
        case Kind::Local:
        case Kind::Federated:
        case Kind::UserPosts:
        case Kind::Hashtag:
        case Kind::RemoteLocal:
        case Kind::RemoteUser:
        case Kind::List:
            return true;
        default:
            return false;
        }
    }
    bool is_notification_timeline() const {
        return kind == Kind::Notifications || kind == Kind::Mentions;
    }
    // Enter on a row here opens its thread regardless of the general post-Enter
    // setting (the Conversations feed is a list of threads to open into).
    bool enter_opens_thread() const { return kind == Kind::Conversations; }
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
        case Kind::RemoteLocal:
        case Kind::RemoteUser:
        case Kind::List:
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
        case Kind::Mutes:
        case Kind::Blocks:
        case Kind::FollowRequests:
        case Kind::PostUsers:
        case Kind::AnalyzedUsers:
        case Kind::Trends:
        case Kind::Feed:
            return std::nullopt; // not a streaming feed; no new-items chime
        case Kind::Conversations:
            return "messages"; // DM chime when a conversation updates
        case Kind::FavoritedBy:
        case Kind::BoostedBy:
            return std::nullopt; // a seeded user list; not a streaming feed
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
    // A remote instance's Local timeline (instance = bare domain, e.g. "mastodon.social").
    static TimelineSource remote_local(std::string instance, std::string title = {}) {
        return {Kind::RemoteLocal, std::move(instance), std::move(title)};
    }
    // A remote user's posts (handle = "user@instance"), fetched from their home server.
    static TimelineSource remote_user(std::string handle, std::string title = {}) {
        return {Kind::RemoteUser, std::move(handle), std::move(title)};
    }
    // A Mastodon list timeline (id = list id, title = "List: <name>").
    static TimelineSource list(std::string list_id, std::string title = {}) {
        return {Kind::List, std::move(list_id), std::move(title)};
    }
    // A Bluesky custom feed / feed generator (id = feed at-uri).
    static TimelineSource feed(std::string feed_uri, std::string title = {}) {
        return {Kind::Feed, std::move(feed_uri), std::move(title)};
    }
    // The users referenced in one post (author + mentions), seeded at spawn.
    static TimelineSource post_users(std::string status_id, std::string title = {}) {
        return {Kind::PostUsers, std::move(status_id), std::move(title)};
    }
    // The result of a User Analysis (category = which analysis), seeded at spawn.
    static TimelineSource analyzed_users(std::string category, std::string title = {}) {
        return {Kind::AnalyzedUsers, std::move(category), std::move(title)};
    }
    // The instance's trending posts (Mastodon only).
    static TimelineSource trends() { return {Kind::Trends}; }
    // The account's direct-message conversations (Mastodon only).
    static TimelineSource conversations() { return {Kind::Conversations}; }
    // Accounts that favorited / boosted a post (Mastodon; param = status id).
    static TimelineSource favorited_by(std::string status_id, std::string title = {}) {
        return {Kind::FavoritedBy, std::move(status_id), std::move(title)};
    }
    static TimelineSource boosted_by(std::string status_id, std::string title = {}) {
        return {Kind::BoostedBy, std::move(status_id), std::move(title)};
    }
    static TimelineSource mutes() { return {Kind::Mutes}; }
    static TimelineSource blocks() { return {Kind::Blocks}; }
    static TimelineSource follow_requests() { return {Kind::FollowRequests}; }
};

} // namespace fastsm
