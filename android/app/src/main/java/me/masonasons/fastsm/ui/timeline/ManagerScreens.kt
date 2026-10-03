package me.masonasons.fastsm.ui.timeline

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.CustomAccessibilityAction
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import me.masonasons.fastsm.ui.CoreViewModel

/** A plain top bar with a Back button, shared by the manager screens below. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ManagerScaffold(
    title: String,
    onClose: () -> Unit,
    content: @Composable (Modifier) -> Unit,
) {
    BackHandler(enabled = true) { onClose() }
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(title) },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
            )
        },
    ) { padding ->
        content(Modifier.padding(padding))
    }
}

/**
 * "Not loaded yet" versus "genuinely empty" are different things to a screen reader, so
 * they read differently rather than both showing a blank list.
 */
@Composable
private fun LoadingOrEmpty(empty: Boolean, emptyText: String) {
    if (empty) {
        Text(emptyText, Modifier.padding(16.dp))
    } else {
        Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            CircularProgressIndicator()
            Text("Loading…", Modifier.padding(start = 12.dp))
        }
    }
}

/**
 * The account's lists: create, rename, delete. Rename and Delete are TalkBack custom
 * actions on each row as well as buttons, so neither path is the only way in.
 */
@Composable
fun ListsManagerScreen(vm: CoreViewModel, onClose: () -> Unit) {
    val lists by vm.lists.collectAsStateWithLifecycle()
    val supported by vm.listsSupported.collectAsStateWithLifecycle()
    var creating by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf<CoreViewModel.ListUi?>(null) }
    var deleting by remember { mutableStateOf<CoreViewModel.ListUi?>(null) }
    var draft by remember { mutableStateOf("") }

    ManagerScaffold("Manage lists", onClose) { mod ->
        Column(mod.verticalScroll(rememberScrollState())) {
            if (!supported) {
                Text("Lists are only available for Mastodon accounts.", Modifier.padding(16.dp))
                return@Column
            }
            Button(
                onClick = { draft = ""; creating = true },
                modifier = Modifier.fillMaxWidth().padding(16.dp),
            ) { Text("New list") }
            val rows = lists
            if (rows.isNullOrEmpty()) {
                LoadingOrEmpty(rows != null, "You don't have any lists yet.")
            } else {
                rows.forEach { list ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .semantics {
                                customActions = listOf(
                                    CustomAccessibilityAction("Rename") {
                                        draft = list.title; renaming = list; true
                                    },
                                    CustomAccessibilityAction("Delete") { deleting = list; true },
                                )
                            }
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(list.title, Modifier.weight(1f))
                        TextButton(onClick = { draft = list.title; renaming = list }) {
                            Text("Rename")
                        }
                        TextButton(onClick = { deleting = list }) { Text("Delete") }
                    }
                    HorizontalDivider()
                }
            }
        }
    }

    if (creating) {
        TextPromptDialog("New list", "Name for the new list:", draft,
            onConfirm = { creating = false; if (it.isNotBlank()) vm.createList(it.trim()) },
            onDismiss = { creating = false })
    }
    renaming?.let { list ->
        TextPromptDialog("Rename list", null, draft,
            onConfirm = { renaming = null; if (it.isNotBlank()) vm.renameList(list.id, it.trim()) },
            onDismiss = { renaming = null })
    }
    deleting?.let { list ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text("Delete list") },
            text = { Text("Delete \"${list.title}\"?") },
            confirmButton = {
                TextButton(onClick = { vm.deleteList(list.id); deleting = null }) { Text("Delete") }
            },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text("Cancel") } },
        )
    }
}

/** The hashtags you follow, with a way to stop following each. */
@Composable
fun FollowedHashtagsScreen(vm: CoreViewModel, onClose: () -> Unit) {
    val tags by vm.followedTags.collectAsStateWithLifecycle()
    ManagerScaffold("Followed hashtags", onClose) { mod ->
        Column(mod.verticalScroll(rememberScrollState())) {
            val rows = tags?.filter { it.following }
            if (rows.isNullOrEmpty()) {
                LoadingOrEmpty(rows != null, "You aren't following any hashtags.")
            } else {
                rows.forEach { tag ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .semantics {
                                customActions = listOf(
                                    CustomAccessibilityAction("Unfollow") {
                                        vm.unfollowHashtag(tag.name); true
                                    },
                                )
                            }
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("#${tag.name}", Modifier.weight(1f))
                        TextButton(onClick = { vm.unfollowHashtag(tag.name) }) { Text("Unfollow") }
                    }
                    HorizontalDivider()
                }
            }
        }
    }
}

/**
 * Server-side filters: the instance applies these, so they work in every client. Each
 * filter has keywords, an action (warn or hide) and the timelines it applies in.
 */
@Composable
fun ServerFiltersScreen(vm: CoreViewModel, onClose: () -> Unit) {
    val filters by vm.serverFilters.collectAsStateWithLifecycle()
    val supported by vm.serverFiltersSupported.collectAsStateWithLifecycle()
    var editing by remember { mutableStateOf<CoreViewModel.ServerFilterUi?>(null) }
    var creatingNew by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<CoreViewModel.ServerFilterUi?>(null) }

    if (creatingNew || editing != null) {
        ServerFilterEditor(
            filter = editing,
            onSave = { id, title, action, context, keywords ->
                vm.saveServerFilter(id, title, action, context, keywords)
                editing = null
                creatingNew = false
            },
            onClose = { editing = null; creatingNew = false },
        )
        return
    }

    ManagerScaffold("Server filters", onClose) { mod ->
        Column(mod.verticalScroll(rememberScrollState())) {
            if (!supported) {
                Text(
                    "Server filters are only available for Mastodon accounts.",
                    Modifier.padding(16.dp),
                )
                return@Column
            }
            Button(
                onClick = { creatingNew = true },
                modifier = Modifier.fillMaxWidth().padding(16.dp),
            ) { Text("New filter") }
            val rows = filters
            if (rows.isNullOrEmpty()) {
                LoadingOrEmpty(rows != null, "You don't have any server filters.")
            } else {
                rows.forEach { f ->
                    val summary = if (f.action == "hide") "Hides posts" else "Warns"
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clickable { editing = f }
                            .semantics {
                                customActions = listOf(
                                    CustomAccessibilityAction("Edit") { editing = f; true },
                                    CustomAccessibilityAction("Delete") { deleting = f; true },
                                )
                            }
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(f.title)
                            Text(summary, style = MaterialTheme.typography.bodySmall)
                        }
                        TextButton(onClick = { deleting = f }) { Text("Delete") }
                    }
                    HorizontalDivider()
                }
            }
        }
    }

    deleting?.let { f ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text("Delete filter") },
            text = { Text("Delete \"${f.title}\"?") },
            confirmButton = {
                TextButton(onClick = {
                    vm.deleteServerFilter(f.id)
                    deleting = null
                }) { Text("Delete") }
            },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text("Cancel") } },
        )
    }
}

/** Create or edit one server filter. Same fields and order as the iOS editor. */
@Composable
private fun ServerFilterEditor(
    filter: CoreViewModel.ServerFilterUi?,
    onSave: (
        id: String, title: String, action: String, context: List<String>, keywords: List<String>,
    ) -> Unit,
    onClose: () -> Unit,
) {
    val contextOptions = listOf(
        "home" to "Home",
        "notifications" to "Notifications",
        "public" to "Public timelines",
        "thread" to "Threads",
        "account" to "Profiles",
    )
    var title by remember { mutableStateOf(filter?.title ?: "") }
    var hide by remember { mutableStateOf(filter?.action == "hide") }
    val contexts = remember {
        mutableStateListOf<String>().apply { addAll(filter?.context ?: listOf("home")) }
    }
    val keywords = remember {
        mutableStateListOf<String>().apply { addAll(filter?.keywords?.map { it.keyword } ?: emptyList()) }
    }
    var newKeyword by remember { mutableStateOf("") }

    ManagerScaffold(if (filter == null) "New filter" else "Edit filter", onClose) { mod ->
        Column(mod.verticalScroll(rememberScrollState()).padding(16.dp)) {
            OutlinedTextField(
                value = title,
                onValueChange = { title = it },
                label = { Text("Title") },
                modifier = Modifier.fillMaxWidth(),
            )
            Row(
                Modifier
                    .fillMaxWidth()
                    .toggleable(value = hide, role = Role.Switch, onValueChange = { hide = it })
                    .padding(vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("Hide matching posts completely", Modifier.weight(1f))
                Switch(checked = hide, onCheckedChange = null)
            }
            Text(
                "Apply in",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.semantics { heading() },
            )
            contextOptions.forEach { (key, label) ->
                val on = contexts.contains(key)
                Row(
                    Modifier
                        .fillMaxWidth()
                        .toggleable(
                            value = on,
                            role = Role.Switch,
                            onValueChange = { if (it) contexts.add(key) else contexts.remove(key) },
                        )
                        .padding(vertical = 12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(label, Modifier.weight(1f))
                    Switch(checked = on, onCheckedChange = null)
                }
            }
            HorizontalDivider(Modifier.padding(vertical = 12.dp))
            Text(
                "Keywords",
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.semantics { heading() },
            )
            keywords.forEachIndexed { i, word ->
                Row(
                    Modifier
                        .fillMaxWidth()
                        .semantics {
                            customActions = listOf(
                                CustomAccessibilityAction("Remove") { keywords.removeAt(i); true },
                            )
                        }
                        .padding(vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(word, Modifier.weight(1f))
                    TextButton(onClick = { keywords.removeAt(i) }) { Text("Remove") }
                }
            }
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(
                    value = newKeyword,
                    onValueChange = { newKeyword = it },
                    label = { Text("Add a keyword") },
                    modifier = Modifier.weight(1f),
                )
                TextButton(onClick = {
                    if (newKeyword.isNotBlank()) {
                        keywords.add(newKeyword.trim())
                        newKeyword = ""
                    }
                }) { Text("Add") }
            }
            Button(
                onClick = {
                    onSave(
                        filter?.id ?: "",
                        title.trim(),
                        if (hide) "hide" else "warn",
                        contexts.toList(),
                        keywords.toList(),
                    )
                },
                modifier = Modifier.fillMaxWidth().padding(top = 16.dp),
                enabled = title.isNotBlank() && keywords.isNotEmpty() && contexts.isNotEmpty(),
            ) { Text("Save") }
        }
    }
}

/** A one-field prompt, for naming and renaming lists. */
@Composable
private fun TextPromptDialog(
    title: String,
    message: String?,
    initial: String,
    onConfirm: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var text by remember { mutableStateOf(initial) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = {
            Column {
                if (message != null) Text(message)
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it },
                    modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                )
            }
        },
        confirmButton = { TextButton(onClick = { onConfirm(text) }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
