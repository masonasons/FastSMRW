package me.masonasons.fastsm.ui.home

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
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
import me.masonasons.fastsm.ui.AccountUi

/**
 * Visual account picker, adapted to the core's account model. Tapping opens a
 * DropdownMenu listing every account plus "Add account" and "Log out" — the
 * same set the TalkBack custom actions expose.
 */
@Composable
fun AccountPicker(
    accounts: List<AccountUi>,
    selectedKey: String,
    onSwitch: (String) -> Unit,
    onAddAccount: () -> Unit,
    onLogOut: (String) -> Unit,
    onAccountSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val active = accounts.firstOrNull { it.key == selectedKey } ?: accounts.firstOrNull()
    val others = accounts.filter { it.key != active?.key }
    var menuOpen by remember { mutableStateOf(false) }

    val spoken = buildString {
        append("Active account: ")
        append(active?.let { "${it.displayName} (@${it.handle}) on ${it.platform}" } ?: "none")
        if (accounts.size > 1) append(". Tap to switch or use actions.")
        else append(". Tap to add another account.")
    }

    val menuActions = buildList {
        others.forEach { account ->
            add(MenuAction("Switch to ${account.displayName} (@${account.handle})") { onSwitch(account.key) })
        }
        if (active != null) add(MenuAction("Account settings") { onAccountSettings() })
        add(MenuAction("Add account") { onAddAccount() })
        if (active != null) add(MenuAction("Log out @${active.handle}") { onLogOut(active.key) })
    }
    val actions = menuActions.toAccessibilityActions()

    Column(
        modifier = modifier
            .fillMaxWidth()
            .clickable { menuOpen = true }
            .clearAndSetSemantics {
                contentDescription = spoken
                customActions = actions
                onClick { menuOpen = true; true }
            }
            .padding(horizontal = 16.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        if (active == null) {
            Text("No account selected", style = MaterialTheme.typography.labelLarge)
        } else {
            Text(active.displayName, style = MaterialTheme.typography.labelLarge)
            Text(
                "@${active.handle} · ${active.platform}",
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        DropdownMenu(expanded = menuOpen, onDismissRequest = { menuOpen = false }) {
            others.forEach { account ->
                DropdownMenuItem(
                    text = {
                        Column {
                            Text(account.displayName, style = MaterialTheme.typography.labelLarge)
                            Text(
                                "@${account.handle} · ${account.platform}",
                                style = MaterialTheme.typography.bodyLarge,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    },
                    onClick = { onSwitch(account.key); menuOpen = false },
                )
            }
            if (others.isNotEmpty()) HorizontalDivider()
            if (active != null) {
                DropdownMenuItem(
                    text = { Text("Account settings") },
                    onClick = { onAccountSettings(); menuOpen = false },
                )
            }
            DropdownMenuItem(
                text = { Text("Add account") },
                onClick = { onAddAccount(); menuOpen = false },
            )
            if (active != null) {
                DropdownMenuItem(
                    text = { Text("Log out @${active.handle}") },
                    onClick = { onLogOut(active.key); menuOpen = false },
                )
            }
        }
    }
}
