import Foundation

/// Swift wrapper around the FastSMRW core's C ABI. The app submits **commands**
/// as JSON and renders **events** as JSON; this class owns the `fastsm_core*`
/// handle, marshals the event sink (which the core invokes on its own threads)
/// onto the main queue, and hands each event's raw JSON to `onEvent`.
///
/// This is the entire seam between the AppKit UI and the shared core — the same
/// boundary a Kotlin or C# front end would bind. No engine logic lives here.
final class CoreClient {
    /// Called on the **main thread** with each event's raw JSON string.
    var onEvent: ((String) -> Void)?

    private var core: OpaquePointer?

    init?(configDir: URL, soundpacksDir: URL?, userAgent: String) {
        var config: [String: Any] = ["config_dir": configDir.path, "user_agent": userAgent]
        if let soundpacksDir { config["soundpacks_dir"] = soundpacksDir.path }
        guard let data = try? JSONSerialization.data(withJSONObject: config),
              let json = String(data: data, encoding: .utf8) else { return nil }

        core = json.withCString { fastsm_core_create($0) }
        guard core != nil else { return nil }

        // The trampoline is a captureless C function; `self` rides along as the
        // opaque user pointer. We keep the instance alive for the core's lifetime.
        let user = Unmanaged.passUnretained(self).toOpaque()
        fastsm_core_set_event_sink(core, { user, json, len in
            guard let user, let json else { return }
            let client = Unmanaged<CoreClient>.fromOpaque(user).takeUnretainedValue()
            // event_json is valid only during this call — copy before hopping.
            let text = String(decoding: UnsafeRawBufferPointer(start: json, count: len),
                              as: UTF8.self)
            DispatchQueue.main.async { client.onEvent?(text) }
        }, user)
    }

    /// Submit a command, e.g. `send("select_timeline", ["dir": "next"])`.
    func send(_ cmd: String, _ payload: [String: Any] = [:]) {
        var object = payload
        object["cmd"] = cmd
        guard let data = try? JSONSerialization.data(withJSONObject: object) else { return }
        data.withUnsafeBytes { raw in
            guard let base = raw.bindMemory(to: CChar.self).baseAddress else { return }
            fastsm_core_dispatch(core, base, data.count)
        }
    }

    func shutdown() {
        guard let core else { return }
        fastsm_core_destroy(core)
        self.core = nil
    }

    deinit { shutdown() }

    static var version: String { String(cString: fastsm_core_version()) }
}
