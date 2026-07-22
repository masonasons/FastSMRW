//
//  Movement.swift
//
//  Movement-unit jumps computed UI-side, for navigators that must answer
//  synchronously (the iOS VoiceOver rotor) — an async round-trip through the
//  core's `move` command can't back a rotor. The algorithm is a faithful port
//  of movement::destination in core/src/timeline/movement.cpp (the desktop
//  path); the per-row data (account_id, time, thread) comes from the core.
//  KEEP THE SEMANTICS IN SYNC with movement.cpp.
//

import Foundation

enum MovementUnit {
    case sameUser
    case thread
    case time(seconds: Int)
    case count(Int)

    /// Parse a stable settings key: "same_user" / "thread" / "time:N" / "count:N".
    static func parse(_ key: String) -> MovementUnit? {
        if key == "same_user" { return .sameUser }
        if key == "thread" { return .thread }
        if key.hasPrefix("time:"), let seconds = Int(key.dropFirst(5)), seconds > 0 {
            return .time(seconds: seconds)
        }
        if key.hasPrefix("count:"), let count = Int(key.dropFirst(6)), count > 0 {
            return .count(count)
        }
        return nil
    }

    /// The row to jump to from `index` by one step (down = toward older/higher
    /// indices), or nil if there's nowhere to go.
    func destination(in rows: [Row], from index: Int, down: Bool) -> Int? {
        guard rows.indices.contains(index) else { return nil }
        let step = down ? 1 : -1

        func scan(_ matches: (Row) -> Bool) -> Int? {
            var i = index + step
            while rows.indices.contains(i) {
                if matches(rows[i]) { return i }
                i += step
            }
            return nil
        }

        switch self {
        case let .time(seconds):
            guard let base = rows[index].time else { return nil }
            return scan { row in
                guard let t = row.time else { return false }
                let diff = down ? base - t : t - base
                return diff >= seconds
            }
        case .sameUser:
            guard let author = rows[index].accountId else { return nil }
            return scan { $0.accountId == author }
        case .thread:
            guard let key = rows[index].thread else { return nil }
            return scan { $0.thread == key }
        case let .count(count):
            var dest = index + step * count
            dest = max(0, min(rows.count - 1, dest))
            return dest == index ? nil : dest // nil = already at that edge
        }
    }
}
