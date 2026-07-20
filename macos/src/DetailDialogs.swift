//
//  DetailDialogs.swift
//
//  The Enter-key detail sheets: Post Info (review a post + act on it) and User
//  Profile (a user + relationship actions), plus the user picker shown when a
//  post references several people. All text is composed by the core; these
//  render it (selectable / VoiceOver-readable) and offer action buttons that map
//  to core commands.
//

import AppKit

/// A sheet with a scrollable, selectable text body and a row of action buttons.
@MainActor
class DetailSheetController: NSWindowController {
    private var actions: [() -> Void] = []

    func configure(title: String, body: String, buttons: [(String, Bool, () -> Void)]) {
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 380),
                              styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.title = title
        self.window = window
        guard let content = window.contentView else { return }

        let textView = NSTextView()
        textView.isEditable = false
        textView.isSelectable = true
        textView.drawsBackground = false
        textView.textContainerInset = NSSize(width: 6, height: 6)
        textView.font = .systemFont(ofSize: 13)
        textView.string = body
        textView.setAccessibilityLabel(title)
        let scroll = NSScrollView()
        scroll.documentView = textView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false
        textView.autoresizingMask = [.width]
        textView.isVerticallyResizable = true
        textView.textContainer?.widthTracksTextView = true

        let buttonRow = NSStackView()
        buttonRow.orientation = .horizontal
        buttonRow.spacing = 8
        buttonRow.alignment = .centerY
        buttonRow.addArrangedSubview(NSView()) // push buttons to the right
        for (index, spec) in buttons.enumerated() {
            actions.append(spec.2)
            let button = NSButton(title: spec.0, target: self, action: #selector(buttonTapped(_:)))
            button.bezelStyle = .rounded
            button.tag = index
            if spec.1 { button.keyEquivalent = "\r" } // default button
            buttonRow.addArrangedSubview(button)
        }

        let stack = NSStackView(views: [scroll, buttonRow])
        stack.orientation = .vertical
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 16),
            scroll.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -16),
            buttonRow.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 16),
            buttonRow.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -16),
        ])
    }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        guard let window else { return }
        parent.beginSheet(window) { _ in completion() }
    }

    func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    @objc private func buttonTapped(_ sender: NSButton) {
        guard actions.indices.contains(sender.tag) else { return }
        actions[sender.tag]()
    }
}

/// Present the moderation "Report" dialog (app-modal), then submit via the core.
/// Pass a post `id` to report a post, or `accountId`/`acct` to report a user.
@MainActor
func presentReport(state: AppState, id: String?, accountId: String?, acct: String, remote: Bool) {
    let alert = NSAlert()
    alert.messageText = "Report"
    alert.informativeText = "This report is sent to your server's moderators."
    let categoryTokens = ["spam", "violation", "legal", "other"]
    let popup = NSPopUpButton(frame: NSRect(x: 0, y: 0, width: 300, height: 25))
    popup.addItems(withTitles: ["It's spam", "It breaks a server rule",
                                "It's illegal content", "Something else"])
    let comment = NSTextField(frame: NSRect(x: 0, y: 0, width: 300, height: 22))
    comment.placeholderString = "Additional details (optional)"
    let forward = NSButton(checkboxWithTitle: "Forward to the user's home server",
                           target: nil, action: nil)
    forward.state = remote ? .on : .off
    let stack = NSStackView(views: [popup, comment, forward])
    stack.orientation = .vertical
    stack.alignment = .leading
    stack.spacing = 8
    stack.frame = NSRect(x: 0, y: 0, width: 300, height: 92)
    alert.accessoryView = stack
    alert.addButton(withTitle: "Submit")
    alert.addButton(withTitle: "Cancel")
    guard alert.runModal() == .alertFirstButtonReturn else { return }
    let category = categoryTokens[max(0, min(popup.indexOfSelectedItem, categoryTokens.count - 1))]
    state.report(id: id, accountId: accountId, acct: acct,
                 category: category, comment: comment.stringValue, forward: forward.state == .on)
}

/// A top-left-origin container so the profile form can be laid out top-to-bottom.
private final class FlippedContainer: NSView {
    override var isFlipped: Bool { true }
}

/// Present the "Edit Profile" dialog (app-modal), prefilled from the current
/// profile (display name, bio, metadata fields, privacy and flags), then submit.
@MainActor
func presentProfileEditor(state: AppState, editor: ProfileEditor) {
    let width: CGFloat = 380
    let rows = max(1, editor.maxFields)
    let privacyTokens = ["public", "unlisted", "private", "direct"]
    let container = FlippedContainer()
    var y: CGFloat = 0
    func addLabel(_ text: String) {
        let l = NSTextField(labelWithString: text)
        l.frame = NSRect(x: 0, y: y, width: width, height: 15)
        container.addSubview(l)
        y += 17
    }

    addLabel("Display name:")
    let nameField = NSTextField(string: editor.displayName)
    nameField.frame = NSRect(x: 0, y: y, width: width, height: 22)
    container.addSubview(nameField)
    y += 28

    addLabel("Bio:")
    let bioScroll = NSScrollView(frame: NSRect(x: 0, y: y, width: width, height: 90))
    bioScroll.hasVerticalScroller = true
    bioScroll.borderType = .bezelBorder
    let bioView = NSTextView(frame: NSRect(x: 0, y: 0, width: width, height: 90))
    bioView.string = editor.note
    bioView.isRichText = false
    bioView.isVerticallyResizable = true
    bioView.isHorizontallyResizable = false
    bioView.textContainer?.widthTracksTextView = true
    bioScroll.documentView = bioView
    container.addSubview(bioScroll)
    y += 96

    addLabel("Default post privacy:")
    let privacy = NSPopUpButton(frame: NSRect(x: 0, y: y, width: 180, height: 24))
    privacy.addItems(withTitles: ["Public", "Unlisted", "Followers only", "Direct"])
    privacy.selectItem(at: privacyTokens.firstIndex(of: editor.privacy) ?? 0)
    container.addSubview(privacy)
    y += 30

    func addCheck(_ title: String, _ on: Bool) -> NSButton {
        let b = NSButton(checkboxWithTitle: title, target: nil, action: nil)
        b.state = on ? .on : .off
        b.frame = NSRect(x: 0, y: y, width: width, height: 18)
        container.addSubview(b)
        y += 20
        return b
    }
    let locked = addCheck("Require follow requests", editor.locked)
    let bot = addCheck("This is a bot account", editor.bot)
    let discoverable = addCheck("List me in the profile directory", editor.discoverable)
    let sensitive = addCheck("Mark my media sensitive by default", editor.sensitive)
    y += 4

    addLabel("Profile fields (label and content):")
    let nameW: CGFloat = 130
    var fieldNames: [NSTextField] = []
    var fieldValues: [NSTextField] = []
    for i in 0..<rows {
        let n = NSTextField(string: i < editor.fields.count ? editor.fields[i].name : "")
        n.frame = NSRect(x: 0, y: y, width: nameW, height: 22)
        let v = NSTextField(string: i < editor.fields.count ? editor.fields[i].value : "")
        v.frame = NSRect(x: nameW + 8, y: y, width: width - nameW - 8, height: 22)
        container.addSubview(n)
        container.addSubview(v)
        fieldNames.append(n)
        fieldValues.append(v)
        y += 26
    }
    container.frame = NSRect(x: 0, y: 0, width: width, height: y)

    let alert = NSAlert()
    alert.messageText = "Edit Profile"
    alert.accessoryView = container
    alert.addButton(withTitle: "Save")
    alert.addButton(withTitle: "Cancel")
    guard alert.runModal() == .alertFirstButtonReturn else { return }
    var fields: [[String: String]] = []
    for i in 0..<rows {
        fields.append(["name": fieldNames[i].stringValue, "value": fieldValues[i].stringValue])
    }
    let token = privacyTokens[max(0, min(privacy.indexOfSelectedItem, privacyTokens.count - 1))]
    state.updateProfile(displayName: nameField.stringValue, note: bioView.string,
                        locked: locked.state == .on, bot: bot.state == .on,
                        discoverable: discoverable.state == .on, sensitive: sensitive.state == .on,
                        privacy: token, fields: fields)
}

/// Post Info — review a post and act on it.
@MainActor
final class PostInfoWindowController: DetailSheetController {
    private var pollController: PollVoteWindowController?

    init(state: AppState, info: PostInfo) {
        super.init(window: nil)
        let id = info.id
        var buttons: [(String, Bool, () -> Void)] = [
            ("Reply", false, { [weak self] in self?.dismiss(); state.requestCompose(mode: "reply", id: id) }),
            ("Boost", false, { state.toggleBoost(id: id) }),
            ("Favorite", false, { state.toggleFavorite(id: id) }),
        ]
        if info.features?["quote"] != false {
            buttons.append(("Quote", false, { [weak self] in
                self?.dismiss(); state.requestCompose(mode: "quote", id: id)
            }))
        }
        if let poll = info.poll {
            buttons.append(("Vote…", false, { [weak self] in
                guard let self, let window = self.window else { return }
                let controller = PollVoteWindowController(state: state, statusId: id, poll: poll)
                self.pollController = controller
                controller.beginSheet(for: window) { [weak self] in self?.pollController = nil }
            }))
        }
        buttons.append(("View Thread", false, { [weak self] in self?.dismiss(); state.openThread(id: id) }))
        if info.favoritesCount > 0 {
            buttons.append(("View Favorited (\(info.favoritesCount))", false, { [weak self] in
                self?.dismiss(); state.openFavoritedBy(id: id)
            }))
        }
        if info.boostsCount > 0 {
            buttons.append(("View Reposters (\(info.boostsCount))", false, { [weak self] in
                self?.dismiss(); state.openRebloggedBy(id: id)
            }))
        }
        if info.features?["mute_conversations"] == true {
            buttons.append((info.muted ? "Unmute Conversation" : "Mute Conversation", false, {
                state.toggleMuteConversation(id: id)
            }))
        }
        if info.hasUrl {
            buttons.append(("Open in Browser", false, { [weak self] in
                self?.dismiss(); state.openStatus(id: id)
            }))
        }
        buttons.append(("Report", false, { [weak self] in
            self?.dismiss()
            presentReport(state: state, id: id, accountId: nil, acct: "", remote: false)
        }))
        buttons.append(("Close", true, { [weak self] in self?.dismiss() }))
        configure(title: "Post Info", body: info.text, buttons: buttons)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
}

/// Vote in a poll: checkboxes for multiple-choice, radios for single.
@MainActor
final class PollVoteWindowController: NSWindowController {
    private let state: AppState
    private let statusId: String
    private let poll: PostInfoPoll
    private var choices: [NSButton] = []

    init(state: AppState, statusId: String, poll: PostInfoPoll) {
        self.state = state
        self.statusId = statusId
        self.poll = poll
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 360, height: 260),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Vote"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
    }
    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        var views: [NSView] = []
        for (i, option) in poll.options.enumerated() {
            let box = poll.multiple ? NSButton(checkboxWithTitle: option, target: nil, action: nil)
                : NSButton(radioButtonWithTitle: option, target: nil, action: nil)
            box.tag = i
            choices.append(box)
            views.append(box)
        }
        let vote = NSButton(title: "Vote", target: self, action: #selector(submit))
        vote.bezelStyle = .rounded
        vote.keyEquivalent = "\r"
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"
        views.append(NSStackView(views: [NSView(), cancel, vote]))

        let stack = NSStackView(views: views)
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 8
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
        ])
    }

    @objc private func submit() {
        let selected = choices.filter { $0.state == .on }.map { $0.tag }
        guard !selected.isEmpty else { NSSound.beep(); return }
        state.votePoll(id: statusId, choices: selected)
        dismiss()
    }
    @objc private func cancel() { dismiss() }
}

/// User Profile — a user plus relationship actions.
@MainActor
final class UserProfileWindowController: DetailSheetController {
    init(state: AppState, profile: UserProfile) {
        super.init(window: nil)
        let acct = profile.acct
        let accountId = profile.accountId
        var buttons: [(String, Bool, () -> Void)] = []
        if profile.hasRelationship {
            let following = profile.following ?? false
            buttons.append((following ? "Unfollow" : "Follow", false, { [weak self] in
                self?.dismiss()
                state.setRelationship(accountId: accountId,
                                      action: following ? "unfollow" : "follow", acct: acct)
            }))
            let muting = profile.muting ?? false
            buttons.append((muting ? "Unmute" : "Mute", false, { [weak self] in
                self?.dismiss()
                state.setRelationship(accountId: accountId,
                                      action: muting ? "unmute" : "mute", acct: acct)
            }))
            let blocking = profile.blocking ?? false
            buttons.append((blocking ? "Unblock" : "Block", false, { [weak self] in
                self?.dismiss()
                state.setRelationship(accountId: accountId,
                                      action: blocking ? "unblock" : "block", acct: acct)
            }))
        }
        buttons.append(("Open Timeline", false, { [weak self] in
            self?.dismiss(); state.openUserTimeline(accountId: accountId, acct: acct)
        }))
        if profile.canUseLists {
            buttons.append(("Add to List…", false, { [weak self] in
                self?.dismiss(); state.getUserLists(accountId: accountId, acct: acct)
            }))
        }
        if let url = profile.url, let link = URL(string: url) {
            buttons.append(("Open in Browser", false, { [weak self] in
                self?.dismiss(); NSWorkspace.shared.open(link)
            }))
        }
        buttons.append(("Report", false, { [weak self] in
            self?.dismiss()
            presentReport(state: state, id: nil, accountId: accountId, acct: acct,
                          remote: acct.contains("@"))
        }))
        buttons.append(("Close", true, { [weak self] in self?.dismiss() }))
        configure(title: "User: @\(acct)", body: profile.text, buttons: buttons)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
}

/// Disambiguation sheet: pick which referenced user the action applies to.
@MainActor
final class UserPickerWindowController: NSWindowController, NSTableViewDataSource,
    NSTableViewDelegate {
    private let state: AppState
    private let picker: UserPicker
    private let tableView = NSTableView()
    private let cellIdentifier = NSUserInterfaceItemIdentifier("PickCell")

    init(state: AppState, picker: UserPicker) {
        self.state = state
        self.picker = picker
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 360, height: 300),
                              styleMask: [.titled], backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Choose a User"
        buildUI()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
        if !picker.users.isEmpty {
            tableView.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
        }
        window?.makeFirstResponder(tableView)
    }

    private func dismiss() {
        guard let window, let parent = window.sheetParent else { return }
        parent.endSheet(window)
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }
        let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier("user"))
        column.resizingMask = .autoresizingMask
        tableView.addTableColumn(column)
        tableView.headerView = nil
        tableView.dataSource = self
        tableView.delegate = self
        tableView.doubleAction = #selector(choose)
        tableView.target = self
        tableView.setAccessibilityLabel("Users")

        let scroll = NSScrollView()
        scroll.documentView = tableView
        scroll.hasVerticalScroller = true
        scroll.borderType = .bezelBorder
        scroll.translatesAutoresizingMaskIntoConstraints = false

        let choose = NSButton(title: "Choose", target: self, action: #selector(self.choose))
        choose.bezelStyle = .rounded
        choose.keyEquivalent = "\r"
        let cancel = NSButton(title: "Cancel", target: self, action: #selector(self.cancel))
        cancel.bezelStyle = .rounded
        cancel.keyEquivalent = "\u{1b}"
        let buttons = NSStackView(views: [NSView(), cancel, choose])
        buttons.orientation = .horizontal
        buttons.spacing = 8

        let stack = NSStackView(views: [scroll, buttons])
        stack.orientation = .vertical
        stack.spacing = 12
        stack.edgeInsets = NSEdgeInsets(top: 16, left: 16, bottom: 16, right: 16)
        stack.translatesAutoresizingMaskIntoConstraints = false
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: stack.leadingAnchor, constant: 16),
            scroll.trailingAnchor.constraint(equalTo: stack.trailingAnchor, constant: -16),
        ])
    }

    @objc private func choose() {
        guard picker.users.indices.contains(tableView.selectedRow) else { return }
        let user = picker.users[tableView.selectedRow]
        switch picker.purpose {
        case "timeline": state.openUserTimeline(accountId: user.id, acct: user.acct)
        case "profile": state.openUserProfile(accountId: user.id, acct: user.acct)
        case "follow_toggle": state.followToggle(accountId: user.id, acct: user.acct)
        case "alias": state.beginAliasAccount(id: picker.id, accountId: user.id)
        default: break
        }
        dismiss()
    }

    @objc private func cancel() { dismiss() }

    func numberOfRows(in tableView: NSTableView) -> Int { picker.users.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let cell: NSTableCellView
        if let reused = tableView.makeView(withIdentifier: cellIdentifier, owner: self)
            as? NSTableCellView {
            cell = reused
        } else {
            cell = NSTableCellView()
            cell.identifier = cellIdentifier
            let textField = NSTextField(labelWithString: "")
            textField.translatesAutoresizingMaskIntoConstraints = false
            cell.addSubview(textField)
            cell.textField = textField
            NSLayoutConstraint.activate([
                textField.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 6),
                textField.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -6),
                textField.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
            ])
        }
        guard picker.users.indices.contains(row) else { return cell }
        let acct = "@\(picker.users[row].acct)"
        cell.textField?.stringValue = acct
        cell.setAccessibilityLabel(acct)
        return cell
    }
}
