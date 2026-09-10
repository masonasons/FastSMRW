//
//  PushCategory.swift
//
//  Shared by the app and the Notification Service Extension: the app registers
//  a category per Mastodon notification type, and the extension stamps each
//  decrypted push with the matching identifier. Both have to agree on the
//  string, and the extension can't see PushManager, so it lives here.
//

import Foundation

enum PushCategory {
    /// The category (and thread) identifier for a Mastodon notification type,
    /// e.g. "mention" -> "fastsm.mention".
    static func id(_ type: String) -> String {
        type.isEmpty ? "fastsm.other" : "fastsm.\(type)"
    }
}
