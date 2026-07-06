package me.masonasons.fastsm.core

import okhttp3.Call
import okhttp3.Headers.Companion.toHeaders
import okhttp3.MediaType.Companion.toMediaTypeOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import okio.Buffer
import okio.BufferedSource
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong

/**
 * OkHttp-backed transport the native core calls through JNI. [send] handles
 * one-shot requests; the openStream/readChunk/closeStream trio backs the core's
 * long-lived SSE streaming (IHttpClient::send_stream), with the read loop living
 * on the native side. Bodies are `ByteArray` so binary payloads (multipart media
 * uploads) round-trip intact.
 *
 * All methods are invoked on core-owned worker/stream threads, never the UI
 * thread, and block until their result is ready — matching the IHttpClient
 * contract.
 */
class HttpBridge {
    private val client = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .writeTimeout(60, TimeUnit.SECONDS)
        .build()

    // Streaming (SSE) is long-lived: no read timeout, or the connection would be
    // torn down between server events.
    private val streamClient = client.newBuilder()
        .readTimeout(0, TimeUnit.SECONDS)
        .build()

    private val streams = ConcurrentHashMap<Long, StreamHolder>()
    private val nextStreamId = AtomicLong(1)

    private class StreamHolder(val call: Call, val response: Response, val source: BufferedSource)

    private fun buildRequest(
        method: String,
        url: String,
        headers: Array<String>,
        body: ByteArray?,
    ): Request {
        var contentType: String? = null
        val pairs = ArrayList<Pair<String, String>>(headers.size / 2)
        var i = 0
        while (i + 1 < headers.size) {
            val k = headers[i]
            val v = headers[i + 1]
            if (k.equals("Content-Type", ignoreCase = true)) contentType = v else pairs.add(k to v)
            i += 2
        }
        val hasBody = !(method.equals("GET", true) || method.equals("HEAD", true))
        val reqBody =
            if (hasBody) (body ?: ByteArray(0)).toRequestBody(contentType?.toMediaTypeOrNull())
            else null
        return Request.Builder()
            .url(url)
            .headers(pairs.toMap().toHeaders())
            .method(method, reqBody)
            .build()
    }

    /**
     * Perform one request. [headers] is a flat [k0, v0, k1, v1, ...] array. Never
     * throws — transport failures come back as [HttpResult] with status 0 and a
     * non-empty [HttpResult.error].
     */
    @Suppress("unused") // called from JNI
    fun send(method: String, url: String, headers: Array<String>, body: ByteArray?): HttpResult {
        return try {
            client.newCall(buildRequest(method, url, headers, body)).execute().use { resp ->
                val respHeaders = ArrayList<String>(resp.headers.size * 2)
                for (n in 0 until resp.headers.size) {
                    respHeaders.add(resp.headers.name(n))
                    respHeaders.add(resp.headers.value(n))
                }
                HttpResult(
                    status = resp.code,
                    headers = respHeaders.toTypedArray(),
                    body = resp.body?.bytes() ?: ByteArray(0),
                    error = "",
                )
            }
        } catch (e: Exception) {
            HttpResult(0, emptyArray(), ByteArray(0), e.message ?: e.toString())
        }
    }

    /** Open a streaming request; returns a stream id (0 on failure). */
    @Suppress("unused") // called from JNI
    fun openStream(method: String, url: String, headers: Array<String>, body: ByteArray?): Long {
        return try {
            val call = streamClient.newCall(buildRequest(method, url, headers, body))
            val response = call.execute()
            val source = response.body?.source()
            if (!response.isSuccessful || source == null) {
                response.close()
                return 0L
            }
            val id = nextStreamId.getAndIncrement()
            streams[id] = StreamHolder(call, response, source)
            id
        } catch (e: Exception) {
            0L
        }
    }

    /** Block for the next bytes on [id]; null on EOF, error, or cancellation. */
    @Suppress("unused") // called from JNI
    fun readChunk(id: Long): ByteArray? {
        val h = streams[id] ?: return null
        return try {
            val buf = Buffer()
            val n = h.source.read(buf, 8192)
            if (n == -1L) null else buf.readByteArray()
        } catch (e: Exception) {
            null // includes the IOException thrown when the stream is cancelled
        }
    }

    @Suppress("unused") // called from JNI
    fun closeStream(id: Long) {
        val h = streams.remove(id) ?: return
        runCatching { h.call.cancel() }
        runCatching { h.response.close() }
    }

    /** Abort every in-progress stream so blocked readChunk calls return. */
    @Suppress("unused") // called from JNI
    fun cancelAllStreams() {
        streams.keys.toList().forEach { closeStream(it) }
    }
}

/** Native reads these fields directly (JMField), so no getters/proguard needed. */
data class HttpResult(
    @JvmField val status: Int,
    @JvmField val headers: Array<String>,
    @JvmField val body: ByteArray,
    @JvmField val error: String,
)
