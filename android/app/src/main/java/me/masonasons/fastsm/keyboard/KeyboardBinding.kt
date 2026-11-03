package me.masonasons.fastsm.keyboard

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.platform.LocalContext
import me.masonasons.fastsm.MainActivity

/** A screen owns shortcuts only while it is visible. Dialog windows keep their
 * normal keyboard handling and text editing never uses the home handler. */
@Composable
fun KeyboardBinding(enabled: Boolean = true, trackPostFocus: Boolean = false, handler: (Shortcut) -> Boolean) {
    val activity = LocalContext.current as? MainActivity
    val latest = rememberUpdatedState(handler)
    DisposableEffect(activity, enabled, trackPostFocus) {
        val binding: (Shortcut) -> Boolean = {
            when {
                !enabled -> false
                it == Shortcut.KEYBOARD_TEST -> { activity?.openKeyboardTest(); true }
                else -> latest.value(it)
            }
        }
        if (enabled) activity?.bindKeyboardHandler(binding, trackPostFocus)
        onDispose {
            activity?.unbindKeyboardHandler(binding)
        }
    }
}

/** Only the focused main writing field owns the two explicit send chords.
 * Other writing keys never consult the timeline shortcut catalog. */
@Composable
fun ComposerSendBinding(enabled: Boolean, handler: () -> Boolean) {
    val activity = LocalContext.current as? MainActivity
    val latest = rememberUpdatedState(handler)
    DisposableEffect(activity, enabled) {
        val binding: () -> Boolean = { latest.value() }
        if (enabled) activity?.bindComposerSendHandler(binding)
        onDispose { activity?.unbindComposerSendHandler(binding) }
    }
}
