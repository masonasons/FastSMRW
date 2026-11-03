package me.masonasons.fastsm.keyboard

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp

@Composable
fun KeyboardHelp(onTest: () -> Unit, onClose: () -> Unit) {
    AlertDialog(
        onDismissRequest = onClose,
        title = { Text("FastSMRW keyboard help", Modifier.semantics { heading() }) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                TextButton(onClick = onTest) { Text("Test keyboard shortcuts") }
                Text("Control Z goes to the top only when you request it. Refresh keeps your place. Control V then Control M opens Mentions; Control V then Control W opens Mastodon Messages. These choices are also near the top of More. Local reading positions are remembered by post ID and scroll offset, separately for each account and timeline.")
                Text("Control X opens Keyboard test directly from the timeline or media viewer. It retains Cut while writing. FastSMRW accepts Control commands delivered only when a key is released as well as complete press-and-release input.")
                Text("Use your usual BrailleSense or Android scrolling. Post actions apply to the post with screen-reader or keyboard focus. If no post is focused, the app asks you to focus one. These app commands use Control and letters, with Shift where shown. Ordinary writing and editing retain their usual keys.")
                Text("Without Shift: press Control V, release both keys, then press Control and the letter of the Shift command. For example, Control V then Control J opens Home; Control V then Control R refreshes. Control V then Control T opens the conversation; Control V then Control Z opens post actions. The second stroke has no Shift and must follow within 30 seconds. Control V again cancels. This prefix is available only on a screen that owns app commands; Control V still pastes while writing.")
                Shortcut.entries.forEach { shortcut ->
                    Text("${shortcut.chord}: ${shortcut.description}",
                        Modifier.fillMaxWidth().padding(vertical = 8.dp))
                    if (shortcut.shifted) {
                        val letter=if (shortcut == Shortcut.ACTIONS) "Z" else shortcut.chord.substringAfterLast('+')
                        Text("Without Shift: Control V, then Control $letter.")
                    }
                }
                Text("Writing: Control G sends from the focused main writing field without Shift or Enter. Control Enter and Control Shift T remain assigned to sending. Shift Enter inserts a new line. Plain Enter follows the Return key sends the post setting. Other post actions are inactive while writing. Control G on the timeline still finds the next match. In dialogs, use your device's usual navigation and activation commands.")
                Text("Tested by the contributor on a BrailleSense 7: no layered command tried so far has failed, and Reply continues to work reliably across repeated uses. Only a couple of Control+Shift combinations have worked in this testing, and those have been less reliable. Control+Shift behavior on other Android devices has not been tested. These are user-reported device results, separate from automated tests; they are not a claim that every function or Android device has been tested.")
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text("Close") } },
    )
}
