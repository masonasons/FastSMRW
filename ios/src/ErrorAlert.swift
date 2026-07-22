//
//  ErrorAlert.swift
//
//  Tiny UIAlertController helpers.
//

import UIKit

@MainActor
func showError(_ message: String, on viewController: UIViewController) {
    let alert = UIAlertController(title: "Error", message: message, preferredStyle: .alert)
    alert.addAction(UIAlertAction(title: "OK", style: .default))
    viewController.present(alert, animated: true)
}

@MainActor
func confirm(_ title: String, message: String, actionTitle: String,
             on viewController: UIViewController, then perform: @escaping () -> Void) {
    let alert = UIAlertController(title: title, message: message, preferredStyle: .alert)
    alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
    alert.addAction(UIAlertAction(title: actionTitle, style: .destructive) { _ in perform() })
    viewController.present(alert, animated: true)
}
