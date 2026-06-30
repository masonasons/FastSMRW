#include "fastsm/store/timeline_codec.hpp"

#include <cstdint>
#include <optional>

namespace fastsm::store {
namespace {

// Bump this whenever the on-disk layout changes so older caches are rejected
// cleanly (a magic mismatch -> empty) instead of being read with a mismatched
// reader. v2 added Status::url.
constexpr char kMagic[4] = {'F', 'S', 'C', '2'};
// Guard against runaway recursion if a file is ever corrupt/misaligned: boost/
// quote nesting is shallow in practice.
constexpr int kMaxStatusDepth = 24;

// --- little-endian writer over a growing byte buffer ---
struct Writer {
    std::string buf;
    void u8(std::uint8_t v) { buf.push_back(static_cast<char>(v)); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            buf.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i)
            buf.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void i32(int v) { u32(static_cast<std::uint32_t>(v)); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        buf.append(s);
    }
    void opt_str(const std::optional<std::string>& o) {
        if (o) {
            u8(1);
            str(*o);
        } else {
            u8(0);
        }
    }
};

// --- bounds-checked reader; sets ok=false on any underrun ---
struct Reader {
    std::string_view d;
    size_t pos = 0;
    bool ok = true;

    std::uint8_t u8() {
        if (pos + 1 > d.size()) {
            ok = false;
            return 0;
        }
        return static_cast<std::uint8_t>(d[pos++]);
    }
    std::uint32_t u32() {
        if (pos + 4 > d.size()) {
            ok = false;
            return 0;
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(d[pos++])) << (8 * i);
        return v;
    }
    std::uint64_t u64() {
        if (pos + 8 > d.size()) {
            ok = false;
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(d[pos++])) << (8 * i);
        return v;
    }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }
    int i32() { return static_cast<int>(u32()); }
    bool boolean() { return u8() != 0; }
    std::string str() {
        const std::uint32_t n = u32();
        if (!ok || pos + n > d.size()) {
            ok = false;
            return {};
        }
        std::string s(d.substr(pos, n));
        pos += n;
        return s;
    }
    std::optional<std::string> opt_str() {
        if (u8())
            return str();
        return std::nullopt;
    }
};

// --- models ---
void write_user(Writer& w, const User& u) {
    w.str(u.id);
    w.str(u.acct);
    w.str(u.username);
    w.str(u.display_name);
    w.str(u.note);
    w.str(u.avatar_url);
    w.str(u.header_url);
    w.str(u.url);
    w.i32(u.followers_count);
    w.i32(u.following_count);
    w.i32(u.statuses_count);
    w.i64(u.created_at);
    w.boolean(u.bot);
    w.boolean(u.locked);
    w.u8(static_cast<std::uint8_t>(u.platform));
}

User read_user(Reader& r) {
    User u;
    u.id = r.str();
    u.acct = r.str();
    u.username = r.str();
    u.display_name = r.str();
    u.note = r.str();
    u.avatar_url = r.str();
    u.header_url = r.str();
    u.url = r.str();
    u.followers_count = r.i32();
    u.following_count = r.i32();
    u.statuses_count = r.i32();
    u.created_at = r.i64();
    u.bot = r.boolean();
    u.locked = r.boolean();
    u.platform = static_cast<Platform>(r.u8());
    return u;
}

void write_media(Writer& w, const MediaAttachment& m) {
    w.str(m.id);
    w.u8(static_cast<std::uint8_t>(m.type));
    w.str(m.url);
    w.str(m.preview_url);
    w.str(m.description);
}

MediaAttachment read_media(Reader& r) {
    MediaAttachment m;
    m.id = r.str();
    m.type = static_cast<MediaAttachment::Kind>(r.u8());
    m.url = r.str();
    m.preview_url = r.str();
    m.description = r.str();
    return m;
}

void write_poll(Writer& w, const Poll& p) {
    w.str(p.id);
    w.i64(p.expires_at);
    w.boolean(p.expired);
    w.boolean(p.multiple);
    w.i32(p.votes_count);
    w.boolean(p.voted);
    w.u32(static_cast<std::uint32_t>(p.options.size()));
    for (const auto& o : p.options) {
        w.str(o.title);
        w.i32(o.votes_count);
    }
}

Poll read_poll(Reader& r) {
    Poll p;
    p.id = r.str();
    p.expires_at = r.i64();
    p.expired = r.boolean();
    p.multiple = r.boolean();
    p.votes_count = r.i32();
    p.voted = r.boolean();
    const std::uint32_t n = r.u32();
    for (std::uint32_t i = 0; i < n && r.ok; ++i) {
        Poll::Option o;
        o.title = r.str();
        o.votes_count = r.i32();
        p.options.push_back(std::move(o));
    }
    return p;
}

void write_card(Writer& w, const Card& c) {
    w.str(c.url);
    w.str(c.title);
    w.str(c.description);
    w.str(c.image_url);
}

Card read_card(Reader& r) {
    Card c;
    c.url = r.str();
    c.title = r.str();
    c.description = r.str();
    c.image_url = r.str();
    return c;
}

void write_status(Writer& w, const Status& s) {
    w.str(s.id);
    write_user(w, s.account);
    w.str(s.content);
    w.str(s.text);
    w.i64(s.created_at);
    w.i32(s.favourites_count);
    w.i32(s.boosts_count);
    w.i32(s.replies_count);
    w.opt_str(s.in_reply_to_id);
    w.opt_str(s.in_reply_to_account_id);
    // reblog / quote (recursive)
    w.boolean(static_cast<bool>(s.reblog));
    if (s.reblog)
        write_status(w, *s.reblog);
    w.boolean(static_cast<bool>(s.quote));
    if (s.quote)
        write_status(w, *s.quote);
    w.u32(static_cast<std::uint32_t>(s.media_attachments.size()));
    for (const auto& m : s.media_attachments)
        write_media(w, m);
    w.u32(static_cast<std::uint32_t>(s.mentions.size()));
    for (const auto& m : s.mentions) {
        w.str(m.id);
        w.str(m.acct);
        w.str(m.username);
        w.str(m.url);
    }
    // visibility (optional)
    if (s.visibility) {
        w.u8(1);
        w.u8(static_cast<std::uint8_t>(*s.visibility));
    } else {
        w.u8(0);
    }
    w.opt_str(s.spoiler_text);
    if (s.card) {
        w.u8(1);
        write_card(w, *s.card);
    } else {
        w.u8(0);
    }
    if (s.poll) {
        w.u8(1);
        write_poll(w, *s.poll);
    } else {
        w.u8(0);
    }
    w.boolean(s.pinned);
    w.boolean(s.favourited);
    w.boolean(s.boosted);
    w.opt_str(s.application_name);
    w.opt_str(s.instance_url);
    w.u8(static_cast<std::uint8_t>(s.platform));
    w.opt_str(s.cid);
    w.opt_str(s.like_uri);
    w.opt_str(s.repost_uri);
    w.str(s.url);
}

Status read_status(Reader& r, int depth = 0) {
    Status s;
    if (depth > kMaxStatusDepth) { // corrupt/misaligned data — refuse to recurse
        r.ok = false;
        return s;
    }
    s.id = r.str();
    s.account = read_user(r);
    s.content = r.str();
    s.text = r.str();
    s.created_at = r.i64();
    s.favourites_count = r.i32();
    s.boosts_count = r.i32();
    s.replies_count = r.i32();
    s.in_reply_to_id = r.opt_str();
    s.in_reply_to_account_id = r.opt_str();
    if (r.boolean())
        s.reblog = std::make_shared<Status>(read_status(r, depth + 1));
    if (r.boolean())
        s.quote = std::make_shared<Status>(read_status(r, depth + 1));
    const std::uint32_t media_n = r.u32();
    for (std::uint32_t i = 0; i < media_n && r.ok; ++i)
        s.media_attachments.push_back(read_media(r));
    const std::uint32_t mention_n = r.u32();
    for (std::uint32_t i = 0; i < mention_n && r.ok; ++i) {
        Mention m;
        m.id = r.str();
        m.acct = r.str();
        m.username = r.str();
        m.url = r.str();
        s.mentions.push_back(std::move(m));
    }
    if (r.u8())
        s.visibility = static_cast<Visibility>(r.u8());
    s.spoiler_text = r.opt_str();
    if (r.u8())
        s.card = read_card(r);
    if (r.u8())
        s.poll = read_poll(r);
    s.pinned = r.boolean();
    s.favourited = r.boolean();
    s.boosted = r.boolean();
    s.application_name = r.opt_str();
    s.instance_url = r.opt_str();
    s.platform = static_cast<Platform>(r.u8());
    s.cid = r.opt_str();
    s.like_uri = r.opt_str();
    s.repost_uri = r.opt_str();
    s.url = r.str();
    return s;
}

void write_notification(Writer& w, const Notification& n) {
    w.str(n.id);
    w.u8(static_cast<std::uint8_t>(n.type));
    write_user(w, n.account);
    w.i64(n.created_at);
    w.boolean(static_cast<bool>(n.status));
    if (n.status)
        write_status(w, *n.status);
    w.u8(static_cast<std::uint8_t>(n.platform));
}

Notification read_notification(Reader& r) {
    Notification n;
    n.id = r.str();
    n.type = static_cast<Notification::Kind>(r.u8());
    n.account = read_user(r);
    n.created_at = r.i64();
    if (r.boolean())
        n.status = std::make_shared<Status>(read_status(r));
    n.platform = static_cast<Platform>(r.u8());
    return n;
}

void write_item(Writer& w, const TimelineItem& item) {
    if (const auto* s = std::get_if<Status>(&item.value)) {
        w.u8(0);
        write_status(w, *s);
    } else if (const auto* n = std::get_if<Notification>(&item.value)) {
        w.u8(1);
        write_notification(w, *n);
    } else if (const auto* u = std::get_if<User>(&item.value)) {
        w.u8(2);
        write_user(w, *u);
    }
}

TimelineItem read_item(Reader& r) {
    TimelineItem item;
    switch (r.u8()) {
    case 1:
        item.value = read_notification(r);
        break;
    case 2:
        item.value = read_user(r);
        break;
    default:
        item.value = read_status(r);
        break;
    }
    return item;
}

} // namespace

std::string encode_items(const std::vector<TimelineItem>& items) {
    Writer w;
    w.buf.append(kMagic, sizeof(kMagic));
    w.u32(static_cast<std::uint32_t>(items.size()));
    for (const auto& item : items)
        write_item(w, item);
    return std::move(w.buf);
}

std::vector<TimelineItem> decode_items(std::string_view data) {
    std::vector<TimelineItem> items;
    if (data.size() < 8 || data.compare(0, 4, kMagic, 4) != 0)
        return items;
    Reader r;
    r.d = data;
    r.pos = 4;
    const std::uint32_t count = r.u32();
    items.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        TimelineItem item = read_item(r);
        if (!r.ok)
            return {}; // truncated/corrupt -> treat as a miss
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace fastsm::store
