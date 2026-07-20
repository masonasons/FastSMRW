package me.masonasons.fastsm.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.selection.selectable
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Checkbox
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier

private val reportCategories = listOf(
    "spam" to "It's spam",
    "violation" to "It breaks a server rule",
    "legal" to "It's illegal content",
    "other" to "Something else",
)

/**
 * The moderation "Report" dialog: pick a reason, add optional details, choose
 * whether to forward to the user's home server. [onSubmit] gets the API category
 * token, the comment, and the forward flag.
 */
@Composable
fun ReportDialog(
    remote: Boolean,
    onSubmit: (category: String, comment: String, forward: Boolean) -> Unit,
    onDismiss: () -> Unit,
) {
    var category by remember { mutableStateOf("other") }
    var comment by remember { mutableStateOf("") }
    var forward by remember { mutableStateOf(remote) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Report") },
        text = {
            Column {
                Text("Reason", style = MaterialTheme.typography.labelLarge)
                reportCategories.forEach { (token, label) ->
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier
                            .fillMaxWidth()
                            .selectable(selected = category == token, onClick = { category = token }),
                    ) {
                        RadioButton(selected = category == token, onClick = { category = token })
                        Text(label)
                    }
                }
                OutlinedTextField(
                    value = comment,
                    onValueChange = { comment = it },
                    label = { Text("Additional details (optional)") },
                    modifier = Modifier.fillMaxWidth(),
                )
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Checkbox(checked = forward, onCheckedChange = { forward = it })
                    Text("Forward to the user's home server")
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onSubmit(category, comment, forward); onDismiss() }) {
                Text("Submit")
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
