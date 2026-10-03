package me.masonasons.fastsm.ui.timeline

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.unit.dp

/**
 * The focused timeline's display filter -- which kinds of post it shows. This is how you
 * hide boosts (switch "Boosts" off).
 *
 * Labels, order and grouping deliberately match the iOS and desktop filter screens: the
 * same feature should not read differently per platform. The filter only changes what is
 * displayed; the timeline still fetches and caches everything, so switching a category
 * back on shows the posts again without a refresh.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ClientFilterScreen(
    initial: Map<String, Boolean>,
    initialText: String,
    onApply: (Map<String, Boolean>, String) -> Unit,
    onClear: () -> Unit,
    onClose: () -> Unit,
) {
    // Same order as the other apps' filter screens.
    val options = listOf(
        "original" to "Original posts",
        "replies" to "Replies",
        "replies_to_me" to "Replies to me",
        "replies_to_unfollowed" to "Bluesky replies to people you don't follow",
        "threads" to "Threads (self-replies)",
        "boosts" to "Boosts",
        "quotes" to "Quotes",
        "media" to "Posts with media",
        "no_media" to "Posts without media",
        "my_posts" to "My posts",
        "my_replies" to "My replies",
    )
    val flags = remember {
        mutableStateMapOf<String, Boolean>().apply {
            options.forEach { (key, _) -> put(key, initial[key] ?: true) }
        }
    }
    var text by remember { mutableStateOf(initialText) }

    BackHandler(enabled = true) { onClose() }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Filter timeline") },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
            )
        },
    ) { padding ->
        Column(
            Modifier.padding(padding).verticalScroll(rememberScrollState()).padding(16.dp),
        ) {
            Text(
                "Show",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.semantics { heading() },
            )
            options.forEach { (key, label) ->
                // Same shape as SettingsScreen's SwitchRow: the row carries the toggle
                // semantics and the Switch is decorative, so TalkBack reads one element
                // ("label, switch, on") rather than a label and an unlabelled control.
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .toggleable(
                            value = flags[key] ?: true,
                            role = Role.Switch,
                            onValueChange = { flags[key] = it },
                        )
                        .padding(vertical = 12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        label,
                        style = MaterialTheme.typography.bodyLarge,
                        modifier = Modifier.weight(1f),
                    )
                    Switch(checked = flags[key] ?: true, onCheckedChange = null)
                }
            }
            HorizontalDivider(Modifier.padding(vertical = 12.dp))
            Text(
                "Text",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.semantics { heading() },
            )
            OutlinedTextField(
                value = text,
                onValueChange = { text = it },
                label = { Text("Only posts containing") },
                supportingText = { Text("Keep only posts containing this text.") },
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
            )
            Button(
                onClick = { onApply(flags.toMap(), text) },
                modifier = Modifier.fillMaxWidth().padding(top = 16.dp),
            ) { Text("Apply") }
            OutlinedButton(
                onClick = onClear,
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
            ) { Text("Clear filter") }
        }
    }
}
