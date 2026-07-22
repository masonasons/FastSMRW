//
//  MediaScreens.swift
//
//  Viewing a post's media: images in a simple zoomable viewer, video/audio in
//  the system player. The core decides what to open (media_open) or offers a
//  chooser (media_picker) when a post has several attachments.
//

import AVKit
import UIKit

@MainActor
enum Media {
    /// Route a media_open event: video/audio/GIF → AVPlayer; images → viewer.
    static func present(_ media: MediaOpen, from presenter: UIViewController) {
        guard let url = URL(string: media.url) else { return }
        switch media.kind {
        case "video", "audio", "gifv", "media":
            let player = AVPlayer(url: url)
            let controller = AVPlayerViewController()
            controller.player = player
            presenter.present(controller, animated: true) { player.play() }
        default:
            let viewer = ImageViewerViewController(url: url, title: media.title)
            let nav = UINavigationController(rootViewController: viewer)
            nav.modalPresentationStyle = .fullScreen
            presenter.present(nav, animated: true)
        }
    }
}

/// A single image, zoomable, with its description (alt text) as the VoiceOver
/// label and shown under the title.
@MainActor
final class ImageViewerViewController: UIViewController, UIScrollViewDelegate {
    private let url: URL
    private let imageTitle: String
    private let scrollView = UIScrollView()
    private let imageView = UIImageView()

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

        let task = URLSession.shared.dataTask(with: url) { [weak self] data, _, _ in
            guard let data, let image = UIImage(data: data) else { return }
            Task { @MainActor in self?.imageView.image = image }
        }
        task.resume()
    }

    func viewForZooming(in scrollView: UIScrollView) -> UIView? { imageView }

    @objc private func done() { dismiss(animated: true) }
}
