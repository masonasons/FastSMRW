package me.masonasons.fastsm.push

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import java.math.BigInteger
import java.security.AlgorithmParameters
import java.security.KeyFactory
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import java.security.spec.ECPrivateKeySpec
import java.util.Base64

/**
 * Checks the Web Push decryption against the worked example in RFC 8291
 * section 5, which fixes every value including the sender's ephemeral key --
 * so a correct implementation must reproduce the plaintext exactly.
 *
 * This runs on the plain JVM (the same ECDH / HMAC / AES-GCM providers the
 * device uses), which is why [WebPushCrypto.decryptAes128Gcm] takes its key
 * material as arguments instead of reading it from storage.
 */
class WebPushCryptoTest {

    /** The receiver's private key, as the RFC gives it: a raw 32-byte scalar. */
    private val uaPrivateScalar = "q1dXpw3UpT5VOmu_cf_v6ih07Aems3njxI-JWgLcM94"

    /** The receiver's public key, uncompressed point. */
    private val uaPublicPoint =
        "BCVxsr7N_eNgVRqvHtD0zTZsEc6-VV-JvLexhqUzORcxaOzi6-AYWXvTBHm4bjyPjs7Vd8pZGH6SRpkNtoIAiw4"

    private val authSecret = "BTBZMqHH6r4Tts7J_aSIgg"

    /** The complete aes128gcm body, exactly as it arrives from the server. */
    private val body =
        "DGv6ra1nlYgDCS1FRnbzlwAAEABBBP4z9KsN6nGRTbVYI_c7VJSPQTBtkgcy27mlmlMoZIIgDll6e3vC" +
            "YLocInmYWAmS6TlzAC8wEqKK6PBru3jl7A_yl95bQpu6cVPTpK4Mqgkf1CXztLVBSt2Ks3oZwbuwXP" +
            "XLWyouBWLVWGNWQexSgSxsj_Qulcy4a-fN"

    private val expected = "When I grow up, I want to be a watermelon"

    @Test
    fun decryptsTheRfc8291Example() {
        val plain = WebPushCrypto.decryptAes128Gcm(
            body = b64(body),
            priv = privateKey(b64(uaPrivateScalar)),
            uaPublic = b64(uaPublicPoint),
            auth = b64(authSecret),
        )
        assertNotNull("decryption returned null", plain)
        assertEquals(expected, String(plain!!, Charsets.UTF_8))
    }

    /** A flipped byte in the ciphertext must fail authentication, not decode. */
    @Test
    fun rejectsATamperedBody() {
        val tampered = b64(body).also { it[it.size - 1] = (it[it.size - 1] + 1).toByte() }
        assertNull(
            WebPushCrypto.decryptAes128Gcm(
                body = tampered,
                priv = privateKey(b64(uaPrivateScalar)),
                uaPublic = b64(uaPublicPoint),
                auth = b64(authSecret),
            )
        )
    }

    /** A body too short to hold the RFC 8188 header is rejected, not indexed. */
    @Test
    fun rejectsATruncatedBody() {
        assertNull(
            WebPushCrypto.decryptAes128Gcm(
                body = b64(body).copyOf(20),
                priv = privateKey(b64(uaPrivateScalar)),
                uaPublic = b64(uaPublicPoint),
                auth = b64(authSecret),
            )
        )
    }

    // ---- helpers ----

    private fun b64(s: String): ByteArray = Base64.getUrlDecoder().decode(s)

    private fun privateKey(scalar: ByteArray) =
        KeyFactory.getInstance("EC")
            .generatePrivate(ECPrivateKeySpec(BigInteger(1, scalar), p256Params()))

    private fun p256Params(): ECParameterSpec {
        val params = AlgorithmParameters.getInstance("EC")
        params.init(ECGenParameterSpec("secp256r1"))
        return params.getParameterSpec(ECParameterSpec::class.java)
    }
}
