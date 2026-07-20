package me.masonasons.fastsm.ui.profile

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import me.masonasons.fastsm.ui.CoreViewModel
import me.masonasons.fastsm.ui.ProfileUi
import me.masonasons.fastsm.ui.ReportDialog

/**
 * A user's profile, driven by the core's user_profile event. [profile.text] is
 * the composed, accessible profile string; the buttons dispatch core commands.
 * Follow state is flipped optimistically (the core confirms with a spoken
 * announcement).
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun ProfileScreen(
    viewModel: CoreViewModel,
    profile: ProfileUi,
    onViewPosts: () -> Unit,
    onOpenUrl: (String) -> Unit,
    onClose: () -> Unit,
) {
    var following by remember(profile.accountId) { mutableStateOf(profile.following) }
    var requested by remember(profile.accountId) { mutableStateOf(profile.requested) }
    var showReport by remember(profile.accountId) { mutableStateOf(false) }

    BackHandler(enabled = true) { onClose() }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("@${profile.acct}") },
                navigationIcon = {
                    IconButton(onClick = onClose) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
                    }
                },
            )
        },
    ) { pad ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            Text(profile.text, style = MaterialTheme.typography.bodyLarge)

            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if (profile.hasRelationship) {
                    val label = when {
                        following -> "Unfollow"
                        requested -> "Cancel request"
                        else -> "Follow"
                    }
                    Button(onClick = {
                        if (following || requested) {
                            viewModel.setRelationship(profile.accountId, "unfollow", profile.acct)
                            following = false
                            requested = false
                        } else {
                            viewModel.setRelationship(profile.accountId, "follow", profile.acct)
                            // A locked account turns the tap into a pending request; we
                            // can't tell here, so assume it followed and let the spoken
                            // confirmation correct the user if it was a request.
                            following = true
                        }
                    }) { Text(label) }
                }

                Button(onClick = onViewPosts) { Text("View posts") }

                TextButton(onClick = { showReport = true }) { Text("Report") }

                if (profile.url.isNotBlank()) {
                    TextButton(onClick = { onOpenUrl(profile.url) }) { Text("Open in browser") }
                }
            }
        }
    }

    if (showReport) {
        ReportDialog(
            remote = profile.acct.contains("@"),
            onSubmit = { category, comment, forward ->
                viewModel.reportUser(profile.accountId, profile.acct, category, comment, forward)
            },
            onDismiss = { showReport = false },
        )
    }
}
