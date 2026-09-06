package me.masonasons.fastsm.push

import android.content.Context
import android.util.Base64
import java.math.BigInteger
import java.security.AlgorithmParameters
import java.security.KeyFactory
import java.security.KeyPairGenerator
import java.security.PrivateKey
import java.security.PublicKey
import java.security.SecureRandom
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.ECPoint
import java.security.spec.ECPublicKeySpec
import java.security.spec.PKCS8EncodedKeySpec

/**
 * The device's Web Push keypair (RFC 8291): a stable P-256 key and a 16-byte
 * auth secret, kept in app-private storage so Mastodon keeps encrypting to the
 * same keys across launches.
 *
 * Crypto lives app-side because the C++ core links no crypto library on any
 * platform — the same arrangement as iOS (CryptoKit there, the JDK providers
 * here), and the same reason HTTP transport is a per-platform shim. The core
 * still owns the Mastodon subscription itself.
 *
 * The public halves (p256dh, auth) go to Mastodon through the core's
 * push_subscribe command; the private key and auth secret never leave the
 * device, and [FastSmMessagingService] uses them to decrypt incoming pushes.
 */
object WebPushKeys {

    private const val PREFS = "webpush"
    private const val PRIVATE = "p256_private" // PKCS#8, base64
    private const val PUBLIC = "p256_public"   // uncompressed point, base64
    private const val AUTH = "auth_secret"     // 16 random bytes, base64

    /** The subscription's public values, base64url (unpadded), for Mastodon. */
    data class PublicKeys(val p256dh: String, val auth: String)

    /** Load or create the keypair and auth secret; returns the public parts. */
    fun ensure(context: Context): PublicKeys {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (prefs.getString(PRIVATE, null) == null) {
            val gen = KeyPairGenerator.getInstance("EC")
            gen.initialize(ECGenParameterSpec("secp256r1"))
            val pair = gen.generateKeyPair()
            val auth = ByteArray(16).also { SecureRandom().nextBytes(it) }
            prefs.edit()
                .putString(PRIVATE, b64(pair.private.encoded))
                .putString(PUBLIC, b64(uncompressedPoint(pair.public as ECPublicKey)))
                .putString(AUTH, b64(auth))
                .apply()
        }
        return PublicKeys(
            p256dh = b64url(prefs.getString(PUBLIC, "")!!.let { Base64.decode(it, Base64.DEFAULT) }),
            auth = b64url(prefs.getString(AUTH, "")!!.let { Base64.decode(it, Base64.DEFAULT) }),
        )
    }

    /** The private key, for decrypting a push. Null before [ensure] has run. */
    fun privateKey(context: Context): PrivateKey? {
        val raw = read(context, PRIVATE) ?: return null
        return runCatching {
            KeyFactory.getInstance("EC").generatePrivate(PKCS8EncodedKeySpec(raw))
        }.getOrNull()
    }

    /**
     * Our own public key as an uncompressed point (0x04 || X || Y, 65 bytes).
     * RFC 8291 feeds it into the key derivation, so decryption needs it too;
     * it is stored rather than recomputed because deriving a public key from a
     * private one means EC point maths the JDK does not expose.
     */
    fun publicPoint(context: Context): ByteArray? = read(context, PUBLIC)

    fun authSecret(context: Context): ByteArray? = read(context, AUTH)

    /** Forget the keypair, so the next subscription starts a fresh one. */
    fun clear(context: Context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().clear().apply()
    }

    /** Rebuild a P-256 public key from an uncompressed point (the sender's). */
    fun publicKeyFromPoint(point: ByteArray): PublicKey? {
        if (point.size != 65 || point[0] != 0x04.toByte()) return null
        return runCatching {
            val x = BigInteger(1, point.copyOfRange(1, 33))
            val y = BigInteger(1, point.copyOfRange(33, 65))
            KeyFactory.getInstance("EC")
                .generatePublic(ECPublicKeySpec(ECPoint(x, y), p256Params()))
        }.getOrNull()
    }

    // ---- helpers ----

    private fun read(context: Context, key: String): ByteArray? =
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(key, null)
            ?.let { runCatching { Base64.decode(it, Base64.DEFAULT) }.getOrNull() }

    private fun p256Params(): ECParameterSpec {
        val params = AlgorithmParameters.getInstance("EC")
        params.init(ECGenParameterSpec("secp256r1"))
        return params.getParameterSpec(ECParameterSpec::class.java)
    }

    /** 0x04 || X || Y, each coordinate left-padded to 32 bytes. */
    private fun uncompressedPoint(key: ECPublicKey): ByteArray {
        val out = ByteArray(65)
        out[0] = 0x04
        copyFixed(key.w.affineX, out, 1)
        copyFixed(key.w.affineY, out, 33)
        return out
    }

    /** Write a 32-byte big-endian coordinate at [offset], zero-padded. */
    private fun copyFixed(value: BigInteger, out: ByteArray, offset: Int) {
        // toByteArray() may carry a leading sign byte, or be short for small values.
        val bytes = value.toByteArray()
        val src = if (bytes.size > 32) bytes.size - 32 else 0
        val len = minOf(bytes.size, 32)
        System.arraycopy(bytes, src, out, offset + (32 - len), len)
    }

    private fun b64(b: ByteArray): String = Base64.encodeToString(b, Base64.NO_WRAP)

    private fun b64url(b: ByteArray): String =
        Base64.encodeToString(b, Base64.URL_SAFE or Base64.NO_PADDING or Base64.NO_WRAP)
}
