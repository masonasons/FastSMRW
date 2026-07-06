//
//  ErrorAlert.swift
//
//  One place to present an error: a summary heading plus the full detail in a
//  selectable / scrollable text view (readable by VoiceOver and copyable), and a
//  "Copy Details" button — never a silent beep. Ported from FastSMApple; the
//  core already composes error text, so this takes plain strings.
//

import AppKit

enum ErrorAlert {
    static func present(_ summary: String, detail: String? = nil, in window: NSWindow?) {
        NSSound.beep()
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = summary
        alert.addButton(withTitle: "OK")
        if let detail, !detail.isEmpty {
            alert.addButton(withTitle: "Copy Details")
            alert.accessoryView = detailView(detail)
        }
        let handle: (NSApplication.ModalResponse) -> Void = { response in
            if response == .alertSecondButtonReturn, let detail {
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(detail, forType: .string)
            }
        }
        if let window {
            alert.beginSheetModal(for: window, completionHandler: handle)
        } else {
            handle(alert.runModal())
        }
    }

    private static func detailView(_ text: String) -> NSView {
        let scroll = NSScrollView(frame: NSRect(x: 0, y: 0, width: 440, height: 120))
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.autohidesScrollers = true
        let textView = NSTextView(frame: scroll.bounds)
        textView.isEditable = false
        textView.isSelectable = true
        textView.drawsBackground = false
        textView.textContainerInset = NSSize(width: 4, height: 4)
        textView.font = .monospacedSystemFont(ofSize: 11, weight: .regular)
        textView.string = text
        textView.setAccessibilityLabel("Error details")
        scroll.documentView = textView
        return scroll
    }
}
