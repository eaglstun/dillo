import UIKit

/// Screen-sized view that paints the visible slice of the engine canvas.
/// Scrolling never grows this view — the draw call translates by the
/// scroll offset — so tall pages cost no extra backing store.
final class RenderView: UIView {
    var bridge: DilloBridge?
    /// Canvas-space origin of the visible slice (scroll offset).
    var canvasOrigin: CGPoint = .zero {
        didSet { setNeedsDisplay() }
    }

    override init(frame: CGRect) {
        super.init(frame: frame)
        isOpaque = true
        backgroundColor = .white
        contentMode = .redraw
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext(),
              let bridge = bridge else { return }
        let visible = CGRect(
            x: canvasOrigin.x, y: canvasOrigin.y,
            width: bounds.width, height: bounds.height)
        bridge.draw(into: ctx, visibleRect: visible)
    }
}

/// The browser window: address bar + back/reload on top, engine canvas
/// below, with a UIScrollView supplying native scrolling physics.
final class BrowserViewController: UIViewController {
    private var bridge: DilloBridge!
    private var loader: PageLoader!

    private let addressField = UITextField()
    private let backButton = UIButton(type: .system)
    private let reloadButton = UIButton(type: .system)
    private let spinner = UIActivityIndicatorView(style: .medium)
    private let titleLabel = UILabel()
    private let scrollView = UIScrollView()
    private let renderView = RenderView(frame: .zero)

    /// Simple app-side history (the desktop nav.c depends on the cache).
    private var history: [String] = []
    private var historyIndex = -1

    private let homePage = "https://dillo-browser.github.io/"

    // MARK: - UI construction

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground

        buildChrome()

        bridge = DilloBridge(
            viewportWidth: Int32(view.bounds.width),
            height: Int32(view.bounds.height))
        loader = PageLoader(resolver: { [weak self] href, base in
            self?.bridge.resolveURL(href, baseURL: base)
        })
        renderView.bridge = bridge

        bridge.onInvalidate = { [weak self] in
            self?.canvasChanged()
        }
        bridge.onTitle = { [weak self] title in
            self?.titleLabel.text = title
        }
        bridge.onOpenURL = { [weak self] url in
            self?.navigate(to: url)
        }

        load(homePage, recordHistory: true)
    }

    private func buildChrome() {
        backButton.setImage(
            UIImage(systemName: "chevron.backward"), for: .normal)
        backButton.addTarget(
            self, action: #selector(goBack), for: .touchUpInside)

        reloadButton.setImage(
            UIImage(systemName: "arrow.clockwise"), for: .normal)
        reloadButton.addTarget(
            self, action: #selector(reload), for: .touchUpInside)

        addressField.borderStyle = .roundedRect
        addressField.font = .systemFont(ofSize: 14)
        addressField.autocapitalizationType = .none
        addressField.autocorrectionType = .no
        addressField.keyboardType = .URL
        addressField.returnKeyType = .go
        addressField.clearButtonMode = .whileEditing
        addressField.placeholder = "example.org"
        addressField.delegate = self

        titleLabel.font = .systemFont(ofSize: 11)
        titleLabel.textColor = .secondaryLabel
        titleLabel.textAlignment = .center
        titleLabel.lineBreakMode = .byTruncatingTail

        let bar = UIStackView(arrangedSubviews:
            [backButton, addressField, spinner, reloadButton])
        bar.axis = .horizontal
        bar.spacing = 8
        bar.alignment = .center

        let chrome = UIStackView(arrangedSubviews: [bar, titleLabel])
        chrome.axis = .vertical
        chrome.spacing = 2

        view.addSubview(chrome)
        view.addSubview(renderView)
        view.addSubview(scrollView)

        chrome.translatesAutoresizingMaskIntoConstraints = false
        renderView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.translatesAutoresizingMaskIntoConstraints = false

        NSLayoutConstraint.activate([
            chrome.topAnchor.constraint(
                equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 4),
            chrome.leadingAnchor.constraint(
                equalTo: view.safeAreaLayoutGuide.leadingAnchor, constant: 8),
            chrome.trailingAnchor.constraint(
                equalTo: view.safeAreaLayoutGuide.trailingAnchor,
                constant: -8),

            renderView.topAnchor.constraint(
                equalTo: chrome.bottomAnchor, constant: 4),
            renderView.leadingAnchor.constraint(
                equalTo: view.leadingAnchor),
            renderView.trailingAnchor.constraint(
                equalTo: view.trailingAnchor),
            renderView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
        // The scroll view exactly overlays the render view; it draws
        // nothing itself, it just owns the pan/deceleration physics.
        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: renderView.topAnchor),
            scrollView.leadingAnchor.constraint(
                equalTo: renderView.leadingAnchor),
            scrollView.trailingAnchor.constraint(
                equalTo: renderView.trailingAnchor),
            scrollView.bottomAnchor.constraint(
                equalTo: renderView.bottomAnchor),
        ])
        scrollView.delegate = self
        scrollView.backgroundColor = .clear

        let tap = UITapGestureRecognizer(
            target: self, action: #selector(handleTap(_:)))
        scrollView.addGestureRecognizer(tap)
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        // Reflow on size changes (first layout, rotation).
        let w = Int32(renderView.bounds.width)
        let h = Int32(renderView.bounds.height)
        if w > 0, bridge != nil {
            bridge.setViewportWidth(w, height: h)
        }
    }

    // MARK: - Navigation

    private func normalizeInput(_ text: String) -> String {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.contains("://") { return trimmed }
        return "https://" + trimmed
    }

    @objc private func reload() {
        guard historyIndex >= 0 else { return }
        load(history[historyIndex], recordHistory: false)
    }

    @objc private func goBack() {
        guard historyIndex > 0 else { return }
        historyIndex -= 1
        load(history[historyIndex], recordHistory: false)
    }

    private func navigate(to url: String) {
        load(url, recordHistory: true)
    }

    private func load(_ urlString: String, recordHistory: Bool) {
        spinner.startAnimating()
        addressField.text = urlString
        loader.load(urlString) { [weak self] result in
            guard let self = self else { return }
            self.spinner.stopAnimating()
            switch result {
            case .success(let page):
                if recordHistory {
                    self.history.removeSubrange(
                        (self.historyIndex + 1)..<self.history.count)
                    self.history.append(page.finalURL)
                    self.historyIndex = self.history.count - 1
                }
                self.addressField.text = page.finalURL
                self.titleLabel.text = ""
                self.bridge.clearCache()
                for (url, css) in page.stylesheets {
                    self.bridge.seedCSS(css, forURL: url)
                }
                self.bridge.loadHTML(page.html, url: page.finalURL)
                self.scrollView.setContentOffset(.zero, animated: false)
            case .failure(let error):
                self.showErrorPage(error, for: urlString)
            }
            self.backButton.isEnabled = self.historyIndex > 0
        }
    }

    private func showErrorPage(_ error: Error, for urlString: String) {
        func esc(_ s: String) -> String {
            s.replacingOccurrences(of: "&", with: "&amp;")
                .replacingOccurrences(of: "<", with: "&lt;")
                .replacingOccurrences(of: ">", with: "&gt;")
        }
        let html = """
        <html><head><title>Error</title></head><body>
        <h1>Can't load page</h1>
        <p><b>\(esc(urlString))</b></p>
        <p>\(esc(error.localizedDescription))</p>
        </body></html>
        """
        bridge.clearCache()
        bridge.loadHTML(html, url: "about:error")
        scrollView.setContentOffset(.zero, animated: false)
    }

    // MARK: - Canvas plumbing

    private func canvasChanged() {
        scrollView.contentSize = CGSize(
            width: CGFloat(bridge.canvasWidth),
            height: CGFloat(bridge.canvasHeight))
        renderView.setNeedsDisplay()
    }

    @objc private func handleTap(_ gr: UITapGestureRecognizer) {
        if addressField.isFirstResponder {
            addressField.resignFirstResponder()
            return
        }
        // A scroll view's bounds origin is its content offset, so this
        // location is already in canvas coordinates.
        let p = gr.location(in: scrollView)
        bridge.tapAt(x: Int32(p.x), y: Int32(p.y))
    }
}

extension BrowserViewController: UIScrollViewDelegate {
    func scrollViewDidScroll(_ scrollView: UIScrollView) {
        renderView.canvasOrigin = scrollView.contentOffset
    }
}

extension BrowserViewController: UITextFieldDelegate {
    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        textField.resignFirstResponder()
        if let text = textField.text, !text.isEmpty {
            navigate(to: normalizeInput(text))
        }
        return true
    }
}
