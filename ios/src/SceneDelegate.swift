//
//  SceneDelegate.swift
//
//  Owns the shared AppState (the command/event bridge to the core), creates the
//  window, completes the fastsm:// OAuth redirect, and refreshes on return to
//  the foreground (the core's streams were suspended while backgrounded).
//

import UIKit

final class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?
    private var state: AppState?
    private var wasBackgrounded = false

    func scene(_ scene: UIScene, willConnectTo session: UISceneSession,
               options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else { return }
        let window = MagicTapWindow(windowScene: windowScene)

        guard let state = AppState() else {
            // The core failed to start — nothing works without it; say so.
            let vc = UIViewController()
            vc.view.backgroundColor = .systemBackground
            let label = UILabel()
            label.text = "FastSM could not start its core engine. Try reinstalling the app."
            label.numberOfLines = 0
            label.textAlignment = .center
            label.translatesAutoresizingMaskIntoConstraints = false
            vc.view.addSubview(label)
            NSLayoutConstraint.activate([
                label.centerYAnchor.constraint(equalTo: vc.view.centerYAnchor),
                label.leadingAnchor.constraint(equalTo: vc.view.layoutMarginsGuide.leadingAnchor),
                label.trailingAnchor.constraint(equalTo: vc.view.layoutMarginsGuide.trailingAnchor),
            ])
            window.rootViewController = vc
            window.makeKeyAndVisible()
            self.window = window
            return
        }
        self.state = state
        Speech.attach(to: state)

        window.rootViewController = RootViewController(state: state)
        window.makeKeyAndVisible()
        self.window = window

        state.start()
        for context in connectionOptions.urlContexts { handle(context.url) }
    }

    func scene(_ scene: UIScene, openURLContexts URLContexts: Set<UIOpenURLContext>) {
        for context in URLContexts { handle(context.url) }
    }

    /// Custom URL scheme: fastsm://oauth?code=… completes a Mastodon login
    /// (the plain-browser fallback path; ASWebAuthenticationSession normally
    /// delivers the callback directly to the add-account screen).
    private func handle(_ url: URL) {
        guard url.scheme == "fastsm" else { return }
        let components = URLComponents(url: url, resolvingAgainstBaseURL: false)
        if let code = components?.queryItems?.first(where: { $0.name == "code" })?.value {
            state?.finishMastodonLogin(code: code)
        }
    }

    func sceneDidBecomeActive(_ scene: UIScene) {
        if wasBackgrounded {
            wasBackgrounded = false
            state?.refresh()
        }
    }

    func sceneDidEnterBackground(_ scene: UIScene) {
        wasBackgrounded = true
    }
}

/// Something that handles the VoiceOver magic tap (usually the root view
/// controller, which knows whether a post is focused).
@MainActor
protocol MagicTapResponder: AnyObject {
    func performMagicTap()
}

/// A window that handles the VoiceOver magic tap globally. Overriding it here —
/// at the top of the responder chain, below UIApplication — means the gesture
/// fires no matter which element has VoiceOver focus, rather than depending on
/// a particular view controller being reached up the chain.
final class MagicTapWindow: UIWindow {
    override func accessibilityPerformMagicTap() -> Bool {
        guard let responder = rootViewController as? MagicTapResponder else { return false }
        responder.performMagicTap()
        return true
    }
}
