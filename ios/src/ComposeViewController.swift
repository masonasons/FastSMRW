//
//  ComposeViewController.swift
//
//  Compose a new post, reply, quote, or edit. Matches the Windows/Mac compose
//  dialogs: reply-recipient checklist, content warning, visibility, language,
//  poll, scheduling, media attachments with alt text, and @-mention
//  autocomplete — each shown only when the account/mode supports it (from
//  compose_context.features). The core composes all text and options; this
//  gathers a `draft` and sends `post`.
//

import PhotosUI
import UIKit
import UniformTypeIdentifiers

@MainActor
final class ComposeViewController: StaticFormViewController {
    private let state: AppState
    private let context: ComposeContext

    private let textView = UITextView()
    private let textCell = UITableViewCell(style: .default, reuseIdentifier: nil)
    private var cwField: UITextField?
    private let counterLabel = UILabel()
    private var postButton: UIBarButtonItem!

    private var participantToggles: [(acct: String, control: UISwitch)] = []
    private var visibilityIndex: Int
    private var showVisibility = false
    private var languages: [ComposeLanguage] = []
    private var languageIndex = 0

    private var pollEnabled = false
    private let pollToggle = UISwitch()
    private var pollOptionFields: [UITextField] = []
    private let pollMultiple = UISwitch()
    private var pollDurationIndex = 4 // 1 day
    private let pollDurations: [(String, Int)] = [
        ("5 minutes", 300), ("30 minutes", 1800), ("1 hour", 3600), ("6 hours", 21600),
        ("1 day", 86400), ("3 days", 259200), ("7 days", 604800),
    ]

    private var scheduleEnabled = false
    private let scheduleToggle = UISwitch()
    private let schedulePicker = UIDatePicker()

    private struct Attachment {
        let filename: String
        let mime: String
        var alt: String
        let data: String
    }
    private var attachments: [Attachment] = []
    private var showMedia = false

    private func feature(_ key: String) -> Bool { context.features?[key] ?? false }
    private var isEditing_: Bool { context.mode == "edit" }

    // Persistent cells (built once; rebuildSections() picks which appear).
    private var contextCell: UITableViewCell?
    private var cwCell: UITableViewCell?
    private var counterCell = UITableViewCell(style: .default, reuseIdentifier: nil)
    private var mentionCell: UITableViewCell?
    private var recipientCells: [UITableViewCell] = []
    private var visibilityCell: UITableViewCell?
    private var languageCell: UITableViewCell?
    private var pollToggleCell: UITableViewCell?
    private var pollDetailCells: [UITableViewCell] = []
    private var scheduleToggleCell: UITableViewCell?
    private var scheduleDetailCell: UITableViewCell?

    init(state: AppState, context: ComposeContext) {
        self.state = state
        self.context = context
        self.visibilityIndex = context.defaultVisibility ?? 0
        self.languages = context.languages ?? []
        super.init()
        title = context.title ?? "New Post"
        isModalInPresentation = true
        navigationItem.leftBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .cancel, target: self, action: #selector(cancelTapped))
        postButton = UIBarButtonItem(title: isEditing_ ? "Save" : "Post", style: .done,
                                     target: self, action: #selector(postTapped))
        navigationItem.rightBarButtonItem = postButton
        buildCells()
        rebuildSections()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        textView.becomeFirstResponder()
    }

    // MARK: Cells

    private func buildCells() {
        if let label = context.contextLabel, !label.isEmpty {
            let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
            var content = cell.defaultContentConfiguration()
            content.text = label
            content.textProperties.numberOfLines = 0
            content.textProperties.color = .secondaryLabel
            content.textProperties.font = .preferredFont(forTextStyle: .footnote)
            cell.contentConfiguration = content
            cell.selectionStyle = .none
            contextCell = cell
        }

        for participant in context.replyParticipants ?? [] {
            let toggle = UISwitch()
            toggle.isOn = participant.checked
            participantToggles.append((participant.acct, toggle))
            let title = participant.displayName.isEmpty
                ? "@\(participant.acct)"
                : "\(participant.displayName) (@\(participant.acct))"
            recipientCells.append(ToggleHostCell(title: title, toggle: toggle))
        }

        if feature("content_warning") {
            let (cell, field) = fieldCell("Content warning", text: context.prefillCw ?? "",
                                          placeholder: "Content warning (optional)")
            field.autocapitalizationType = .sentences
            cwField = field
            cwCell = cell
        }

        textView.font = .preferredFont(forTextStyle: .body)
        textView.isScrollEnabled = false
        textView.accessibilityLabel = "Post text"
        textView.delegate = self
        if let prefill = context.prefillText { textView.text = prefill }
        textView.translatesAutoresizingMaskIntoConstraints = false
        textCell.contentView.addSubview(textView)
        NSLayoutConstraint.activate([
            textView.leadingAnchor.constraint(
                equalTo: textCell.contentView.layoutMarginsGuide.leadingAnchor),
            textView.trailingAnchor.constraint(
                equalTo: textCell.contentView.layoutMarginsGuide.trailingAnchor),
            textView.topAnchor.constraint(equalTo: textCell.contentView.topAnchor, constant: 8),
            textView.bottomAnchor.constraint(equalTo: textCell.contentView.bottomAnchor,
                                             constant: -8),
            textView.heightAnchor.constraint(greaterThanOrEqualToConstant: 120),
        ])
        textCell.selectionStyle = .none

        counterLabel.font = .preferredFont(forTextStyle: .footnote)
        counterLabel.textColor = .secondaryLabel
        counterLabel.textAlignment = .right
        counterLabel.translatesAutoresizingMaskIntoConstraints = false
        counterCell.contentView.addSubview(counterLabel)
        NSLayoutConstraint.activate([
            counterLabel.leadingAnchor.constraint(
                equalTo: counterCell.contentView.layoutMarginsGuide.leadingAnchor),
            counterLabel.trailingAnchor.constraint(
                equalTo: counterCell.contentView.layoutMarginsGuide.trailingAnchor),
            counterLabel.topAnchor.constraint(equalTo: counterCell.contentView.topAnchor,
                                              constant: 6),
            counterLabel.bottomAnchor.constraint(equalTo: counterCell.contentView.bottomAnchor,
                                                 constant: -6),
        ])
        counterCell.selectionStyle = .none
        updateCounter()

        let mention = UITableViewCell(style: .default, reuseIdentifier: nil)
        var mentionContent = mention.defaultContentConfiguration()
        mentionContent.text = "Mention Someone…"
        mentionContent.textProperties.color = .tintColor
        mention.contentConfiguration = mentionContent
        mention.accessibilityTraits = .button
        mentionCell = mention

        if feature("visibility"), !isEditing_ {
            showVisibility = true
            visibilityCell = pickerCell(
                "Visibility", options: ["Public", "Quiet public", "Followers",
                                        "Specific people"],
                selected: visibilityIndex) { [weak self] index in
                self?.visibilityIndex = index
            }
        }
        if !languages.isEmpty, !isEditing_ {
            languageCell = pickerCell("Language", options: languages.map { $0.name },
                                      selected: 0) { [weak self] index in
                self?.languageIndex = index
            }
        }

        if feature("polls"), !isEditing_ {
            pollToggle.addTarget(self, action: #selector(pollToggled), for: .valueChanged)
            pollToggleCell = ToggleHostCell(title: "Add a poll", toggle: pollToggle)
            for i in 0..<4 {
                let (cell, field) = fieldCell("Poll choice \(i + 1)", text: "",
                                              placeholder: "Choice \(i + 1)")
                field.autocapitalizationType = .sentences
                pollOptionFields.append(field)
                pollDetailCells.append(cell)
            }
            pollDetailCells.append(ToggleHostCell(title: "Allow multiple choices",
                                                  toggle: pollMultiple))
            pollDetailCells.append(pickerCell("Duration",
                                              options: pollDurations.map { $0.0 },
                                              selected: pollDurationIndex) { [weak self] index in
                self?.pollDurationIndex = index
            })
        }

        if feature("scheduling"), !isEditing_ {
            scheduleToggle.addTarget(self, action: #selector(scheduleToggled),
                                     for: .valueChanged)
            scheduleToggleCell = ToggleHostCell(title: "Schedule for later",
                                                toggle: scheduleToggle)
            let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
            schedulePicker.datePickerMode = .dateAndTime
            schedulePicker.minimumDate = Date()
            schedulePicker.date = Date(timeIntervalSinceNow: 3600)
            schedulePicker.accessibilityLabel = "Scheduled time"
            schedulePicker.translatesAutoresizingMaskIntoConstraints = false
            cell.contentView.addSubview(schedulePicker)
            NSLayoutConstraint.activate([
                schedulePicker.leadingAnchor.constraint(
                    equalTo: cell.contentView.layoutMarginsGuide.leadingAnchor),
                schedulePicker.topAnchor.constraint(equalTo: cell.contentView.topAnchor,
                                                    constant: 6),
                schedulePicker.bottomAnchor.constraint(equalTo: cell.contentView.bottomAnchor,
                                                       constant: -6),
            ])
            cell.selectionStyle = .none
            scheduleDetailCell = cell
        }

        showMedia = feature("media") && !isEditing_
    }

    /// Compose the visible sections from the current toggle state. Cells are
    /// persistent, so text the user typed survives every rebuild.
    private func rebuildSections() {
        var sections: [FormSection] = []
        if let contextCell {
            sections.append(FormSection(title: nil, footer: nil, cells: [contextCell]))
        }
        if !recipientCells.isEmpty {
            sections.append(FormSection(title: "Recipients", footer: nil,
                                        cells: recipientCells))
        }
        var postCells: [UITableViewCell] = []
        if let cwCell { postCells.append(cwCell) }
        postCells.append(textCell)
        postCells.append(counterCell)
        if let mentionCell { postCells.append(mentionCell) }
        sections.append(FormSection(title: nil, footer: nil, cells: postCells))

        var optionCells: [UITableViewCell] = []
        if let visibilityCell { optionCells.append(visibilityCell) }
        if let languageCell { optionCells.append(languageCell) }
        if !optionCells.isEmpty {
            sections.append(FormSection(title: nil, footer: nil, cells: optionCells))
        }
        if let pollToggleCell {
            sections.append(FormSection(title: "Poll", footer: nil,
                                        cells: [pollToggleCell]
                                            + (pollEnabled ? pollDetailCells : [])))
        }
        if let scheduleToggleCell {
            var cells = [scheduleToggleCell]
            if scheduleEnabled, let scheduleDetailCell { cells.append(scheduleDetailCell) }
            sections.append(FormSection(title: "Scheduling", footer: nil, cells: cells))
        }
        if showMedia {
            var cells: [UITableViewCell] = attachments.map { attachmentCell(for: $0) }
            let add = UITableViewCell(style: .default, reuseIdentifier: nil)
            var content = add.defaultContentConfiguration()
            content.text = "Add Attachment…"
            content.textProperties.color = .tintColor
            add.contentConfiguration = content
            add.accessibilityTraits = .button
            cells.append(add)
            sections.append(FormSection(
                title: "Attachments",
                footer: attachments.isEmpty ? nil
                    : "Tap an attachment to edit its description; swipe to remove.",
                cells: cells))
        }
        formSections = sections
        if viewIfLoaded != nil { tableView.reloadData() }
    }

    private func attachmentCell(for attachment: Attachment) -> UITableViewCell {
        let cell = UITableViewCell(style: .subtitle, reuseIdentifier: nil)
        var content = cell.defaultContentConfiguration()
        content.text = attachment.filename
        content.secondaryText = attachment.alt.isEmpty ? "No description" : attachment.alt
        cell.contentConfiguration = content
        cell.accessibilityLabel = attachment.filename + ", "
            + (attachment.alt.isEmpty ? "no description" : "description: \(attachment.alt)")
        return cell
    }

    @objc private func pollToggled() {
        pollEnabled = pollToggle.isOn
        rebuildSections()
    }

    @objc private func scheduleToggled() {
        scheduleEnabled = scheduleToggle.isOn
        rebuildSections()
    }

    private func updateCounter() {
        let count = textView.text.count
        if let max = context.maxChars {
            counterLabel.text = "\(count)/\(max)"
            counterLabel.textColor = count > max ? .systemRed : .secondaryLabel
            counterLabel.accessibilityLabel = "\(max - count) characters remaining"
        } else {
            counterLabel.text = "\(count)"
            counterLabel.accessibilityLabel = "\(count) characters"
        }
    }

    // MARK: Row taps

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: true)
        let cell = formSections[indexPath.section].cells[indexPath.row]
        if cell === mentionCell {
            presentMentionPicker()
        } else if showMedia, indexPath.section == formSections.count - 1 {
            if indexPath.row == attachments.count {
                presentAttachmentSource()
            } else if attachments.indices.contains(indexPath.row) {
                editAlt(row: indexPath.row)
            }
        }
    }

    override func tableView(_ tableView: UITableView,
                            trailingSwipeActionsConfigurationForRowAt indexPath: IndexPath)
        -> UISwipeActionsConfiguration? {
        guard showMedia, indexPath.section == formSections.count - 1,
              attachments.indices.contains(indexPath.row) else { return nil }
        let remove = UIContextualAction(style: .destructive, title: "Remove") {
            [weak self] _, _, done in
            self?.attachments.remove(at: indexPath.row)
            self?.rebuildSections()
            self?.updateCounter()
            done(true)
        }
        return UISwipeActionsConfiguration(actions: [remove])
    }

    // MARK: Attachments

    private func presentAttachmentSource() {
        let sheet = UIAlertController(title: "Add Attachment", message: nil,
                                      preferredStyle: .actionSheet)
        sheet.addAction(UIAlertAction(title: "Photo or Video", style: .default) {
            [weak self] _ in
            var config = PHPickerConfiguration()
            config.selectionLimit = 4
            let picker = PHPickerViewController(configuration: config)
            picker.delegate = self
            self?.present(picker, animated: true)
        })
        sheet.addAction(UIAlertAction(title: "Browse Files…", style: .default) {
            [weak self] _ in
            let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.item],
                                                        asCopy: true)
            picker.delegate = self
            self?.present(picker, animated: true)
        })
        sheet.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        sheet.popoverPresentationController?.sourceView = view
        sheet.popoverPresentationController?.sourceRect = view.bounds
        present(sheet, animated: true)
    }

    private func addAttachment(filename: String, mime: String, data: Data) {
        attachments.append(Attachment(filename: filename, mime: mime, alt: "",
                                      data: data.base64EncodedString()))
        rebuildSections()
        updateCounter()
    }

    private func editAlt(row: Int) {
        guard attachments.indices.contains(row) else { return }
        let alert = UIAlertController(
            title: "Description",
            message: "Describe \(attachments[row].filename) for people who can't see it.",
            preferredStyle: .alert)
        alert.addTextField { [weak self] field in
            field.text = self?.attachments[row].alt
            field.placeholder = "Alt text"
        }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        alert.addAction(UIAlertAction(title: "Save", style: .default) {
            [weak self, weak alert] _ in
            guard let self, self.attachments.indices.contains(row) else { return }
            self.attachments[row].alt = alert?.textFields?.first?.text ?? ""
            self.rebuildSections()
        })
        present(alert, animated: true)
    }

    // MARK: Mentions

    private func presentMentionPicker() {
        let (range, word) = mentionWordAtCaret()
        let picker = MentionPickerViewController(state: state, initialQuery: word) {
            [weak self] acct in
            self?.insertMention(acct, replacing: range)
        }
        present(UINavigationController(rootViewController: picker), animated: true)
    }

    /// The handle-like word ending at the caret, so completing replaces it.
    private func mentionWordAtCaret() -> (NSRange, String) {
        let ns = textView.text as NSString
        let caret = textView.selectedRange.location
        var start = caret
        while start > 0 {
            guard let scalar = UnicodeScalar(ns.character(at: start - 1)) else { break }
            let ch = Character(scalar)
            if ch.isLetter || ch.isNumber || "_.-@".contains(ch) { start -= 1 } else { break }
        }
        let range = NSRange(location: start, length: caret - start)
        var word = ns.substring(with: range)
        if word.hasPrefix("@") { word.removeFirst() }
        return (range, word)
    }

    private func insertMention(_ acct: String, replacing range: NSRange) {
        let insertion = "@\(acct) "
        let ns = textView.text as NSString
        textView.text = ns.replacingCharacters(in: range, with: insertion)
        textView.selectedRange = NSRange(location: range.location + insertion.count, length: 0)
        updateCounter()
        textView.becomeFirstResponder()
    }

    // MARK: Send

    @objc private func cancelTapped() { dismiss(animated: true) }

    @objc private func postTapped() {
        let text = textView.text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty || !attachments.isEmpty else { return }

        var draft: [String: Any] = ["text": textView.text ?? ""]
        if let cw = cwField?.text?.trimmingCharacters(in: .whitespaces), !cw.isEmpty {
            draft["spoiler_text"] = cw
        }
        let mentions = participantToggles.filter { $0.control.isOn }.map { $0.acct }
        if !mentions.isEmpty { draft["mentions"] = mentions }
        if showVisibility { draft["visibility"] = visibilityIndex }
        if !languages.isEmpty, !isEditing_, languages.indices.contains(languageIndex) {
            draft["language"] = languages[languageIndex].code
        }
        if pollEnabled {
            let options = pollOptionFields
                .compactMap { $0.text?.trimmingCharacters(in: .whitespaces) }
                .filter { !$0.isEmpty }
            guard options.count >= 2 else {
                showError("A poll needs at least two choices.", on: self)
                return
            }
            draft["poll"] = [
                "options": options,
                "multiple": pollMultiple.isOn,
                "expires_in_seconds": pollDurations[pollDurationIndex].1,
            ]
        }
        if scheduleEnabled {
            draft["scheduled_at"] = Int(schedulePicker.date.timeIntervalSince1970)
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
        postButton.isEnabled = false
        state.post(draft: draft, editId: isEditing_ ? context.editId : nil)
    }

    /// post_result arrived: close on success, re-enable on failure (the core
    /// announces the error).
    func postFinished(ok: Bool) {
        if ok {
            dismiss(animated: true)
        } else {
            postButton.isEnabled = true
        }
    }
}

// MARK: - Text view

extension ComposeViewController: UITextViewDelegate {
    func textViewDidChange(_ textView: UITextView) {
        updateCounter()
        // Self-sizing cell: re-measure without reloading (reload would drop
        // the keyboard and VoiceOver focus).
        tableView.performBatchUpdates {}
    }
}

// MARK: - Attachment pickers

extension ComposeViewController: PHPickerViewControllerDelegate {
    nonisolated func picker(_ picker: PHPickerViewController,
                            didFinishPicking results: [PHPickerResult]) {
        Task { @MainActor in
            picker.dismiss(animated: true)
            for result in results {
                let provider = result.itemProvider
                guard let typeId = provider.registeredTypeIdentifiers.first else { continue }
                let type = UTType(typeId)
                let mime = type?.preferredMIMEType ?? "application/octet-stream"
                let suggested = provider.suggestedName ?? "attachment"
                let ext = type?.preferredFilenameExtension
                let filename = ext != nil && !suggested.lowercased().hasSuffix(".\(ext!)")
                    ? "\(suggested).\(ext!)" : suggested
                provider.loadDataRepresentation(forTypeIdentifier: typeId) { data, _ in
                    guard let data else { return }
                    Task { @MainActor [weak self] in
                        self?.addAttachment(filename: filename, mime: mime, data: data)
                    }
                }
            }
        }
    }
}

extension ComposeViewController: UIDocumentPickerDelegate {
    nonisolated func documentPicker(_ controller: UIDocumentPickerViewController,
                                    didPickDocumentsAt urls: [URL]) {
        Task { @MainActor in
            for url in urls {
                guard let data = try? Data(contentsOf: url) else { continue }
                let mime = UTType(filenameExtension: url.pathExtension)?.preferredMIMEType
                    ?? "application/octet-stream"
                addAttachment(filename: url.lastPathComponent, mime: mime, data: data)
            }
        }
    }
}

// MARK: - Mention picker

/// Search users to @-mention: a text field plus live suggestions from the
/// core's autocomplete_users.
@MainActor
final class MentionPickerViewController: UITableViewController, UISearchBarDelegate {
    private let state: AppState
    private let onPick: (String) -> Void
    private let searchBar = UISearchBar()
    private var suggestions: [UserSuggestion] = []

    init(state: AppState, initialQuery: String, onPick: @escaping (String) -> Void) {
        self.state = state
        self.onPick = onPick
        super.init(style: .plain)
        title = "Mention"
        searchBar.text = initialQuery
        navigationItem.leftBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .cancel, target: self, action: #selector(cancelTapped))
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        searchBar.placeholder = "Name or handle"
        searchBar.delegate = self
        searchBar.autocapitalizationType = .none
        navigationItem.titleView = searchBar
        state.onUserSuggestions = { [weak self] result in
            guard let self else { return }
            // Ignore stale replies for earlier keystrokes.
            guard result.query == self.searchBar.text ?? "" else { return }
            self.suggestions = result.users
            self.tableView.reloadData()
        }
        if let query = searchBar.text, !query.isEmpty {
            state.autocompleteUsers(query: query)
        }
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        searchBar.becomeFirstResponder()
    }

    override func viewDidDisappear(_ animated: Bool) {
        super.viewDidDisappear(animated)
        state.onUserSuggestions = nil
    }

    func searchBar(_ searchBar: UISearchBar, textDidChange searchText: String) {
        guard !searchText.isEmpty else {
            suggestions = []
            tableView.reloadData()
            return
        }
        state.autocompleteUsers(query: searchText)
    }

    @objc private func cancelTapped() { dismiss(animated: true) }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { suggestions.count }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .subtitle, reuseIdentifier: nil)
        guard suggestions.indices.contains(indexPath.row) else { return cell }
        let user = suggestions[indexPath.row]
        var content = cell.defaultContentConfiguration()
        content.text = user.label.isEmpty ? "@\(user.acct)" : user.label
        content.secondaryText = user.label.isEmpty ? nil : "@\(user.acct)"
        cell.contentConfiguration = content
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        guard suggestions.indices.contains(indexPath.row) else { return }
        let acct = suggestions[indexPath.row].acct
        dismiss(animated: true) { [onPick] in onPick(acct) }
    }
}
