package me.masonasons.fastsm.ui.settings

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import me.masonasons.fastsm.core.FastSmCore
import me.masonasons.fastsm.ui.CoreViewModel
import org.json.JSONObject
import kotlin.math.roundToInt

private const val CACHE_MAX = 20000
private const val FETCH_MIN = 1
private const val FETCH_MAX = 10

private val autoRefreshOptions = listOf(
    0 to "Off", 30 to "Every 30 seconds", 60 to "Every minute",
    120 to "Every 2 minutes", 300 to "Every 5 minutes",
)
private val cwOptions = listOf(
    "hide" to "Hide post text (show warning only)",
    "show" to "Show warning, then post text",
    "ignore" to "Ignore warning (show post text)",
)
private val emojiOptions = listOf(
    "none" to "Off", "unicode" to "Unicode emoji",
    "mastodon" to "Custom (:shortcode:)", "both" to "Both",
)

private val statusFieldLabels = mapOf(
    "boostedBy" to "Boosted by", "author" to "Author name", "handle" to "Handle (@user)",
    "contentWarning" to "Content warning", "text" to "Post text", "quote" to "Quoted post",
    "media" to "Media / attachments", "poll" to "Poll", "time" to "Time",
    "stats" to "Reply / boost / favorite counts", "favorited" to "Favorited state",
    "boosted" to "Boosted state", "visibility" to "Visibility", "source" to "Posting app / source",
)
private val userFieldLabels = mapOf(
    "author" to "Display name", "handle" to "Handle (@user)", "bot" to "Bot indicator",
    "locked" to "Locked indicator", "bio" to "Bio", "followers" to "Followers count",
    "following" to "Following count", "posts" to "Posts count",
)
private val notificationFieldLabels = mapOf(
    "actor" to "Who (name)", "action" to "What they did", "handle" to "Handle (@user)",
    "text" to "Related post text", "time" to "Time",
)

private fun fieldLabel(list: String, field: String): String = when (list) {
    "status" -> statusFieldLabels
    "user" -> userFieldLabels
    else -> notificationFieldLabels
}[field] ?: field

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(viewModel: CoreViewModel, onClose: () -> Unit) {
    val settings by viewModel.settings.collectAsStateWithLifecycle()
    val soundpacks by viewModel.soundpacks.collectAsStateWithLifecycle()

    // Two-level navigation: root category list -> a panel -> (speech) a field editor.
    var panel by remember { mutableStateOf<String?>(null) }
    var speechList by remember { mutableStateOf<String?>(null) }

    val goBack: () -> Unit = {
        when {
            speechList != null -> speechList = null
            panel != null -> panel = null
            else -> onClose()
        }
    }
    BackHandler(enabled = true, onBack = goBack)

    val s = settings
    val title = when {
        speechList == "status" -> "Posts"
        speechList == "user" -> "Users"
        speechList == "notification" -> "Notifications"
        panel == "timelines" -> "Timelines"
        panel == "audio" -> "Audio"
        panel == "speech" -> "Speech"
        panel == "advanced" -> "Advanced"
        panel == "confirmation" -> "Confirmation"
        panel == "behavior" -> "Behavior"
        panel == "updates" -> "Updates"
        else -> "Settings"
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(title) },
                navigationIcon = {
                    IconButton(onClick = goBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
            )
        },
    ) { pad ->
        if (s == null) {
            Box(Modifier.fillMaxSize().padding(pad), contentAlignment = Alignment.Center) {
                CircularProgressIndicator()
            }
            return@Scaffold
        }
        Column(
            modifier = Modifier.fillMaxSize().padding(pad).verticalScroll(rememberScrollState()),
        ) {
            when {
                speechList != null -> SpeechFieldEditor(s, speechList!!, viewModel)
                panel == "timelines" -> TimelinesPanel(s, viewModel)
                panel == "audio" -> AudioPanel(s, soundpacks, viewModel)
                panel == "speech" -> SpeechPanel(s, viewModel) { speechList = it }
                panel == "advanced" -> AdvancedPanel(s, viewModel)
                panel == "confirmation" -> ConfirmationPanel(s, viewModel)
                panel == "behavior" -> BehaviorPanel(s, viewModel)
                panel == "updates" -> UpdatesPanel(s, viewModel)
                else -> RootList { panel = it }
            }
        }
    }
}

@Composable
private fun RootList(onOpen: (String) -> Unit) {
    listOf(
        "timelines" to "Timelines",
        "audio" to "Audio",
        "speech" to "Speech",
        "advanced" to "Advanced",
        "confirmation" to "Confirmation",
        "behavior" to "Behavior",
        "updates" to "Updates",
    ).forEach { (key, label) ->
        Text(
            label,
            style = MaterialTheme.typography.titleLarge,
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onOpen(key) }
                .padding(horizontal = 16.dp, vertical = 16.dp),
        )
        HorizontalDivider()
    }
}

@Composable
private fun TimelinesPanel(s: JSONObject, vm: CoreViewModel) {
    NumberField(
        "Maximum posts to cache per timeline", s.optInt("cache_limit"), 0, CACHE_MAX,
        "How many posts to keep saved per timeline for instant startup (0-20000, 0 turns " +
            "caching off). This is storage, not how many load at once — see Advanced.",
    ) { vm.updateSetting { put("cache_limit", it) } }
    ComboRow("Auto-refresh", autoRefreshOptions, s.optInt("auto_refresh_seconds")) {
        vm.updateSetting { put("auto_refresh_seconds", it) }
    }
    HelpText("Check timelines for new posts on this interval; new posts play that timeline's sound.")
    SwitchRow("Stream in real time (Mastodon)", s.optBoolean("streaming_enabled")) {
        vm.updateSetting { put("streaming_enabled", it) }
    }
    SwitchRow("Show mentions in the Notifications timeline", s.optBoolean("show_mentions_in_notifications")) {
        vm.updateSetting { put("show_mentions_in_notifications", it) }
    }
    SwitchRow("Reverse timelines (newest at the bottom)", s.optBoolean("reverse_timelines")) {
        vm.updateSetting { put("reverse_timelines", it) }
    }
    SwitchRow("Automatically load older posts when you reach the end", s.optBoolean("auto_load_older")) {
        vm.updateSetting { put("auto_load_older", it) }
    }
}

@Composable
private fun AudioPanel(s: JSONObject, soundpacks: List<String>, vm: CoreViewModel) {
    SwitchRow("Play sounds", s.optBoolean("sounds_enabled")) {
        vm.updateSetting { put("sounds_enabled", it) }
    }
    if (soundpacks.isNotEmpty()) {
        ComboRow("Soundpack", soundpacks.map { it to it }, s.optString("soundpack")) {
            vm.updateSetting { put("soundpack", it) }
        }
    }
    HelpText("A Default pack is built in. Add your own pack to the soundpacks folder, then pick it here.")
    SliderRow("Volume", s.optInt("sound_volume", 100)) {
        vm.updateSetting { put("sound_volume", it) }
    }
    SwitchRow("Play a sound at the top or bottom of a timeline (in the window)", s.optBoolean("boundary_sound")) {
        vm.updateSetting { put("boundary_sound", it) }
    }
}

@Composable
private fun SpeechPanel(s: JSONObject, vm: CoreViewModel, onConfigure: (String) -> Unit) {
    HelpText("Choose which details the screen reader speaks, and their order, for each kind of row:")
    ActionRow("Configure Posts…") { onConfigure("status") }
    ActionRow("Configure Users…") { onConfigure("user") }
    ActionRow("Configure Notifications…") { onConfigure("notification") }
    ComboRow("Content warnings", cwOptions, s.optString("cw_mode")) {
        vm.updateSetting { put("cw_mode", it) }
    }
    ComboRow("Remove emoji from posts", emojiOptions, s.optString("post_emoji_removal")) {
        vm.updateSetting { put("post_emoji_removal", it) }
    }
    ComboRow("Remove emoji from names", emojiOptions, s.optString("name_emoji_removal")) {
        vm.updateSetting { put("name_emoji_removal", it) }
    }
    NumberField("Max usernames in a post (0 = all)", s.optInt("max_usernames_in_post"), 0, 999, null) {
        vm.updateSetting { put("max_usernames_in_post", it) }
    }
    SwitchRow("Use absolute time (clock time), not relative", s.optBoolean("absolute_time")) {
        vm.updateSetting { put("absolute_time", it) }
    }
    val separator = s.optJSONObject("speech")?.optString("separator") ?: ", "
    TextRow("Separator spoken between items", separator) { v ->
        vm.updateSetting {
            optJSONObject("speech")?.put("separator", v)
        }
    }
}

@Composable
private fun AdvancedPanel(s: JSONObject, vm: CoreViewModel) {
    NumberField(
        "API calls per timeline load (1-10)", s.optInt("fetch_pages", 3), FETCH_MIN, FETCH_MAX,
        "Posts loaded per refresh is about 40 × this number. Raise it to load more at once " +
            "(slower). Applies to refresh and scrollback.",
    ) { vm.updateSetting { put("fetch_pages", it) } }
}

@Composable
private fun BehaviorPanel(s: JSONObject, vm: CoreViewModel) {
    SwitchRow("Put extra reply mentions at the end", s.optBoolean("reply_mentions_at_end")) {
        vm.updateSetting { put("reply_mentions_at_end", it) }
    }
    HelpText("In a reply, mention the person you're replying to up front and move the other mentioned users to the end of the post.")
}

@Composable
private fun UpdatesPanel(s: JSONObject, vm: CoreViewModel) {
    SwitchRow("Check for updates when FastSMRW starts", s.optBoolean("check_updates_on_startup", true)) {
        vm.updateSetting { put("check_updates_on_startup", it) }
    }
    HorizontalDivider()
    ActionRow("Check for updates now") { vm.checkForUpdate() }
    HelpText("You're running FastSMRW ${FastSmCore.version}. Updates download from GitHub; tap the downloaded APK to install.")
}

@Composable
private fun ConfirmationPanel(s: JSONObject, vm: CoreViewModel) {
    HelpText("Show a confirmation before:")
    listOf(
        "confirm_boost" to "Boosting",
        "confirm_unboost" to "Unboosting",
        "confirm_favorite" to "Favoriting",
        "confirm_unfavorite" to "Unfavoriting",
        "confirm_clear_timeline" to "Clearing a timeline",
        "confirm_block" to "Blocking a user",
        "confirm_unblock" to "Unblocking a user",
        "confirm_delete_post" to "Deleting a post",
    ).forEach { (key, label) ->
        SwitchRow(label, s.optBoolean(key)) { vm.updateSetting { put(key, it) } }
    }
}

@Composable
private fun SpeechFieldEditor(s: JSONObject, list: String, vm: CoreViewModel) {
    val arr = s.optJSONObject("speech")?.optJSONArray(list) ?: return
    val n = arr.length()
    // A field currently being edited for its before/after wrap text (dialog open).
    var editing by remember { mutableStateOf<String?>(null) }

    HelpText("Double-tap to toggle whether a detail is spoken; use the item's actions to move it up or down or set extra spoken text.")
    for (i in 0 until n) {
        val o = arr.getJSONObject(i)
        val field = o.optString("field")
        val enabled = o.optBoolean("enabled")

        // key(field) keeps this row's semantics node identity stable across
        // reordering, so TalkBack focus follows the moved item to its new spot
        // instead of staying on whatever shifts into the old position.
        key(field) {
            // One TalkBack node: double-tap toggles spoken/muted; Move up/down and
            // Edit extra text are custom actions. The visible switch and arrows stay
            // for touch but are hidden from TalkBack by clearAndSetSemantics.
            val actions = buildList {
                add(CustomAccessibilityAction(if (enabled) "Do not speak this" else "Speak this") {
                    vm.toggleSpeechField(list, field, !enabled); true
                })
                if (i > 0) add(CustomAccessibilityAction("Move up") { vm.moveSpeechField(list, i, -1); true })
                if (i < n - 1) add(CustomAccessibilityAction("Move down") { vm.moveSpeechField(list, i, +1); true })
                add(CustomAccessibilityAction("Edit extra spoken text") { editing = field; true })
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clearAndSetSemantics {
                        contentDescription =
                            "${fieldLabel(list, field)}, ${if (enabled) "spoken" else "not spoken"}, " +
                                "position ${i + 1} of $n"
                        customActions = actions
                        onClick { vm.toggleSpeechField(list, field, !enabled); true }
                    }
                    .padding(start = 16.dp, top = 4.dp, end = 4.dp, bottom = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Switch(
                    checked = enabled,
                    onCheckedChange = { vm.toggleSpeechField(list, field, it) },
                )
                Text(
                    fieldLabel(list, field),
                    style = MaterialTheme.typography.bodyLarge,
                    modifier = Modifier.weight(1f).padding(start = 8.dp),
                )
                IconButton(onClick = { vm.moveSpeechField(list, i, -1) }, enabled = i > 0) {
                    Icon(Icons.Filled.KeyboardArrowUp, contentDescription = null)
                }
                IconButton(onClick = { vm.moveSpeechField(list, i, +1) }, enabled = i < n - 1) {
                    Icon(Icons.Filled.KeyboardArrowDown, contentDescription = null)
                }
            }
            HorizontalDivider()
        }
    }

    editing?.let { field ->
        val o = (0 until arr.length())
            .map { arr.getJSONObject(it) }
            .firstOrNull { it.optString("field") == field }
        WrapDialog(
            fieldLabel = fieldLabel(list, field),
            before = o?.optString("before") ?: "",
            after = o?.optString("after") ?: "",
            noSeparatorAfter = o?.optBoolean("no_separator_after") ?: false,
            onSave = { b, a, noSep ->
                vm.setSpeechWrap(list, field, b, a, noSep); editing = null
            },
            onDismiss = { editing = null },
        )
    }
}

@Composable
private fun WrapDialog(
    fieldLabel: String,
    before: String,
    after: String,
    noSeparatorAfter: Boolean,
    onSave: (String, String, Boolean) -> Unit,
    onDismiss: () -> Unit,
) {
    var b by remember { mutableStateOf(before) }
    var a by remember { mutableStateOf(after) }
    var noSep by remember { mutableStateOf(noSeparatorAfter) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Extra spoken text: $fieldLabel") },
        text = {
            Column {
                OutlinedTextField(
                    value = b,
                    onValueChange = { b = it },
                    label = { Text("Speak before") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = a,
                    onValueChange = { a = it },
                    label = { Text("Speak after") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                )
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .toggleable(
                            value = noSep,
                            role = Role.Switch,
                            onValueChange = { noSep = it },
                        )
                        .padding(top = 8.dp, bottom = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        "No separator after this field",
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.weight(1f),
                    )
                    Switch(checked = noSep, onCheckedChange = null)
                }
            }
        },
        confirmButton = { TextButton(onClick = { onSave(b, a, noSep) }) { Text("Save") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

// --- Shared rows ----------------------------------------------------------

@Composable
private fun SwitchRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .toggleable(value = checked, role = Role.Switch, onValueChange = onChange)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
        Switch(checked = checked, onCheckedChange = null)
    }
}

@Composable
private fun ActionRow(label: String, onClick: () -> Unit) {
    Text(
        label,
        style = MaterialTheme.typography.bodyLarge,
        color = MaterialTheme.colorScheme.primary,
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 14.dp),
    )
}

@Composable
private fun <T> ComboRow(
    label: String,
    options: List<Pair<T, String>>,
    selected: T,
    onSelect: (T) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    val current = options.firstOrNull { it.first == selected }?.second ?: ""
    Box {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { open = true }
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(label, style = MaterialTheme.typography.labelLarge)
                Text(
                    current,
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { (value, optLabel) ->
                DropdownMenuItem(
                    text = { Text(optLabel) },
                    onClick = { onSelect(value); open = false },
                )
            }
        }
    }
}

@Composable
private fun NumberField(
    label: String,
    value: Int,
    min: Int,
    max: Int,
    help: String?,
    onCommit: (Int) -> Unit,
) {
    var text by remember(value) { mutableStateOf(value.toString()) }
    Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        OutlinedTextField(
            value = text,
            onValueChange = { text = it.filter { c -> c.isDigit() } },
            label = { Text(label) },
            singleLine = true,
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier
                .fillMaxWidth()
                .onFocusChanged {
                    if (!it.isFocused) {
                        val v = (text.toIntOrNull() ?: value).coerceIn(min, max)
                        if (v != value) onCommit(v)
                        text = v.toString()
                    }
                },
        )
        help?.let { HelpText(it) }
    }
}

@Composable
private fun TextRow(label: String, value: String, onCommit: (String) -> Unit) {
    var text by remember(value) { mutableStateOf(value) }
    OutlinedTextField(
        value = text,
        onValueChange = { text = it },
        label = { Text(label) },
        singleLine = true,
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .onFocusChanged { if (!it.isFocused && text != value) onCommit(text) },
    )
}

@Composable
private fun SliderRow(label: String, value: Int, onChange: (Int) -> Unit) {
    var pos by remember(value) { mutableFloatStateOf(value.toFloat()) }
    Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        Text("$label: ${pos.roundToInt()}", style = MaterialTheme.typography.bodyLarge)
        Slider(
            value = pos,
            onValueChange = { pos = it },
            onValueChangeFinished = { onChange(pos.roundToInt()) },
            valueRange = 0f..100f,
        )
    }
}

@Composable
private fun HelpText(text: String) {
    Text(
        text,
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp),
    )
}
