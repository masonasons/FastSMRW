import AppKit

/// The player's audio effects: checkboxes for which are on, a list of the settings
/// those effects offer, and a slider for the selected setting's value.
///
/// Every string here is composed by the core — the effect names, and the value as text
/// ("+3.0 dB", "Cathedral") — so this page reads the same as the Windows one. Changes
/// apply and are saved as they are made; there is no Apply button because the whole
/// point is hearing the effect while you adjust it.
final class EffectsWindowController: NSWindowController {
    private let state: AppState
    private var boxes: [(String, NSButton)] = [] // (effect key, checkbox)
    private let paramList = NSTableView()
    private let paramScroll = NSScrollView()
    private let valueSlider = NSSlider()
    private let valueLabel = NSTextField(labelWithString: "Value")
    private let effectsStack = NSStackView()
    /// The parameters that currently do anything, in catalog order.
    private var shown: [MediaParam] = []

    init(state: AppState) {
        self.state = state
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 420, height: 520),
                              styleMask: [.titled, .closable, .resizable],
                              backing: .buffered, defer: false)
        super.init(window: window)
        window.title = "Effects"
        buildUI()
        state.onMediaEffects = { [weak self] in self?.reload() }
        state.getMediaEffects()
        window.delegate = self
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func beginSheet(for parent: NSWindow, completion: @escaping () -> Void) {
        parent.beginSheet(window!) { _ in completion() }
    }

    private func buildUI() {
        guard let content = window?.contentView else { return }

        let intro = NSTextField(wrappingLabelWithString:
            "Turn effects on, pick one of their settings, then set its value. "
            + "A setting can only be adjusted while its effect is on.")
        intro.widthAnchor.constraint(lessThanOrEqualToConstant: 380).isActive = true

        effectsStack.orientation = .vertical
        effectsStack.alignment = .leading
        effectsStack.spacing = 4

        paramList.addTableColumn(NSTableColumn(identifier: NSUserInterfaceItemIdentifier("p")))
        paramList.headerView = nil
        paramList.dataSource = self
        paramList.delegate = self
        paramList.setAccessibilityLabel("Setting to adjust")
        paramScroll.documentView = paramList
        paramScroll.hasVerticalScroller = true
        paramScroll.borderType = .bezelBorder
        paramScroll.heightAnchor.constraint(equalToConstant: 140).isActive = true
        paramScroll.widthAnchor.constraint(greaterThanOrEqualToConstant: 380).isActive = true

        valueSlider.target = self
        valueSlider.action = #selector(valueChanged)
        valueSlider.isContinuous = false // one change per release, not per pixel
        valueSlider.widthAnchor.constraint(greaterThanOrEqualToConstant: 380).isActive = true

        let close = NSButton(title: "Close", target: self, action: #selector(closeSheet))
        close.bezelStyle = .rounded
        close.keyEquivalent = "\u{1b}"
        let buttons = NSStackView(views: [NSView(), close])
        buttons.orientation = .horizontal

        let stack = NSStackView(views: [
            intro,
            NSTextField(labelWithString: "Effects:"),
            effectsStack,
            NSTextField(labelWithString: "Setting to adjust:"),
            paramScroll,
            valueLabel,
            valueSlider,
            buttons,
        ])
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

    private func reload() {
        guard state.mediaEffects.available else {
            valueLabel.stringValue = "This build of FastSMRW has no FastPlay player."
            valueSlider.isEnabled = false
            return
        }
        // Rebuild the checkboxes only when the set of effects changes; otherwise a
        // click would rebuild the control it was made on, taking focus with it.
        if boxes.count != state.mediaEffects.effects.count {
            for view in effectsStack.arrangedSubviews { view.removeFromSuperview() }
            boxes.removeAll()
            for effect in state.mediaEffects.effects {
                let box = NSButton(checkboxWithTitle: effect.name, target: self,
                                   action: #selector(effectToggled(_:)))
                box.state = effect.enabled ? .on : .off
                boxes.append((effect.key, box))
                effectsStack.addArrangedSubview(box)
            }
        } else {
            for (index, pair) in boxes.enumerated() {
                pair.1.state = state.mediaEffects.effects[index].enabled ? .on : .off
            }
        }

        let was = shown.indices.contains(paramList.selectedRow) ? shown[paramList.selectedRow].key : nil
        shown = state.mediaEffects.params.filter { $0.applies(given: state.mediaEffects.effects) }
        paramList.reloadData()
        // Keep adjusting the same setting where it still applies, so switching an
        // unrelated effect doesn't move the selection somewhere else.
        let row = was.flatMap { key in shown.firstIndex { $0.key == key } } ?? 0
        if !shown.isEmpty {
            paramList.selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
        }
        showValue()
    }

    private var current: MediaParam? {
        shown.indices.contains(paramList.selectedRow) ? shown[paramList.selectedRow] : nil
    }

    private func showValue() {
        guard let param = current else {
            valueSlider.isEnabled = false
            valueLabel.stringValue = shown.isEmpty
                ? "Turn an effect on to adjust its settings."
                : "Value"
            return
        }
        valueSlider.isEnabled = true
        valueSlider.minValue = Double(param.min)
        valueSlider.maxValue = Double(param.max)
        valueSlider.doubleValue = Double(param.value)
        // The label carries the value as text, because a slider announces only a
        // number and "Cathedral" is what matters for a choice setting.
        valueLabel.stringValue = "Value — \(param.name): \(param.display)"
        valueSlider.setAccessibilityLabel("Value, \(param.name)")
        valueSlider.setAccessibilityValueDescription(param.display)
    }

    @objc private func effectToggled(_ sender: NSButton) {
        guard let pair = boxes.first(where: { $0.1 === sender }) else { return }
        // The core answers with a fresh catalog, which reload() picks up — an effect's
        // settings only appear in the list while it is on.
        state.setMediaEffect(pair.0, on: sender.state == .on)
    }

    @objc private func valueChanged() {
        guard let param = current else { return }
        // Snap to the setting's step: a choice setting has only whole values, and a
        // free slider would land between two of them.
        let step = param.step > 0 ? param.step : 1
        let snapped = ((Float(valueSlider.doubleValue) - param.min) / step).rounded() * step + param.min
        valueSlider.doubleValue = Double(snapped)
        state.setMediaParam(param.key, value: snapped)
        // The core speaks where the value landed (it clamps), so nothing is said here.
    }

    @objc private func closeSheet() {
        guard let window else { return }
        // Shown as a window of its own from Settings, but kept able to be a sheet in
        // case it is ever presented that way.
        if let parent = window.sheetParent {
            parent.endSheet(window)
        } else {
            window.close()
        }
    }
}

extension EffectsWindowController: NSTableViewDataSource, NSTableViewDelegate {
    func numberOfRows(in tableView: NSTableView) -> Int { shown.count }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?,
                   row: Int) -> NSView? {
        let param = shown[row]
        let text = param.display.isEmpty ? param.name : "\(param.name): \(param.display)"
        let field = NSTextField(labelWithString: text)
        field.lineBreakMode = .byTruncatingTail
        return field
    }

    func tableViewSelectionDidChange(_ notification: Notification) { showValue() }
}

extension EffectsWindowController: NSWindowDelegate {
    func windowWillClose(_ notification: Notification) {
        // Hand the single catalog callback back, so nothing else is shadowed by a
        // window that is no longer on screen.
        state.onMediaEffects = nil
    }
}
