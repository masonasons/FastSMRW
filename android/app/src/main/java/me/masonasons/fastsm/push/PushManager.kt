package me.masonasons.fastsm.push

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import com.google.firebase.FirebaseApp
import com.google.firebase.messaging.FirebaseMessaging
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject

/**
 * The Android side of push notifications: the Firebase registration token, the
 * relay registration that turns it into a Web Push endpoint, and the on/off
 * preference. This is the OS-level plumbing an Android app must do itself.
 *
 * Everything portable stays in the core: it owns the Mastodon
 * /api/v1/push/subscription call for every signed-in account (push_subscribe /
 * push_unsubscribe), exactly as on iOS. The app only supplies the endpoint and
 * the public key material.
 *
 * Delivery path: Mastodon posts an encrypted Web Push body to the relay, the
 * relay forwards it to Firebase Cloud Messaging as a data-only message, and
 * [FastSmMessagingService] decrypts it on-device. The relay never sees the
 * contents.
 */
object PushManager {

    private const val TAG = "FastSmPush"

    private const val PREFS = "push"
    private const val ENABLED = "enabled"
    private const val ENDPOINT_ID = "endpoint_id"

    /** The self-hosted relay that bridges Mastodon Web Push to FCM and APNs. */
    private const val RELAY_BASE = "https://push.brynify.me"

    /**
     * Shared secret the relay's /register requires. Low-secrecy: it only gates
     * casual abuse of the registration endpoint, and mirrors
     * RELAY_REGISTER_TOKEN in the relay's config.
     */
    private const val RELAY_REGISTER_TOKEN =
        "be354d899a9bf6af74578ed7a631f2a916e25348d1e07f5fb4153121a13d90be"

    private val http = OkHttpClient()

    /** What the core needs to subscribe an account with its Mastodon server. */
    data class Subscription(val endpoint: String, val p256dh: String, val auth: String)

    /** Whether the user has turned push on (persisted). */
    fun isEnabled(context: Context): Boolean = prefs(context).getBoolean(ENABLED, false)

    fun setEnabled(context: Context, on: Boolean) {
        prefs(context).edit().putBoolean(ENABLED, on).apply()
    }

    /**
     * Whether this build can do push at all. Firebase needs a google-services.json
     * at build time; a build without one (a fork, or a local build without the
     * config) still runs, it just has no push.
     */
    fun isAvailable(context: Context): Boolean =
        runCatching { FirebaseApp.getApps(context).isNotEmpty() }.getOrDefault(false)

    /** Whether Android will let us post notifications (API 33+ asks the user). */
    fun hasNotificationPermission(context: Context): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU ||
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) ==
            PackageManager.PERMISSION_GRANTED

    /** The runtime permission to ask for on API 33+, or null when none is needed. */
    fun notificationPermission(): String? =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            Manifest.permission.POST_NOTIFICATIONS
        } else {
            null
        }

    /**
     * Turn push on: fetch the Firebase token, register it with the relay for a
     * per-device endpoint, and hand that endpoint plus our Web Push public keys
     * back so the caller can pass them to the core.
     *
     * [onResult] gets null if anything failed; it runs on a background thread.
     */
    fun enable(context: Context, onResult: (Subscription?) -> Unit) {
        setEnabled(context, true)
        register(context, onResult)
    }

    /**
     * Turn push off. The caller sends push_unsubscribe to the core (which drops
     * the Mastodon subscriptions); here we only stop this device receiving.
     */
    fun disable(context: Context) {
        setEnabled(context, false)
        // Drop the Firebase token so nothing more is delivered even if a
        // subscription lingers server-side.
        runCatching { FirebaseMessaging.getInstance().deleteToken() }
            .onFailure { Log.w(TAG, "deleteToken failed", it) }
    }

    /**
     * Called at app start: if push was left on, re-register and re-subscribe.
     * Mastodon subscriptions can lapse and the POST simply replaces, so this is
     * safe to repeat.
     */
    fun refreshIfEnabled(context: Context, onResult: (Subscription?) -> Unit) {
        if (isEnabled(context)) register(context, onResult)
    }

    /**
     * Re-point the existing endpoint at a new Firebase token, without touching
     * the Mastodon subscription. Firebase can rotate a token at any time; because
     * the endpoint id is stable, updating the relay's mapping is enough and the
     * subscription Mastodon already holds keeps delivering. Used from
     * [FastSmMessagingService.onNewToken], where the core isn't running.
     */
    fun onTokenRotated(context: Context, token: String) {
        if (!isEnabled(context)) return
        registerWithRelay(context, token) { endpoint ->
            if (endpoint == null) Log.w(TAG, "relay re-registration after token rotation failed")
        }
    }

    // ---- internals ----

    private fun register(context: Context, onResult: (Subscription?) -> Unit) {
        if (!isAvailable(context)) {
            Log.w(TAG, "no Firebase configuration in this build; push unavailable")
            onResult(null)
            return
        }
        val messaging = runCatching { FirebaseMessaging.getInstance() }.getOrNull()
        if (messaging == null) {
            onResult(null)
            return
        }
        messaging.token.addOnCompleteListener { task ->
            val token = task.result
            if (!task.isSuccessful || token.isNullOrEmpty()) {
                Log.w(TAG, "no Firebase token", task.exception)
                onResult(null)
                return@addOnCompleteListener
            }
            registerWithRelay(context, token) { endpoint ->
                if (endpoint == null) {
                    onResult(null)
                } else {
                    val keys = WebPushKeys.ensure(context)
                    onResult(Subscription(endpoint, keys.p256dh, keys.auth))
                }
            }
        }
    }

    /**
     * Trade the Firebase token for the relay endpoint URL Mastodon will post to.
     * The endpoint id is remembered and sent back on later registrations, so a
     * device keeps one endpoint instead of stranding a new one each time.
     */
    private fun registerWithRelay(
        context: Context,
        token: String,
        onEndpoint: (String?) -> Unit,
    ) {
        val body = JSONObject()
            .put("device_token", token)
            .put("platform", "fcm")
        prefs(context).getString(ENDPOINT_ID, null)?.let { body.put("endpoint_id", it) }

        val request = Request.Builder()
            .url("$RELAY_BASE/register")
            .header("Authorization", "Bearer $RELAY_REGISTER_TOKEN")
            .post(body.toString().toRequestBody("application/json".toMediaType()))
            .build()

        http.newCall(request).enqueue(object : okhttp3.Callback {
            override fun onFailure(call: okhttp3.Call, e: java.io.IOException) {
                Log.w(TAG, "relay register failed", e)
                onEndpoint(null)
            }

            override fun onResponse(call: okhttp3.Call, response: okhttp3.Response) {
                response.use {
                    val text = it.body?.string().orEmpty()
                    if (!it.isSuccessful) {
                        Log.w(TAG, "relay register: HTTP " + it.code)
                        onEndpoint(null)
                        return
                    }
                    val endpoint = runCatching { JSONObject(text).optString("endpoint") }
                        .getOrDefault("")
                    if (endpoint.isEmpty()) {
                        Log.w(TAG, "relay register: no endpoint in response")
                        onEndpoint(null)
                        return
                    }
                    prefs(context).edit()
                        .putString(ENDPOINT_ID, endpoint.substringAfterLast("/"))
                        .apply()
                    onEndpoint(endpoint)
                }
            }
        })
    }

    private fun prefs(context: Context) =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
}
