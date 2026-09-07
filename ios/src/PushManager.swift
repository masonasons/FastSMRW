//
//  PushManager.swift
//
//  The iOS side of push notifications: asking the user's permission,
//  registering with APNs, and holding the resulting device token. This is the
//  OS-level plumbing a Swift app must do itself. Everything portable — the Web
//  Push keypair, the Mastodon /api/v1/push/subscription call, and the strings a
//  notification shows — lives in the core so every front end shares it.
//
//  P1 (this file) only registers and captures the token; it is deliberately NOT
//  invoked at launch, since prompting for notifications before the relay exists
//  would lead nowhere. A Notifications setting will drive it once the backend is
//  live (P5). `onDeviceToken` is where the core subscription hooks in (P2).
//

import Foundation
import UIKit
import UserNotifications
import os

final class PushManager: NSObject {
    static let shared = PushManager()

    private let log = Logger(subsystem: "me.masonasons.FastSMRW", category: "push")

    /// The most recent APNs device token (lowercase hex), once registration has
    /// succeeded. Handed to the core Web Push subscription at P2.
    private(set) var deviceTokenHex: String?

    /// Fired when a fresh device token arrives, after the relay+Mastodon
    /// subscription is kicked off. Optional hook for UI.
    var onDeviceToken: ((String) -> Void)?

    // The self-hosted relay that bridges Mastodon Web Push to APNs.
    private static let relayBase = "https://push.brynify.me"
    // Shared secret the relay's /register requires (low-secrecy; gates casual
    // abuse). Mirrors RELAY_REGISTER_TOKEN in the relay's config.
    private static let relayRegisterToken =
        "be354d899a9bf6af74578ed7a631f2a916e25348d1e07f5fb4153121a13d90be"

    private static let enabledKey = "push_enabled"

    /// Whether the user has turned push on (persisted).
    var isEnabled: Bool { UserDefaults.standard.bool(forKey: Self.enabledKey) }

    private weak var state: AppState?

    /// Set while an explicit enable is in flight, so the core speaks the result
    /// only for a change the user asked for -- not for the renewal at launch.
    private var announceNextResult = false
	private var accountKeys: Set<String> = []

	/// Renew after accounts have loaded, and whenever another account is added.
	@MainActor
	func accountsChanged(state: AppState) {
		let keys = Set(state.accounts.filter { $0.platform == "mastodon" }.map { $0.key })
		let added = !keys.subtracting(accountKeys).isEmpty
		accountKeys = keys
		if added { refreshIfEnabled(state: state) }
	}

    /// Turn push on: remember the choice, ask permission, register with APNs,
    /// and (once the token arrives) subscribe with the relay + Mastodon.
    @MainActor
    func enable(state: AppState) {
        self.state = state
        UserDefaults.standard.set(true, forKey: Self.enabledKey)
        announceNextResult = true
        requestAuthorizationAndRegister()
    }

    /// Turn push off: drop the Mastodon subscription and stop APNs.
    @MainActor
    func disable(state: AppState) {
        self.state = state
        UserDefaults.standard.set(false, forKey: Self.enabledKey)
		announceNextResult = false
        state.pushUnsubscribe(announce: true)
        UIApplication.shared.unregisterForRemoteNotifications()
    }

    /// Called at launch: if push was left on, re-register and re-subscribe
    /// (Mastodon subscriptions can lapse; the POST simply replaces).
    @MainActor
    func refreshIfEnabled(state: AppState) {
        guard isEnabled else { return }
        self.state = state
        requestAuthorizationAndRegister()
    }

    /// Ask permission and, if granted, register with APNs. Safe to call more
    /// than once — iOS shows the system prompt only the first time.
    func requestAuthorizationAndRegister() {
        UNUserNotificationCenter.current()
            .requestAuthorization(options: [.alert, .sound, .badge]) { [weak self] granted, error in
                if let error {
                    self?.log.error("push auth error: \(error.localizedDescription, privacy: .public)")
                }
                guard granted else {
                    self?.log.info("push authorization denied")
                    return
                }
                Task { @MainActor in UIApplication.shared.registerForRemoteNotifications() }
            }
    }

    // MARK: Called from AppDelegate's OS callbacks (on the main thread).

    func didRegister(deviceToken: Data) {
        let hex = deviceToken.map { String(format: "%02x", $0) }.joined()
        deviceTokenHex = hex
        log.info("APNs device token received")
        if isEnabled { subscribe(deviceTokenHex: hex) }
        onDeviceToken?(hex)
    }

    func didFailToRegister(error: Error) {
        log.error("APNs registration failed: \(error.localizedDescription, privacy: .public)")
    }

    // MARK: - relay registration + Mastodon subscription

    /// Register the device token with the relay to get a per-device endpoint,
    /// then hand that endpoint + our Web Push public keys to the core, which
    /// subscribes the selected Mastodon account.
    private func subscribe(deviceTokenHex: String) {
        let keys = WebPushKeys.ensure()
        var req = URLRequest(url: URL(string: Self.relayBase + "/register")!)
        req.httpMethod = "POST"
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.setValue("Bearer " + Self.relayRegisterToken, forHTTPHeaderField: "Authorization")
        let body: [String: String] = ["device_token": deviceTokenHex,
                                       "environment": Self.apnsEnvironment()]
        req.httpBody = try? JSONSerialization.data(withJSONObject: body)
        URLSession.shared.dataTask(with: req) { [weak self] data, resp, err in
            guard let self else { return }
            if let err {
                self.log.error("relay register failed: \(err.localizedDescription, privacy: .public)")
                return
            }
            guard let data,
                  let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let endpoint = obj["endpoint"] as? String else {
                let code = (resp as? HTTPURLResponse)?.statusCode ?? -1
                self.log.error("relay register: bad response (\(code, privacy: .public))")
                return
            }
            Task { @MainActor in
				guard self.isEnabled else { return }
				let announce = self.announceNextResult
				self.announceNextResult = false
                self.state?.pushSubscribe(endpoint: endpoint, p256dh: keys.p256dh,
                                          auth: keys.auth, announce: announce)
            }
        }.resume()
    }

    /// Which APNs environment this build's device token belongs to, read from the
    /// embedded provisioning profile. App Store builds carry no profile ->
    /// production.
    static func apnsEnvironment() -> String {
        guard let url = Bundle.main.url(forResource: "embedded", withExtension: "mobileprovision"),
              let data = try? Data(contentsOf: url),
              let text = String(data: data, encoding: .isoLatin1),
              let k = text.range(of: "aps-environment"),
              let s = text.range(of: "<string>", range: k.upperBound..<text.endIndex),
              let e = text.range(of: "</string>", range: s.upperBound..<text.endIndex)
        else { return "production" }
        return text[s.upperBound..<e.lowerBound] == "development" ? "sandbox" : "production"
    }
}

extension PushManager: UNUserNotificationCenterDelegate {
    /// Show pushes even while the app is in the foreground.
    func userNotificationCenter(_ center: UNUserNotificationCenter,
                                willPresent notification: UNNotification,
                                withCompletionHandler completionHandler:
                                    @escaping (UNNotificationPresentationOptions) -> Void) {
        completionHandler([.banner, .sound, .list])
    }
}
