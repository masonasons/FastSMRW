#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "fastsm/models/notification.hpp"
#include "fastsm/models/status.hpp"
#include "fastsm/models/user.hpp"

namespace fastsm {

// A single row in a timeline: a post, a notification, or a user.
struct TimelineItem {
    std::variant<Status, Notification, User> value;

    bool is_status() const { return std::holds_alternative<Status>(value); }
    bool is_notification() const { return std::holds_alternative<Notification>(value); }
    bool is_user() const { return std::holds_alternative<User>(value); }

    // Stable id, prefixed by kind so rows of different kinds never collide.
    //
    // Two row kinds must key off something steadier than their own id, because the
    // id names the *newest thing in* the row rather than the row itself and so gets
    // rewritten as the row lives: a grouped notification (its notification id
    // advances with every new actor, and the streamed and REST copies of the same
    // group don't even agree on it) and a DM conversation (its status id advances
    // with every new message). Both carry a server-stable key; use it. Otherwise a
    // routine refresh silently renames the row the user is parked on, the reading
    // position can't be found, and they get thrown to the top of the timeline.
    std::string id() const {
        if (const auto* s = std::get_if<Status>(&value))
            return s->conversation_id.empty() ? "s:" + s->id : "c:" + s->conversation_id;
        if (const auto* n = std::get_if<Notification>(&value))
            return n->group_key.empty() ? "n:" + n->id : "n:" + n->group_key;
        if (const auto* u = std::get_if<User>(&value))
            return "u:" + u->id;
        return {};
    }

    // The status this row carries: itself, or a notification's referenced post.
    const Status* status() const {
        if (const auto* s = std::get_if<Status>(&value))
            return s;
        if (const auto* n = std::get_if<Notification>(&value))
            return n->status.get();
        return nullptr;
    }

    // The boost-unwrapped status to act on (boost/favorite/reply target).
    const Status* actionable_status() const {
        const Status* s = status();
        return s ? &s->display_status() : nullptr;
    }

    const User* user() const { return std::get_if<User>(&value); }
    const Notification* notification() const { return std::get_if<Notification>(&value); }

    // A direct message / direct-visibility mention (played with the "messages"
    // chime instead of the usual mentions/notification sound).
    bool is_direct() const {
        const Status* s = status();
        return s && s->visibility && *s->visibility == Visibility::Direct;
    }

    // The id this row paginates by (the outer status/notification/user id, not a
    // boost's underlying post) — used to seed scrollback after a cache load.
    std::string pagination_id() const {
        if (const auto* s = std::get_if<Status>(&value))
            return s->id;
        if (const auto* n = std::get_if<Notification>(&value))
            return n->id;
        if (const auto* u = std::get_if<User>(&value))
            return u->id;
        return {};
    }

    // Mutable access for optimistic UI updates (boost/favorite toggles).
    Status* mutable_status() {
        if (auto* s = std::get_if<Status>(&value))
            return s;
        if (auto* n = std::get_if<Notification>(&value))
            return n->status.get();
        return nullptr;
    }
    Status* mutable_actionable_status() {
        Status* s = mutable_status();
        return s ? &s->display_status() : nullptr;
    }

    // Whether this row is a pinned post (user timelines float pinned posts to the
    // top). The flag lives on the outer status (only set for a user's own pins).
    bool is_pinned() const {
        const auto* s = std::get_if<Status>(&value);
        return s && s->pinned;
    }

    // Sort key (unix seconds); 0 for users (which are not time-ordered).
    std::int64_t sort_date() const {
        if (const auto* s = std::get_if<Status>(&value))
            return s->created_at;
        if (const auto* n = std::get_if<Notification>(&value))
            return n->created_at;
        return 0;
    }
};

} // namespace fastsm
