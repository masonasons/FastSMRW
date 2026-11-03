package me.masonasons.fastsm.keyboard

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.view.KeyEvent
import android.view.Window
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.compose.ui.window.DialogWindowProvider
import me.masonasons.fastsm.BuildConfig

private fun signalText(signal: KeySignal?): String {
    if (signal == null) return "No Android key event received yet."
    val keys = listOfNotNull(
        if (KeyboardMeta.control(signal.state)) "Control" else null,
        if (KeyboardMeta.shift(signal.state)) "Shift" else null,
        if (KeyboardMeta.alt(signal.state)) "Alt" else null,
        if (KeyboardMeta.meta(signal.state)) "Meta" else null,
        KeyEvent.keyCodeToString(signal.code).removePrefix("KEYCODE_")
    )
    val action = when (signal.action) { KeyEvent.ACTION_DOWN -> "key pressed"; KeyEvent.ACTION_UP -> "key released"; else -> "other event" }
    return "Received: ${keys.joinToString(" plus ")}, $action."
}

@Composable
fun KeyboardTest(onClose: () -> Unit) {
    val probe = remember { KeyProbe() }
    var revision by remember { mutableIntStateOf(0) }
    val context = LocalContext.current
    fun latest() = probe.lastCommand ?: probe.lastDown ?: probe.events.lastOrNull()
    fun record(event: KeyEvent, route: String) {
        if (probe.record(KeySignal(event.action,event.keyCode,event.metaState,event.repeatCount,
                event.flags,event.source,event.eventTime,route,event.downTime))) revision++
    }
    fun report(): String = buildString {
        appendLine("FastSMRW ${BuildConfig.VERSION_NAME} keyboard test")
        appendLine(signalText(latest()))
        appendLine("Android events: ${probe.totalEvents}; committed input character count: ${probe.textInputCount}")
        for (e in probe.events) appendLine("${e.route} action=${e.action} key=${KeyEvent.keyCodeToString(e.code)} meta=${e.state} repeat=${e.repeats} flags=${e.flags} source=${e.source} down=${e.downTime} time=${e.time}")
    }
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        val view = LocalView.current
        val window = (view.parent as? DialogWindowProvider)?.window
        DisposableEffect(window) {
            val original = window?.callback
            val observer = original?.let { callback ->
                object : Window.Callback by callback {
                    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
                        record(event,"window")
                        // Explicit command chords are samples in this modal test.
                        if (KeyboardMeta.control(event.metaState) && event.keyCode != KeyEvent.KEYCODE_BACK) return true
                        return callback.dispatchKeyEvent(event)
                    }
                }
            }
            if (observer != null) window?.callback = observer
            onDispose {
                if (observer != null && window != null && window.callback === observer) window.callback = original
                probe.clear()
            }
        }
        Surface {
            Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(20.dp)
                .onPreviewKeyEvent { event ->
                    record(event.nativeKeyEvent,"field")
                    KeyboardMeta.control(event.nativeKeyEvent.metaState)
                }) {
                Text("Keyboard test",Modifier.semantics { heading() })
                Text("This test reports key names and modifiers received from Android. App commands do not run here. It keeps at most 32 events in memory and counts text input without keeping the characters. Closing the test clears its results.")
                Text("First try Control N, then Control R, then Control plus Backspace plus J, then Control Enter. Read the Received line after each attempt. Both key-press and key-release events are shown. The line keeps the last modified combination while you navigate; Copy test results includes every captured event.")
                Text("Try the combinations before entering the test input box and again after activating it. This checks reading and text-entry delivery.")
                val received = remember(revision) { signalText(latest()) }
                Text(received,Modifier.padding(vertical = 12.dp).semantics { liveRegion = LiveRegionMode.Polite })
                Text("Android events: ${probe.totalEvents}. Text input characters: ${probe.textInputCount}.")
                OutlinedTextField(value = "",onValueChange = { probe.textInput(it.length); revision++ },
                    label = { Text("Keyboard test input") },
                    supportingText = { Text("Activate this box for the text-entry test. Typed characters are discarded.") },
                    modifier = Modifier.fillMaxWidth())
                TextButton(onClick = { view.announceForAccessibility(signalText(latest())) }) { Text("Read last result") }
                Button(onClick = {
                    val clipboard=context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                    clipboard?.setPrimaryClip(ClipData.newPlainText("FastSM keyboard test",report()))
                    view.announceForAccessibility("Keyboard test results copied.")
                }) { Text("Copy test results") }
                TextButton(onClick = { probe.clear(); revision++ }) { Text("Clear test results") }
                TextButton(onClick = onClose) { Text("Close keyboard test") }
            }
        }
    }
}
