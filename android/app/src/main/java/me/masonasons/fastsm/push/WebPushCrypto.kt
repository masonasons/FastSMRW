package me.masonasons.fastsm.push

import android.content.Context
import android.util.Base64
import java.security.PrivateKey
import javax.crypto.Cipher
import javax.crypto.KeyAgreement
import javax.crypto.Mac
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * RFC 8291 (Web Push message encryption) over RFC 8188 (the aes128gcm content
 * coding), plus the older "aesgcm" coding that Mastodon's web-push library
 * still emits. [FastSmMessagingService] uses this to decrypt an incoming push
 * on-device with the keypair in [WebPushKeys].
 *
 * The mirror of ios/src/WebPushDecrypt.swift, built on the JDK's own providers
 * (ECDH key agreement, HMAC-SHA256, AES-GCM) so the app needs no crypto library.
 */
object WebPushCrypto {

    /**
     * Decrypt whatever the relay forwarded and return the plaintext (Mastodon's
     * push JSON), or null if anything is missing or authentication fails.
     *
     * [contentEncoding] is the push's Content-Encoding; for "aesgcm" the salt
     * and the sender's key arrive separately in the Encryption / Crypto-Key
     * header values, which the relay passes through verbatim.
     */
    fun decrypt(
        context: Context,
        body: ByteArray,
        contentEncoding: String,
        encryptionHeader: String?,
        cryptoKeyHeader: String?,
    ): ByteArray? {
        val priv = WebPushKeys.privateKey(context) ?: return null
        val uaPublic = WebPushKeys.publicPoint(context) ?: return null
        val auth = WebPushKeys.authSecret(context) ?: return null
        return runCatching {
            if (contentEncoding == "aesgcm") {
                val salt = header("salt", encryptionHeader)?.let { b64urlDecode(it) }
                val serverPublic = header("dh", cryptoKeyHeader)?.let { b64urlDecode(it) }
                if (salt == null || serverPublic == null) null
                else decryptAesGcm(body, salt, serverPublic, priv, uaPublic, auth)
            } else {
                decryptAes128Gcm(body, priv, uaPublic, auth)
            }
        }.getOrNull()
    }

    /**
     * The current coding. Body layout (RFC 8188 section 2.1):
     *
     *     salt(16) | rs(4, uint32) | idlen(1) | keyid(idlen) | ciphertext+tag
     *
     * For Web Push the keyid is the sender's ephemeral P-256 public key.
     *
     * Internal rather than private so the unit test can drive it with the RFC
     * 8291 test vector, which needs the key material passed in directly.
     */
    internal fun decryptAes128Gcm(
        body: ByteArray,
        priv: PrivateKey,
        uaPublic: ByteArray,
        auth: ByteArray,
    ): ByteArray? {
        if (body.size <= 21) return null
        val salt = body.copyOfRange(0, 16)
        val idLen = body[20].toInt() and 0xff
        if (idLen != 65) return null // a P-256 uncompressed point
        val headerEnd = 21 + idLen
        if (body.size <= headerEnd + 16) return null
        val serverPublic = body.copyOfRange(21, headerEnd)
        val sealed = body.copyOfRange(headerEnd, body.size)

        val ikm = webPushIkm(priv, serverPublic, uaPublic, auth) ?: return null
        val cek = hkdf(salt, ikm, label("Content-Encoding: aes128gcm"), 16)
        val nonce = hkdf(salt, ikm, label("Content-Encoding: nonce"), 12)
        return stripPadding(aesGcmOpen(cek, nonce, sealed) ?: return null)
    }

    /**
     * The older coding (draft-ietf-webpush-encryption-04): the body is just the
     * ciphertext, and the derivation mixes in a "context" naming both keys.
     */
    private fun decryptAesGcm(
        body: ByteArray,
        salt: ByteArray,
        serverPublic: ByteArray,
        priv: PrivateKey,
        uaPublic: ByteArray,
        auth: ByteArray,
    ): ByteArray? {
        if (body.size <= 16) return null
        val shared = sharedSecret(priv, serverPublic) ?: return null
        val ikm = hkdf(auth, shared, label("Content-Encoding: auth"), 32)

        // context = "P-256" 0x00 || len16(ua) || ua || len16(server) || server
        val context = label("P-256") +
            byteArrayOf((uaPublic.size shr 8).toByte(), uaPublic.size.toByte()) + uaPublic +
            byteArrayOf((serverPublic.size shr 8).toByte(), serverPublic.size.toByte()) +
            serverPublic

        val cek = hkdf(salt, ikm, label("Content-Encoding: aesgcm") + context, 16)
        val nonce = hkdf(salt, ikm, label("Content-Encoding: nonce") + context, 12)
        val plain = aesGcmOpen(cek, nonce, body) ?: return null

        // aesgcm padding: a 2-octet big-endian pad length, that many zero
        // octets, then the content.
        if (plain.size < 2) return null
        val padLen = ((plain[0].toInt() and 0xff) shl 8) or (plain[1].toInt() and 0xff)
        val start = 2 + padLen
        if (plain.size < start) return null
        return plain.copyOfRange(start, plain.size)
    }

    /**
     * RFC 8291 section 3.3 - the input keying material both codings share:
     * HKDF(salt = auth secret, ikm = ECDH output,
     *      info = "WebPush: info" 0x00 || ua_public || as_public).
     */
    private fun webPushIkm(
        priv: PrivateKey,
        serverPublic: ByteArray,
        uaPublic: ByteArray,
        auth: ByteArray,
    ): ByteArray? {
        val shared = sharedSecret(priv, serverPublic) ?: return null
        return hkdf(auth, shared, label("WebPush: info") + uaPublic + serverPublic, 32)
    }

    private fun sharedSecret(priv: PrivateKey, serverPublic: ByteArray): ByteArray? {
        val pub = WebPushKeys.publicKeyFromPoint(serverPublic) ?: return null
        return runCatching {
            KeyAgreement.getInstance("ECDH").run {
                init(priv)
                doPhase(pub, true)
                generateSecret()
            }
        }.getOrNull()
    }

    /**
     * HKDF-SHA256. Every output here is 32 bytes or fewer, so a single expand
     * block is always enough.
     */
    private fun hkdf(salt: ByteArray, ikm: ByteArray, info: ByteArray, length: Int): ByteArray {
        val mac = Mac.getInstance("HmacSHA256")
        mac.init(SecretKeySpec(if (salt.isEmpty()) ByteArray(32) else salt, "HmacSHA256"))
        val prk = mac.doFinal(ikm)
        mac.init(SecretKeySpec(prk, "HmacSHA256"))
        mac.update(info)
        mac.update(1.toByte())
        return mac.doFinal().copyOf(length)
    }

    private fun aesGcmOpen(key: ByteArray, nonce: ByteArray, sealed: ByteArray): ByteArray? =
        runCatching {
            // The JDK expects ciphertext and tag concatenated, which is the
            // layout on the wire, so the sealed block goes in as one piece.
            Cipher.getInstance("AES/GCM/NoPadding").run {
                init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(128, nonce))
                doFinal(sealed)
            }
        }.getOrNull()

    /**
     * RFC 8188 padding: a record's plaintext is the data, then 0x02 (0x01 for a
     * non-final record), then zero padding. Trim the padding and the delimiter.
     */
    private fun stripPadding(data: ByteArray): ByteArray {
        var end = data.size
        while (end > 0 && data[end - 1] == 0.toByte()) end--
        if (end > 0 && (data[end - 1] == 2.toByte() || data[end - 1] == 1.toByte())) end--
        return data.copyOf(end)
    }

    /** An info label: the ASCII text followed by the 0x00 separator. */
    private fun label(text: String): ByteArray =
        text.toByteArray(Charsets.US_ASCII) + byteArrayOf(0)

    /**
     * Pull a name=value parameter out of a semicolon-separated header value
     * such as "dh=...;p256ecdsa=..." or "salt=...". Values are base64url.
     */
    private fun header(name: String, value: String?): String? {
        if (value.isNullOrEmpty()) return null
        for (part in value.split(";")) {
            val kv = part.trim()
            if (kv.startsWith(name + "=")) return kv.substring(name.length + 1)
        }
        return null
    }

    private fun b64urlDecode(s: String): ByteArray =
        Base64.decode(s, Base64.URL_SAFE or Base64.NO_PADDING)
}
