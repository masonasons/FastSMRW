import AppKit

/// The app-wide view-model. Owns the `CoreClient`, holds the latest decoded
/// state (accounts, timelines, rows), routes core events to UI callbacks, and
/// exposes command helpers the view controllers call. This is the macOS
/// equivalent of FastSMApple's `AppServices`, but backed by the command/event
/// bus instead of a Swift core API. No engine logic lives here — it only
/// forwards commands and renders events.
@MainActor
final class AppState {
    let client: CoreClient
    let configDir: URL

    // Latest state from the core.
    private(set) var accounts: [Account] = []
    private(set) var selectedAccountKey = ""
    private(set) var timelines: [Timeline] = []
    private(set) var currentIndex = 0
    private(set) var rowsByIndex: [Int: [Row]] = [:]
    private(set) var selectedIdByIndex: [Int: String] = [:]
    private(set) var reversedByIndex: [Int: Bool] = [:]
    /// The full settings object from the last `settings` event, echoed back
    /// (with edits) on update_settings — the core re-applies defaults for any
    /// missing key, so we must always send the whole object.
    private(set) var settingsRaw: [String: Any] = [:]
    private(set) var soundpacks: [String] = []
    private(set) var speechCatalog: SpeechCatalog?

    // UI subscriptions (set by the controllers).
    var onAccountsChanged: (() -> Void)?
    var onTimelinesChanged: (() -> Void)?
    var onTimelineRows: ((Int) -> Void)?
    var onSelectRow: ((String) -> Void)?
    var onAnnounce: ((String) -> Void)?
    var onOpenURL: ((URL) -> Void)?
    var onAuthResult: ((AuthResult) -> Void)?
    var onComposeContext: ((ComposeContext) -> Void)?
    var onPostResult: ((Bool) -> Void)?
    var onSettings: (() -> Void)?
    var onSpawnable: (([Spawnable]) -> Void)?
    var onPostInfo: ((PostInfo) -> Void)?
    var onProfileEditor: ((ProfileEditor) -> Void)?
    var onUserProfile: ((UserProfile) -> Void)?
    var onUserPicker: ((UserPicker) -> Void)?
    var onClientFilter: ((ClientFilter) -> Void)?
    var onHashtagPrompt: (([String]) -> Void)?
    var onFollowedHashtags: ((FollowedHashtags) -> Void)?
    var onTrendingHashtags: ((FollowedHashtags) -> Void)?
    var onAliasPrompt: ((AliasPrompt) -> Void)?
    var onAliasesList: ((AliasesList) -> Void)?
    var onLists: ((Lists) -> Void)?
    var onServerFilters: ((ServerFilters) -> Void)?
    var onUserSuggestions: ((UserSuggestions) -> Void)?
    var onUserLists: ((UserLists) -> Void)?
    var onMediaOpen: ((MediaOpen) -> Void)?
    var onMediaPicker: ((MediaPicker) -> Void)?
    var onURLPicker: ((URLPicker) -> Void)?
    var onUpdateStatus: ((UpdateStatus) -> Void)?
    /// The core's "UserActions" request (Enter on a user row when enter_user_action
    /// is "actions"): the Mac's equivalent is opening the user profile dialog.
    var onUserActionsMenu: (() -> Void)?
    private var didStartupUpdateCheck = false
    /// One-shot: delay the next `announce` slightly so a menu key-equivalent's
    /// title (spoken by VoiceOver) doesn't stomp a Speak User/Reply result.
    private var delayNextAnnounce = false

    var currentRows: [Row] { rowsByIndex[currentIndex] ?? [] }
    var currentSelectedId: String { selectedIdByIndex[currentIndex] ?? "" }
    var currentReversed: Bool { reversedByIndex[currentIndex] ?? false }
    var currentTimelineTitle: String? {
        timelines.indices.contains(currentIndex) ? timelines[currentIndex].title : nil
    }
    var currentAccountHandle: String? {
        accounts.first { $0.key == selectedAccountKey }.map { "@\($0.handle)" }
    }

    init?() {
        let fm = FileManager.default
        guard let support = try? fm.url(for: .applicationSupportDirectory, in: .userDomainMask,
                                        appropriateFor: nil, create: true) else { return nil }
        let configDir = support.appendingPathComponent("FastSMRW", isDirectory: true)
        try? fm.createDirectory(at: configDir, withIntermediateDirectories: true)
        let soundpacks = Bundle.main.resourceURL?.appendingPathComponent("soundpacks",
                                                                          isDirectory: true)

        guard let client = CoreClient(configDir: configDir, soundpacksDir: soundpacks,
                                      userAgent: "FastSMRW/\(CoreClient.version)") else { return nil }
        self.configDir = configDir
        self.client = client
        client.onEvent = { [weak self] json in self?.handle(json) }
    }

    func start() {
        client.send("start")
        client.send("get_speech_catalog") // labels for the Speech Details lists
    }

    // MARK: Event routing

    private func handle(_ json: String) {
        // Settings carry a dynamic object; capture it raw so we can echo it back.
        if let data = json.data(using: .utf8),
           let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           obj["event"] as? String == "settings" {
            if let s = obj["settings"] as? [String: Any] { settingsRaw = s }
            if let packs = obj["soundpacks"] as? [String] { soundpacks = packs }
            onSettings?()
            // One silent update check at startup, once settings say it's wanted.
            if !didStartupUpdateCheck {
                didStartupUpdateCheck = true
                if settingsRaw["check_updates_on_startup"] as? Bool ?? true {
                    checkForUpdate(silent: true)
                }
            }
            return
        }
        // Copy the core-composed string to the system clipboard.
        if let data = json.data(using: .utf8),
           let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           obj["event"] as? String == "copy_to_clipboard" {
            if let text = obj["text"] as? String {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(text, forType: .string)
            }
            return
        }
        // The core asks the UI to show its per-platform "user actions" affordance.
        if let data = json.data(using: .utf8),
           let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           obj["event"] as? String == "invisible_ui_action" {
            if obj["action"] as? String == "UserActions" { onUserActionsMenu?() }
            return
        }
        guard let event = CoreEvent.decode(json) else { return }
        switch event {
        case let .accountsChanged(e):
            accounts = e.accounts
            selectedAccountKey = e.selected
            onAccountsChanged?()
        case let .timelinesChanged(e):
            timelines = e.timelines
            currentIndex = e.current
            selectedAccountKey = e.account
            onTimelinesChanged?()
        case let .timelineUpdated(e):
            rowsByIndex[e.index] = e.rows
            selectedIdByIndex[e.index] = e.selectedId
            reversedByIndex[e.index] = e.reversed
            onTimelineRows?(e.index)
        case let .selectRow(e):
            onSelectRow?(e.id)
        case let .announce(e):
            if delayNextAnnounce {
                delayNextAnnounce = false
                let message = e.message
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { [weak self] in
                    self?.onAnnounce?(message)
                }
            } else {
                onAnnounce?(e.message)
            }
        case let .openURL(e):
            if let url = URL(string: e.url) { onOpenURL?(url) }
        case let .authResult(e):
            onAuthResult?(e)
        case let .composeContext(e):
            onComposeContext?(e)
        case let .postResult(e):
            onPostResult?(e.ok)
        case let .spawnableTimelines(e):
            onSpawnable?(e.timelines)
        case let .postInfo(e):
            onPostInfo?(e)
        case let .profileEditor(e):
            onProfileEditor?(e)
        case let .userProfile(e):
            onUserProfile?(e)
        case let .userPicker(e):
            onUserPicker?(e)
        case let .clientFilter(e):
            if e.available { onClientFilter?(e.filter) }
        case let .hashtagPrompt(e):
            onHashtagPrompt?(e.tags)
        case let .followedHashtags(e):
            onFollowedHashtags?(e)
        case let .trendingHashtags(e):
            onTrendingHashtags?(e)
        case let .aliasPrompt(e):
            onAliasPrompt?(e)
        case let .aliasesList(e):
            onAliasesList?(e)
        case let .lists(e):
            onLists?(e)
        case let .serverFilters(e):
            onServerFilters?(e)
        case let .userSuggestions(e):
            onUserSuggestions?(e)
        case let .userLists(e):
            onUserLists?(e)
        case let .mediaOpen(e):
            onMediaOpen?(e)
        case let .mediaPicker(e):
            onMediaPicker?(e)
        case let .urlPicker(e):
            onURLPicker?(e)
        case let .speechCatalog(e):
            speechCatalog = e
        case let .updateStatus(e):
            onUpdateStatus?(e)
        case .other:
            break
        }
    }

    // MARK: Command helpers

    func playEarcon(_ name: String) { client.send("play_earcon", ["name": name]) }

    /// The configurable "interact" ("Enter") and "secondary interact"
    /// ("SecondaryAction") actions. The core resolves them from the Behavior
    /// settings (enter_post_action / enter_user_action / secondary_post_action)
    /// against the currently selected row, so the app doesn't duplicate that logic.
    func performAction(_ action: String) { client.send("perform_action", ["action": action]) }
    func refresh() { client.send("refresh") }

    // Settings: mutate the full object and echo it back so the core keeps every
    // field (it re-applies defaults for anything missing).
    func updateSettings(_ mutate: (inout [String: Any]) -> Void) {
        var s = settingsRaw
        mutate(&s)
        settingsRaw = s
        client.send("update_settings", ["settings": s])
    }
    func selectTimeline(dir: String) { client.send("select_timeline", ["dir": dir]) }
    func selectTimeline(number: Int) { client.send("select_timeline", ["number": number]) }
    func selectTimeline(index: Int) { client.send("select_timeline", ["index": index]) }
    func noteSelection(id: String) { client.send("note_selection", ["id": id]) }
    /// `automatic` marks a scroll/navigation-triggered load: the core gates those so
    /// a sparse feed (mentions) isn't paged further back every time we touch the end.
    func loadOlder(automatic: Bool = false) {
        client.send("load_older", ["automatic": automatic])
    }
    func loadGap(id: String) { client.send("load_gap", ["id": id]) }
    var autoLoadOlder: Bool { settingsRaw["auto_load_older"] as? Bool ?? true }
    var confirmDeletePost: Bool { settingsRaw["confirm_delete_post"] as? Bool ?? true }
    var confirmClearTimeline: Bool { settingsRaw["confirm_clear_timeline"] as? Bool ?? true }

    // Timelines: open a new one, close/clear the current one.
    func getSpawnable() { client.send("get_spawnable") }
    func spawnTimeline(kind: String, value: String? = nil, param: String? = nil) {
        var payload: [String: Any] = ["kind": kind]
        if let value { payload["value"] = value }
        if let param { payload["param"] = param }
        client.send("spawn_timeline", payload)
    }
    func closeTimeline() { client.send("close_timeline") }
    func clearTimeline() { client.send("clear_timeline") }
    func togglePin() { client.send("toggle_pin") }
    func toggleMute() { client.send("toggle_mute") }
    func beginAlias(id: String) { client.send("begin_alias", ["id": id, "pick": true]) }
    func beginAliasAccount(id: String, accountId: String) {
        client.send("begin_alias", ["id": id, "account_id": accountId])
    }
    func beginAliasHandle(_ handle: String) { client.send("begin_alias", ["handle": handle]) }
    func setAlias(key: String, handle: String, alias: String) {
        client.send("set_alias", ["key": key, "handle": handle, "alias": alias])
    }
    func clearAlias(key: String, handle: String) {
        client.send("clear_alias", ["key": key, "handle": handle])
    }
    func listAliases() { client.send("list_aliases") }
    // User Analysis: fetch the full follow lists and spawn a user timeline of the
    // chosen category (or announce an error if they can't be fully loaded).
    func analyzeUsers(category: String) { client.send("analyze_users", ["category": category]) }
    func reorderTimeline(dir: String) { client.send("reorder_timeline", ["dir": dir]) }
    // Movement units: jump the cursor forward/back by the active unit ("prev"/"next"),
    // and cycle which unit is active. The core replies with select_row / an announce.
    func moveByUnit(dir: String) {
        let from = currentSelectedId
        guard !from.isEmpty else { return }
        client.send("move", ["from_id": from, "dir": dir])
    }
    func cycleMovement(dir: String) { client.send("cycle_movement", ["dir": dir]) }
    // Speech field order/enable (nested under settings.speech.<category>).
    func speechItems(for category: String) -> [[String: Any]] {
        (settingsRaw["speech"] as? [String: Any])?[category] as? [[String: Any]] ?? []
    }
    func setSpeechItems(_ items: [[String: Any]], for category: String) {
        updateSettings { s in
            var speech = s["speech"] as? [String: Any] ?? [:]
            speech[category] = items
            s["speech"] = speech
        }
    }

    // Followed hashtags (Mastodon)
    func followHashtagPrompt(id: String) { client.send("follow_hashtag_prompt", ["id": id]) }
    func followHashtag(name: String) { client.send("follow_hashtag", ["name": name]) }
    func unfollowHashtag(name: String) { client.send("unfollow_hashtag", ["name": name]) }
    func listFollowedHashtags() { client.send("list_followed_hashtags") }
    func listTrendingHashtags() { client.send("list_trending_hashtags") }

    // Mastodon lists
    func listLists() { client.send("list_lists") }
    func createList(title: String) { client.send("create_list", ["title": title]) }
    func renameList(id: String, title: String) {
        client.send("rename_list", ["id": id, "title": title])
    }
    func deleteList(id: String) { client.send("delete_list", ["id": id]) }

    // Updates. Mac checks the version-tagged "stable" channel (the commit-based
    // "latest" channel needs an embedded build commit, which the Mac build lacks
    // — same rationale as Android).
    func checkForUpdate(silent: Bool) {
        client.send("check_for_update", ["silent": silent, "branch": "stable"])
    }

    func getClientFilter() { client.send("get_client_filter") }
    func setClientFilter(_ filter: [String: Any]) { client.send("set_client_filter", ["filter": filter]) }
    func clearClientFilter() { client.send("clear_client_filter") }

    // Mastodon server-side keyword filters
    func autocompleteUsers(query: String) { client.send("autocomplete_users", ["query": query]) }
    func getUserLists(accountId: String, acct: String) {
        client.send("get_user_lists", ["account_id": accountId, "acct": acct])
    }
    func setUserList(listId: String, accountId: String, add: Bool) {
        client.send("set_user_list", ["list_id": listId, "account_id": accountId, "add": add])
    }

    func listServerFilters() { client.send("list_server_filters") }
    func saveServerFilter(_ filter: [String: Any]) { client.send("save_server_filter", ["filter": filter]) }
    func deleteServerFilter(id: String) { client.send("delete_server_filter", ["id": id]) }
    func openThread(id: String) { client.send("open_thread", ["id": id]) }
    func openFollowers(id: String) { client.send("open_followers", ["id": id]) }
    func openFollowing(id: String) { client.send("open_following", ["id": id]) }
    func openUserTimeline(id: String) { client.send("open_user_timeline", ["id": id]) }
    func openUserProfile(id: String) { client.send("open_user_profile", ["id": id]) }
    func postInfo(id: String) { client.send("post_info", ["id": id]) }

    // Disambiguated user actions (from the user picker or profile dialog).
    func openUserTimeline(accountId: String, acct: String) {
        client.send("open_user_timeline", ["account_id": accountId, "acct": acct])
    }
    func openUserProfile(accountId: String, acct: String) {
        client.send("open_user_profile", ["account_id": accountId, "acct": acct])
    }
    func followToggle(accountId: String, acct: String) {
        client.send("follow_toggle", ["account_id": accountId, "acct": acct])
    }
    func setRelationship(accountId: String, action: String, acct: String) {
        client.send("set_relationship", ["account_id": accountId, "action": action, "acct": acct])
    }

    func toggleBoost(id: String) { client.send("toggle_boost", ["id": id]) }
    func toggleFavorite(id: String) { client.send("toggle_favorite", ["id": id]) }
    func toggleBookmark(id: String) { client.send("toggle_bookmark", ["id": id]) }
    func toggleAutoRead() { client.send("toggle_auto_read") }
    func copy(id: String) { client.send("copy", ["id": id]) }
    func openProfileEditor() { client.send("open_profile_editor") }
    func updateProfile(displayName: String, note: String, locked: Bool, bot: Bool,
                       discoverable: Bool, sensitive: Bool, privacy: String,
                       fields: [[String: String]]) {
        client.send("update_profile", [
            "display_name": displayName, "note": note,
            "locked": locked, "bot": bot, "discoverable": discoverable,
            "sensitive": sensitive, "privacy": privacy, "fields": fields,
        ])
    }
    func report(id: String?, accountId: String?, acct: String,
                category: String, comment: String, forward: Bool) {
        var payload: [String: Any] = ["category": category, "forward": forward]
        if let id { payload["id"] = id }
        if let accountId { payload["account_id"] = accountId }
        if !acct.isEmpty { payload["acct"] = acct }
        if !comment.isEmpty { payload["comment"] = comment }
        client.send("report", payload)
    }
    func togglePinPost(id: String) { client.send("toggle_pin_post", ["id": id]) }
    func toggleMuteConversation(id: String) { client.send("toggle_mute_conversation", ["id": id]) }
    func openFavoritedBy(id: String) { client.send("open_favorited_by", ["id": id]) }
    func openRebloggedBy(id: String) { client.send("open_reblogged_by", ["id": id]) }
    func deletePost(id: String) { client.send("delete_post", ["id": id]) }
    func openStatus(id: String) { client.send("open_status", ["id": id]) }
    // These are invoked from menu items with ⌘/⌘⇧ key-equivalents. VoiceOver
    // speaks the menu item's title when a key-equivalent fires, which stomps the
    // text we announce back. Arm a one-shot delay so our announcement lands just
    // after the title and wins.
    func speakUser(id: String) { delayNextAnnounce = true; client.send("speak_user", ["id": id]) }
    func speakReply(id: String) { delayNextAnnounce = true; client.send("speak_reply", ["id": id]) }
    func votePoll(id: String, choices: [Int]) { client.send("vote_poll", ["id": id, "choices": choices]) }
    func goBack() { client.send("go_back") }
    func refreshAll() { client.send("refresh_all") }
    func clearAllTimelines() { client.send("clear_all_timelines") }
    func removeAccount(key: String) { client.send("remove_account", ["key": key]) }
    func playMedia(id: String) { client.send("play_media", ["id": id]) }
    func playMedia(url: String, kind: String, title: String) {
        client.send("play_media", ["url": url, "kind": kind, "title": title])
    }
    func openPostLinks(id: String) { client.send("open_post_links", ["id": id]) }

    func requestCompose(mode: String, id: String? = nil) {
        var payload: [String: Any] = ["mode": mode]
        if let id { payload["id"] = id }
        client.send("compose_context", payload)
    }

    func post(draft: [String: Any], editId: String? = nil) {
        var payload: [String: Any] = ["draft": draft]
        if let editId { payload["edit_id"] = editId }
        client.send("post", payload)
    }

    // Accounts
    func selectAccount(dir: String) { client.send("select_account", ["dir": dir]) }
    func beginMastodonLogin(instance: String) {
        client.send("begin_mastodon_login", ["instance": instance])
    }
    func finishMastodonLogin(code: String) {
        client.send("finish_mastodon_login", ["code": code])
    }
    func addBluesky(service: String, handle: String, appPassword: String) {
        client.send("add_account", ["platform": "bluesky", "service": service,
                                    "handle": handle, "app_password": appPassword])
    }
}
