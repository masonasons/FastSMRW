//
//  MediaScreens.swift
//
//  Viewing a post's media: images in a simple zoomable viewer, video/audio in
//  a player with Save and Share. The core decides what to open (media_open) or
//  offers a chooser (media_picker) when a post has several attachments.
//

import AVKit
import Photos
import UIKit

@MainActor
enum Media {
    /// Route a media_open event: video/audio/GIF → player; images → viewer.
    static func present(_ media: MediaOpen, from presenter: UIViewController) {
        guard let url = URL(string: media.url) else { return }
        let vc: UIViewController
        switch media.kind {
        case "video", "audio", "gifv", "media":
            vc = MediaPlayerViewController(url: url, kind: media.kind, title: media.title)
        default:
            vc = ImageViewerViewController(url: url, title: media.title)
        }
        let nav = UINavigationController(rootViewController: vc)
        nav.modalPresentationStyle = .fullScreen
        presenter.present(nav, animated: true)
    }
}

/// Save-to-Photos / Share, shared by the image viewer and the media player.
@MainActor
enum MediaActions {
    /// Save a downloaded image to the photo library.
    static func saveImage(_ image: UIImage, on vc: UIViewController) {
        PHPhotoLibrary.shared().performChanges {
            PHAssetChangeRequest.creationRequestForAsset(from: image)
        } completionHandler: { ok, _ in
            Task { @MainActor in announce(ok ? "Image saved to Photos"
                                            : "Couldn't save the image", on: vc) }
        }
    }

    /// Download a video/GIF and save it to the photo library. (Audio can't go to
    /// Photos — the Share sheet's "Save to Files" covers it.)
    static func saveMedia(url: URL, kind: String, on vc: UIViewController) {
        guard kind != "audio" else {
            announce("Audio can't be saved to Photos — use Share to save it to Files.", on: vc)
            return
        }
        announce("Saving…", on: vc)
        download(url) { fileURL in
            guard let fileURL else { announce("Couldn't download the media", on: vc); return }
            PHPhotoLibrary.shared().performChanges {
                PHAssetChangeRequest.creationRequestForAssetFromVideo(atFileURL: fileURL)
            } completionHandler: { ok, _ in
                try? FileManager.default.removeItem(at: fileURL)
                Task { @MainActor in announce(ok ? "Saved to Photos"
                                                : "Couldn't save the media", on: vc) }
            }
        }
    }

    /// Share the media. `image` (when already loaded) shares directly; otherwise
    /// the file is downloaded so the sheet offers Save Video / Save to Files.
    static func share(url: URL, image: UIImage?, from item: UIBarButtonItem,
                      on vc: UIViewController) {
        if let image {
            present([image], from: item, on: vc)
            return
        }
        announce("Preparing…", on: vc)
        download(url) { fileURL in
            present([fileURL ?? url], from: item, on: vc)
        }
    }

    private static func present(_ items: [Any], from item: UIBarButtonItem,
                                on vc: UIViewController) {
        let sheet = UIActivityViewController(activityItems: items, applicationActivities: nil)
        sheet.popoverPresentationController?.barButtonItem = item
        vc.present(sheet, animated: true)
    }

    /// Download to a temp file named after the URL (so the extension/type is
    /// right for Photos and the Share sheet).
    private static func download(_ url: URL, then: @escaping (URL?) -> Void) {
        URLSession.shared.downloadTask(with: url) { temp, _, _ in
            guard let temp else { Task { @MainActor in then(nil) }; return }
            let name = url.lastPathComponent.isEmpty ? "media" : url.lastPathComponent
            let dest = FileManager.default.temporaryDirectory
                .appendingPathComponent(UUID().uuidString + "-" + name)
            try? FileManager.default.moveItem(at: temp, to: dest)
            Task { @MainActor in then(dest) }
        }.resume()
    }

    private static func announce(_ message: String, on vc: UIViewController) {
        UIAccessibility.post(notification: .announcement, argument: message)
    }
}

/// A single image, zoomable, with its description (alt text) as the VoiceOver
/// label. Bar buttons Save it to Photos or Share it.
@MainActor
final class ImageViewerViewController: UIViewController, UIScrollViewDelegate {
    private let url: URL
    private let imageTitle: String
    private let scrollView = UIScrollView()
    private let imageView = UIImageView()
    private var saveButton: UIBarButtonItem!
    private var shareButton: UIBarButtonItem!

    init(url: URL, title: String) {
        self.url = url
        self.imageTitle = title
        super.init(nibName: nil, bundle: nil)
        self.title = title.isEmpty ? "Image" : title
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .done, target: self, action: #selector(done))
        saveButton = UIBarButtonItem(barButtonSystemItem: .save, target: self,
                                     action: #selector(saveTapped))
        shareButton = UIBarButtonItem(barButtonSystemItem: .action, target: self,
                                      action: #selector(shareTapped))
        // Disabled until the image finishes loading.
        saveButton.isEnabled = false
        shareButton.isEnabled = false
        navigationItem.leftBarButtonItems = [shareButton, saveButton]

        scrollView.delegate = self
        scrollView.maximumZoomScale = 5
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        imageView.contentMode = .scaleAspectFit
        imageView.translatesAutoresizingMaskIntoConstraints = false
        imageView.isAccessibilityElement = true
        imageView.accessibilityLabel = imageTitle.isEmpty ? "Image" : imageTitle
        imageView.accessibilityTraits = .image
        scrollView.addSubview(imageView)
        view.addSubview(scrollView)
        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            scrollView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            imageView.widthAnchor.constraint(equalTo: scrollView.frameLayoutGuide.widthAnchor),
            imageView.heightAnchor.constraint(equalTo: scrollView.frameLayoutGuide.heightAnchor),
            imageView.centerXAnchor.constraint(equalTo: scrollView.contentLayoutGuide.centerXAnchor),
            imageView.centerYAnchor.constraint(equalTo: scrollView.contentLayoutGuide.centerYAnchor),
        ])

        URLSession.shared.dataTask(with: url) { [weak self] data, _, _ in
            guard let data, let image = UIImage(data: data) else { return }
            Task { @MainActor in
                self?.imageView.image = image
                self?.saveButton.isEnabled = true
                self?.shareButton.isEnabled = true
            }
        }.resume()
    }

    func viewForZooming(in scrollView: UIScrollView) -> UIView? { imageView }

    @objc private func saveTapped() {
        guard let image = imageView.image else { return }
        MediaActions.saveImage(image, on: self)
    }

    @objc private func shareTapped() {
        MediaActions.share(url: url, image: imageView.image, from: shareButton, on: self)
    }

    @objc private func done() { dismiss(animated: true) }
}

/// Plays a video/audio attachment (AVPlayerViewController embedded so we can add
/// Save and Share around it).
@MainActor
final class MediaPlayerViewController: UIViewController {
    private let url: URL
    private let kind: String
    private let player: AVPlayer
    private let playerVC = AVPlayerViewController()
    private var shareButton: UIBarButtonItem!

    init(url: URL, kind: String, title: String) {
        self.url = url
        self.kind = kind
        self.player = AVPlayer(url: url)
        super.init(nibName: nil, bundle: nil)
        self.title = title.isEmpty ? "Media" : title
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .black
        navigationItem.rightBarButtonItem = UIBarButtonItem(
            barButtonSystemItem: .done, target: self, action: #selector(done))
        shareButton = UIBarButtonItem(barButtonSystemItem: .action, target: self,
                                      action: #selector(shareTapped))
        navigationItem.leftBarButtonItems = [
            shareButton,
            UIBarButtonItem(barButtonSystemItem: .save, target: self,
                            action: #selector(saveTapped)),
        ]

        playerVC.player = player
        addChild(playerVC)
        playerVC.view.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(playerVC.view)
        NSLayoutConstraint.activate([
            playerVC.view.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            playerVC.view.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            playerVC.view.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            playerVC.view.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
        playerVC.didMove(toParent: self)
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        player.play()
    }

    @objc private func saveTapped() { MediaActions.saveMedia(url: url, kind: kind, on: self) }
    @objc private func shareTapped() {
        MediaActions.share(url: url, image: nil, from: shareButton, on: self)
    }

    @objc private func done() { player.pause(); dismiss(animated: true) }
}
