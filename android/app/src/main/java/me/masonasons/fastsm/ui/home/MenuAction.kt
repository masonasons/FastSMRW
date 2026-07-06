package me.masonasons.fastsm.ui.home

import androidx.compose.ui.semantics.CustomAccessibilityAction

/**
 * A single contextual action on an item (post, tab, account). Rendered both as
 * a [CustomAccessibilityAction] for TalkBack's custom-action menu and as a row
 * in a visible [androidx.compose.material3.DropdownMenu] (tap or long-press),
 * from one list so the two paths can't drift.
 */
data class MenuAction(val label: String, val run: () -> Unit)

fun List<MenuAction>.toAccessibilityActions(): List<CustomAccessibilityAction> =
    map { a -> CustomAccessibilityAction(a.label) { a.run(); true } }
