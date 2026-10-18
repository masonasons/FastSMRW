//
//  SceneDelegate.swift
//
//  Owns the shared AppState (the command/event bridge to the core), creates the
//  window, completes the fastsm:// OAuth redirect, and refreshes on return to
//  the foreground (the core's streams were suspended while backgrounded).
//

import AVFAudio
import UIKit

final class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?
    private var state: AppState?
    private var wasBackgrounded = false
    private var lastAudioRevive = Date.distantPast

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
        observeAudioSession()
		// RootViewController renews push once accounts have finished loading.
        for context in connectionOptions.urlContexts { handle(context.url) }
    }

    deinit { NotificationCenter.default.removeObserver(self) }

    // MARK: Earcon audio recovery
    //
    // Earcons play through the core's own audio device (miniaudio). An audio-
    // session interruption, a route or category change (including the media
    // player reconfiguring the session), or a media-services reset can leave
    // that device silently dead — and it often still reports "running", so the
    // core's lazy self-heal never fires and earcons stay gone until relaunch.
    // Listen for those events and rebuild the device via resetAudio() (the same
    // path macOS already runs on sleep/wake). Media playback is a separate
    // engine and is unaffected.
    private func observeAudioSession() {
        let nc = NotificationCenter.default
        let session = AVAudioSession.sharedInstance()
        nc.addObserver(self, selector: #selector(audioInterruption(_:)),
                       name: AVAudioSession.interruptionNotification, object: session)
        nc.addObserver(self, selector: #selector(audioRouteChanged(_:)),
                       name: AVAudioSession.routeChangeNotification, object: session)
        nc.addObserver(self, selector: #selector(audioServicesReset(_:)),
                       name: AVAudioSession.mediaServicesWereResetNotification, object: session)
    }

    @objc private func audioInterruption(_ note: Notification) {
        guard let raw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt,
              AVAudioSession.InterruptionType(rawValue: raw) == .ended else { return }
        reviveAudio()
    }

    @objc private func audioRouteChanged(_ note: Notification) {
        reviveAudio()
    }

    @objc private func audioServicesReset(_ note: Notification) {
        // The whole audio stack was torn down — our category/activation went with
        // it, so set the earcon category again before rebuilding.
        try? AVAudioSession.sharedInstance().setCategory(.playback, options: [.mixWithOthers])
        reviveAudio(force: true)
    }

    private func reviveAudio(force: Bool = false) {
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            // Coalesce the bursts of route-change notifications a single change
            // can produce into one rebuild.
            if !force, Date().timeIntervalSince(self.lastAudioRevive) < 0.5 { return }
            self.lastAudioRevive = Date()
            try? AVAudioSession.sharedInstance().setActive(true)
            self.state?.resetAudio()
        }
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
            state?.resume()
        }
    }

    func sceneDidEnterBackground(_ scene: UIScene) {
        wasBackgrounded = true
        // iOS can suspend (or terminate) us at any point from here, so publish the
        // reading position now rather than waiting out the idle timer — otherwise the
        // spot reached just before switching away never reaches the server.
        state?.pause()
    }
}

/// Something that handles the VoiceOver magic tap (usually the root view
/// controller, which knows whether a post is focused).
@MainActor
protocol MagicTapResponder: AnyObject {
	func performMagicTap() -> Bool
}

/// A window that handles the VoiceOver magic tap globally. Overriding it here —
/// at the top of the responder chain, below UIApplication — means the gesture
/// fires no matter which element has VoiceOver focus, rather than depending on
/// a particular view controller being reached up the chain.
final class MagicTapWindow: UIWindow {
    override func accessibilityPerformMagicTap() -> Bool {
        guard let responder = rootViewController as? MagicTapResponder else { return false }
		return responder.performMagicTap()
    }
}
