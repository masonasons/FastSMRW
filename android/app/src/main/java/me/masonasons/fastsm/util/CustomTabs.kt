package me.masonasons.fastsm.util

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.browser.customtabs.CustomTabsIntent

/**
 * Opens a URL in a Chrome Custom Tab (in-app browser) for the Mastodon OAuth
 * flow, falling back to the system browser. Either way the fastsm://oauth
 * redirect returns to MainActivity via its deep-link intent-filter.
 */
object CustomTabs {
    fun launch(context: Context, uri: Uri) {
        try {
            CustomTabsIntent.Builder().build().launchUrl(context, uri)
        } catch (_: Exception) {
            runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, uri)) }
        }
    }
}
