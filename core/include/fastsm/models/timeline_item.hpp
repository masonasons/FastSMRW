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
    std::string id() const {
        if (const auto* s = std::get_if<Status>(&value))
            return "s:" + s->id;
        if (const auto* n = std::get_if<Notification>(&value))
            return "n:" + n->id;
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
