package me.masonasons.fastsm.core

/**
 * Thin JNI surface over the shared C++ core (libfastsm.so). Kotlin exchanges
 * JSON only — commands in, events out — exactly like the Win32 front end does
 * over the C ABI. Prefer [FastSmCore] for app code; this object is the raw
 * binding.
 */
internal object FastSmNative {
    init {
        System.loadLibrary("fastsm")
    }

    /** Receives core events as UTF-8 JSON, on a core-owned (background) thread. */
    fun interface EventSink {
        fun onEvent(json: String)
    }

    /** Engine version string (from fastsm::version()). */
    external fun nativeVersion(): String

    /** Create a core instance; returns an opaque handle (0 on failure). */
    external fun nativeCreate(configJson: String, sink: EventSink, http: HttpBridge): Long

    /** Submit a command (JSON). Non-blocking; results arrive via the sink. */
    external fun nativeDispatch(handle: Long, commandJson: String)

    /** Destroy the instance, stopping its threads. No events fire afterward. */
    external fun nativeDestroy(handle: Long)
}
