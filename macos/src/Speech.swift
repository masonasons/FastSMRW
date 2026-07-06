import AppKit

/// Routes the core's `announce` events to VoiceOver. All spoken *text* is
/// composed in the core (StatusPresenter + SpeechSettings); the app only speaks
/// what it's handed. Earcons are played inside the core (miniaudio), so there's
/// nothing to do for sound here.
enum Speech {
    /// Speak a string through VoiceOver, interrupting current speech.
    static func announce(_ message: String) {
        guard !message.isEmpty else { return }
        NSAccessibility.post(
            element: NSApp.mainWindow ?? NSApp as Any,
            notification: .announcementRequested,
            userInfo: [
                .announcement: message,
                .priority: NSAccessibilityPriorityLevel.high.rawValue,
            ]
        )
    }
}
