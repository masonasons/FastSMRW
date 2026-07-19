package me.masonasons.fastsm.ui.home

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.TextButton
import me.masonasons.fastsm.ui.AliasEntry
import me.masonasons.fastsm.ui.CoreViewModel
import me.masonasons.fastsm.ui.RowUi
import me.masonasons.fastsm.ui.TabUi
import me.masonasons.fastsm.ui.TrendingTagUi

/**
 * The home surface: account picker + timeline tabs + a pager of row lists.
 * All data comes from the core's events; interactions dispatch core commands.
 * Advanced features (compose, filters, add-timeline, thread/profile nav) arrive
 * in later phases.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    viewModel: CoreViewModel,
    onAddAccount: () -> Unit,
    onOpenSettings: () -> Unit,
    onOpenAddTimeline: () -> Unit,
    onOpenAccountSettings: () -> Unit,
) {
    val accounts by viewModel.accounts.collectAsStateWithLifecycle()
    val selected by viewModel.selectedAccount.collectAsStateWithLifecycle()
    val tabs by viewModel.tabs.collectAsStateWithLifecycle()
    val currentTab by viewModel.currentTab.collectAsStateWithLifecycle()
    val rowsByTab by viewModel.rowsByTab.collectAsStateWithLifecycle()
    val selectedIdByTab by viewModel.selectedIdByTab.collectAsStateWithLifecycle()

    val pagerState = rememberPagerState(pageCount = { tabs.size })
    val scope = rememberCoroutineScope()
    // The User Analysis picker (opened from the overflow menu).
    var showUserAnalysis by remember { mutableStateOf(false) }

    // Keep the pager and the core's selected timeline in sync both ways.
    LaunchedEffect(currentTab) {
        if (currentTab in tabs.indices && currentTab != pagerState.currentPage) {
            pagerState.scrollToPage(currentTab)
        }
    }
    LaunchedEffect(pagerState.currentPage, tabs.size) {
        if (pagerState.currentPage in tabs.indices) viewModel.selectTimeline(pagerState.currentPage)
    }

    // The visible tab is closable (a thread/search/user/list tab, not Home).
    val currentClosable = tabs.getOrNull(pagerState.currentPage)?.dismissable == true
    // Back gesture closes the focused timeline when it's closable (else it falls
    // through to the system, exiting the app).
    BackHandler(enabled = currentClosable) { viewModel.closeTimeline(pagerState.currentPage) }

    Scaffold(
        topBar = {
            Column {
                TopAppBar(
                    title = { Text("FastSM") },
                    navigationIcon = {
                        if (currentClosable) {
                            IconButton(onClick = { viewModel.closeTimeline(pagerState.currentPage) }) {
                                Icon(Icons.Filled.Close, contentDescription = "Close this timeline")
                            }
                        }
                    },
                    actions = {
                        IconButton(onClick = { viewModel.refresh() }) {
                            Icon(Icons.Filled.Refresh, contentDescription = "Refresh")
                        }
                        var menuOpen by remember { mutableStateOf(false) }
                        IconButton(onClick = { menuOpen = true }) {
                            Icon(Icons.Filled.MoreVert, contentDescription = "More")
                        }
                        DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                            DropdownMenuItem(
                                text = { Text("Add timeline or search") },
                                onClick = { menuOpen = false; onOpenAddTimeline() },
                            )
                            DropdownMenuItem(
                                text = { Text("User aliases") },
                                onClick = { menuOpen = false; viewModel.listAliases() },
                            )
                            DropdownMenuItem(
                                text = { Text("User analysis") },
                                onClick = { menuOpen = false; showUserAnalysis = true },
                            )
                            DropdownMenuItem(
                                text = { Text("Trending hashtags") },
                                onClick = { menuOpen = false; viewModel.listTrendingHashtags() },
                            )
                            DropdownMenuItem(
                                text = { Text("Settings") },
                                onClick = { menuOpen = false; onOpenSettings() },
                            )
                        }
                    },
                )
                AccountPicker(
                    accounts = accounts,
                    selectedKey = selected,
                    onSwitch = viewModel::switchAccount,
                    onAddAccount = onAddAccount,
                    onLogOut = viewModel::removeAccount,
                    onAccountSettings = onOpenAccountSettings,
                )
                TimelineTabs(
                    tabs = tabs,
                    selectedIndex = pagerState.currentPage,
                    onSelect = { index -> scope.launch { pagerState.animateScrollToPage(index) } },
                    onClose = viewModel::closeTimeline,
                    onPin = viewModel::pinTimeline,
                    onMute = viewModel::muteTimeline,
                    onMove = viewModel::moveTimeline,
                )
            }
        },
        floatingActionButton = {
            FloatingActionButton(onClick = viewModel::composeNew) {
                Icon(Icons.Filled.Edit, contentDescription = "New post")
            }
        },
    ) { innerPadding ->
        HorizontalPager(
            state = pagerState,
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
        ) { pageIndex ->
            StatusList(
                rows = rowsByTab[pageIndex] ?: emptyList(),
                isCurrent = pageIndex == pagerState.currentPage,
                selectedId = selectedIdByTab[pageIndex].orEmpty(),
                onLoadOlder = { viewModel.loadOlder(automatic = true) },
                onNoteSelection = viewModel::noteSelection,
                onOpenThread = viewModel::openThread,
                onOpenAuthor = viewModel::openUserTimeline,
                onOpenProfile = viewModel::openUserProfile,
                onViewMedia = viewModel::playMedia,
                onToggleFavorite = viewModel::toggleFavorite,
                onToggleBoost = viewModel::toggleBoost,
                onToggleMuteConversation = viewModel::toggleMuteConversation,
                onOpenFavoritedBy = viewModel::openFavoritedBy,
                onOpenRebloggedBy = viewModel::openRebloggedBy,
                onReply = viewModel::composeReply,
                onQuote = viewModel::composeQuote,
                onEdit = viewModel::composeEdit,
                onDelete = viewModel::deletePost,
                onSpeakUser = viewModel::speakUser,
                onSpeakReply = viewModel::speakReply,
                onJumpToReply = viewModel::jumpToReply,
                onAddAlias = viewModel::beginAlias,
            )
        }
    }

    val picker by viewModel.userPicker.collectAsStateWithLifecycle()
    picker?.let { req ->
        AlertDialog(
            onDismissRequest = viewModel::dismissUserPicker,
            title = { Text("Which user?") },
            text = {
                Column {
                    req.users.forEach { u ->
                        TextButton(onClick = {
                            when (req.purpose) {
                                "profile" -> viewModel.openUserProfilePicked(req.rowId, u.id)
                                "alias" -> viewModel.beginAliasPicked(req.rowId, u.id)
                                else -> viewModel.openUserTimelinePicked(u.id, u.acct)
                            }
                        }) {
                            Text("@${u.acct}")
                        }
                    }
                }
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = viewModel::dismissUserPicker) { Text("Cancel") }
            },
        )
    }

    // Add/edit an alias for a single user (raised by "Add alias" or the picker).
    val aliasPrompt by viewModel.aliasPrompt.collectAsStateWithLifecycle()
    aliasPrompt?.let { req ->
        AliasEditDialog(
            handle = req.handle,
            current = req.current,
            onDismiss = viewModel::dismissAliasPrompt,
            onConfirm = { value ->
                if (value.isEmpty()) viewModel.clearAlias(req.key, req.handle)
                else viewModel.setAlias(req.key, req.handle, value)
                viewModel.dismissAliasPrompt()
            },
        )
    }

    // The aliases manager: list every alias with edit / remove.
    val aliasesList by viewModel.aliasesList.collectAsStateWithLifecycle()
    aliasesList?.let { list ->
        var editing by remember { mutableStateOf<AliasEntry?>(null) }
        AlertDialog(
            onDismissRequest = viewModel::dismissAliasesList,
            title = { Text("User Aliases") },
            text = {
                if (list.isEmpty()) {
                    Text("No aliases yet. Use \"Add alias\" on a post to create one.")
                } else {
                    LazyColumn {
                        items(list, key = { it.key }) { a ->
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                modifier = Modifier.fillMaxWidth(),
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(a.alias, style = MaterialTheme.typography.bodyLarge)
                                    Text("@${a.handle}", style = MaterialTheme.typography.bodySmall)
                                }
                                TextButton(onClick = { editing = a }) { Text("Edit") }
                                TextButton(onClick = {
                                    viewModel.clearAlias(a.key, a.handle)
                                    viewModel.listAliases() // refresh the manager
                                }) { Text("Remove") }
                            }
                        }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = viewModel::dismissAliasesList) { Text("Close") }
            },
        )
        editing?.let { a ->
            AliasEditDialog(
                handle = a.handle,
                current = a.alias,
                onDismiss = { editing = null },
                onConfirm = { value ->
                    if (value.isEmpty()) viewModel.clearAlias(a.key, a.handle)
                    else viewModel.setAlias(a.key, a.handle, value)
                    editing = null
                    viewModel.listAliases() // refresh the manager
                },
            )
        }
    }

    if (showUserAnalysis) {
        UserAnalysisDialog(
            onSelect = { category ->
                viewModel.analyzeUsers(category)
                showUserAnalysis = false
            },
            onDismiss = { showUserAnalysis = false },
        )
    }

    // Trending hashtags: pick one to open as a timeline or follow it.
    val trendingHashtags by viewModel.trendingHashtags.collectAsStateWithLifecycle()
    trendingHashtags?.let { tags ->
        TrendingHashtagsDialog(
            tags = tags,
            onOpen = { name ->
                viewModel.spawnTimeline("hashtag", value = name)
                viewModel.dismissTrendingHashtags()
            },
            onFollow = { name -> viewModel.followHashtag(name) },
            onDismiss = viewModel::dismissTrendingHashtags,
        )
    }

    val mediaPicker by viewModel.mediaPicker.collectAsStateWithLifecycle()
    mediaPicker?.let { items ->
        AlertDialog(
            onDismissRequest = viewModel::dismissMediaPicker,
            title = { Text("Which attachment?") },
            text = {
                Column {
                    items.forEach { m ->
                        TextButton(onClick = { viewModel.playMediaItem(m) }) {
                            Text(m.title.ifBlank { m.kind })
                        }
                    }
                }
            },
            confirmButton = {},
            dismissButton = {
                TextButton(onClick = viewModel::dismissMediaPicker) { Text("Cancel") }
            },
        )
    }
}

/**
 * User Analysis picker: choose an analysis of your follow relationships. Picking
 * one dispatches analyze_users; the core spawns a user timeline of the result (or
 * announces an error if your follow lists can't be fully loaded). Keep the options
 * in sync with the Windows/Mac pickers and CoreSession::cmd_analyze_users.
 */
@Composable
private fun UserAnalysisDialog(
    onSelect: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val analyses = listOf(
        "People who follow you that you don't follow back" to "not_following_back",
        "People you follow who don't follow you back" to "no_followback",
        "Mutual follows (you both follow each other)" to "mutuals",
    )
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("User Analysis") },
        text = {
            Column {
                analyses.forEach { (label, category) ->
                    TextButton(
                        onClick = { onSelect(category) },
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Text(label, modifier = Modifier.fillMaxWidth())
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

/**
 * Trending Hashtags picker: the instance's trending tags. Open one as a timeline
 * or follow it (Follow is disabled for tags you already follow). Mirrors the
 * Windows/Mac Trending Hashtags managers.
 */
@Composable
private fun TrendingHashtagsDialog(
    tags: List<TrendingTagUi>,
    onOpen: (String) -> Unit,
    onFollow: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    // Track tags followed during this session so Follow greys out immediately.
    var justFollowed by remember { mutableStateOf(setOf<String>()) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Trending Hashtags") },
        text = {
            if (tags.isEmpty()) {
                Text("Nothing is trending right now.")
            } else {
                LazyColumn {
                    items(tags, key = { it.name }) { tag ->
                        val followed = tag.following || tag.name in justFollowed
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text(
                                "#${tag.name}",
                                style = MaterialTheme.typography.bodyLarge,
                                modifier = Modifier.weight(1f),
                            )
                            TextButton(onClick = { onOpen(tag.name) }) { Text("Open") }
                            TextButton(
                                enabled = !followed,
                                onClick = {
                                    onFollow(tag.name)
                                    justFollowed = justFollowed + tag.name
                                },
                            ) { Text(if (followed) "Following" else "Follow") }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) { Text("Close") }
        },
    )
}

/** Prompt for a user's alias. An empty value clears the alias. */
@Composable
private fun AliasEditDialog(
    handle: String,
    current: String,
    onDismiss: () -> Unit,
    onConfirm: (String) -> Unit,
) {
    var text by remember(handle, current) { mutableStateOf(current) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Alias for @$handle") },
        text = {
            Column {
                Text("Enter a custom display name, or leave blank to remove.")
                OutlinedTextField(
                    value = text,
                    onValueChange = { text = it },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        },
        confirmButton = {
            TextButton(onClick = { onConfirm(text.trim()) }) { Text("OK") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel") }
        },
    )
}

/**
 * One tab's post list. Fires [onLoadOlder] when scrolled near the end, restores
 * the core's remembered reading position ([selectedId]) once, and reports the
 * settled position back ([onNoteSelection]) so the core persists it. The
 * load-older / report hooks only fire for the visible page ([isCurrent]) since
 * those core commands act on the currently-selected timeline.
 */
@OptIn(kotlinx.coroutines.FlowPreview::class)
@Composable
private fun StatusList(
    rows: List<RowUi>,
    isCurrent: Boolean,
    selectedId: String,
    onLoadOlder: () -> Unit,
    onNoteSelection: (String) -> Unit,
    onOpenThread: (String) -> Unit,
    onOpenAuthor: (String) -> Unit,
    onOpenProfile: (String) -> Unit,
    onViewMedia: (String) -> Unit,
    onToggleFavorite: (String) -> Unit,
    onToggleBoost: (String) -> Unit,
    onToggleMuteConversation: (String) -> Unit,
    onOpenFavoritedBy: (String) -> Unit,
    onOpenRebloggedBy: (String) -> Unit,
    onReply: (String) -> Unit,
    onQuote: (String) -> Unit,
    onEdit: (String) -> Unit,
    onDelete: (String) -> Unit,
    onSpeakUser: (String) -> Unit,
    onSpeakReply: (String) -> Unit,
    onJumpToReply: (String) -> Unit,
    onAddAlias: (String) -> Unit,
) {
    if (rows.isEmpty()) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text("No posts yet.")
        }
        return
    }

    val listState = rememberLazyListState()

    // Load older posts as the end of the list approaches (visible page only).
    val shouldLoadOlder by remember(rows.size, isCurrent) {
        derivedStateOf {
            if (!isCurrent) return@derivedStateOf false
            val last = listState.layoutInfo.visibleItemsInfo.lastOrNull()?.index
                ?: return@derivedStateOf false
            last >= rows.size - 3
        }
    }
    LaunchedEffect(listState, rows.size, isCurrent) {
        snapshotFlow { shouldLoadOlder }.distinctUntilChanged().collect { if (it) onLoadOlder() }
    }

    // Restore the core's remembered position once per distinct selected_id.
    var restoredFor by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(selectedId, rows) {
        if (selectedId.isBlank() || selectedId == restoredFor) return@LaunchedEffect
        val idx = rows.indexOfFirst { it.id == selectedId }
        if (idx >= 0) {
            listState.scrollToItem(idx)
            restoredFor = selectedId
        }
    }

    // Report the settled reading position so the core persists it.
    LaunchedEffect(listState, isCurrent) {
        if (!isCurrent) return@LaunchedEffect
        snapshotFlow { listState.firstVisibleItemIndex }
            .distinctUntilChanged()
            .debounce(1000)
            .collect { idx -> rows.getOrNull(idx)?.let { onNoteSelection(it.id) } }
    }

    LazyColumn(state = listState, modifier = Modifier.fillMaxSize()) {
        items(rows, key = { it.id }) { row ->
            StatusRow(
                row = row,
                onOpenThread = onOpenThread,
                onOpenAuthor = onOpenAuthor,
                onOpenProfile = onOpenProfile,
                onViewMedia = onViewMedia,
                onToggleFavorite = onToggleFavorite,
                onToggleBoost = onToggleBoost,
                onToggleMuteConversation = onToggleMuteConversation,
                onOpenFavoritedBy = onOpenFavoritedBy,
                onOpenRebloggedBy = onOpenRebloggedBy,
                onReply = onReply,
                onQuote = onQuote,
                onEdit = onEdit,
                onDelete = onDelete,
                onSpeakUser = onSpeakUser,
                onSpeakReply = onSpeakReply,
                onJumpToReply = onJumpToReply,
                onAddAlias = onAddAlias,
            )
        }
    }
}

/**
 * Tab strip: plain Row + horizontalScroll so every tab stays in the
 * accessibility tree even off-screen (TalkBack swipe reaches them all).
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun TimelineTabs(
    tabs: List<TabUi>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    onClose: (Int) -> Unit,
    onPin: (Int) -> Unit,
    onMute: (Int) -> Unit,
    onMove: (Int, String) -> Unit,
) {
    val scrollState = rememberScrollState()
    val n = tabs.size
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(scrollState),
    ) {
        tabs.forEachIndexed { index, tab ->
            // key by the timeline's cache_key so a tab's semantics node keeps
            // identity across reordering — TalkBack focus follows the moved tab.
            key(tab.kind) {
                val selected = index == selectedIndex
                var menuOpen by remember { mutableStateOf(false) }
                // One list drives both TalkBack custom actions and the long-press
                // menu (Pin/Move/Close), so the two paths can't drift.
                val menuActions = buildList {
                    add(MenuAction(if (tab.pinned) "Unpin tab" else "Pin tab") { onPin(index) })
                    add(MenuAction(if (tab.muted) "Unmute sounds" else "Mute sounds") { onMute(index) })
                    if (index > 0) add(MenuAction("Move left") { onMove(index, "up") })
                    if (index < n - 1) add(MenuAction("Move right") { onMove(index, "down") })
                    if (tab.dismissable) add(MenuAction("Close tab") { onClose(index) })
                }
                val actions = menuActions.toAccessibilityActions()
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .combinedClickable(
                            onClick = { onSelect(index) },
                            onLongClick = { menuOpen = true },
                        )
                        .background(
                            if (selected) MaterialTheme.colorScheme.primaryContainer
                            else MaterialTheme.colorScheme.surface,
                        )
                        .padding(
                            start = 12.dp, top = 12.dp, bottom = 12.dp,
                            end = if (tab.dismissable) 0.dp else 12.dp,
                        )
                        .clearAndSetSemantics {
                            contentDescription = buildString {
                                append(tab.title).append(" tab")
                                if (tab.pinned) append(", pinned")
                                if (tab.muted) append(", muted")
                                if (selected) append(", selected")
                            }
                            customActions = actions
                            onClick { onSelect(index); true }
                        },
                ) {
                    Text(tab.title)
                    if (tab.dismissable) {
                        IconButton(
                            onClick = { onClose(index) },
                            // Exposed via the tab's "Close tab" custom action instead.
                            modifier = Modifier.clearAndSetSemantics {},
                        ) {
                            Icon(Icons.Filled.Close, contentDescription = null)
                        }
                    }
                    DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
                        menuActions.forEach { action ->
                            DropdownMenuItem(
                                text = { Text(action.label) },
                                onClick = { action.run(); menuOpen = false },
                            )
                        }
                    }
                }
            }
        }
    }
}
