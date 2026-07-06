package me.masonasons.fastsm.ui.compose

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Base64
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.masonasons.fastsm.ui.ComposeContext
import me.masonasons.fastsm.ui.CoreViewModel
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
    var text by remember { mutableStateOf(ctx.prefillText) }
    var cw by remember { mutableStateOf(ctx.prefillCw) }
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

    val remaining = ctx.maxChars - text.length
    val canSend = (text.isNotBlank() || attachments.isNotEmpty()) && remaining >= 0 && !sending

    fun send() {
        error = null
        sending = true
        val mentions = ctx.participants.filter { checked[it.acct] == true }.map { it.acct }
        val picked = attachments.toList()
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
            viewModel.sendPost(text = text, cw = cw, visibility = visibility, mentions = mentions, media = media)
        }
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
                value = text,
                onValueChange = { text = it },
                label = { Text("What's on your mind?") },
                enabled = !sending,
                minLines = 4,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Default),
                modifier = Modifier.fillMaxWidth(),
            )

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
