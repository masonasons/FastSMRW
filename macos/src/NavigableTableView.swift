//
//  NavigableTableView.swift
//
//  An NSTableView that surfaces Tab / Shift-Tab and left/right arrows as
//  closures, so we can move focus between the timelines and posts panes and
//  switch timelines with the arrow keys — the keyboard model from FastSM/Quinter.
//  Up/down keep their native row-navigation behavior. Ported from FastSMApple.
//

import AppKit

final class NavigableTableView: NSTableView {
    var onTab: (() -> Void)?
    var onBacktab: (() -> Void)?
    var onLeftArrow: (() -> Void)?
    var onRightArrow: (() -> Void)?
    var onCommandLeft: (() -> Void)?
    var onCommandRight: (() -> Void)?
    var onCommandUp: (() -> Void)?
    var onCommandDown: (() -> Void)?
    var onReturn: (() -> Void)?
    var onCommandReturn: (() -> Void)?
    var onShiftReturn: (() -> Void)?
    var onSpace: (() -> Void)?
    var onDelete: (() -> Void)?
    var onCommandDelete: (() -> Void)?
    /// Fired when trying to move past the top/bottom of the list.
    var onBoundary: (() -> Void)?
    /// Builds the right-click (Ctrl-click) menu for the row at the given index
    /// (-1 if the click missed all rows). Return nil for no menu.
    var menuProvider: ((Int) -> NSMenu?)?

    override func menu(for event: NSEvent) -> NSMenu? {
        guard let menuProvider else { return super.menu(for: event) }
        let point = convert(event.locationInWindow, from: nil)
        return menuProvider(row(at: point))
    }
    /// Handle a plain (un-modified) character key. Return true if consumed.
    var onCharacter: ((Character) -> Bool)?

    private enum Key {
        static let tab: UInt16 = 48
        static let `return`: UInt16 = 36
        static let keypadEnter: UInt16 = 76
        static let space: UInt16 = 49
        static let delete: UInt16 = 51
        static let leftArrow: UInt16 = 123
        static let rightArrow: UInt16 = 124
        static let upArrow: UInt16 = 126
        static let downArrow: UInt16 = 125
    }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case Key.tab:
            if event.modifierFlags.contains(.shift) {
                if let onBacktab { onBacktab(); return }
            } else if let onTab {
                onTab(); return
            }
        case Key.return, Key.keypadEnter:
            if event.modifierFlags.contains(.command) {
                if let onCommandReturn { onCommandReturn(); return }
            } else if event.modifierFlags.contains(.shift) {
                if let onShiftReturn { onShiftReturn(); return }
            } else if let onReturn {
                onReturn(); return
            }
        case Key.space:
            if let onSpace { onSpace(); return }
        case Key.delete:
            let mods = event.modifierFlags.intersection([.command, .shift, .option, .control])
            if mods == .command {
                if let onCommandDelete { onCommandDelete(); return }
            } else if mods.isEmpty, let onDelete {
                onDelete(); return
            }
        case Key.leftArrow:
            if event.modifierFlags.contains(.command), let onCommandLeft { onCommandLeft(); return }
            if let onLeftArrow { onLeftArrow(); return }
        case Key.rightArrow:
            if event.modifierFlags.contains(.command), let onCommandRight { onCommandRight(); return }
            if let onRightArrow { onRightArrow(); return }
        case Key.upArrow:
            if event.modifierFlags.contains(.command), let onCommandUp { onCommandUp(); return }
            if selectedRow == 0 { onBoundary?(); return }
        case Key.downArrow:
            if event.modifierFlags.contains(.command), let onCommandDown { onCommandDown(); return }
            if numberOfRows > 0, selectedRow == numberOfRows - 1 { onBoundary?(); return }
        default:
            // Plain character keys (no ⌘/⌃/⌥) become single-key shortcuts.
            let modifiers = event.modifierFlags.intersection([.command, .control, .option])
            if modifiers.isEmpty,
               let character = event.charactersIgnoringModifiers?.first,
               let onCharacter, onCharacter(character) {
                return
            }
        }
        super.keyDown(with: event)
    }
}
