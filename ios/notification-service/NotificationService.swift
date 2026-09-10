//
//  NotificationService.swift
//
//  The Notification Service Extension: when a push arrives with
//  mutable-content, iOS runs this in the background before showing it. We
//  decrypt the Web Push body the relay forwarded (base64url in "m") using the
//  on-device keypair (WebPushKeys, shared via a keychain access group) and
//  replace the generic fallback with the real notification text.
//
//  Mastodon's Web Push payload already carries a composed title + body, which
//  we display here, plus the notification type, which picks the category.
//  (Routing this through the core's own string composition — for
//  FastSM-consistent phrasing — is a deliberate later refinement.)
//

import CryptoKit
import Foundation
import UserNotifications

final class NotificationService: UNNotificationServiceExtension {
    private var contentHandler: ((UNNotificationContent) -> Void)?
    private var bestAttempt: UNMutableNotificationContent?

    override func didReceive(_ request: UNNotificationRequest,
                             withContentHandler contentHandler:
                                 @escaping (UNNotificationContent) -> Void) {
        self.contentHandler = contentHandler
        let content = request.content.mutableCopy() as? UNMutableNotificationContent
        bestAttempt = content
        guard let content else { contentHandler(request.content); return }

        // On any problem, keep the generic fallback the relay set.
        let info = request.content.userInfo
        guard let m = info["m"] as? String,
              let body = Data(base64urlEncoded: m),
              let priv = WebPushKeys.privateKey(),
              let auth = WebPushKeys.authSecret()
        else { contentHandler(content); return }

        let decrypted: Data?
        if (info["ce"] as? String) == "aesgcm" {
            // Older scheme: salt in the Encryption header (salt=…), server key in
            // Crypto-Key (dh=…), both forwarded by the relay.
            if let saltB64 = param("salt", from: info["enc"] as? String),
               let salt = Data(base64urlEncoded: saltB64),
               let dhB64 = param("dh", from: info["ck"] as? String),
               let serverPub = Data(base64urlEncoded: dhB64) {
                decrypted = WebPush.decryptAESGCM(ciphertext: body, salt: salt, serverPublic: serverPub,
                                                  privateKey: priv, authSecret: auth)
            } else {
                decrypted = nil
            }
        } else {
            decrypted = WebPush.decrypt(body: body, privateKey: priv, authSecret: auth)
        }

        guard let plain = decrypted,
              let obj = try? JSONSerialization.jsonObject(with: plain) as? [String: Any]
        else { contentHandler(content); return }

        if let title = obj["title"] as? String, !title.isEmpty { content.title = title }
        if let text = obj["body"] as? String { content.body = text }
        // Tag the push with its Mastodon notification type. The category is what
        // the app registered at launch; the thread identifier makes iOS group
        // mentions separately from boosts in Notification Centre.
        let type = obj["notification_type"] as? String ?? ""
        content.categoryIdentifier = PushCategory.id(type)
        content.threadIdentifier = PushCategory.id(type)
        contentHandler(content)
    }

    override func serviceExtensionTimeWillExpire() {
        if let handler = contentHandler, let content = bestAttempt { handler(content) }
    }

    /// Extract a `name=value` parameter from a `;`-separated header value such as
    /// "dh=…;p256ecdsa=…" or "salt=…". The values are base64url (no unquoting).
    private func param(_ name: String, from header: String?) -> String? {
        guard let header else { return nil }
        for part in header.split(separator: ";") {
            let kv = part.trimmingCharacters(in: .whitespaces)
            if kv.hasPrefix(name + "=") { return String(kv.dropFirst(name.count + 1)) }
        }
        return nil
    }
}

private extension Data {
    /// Decode base64url (no padding), as the relay encodes the encrypted body.
    init?(base64urlEncoded s: String) {
        var str = s.replacingOccurrences(of: "-", with: "+")
            .replacingOccurrences(of: "_", with: "/")
        while str.count % 4 != 0 { str.append("=") }
        self.init(base64Encoded: str)
    }
}
