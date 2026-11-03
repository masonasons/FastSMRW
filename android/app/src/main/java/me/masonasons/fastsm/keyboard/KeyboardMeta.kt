package me.masonasons.fastsm.keyboard

import android.view.KeyEvent

/** IMEs can supply a left/right modifier flag without the universal flag. */
object KeyboardMeta {
    fun control(state: Int) = state and KeyEvent.META_CTRL_MASK != 0
    fun shift(state: Int) = state and KeyEvent.META_SHIFT_MASK != 0
    fun alt(state: Int) = state and KeyEvent.META_ALT_MASK != 0
    fun meta(state: Int) = state and KeyEvent.META_META_MASK != 0

    /** Android activation keys count as Enter only when Control is present. */
    fun enterCode(code: Int, control: Boolean): Int = when {
        code == KeyEvent.KEYCODE_NUMPAD_ENTER -> KeyEvent.KEYCODE_ENTER
        control && code == KeyEvent.KEYCODE_DPAD_CENTER -> KeyEvent.KEYCODE_ENTER
        else -> code
    }
}
