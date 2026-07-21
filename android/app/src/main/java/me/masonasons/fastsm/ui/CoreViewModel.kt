package me.masonasons.fastsm.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import me.masonasons.fastsm.core.FastSmCore
import org.json.JSONArray
import org.json.JSONObject

/** An account as the core reports it (accounts_changed). */
data class AccountUi(
    val key: String,
    val handle: String,
    val displayName: String,
    val platform: String,
)

/** An open timeline / tab (timelines_changed). */
data class TabUi(
    val title: String,
    val kind: String,
    val dismissable: Boolean,
    val pinned: Boolean,
    val muted: Boolean,
)

/**
 * One timeline row. [text] is the core's fully-composed accessibility label —
 * the UI uses it verbatim as the TalkBack contentDescription (all speech
 * composition lives in the core).
 */
data class RowUi(
    val id: String,
    val text: String,
    val favorited: Boolean,
    val boosted: Boolean,
    val bookmarked: Boolean,
    val isMine: Boolean,
    val hasMedia: Boolean,
    val isReply: Boolean,
    val muted: Boolean, // conversation muted (thread notifications off)
    // Grouped like/boost notification: "favorited_by"/"reblogged_by" (or "") — tap
    // opens the list of everyone in the group instead of the thread.
    val groupActors: String,
    val favoritesCount: Int, // >0 -> "See who favorited" is offered
    val boostsCount: Int,    // >0 -> "See who boosted" is offered
)

/** A media attachment to view (media_open, or an item in a media_picker). */
data class MediaItemUi(val url: String, val kind: String, val title: String)

/** Per-account settings (currently the account's soundpack). */
data class AccountSettingsUi(
    val accountKey: String,
    val acct: String,
    val soundpack: String,
    val soundpacks: List<String>,
)

/**
 * A timeline the user can open (spawnable_timelines). [input] non-null means it
 * needs a typed value (search query, hashtag, instance); [param] is a fixed id
 * (e.g. a list). Opening spawns a new tab.
 */
data class SpawnableUi(val kind: String, val title: String, val input: String?, val param: String?)

/** A media attachment to upload with a post (bytes already base64-encoded). */
data class OutgoingMedia(
    val filename: String,
    val mime: String,
    val alt: String,
    val dataBase64: String,
)

/** Result of an add_account attempt, routed to the Add-account screen. */
data class AuthResult(val ok: Boolean, val error: String)

/** A reply recipient the user can toggle (Mastodon). */
data class ComposeParticipant(val acct: String, val displayName: String, val checked: Boolean)

/**
 * A newer release the update check found (update_status with available=true and a
 * usable APK asset). [apkUrl] is the FastSMRW.apk download to open in the browser.
 */
data class AppUpdateUi(val version: String, val notes: String, val apkUrl: String)

/** One candidate user in a disambiguation picker. */
data class UserPick(val id: String, val acct: String)

/** One @-mention autocomplete candidate: [acct] is inserted, [label] is shown. */
data class MentionSuggestion(val acct: String, val label: String)

/** A batch of @-mention suggestions echoing the [query] they answered. */
data class MentionSuggestions(val query: String, val users: List<MentionSuggestion>)

/**
 * The core asks which user to act on when a post references more than one
 * (author + mentions). [purpose] is "timeline" or "profile".
 */
data class UserPickerRequest(val purpose: String, val rowId: String, val users: List<UserPick>)

/** Prompt to add/edit a user alias (a global, cross-account custom display name). */
data class AliasPromptRequest(val key: String, val handle: String, val current: String)

/** One saved alias (aliases_list event), for the aliases manager. */
data class AliasEntry(val key: String, val handle: String, val alias: String)

/** One trending hashtag (from trending_hashtags). [following] = you already follow it. */
data class TrendingTagUi(val name: String, val url: String, val following: Boolean)

/** One profile metadata row (label + content). */
data class ProfileFieldUi(val name: String, val value: String)

/** Your own profile's editable source, for the Edit Profile dialog. */
data class ProfileEditorUi(
    val displayName: String,
    val note: String,
    val locked: Boolean,
    val bot: Boolean,
    val discoverable: Boolean,
    val sensitive: Boolean,
    val privacy: String,
    val maxFields: Int,
    val fields: List<ProfileFieldUi>,
    // Bluesky: only display name + bio apply; hide the Mastodon-only controls.
    val simple: Boolean = false,
)

/** A user's profile card (user_profile event). [text] is the composed profile. */
data class ProfileUi(
    val text: String,
    val accountId: String,
    val acct: String,
    val url: String,
    val hasRelationship: Boolean,
    val following: Boolean,
    val muting: Boolean,
    val blocking: Boolean,
    val requested: Boolean,
)

/**
 * Everything the compose screen needs, from the core's compose_context event:
 * prefill, the post being replied-to/quoted/edited, platform capabilities, and
 * the char limit. Presenting when non-null opens the composer.
 */
data class ComposeContext(
    val mode: String,
    val title: String,
    val contextLabel: String?,
    val maxChars: Int,
    val platform: String,
    val featureContentWarning: Boolean,
    val featureVisibility: Boolean,
    val featureEditing: Boolean,
    val featureMedia: Boolean,
    val defaultVisibility: Int, // -1 = none supplied
    val prefillText: String,
    val prefillCw: String,
    val replyToId: String?,
    val replyToUrl: String?,
    val quotedStatusId: String?,
    val quotedStatusCid: String?,
    val quotedStatusUrl: String?,
    val editId: String?,
    val participants: List<ComposeParticipant>,
)

/**
 * Owns the single [FastSmCore] instance and turns its JSON event stream into
 * Compose-friendly state. The UI sends intents through the command methods and
 * renders these flows; it never touches the native layer directly.
 *
 * Core events arrive on a background (core-owned) thread; StateFlow is
 * thread-safe to update, and Compose collects on the main thread.
 */
class CoreViewModel(app: Application) : AndroidViewModel(app) {

    private val core: FastSmCore = FastSmCore.create(app)

    private val _accounts = MutableStateFlow<List<AccountUi>>(emptyList())
    val accounts: StateFlow<List<AccountUi>> = _accounts.asStateFlow()

    private val _selectedAccount = MutableStateFlow("")
    val selectedAccount: StateFlow<String> = _selectedAccount.asStateFlow()

    private val _tabs = MutableStateFlow<List<TabUi>>(emptyList())
    val tabs: StateFlow<List<TabUi>> = _tabs.asStateFlow()

    private val _currentTab = MutableStateFlow(0)
    val currentTab: StateFlow<Int> = _currentTab.asStateFlow()

    private val _rowsByTab = MutableStateFlow<Map<Int, List<RowUi>>>(emptyMap())
    val rowsByTab: StateFlow<Map<Int, List<RowUi>>> = _rowsByTab.asStateFlow()

    // The core's remembered reading position per tab (row id to restore to).
    private val _selectedIdByTab = MutableStateFlow<Map<Int, String>>(emptyMap())
    val selectedIdByTab: StateFlow<Map<Int, String>> = _selectedIdByTab.asStateFlow()

    private val _authResults = MutableSharedFlow<AuthResult>(
        extraBufferCapacity = 4,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val authResults: SharedFlow<AuthResult> = _authResults.asSharedFlow()

    /** Spoken announcements from the core (e.g. "Signing in…", errors). */
    private val _announcements = MutableSharedFlow<String>(
        extraBufferCapacity = 8,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val announcements: SharedFlow<String> = _announcements.asSharedFlow()

    /** Text the core asked us to copy to the system clipboard (Ctrl+C / Copy). */
    private val _copyText = MutableSharedFlow<String>(
        extraBufferCapacity = 4,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val copyText: SharedFlow<String> = _copyText.asSharedFlow()

    /** Mastodon OAuth (Phase 1b): the core asks us to open a URL in the browser. */
    private val _openUrls = MutableSharedFlow<String>(
        extraBufferCapacity = 4,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val openUrls: SharedFlow<String> = _openUrls.asSharedFlow()

    /** Non-null while the composer is open (from a compose_context event). */
    private val _composeContext = MutableStateFlow<ComposeContext?>(null)
    val composeContext: StateFlow<ComposeContext?> = _composeContext.asStateFlow()

    /** Non-null while a user-disambiguation picker should be shown. */
    private val _userPicker = MutableStateFlow<UserPickerRequest?>(null)
    val userPicker: StateFlow<UserPickerRequest?> = _userPicker.asStateFlow()

    /** Non-null while the add/edit-alias prompt should be shown. */
    private val _aliasPrompt = MutableStateFlow<AliasPromptRequest?>(null)
    val aliasPrompt: StateFlow<AliasPromptRequest?> = _aliasPrompt.asStateFlow()

    /** Non-null while the aliases manager should be shown (the current alias list). */
    private val _aliasesList = MutableStateFlow<List<AliasEntry>?>(null)
    val aliasesList: StateFlow<List<AliasEntry>?> = _aliasesList.asStateFlow()

    /** Non-null while the Trending Hashtags picker should be shown (the trend list). */
    private val _trendingHashtags = MutableStateFlow<List<TrendingTagUi>?>(null)
    val trendingHashtags: StateFlow<List<TrendingTagUi>?> = _trendingHashtags.asStateFlow()

    /** Latest @-mention autocomplete results (from user_suggestions), or null. */
    private val _mentionSuggestions = MutableStateFlow<MentionSuggestions?>(null)
    val mentionSuggestions: StateFlow<MentionSuggestions?> = _mentionSuggestions.asStateFlow()

    /** Non-null while a profile screen is open (from a user_profile event). */
    private val _profile = MutableStateFlow<ProfileUi?>(null)
    val profile: StateFlow<ProfileUi?> = _profile.asStateFlow()

    /** Non-null while the Edit Profile dialog should be shown (profile_editor event). */
    private val _profileEditor = MutableStateFlow<ProfileEditorUi?>(null)
    val profileEditor: StateFlow<ProfileEditorUi?> = _profileEditor.asStateFlow()

    /** The core's full settings object (from the settings event), or null. */
    private val _settings = MutableStateFlow<JSONObject?>(null)
    val settings: StateFlow<JSONObject?> = _settings.asStateFlow()

    /** Available soundpack names. */
    private val _soundpacks = MutableStateFlow<List<String>>(emptyList())
    val soundpacks: StateFlow<List<String>> = _soundpacks.asStateFlow()

    /** Non-null while the media viewer is open (from a media_open event). */
    private val _media = MutableStateFlow<MediaItemUi?>(null)
    val media: StateFlow<MediaItemUi?> = _media.asStateFlow()

    /** Non-null while a media-attachment chooser should be shown. */
    private val _mediaPicker = MutableStateFlow<List<MediaItemUi>?>(null)
    val mediaPicker: StateFlow<List<MediaItemUi>?> = _mediaPicker.asStateFlow()

    /** Timelines the user can open (Add-timeline / search screen). */
    private val _spawnables = MutableStateFlow<List<SpawnableUi>>(emptyList())
    val spawnables: StateFlow<List<SpawnableUi>> = _spawnables.asStateFlow()

    /** Per-account settings for the active account (Account Settings screen). */
    private val _accountSettings = MutableStateFlow<AccountSettingsUi?>(null)
    val accountSettings: StateFlow<AccountSettingsUi?> = _accountSettings.asStateFlow()

    /** post_result: true = sent OK. */
    private val _postResults = MutableSharedFlow<Boolean>(
        extraBufferCapacity = 4,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val postResults: SharedFlow<Boolean> = _postResults.asSharedFlow()

    /** Non-null when a newer version is available (from update_status). */
    private val _appUpdate = MutableStateFlow<AppUpdateUi?>(null)
    val appUpdate: StateFlow<AppUpdateUi?> = _appUpdate.asStateFlow()

    // Guards the once-per-launch startup update check (fired when settings first
    // arrive, so we can honour the check_updates_on_startup preference).
    private var startupUpdateChecked = false

    init {
        core.setEventListener { json -> onEvent(json) }
        core.dispatch("start") // load persisted accounts + timelines
    }

    private fun onEvent(e: JSONObject) {
        when (e.optString("event")) {
            "settings" -> {
                val settings = e.optJSONObject("settings")
                _settings.value = settings
                val sp = e.optJSONArray("soundpacks")
                _soundpacks.value = buildList {
                    if (sp != null) for (i in 0 until sp.length()) add(sp.optString(i))
                }
                // Quietly check GitHub once on launch if the preference is on. "stable"
                // forces a version-tag comparison (Android embeds no build commit for
                // the "latest" branch).
                if (!startupUpdateChecked) {
                    startupUpdateChecked = true
                    if (settings?.optBoolean("check_updates_on_startup", true) != false) {
                        core.dispatch("check_for_update") { put("silent", true); put("branch", "stable") }
                    }
                }
            }

            "account_settings" -> {
                val sp = e.optJSONArray("soundpacks")
                _accountSettings.value = AccountSettingsUi(
                    accountKey = e.optString("account_key"),
                    acct = e.optString("acct"),
                    soundpack = e.optString("soundpack"),
                    soundpacks = buildList {
                        if (sp != null) for (i in 0 until sp.length()) add(sp.optString(i))
                    },
                )
            }

            "accounts_changed" -> {
                val arr = e.optJSONArray("accounts")
                val list = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val a = arr.getJSONObject(i)
                        add(
                            AccountUi(
                                key = a.optString("key"),
                                handle = a.optString("handle"),
                                displayName = a.optString("display_name"),
                                platform = a.optString("platform"),
                            )
                        )
                    }
                }
                _accounts.value = list
                _selectedAccount.value = e.optString("selected")
            }

            "timelines_changed" -> {
                val arr = e.optJSONArray("timelines")
                val list = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val t = arr.getJSONObject(i)
                        add(
                            TabUi(
                                title = t.optString("title"),
                                kind = t.optString("kind"),
                                dismissable = t.optBoolean("dismissable"),
                                pinned = t.optBoolean("pinned"),
                                muted = t.optBoolean("muted"),
                            )
                        )
                    }
                }
                _tabs.value = list
                _currentTab.value = e.optInt("current", 0)
                // Drop stale row lists for indices that no longer exist.
                _rowsByTab.update { old -> old.filterKeys { it < list.size } }
            }

            "timeline_updated" -> {
                val index = e.optInt("index", -1)
                if (index < 0) return
                val arr = e.optJSONArray("rows")
                val rows = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val r = arr.getJSONObject(i)
                        add(
                            RowUi(
                                id = r.optString("id"),
                                text = r.optString("text"),
                                favorited = r.optBoolean("favorited"),
                                boosted = r.optBoolean("boosted"),
                                bookmarked = r.optBoolean("bookmarked"),
                                isMine = r.optBoolean("is_mine"),
                                hasMedia = r.optBoolean("has_media"),
                                isReply = r.optBoolean("is_reply"),
                                muted = r.optBoolean("muted"),
                                groupActors = r.optString("group_actors"),
                                favoritesCount = r.optInt("favorites_count"),
                                boostsCount = r.optInt("boosts_count"),
                            )
                        )
                    }
                }
                _rowsByTab.update { it + (index to rows) }
                _selectedIdByTab.update { it + (index to e.optString("selected_id")) }
            }

            "auth_result" ->
                _authResults.tryEmit(AuthResult(e.optBoolean("ok"), e.optString("error")))

            "compose_context" -> _composeContext.value = parseComposeContext(e)
            "post_result" -> _postResults.tryEmit(e.optBoolean("ok"))

            "profile_editor" -> {
                val arr = e.optJSONArray("fields")
                val fields = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val f = arr.getJSONObject(i)
                        add(ProfileFieldUi(f.optString("name"), f.optString("value")))
                    }
                }
                _profileEditor.value = ProfileEditorUi(
                    displayName = e.optString("display_name"),
                    note = e.optString("note"),
                    locked = e.optBoolean("locked"),
                    bot = e.optBoolean("bot"),
                    discoverable = e.optBoolean("discoverable"),
                    sensitive = e.optBoolean("sensitive"),
                    privacy = e.optString("privacy", "public"),
                    maxFields = e.optInt("max_fields", 4),
                    fields = fields,
                    simple = e.optBoolean("simple"),
                )
            }

            "user_picker" -> {
                val arr = e.optJSONArray("users")
                val users = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val u = arr.getJSONObject(i)
                        add(UserPick(u.optString("id"), u.optString("acct")))
                    }
                }
                _userPicker.value = UserPickerRequest(e.optString("purpose"), e.optString("id"), users)
            }

            "alias_prompt" ->
                _aliasPrompt.value =
                    AliasPromptRequest(e.optString("key"), e.optString("handle"), e.optString("current"))

            "aliases_list" -> {
                val arr = e.optJSONArray("aliases")
                _aliasesList.value = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val a = arr.getJSONObject(i)
                        add(AliasEntry(a.optString("key"), a.optString("handle"), a.optString("alias")))
                    }
                }
            }

            "trending_hashtags" -> {
                if (!e.optBoolean("supported", false)) {
                    _announcements.tryEmit("Trending hashtags are only available for Mastodon accounts.")
                } else {
                    val arr = e.optJSONArray("tags")
                    _trendingHashtags.value = buildList {
                        if (arr != null) for (i in 0 until arr.length()) {
                            val t = arr.getJSONObject(i)
                            add(
                                TrendingTagUi(
                                    name = t.optString("name"),
                                    url = t.optString("url"),
                                    following = t.optBoolean("following", false),
                                )
                            )
                        }
                    }
                }
            }

            "user_suggestions" -> {
                val arr = e.optJSONArray("users")
                val users = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val u = arr.getJSONObject(i)
                        val acct = u.optString("acct")
                        if (acct.isNotEmpty()) {
                            val label = u.optString("label").ifEmpty { "@$acct" }
                            add(MentionSuggestion(acct, label))
                        }
                    }
                }
                _mentionSuggestions.value = MentionSuggestions(e.optString("query"), users)
            }

            "user_profile" -> _profile.value = ProfileUi(
                text = e.optString("text"),
                accountId = e.optString("account_id"),
                acct = e.optString("acct"),
                url = e.optString("url"),
                hasRelationship = e.optBoolean("has_relationship"),
                following = e.optBoolean("following"),
                muting = e.optBoolean("muting"),
                blocking = e.optBoolean("blocking"),
                requested = e.optBoolean("requested"),
            )

            "media_open" -> _media.value =
                MediaItemUi(e.optString("url"), e.optString("kind"), e.optString("title"))

            "media_picker" -> {
                val arr = e.optJSONArray("items")
                _mediaPicker.value = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val m = arr.getJSONObject(i)
                        add(MediaItemUi(m.optString("url"), m.optString("kind"), m.optString("title")))
                    }
                }
            }

            "spawnable_timelines" -> {
                val arr = e.optJSONArray("timelines")
                _spawnables.value = buildList {
                    if (arr != null) for (i in 0 until arr.length()) {
                        val t = arr.getJSONObject(i)
                        add(
                            SpawnableUi(
                                kind = t.optString("kind"),
                                title = t.optString("title"),
                                input = t.optString("input").ifBlank { null },
                                param = t.optString("param").ifBlank { null },
                            )
                        )
                    }
                }
            }

            "announce" -> _announcements.tryEmit(e.optString("message"))
            "copy_to_clipboard" -> _copyText.tryEmit(e.optString("text"))
            "open_url" -> _openUrls.tryEmit(e.optString("url"))

            // Core asks us to move to a row (e.g. jump to a reply's parent): update
            // the current tab's selected id so the list scrolls there.
            "select_row" -> {
                val id = e.optString("id")
                if (id.isNotBlank()) {
                    val idx = _currentTab.value
                    _selectedIdByTab.update { it + (idx to id) }
                }
            }

            "update_status" -> {
                val available = e.optBoolean("available")
                val apkUrl = e.optString("apk_url")
                val silent = e.optBoolean("silent")
                if (available && apkUrl.isNotBlank()) {
                    _appUpdate.value = AppUpdateUi(
                        version = e.optString("version"),
                        notes = e.optString("notes"),
                        apkUrl = apkUrl,
                    )
                } else if (!silent) {
                    // A manual check with nothing to install: tell the user why.
                    val error = e.optString("error")
                    _announcements.tryEmit(
                        when {
                            error.isNotBlank() -> "Couldn't check for updates: $error"
                            available -> "An update is available, but no Android build was found."
                            else -> "FastSMRW is up to date."
                        }
                    )
                }
            }
        }
    }

    // --- Commands ---------------------------------------------------------

    fun addBluesky(handle: String, appPassword: String, service: String = "https://bsky.social") {
        core.dispatch("add_account") {
            put("platform", "bluesky")
            put("service", service)
            put("handle", handle)
            put("app_password", appPassword)
        }
    }

    /**
     * Mastodon interactive login, step 1: register the app and get an authorize
     * URL. The core replies with an [openUrls] emission; we open it in a Custom
     * Tab, and the fastsm://oauth redirect drives [finishMastodonLogin].
     */
    fun beginMastodonLogin(instance: String) =
        core.dispatch("begin_mastodon_login") { put("instance", instance) }

    /** Mastodon login, step 2: exchange the redirect's code for a session. */
    fun finishMastodonLogin(code: String) =
        core.dispatch("finish_mastodon_login") { put("code", code) }

    fun switchAccount(key: String) = core.dispatch("select_account") { put("key", key) }

    fun removeAccount(key: String) = core.dispatch("remove_account") { put("key", key) }

    fun selectTimeline(index: Int) {
        _currentTab.value = index
        core.dispatch("select_timeline") { put("index", index) }
    }

    fun refresh() = core.dispatch("refresh")

    fun refreshAll() = core.dispatch("refresh_all")

    /** Open a post's conversation as a new tab. */
    fun openThread(id: String) = core.dispatch("open_thread") { put("id", id) }

    /** Open a post author's timeline as a new tab (may raise a user_picker). */
    fun openUserTimeline(rowId: String) =
        core.dispatch("open_user_timeline") { put("id", rowId) }

    /** Open a specific picked user's timeline. */
    fun openUserTimelinePicked(accountId: String, acct: String) {
        _userPicker.value = null
        core.dispatch("open_user_timeline") { put("account_id", accountId); put("acct", acct) }
    }

    /** Open a post author's profile (may raise a user_picker). */
    fun openUserProfile(rowId: String) =
        core.dispatch("open_user_profile") { put("id", rowId) }

    /** Search accounts for @-mention autocomplete; results arrive on mentionSuggestions. */
    fun autocompleteUsers(query: String) = core.dispatch("autocomplete_users") { put("query", query) }

    /** Clear the current @-mention suggestions (e.g. when the picker closes). */
    fun clearMentionSuggestions() { _mentionSuggestions.value = null }

    /** Speak the post's user info (one user), or open a timeline of its users. */
    fun speakUser(rowId: String) = core.dispatch("speak_user") { put("id", rowId) }

    /** Speak the post this reply is replying to. */
    fun speakReply(rowId: String) = core.dispatch("speak_reply") { put("id", rowId); put("jump", false) }

    /** Jump to the post this reply is replying to (select it, or open the thread). */
    fun jumpToReply(rowId: String) = core.dispatch("speak_reply") { put("id", rowId); put("jump", true) }

    /** Open a specific picked user's profile. */
    fun openUserProfilePicked(rowId: String, accountId: String) {
        _userPicker.value = null
        core.dispatch("open_user_profile") { put("id", rowId); put("account_id", accountId) }
    }

    fun setRelationship(accountId: String, action: String, acct: String) =
        core.dispatch("set_relationship") {
            put("account_id", accountId); put("action", action); put("acct", acct)
        }

    fun closeProfile() {
        _profile.value = null
    }

    fun dismissUserPicker() {
        _userPicker.value = null
    }

    /** Add/edit the alias for a post's user (may raise a user_picker if several). */
    fun beginAlias(rowId: String) =
        core.dispatch("begin_alias") { put("id", rowId); put("pick", true) }

    /** Add/edit the alias for a specific picked user. */
    fun beginAliasPicked(rowId: String, accountId: String) {
        _userPicker.value = null
        core.dispatch("begin_alias") { put("id", rowId); put("account_id", accountId) }
    }

    fun setAlias(key: String, handle: String, alias: String) =
        core.dispatch("set_alias") { put("key", key); put("handle", handle); put("alias", alias) }

    fun clearAlias(key: String, handle: String) =
        core.dispatch("clear_alias") { put("key", key); put("handle", handle) }

    /** Ask the core for the full alias list (opens/refreshes the aliases manager). */
    fun listAliases() = core.dispatch("list_aliases")

    /**
     * User Analysis: fetch the current account's complete followers/following
     * lists and spawn a user timeline of the chosen category ("not_following_back"
     * | "no_followback" | "mutuals"). The core announces an error and spawns
     * nothing if the lists can't be fully loaded (rate limit / failure).
     */
    fun analyzeUsers(category: String) = core.dispatch("analyze_users") { put("category", category) }

    fun dismissAliasPrompt() { _aliasPrompt.value = null }
    fun dismissAliasesList() { _aliasesList.value = null }

    /** Open the Trending Hashtags picker (the core replies with trending_hashtags). */
    fun listTrendingHashtags() = core.dispatch("list_trending_hashtags")
    fun dismissTrendingHashtags() { _trendingHashtags.value = null }

    /** Follow a hashtag by name (no '#'). */
    fun followHashtag(name: String) = core.dispatch("follow_hashtag") { put("name", name) }

    /** Close a tab: select it (so it's current), then dismiss it. */
    fun closeTimeline(index: Int) {
        selectTimeline(index)
        core.dispatch("close_timeline")
    }

    /** Pin/unpin a tab (pinning locks it from being closed). */
    fun pinTimeline(index: Int) {
        selectTimeline(index)
        core.dispatch("toggle_pin")
    }

    /** Mute/unmute a tab's new-item earcon. */
    fun muteTimeline(index: Int) {
        selectTimeline(index)
        core.dispatch("toggle_mute")
    }

    /** Move a tab earlier ("up") or later ("down") in the tab order. */
    fun moveTimeline(index: Int, dir: String) {
        selectTimeline(index)
        core.dispatch("reorder_timeline") { put("dir", dir) }
    }

    /** Load per-account settings for the active account. */
    fun loadAccountSettings() = core.dispatch("get_account_settings")

    /** Set the active account's soundpack. */
    fun setAccountSoundpack(pack: String) =
        core.dispatch("set_account_settings") { put("soundpack", pack) }

    /** Ask the core which timelines can be opened (for the Add-timeline screen). */
    fun loadSpawnable() = core.dispatch("get_spawnable")

    /** Open a new timeline (search query / hashtag / list / built-in) as a tab. */
    fun spawnTimeline(kind: String, value: String? = null, param: String? = null) =
        core.dispatch("spawn_timeline") {
            put("kind", kind)
            value?.let { put("value", it) }
            param?.let { put("param", it) }
        }

    /**
     * Fetch older posts at the end of the current timeline. Scroll-triggered loads
     * pass [automatic] so the core can stop paging a sparse feed (mentions) that
     * keeps returning posts we already have.
     */
    fun loadOlder(automatic: Boolean = false) =
        core.dispatch("load_older") { put("automatic", automatic) }

    /** Report the reading position on the current timeline (persists it). */
    fun noteSelection(id: String) = core.dispatch("note_selection") { put("id", id) }

    fun toggleFavorite(id: String) = core.dispatch("toggle_favorite") { put("id", id) }

    fun toggleBoost(id: String) = core.dispatch("toggle_boost") { put("id", id) }

    fun toggleBookmark(id: String) = core.dispatch("toggle_bookmark") { put("id", id) }

    /** Toggle auto-reading of new posts for the current timeline. */
    fun toggleAutoRead() = core.dispatch("toggle_auto_read")
    /** Copy the focused post/user/notification to the clipboard. */
    fun copyRow(id: String) = core.dispatch("copy") { put("id", id) }

    /** Open the Edit Profile dialog (the core replies with a profile_editor event). */
    fun openProfileEditor() = core.dispatch("open_profile_editor")
    fun dismissProfileEditor() { _profileEditor.value = null }
    fun updateProfile(displayName: String, note: String, locked: Boolean, bot: Boolean,
                      discoverable: Boolean, sensitive: Boolean, privacy: String,
                      fields: List<ProfileFieldUi>) {
        core.dispatch("update_profile") {
            put("display_name", displayName)
            put("note", note)
            put("locked", locked)
            put("bot", bot)
            put("discoverable", discoverable)
            put("sensitive", sensitive)
            put("privacy", privacy)
            val arr = org.json.JSONArray()
            for (f in fields) arr.put(org.json.JSONObject().put("name", f.name).put("value", f.value))
            put("fields", arr)
        }
    }

    /** Report a post to the server's moderators. */
    fun reportPost(id: String, category: String, comment: String, forward: Boolean) =
        core.dispatch("report") {
            put("id", id)
            put("category", category)
            put("forward", forward)
            if (comment.isNotBlank()) put("comment", comment)
        }

    /** Report a user to the server's moderators. */
    fun reportUser(accountId: String, acct: String, category: String, comment: String, forward: Boolean) =
        core.dispatch("report") {
            put("account_id", accountId)
            put("acct", acct)
            put("category", category)
            put("forward", forward)
            if (comment.isNotBlank()) put("comment", comment)
        }

    /** Mute/unmute the conversation (thread) a post belongs to (Mastodon). */
    fun toggleMuteConversation(id: String) =
        core.dispatch("toggle_mute_conversation") { put("id", id) }

    fun openPostInfo(id: String) = core.dispatch("post_info") { put("id", id) }

    /** Open a user list of everyone who favorited / boosted a post (Mastodon). */
    fun openFavoritedBy(id: String) = core.dispatch("open_favorited_by") { put("id", id) }

    fun openRebloggedBy(id: String) = core.dispatch("open_reblogged_by") { put("id", id) }

    // --- Media ------------------------------------------------------------

    /** View a post's media (single opens directly; multiple raises a chooser). */
    fun playMedia(id: String) = core.dispatch("play_media") { put("id", id) }

    /** View a specific chosen attachment. */
    fun playMediaItem(item: MediaItemUi) {
        _mediaPicker.value = null
        core.dispatch("play_media") {
            put("url", item.url); put("kind", item.kind); put("title", item.title)
        }
    }

    fun closeMedia() {
        _media.value = null
    }

    fun dismissMediaPicker() {
        _mediaPicker.value = null
    }

    // --- Updates ----------------------------------------------------------

    /** Manually check GitHub for a newer release (announces the outcome). */
    fun checkForUpdate() =
        core.dispatch("check_for_update") { put("silent", false); put("branch", "stable") }

    /** Open the APK download for the pending update in the browser, then dismiss. */
    fun openUpdate() {
        val url = _appUpdate.value?.apkUrl ?: return
        _openUrls.tryEmit(url)
        _appUpdate.value = null
    }

    /** Dismiss the update prompt for now (re-offered on the next launch/check). */
    fun dismissUpdate() {
        _appUpdate.value = null
    }

    // --- Settings ---------------------------------------------------------

    /**
     * Apply an edit and send the whole settings object back (the core resets any
     * omitted field to its default, so we must round-trip the full object).
     */
    fun updateSetting(apply: JSONObject.() -> Unit) {
        val current = _settings.value ?: return
        val copy = JSONObject(current.toString())
        copy.apply()
        core.dispatch("update_settings") { put("settings", copy) }
    }

    /** Toggle one field's "enabled" in a speech list (status/notification/user). */
    fun toggleSpeechField(list: String, field: String, enabled: Boolean) {
        updateSetting {
            val speech = optJSONObject("speech") ?: return@updateSetting
            val arr = speech.optJSONArray(list) ?: return@updateSetting
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                if (o.optString("field") == field) {
                    o.put("enabled", enabled)
                    break
                }
            }
        }
    }

    /** Move a speech field up (delta -1) or down (+1), changing spoken order. */
    fun moveSpeechField(list: String, index: Int, delta: Int) {
        updateSetting {
            val arr = optJSONObject("speech")?.optJSONArray(list) ?: return@updateSetting
            val target = index + delta
            if (index !in 0 until arr.length() || target !in 0 until arr.length()) return@updateSetting
            val a = arr.getJSONObject(index)
            val b = arr.getJSONObject(target)
            arr.put(index, b)
            arr.put(target, a)
        }
    }

    /**
     * Set the optional text spoken before/after a field's value, and whether the
     * separator is suppressed after this field.
     */
    fun setSpeechWrap(
        list: String,
        field: String,
        before: String,
        after: String,
        noSeparatorAfter: Boolean,
    ) {
        updateSetting {
            val arr = optJSONObject("speech")?.optJSONArray(list) ?: return@updateSetting
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                if (o.optString("field") == field) {
                    // Only drop when truly empty — a lone space is a valid value
                    // (it separates a field's before/after text), same as Win/Mac.
                    if (before.isEmpty()) o.remove("before") else o.put("before", before)
                    if (after.isEmpty()) o.remove("after") else o.put("after", after)
                    if (noSeparatorAfter) o.put("no_separator_after", true)
                    else o.remove("no_separator_after")
                    break
                }
            }
        }
    }

    // --- Compose ----------------------------------------------------------

    fun composeNew() = core.dispatch("compose_context") { put("mode", "new") }

    fun composeReply(id: String) = core.dispatch("compose_context") {
        put("mode", "reply"); put("id", id)
    }

    fun composeQuote(id: String) = core.dispatch("compose_context") {
        put("mode", "quote"); put("id", id)
    }

    fun composeEdit(id: String) = core.dispatch("compose_context") {
        put("mode", "edit"); put("id", id)
    }

    fun deletePost(id: String) = core.dispatch("delete_post") { put("id", id) }

    fun cancelCompose() {
        _composeContext.value = null
    }

    /** Build a draft from the composer fields + the open context, and post it. */
    fun sendPost(
        text: String,
        cw: String,
        visibility: Int,
        mentions: List<String>,
        media: List<OutgoingMedia> = emptyList(),
    ) {
        val ctx = _composeContext.value ?: return
        val draft = JSONObject().apply {
            put("text", text)
            if (cw.isNotBlank()) put("spoiler_text", cw)
            if (ctx.featureVisibility && visibility >= 0) put("visibility", visibility)
            if (mentions.isNotEmpty()) put("mentions", JSONArray(mentions))
            if (media.isNotEmpty()) {
                put("attachments", JSONArray().apply {
                    media.forEach { m ->
                        put(JSONObject().apply {
                            put("filename", m.filename)
                            put("mime", m.mime)
                            put("alt", m.alt)
                            put("data", m.dataBase64)
                        })
                    }
                })
            }
            ctx.replyToId?.let { put("reply_to_id", it) }
            ctx.replyToUrl?.let { put("reply_to_url", it) }
            ctx.quotedStatusId?.let { put("quoted_status_id", it) }
            ctx.quotedStatusCid?.let { put("quoted_status_cid", it) }
            ctx.quotedStatusUrl?.let { put("quoted_status_url", it) }
        }
        core.dispatch("post") {
            put("draft", draft)
            ctx.editId?.let { put("edit_id", it) }
        }
    }

    private fun parseComposeContext(e: JSONObject): ComposeContext {
        val features = e.optJSONObject("features")
        val partsArr = e.optJSONArray("reply_participants")
        val participants = buildList {
            if (partsArr != null) for (i in 0 until partsArr.length()) {
                val p = partsArr.getJSONObject(i)
                add(
                    ComposeParticipant(
                        acct = p.optString("acct"),
                        displayName = p.optString("display_name"),
                        checked = p.optBoolean("checked"),
                    )
                )
            }
        }
        fun nullable(key: String) = e.optString(key).ifBlank { null }
        return ComposeContext(
            mode = e.optString("mode", "new"),
            title = e.optString("title", "New Post"),
            contextLabel = nullable("context_label"),
            maxChars = e.optInt("max_chars", 500),
            platform = e.optString("platform"),
            featureContentWarning = features?.optBoolean("content_warning") ?: false,
            featureVisibility = features?.optBoolean("visibility") ?: false,
            featureEditing = features?.optBoolean("editing") ?: false,
            featureMedia = features?.optBoolean("media") ?: false,
            defaultVisibility = e.optInt("default_visibility", -1),
            prefillText = e.optString("prefill_text"),
            prefillCw = e.optString("prefill_cw"),
            replyToId = nullable("reply_to_id"),
            replyToUrl = nullable("reply_to_url"),
            quotedStatusId = nullable("quoted_status_id"),
            quotedStatusCid = nullable("quoted_status_cid"),
            quotedStatusUrl = nullable("quoted_status_url"),
            editId = nullable("edit_id"),
            participants = participants,
        )
    }

    override fun onCleared() {
        core.destroy()
        super.onCleared()
    }
}
