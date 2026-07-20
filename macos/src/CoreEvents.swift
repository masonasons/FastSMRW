import Foundation

/// Decoded shapes of the core events the M1 app renders. Each event JSON carries
/// an `"event"` discriminator; `CoreEvent.decode` reads it, then decodes the
/// matching payload. Fields the app doesn't use yet are simply omitted.

// MARK: Models

struct Account: Decodable, Equatable {
    let key: String
    let handle: String
    let displayName: String
    let platform: String

    enum CodingKeys: String, CodingKey {
        case key, handle, platform
        case displayName = "display_name"
    }
}

struct Timeline: Decodable, Equatable {
    let title: String
    let kind: String
    var dismissable = false
    var pinned = false
    var muted = false
    var userList = false

    enum CodingKeys: String, CodingKey {
        case title, kind, dismissable, pinned, muted
        case userList = "user_list"
    }
}

struct Row: Decodable, Equatable {
    let id: String
    let text: String
    var favorited = false
    var boosted = false
    var hasMedia = false
    var isReply = false
    var isMine = false
    var gapAfter = false
    var followRequest = false
    var accountId: String?
    var acct: String?

    enum CodingKeys: String, CodingKey {
        case id, text, favorited, boosted, acct
        case hasMedia = "has_media"
        case isReply = "is_reply"
        case isMine = "is_mine"
        case gapAfter = "gap_after"
        case followRequest = "follow_request"
        case accountId = "account_id"
    }

    // Custom decoder: the core omits most boolean flags when they're false (e.g.
    // has_media, is_reply), and Swift's synthesized Decodable would throw on a
    // missing key even though the property has a default. Tolerate absence.
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(String.self, forKey: .id)
        text = try c.decode(String.self, forKey: .text)
        favorited = try c.decodeIfPresent(Bool.self, forKey: .favorited) ?? false
        boosted = try c.decodeIfPresent(Bool.self, forKey: .boosted) ?? false
        hasMedia = try c.decodeIfPresent(Bool.self, forKey: .hasMedia) ?? false
        isReply = try c.decodeIfPresent(Bool.self, forKey: .isReply) ?? false
        isMine = try c.decodeIfPresent(Bool.self, forKey: .isMine) ?? false
        gapAfter = try c.decodeIfPresent(Bool.self, forKey: .gapAfter) ?? false
        followRequest = try c.decodeIfPresent(Bool.self, forKey: .followRequest) ?? false
        accountId = try c.decodeIfPresent(String.self, forKey: .accountId)
        acct = try c.decodeIfPresent(String.self, forKey: .acct)
    }
}

// MARK: Event payloads

struct AccountsChanged: Decodable {
    let accounts: [Account]
    let selected: String
}

struct TimelinesChanged: Decodable {
    let timelines: [Timeline]
    let current: Int
    let account: String
}

struct TimelineUpdated: Decodable {
    let index: Int
    let selectedId: String
    var reversed = false
    let rows: [Row]

    enum CodingKeys: String, CodingKey {
        case index, reversed, rows
        case selectedId = "selected_id"
    }
}

struct AuthResult: Decodable {
    let ok: Bool
    var error: String?
}

struct PostResult: Decodable { let ok: Bool }

/// One entry offered by the New Timeline sheet. `input` (if present) is a prompt
/// label meaning the kind needs a typed value; `param` (lists) is echoed back.
struct Spawnable: Decodable {
    let kind: String
    let title: String
    var param: String?
    var input: String?
}
struct SpawnableTimelines: Decodable { let timelines: [Spawnable] }

/// A poll the viewer can still vote in (shown in Post Info).
struct PostInfoPoll: Decodable { var multiple = false; let options: [String] }

/// Enter-on-post detail dialog.
struct PostInfo: Decodable {
    let id: String
    let text: String
    var hasUrl = false
    var isMine = false
    var muted = false
    var favoritesCount = 0
    var boostsCount = 0
    var features: [String: Bool]?
    var poll: PostInfoPoll?

    enum CodingKeys: String, CodingKey {
        case id, text, features, poll, muted
        case hasUrl = "has_url"
        case isMine = "is_mine"
        case favoritesCount = "favorites_count"
        case boostsCount = "boosts_count"
    }
}

/// One profile metadata row (label + content).
struct ProfileFieldItem: Decodable {
    let name: String
    let value: String
}

/// Your own profile's editable source (raw, not HTML), for Edit Profile.
struct ProfileEditor: Decodable {
    let displayName: String
    let note: String
    var locked = false
    var bot = false
    var discoverable = false
    var sensitive = false
    var privacy = "public"
    var maxFields = 4
    var fields: [ProfileFieldItem] = []
    enum CodingKeys: String, CodingKey {
        case note, locked, bot, discoverable, sensitive, privacy, fields
        case displayName = "display_name"
        case maxFields = "max_fields"
    }
}

/// A user's profile + relationship, for the profile dialog.
struct UserProfile: Decodable {
    let text: String
    let accountId: String
    let acct: String
    var url: String?
    var hasRelationship = false
    var canHideBoosts = false
    var canUseLists = false
    var following: Bool?
    var muting: Bool?
    var blocking: Bool?
    var requested: Bool?
    var showingReblogs: Bool?

    enum CodingKeys: String, CodingKey {
        case text, acct, url, following, muting, blocking, requested
        case accountId = "account_id"
        case hasRelationship = "has_relationship"
        case canHideBoosts = "can_hide_boosts"
        case canUseLists = "can_use_lists"
        case showingReblogs = "showing_reblogs"
    }
}

/// @-mention autocomplete suggestions while composing.
struct UserSuggestion: Decodable {
    let id: String
    let acct: String
    var display = ""
    var label = ""
    enum CodingKeys: String, CodingKey { case id, acct, display, label }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(String.self, forKey: .id) ?? ""
        acct = try c.decodeIfPresent(String.self, forKey: .acct) ?? ""
        display = try c.decodeIfPresent(String.self, forKey: .display) ?? ""
        label = try c.decodeIfPresent(String.self, forKey: .label) ?? ""
    }
}
struct UserSuggestions: Decodable { let query: String; let users: [UserSuggestion] }

/// A user's list memberships (for the Add-to-List sheet).
struct UserListEntry: Decodable {
    let id: String
    let title: String
    var member = false
    enum CodingKeys: String, CodingKey { case id, title, member }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(String.self, forKey: .id) ?? ""
        title = try c.decodeIfPresent(String.self, forKey: .title) ?? ""
        member = try c.decodeIfPresent(Bool.self, forKey: .member) ?? false
    }
}
struct UserLists: Decodable {
    var supported = false
    var accountId = ""
    var acct = ""
    var lists: [UserListEntry] = []
    enum CodingKeys: String, CodingKey {
        case supported, acct, lists
        case accountId = "account_id"
    }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        supported = try c.decodeIfPresent(Bool.self, forKey: .supported) ?? false
        accountId = try c.decodeIfPresent(String.self, forKey: .accountId) ?? ""
        acct = try c.decodeIfPresent(String.self, forKey: .acct) ?? ""
        lists = try c.decodeIfPresent([UserListEntry].self, forKey: .lists) ?? []
    }
}

/// Disambiguation when a post references several users.
struct PickUser: Decodable { let id: String; let acct: String }
struct UserPicker: Decodable {
    let purpose: String  // follow_toggle | timeline | profile
    let id: String
    let users: [PickUser]
}
struct Announce: Decodable { let message: String }
struct OpenURL: Decodable { let url: String }
struct SelectRow: Decodable { let id: String }

/// A reply recipient the user can toggle on/off (Mastodon).
struct ReplyParticipant: Decodable {
    let acct: String
    var displayName = ""
    var checked = true
    enum CodingKeys: String, CodingKey {
        case acct, checked
        case displayName = "display_name"
    }
}
struct ComposeLanguage: Decodable { let code: String; let name: String }

/// Prefill + capabilities for a compose sheet (mode: new/reply/quote/edit).
struct ComposeContext: Decodable {
    let mode: String
    var title: String?
    var maxChars: Int?
    var enterToSend: Bool?
    var platform: String?
    var features: [String: Bool]?
    var languages: [ComposeLanguage]?
    var replyParticipants: [ReplyParticipant]?
    var defaultVisibility: Int?
    var contextLabel: String?
    var prefillText: String?
    var prefillCw: String?
    var replyToId: String?
    var replyToUrl: String?
    var quotedStatusId: String?
    var quotedStatusCid: String?
    var quotedStatusUrl: String?
    var editId: String?

    enum CodingKeys: String, CodingKey {
        case mode, title, platform, features, languages
        case maxChars = "max_chars"
        case enterToSend = "enter_to_send"
        case replyParticipants = "reply_participants"
        case defaultVisibility = "default_visibility"
        case contextLabel = "context_label"
        case prefillText = "prefill_text"
        case prefillCw = "prefill_cw"
        case replyToId = "reply_to_id"
        case replyToUrl = "reply_to_url"
        case quotedStatusId = "quoted_status_id"
        case quotedStatusCid = "quoted_status_cid"
        case quotedStatusUrl = "quoted_status_url"
        case editId = "edit_id"
    }
}

/// Per-timeline client-side filter. Each bool is "show this category"; `text`
/// keeps only posts containing it. All-true + empty text = no filtering.
struct ClientFilter: Decodable {
    var original = true
    var replies = true
    var repliesToMe = true
    var threads = true
    var boosts = true
    var quotes = true
    var media = true
    var noMedia = true
    var myPosts = true
    var myReplies = true
    var text = ""

    enum CodingKeys: String, CodingKey {
        case original, replies, threads, boosts, quotes, media, text
        case repliesToMe = "replies_to_me"
        case noMedia = "no_media"
        case myPosts = "my_posts"
        case myReplies = "my_replies"
    }
}
struct ClientFilterEvent: Decodable { let available: Bool; let filter: ClientFilter }

/// A single media attachment to view/play. kind ∈ image/video/audio/gifv/media.
struct MediaOpen: Decodable { let url: String; let kind: String; var title = "" }
struct MediaItem: Decodable { let title: String; let url: String; let kind: String }
struct MediaPicker: Decodable { let id: String; let items: [MediaItem] }

/// Links in a post, when there's more than one to choose from.
struct URLLink: Decodable { let title: String; let url: String }
struct URLPicker: Decodable { let links: [URLLink] }

/// Each speech field's stable key + human label, per category. Order/enabled
/// come from the settings event's speech arrays; this supplies the labels.
struct SpeechField: Decodable { let key: String; let label: String }
struct SpeechCatalog: Decodable {
    let status: [SpeechField]
    let user: [SpeechField]
    let notification: [SpeechField]
    func fields(for category: String) -> [SpeechField] {
        switch category {
        case "user": return user
        case "notification": return notification
        default: return status
        }
    }
}

/// Followed hashtags (Mastodon). `supported` is omitted on the refresh emitted
/// after an unfollow, so tolerate its absence.
struct HashtagPrompt: Decodable { let tags: [String] }

/// Prompt to add/edit a user alias (a global, cross-account custom display name).
struct AliasPrompt: Decodable { let key: String; let handle: String; let current: String }
struct AliasItem: Decodable, Equatable { let key: String; let handle: String; let alias: String }
struct AliasesList: Decodable { let aliases: [AliasItem] }
struct FollowedTag: Decodable {
    let name: String
    var url = ""
    var following = true
    enum CodingKeys: String, CodingKey { case name, url, following }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        name = try c.decode(String.self, forKey: .name)
        url = try c.decodeIfPresent(String.self, forKey: .url) ?? ""
        following = try c.decodeIfPresent(Bool.self, forKey: .following) ?? true
    }
}
struct FollowedHashtags: Decodable {
    var supported = true
    var tags: [FollowedTag] = []
    enum CodingKeys: String, CodingKey { case supported, tags }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        supported = try c.decodeIfPresent(Bool.self, forKey: .supported) ?? true
        tags = try c.decodeIfPresent([FollowedTag].self, forKey: .tags) ?? []
    }
}

/// Mastodon lists.
struct ListInfo: Decodable {
    let id: String
    let title: String
    var repliesPolicy = ""
    var exclusive = false
    enum CodingKeys: String, CodingKey {
        case id, title, exclusive
        case repliesPolicy = "replies_policy"
    }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(String.self, forKey: .id)
        title = try c.decode(String.self, forKey: .title)
        repliesPolicy = try c.decodeIfPresent(String.self, forKey: .repliesPolicy) ?? ""
        exclusive = try c.decodeIfPresent(Bool.self, forKey: .exclusive) ?? false
    }
}
struct Lists: Decodable {
    var supported = false
    var lists: [ListInfo] = []
    enum CodingKeys: String, CodingKey { case supported, lists }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        supported = try c.decodeIfPresent(Bool.self, forKey: .supported) ?? false
        lists = try c.decodeIfPresent([ListInfo].self, forKey: .lists) ?? []
    }
}

/// Result of a check_for_update. Mac uses the `dmg_url` (a .dmg release asset);
/// falls back to opening `download_url` if none.
struct UpdateStatus: Decodable {
    var silent = false
    var available = false
    var version = ""
    var notes = ""
    var downloadUrl = ""
    var dmgUrl = ""
    var error = ""
    enum CodingKeys: String, CodingKey {
        case silent, available, version, notes, error
        case downloadUrl = "download_url"
        case dmgUrl = "dmg_url"
    }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        silent = try c.decodeIfPresent(Bool.self, forKey: .silent) ?? false
        available = try c.decodeIfPresent(Bool.self, forKey: .available) ?? false
        version = try c.decodeIfPresent(String.self, forKey: .version) ?? ""
        notes = try c.decodeIfPresent(String.self, forKey: .notes) ?? ""
        downloadUrl = try c.decodeIfPresent(String.self, forKey: .downloadUrl) ?? ""
        dmgUrl = try c.decodeIfPresent(String.self, forKey: .dmgUrl) ?? ""
        error = try c.decodeIfPresent(String.self, forKey: .error) ?? ""
    }
}

/// Mastodon server-side keyword filters.
struct ServerFilterKeyword: Decodable {
    var id = ""
    var keyword = ""
    var wholeWord = false
    enum CodingKeys: String, CodingKey {
        case id, keyword
        case wholeWord = "whole_word"
    }
    init(id: String = "", keyword: String, wholeWord: Bool) {
        self.id = id; self.keyword = keyword; self.wholeWord = wholeWord
    }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(String.self, forKey: .id) ?? ""
        keyword = try c.decodeIfPresent(String.self, forKey: .keyword) ?? ""
        wholeWord = try c.decodeIfPresent(Bool.self, forKey: .wholeWord) ?? false
    }
}
struct ServerFilter: Decodable {
    var id = ""
    var title = ""
    var action = "warn"
    var context: [String] = []
    var keywords: [ServerFilterKeyword] = []
    var expiresAt: Int?
    enum CodingKeys: String, CodingKey {
        case id, title, action, context, keywords
        case expiresAt = "expires_at"
    }
    init() {}
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decodeIfPresent(String.self, forKey: .id) ?? ""
        title = try c.decodeIfPresent(String.self, forKey: .title) ?? ""
        action = try c.decodeIfPresent(String.self, forKey: .action) ?? "warn"
        context = try c.decodeIfPresent([String].self, forKey: .context) ?? []
        keywords = try c.decodeIfPresent([ServerFilterKeyword].self, forKey: .keywords) ?? []
        expiresAt = try c.decodeIfPresent(Int.self, forKey: .expiresAt)
    }
}
struct ServerFilters: Decodable {
    var supported = false
    var filters: [ServerFilter] = []
    enum CodingKeys: String, CodingKey { case supported, filters }
    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        supported = try c.decodeIfPresent(Bool.self, forKey: .supported) ?? false
        filters = try c.decodeIfPresent([ServerFilter].self, forKey: .filters) ?? []
    }
}

// MARK: Envelope

enum CoreEvent {
    case accountsChanged(AccountsChanged)
    case timelinesChanged(TimelinesChanged)
    case timelineUpdated(TimelineUpdated)
    case announce(Announce)
    case authResult(AuthResult)
    case postResult(PostResult)
    case openURL(OpenURL)
    case selectRow(SelectRow)
    case composeContext(ComposeContext)
    case spawnableTimelines(SpawnableTimelines)
    case postInfo(PostInfo)
    case profileEditor(ProfileEditor)
    case userProfile(UserProfile)
    case userPicker(UserPicker)
    case clientFilter(ClientFilterEvent)
    case mediaOpen(MediaOpen)
    case mediaPicker(MediaPicker)
    case urlPicker(URLPicker)
    case speechCatalog(SpeechCatalog)
    case hashtagPrompt(HashtagPrompt)
    case followedHashtags(FollowedHashtags)
    case trendingHashtags(FollowedHashtags)
    case aliasPrompt(AliasPrompt)
    case aliasesList(AliasesList)
    case lists(Lists)
    case updateStatus(UpdateStatus)
    case serverFilters(ServerFilters)
    case userSuggestions(UserSuggestions)
    case userLists(UserLists)
    case other(name: String)

    /// Parse a raw event JSON string into a typed case. Returns nil only if the
    /// JSON is malformed or lacks an `"event"` name.
    static func decode(_ json: String) -> CoreEvent? {
        guard let data = json.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let name = obj["event"] as? String else { return nil }
        let d = JSONDecoder()
        func decode<T: Decodable>(_ type: T.Type) -> T? { try? d.decode(T.self, from: data) }
        switch name {
        case "accounts_changed": return decode(AccountsChanged.self).map(CoreEvent.accountsChanged)
        case "timelines_changed": return decode(TimelinesChanged.self).map(CoreEvent.timelinesChanged)
        case "timeline_updated": return decode(TimelineUpdated.self).map(CoreEvent.timelineUpdated)
        case "announce": return decode(Announce.self).map(CoreEvent.announce)
        case "auth_result": return decode(AuthResult.self).map(CoreEvent.authResult)
        case "post_result": return decode(PostResult.self).map(CoreEvent.postResult)
        case "open_url": return decode(OpenURL.self).map(CoreEvent.openURL)
        case "select_row": return decode(SelectRow.self).map(CoreEvent.selectRow)
        case "compose_context": return decode(ComposeContext.self).map(CoreEvent.composeContext)
        case "spawnable_timelines":
            return decode(SpawnableTimelines.self).map(CoreEvent.spawnableTimelines)
        case "post_info": return decode(PostInfo.self).map(CoreEvent.postInfo)
        case "profile_editor": return decode(ProfileEditor.self).map(CoreEvent.profileEditor)
        case "user_profile": return decode(UserProfile.self).map(CoreEvent.userProfile)
        case "user_picker": return decode(UserPicker.self).map(CoreEvent.userPicker)
        case "client_filter": return decode(ClientFilterEvent.self).map(CoreEvent.clientFilter)
        case "media_open": return decode(MediaOpen.self).map(CoreEvent.mediaOpen)
        case "media_picker": return decode(MediaPicker.self).map(CoreEvent.mediaPicker)
        case "url_picker": return decode(URLPicker.self).map(CoreEvent.urlPicker)
        case "speech_catalog": return decode(SpeechCatalog.self).map(CoreEvent.speechCatalog)
        case "hashtag_prompt": return decode(HashtagPrompt.self).map(CoreEvent.hashtagPrompt)
        case "followed_hashtags": return decode(FollowedHashtags.self).map(CoreEvent.followedHashtags)
        case "trending_hashtags": return decode(FollowedHashtags.self).map(CoreEvent.trendingHashtags)
        case "alias_prompt": return decode(AliasPrompt.self).map(CoreEvent.aliasPrompt)
        case "aliases_list": return decode(AliasesList.self).map(CoreEvent.aliasesList)
        case "lists": return decode(Lists.self).map(CoreEvent.lists)
        case "update_status": return decode(UpdateStatus.self).map(CoreEvent.updateStatus)
        case "server_filters": return decode(ServerFilters.self).map(CoreEvent.serverFilters)
        case "user_suggestions": return decode(UserSuggestions.self).map(CoreEvent.userSuggestions)
        case "user_lists": return decode(UserLists.self).map(CoreEvent.userLists)
        default: return .other(name: name)
        }
    }
}
