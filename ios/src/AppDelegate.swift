//
//  AppDelegate.swift
//
//  iOS entry point. The real setup lives in SceneDelegate; here we only
//  configure the audio session (the core plays earcons via miniaudio) and hand
//  UIKit the scene delegate class.
//

import AVFAudio
import UIKit

@main
final class AppDelegate: UIResponder, UIApplicationDelegate {
    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions launchOptions:
                         [UIApplication.LaunchOptionsKey: Any]? = nil) -> Bool {
        // Earcons are core UX for blind users: play them in the "playback"
        // category (audible regardless of the ring/silent switch) mixed with
        // other audio so they never interrupt music or VoiceOver speech.
        let session = AVAudioSession.sharedInstance()
        try? session.setCategory(.playback, options: [.mixWithOthers])
        try? session.setActive(true)
        return true
    }

    func application(_ application: UIApplication,
                     configurationForConnecting connectingSceneSession: UISceneSession,
                     options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let config = UISceneConfiguration(name: "Default",
                                          sessionRole: connectingSceneSession.role)
        config.delegateClass = SceneDelegate.self
        return config
    }
}
