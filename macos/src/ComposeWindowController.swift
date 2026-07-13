//
//  ComposeWindowController.swift
//
//  Compose a new post, reply, quote, or edit. Matches the Windows compose
//  dialog: reply-recipient checklist, content warning, visibility, language,
//  poll, scheduling, and media attachments with alt text — each shown only when
//  the account/mode supports it (from compose_context.features). The core
//  composes all text and options; this gathers a `draft` and sends `post`.
//

import AppKit
import UniformTypeIdentifiers

@MainActor
final class ComposeWindowController: NSWindowController, NSTextViewDelegate, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private let context: ComposeContext

    private let textView = ComposeTextView()
    private let cwField = NSTextField()
    private let counter = NSTextField(labelWithString: "")
    private let sendButton = NSButton()

    // Optional sections (built only when the feature applies).
    private var participantChecks: [(String, NSButton)] = []   // acct, checkbox
    private let visibilityPopup = NSPopUpButton()
    private var showVisibility = false
    private let languagePopup = NSPopUpButton()
    private var languages: [ComposeLanguage] = []

    private let pollCheck = NSButton(checkboxWithTitle: "Add a poll", target: nil, action: nil)
    private var pollOptionFields: [NSTextField] = []
    private let pollMultiple = NSButton(checkboxWithTitle: "Allow multiple choices", target: nil, action: nil)
    private let pollDuration = NSPopUpButton()
    private var pollViews: [NSView] = []
    private let pollDurations: [(String, Int)] = [
        ("5 minutes", 300), ("30 minutes", 1800), ("1 hour", 3600), ("6 hours", 21600),
        ("1 day", 86400), ("3 days", 259200), ("7 days", 604800),
    ]

    private let scheduleCheck = NSButton(checkboxWithTitle: "Schedule for later", target: nil, action: nil)
    private let schedulePicker = NSDatePicker()

    private var mentionPicker: MentionPickerWindowController?

    private struct Attachment { let filename: String; let mime: String; var alt: String; let data: String }
    private var attachments: [Attachment] = []
    private let attachmentTable = NSTableView()
    private let altField = NSTextField()
    private var showMedia = false

    init(state: AppState, context: ComposeContext) {
        self.state = state
        self.context = context
        self.languages = context.languages ?? []
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 620),
                              styleMask: [.titled, .closable, .resizable], backing: .buffered,
                              defer: false)
        super.init(window: window)
        window.title = context.title ?? "New Post"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        state.onPostResult = { [weak self] ok in
            if ok { self?.dismiss() } else { self?.sendButton.isEnabled = true } // allow retry
        }
        parent.beginSheet(window!) { _ in
            self.state.onPostResult = nil
            completion()
        }
        window?.makeFirstResponder(textView)
    }

    func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func feature(_ key: String) -> Bool { context.features?[key] ?? false }
    private var isEditing: Bool { context.mode == "edit" }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        var sections: [NSView] = []

        if let label = context.contextLabel, !label.isEmpty {
            let l = NSTextField(wrappingLabelWithString: label)
            l.textColor = .secondaryLabelColor
            l.setAccessibilityLabel(label)
            sections.append(l)
        }

        // Reply recipients (Mastodon): a checklist; the core mentions the checked.
        if let participants = context.replyParticipants, !participants.isEmpty {
            var views: [NSView] = [NSTextField(labelWithString: "Recipients:")]
            for p in participants {
                let title = p.displayName.isEmpty ? "@\(p.acct)" : "\(p.displayName) (@\(p.acct))"
                let box = NSButton(checkboxWithTitle: title, target: nil, action: nil)
                box.state = p.checked ? .on : .off
                participantChecks.append((p.acct, box))
                views.append(box)
            }
            let stack = NSStackView(views: views)
            stack.orientation = .vertical
            stack.alignment = .leading
            stack.spacing = 2
            sections.append(stack)
        }

        if feature("content_warning") {
            cwField.placeholderString = "Content warning (optional)"
            cwField.setAccessibilityLabel("Content warning")
            if let cw = context.prefillCw { cwField.stringValue = cw }
            sections.append(cwField)
        }

        textView.isRichText = false
        textView.font = .systemFont(ofSize: 13)
        textView.delegate = self
        textView.enterToSend = context.enterToSend ?? false
        textView.onSend = { [weak self] in self?.send() }
        textView.onMention = { [weak self] in self?.mention() }
        textView.setAccessibilityLabel("Post text")
        if let prefill = context.prefillText { textView.string = prefill }
        let textScroll = NSScrollView()
        textScroll.documentView = textView
        textScroll.hasVerticalScroller = true
        textScroll.borderType = .bezelBorder
        textScroll.translatesAutoresizingMaskIntoConstraints = false
        textScroll.heightAnchor.constraint(greaterThanOrEqualToConstant: 120).isActive = true
        textView.autoresizingMask = [.width]
        textView.isVerticallyResizable = true
        textView.textContainer?.widthTracksTextView = true
        sections.append(textScroll)

        counter.textColor = .secondaryLabelColor
        counter.font = .systemFont(ofSize: 11)
        sections.append(counter)
        updateCounter()

        // Visibility (Mastodon, not while editing).
        if feature("visibility"), !isEditing {
            showVisibility = true
            visibilityPopup.addItems(withTitles: ["Public", "Quiet public", "Followers", "Specific people"])
            visibilityPopup.selectItem(at: context.defaultVisibility ?? 0)
            visibilityPopup.setAccessibilityLabel("Visibility")
            sections.append(labeledRow("Visibility:", visibilityPopup))
        }

        // Language — not while editing: the core doesn't send the post's current
        // language, so we can't pre-select it, and sending a default would silently
        // re-tag the post. Editing therefore leaves the language unchanged.
        if !languages.isEmpty, !isEditing {
            languagePopup.addItems(withTitles: languages.map { $0.name })
            languagePopup.setAccessibilityLabel("Language")
            sections.append(labeledRow("Language:", languagePopup))
        }

        // Poll (Mastodon, not while editing).
        if feature("polls"), !isEditing {
            pollCheck.target = self
            pollCheck.action = #selector(togglePoll)
            sections.append(pollCheck)
            for i in 0..<4 {
                let f = NSTextField()
                f.placeholderString = "Choice \(i + 1)"
                f.setAccessibilityLabel("Poll choice \(i + 1)")
                pollOptionFields.append(f)
                pollViews.append(f)
            }
            pollMultiple.setAccessibilityLabel("Allow multiple choices")
            pollViews.append(pollMultiple)
            pollDuration.addItems(withTitles: pollDurations.map { $0.0 })
            pollDuration.selectItem(at: 4) // 1 day
            pollDuration.setAccessibilityLabel("Poll duration")
            pollViews.append(labeledRow("Duration:", pollDuration))
            let stack = NSStackView(views: pollViews)
            stack.orientation = .vertical
            stack.alignment = .leading
            stack.spacing = 4
            sections.append(stack)
            setPollVisible(false)
        }

        // Schedule (Mastodon, not while editing).
        if feature("scheduling"), !isEditing {
            scheduleCheck.target = self
            scheduleCheck.action = #selector(toggleSchedule)
            sections.append(scheduleCheck)
            schedulePicker.datePickerElements = [.yearMonthDay, .hourMinute]
            schedulePicker.dateValue = Date(timeIntervalSinceNow: 3600)
            schedulePicker.minDate = Date()
            schedulePicker.setAccessibilityLabel("Scheduled time")
            schedulePicker.isHidden = true
            sections.append(schedulePicker)
        }

        // Media attachments (not while editing).
        if feature("media"), !isEditing {
            showMedia = true
            sections.append(NSTextField(labelWithString: "Attachments:"))
            attachmentTable.addTableColumn(NSTableColumn(identifier: NSUserInterfaceItemIdentifier("att")))
            attachmentTable.headerView = nil
            attachmentTable.dataSource = self
            attachmentTable.delegate = self
            attachmentTable.setAccessibilityLabel("Attachments")
            let attScroll = NSScrollView()
            attScroll.documentView = attachmentTable
            attScroll.hasVerticalScroller = true
            attScroll.borderType = .bezelBorder
            attScroll.translatesAutoresizingMaskIntoConstraints = false
            attScroll.heightAnchor.constraint(equalToConstant: 70).isActive = true
            sections.append(attScroll)
            altField.placeholderString = "Alt text for the selected attachment"
            altField.setAccessibilityLabel("Alt text")
            altField.target = self
            altField.action = #selector(commitAlt)
            let addBtn = NSButton(title: "Add…", target: self, action: #selector(addAttachment))
            addBtn.bezelStyle = .rounded
            let removeBtn = NSButton(title: "Remove", target: self, action: #selector(removeAttachment))
            removeBtn.bezelStyle = .rounded
            let mediaControls = NSStackView(views: [addBtn, removeBtn, altField])
            mediaControls.spacing = 8
            altField.widthAnchor.constraint(greaterThanOrEqualToConstant: 200).isActive = true
            sections.append(mediaControls)
        }

        // Buttons.
        sendButton.title = isEditing ? "Save" : "Post"
        sendButton.bezelStyle = .rounded
        sendButton.keyEquivalent = "\r"
        sendButton.keyEquivalentModifierMask = [.command]
        sendButton.target = self
        sendButton.action = #selector(sendAction)
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"
        let mentionButton = NSButton(title: "Mention… (⌥A)", target: self, action: #selector(mentionAction))
        mentionButton.bezelStyle = .rounded
        let buttons = NSStackView(views: [mentionButton, NSView(), cancel, sendButton])
        buttons.spacing = 8
        sections.append(buttons)

        let form = NSStackView(views: sections)
        form.orientation = .vertical
        form.alignment = .leading
        form.spacing = 10
        form.translatesAutoresizingMaskIntoConstraints = false

        // Full-width controls stretch to the form width.
        for v in [cwField, textScroll, counter] as [NSView] {
            v.leadingAnchor.constraint(equalTo: form.leadingAnchor).isActive = true
            v.trailingAnchor.constraint(equalTo: form.trailingAnchor).isActive = true
        }

        // Pin the form directly to the window (no wrapping scroll view — that
        // breaks VoiceOver navigation of the form). The window is resizable and
        // tall; optional sections collapse when hidden.
        content.addSubview(form)
        NSLayoutConstraint.activate([
            form.topAnchor.constraint(equalTo: content.topAnchor, constant: 16),
            form.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 16),
            form.trailingAnchor.constraint(equalTo: content.trailingAnchor, constant: -16),
            buttons.leadingAnchor.constraint(equalTo: form.leadingAnchor),
            buttons.trailingAnchor.constraint(equalTo: form.trailingAnchor),
        ])
    }

    private func labeledRow(_ title: String, _ control: NSView) -> NSView {
        let row = NSStackView(views: [NSTextField(labelWithString: title), control])
        row.spacing = 8
        return row
    }

    // MARK: Poll / schedule toggles

    private func setPollVisible(_ on: Bool) { pollViews.forEach { $0.isHidden = !on } }
    @objc private func togglePoll() { setPollVisible(pollCheck.state == .on); updateCounter() }
    @objc private func toggleSchedule() {
        schedulePicker.isHidden = scheduleCheck.state != .on
        sendButton.title = scheduleCheck.state == .on ? "Schedule" : (isEditing ? "Save" : "Post")
    }

    // MARK: Media

    @objc private func addAttachment() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.allowedContentTypes = [.image, .movie, .audio]
        panel.beginSheetModal(for: window!) { [weak self] resp in
            guard resp == .OK, let self else { return }
            for url in panel.urls {
                guard let data = try? Data(contentsOf: url) else { continue }
                let mime = UTType(filenameExtension: url.pathExtension)?.preferredMIMEType
                    ?? "application/octet-stream"
                self.attachments.append(Attachment(filename: url.lastPathComponent, mime: mime,
                                                    alt: "", data: data.base64EncodedString()))
            }
            self.attachmentTable.reloadData()
            self.updateCounter()
        }
    }
    @objc private func removeAttachment() {
        let row = attachmentTable.selectedRow
        guard attachments.indices.contains(row) else { return }
        attachments.remove(at: row)
        attachmentTable.reloadData()
        altField.stringValue = ""
        updateCounter()
    }
    @objc private func commitAlt() {
        let row = attachmentTable.selectedRow
        guard attachments.indices.contains(row) else { return }
        attachments[row].alt = altField.stringValue
        attachmentTable.reloadData()
    }

    func numberOfRows(in tableView: NSTableView) -> Int { attachments.count }
    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell = reuseCell(tableView, NSUserInterfaceItemIdentifier("AttCell"))
        if attachments.indices.contains(row) {
            let a = attachments[row]
            let label = a.filename + (a.alt.isEmpty ? "  (no alt text)" : "  [alt set]")
            cell.textField?.stringValue = label
            cell.setAccessibilityLabel(label)
        }
        return cell
    }
    func tableViewSelectionDidChange(_ notification: Notification) {
        let row = attachmentTable.selectedRow
        altField.stringValue = attachments.indices.contains(row) ? attachments[row].alt : ""
    }

    // MARK: Counter

    func textDidChange(_ notification: Notification) { updateCounter() }
    private func updateCounter() {
        let count = textView.string.count
        if let max = context.maxChars {
            counter.stringValue = "\(count)/\(max)"
            counter.textColor = count > max ? .systemRed : .secondaryLabelColor
        } else {
            counter.stringValue = "\(count)"
        }
    }

    // MARK: Send

    @objc private func sendAction() { send() }
    @objc private func cancel() { dismiss() }

    // MARK: @-mention autocomplete

    @objc private func mentionAction() { mention() }
    private func mention() {
        guard let window else { return }
        let (range, word) = mentionWordUnderCaret()
        let picker = MentionPickerWindowController(state: state, initialQuery: word) { [weak self] acct in
            self?.insertMention(acct, replacing: range)
        }
        mentionPicker = picker
        picker.beginSheet(for: window) { [weak self] in self?.mentionPicker = nil }
    }

    /// The handle-like word ending at the caret, so completing replaces it.
    private func mentionWordUnderCaret() -> (NSRange, String) {
        let ns = textView.string as NSString
        let caret = textView.selectedRange().location
        var start = caret
        while start > 0 {
            let ch = Character(UnicodeScalar(ns.character(at: start - 1))!)
            if ch.isLetter || ch.isNumber || "_.-@".contains(ch) { start -= 1 } else { break }
        }
        let range = NSRange(location: start, length: caret - start)
        var word = ns.substring(with: range)
        if word.hasPrefix("@") { word.removeFirst() }
        return (range, word)
    }

    private func insertMention(_ acct: String, replacing range: NSRange) {
        let insertion = "@\(acct) "
        if textView.shouldChangeText(in: range, replacementString: insertion) {
            textView.replaceCharacters(in: range, with: insertion)
            textView.didChangeText()
        }
        updateCounter()
        window?.makeFirstResponder(textView)
    }

    private func send() {
        let text = textView.string.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty || !attachments.isEmpty else { NSSound.beep(); return }

        var draft: [String: Any] = ["text": textView.string]
        if feature("content_warning") {
            let cw = cwField.stringValue.trimmingCharacters(in: .whitespaces)
            if !cw.isEmpty { draft["spoiler_text"] = cw }
        }
        let mentions = participantChecks.filter { $0.1.state == .on }.map { $0.0 }
        if !mentions.isEmpty { draft["mentions"] = mentions }
        if showVisibility { draft["visibility"] = visibilityPopup.indexOfSelectedItem }
        if !languages.isEmpty, languages.indices.contains(languagePopup.indexOfSelectedItem) {
            draft["language"] = languages[languagePopup.indexOfSelectedItem].code
        }
        if pollCheck.state == .on {
            let options = pollOptionFields.map { $0.stringValue.trimmingCharacters(in: .whitespaces) }
                .filter { !$0.isEmpty }
            guard options.count >= 2 else { NSSound.beep(); return }
            draft["poll"] = [
                "options": options,
                "multiple": pollMultiple.state == .on,
                "expires_in_seconds": pollDurations[pollDuration.indexOfSelectedItem].1,
            ]
        }
        if scheduleCheck.state == .on {
            draft["scheduled_at"] = Int(schedulePicker.dateValue.timeIntervalSince1970)
        }
        if !attachments.isEmpty {
            draft["attachments"] = attachments.map { a in
                ["filename": a.filename, "mime": a.mime, "alt": a.alt, "data": a.data]
            }
        }
        switch context.mode {
        case "reply":
            if let id = context.replyToId { draft["reply_to_id"] = id }
            if let url = context.replyToUrl { draft["reply_to_url"] = url }
        case "quote":
            if let id = context.quotedStatusId { draft["quoted_status_id"] = id }
            if let cid = context.quotedStatusCid { draft["quoted_status_cid"] = cid }
            if let url = context.quotedStatusUrl { draft["quoted_status_url"] = url }
        default:
            break
        }
        state.post(draft: draft, editId: isEditing ? context.editId : nil)
        sendButton.isEnabled = false
    }
}

/// A text view that sends on Return or ⌘Return depending on the user's
/// "Return to send" preference (⇧Return always inserts a newline).
final class ComposeTextView: NSTextView {
    var enterToSend = false
    var onSend: (() -> Void)?
    var onMention: (() -> Void)?

    override func keyDown(with event: NSEvent) {
        // ⌥A — @-mention autocomplete (matches the Windows Alt+A shortcut).
        if event.modifierFlags.contains(.option), event.keyCode == 0 { onMention?(); return }
        let isReturn = event.keyCode == 36 || event.keyCode == 76
        if isReturn {
            let mods = event.modifierFlags
            if mods.contains(.command) { onSend?(); return }
            if enterToSend, !mods.contains(.shift) { onSend?(); return }
        }
        super.keyDown(with: event)
    }
}
