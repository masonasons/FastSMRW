//
//  SelfUpdater.swift
//
//  Updating in place. The release carries the app itself, zipped
//  (FastSMRW-macOS.zip). It is downloaded and unpacked here, by this app, so it
//  never picks up the quarantine that made a downloaded disk image's copy of
//  the app "damaged" in Gatekeeper's eyes; then the downloaded copy is started
//  with --apply-update. A running app cannot replace its own bundle, so that
//  copy waits for this one to quit, puts itself where this one was, starts it,
//  and goes. It runs before any window (main.swift), and what goes wrong is
//  written down for the restarted app to say, as nobody is left to hear it.
//

import AppKit

enum SelfUpdater {
    static let applyFlag = "--apply-update"

    // MARK: Downloading and handing over (the running app)

    /// Downloads the zipped app at `url`, checks it, and hands over to it; this
    /// app then quits. Says what fails.
    static func install(from url: URL) {
        let fail: (String) -> Void = { why in
            DispatchQueue.main.async { Speech.announce("The update could not be installed: \(why)") }
        }
        URLSession.shared.downloadTask(with: url) { tmp, response, error in
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            guard error == nil, let tmp, (200..<300).contains(status) else {
                fail(error?.localizedDescription ?? "the download failed (\(status))")
                return
            }
            let fm = FileManager.default
            let staging = fm.temporaryDirectory.appendingPathComponent("FastSMRW-update", isDirectory: true)
            try? fm.removeItem(at: staging)
            let files = staging.appendingPathComponent("files", isDirectory: true)
            let zip = staging.appendingPathComponent("FastSMRW-macOS.zip")
            do {
                try fm.createDirectory(at: files, withIntermediateDirectories: true)
                try fm.moveItem(at: tmp, to: zip)
            } catch {
                fail("it could not be saved")
                return
            }
            guard run("/usr/bin/ditto", ["-x", "-k", zip.path, files.path]) == 0,
                  let app = (try? fm.contentsOfDirectory(at: files, includingPropertiesForKeys: nil))?
                      .first(where: { $0.pathExtension == "app" })
            else {
                fail("it could not be unpacked")
                return
            }
            // Never quarantined (this app downloaded it), but nothing is lost by making sure
            _ = run("/usr/bin/xattr", ["-dr", "com.apple.quarantine", app.path])
            // A damaged or incomplete download is not copied over a working app
            guard run("/usr/bin/codesign", ["--verify", "--deep", "--strict", app.path]) == 0 else {
                fail("the downloaded app did not check out")
                return
            }
            let name = Bundle.main.executableURL?.lastPathComponent ?? "FastSMRW"
            let program = app.appendingPathComponent("Contents/MacOS/\(name)")
            let handover = Process()
            handover.executableURL = program
            handover.arguments = [applyFlag, Bundle.main.bundlePath, String(getpid())]
            do {
                try handover.run()
            } catch {
                fail("the new version would not start")
                return
            }
            DispatchQueue.main.async { NSApp.terminate(nil) }
        }.resume()
    }

    // MARK: Putting the new copy in place (the downloaded app)

    /// With --apply-update <installed bundle> <pid>: waits for that process to
    /// quit, replaces the bundle with this one, starts it. True if this was
    /// that (the caller exits); false for an ordinary start.
    static func runApplyModeIfAsked() -> Bool {
        let args = CommandLine.arguments
        guard let flag = args.firstIndex(of: applyFlag), flag + 2 < args.count else { return false }
        let target = args[flag + 1]
        let pid = pid_t(args[flag + 2]) ?? 0

        // A minute for the old copy to finish quitting (usually well under a second)
        for _ in 0..<300 where pid > 0 && kill(pid, 0) == 0 {
            usleep(200_000)
        }

        let fm = FileManager.default
        let staged = Bundle.main.bundlePath
        let previous = target + ".previous"
        try? fm.removeItem(atPath: previous)
        do {
            try fm.moveItem(atPath: target, toPath: previous)
        } catch {
            recordFailure("FastSMRW could not be replaced in \((target as NSString).deletingLastPathComponent). "
                + "Download it from the releases page instead.")
            open(target)
            return true
        }
        if run("/usr/bin/ditto", [staged, target]) != 0 {
            try? fm.removeItem(atPath: target)
            try? fm.moveItem(atPath: previous, toPath: target)
            recordFailure("the new version could not be copied into place.")
            open(target)
            return true
        }
        try? fm.removeItem(atPath: previous)
        _ = run("/usr/bin/xattr", ["-dr", "com.apple.quarantine", target])
        open(target)
        return true
    }

    // MARK: What went wrong last time

    private static var failureFile: URL? {
        guard let support = try? FileManager.default.url(for: .applicationSupportDirectory, in: .userDomainMask,
                                                         appropriateFor: nil, create: true)
        else { return nil }
        let dir = support.appendingPathComponent("FastSMRW", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("update-failure.txt")
    }

    private static func recordFailure(_ reason: String) {
        if let file = failureFile { try? reason.write(to: file, atomically: true, encoding: .utf8) }
    }

    /// Why the last update did not take, once; nil if it did (or there was none).
    static func takeFailure() -> String? {
        guard let file = failureFile, let reason = try? String(contentsOf: file, encoding: .utf8) else {
            return nil
        }
        try? FileManager.default.removeItem(at: file)
        let trimmed = reason.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? nil : trimmed
    }

    // MARK: Helpers

    @discardableResult
    private static func run(_ tool: String, _ arguments: [String]) -> Int32 {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: tool)
        process.arguments = arguments
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
        } catch {
            return -1
        }
        process.waitUntilExit()
        return process.terminationStatus
    }

    private static func open(_ bundle: String) {
        run("/usr/bin/open", [bundle])
    }
}
