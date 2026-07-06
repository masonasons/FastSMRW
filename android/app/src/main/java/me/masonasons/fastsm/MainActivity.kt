package me.masonasons.fastsm

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import me.masonasons.fastsm.ui.CoreViewModel
import me.masonasons.fastsm.ui.compose.ComposeScreen
import me.masonasons.fastsm.ui.home.HomeScreen
import me.masonasons.fastsm.ui.media.MediaScreen
import me.masonasons.fastsm.ui.profile.ProfileScreen
import me.masonasons.fastsm.ui.settings.AccountSettingsScreen
import me.masonasons.fastsm.ui.settings.SettingsScreen
import me.masonasons.fastsm.ui.setup.AddAccountScreen
import me.masonasons.fastsm.ui.timeline.AddTimelineScreen
import me.masonasons.fastsm.ui.theme.FastSmTheme
import me.masonasons.fastsm.util.CustomTabs

class MainActivity : ComponentActivity() {

    private val vm: CoreViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handleOAuthRedirect(intent)
        setContent {
            FastSmTheme {
                Surface {
                    App(vm)
                }
            }
        }
    }

    // singleTop: the fastsm://oauth redirect re-enters the live activity here.
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleOAuthRedirect(intent)
    }

    private fun handleOAuthRedirect(intent: Intent?) {
        val data = intent?.data ?: return
        if (data.scheme != "fastsm" || data.host != "oauth") return
        val code = data.getQueryParameter("code")
        if (!code.isNullOrBlank()) vm.finishMastodonLogin(code)
    }
}

@Composable
private fun App(vm: CoreViewModel) {
    val accounts by vm.accounts.collectAsStateWithLifecycle()
    val composeContext by vm.composeContext.collectAsStateWithLifecycle()
    val profile by vm.profile.collectAsStateWithLifecycle()
    val media by vm.media.collectAsStateWithLifecycle()
    val appUpdate by vm.appUpdate.collectAsStateWithLifecycle()
    // Show the add-account screen when there are no accounts, or when the user
    // explicitly asks to add one from the account picker.
    var addingAccount by remember { mutableStateOf(false) }
    var showSettings by remember { mutableStateOf(false) }
    var showAddTimeline by remember { mutableStateOf(false) }
    var showAccountSettings by remember { mutableStateOf(false) }

    val view = LocalView.current
    val context = LocalContext.current

    // Speak the core's announcements ("Signing in…", errors, …) through TalkBack.
    LaunchedEffect(vm) {
        vm.announcements.collect { msg -> view.announceForAccessibility(msg) }
    }
    // Mastodon OAuth: the core hands us the authorize URL to open in a browser.
    LaunchedEffect(vm) {
        vm.openUrls.collect { url -> CustomTabs.launch(context, Uri.parse(url)) }
    }

    val ctx = composeContext
    val prof = profile
    val med = media
    when {
        med != null -> MediaScreen(item = med, onClose = { vm.closeMedia() })
        ctx != null -> ComposeScreen(
            viewModel = vm,
            ctx = ctx,
            onDone = { vm.cancelCompose() },
        )
        prof != null -> ProfileScreen(
            viewModel = vm,
            profile = prof,
            onViewPosts = {
                vm.openUserTimelinePicked(prof.accountId, prof.acct)
                vm.closeProfile()
            },
            onOpenUrl = { url -> CustomTabs.launch(context, android.net.Uri.parse(url)) },
            onClose = { vm.closeProfile() },
        )
        accounts.isEmpty() || addingAccount -> AddAccountScreen(
            viewModel = vm,
            onLoggedIn = { addingAccount = false },
        )
        showSettings -> SettingsScreen(
            viewModel = vm,
            onClose = { showSettings = false },
        )
        showAddTimeline -> AddTimelineScreen(
            viewModel = vm,
            onClose = { showAddTimeline = false },
        )
        showAccountSettings -> AccountSettingsScreen(
            viewModel = vm,
            onClose = { showAccountSettings = false },
        )
        else -> HomeScreen(
            viewModel = vm,
            onAddAccount = { addingAccount = true },
            onOpenSettings = { showSettings = true },
            onOpenAddTimeline = { showAddTimeline = true },
            onOpenAccountSettings = { showAccountSettings = true },
        )
    }

    // A newer release was found: offer to open its APK download. Shown over
    // whatever screen is active (an AlertDialog is its own window).
    appUpdate?.let { update ->
        AlertDialog(
            onDismissRequest = { vm.dismissUpdate() },
            title = { Text("Update available") },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    Text("FastSMRW ${update.version} is available. Download it to update?")
                    if (update.notes.isNotBlank()) {
                        Text(update.notes, modifier = Modifier.padding(top = 12.dp))
                    }
                }
            },
            confirmButton = { TextButton(onClick = { vm.openUpdate() }) { Text("Download") } },
            dismissButton = { TextButton(onClick = { vm.dismissUpdate() }) { Text("Later") } },
        )
    }
}
