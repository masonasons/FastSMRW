package me.masonasons.fastsm.ui.home

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.customActions
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.unit.dp
import me.masonasons.fastsm.ui.ReportDialog
import me.masonasons.fastsm.ui.RowUi

/**
 * A timeline row. The core hands us the finished spoken string in [row.text], so
 * this is one TalkBack node whose contentDescription is that string — no speech
 * is composed in the UI. Contextual actions dispatch core commands by row id;
 * the same [MenuAction] list drives both TalkBack's custom actions and the
 * long-press menu, so they can't drift.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
fun StatusRow(
    row: RowUi,
    onOpenThread: (String) -> Unit,
    onOpenAuthor: (String) -> Unit,
    onOpenProfile: (String) -> Unit,
    onViewMedia: (String) -> Unit,
    onToggleFavorite: (String) -> Unit,
    onToggleBoost: (String) -> Unit,
    onToggleBookmark: (String) -> Unit,
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
    onReport: (id: String, category: String, comment: String, forward: Boolean) -> Unit,
) {
    var menuOpen by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf(false) }
    var showReport by remember { mutableStateOf(false) }

    val menuActions = buildList {
        add(MenuAction("Reply") { onReply(row.id) })
        add(MenuAction(if (row.favorited) "Unfavourite" else "Favourite") { onToggleFavorite(row.id) })
        add(MenuAction(if (row.boosted) "Unboost" else "Boost") { onToggleBoost(row.id) })
        add(MenuAction(if (row.bookmarked) "Remove bookmark" else "Bookmark") { onToggleBookmark(row.id) })
        add(MenuAction("Quote") { onQuote(row.id) })
        if (row.hasMedia) add(MenuAction("View media") { onViewMedia(row.id) })
        add(MenuAction("View conversation") { onOpenThread(row.id) })
        add(MenuAction(if (row.muted) "Unmute conversation" else "Mute conversation") {
            onToggleMuteConversation(row.id)
        })
        if (row.favoritesCount > 0) add(MenuAction("See who favorited") { onOpenFavoritedBy(row.id) })
        if (row.boostsCount > 0) add(MenuAction("See who boosted") { onOpenRebloggedBy(row.id) })
        add(MenuAction("Report post") { showReport = true })
        add(MenuAction("View author's posts") { onOpenAuthor(row.id) })
        add(MenuAction("View author's profile") { onOpenProfile(row.id) })
        add(MenuAction("Speak user info") { onSpeakUser(row.id) })
        add(MenuAction("Add or edit alias") { onAddAlias(row.id) })
        if (row.isReply) {
            add(MenuAction("Speak referenced reply") { onSpeakReply(row.id) })
            add(MenuAction("Jump to referenced reply") { onJumpToReply(row.id) })
        }
        if (row.isMine) {
            add(MenuAction("Edit") { onEdit(row.id) })
            add(MenuAction("Delete") { confirmDelete = true })
        }
    }
    val actions = menuActions.toAccessibilityActions()

    // Primary open: a grouped like/boost notification opens the list of everyone in
    // the group; every other row opens its thread.
    val primaryOpen: () -> Unit = when (row.groupActors) {
        "favorited_by" -> ({ onOpenFavoritedBy(row.id) })
        "reblogged_by" -> ({ onOpenRebloggedBy(row.id) })
        else -> ({ onOpenThread(row.id) })
    }

    val base = Modifier
        .combinedClickable(
            onClick = { primaryOpen() },
            onLongClick = { menuOpen = true },
        )
        .clearAndSetSemantics {
            contentDescription = row.text
            customActions = actions
            onClick { primaryOpen(); true }
        }
        .padding(horizontal = 16.dp, vertical = 12.dp)

    Column(modifier = base) {
        Text(row.text, style = MaterialTheme.typography.bodyLarge)
        DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
            menuActions.forEach { action ->
                DropdownMenuItem(
                    text = { Text(action.label) },
                    onClick = { action.run(); menuOpen = false },
                )
            }
        }
    }
    HorizontalDivider()

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("Delete post?") },
            text = { Text("This permanently deletes your post.") },
            confirmButton = {
                TextButton(onClick = { confirmDelete = false; onDelete(row.id) }) { Text("Delete") }
            },
            dismissButton = {
                TextButton(onClick = { confirmDelete = false }) { Text("Cancel") }
            },
        )
    }

    if (showReport) {
        ReportDialog(
            remote = false,
            onSubmit = { category, comment, forward ->
                onReport(row.id, category, comment, forward)
            },
            onDismiss = { showReport = false },
        )
    }
}
