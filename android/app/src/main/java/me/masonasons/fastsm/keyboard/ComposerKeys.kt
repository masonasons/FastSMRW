package me.masonasons.fastsm.keyboard

import android.view.KeyEvent

enum class EnterAction { SEND, NEWLINE, DEFAULT, CONSUME }

object ComposerKeys {
    fun sendChord(code: Int, control: Boolean, shift: Boolean,
                  alt: Boolean = false, meta: Boolean = false): Boolean {
        if (!control || alt || meta) return false
        return (!shift && code == KeyEvent.KEYCODE_G) ||
            (!shift && KeyboardMeta.enterCode(code, true) == KeyEvent.KEYCODE_ENTER) ||
            (shift && code == KeyEvent.KEYCODE_T)
    }

    fun enter(control: Boolean, shift: Boolean, returnSends: Boolean,
              alt: Boolean = false, meta: Boolean = false, repeat: Boolean = false): EnterAction = when {
        alt || meta -> EnterAction.DEFAULT
        repeat -> EnterAction.CONSUME
        shift -> EnterAction.NEWLINE
        control || returnSends -> EnterAction.SEND
        else -> EnterAction.DEFAULT
    }
}
