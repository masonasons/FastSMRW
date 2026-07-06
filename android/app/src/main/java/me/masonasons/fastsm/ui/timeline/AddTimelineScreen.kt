package me.masonasons.fastsm.ui.timeline

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.AlertDialog
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import me.masonasons.fastsm.ui.CoreViewModel
import me.masonasons.fastsm.ui.SpawnableUi

/**
 * "Add timeline or search": lists everything the core says can be opened
 * (built-in timelines, hashtags, search posts/people, remote instances, lists).
 * Entries that need a typed value (search query, hashtag, instance) prompt for
 * it; opening any spawns a new tab, so we close and let the pager switch to it.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AddTimelineScreen(viewModel: CoreViewModel, onClose: () -> Unit) {
    val spawnables by viewModel.spawnables.collectAsStateWithLifecycle()
    val accounts by viewModel.accounts.collectAsStateWithLifecycle()
    val selected by viewModel.selectedAccount.collectAsStateWithLifecycle()
    var prompt by remember { mutableStateOf<SpawnableUi?>(null) }

    // Muted/blocked/follow-requests are Mastodon-only and not in get_spawnable;
    // offer them here for a Mastodon account (spawn_timeline accepts these kinds).
    val isMastodon = accounts.firstOrNull { it.key == selected }?.platform == "mastodon"
    val entries = spawnables + if (isMastodon) listOf(
        SpawnableUi("mutes", "Muted users", null, null),
        SpawnableUi("blocks", "Blocked users", null, null),
        SpawnableUi("follow_requests", "Follow requests", null, null),
    ) else emptyList()

    LaunchedEffect(Unit) { viewModel.loadSpawnable() }
    BackHandler(enabled = true) { onClose() }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Add timeline or search") },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
            )
        },
    ) { pad ->
        Column(
            modifier = Modifier.fillMaxSize().padding(pad).verticalScroll(rememberScrollState()),
        ) {
            entries.forEach { sp ->
                Text(
                    text = if (sp.input != null) "${sp.title}…" else sp.title,
                    style = MaterialTheme.typography.bodyLarge,
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable {
                            if (sp.input != null) {
                                prompt = sp
                            } else {
                                viewModel.spawnTimeline(sp.kind, param = sp.param)
                                onClose()
                            }
                        }
                        .padding(horizontal = 16.dp, vertical = 16.dp),
                )
                HorizontalDivider()
            }
        }
    }

    prompt?.let { sp ->
        var value by remember(sp) { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { prompt = null },
            title = { Text(sp.title) },
            text = {
                OutlinedTextField(
                    value = value,
                    onValueChange = { value = it },
                    label = { Text(sp.input ?: "") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            },
            confirmButton = {
                TextButton(
                    enabled = value.isNotBlank(),
                    onClick = {
                        viewModel.spawnTimeline(sp.kind, value = value.trim())
                        prompt = null
                        onClose()
                    },
                ) { Text("Open") }
            },
            dismissButton = {
                TextButton(onClick = { prompt = null }) { Text("Cancel") }
            },
        )
    }
}
