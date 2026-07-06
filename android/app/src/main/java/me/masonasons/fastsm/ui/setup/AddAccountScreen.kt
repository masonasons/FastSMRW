package me.masonasons.fastsm.ui.setup

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import me.masonasons.fastsm.ui.CoreViewModel

private enum class Platform(val displayName: String) { MASTODON("Mastodon"), BLUESKY("Bluesky") }

/**
 * Add-account screen, driven by the core. Bluesky (app-password) dispatches
 * add_account and lands on the timeline when the core replies auth_result ok.
 * Mastodon interactive OAuth is wired in a later step (the core currently
 * replies with an error on this platform).
 */
@Composable
fun AddAccountScreen(
    viewModel: CoreViewModel,
    onLoggedIn: () -> Unit,
) {
    var platform by remember { mutableStateOf(Platform.BLUESKY) }
    var instance by remember { mutableStateOf("") }
    var handle by remember { mutableStateOf("") }
    var appPassword by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(viewModel) {
        viewModel.authResults.collect { result ->
            busy = false
            if (result.ok) onLoggedIn()
            else error = result.error.ifBlank { "Login failed" }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Add account", style = MaterialTheme.typography.titleLarge)

        val entries = Platform.entries
        SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
            entries.forEachIndexed { index, p ->
                SegmentedButton(
                    selected = p == platform,
                    onClick = { if (!busy) { platform = p; error = null } },
                    shape = SegmentedButtonDefaults.itemShape(index = index, count = entries.size),
                    enabled = !busy,
                ) { Text(p.displayName) }
            }
        }

        when (platform) {
            Platform.MASTODON -> {
                Text(
                    "Enter your Mastodon instance. You'll be sent to your browser to log in.",
                    style = MaterialTheme.typography.bodyLarge,
                )
                OutlinedTextField(
                    value = instance,
                    onValueChange = { instance = it; error = null },
                    label = { Text("Instance (e.g. mastodon.social)") },
                    singleLine = true,
                    enabled = !busy,
                    modifier = Modifier.fillMaxWidth(),
                )
            }

            Platform.BLUESKY -> {
                var revealed by remember { mutableStateOf(false) }
                Text(
                    "Enter your Bluesky handle and an app password. " +
                        "Create one at bsky.app → Settings → App Passwords.",
                    style = MaterialTheme.typography.bodyLarge,
                )
                OutlinedTextField(
                    value = handle,
                    onValueChange = { handle = it; error = null },
                    label = { Text("Handle (e.g. alice.bsky.social)") },
                    singleLine = true,
                    enabled = !busy,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = appPassword,
                    onValueChange = { appPassword = it; error = null },
                    label = { Text("App password") },
                    singleLine = true,
                    enabled = !busy,
                    visualTransformation = if (revealed) VisualTransformation.None else PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        Button(
            enabled = !busy,
            onClick = {
                error = null
                when (platform) {
                    Platform.BLUESKY -> {
                        val h = handle.trim().removePrefix("@")
                        if (h.isBlank() || appPassword.isBlank()) {
                            error = "Enter your handle and app password"
                        } else {
                            busy = true
                            viewModel.addBluesky(h, appPassword)
                        }
                    }
                    Platform.MASTODON -> {
                        val i = instance.trim()
                        if (i.isBlank()) {
                            error = "Enter an instance URL (e.g. mastodon.social)"
                        } else {
                            busy = true
                            viewModel.beginMastodonLogin(i)
                        }
                    }
                }
            },
        ) {
            Text(if (busy) "Signing in…" else "Log in")
        }

        if (busy) {
            Spacer(Modifier.height(4.dp))
            CircularProgressIndicator()
        }

        error?.let { err ->
            Text(err, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyLarge)
        }
    }
}
