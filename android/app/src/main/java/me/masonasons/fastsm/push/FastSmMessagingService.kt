package me.masonasons.fastsm.push

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Base64
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage
import me.masonasons.fastsm.MainActivity
import me.masonasons.fastsm.R
import org.json.JSONObject

/**
 * Receives pushes from Firebase and shows them.
 *
 * The relay forwards Mastodon's Web Push body still encrypted, as a data-only
 * message, so this is where it becomes readable: decrypt with the device
 * keypair ([WebPushCrypto]) and post the notification. Android starts the
 * process for this even when the app is closed, and nothing here needs the
 * core, so no engine is spun up just to show a notification.
 */
class FastSmMessagingService : FirebaseMessagingService() {

    override fun onNewToken(token: String) {
        // Firebase rotated our token. The endpoint URL Mastodon posts to is
        // stable, so re-pointing the relay at the new token is enough -- the
        // subscription the server already holds keeps working.
        PushManager.onTokenRotated(applicationContext, token)
    }

    override fun onMessageReceived(message: RemoteMessage) {
        val data = message.data
        val blob = data["m"]
        var title = getString(R.string.app_name)
        var text = "New notification"
        var id = 0

        if (blob != null) {
            val payload = runCatching {
                Base64.decode(blob, Base64.URL_SAFE or Base64.NO_PADDING)
            }.getOrNull()?.let {
                WebPushCrypto.decrypt(
                    context = applicationContext,
                    body = it,
                    contentEncoding = data["ce"].orEmpty(),
                    encryptionHeader = data["enc"],
                    cryptoKeyHeader = data["ck"],
                )
            }
            if (payload == null) {
                // Keep the generic text rather than dropping the notification:
                // the user still learns something happened.
                Log.w(TAG, "could not decrypt push; showing generic text")
            } else {
                runCatching { JSONObject(String(payload, Charsets.UTF_8)) }.getOrNull()?.let { o ->
                    o.optString("title").takeIf { it.isNotEmpty() }?.let { title = it }
                    o.optString("body").takeIf { it.isNotEmpty() }?.let { text = it }
                    // Replace an earlier notification about the same event
                    // instead of stacking duplicates.
                    id = o.optString("notification_id").hashCode()
                }
            }
        }
        show(applicationContext, id, title, text)
    }

    companion object {
        private const val TAG = "FastSmPush"
        private const val CHANNEL = "notifications"

        /** Create the notification channel; safe to call repeatedly. */
        fun ensureChannel(context: Context) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
            val channel = NotificationChannel(
                CHANNEL,
                "Notifications",
                NotificationManager.IMPORTANCE_HIGH,
            )
            channel.description = "Mentions, boosts, favorites, follows and other activity."
            context.getSystemService(NotificationManager::class.java)
                ?.createNotificationChannel(channel)
        }

        private fun show(context: Context, id: Int, title: String, text: String) {
            ensureChannel(context)
            val tap = PendingIntent.getActivity(
                context,
                0,
                Intent(context, MainActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
            )
            val notification = NotificationCompat.Builder(context, CHANNEL)
                .setSmallIcon(android.R.drawable.stat_notify_chat)
                .setContentTitle(title)
                .setContentText(text)
                // The body is often longer than one line; let it expand so a
                // screen reader gets the whole thing.
                .setStyle(NotificationCompat.BigTextStyle().bigText(text))
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .setContentIntent(tap)
                .build()
            if (!PushManager.hasNotificationPermission(context)) return
            runCatching { NotificationManagerCompat.from(context).notify(id, notification) }
        }
    }
}
