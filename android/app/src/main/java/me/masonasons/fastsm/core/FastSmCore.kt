package me.masonasons.fastsm.core

import android.content.Context
import org.json.JSONObject
import java.io.File

/**
 * App-facing wrapper around [FastSmNative]. Owns one core instance, marshals
 * events off the core thread onto a listener, and builds the config (paths +
 * user agent) the core needs. The UI layer sends commands via [dispatch] and
 * renders whatever events arrive.
 *
 * Events are delivered on the core's background thread — callers that touch UI
 * must hop to the main thread themselves (e.g. a ViewModel posting to a
 * StateFlow).
 */
class FastSmCore private constructor(private var handle: Long) {

    fun interface EventListener {
        fun onEvent(json: JSONObject)
    }

    @Volatile
    private var listener: EventListener? = null

    fun setEventListener(l: EventListener?) {
        listener = l
    }

    /** Submit a command object, e.g. `{"cmd":"start"}`. */
    fun dispatch(command: JSONObject) {
        if (handle != 0L) FastSmNative.nativeDispatch(handle, command.toString())
    }

    fun dispatch(cmd: String, build: (JSONObject.() -> Unit)? = null) {
        val obj = JSONObject().put("cmd", cmd)
        build?.invoke(obj)
        dispatch(obj)
    }

    fun destroy() {
        val h = handle
        handle = 0L
        listener = null
        if (h != 0L) FastSmNative.nativeDestroy(h)
    }

    private fun onNativeEvent(json: String) {
        val l = listener ?: return
        val obj = try {
            JSONObject(json)
        } catch (_: Exception) {
            return
        }
        l.onEvent(obj)
    }

    companion object {
        val version: String get() = FastSmNative.nativeVersion()

        /**
         * Create the core with app-private data dirs. Bundled soundpacks/keymaps
         * are unpacked from assets into internal storage so the native core (which
         * reads them as files) can find them; Phase 0 just points at the dirs.
         */
        fun create(context: Context): FastSmCore {
            val configDir = File(context.filesDir, "core").apply { mkdirs() }
            val keymaps = File(context.filesDir, "keymaps").apply { mkdirs() }

            // The native SoundManager reads soundpacks as files, so unpack the
            // bundled default pack from assets on first run.
            val soundpacks = File(context.filesDir, "soundpacks")
            if (!File(soundpacks, "default").exists()) {
                runCatching { unpackAssets(context, "soundpacks", soundpacks) }
            }
            soundpacks.mkdirs()

            val config = JSONObject().apply {
                put("config_dir", configDir.absolutePath)
                put("soundpacks_dir", soundpacks.absolutePath)
                put("keymaps_dir", keymaps.absolutePath)
                put("user_agent", "FastSMRW-Android/${version}")
            }

            // Hold the instance in a box so the sink lambda can reach it before
            // the constructor returns.
            val box = arrayOfNulls<FastSmCore>(1)
            val sink = FastSmNative.EventSink { json -> box[0]?.onNativeEvent(json) }
            val h = FastSmNative.nativeCreate(config.toString(), sink, HttpBridge())
            val core = FastSmCore(h)
            box[0] = core
            return core
        }

        /** Recursively copy an assets subtree to [target] (dir mirrors assets). */
        private fun unpackAssets(context: Context, assetPath: String, target: File) {
            val am = context.assets
            val entries = am.list(assetPath) ?: return
            if (entries.isEmpty()) {
                // A leaf (file): copy it.
                target.parentFile?.mkdirs()
                am.open(assetPath).use { input ->
                    target.outputStream().use { output -> input.copyTo(output) }
                }
                return
            }
            target.mkdirs()
            for (e in entries) unpackAssets(context, "$assetPath/$e", File(target, e))
        }
    }
}
