package me.masonasons.fastsm.keyboard

import android.view.KeyEvent

/** Ordinary text goes straight to Android. Only a Control command on a screen
 * that owns shortcuts reaches the catalog or its handler. */
class KeyboardInputRouter(
    private val clock: () -> Long = { System.nanoTime() / 1_000_000 },
    private val prefixMessage: (String) -> Unit = {},
    private val decode: (Int, Boolean, Boolean, Boolean, Boolean) -> Shortcut? = KeyboardShortcuts::resolve,
) {
    private val presses = ActionPresses()
    private val sequences = KeyEventSequence()
    private var prefixAt: Long? = null
    fun cancelPrefix() { prefixAt = null }
    fun keyUp(code: Int): Boolean = presses.up(code)
    fun clear() { presses.clear(); sequences.clear(); cancelPrefix() }
    fun directKeyDown(code: Int, repeats: Int, action: () -> Boolean): Boolean =
        presses.down(code, repeats, action)
    fun keyDown(code: Int, repeats: Int, control: Boolean, shift: Boolean,
                alt: Boolean, meta: Boolean, handler: ((Shortcut) -> Boolean)?): Boolean =
        keyDownEvent(code, repeats, 0, control, shift, alt, meta, handler)

    /** Production entry point for both complete and release-only event streams. */
    fun event(action: Int, code: Int, repeats: Int, downTime: Long, eventTime: Long,
              control: Boolean, shift: Boolean, alt: Boolean, meta: Boolean,
              cancelled: Boolean = false, handler: ((Shortcut) -> Boolean)? = null,
              send: (() -> Boolean)? = null): Boolean {
        fun dispatch(): Boolean {
            if (send != null && ComposerKeys.sendChord(code, control, shift, alt, meta) &&
                presses.downEvent(code, repeats, downTime, send)) return true
            return keyDownEvent(code, repeats, downTime, control, shift, alt, meta, handler)
        }
        if (action == KeyEvent.ACTION_DOWN) {
            sequences.down(code, downTime)
            return dispatch()
        }
        if (action != KeyEvent.ACTION_UP) return false
        val release = sequences.up(code, downTime, eventTime, cancelled)
        val handledDown = presses.upEvent(code, downTime)
        if (handledDown) return true
        if (release == KeyEventSequence.Release.DUPLICATE) return control
        if (release != KeyEventSequence.Release.RELEASE_ONLY) return false
        return try { dispatch() } finally { presses.upEvent(code, downTime) }
    }

    private fun keyDownEvent(code: Int, repeats: Int, downTime: Long, control: Boolean, shift: Boolean,
                             alt: Boolean, meta: Boolean, handler: ((Shortcut) -> Boolean)?): Boolean {
        // A command may open the composer before its physical key is released.
        if (presses.isPressedEvent(code, repeats, downTime)) return true
        // Releasing and pressing Control between the two strokes must not use up
        // the prefix. The modifier itself is not the requested command letter.
        if (code == KeyEvent.KEYCODE_CTRL_LEFT || code == KeyEvent.KEYCODE_CTRL_RIGHT ||
            code == KeyEvent.KEYCODE_SHIFT_LEFT || code == KeyEvent.KEYCODE_SHIFT_RIGHT) return false
        if (handler == null || !control || alt || meta) { cancelPrefix(); return false }
        if (code == KeyEvent.KEYCODE_X && !shift) {
            cancelPrefix()
            return presses.downEvent(code, repeats, downTime) { handler(Shortcut.KEYBOARD_TEST) }
        }
        if (code == KeyEvent.KEYCODE_V && !shift) {
            return presses.downEvent(code, repeats, downTime) {
                if (prefixAt != null) {
                    cancelPrefix(); prefixMessage("Additional command cancelled.")
                } else {
                    prefixAt = clock()
                    prefixMessage("Additional command. Release the keys, then press Control and the command letter. Control Z opens post actions; Control T opens the conversation.")
                }
                true
            }
        }
        val started = prefixAt
        if (started != null) {
            cancelPrefix()
            return presses.downEvent(code, repeats, downTime) {
                if (clock() - started > 30_000) {
                    prefixMessage("Additional command expired. Press Control V again.")
                    true
                } else {
                    val shortcut = when (code) {
                        KeyEvent.KEYCODE_Z -> Shortcut.ACTIONS
                        KeyEvent.KEYCODE_T -> Shortcut.THREAD
                        else -> decode(code, true, true, false, false)
                    }
                    if (shortcut == null) {
                        prefixMessage("No additional command for that key."); true
                    } else handler(shortcut)
                }
            }
        }
        val shortcut = decode(code, control, shift, alt, meta) ?: return false
        return presses.downEvent(code, repeats, downTime) { handler(shortcut) }
    }
}
