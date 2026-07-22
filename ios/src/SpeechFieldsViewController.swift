//
//  SpeechFieldsViewController.swift
//
//  One speech/copy category's field list (from the core's speech catalog +
//  saved settings): tap toggles whether a field is spoken; VoiceOver custom
//  actions (or Edit-mode dragging) reorder; "Extra Spoken Text" edits the
//  per-field before/after wrap text. Every change writes straight back into
//  settings.speech.<category> via update_settings.
//

import UIKit

@MainActor
final class SpeechFieldsViewController: UITableViewController {
    private let state: AppState
    private let category: String

    private struct Field {
        let key: String
        let label: String
        var enabled: Bool
        var before: String
        var after: String
        var noSeparatorAfter: Bool
    }
    private var fields: [Field]

    init(state: AppState, category: String, title: String) {
        self.state = state
        self.category = category

        // Merge saved order/enabled/wrap with the catalog's labels; append any
        // catalog field the saved list is missing (enabled) so nothing is lost.
        // Same logic as the Mac's Speech Details sheet.
        let catalog = state.speechCatalog?.fields(for: category) ?? []
        let labels = Dictionary(catalog.map { ($0.key, $0.label) },
                                uniquingKeysWith: { a, _ in a })
        var built: [Field] = []
        var seen = Set<String>()
        for item in state.speechItems(for: category) {
            guard let key = item["field"] as? String, let label = labels[key] else { continue }
            seen.insert(key)
            built.append(Field(key: key, label: label,
                               enabled: item["enabled"] as? Bool ?? true,
                               before: item["before"] as? String ?? "",
                               after: item["after"] as? String ?? "",
                               noSeparatorAfter: item["no_separator_after"] as? Bool ?? false))
        }
        for field in catalog where !seen.contains(field.key) {
            built.append(Field(key: field.key, label: field.label, enabled: true,
                               before: "", after: "", noSeparatorAfter: false))
        }
        fields = built

        super.init(style: .insetGrouped)
        self.title = title
        navigationItem.rightBarButtonItem = editButtonItem
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    private func save() {
        let items: [[String: Any]] = fields.map { f in
            var item: [String: Any] = ["field": f.key, "enabled": f.enabled]
            if !f.before.isEmpty { item["before"] = f.before }
            if !f.after.isEmpty { item["after"] = f.after }
            if f.noSeparatorAfter { item["no_separator_after"] = true }
            return item
        }
        state.setSpeechItems(items, for: category)
    }

    // MARK: Table

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { fields.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        "Tap a detail to toggle whether it's spoken. Use the row's actions (or Edit) "
            + "to move it up or down, or to add extra spoken text around it."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        guard fields.indices.contains(indexPath.row) else { return cell }
        let field = fields[indexPath.row]
        var content = cell.defaultContentConfiguration()
        content.text = field.label
        cell.contentConfiguration = content
        cell.accessoryType = field.enabled ? .checkmark : .none
        cell.isAccessibilityElement = true
        cell.accessibilityLabel = field.label
        cell.accessibilityValue = field.enabled ? "Spoken" : "Not spoken"
        cell.accessibilityCustomActions = customActions(row: indexPath.row)
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: false)
        guard fields.indices.contains(indexPath.row) else { return }
        fields[indexPath.row].enabled.toggle()
        save()
        tableView.reloadRows(at: [indexPath], with: .none)
    }

    private func customActions(row: Int) -> [UIAccessibilityCustomAction] {
        var actions: [UIAccessibilityCustomAction] = []
        if row > 0 {
            actions.append(UIAccessibilityCustomAction(name: "Move Up") { [weak self] _ in
                self?.move(from: row, to: row - 1)
                return true
            })
        }
        if row < fields.count - 1 {
            actions.append(UIAccessibilityCustomAction(name: "Move Down") { [weak self] _ in
                self?.move(from: row, to: row + 1)
                return true
            })
        }
        actions.append(UIAccessibilityCustomAction(name: "Extra Spoken Text") { [weak self] _ in
            self?.editWrapText(row: row)
            return true
        })
        return actions
    }

    private func move(from: Int, to: Int) {
        guard fields.indices.contains(from), fields.indices.contains(to) else { return }
        fields.swapAt(from, to)
        save()
        tableView.reloadData()
        // Keep VoiceOver on the moved field and confirm its new position.
        let cell = tableView.cellForRow(at: IndexPath(row: to, section: 0))
        UIAccessibility.post(notification: .layoutChanged, argument: cell)
        UIAccessibility.post(notification: .announcement,
                             argument: "\(fields[to].label), position \(to + 1) of \(fields.count)")
    }

    private func editWrapText(row: Int) {
        guard fields.indices.contains(row) else { return }
        let editor = WrapTextViewController(field: fields[row].label,
                                            before: fields[row].before,
                                            after: fields[row].after,
                                            noSeparator: fields[row].noSeparatorAfter) {
            [weak self] before, after, noSeparator in
            guard let self, self.fields.indices.contains(row) else { return }
            self.fields[row].before = before
            self.fields[row].after = after
            self.fields[row].noSeparatorAfter = noSeparator
            self.save()
        }
        navigationController?.pushViewController(editor, animated: true)
    }

    // MARK: Edit-mode reordering (sighted drag handles)

    override func tableView(_ tableView: UITableView,
                            canMoveRowAt indexPath: IndexPath) -> Bool { true }

    override func tableView(_ tableView: UITableView, moveRowAt sourceIndexPath: IndexPath,
                            to destinationIndexPath: IndexPath) {
        let field = fields.remove(at: sourceIndexPath.row)
        fields.insert(field, at: destinationIndexPath.row)
        save()
    }

    override func tableView(_ tableView: UITableView,
                            editingStyleForRowAt indexPath: IndexPath)
        -> UITableViewCell.EditingStyle { .none }

    override func tableView(_ tableView: UITableView,
                            shouldIndentWhileEditingRowAt indexPath: IndexPath) -> Bool { false }
}

// MARK: - Movement units (rotor) picker

/// Choose which movement units appear in the VoiceOver rotor, and their order.
/// Tap toggles a unit; the row's actions (or Edit) reorder. Writes straight
/// into the movement_units setting, which also drives the desktop unit cycle.
@MainActor
final class MovementUnitsViewController: UITableViewController {
    private let state: AppState

    private struct Unit {
        let key: String
        let label: String
        var enabled: Bool
    }
    private var units: [Unit]

    init(state: AppState) {
        self.state = state
        // Merge the saved order/enabled with the catalog's labels; append any
        // catalog unit the saved list is missing (enabled) — same shape the
        // core's normalization uses.
        let labels = Dictionary(state.movementCatalog.map { ($0.key, $0.label) },
                                uniquingKeysWith: { a, _ in a })
        var built: [Unit] = []
        var seen = Set<String>()
        for item in state.movementItems() {
            guard let key = item["unit"] as? String, let label = labels[key],
                  seen.insert(key).inserted else { continue }
            built.append(Unit(key: key, label: label,
                              enabled: item["enabled"] as? Bool ?? true))
        }
        for field in state.movementCatalog where seen.insert(field.key).inserted {
            built.append(Unit(key: field.key, label: field.label, enabled: true))
        }
        units = built
        super.init(style: .insetGrouped)
        title = "Movement Rotors"
        navigationItem.rightBarButtonItem = editButtonItem
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    private func save() {
        state.setMovementItems(units.map { ["unit": $0.key, "enabled": $0.enabled] })
    }

    override func tableView(_ tableView: UITableView,
                            numberOfRowsInSection section: Int) -> Int { units.count }

    override func tableView(_ tableView: UITableView,
                            titleForFooterInSection section: Int) -> String? {
        "Each enabled unit is a VoiceOver rotor on the posts list. Tap a unit to "
            + "toggle it; use its actions (or Edit) to reorder."
    }

    override func tableView(_ tableView: UITableView,
                            cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = UITableViewCell(style: .default, reuseIdentifier: nil)
        guard units.indices.contains(indexPath.row) else { return cell }
        let unit = units[indexPath.row]
        var content = cell.defaultContentConfiguration()
        content.text = unit.label
        cell.contentConfiguration = content
        cell.accessoryType = unit.enabled ? .checkmark : .none
        cell.isAccessibilityElement = true
        cell.accessibilityLabel = unit.label
        cell.accessibilityValue = unit.enabled ? "In rotor" : "Hidden"
        var actions: [UIAccessibilityCustomAction] = []
        if indexPath.row > 0 {
            actions.append(UIAccessibilityCustomAction(name: "Move Up") { [weak self] _ in
                self?.move(from: indexPath.row, to: indexPath.row - 1)
                return true
            })
        }
        if indexPath.row < units.count - 1 {
            actions.append(UIAccessibilityCustomAction(name: "Move Down") { [weak self] _ in
                self?.move(from: indexPath.row, to: indexPath.row + 1)
                return true
            })
        }
        cell.accessibilityCustomActions = actions
        return cell
    }

    override func tableView(_ tableView: UITableView, didSelectRowAt indexPath: IndexPath) {
        tableView.deselectRow(at: indexPath, animated: false)
        guard units.indices.contains(indexPath.row) else { return }
        units[indexPath.row].enabled.toggle()
        save()
        tableView.reloadRows(at: [indexPath], with: .none)
    }

    private func move(from: Int, to: Int) {
        guard units.indices.contains(from), units.indices.contains(to) else { return }
        units.swapAt(from, to)
        save()
        tableView.reloadData()
        let cell = tableView.cellForRow(at: IndexPath(row: to, section: 0))
        UIAccessibility.post(notification: .layoutChanged, argument: cell)
        UIAccessibility.post(notification: .announcement,
                             argument: "\(units[to].label), position \(to + 1) of \(units.count)")
    }

    override func tableView(_ tableView: UITableView,
                            canMoveRowAt indexPath: IndexPath) -> Bool { true }

    override func tableView(_ tableView: UITableView, moveRowAt sourceIndexPath: IndexPath,
                            to destinationIndexPath: IndexPath) {
        let unit = units.remove(at: sourceIndexPath.row)
        units.insert(unit, at: destinationIndexPath.row)
        save()
    }

    override func tableView(_ tableView: UITableView,
                            editingStyleForRowAt indexPath: IndexPath)
        -> UITableViewCell.EditingStyle { .none }

    override func tableView(_ tableView: UITableView,
                            shouldIndentWhileEditingRowAt indexPath: IndexPath) -> Bool { false }
}

// MARK: - Wrap text editor

/// Edits one field's "speak before" / "speak after" text and the no-separator
/// flag. A small pushed form — deliberately not wrapped in a scroll view.
@MainActor
final class WrapTextViewController: UIViewController {
    private let beforeField = UITextField()
    private let afterField = UITextField()
    private let separatorToggle = UISwitch()
    private let onSave: (String, String, Bool) -> Void

    init(field: String, before: String, after: String, noSeparator: Bool,
         onSave: @escaping (String, String, Bool) -> Void) {
        self.onSave = onSave
        super.init(nibName: nil, bundle: nil)
        title = field
        beforeField.text = before
        afterField.text = after
        separatorToggle.isOn = noSeparator
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .save, target: self, action: #selector(saveTapped))

        for (field, label) in [(beforeField, "Speak before"), (afterField, "Speak after")] {
            field.placeholder = label
            field.accessibilityLabel = label
            field.borderStyle = .roundedRect
            field.autocapitalizationType = .none
        }
        let separatorLabel = UILabel()
        separatorLabel.text = "No separator after"
        separatorLabel.font = .preferredFont(forTextStyle: .body)
        separatorToggle.accessibilityLabel = "No separator after"
        let separatorRow = UIStackView(arrangedSubviews: [separatorLabel, separatorToggle])
        separatorRow.spacing = 8

        let stack = UIStackView(arrangedSubviews: [beforeField, afterField, separatorRow])
        stack.axis = .vertical
        stack.spacing = 16
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor,
                                       constant: 20),
            stack.leadingAnchor.constraint(equalTo: view.layoutMarginsGuide.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.layoutMarginsGuide.trailingAnchor),
        ])
    }

    @objc private func saveTapped() {
        onSave(beforeField.text ?? "", afterField.text ?? "", separatorToggle.isOn)
        navigationController?.popViewController(animated: true)
    }
}
