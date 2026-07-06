package me.masonasons.fastsm.ui.compose

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Base64
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.masonasons.fastsm.ui.ComposeContext
import me.masonasons.fastsm.ui.CoreViewModel
import me.masonasons.fastsm.ui.MentionSuggestion
import me.masonasons.fastsm.ui.OutgoingMedia

private data class PickedMedia(val uri: Uri, val name: String, val mime: String, val alt: String)

/** Resolve a content Uri's display name + MIME type for the upload. */
private fun queryMeta(context: Context, uri: Uri): Pair<String, String> {
    val mime = context.contentResolver.getType(uri) ?: "application/octet-stream"
    var name = "attachment"
    runCatching {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { c ->
                if (c.moveToFirst()) {
                    val idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) c.getString(idx)?.let { name = it }
                }
            }
    }
    return name to mime
}

private fun visibilityLabel(v: Int): String = when (v) {
    0 -> "Public"
    1 -> "Unlisted"
    2 -> "Followers only"
    3 -> "Mentioned people only"
    else -> "Public"
}

/**
 * The composer, driven entirely by the core's compose_context: it fills in the
 * reply/quote/edit target, char limit, and platform capabilities, and `post`
 * does the sending. Media and polls come in a later pass.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ComposeScreen(
    viewModel: CoreViewModel,
    ctx: ComposeContext,
    onDone: () -> Unit,
) {
    var field by remember {
        mutableStateOf(TextFieldValue(ctx.prefillText, TextRange(ctx.prefillText.length)))
    }
    var cw by remember { mutableStateOf(ctx.prefillCw) }
    // @-mention picker: open flag, the seed query, and the [start, caret) span of
    // the partial handle under the caret that a pick replaces.
    var mentionOpen by remember { mutableStateOf(false) }
    var mentionSeed by remember { mutableStateOf("") }
    var mentionAnchor by remember { mutableStateOf(0 to 0) }
    var visibility by remember {
        mutableIntStateOf(if (ctx.defaultVisibility in 0..3) ctx.defaultVisibility else 0)
    }
    val checked = remember {
        mutableStateMapOf<String, Boolean>().apply {
            ctx.participants.forEach { put(it.acct, it.checked) }
        }
    }
    var sending by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }

    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val attachments = remember { mutableStateListOf<PickedMedia>() }
    val picker = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments(),
    ) { uris ->
        uris.forEach { uri ->
            val (name, mime) = queryMeta(context, uri)
            attachments.add(PickedMedia(uri, name, mime, ""))
        }
    }

    LaunchedEffect(viewModel) {
        viewModel.postResults.collect { ok ->
            sending = false
            if (ok) onDone() else error = "Could not send. Please try again."
        }
    }

    BackHandler(enabled = true) { onDone() }

    val remaining = ctx.maxChars - field.text.length
    val canSend = (field.text.isNotBlank() || attachments.isNotEmpty()) && remaining >= 0 && !sending

    fun send() {
        error = null
        sending = true
        val mentions = ctx.participants.filter { checked[it.acct] == true }.map { it.acct }
        val picked = attachments.toList()
        val body = field.text
        scope.launch {
            // Read + base64-encode off the main thread; the binary bridge uploads.
            val media = withContext(Dispatchers.IO) {
                picked.mapNotNull { m ->
                    val bytes = runCatching {
                        context.contentResolver.openInputStream(m.uri)?.use { it.readBytes() }
                    }.getOrNull() ?: return@mapNotNull null
                    OutgoingMedia(m.name, m.mime, m.alt, Base64.encodeToString(bytes, Base64.NO_WRAP))
                }
            }
            viewModel.sendPost(text = body, cw = cw, visibility = visibility, mentions = mentions, media = media)
        }
    }

    // Open the @-mention picker, seeded from the handle word under the caret.
    fun openMention() {
        val t = field.text
        val caret = field.selection.end.coerceIn(0, t.length)
        var start = caret
        while (start > 0 && isHandleChar(t[start - 1])) start--
        mentionAnchor = start to caret
        mentionSeed = t.substring(start, caret).removePrefix("@")
        viewModel.clearMentionSuggestions()
        mentionOpen = true
    }

    // Insert the chosen @handle, replacing the partial word the picker started on.
    fun insertMention(acct: String) {
        val (start, caret) = mentionAnchor
        val t = field.text
        val s = start.coerceIn(0, t.length)
        val c = caret.coerceIn(s, t.length)
        val prefix = t.substring(0, s) + "@" + acct + " "
        field = TextFieldValue(prefix + t.substring(c), TextRange(prefix.length))
        mentionOpen = false
        viewModel.clearMentionSuggestions()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(ctx.title) },
                navigationIcon = {
                    IconButton(onClick = onDone) {
                        Icon(Icons.Filled.Close, contentDescription = "Cancel")
                    }
                },
                actions = {
                    TextButton(onClick = { send() }, enabled = canSend) {
                        Text(if (sending) "Sending…" else "Send")
                    }
                },
            )
        },
    ) { pad ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            ctx.contextLabel?.let { label ->
                Text(label, style = MaterialTheme.typography.labelLarge)
            }

            if (ctx.featureContentWarning) {
                OutlinedTextField(
                    value = cw,
                    onValueChange = { cw = it },
                    label = { Text("Content warning (optional)") },
                    singleLine = true,
                    enabled = !sending,
                    modifier = Modifier.fillMaxWidth(),
                )
            }

            OutlinedTextField(
                value = field,
                onValueChange = { field = it },
                label = { Text("What's on your mind?") },
                enabled = !sending,
                minLines = 4,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Default),
                modifier = Modifier.fillMaxWidth(),
            )

            TextButton(onClick = { openMention() }, enabled = !sending) {
                Text("Mention someone")
            }

            Text(
                text = "$remaining",
                style = MaterialTheme.typography.labelLarge,
                color = if (remaining < 0) MaterialTheme.colorScheme.error
                else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.clearAndSetSemantics {
                    contentDescription = "$remaining characters remaining"
                },
            )

            if (ctx.featureMedia) {
                Button(onClick = { picker.launch(arrayOf("image/*", "video/*")) }, enabled = !sending) {
                    Text("Add media")
                }
                attachments.forEachIndexed { i, m ->
                    Column {
                        Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                            Text(
                                m.name,
                                style = MaterialTheme.typography.bodyLarge,
                                modifier = Modifier.weight(1f),
                            )
                            TextButton(onClick = { attachments.removeAt(i) }, enabled = !sending) {
                                Text("Remove")
                            }
                        }
                        OutlinedTextField(
                            value = m.alt,
                            onValueChange = { attachments[i] = m.copy(alt = it) },
                            label = { Text("Description (alt text)") },
                            enabled = !sending,
                            modifier = Modifier.fillMaxWidth(),
                        )
                        HorizontalDivider(Modifier.padding(top = 8.dp))
                    }
                }
            }

            if (ctx.featureVisibility) {
                VisibilitySelector(current = visibility, enabled = !sending, onSelect = { visibility = it })
            }

            if (ctx.participants.isNotEmpty()) {
                Text("Recipients", style = MaterialTheme.typography.labelLarge)
                ctx.participants.forEach { p ->
                    val isChecked = checked[p.acct] == true
                    Row(
                        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
                        modifier = Modifier
                            .fillMaxWidth()
                            .clearAndSetSemantics {
                                contentDescription =
                                    "@${p.acct}${if (isChecked) ", included" else ", not included"}"
                            },
                    ) {
                        Checkbox(
                            checked = isChecked,
                            onCheckedChange = { checked[p.acct] = it },
                            enabled = !sending,
                        )
                        Text("@${p.acct}", style = MaterialTheme.typography.bodyLarge)
                    }
                }
            }

            error?.let { err ->
                Text(err, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyLarge)
            }
        }
    }

    if (mentionOpen) {
        MentionDialog(
            viewModel = viewModel,
            seed = mentionSeed,
            onPick = { insertMention(it) },
            onDismiss = {
                mentionOpen = false
                viewModel.clearMentionSuggestions()
            },
        )
    }
}

private fun isHandleChar(c: Char): Boolean =
    c.isLetterOrDigit() || c == '_' || c == '.' || c == '-' || c == '@'

/**
 * @-mention autocomplete: type part of a handle, tap a match to insert it. The
 * core does the searching (search shared across front ends); this is the touch
 * equivalent of the desktop's Alt+A picker.
 */
@Composable
private fun MentionDialog(
    viewModel: CoreViewModel,
    seed: String,
    onPick: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var query by remember { mutableStateOf(seed) }
    val suggestions by viewModel.mentionSuggestions.collectAsState()
    val searchFocus = remember { FocusRequester() }
    val listFocus = remember { FocusRequester() }
    var listFocused by remember { mutableStateOf(false) }

    val users: List<MentionSuggestion> = suggestions?.users ?: emptyList()

    // Debounce so each keystroke doesn't fire a request; the core echoes the query.
    LaunchedEffect(query) {
        delay(200)
        viewModel.autocompleteUsers(query)
    }
    // Land on the results (matches are coming for the seeded handle) so a screen
    // reader moves straight through them; with nothing seeded, start in the search
    // box instead since there's nothing yet to land on.
    LaunchedEffect(Unit) {
        if (seed.isEmpty()) runCatching { searchFocus.requestFocus() }
    }
    LaunchedEffect(users.isNotEmpty()) {
        if (seed.isNotEmpty() && !listFocused && users.isNotEmpty()) {
            listFocused = true
            runCatching { listFocus.requestFocus() }
        }
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Mention a user") },
        text = {
            Column {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    label = { Text("Search") },
                    singleLine = true,
                    modifier = Modifier
                        .fillMaxWidth()
                        .focusRequester(searchFocus),
                )
                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 260.dp)
                        .focusRequester(listFocus)
                        .focusable(),
                ) {
                    items(users) { s ->
                        Text(
                            s.label,
                            style = MaterialTheme.typography.bodyLarge,
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onPick(s.acct) }
                                .padding(vertical = 12.dp),
                        )
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
private fun VisibilitySelector(current: Int, enabled: Boolean, onSelect: (Int) -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text("Visibility", style = MaterialTheme.typography.labelLarge)
        (0..3).forEach { v ->
            Row(
                verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .clearAndSetSemantics {
                        contentDescription =
                            "${visibilityLabel(v)}${if (v == current) ", selected" else ""}"
                    },
            ) {
                androidx.compose.material3.RadioButton(
                    selected = v == current,
                    onClick = { if (enabled) onSelect(v) },
                    enabled = enabled,
                )
                Text(visibilityLabel(v), style = MaterialTheme.typography.bodyLarge)
            }
        }
    }
}
