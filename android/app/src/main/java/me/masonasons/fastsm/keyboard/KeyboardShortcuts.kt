package me.masonasons.fastsm.keyboard

import android.view.KeyEvent

/** The same catalog drives decoding and the accessible help. No bare letters,
 * arrows, function keys or number chords are required. */
enum class Shortcut(val letter: Int, val shifted: Boolean, val chord: String, val description: String) {
    NEW(KeyEvent.KEYCODE_N, false, "Control+N", "Write a new post"),
    REPLY(KeyEvent.KEYCODE_R, false, "Control+R", "Reply to the focused post"),
    QUOTE(KeyEvent.KEYCODE_Q, false, "Control+Q", "Quote the focused post"),
    BOOST(KeyEvent.KEYCODE_B, false, "Control+B", "Boost or unboost the focused post"),
    FAVORITE(KeyEvent.KEYCODE_F, false, "Control+F", "Favourite or unfavourite the focused post"),
    BOOKMARK(KeyEvent.KEYCODE_M, false, "Control+M", "Bookmark or remove the bookmark"),
    MEDIA(KeyEvent.KEYCODE_P, false, "Control+P", "Open and play the focused post's media; pause or resume in the media viewer"),
    STOP(KeyEvent.KEYCODE_S, false, "Control+S", "Stop playback and close the media viewer"),
    LINKS(KeyEvent.KEYCODE_O, false, "Control+O", "Open links in the focused post"),
    BROWSER(KeyEvent.KEYCODE_O, true, "Control+Shift+O", "Open the focused post itself in the web browser"),
    COPY(KeyEvent.KEYCODE_C, false, "Control+C", "Copy the focused post"),
    AUTHOR(KeyEvent.KEYCODE_U, false, "Control+U", "Open the author's timeline"),
    PROFILE(KeyEvent.KEYCODE_U, true, "Control+Shift+U", "Open the author's profile"),
    MESSAGE(KeyEvent.KEYCODE_D, false, "Control+D", "Write a direct message to the author on Mastodon"),
    EDIT(KeyEvent.KEYCODE_E, false, "Control+E", "Edit your own focused post"),
    FOLLOW(KeyEvent.KEYCODE_L, false, "Control+L", "Follow or unfollow the author"),
    USER_INFO(KeyEvent.KEYCODE_I, false, "Control+I", "Read information about the author"),
    PARENT(KeyEvent.KEYCODE_I, true, "Control+Shift+I", "Read the post this reply is answering"),
    ALIAS(KeyEvent.KEYCODE_N, true, "Control+Shift+N", "Add or edit an alias for the author"),
    THREAD(KeyEvent.KEYCODE_ENTER, false, "Control+Enter", "Open the focused conversation; send while writing"),
    ACTIONS(KeyEvent.KEYCODE_ENTER, true, "Control+Shift+Enter", "Open the focused post's actions menu"),
    NEXT_TIMELINE(KeyEvent.KEYCODE_J, false, "Control+J", "Switch to the next open timeline"),
    PREVIOUS_TIMELINE(KeyEvent.KEYCODE_K, false, "Control+K", "Switch to the previous open timeline"),
    HOME(KeyEvent.KEYCODE_J, true, "Control+Shift+J", "Open the Home timeline"),
    NOTIFICATIONS(KeyEvent.KEYCODE_K, true, "Control+Shift+K", "Open Notifications"),
    MENTIONS(KeyEvent.KEYCODE_M, true, "Control+Shift+M", "Open Mentions"),
    SENT(KeyEvent.KEYCODE_D, true, "Control+Shift+D", "Open Sent posts"),
    ADD_TIMELINE(KeyEvent.KEYCODE_T, false, "Control+T", "Add a timeline or search"),
    CLOSE_TIMELINE(KeyEvent.KEYCODE_W, false, "Control+W", "Close the current timeline if it can be closed"),
    REFRESH(KeyEvent.KEYCODE_R, true, "Control+Shift+R", "Refresh the current timeline"),
    AUTO_READ(KeyEvent.KEYCODE_A, false, "Control+A", "Toggle automatic reading of new posts in this timeline"),
    FIND(KeyEvent.KEYCODE_F, true, "Control+Shift+F", "Find text in the current timeline"),
    FIND_NEXT(KeyEvent.KEYCODE_G, false, "Control+G", "Find the next match"),
    FIND_PREVIOUS(KeyEvent.KEYCODE_G, true, "Control+Shift+G", "Find the previous match"),
    FILTER(KeyEvent.KEYCODE_L, true, "Control+Shift+L", "Open the current timeline's display filter"),
    PIN_TIMELINE(KeyEvent.KEYCODE_P, true, "Control+Shift+P", "Pin or unpin the current timeline"),
    MUTE_TIMELINE(KeyEvent.KEYCODE_Q, true, "Control+Shift+Q", "Mute or unmute the current timeline's sounds"),
    ADD_ACCOUNT(KeyEvent.KEYCODE_A, true, "Control+Shift+A", "Add an account"),
    NEXT_ACCOUNT(KeyEvent.KEYCODE_C, true, "Control+Shift+C", "Switch to the next account"),
    PREVIOUS_ACCOUNT(KeyEvent.KEYCODE_B, true, "Control+Shift+B", "Switch to the previous account"),
    ACCOUNT_SETTINGS(KeyEvent.KEYCODE_S, true, "Control+Shift+S", "Open settings for this account"),
    EDIT_PROFILE(KeyEvent.KEYCODE_E, true, "Control+Shift+E", "Edit your profile"),
    SETTINGS(KeyEvent.KEYCODE_Y, false, "Control+Y", "Open application settings"),
    HELP(KeyEvent.KEYCODE_H, false, "Control+H", "Open keyboard help"),
    KEYBOARD_TEST(KeyEvent.KEYCODE_X, false, "Control+X", "Open keyboard test"),
    TOP(KeyEvent.KEYCODE_Z, false, "Control+Z", "Go to the top of the current timeline"),
    MESSAGES(KeyEvent.KEYCODE_W, true, "Control+Shift+W", "Open Messages on Mastodon"),
}

object KeyboardShortcuts {
    const val ROW_PREFIX = "fastsm-keyboard-row:"

    fun resolve(keyCode: Int, control: Boolean, shift: Boolean, alt: Boolean = false,
                meta: Boolean = false): Shortcut? {
        if (!control || alt || meta) return null
        // A letter alternative is usable when a device intercepts Enter chords.
        if (keyCode == KeyEvent.KEYCODE_T && shift) return Shortcut.THREAD
        val key = KeyboardMeta.enterCode(keyCode, control)
        return Shortcut.entries.firstOrNull { it.letter == key && it.shifted == shift }
    }

    /** Resolve only an actually focused row. Ambiguous descriptions and stale
     * row IDs never fall back to a previous post. */
    fun focusedRow(tag: String?, description: String?, rows: List<Pair<String, String>>): String? {
        return focusedRow(tag, description, rows, { it.first }, { it.second })
    }

    /** Stable tags scan IDs only, without allocating a copy of the timeline or
     * inspecting every post's spoken text for each focus action. */
    fun <T> focusedRow(tag: String?, description: String?, rows: List<T>,
                      idOf: (T) -> String, textOf: (T) -> String): String? {
        if (tag?.startsWith(ROW_PREFIX) == true) {
            val id = tag.removePrefix(ROW_PREFIX)
            return if (rows.any { idOf(it) == id }) id else null
        }
        if (description.isNullOrBlank()) return null
        var match: String? = null
        for (row in rows) {
            if (textOf(row) != description) continue
            if (match != null) return null
            match = idOf(row)
        }
        return match
    }
}
