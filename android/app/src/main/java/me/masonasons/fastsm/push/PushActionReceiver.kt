package me.masonasons.fastsm.push

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.RemoteInput
import me.masonasons.fastsm.core.FastSmCore
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

/**
 * Runs a push notification's Reply, Favourite or Boost button.
 *
 * The press can come with the app closed, so this starts its own short-lived
 * core and sends it push_action; the core finds the account by the push's
 * access token, looks the notification's post up and acts on it. The outcome is
 * a toast, which screen readers read, and the notification goes once it worked.
 */
class PushActionReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.getStringExtra(EXTRA_ACTION) ?: return
        val token = intent.getStringExtra(EXTRA_TOKEN) ?: return
        val notificationId = intent.getStringExtra(EXTRA_NOTIFICATION_ID) ?: return
        val shownId = intent.getIntExtra(EXTRA_SHOWN_ID, 0)
        val text = RemoteInput.getResultsFromIntent(intent)?.getCharSequence(KEY_REPLY)?.toString()?.trim()
        val app = context.applicationContext
        val pending = goAsync()
        thread(name = "fastsm-push-action") {
            try {
                val ok = run(app, token, notificationId, action, text)
                if (ok) NotificationManagerCompat.from(app).cancel(shownId)
                val message = when {
                    !ok -> "Couldn't ${verb(action)}. Open FastSMRW to try again."
                    action == ACTION_REPLY -> "Reply sent"
                    action == ACTION_BOOST -> "Boosted"
                    else -> "Favourited"
                }
                Handler(Looper.getMainLooper()).post { Toast.makeText(app, message, Toast.LENGTH_SHORT).show() }
            } finally {
                pending.finish()
            }
        }
    }

    private fun run(context: Context, token: String, id: String, action: String, text: String?): Boolean {
        val core = FastSmCore.create(context)
        val done = CountDownLatch(1)
        var ok = false
        core.setEventListener { e ->
            if (e.optString("event") == "push_action_result") {
                ok = e.optBoolean("ok")
                done.countDown()
            }
        }
        core.dispatch("push_action") {
            put("access_token", token)
            put("notification_id", id)
            put("action", action)
            if (text != null) put("text", text)
        }
        // A broadcast has about ten seconds; leave room to post the toast.
        val answered = done.await(8, TimeUnit.SECONDS)
        core.destroy()
        return answered && ok
    }

    private fun verb(action: String) = when (action) {
        ACTION_REPLY -> "send the reply"
        ACTION_BOOST -> "boost"
        else -> "favourite"
    }

    companion object {
        const val ACTION_REPLY = "reply"
        const val ACTION_FAVOURITE = "favourite"
        const val ACTION_BOOST = "boost"
        private const val KEY_REPLY = "reply_text"
        private const val EXTRA_ACTION = "action"
        private const val EXTRA_TOKEN = "access_token"
        private const val EXTRA_NOTIFICATION_ID = "notification_id"
        private const val EXTRA_SHOWN_ID = "shown_id"

        /** Mastodon notification types that are about a post someone else wrote. */
        private val ACTIONABLE_TYPES = setOf("mention", "status")

        /**
         * Adds Reply, Favourite and Boost to a mention's notification. Nothing is
         * added without the account's token and the notification id from the push.
         */
        fun addActions(
            context: Context,
            builder: NotificationCompat.Builder,
            shownId: Int,
            type: String,
            token: String,
            notificationId: String,
        ) {
            if (type !in ACTIONABLE_TYPES || token.isEmpty() || notificationId.isEmpty()) return
            fun intent(action: String, index: Int, mutable: Boolean): PendingIntent {
                val i = Intent(context, PushActionReceiver::class.java)
                    .putExtra(EXTRA_ACTION, action)
                    .putExtra(EXTRA_TOKEN, token)
                    .putExtra(EXTRA_NOTIFICATION_ID, notificationId)
                    .putExtra(EXTRA_SHOWN_ID, shownId)
                // Reply's must be mutable so the system can add the typed text.
                val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                    (if (mutable) PendingIntent.FLAG_MUTABLE else PendingIntent.FLAG_IMMUTABLE)
                return PendingIntent.getBroadcast(context, shownId * 3 + index, i, flags)
            }
            val reply = RemoteInput.Builder(KEY_REPLY).setLabel("Reply").build()
            builder
                .addAction(
                    NotificationCompat.Action.Builder(0, "Reply", intent(ACTION_REPLY, 0, mutable = true))
                        .addRemoteInput(reply)
                        .setAllowGeneratedReplies(false)
                        .build(),
                )
                .addAction(0, "Favourite", intent(ACTION_FAVOURITE, 1, mutable = false))
                .addAction(0, "Boost", intent(ACTION_BOOST, 2, mutable = false))
        }
    }
}
