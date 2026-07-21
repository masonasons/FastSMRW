package me.masonasons.fastsm.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier

private val privacyOptions = listOf(
    "public" to "Public",
    "unlisted" to "Unlisted",
    "private" to "Followers only",
    "direct" to "Direct",
)

/**
 * The Edit Profile dialog: display name, bio, default post privacy, account flags,
 * and the server's allowed number of metadata fields. [onSubmit] gets the edited
 * profile to send to the core.
 */
@Composable
fun ProfileEditorDialog(
    editor: ProfileEditorUi,
    onSubmit: (ProfileEditorUi) -> Unit,
    onDismiss: () -> Unit,
) {
    var displayName by remember { mutableStateOf(editor.displayName) }
    var note by remember { mutableStateOf(editor.note) }
    var locked by remember { mutableStateOf(editor.locked) }
    var bot by remember { mutableStateOf(editor.bot) }
    var discoverable by remember { mutableStateOf(editor.discoverable) }
    var sensitive by remember { mutableStateOf(editor.sensitive) }
    var privacy by remember { mutableStateOf(editor.privacy) }
    var privacyOpen by remember { mutableStateOf(false) }
    // Bluesky profiles are just display name + bio: no metadata field rows.
    val rows = if (editor.simple) 0 else maxOf(1, editor.maxFields)
    val fieldNames = remember {
        mutableStateListOf<String>().apply {
            for (i in 0 until rows) add(editor.fields.getOrNull(i)?.name ?: "")
        }
    }
    val fieldValues = remember {
        mutableStateListOf<String>().apply {
            for (i in 0 until rows) add(editor.fields.getOrNull(i)?.value ?: "")
        }
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Edit Profile") },
        text = {
            Column(modifier = Modifier.verticalScroll(rememberScrollState())) {
                OutlinedTextField(
                    value = displayName, onValueChange = { displayName = it },
                    label = { Text("Display name") }, modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = note, onValueChange = { note = it },
                    label = { Text("Bio") }, modifier = Modifier.fillMaxWidth(),
                )
                if (!editor.simple) {
                    Text("Default post privacy", style = MaterialTheme.typography.labelLarge)
                    Box {
                        TextButton(onClick = { privacyOpen = true }) {
                            Text(privacyOptions.firstOrNull { it.first == privacy }?.second ?: "Public")
                        }
                        DropdownMenu(expanded = privacyOpen, onDismissRequest = { privacyOpen = false }) {
                            privacyOptions.forEach { (token, label) ->
                                DropdownMenuItem(
                                    text = { Text(label) },
                                    onClick = { privacy = token; privacyOpen = false },
                                )
                            }
                        }
                    }
                    CheckRow("Require follow requests", locked) { locked = it }
                    CheckRow("This is a bot account", bot) { bot = it }
                    CheckRow("List me in the profile directory", discoverable) { discoverable = it }
                    CheckRow("Mark my media sensitive by default", sensitive) { sensitive = it }
                    Text("Profile fields", style = MaterialTheme.typography.labelLarge)
                }
                for (i in 0 until rows) {
                    OutlinedTextField(
                        value = fieldNames[i], onValueChange = { fieldNames[i] = it },
                        label = { Text("Label ${i + 1}") }, modifier = Modifier.fillMaxWidth(),
                    )
                    OutlinedTextField(
                        value = fieldValues[i], onValueChange = { fieldValues[i] = it },
                        label = { Text("Content ${i + 1}") }, modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        },
        confirmButton = {
            TextButton(onClick = {
                val fields = (0 until rows).map { ProfileFieldUi(fieldNames[it], fieldValues[it]) }
                onSubmit(
                    editor.copy(
                        displayName = displayName, note = note, locked = locked, bot = bot,
                        discoverable = discoverable, sensitive = sensitive, privacy = privacy,
                        fields = fields,
                    )
                )
            }) { Text("Save") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

@Composable
private fun CheckRow(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = checked, onCheckedChange = onChange)
        Text(label)
    }
}
