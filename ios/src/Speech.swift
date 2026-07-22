//
//  Speech.swift
//
//  Routes the core's `announce` events to VoiceOver. All spoken strings are
//  composed in the core; the app only posts them.
//

import UIKit

enum Speech {
    @MainActor
    static func attach(to state: AppState) {
        state.onAnnounce = { message in
            UIAccessibility.post(notification: .announcement, argument: message)
        }
    }
}
