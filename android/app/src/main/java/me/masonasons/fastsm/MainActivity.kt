package me.masonasons.fastsm

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityEvent
import me.masonasons.fastsm.keyboard.KeyboardShortcuts
import me.masonasons.fastsm.keyboard.KeyboardHelp
import me.masonasons.fastsm.keyboard.KeyboardTest
import me.masonasons.fastsm.keyboard.Shortcut
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
import me.masonasons.fastsm.push.FastSmMessagingService
import me.masonasons.fastsm.ui.CoreViewModel
import me.masonasons.fastsm.ui.ProfileEditorDialog
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
    private var hasResumed = false
    var keyboardHandler: ((Shortcut) -> Boolean)? = null
        private set
    var showKeyboardHelp by mutableStateOf(false)
    var showKeyboardTest by mutableStateOf(false)
    fun openKeyboardTest() {
        inputRouter.cancelPrefix()
        showKeyboardHelp = false
        showKeyboardTest = true
    }
    private val inputRouter = me.masonasons.fastsm.keyboard.KeyboardInputRouter(
        prefixMessage = { vm.announceKeyboard(it) })
    private var composerSendHandler: (() -> Boolean)? = null

    fun bindComposerSendHandler(handler: () -> Boolean) { composerSendHandler = handler }
    fun unbindComposerSendHandler(handler: () -> Boolean) {
        if (composerSendHandler === handler) composerSendHandler = null
    }

    // Public android.app.Activity override. The inherited AndroidX Core class
    // carries a library-group annotation that lint also applies to this SDK API.
    @android.annotation.SuppressLint("RestrictedApi")
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val state = event.metaState
        val control = me.masonasons.fastsm.keyboard.KeyboardMeta.control(state)
        val shift = me.masonasons.fastsm.keyboard.KeyboardMeta.shift(state)
        val alt = me.masonasons.fastsm.keyboard.KeyboardMeta.alt(state)
        val meta = me.masonasons.fastsm.keyboard.KeyboardMeta.meta(state)
        if (inputRouter.event(event.action, event.keyCode, event.repeatCount, event.downTime,
            event.eventTime, control, shift, alt, meta, event.isCanceled,
            keyboardHandler, composerSendHandler)) return true
        return super.dispatchKeyEvent(event)
    }

    private val focusBridge by lazy {
        me.masonasons.fastsm.keyboard.KeyboardFocusBridge(vm::keyboardFocus)
    }
    private var focusRoot: ViewGroup? = null
    private var postFocusEnabled = false
    private val installPostFocus = Runnable {
        if (postFocusEnabled) focusRoot?.let { focusBridge.install(it) }
    }

    fun bindKeyboardHandler(handler: (Shortcut) -> Boolean, trackPosts: Boolean) {
        keyboardHandler = handler
        setPostFocusTracking(trackPosts)
    }

    fun unbindKeyboardHandler(handler: (Shortcut) -> Boolean) {
        if (keyboardHandler !== handler) return
        keyboardHandler = null
        inputRouter.cancelPrefix()
        setPostFocusTracking(false)
    }

    private fun setPostFocusTracking(enabled: Boolean) {
        if (postFocusEnabled == enabled) return
        postFocusEnabled = enabled
        focusRoot?.removeCallbacks(installPostFocus)
        if (enabled) focusRoot?.post(installPostFocus)
        else {
            focusBridge.uninstall()
            vm.clearKeyboardFocus()
        }
    }

    private fun observePostFocus() {
        val root = findViewById<ViewGroup>(android.R.id.content)
        focusRoot = root
        // One discovery when entering the reading screen. No layout listener
        // and no post-focus interception while writing or in settings.
        if (postFocusEnabled) root.post(installPostFocus)
    }

    override fun onDestroy() {
        focusRoot?.removeCallbacks(installPostFocus)
        focusBridge.uninstall()
        focusRoot = null
        super.onDestroy()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // The channel must exist before the first push arrives, and a push can
        // land while the app is closed -- so create it now, not when the
        // Notifications setting is opened. Renewing the subscription itself
        // waits for the accounts to load (see CoreViewModel).
        FastSmMessagingService.ensureChannels(this)
        handleOAuthRedirect(intent)
        setContent {
            FastSmTheme {
                Surface {
                    App(vm)
                }
            }
        }
        observePostFocus()
    }

    override fun onResume() {
        super.onResume()
        // Startup already loads and syncs the timelines. Later resumes begin a
        // new marker-sync window so a position moved on another client is pulled.
        if (hasResumed) vm.resume() else hasResumed = true
    }

    override fun onPause() {
        super.onPause()
        // Android can kill a backgrounded process at any time, so send the reading
        // position now instead of waiting out the idle timer.
        vm.pause()
        inputRouter.clear()
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
    val profileEditor by vm.profileEditor.collectAsStateWithLifecycle()
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
    // Copy the core-composed string to the system clipboard on request.
    LaunchedEffect(vm) {
        vm.copyText.collect { text ->
            val cm = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
            cm?.setPrimaryClip(ClipData.newPlainText("FastSM", text))
        }
    }

    (context as? MainActivity)?.let { activity ->
        if (activity.showKeyboardHelp) KeyboardHelp(
            onTest = { activity.openKeyboardTest() },
            onClose = { activity.showKeyboardHelp = false })
        if (activity.showKeyboardTest) KeyboardTest { activity.showKeyboardTest = false }
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
            onEditProfile = { vm.openProfileEditor() },
        )
    }

    // Edit Profile dialog, shown over whatever screen is active.
    profileEditor?.let { editor ->
        ProfileEditorDialog(
            editor = editor,
            onSubmit = { u ->
                vm.updateProfile(u.displayName, u.note, u.locked, u.bot, u.discoverable,
                    u.sensitive, u.privacy, u.fields)
                vm.dismissProfileEditor()
            },
            onDismiss = { vm.dismissProfileEditor() },
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
