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
    private let findButton = UIButton(type: .system)
    private let spinner = UIActivityIndicatorView(style: .medium)
    private let titleLabel = UILabel()
    private let scrollView = UIScrollView()
    private let renderView = RenderView(frame: .zero)
    private var contextMenuInfo: DilloContextInfo?
    private var isSelectingText = false
    private lazy var editMenuInteraction = UIEditMenuInteraction(
        delegate: self)

    private var zoomFactor: CGFloat = 1
    private var pinchStartZoom: CGFloat = 1
    private var pinchStartCanvasPoint = CGPoint.zero
    private var pinchViewportPoint = CGPoint.zero
    private var pinchPreviewClip: UIView?
    private var pinchPreviewImage: UIImageView?
    /// How much larger than the viewport the current preview was painted.
    private var pinchOverdraw: CGFloat = 1
    /// Gesture range. The bridge clamps to the wider desktop limits itself.
    private let minZoom: CGFloat = 0.5
    private let maxZoom: CGFloat = 3
    /// How much wider/taller than the viewport the pinch preview is painted.
    /// Bounds the cost of the one oversized repaint at gesture start.
    private let maxPreviewOverdraw: CGFloat = 2.5

    private let findBar = UIStackView()
    private let findField = UITextField()
    private let findStatus = UILabel()

    /// One navigation: URL plus, for form POSTs, the request body.
    private struct HistoryEntry {
        let url: String
        let method: String
        let body: Data?
        let bodyContentType: String?

        init(url: String, method: String = "GET", body: Data? = nil,
             bodyContentType: String? = nil) {
            self.url = url
            self.method = method
            self.body = body
            self.bodyContentType = bodyContentType
        }
    }

    /// Simple app-side history (the desktop nav.c depends on the cache).
    private var history: [HistoryEntry] = []
    private var historyIndex = -1

    private let homePage = "https://dillo-browser.github.io/"
    private let initialURL: URL?

    init(initialURL: URL? = nil) {
        self.initialURL = initialURL
        super.init(nibName: nil, bundle: nil)
    }

    required init?(coder: NSCoder) {
        fatalError("not used")
    }

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
        zoomFactor = CGFloat(bridge.zoomFactor)

        bridge.onInvalidate = { [weak self] in
            self?.canvasChanged()
        }
        bridge.onTitle = { [weak self] title in
            self?.titleLabel.text = title
        }
        bridge.onOpenURL = { [weak self] url, body, contentType in
            guard let self = self else { return }
            // Same-document #fragment: scroll instead of reloading.
            if body == nil, self.bridge.navigateFragment(url) {
                self.addressField.text = url
                return
            }
            self.load(HistoryEntry(
                url: url, method: body != nil ? "POST" : "GET",
                body: body, bodyContentType: contentType),
                recordHistory: true)
        }
        bridge.onScrollTo = { [weak self] origin in
            guard let self = self else { return }
            let maxY = max(0, self.scrollView.contentSize.height
                - self.scrollView.bounds.height)
            let maxX = max(0, self.scrollView.contentSize.width
                - self.scrollView.bounds.width)
            self.scrollView.setContentOffset(
                CGPoint(x: min(max(0, origin.x), maxX),
                        y: min(max(0, origin.y), maxY)),
                animated: true)
        }
        bridge.onCopy = { [weak self] text in
            self?.flashStatus("Copied: \(text.prefix(60))")
        }
        bridge.setFormHost(scrollView)

        load(HistoryEntry(url: initialURL?.absoluteString ?? homePage),
             recordHistory: true)

        NotificationCenter.default.addObserver(
            self, selector: #selector(keyboardChanged(_:)),
            name: UIResponder.keyboardWillChangeFrameNotification,
            object: nil)
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

        findButton.setImage(
            UIImage(systemName: "magnifyingglass"), for: .normal)
        findButton.addTarget(
            self, action: #selector(toggleFindBar), for: .touchUpInside)

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
            [backButton, addressField, spinner, findButton, reloadButton])
        bar.axis = .horizontal
        bar.spacing = 8
        bar.alignment = .center

        buildFindBar()

        let chrome = UIStackView(arrangedSubviews:
            [bar, findBar, titleLabel])
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
        scrollView.keyboardDismissMode = .interactive
        scrollView.addInteraction(editMenuInteraction)

        let tap = UITapGestureRecognizer(
            target: self, action: #selector(handleTap(_:)))
        tap.delegate = self
        scrollView.addGestureRecognizer(tap)

        let longPress = UILongPressGestureRecognizer(
            target: self, action: #selector(handleLongPress(_:)))
        longPress.delegate = self
        scrollView.addGestureRecognizer(longPress)

        let pinch = UIPinchGestureRecognizer(
            target: self, action: #selector(handlePinch(_:)))
        pinch.delegate = self
        scrollView.addGestureRecognizer(pinch)

        let edgeSwipe = UIScreenEdgePanGestureRecognizer(
            target: self, action: #selector(handleEdgeSwipe(_:)))
        edgeSwipe.edges = .left
        view.addGestureRecognizer(edgeSwipe)
    }

    private func buildFindBar() {
        findField.borderStyle = .roundedRect
        findField.font = .systemFont(ofSize: 14)
        findField.autocapitalizationType = .none
        findField.autocorrectionType = .no
        findField.placeholder = "Find in page"
        findField.returnKeyType = .search
        findField.delegate = self

        findStatus.font = .systemFont(ofSize: 11)
        findStatus.textColor = .secondaryLabel

        let prev = UIButton(type: .system)
        prev.setImage(UIImage(systemName: "chevron.up"), for: .normal)
        prev.addTarget(self, action: #selector(findPrev),
                       for: .touchUpInside)
        let next = UIButton(type: .system)
        next.setImage(UIImage(systemName: "chevron.down"), for: .normal)
        next.addTarget(self, action: #selector(findNextMatch),
                       for: .touchUpInside)
        let done = UIButton(type: .system)
        done.setTitle("Done", for: .normal)
        done.addTarget(self, action: #selector(toggleFindBar),
                       for: .touchUpInside)

        findBar.axis = .horizontal
        findBar.spacing = 8
        findBar.alignment = .center
        [findField, findStatus, prev, next, done].forEach {
            findBar.addArrangedSubview($0)
        }
        findBar.isHidden = true
    }

    // MARK: - Find in page

    @objc private func toggleFindBar() {
        findBar.isHidden.toggle()
        if findBar.isHidden {
            bridge.resetFind()
            findStatus.text = ""
            findField.resignFirstResponder()
        } else {
            findField.becomeFirstResponder()
        }
    }

    private func find(backwards: Bool) {
        guard let key = findField.text, !key.isEmpty else { return }
        let result = bridge.findText(
            key, caseSensitive: false, backwards: backwards)
        switch result {
        case 0: findStatus.text = ""
        case 1: findStatus.text = "wrapped"
        default: findStatus.text = "not found"
        }
    }

    @objc private func findNextMatch() { find(backwards: false) }
    @objc private func findPrev() { find(backwards: true) }

    // MARK: - Gestures

    @objc private func handleLongPress(_ gr: UILongPressGestureRecognizer) {
        let p = gr.location(in: scrollView)
        switch gr.state {
        case .began:
            if let context = bridge.contextInfo(
                x: Int32(p.x), y: Int32(p.y)) {
                contextMenuInfo = context
                isSelectingText = false
                let configuration = UIEditMenuConfiguration(
                    identifier: nil, sourcePoint: p)
                editMenuInteraction.presentEditMenu(with: configuration)
                return
            }
            // The drag extends the selection; it must not pan the page.
            isSelectingText = true
            scrollView.isScrollEnabled = false
            bridge.selectionBegan(x: Int32(p.x), y: Int32(p.y))
        case .changed:
            guard isSelectingText else { return }
            bridge.selectionMoved(x: Int32(p.x), y: Int32(p.y))
        case .ended, .cancelled:
            guard isSelectingText else { return }
            bridge.selectionEnded(x: Int32(p.x), y: Int32(p.y))
            isSelectingText = false
            scrollView.isScrollEnabled = true
        default:
            break
        }
    }

    @objc private func handleEdgeSwipe(_ gr: UIScreenEdgePanGestureRecognizer)
    {
        if gr.state == .ended {
            goBack()
        }
    }

    /// Pinch zoom in two halves: a continuous transform of a pre-painted
    /// preview for as long as the fingers are down, then exactly one reflow
    /// when they lift.
    ///
    /// Rebuilding the Dillo widget tree is far more work than scaling an
    /// already-painted layer, so reflowing per gesture update forced coarse
    /// steps to stay responsive. Previewing with a transform instead keeps
    /// the gesture continuous, costs one reparse per gesture rather than one
    /// per step, and — since re-parsing drops transient form values — loses
    /// those at most once. The trade is that the preview is bitmap-scaled,
    /// so text is soft until the fingers lift.
    @objc private func handlePinch(_ gr: UIPinchGestureRecognizer) {
        switch gr.state {
        case .began:
            pinchStartZoom = zoomFactor
            pinchViewportPoint = gr.location(in: renderView)
            pinchStartCanvasPoint = CGPoint(
                x: scrollView.contentOffset.x + pinchViewportPoint.x,
                y: scrollView.contentOffset.y + pinchViewportPoint.y)
            // The render view is viewport-sized and paints the slice at the
            // current offset, so scrolling underneath a scaled preview would
            // repaint content that no longer lines up with it.
            scrollView.isScrollEnabled = false
            beginPinchPreview()

        case .changed:
            if let preview = pinchPreviewImage {
                // Never shrink the preview past its own painted edge — that
                // is what would expose a gutter. Where the overdraw wasn't
                // capped this clamp is exactly the reachable zoom limit and
                // so never bites; past the cap the preview saturates while
                // the fingers keep going, and release resolves it.
                let scale = max(pinchScale(gr), 1 / pinchOverdraw)
                preview.transform = pinchTransform(
                    scale, about: preview.center)
            }

        case .ended:
            let wanted = pinchStartZoom * pinchScale(gr)
            if abs(wanted - zoomFactor) >= 0.01 {
                zoomFactor = CGFloat(bridge.applyZoom(Float(wanted)))

                // Keep approximately the same canvas point under the fingers.
                // Reflow can move individual lines, but this avoids jumping to
                // the top-left as the newly sized canvas replaces the old one.
                let relativeScale = zoomFactor / pinchStartZoom
                setClampedContentOffset(CGPoint(
                    x: pinchStartCanvasPoint.x * relativeScale
                        - pinchViewportPoint.x,
                    y: pinchStartCanvasPoint.y * relativeScale
                        - pinchViewportPoint.y))
            }
            // Torn down only after the reflow, so the preview covers it and
            // the crisp page is what appears when it goes.
            endPinchPreview()
            flashStatus("Zoom: \(Int(zoomFactor * 100))%")

        case .cancelled, .failed:
            endPinchPreview()

        default:
            break
        }
    }

    /// The zoom the fingers are asking for, as a multiple of the zoom that
    /// was in effect when they landed.
    private func pinchScale(_ gr: UIPinchGestureRecognizer) -> CGFloat {
        let proposed = min(maxZoom, max(minZoom, pinchStartZoom * gr.scale))
        return proposed / pinchStartZoom
    }

    /// Scale about the point the fingers landed on, so that point holds still
    /// instead of the view growing from its own centre.
    private func pinchTransform(
        _ scale: CGFloat, about centre: CGPoint
    ) -> CGAffineTransform {
        return CGAffineTransform(
            translationX: (1 - scale) * (pinchViewportPoint.x - centre.x),
            y: (1 - scale) * (pinchViewportPoint.y - centre.y))
            .scaledBy(x: scale, y: scale)
    }

    /// Paint a slice *larger* than the viewport into an image and scale that,
    /// rather than the render view itself.
    ///
    /// The render view only ever paints the visible slice, so scaling it down
    /// cropped the page to whatever happened to be on screen when the fingers
    /// landed — there was nothing beyond its edges to reveal. Overdrawing at
    /// gesture start gives zooming out real content to expose. The margin is
    /// capped so the repaint stays affordable; a pinch that starts deeply
    /// zoomed in can out-run it, which `.changed` absorbs by clamping the
    /// preview rather than letting the page ground show through.
    private func beginPinchPreview() {
        let viewport = renderView.bounds
        guard viewport.width > 0, viewport.height > 0 else { return }

        // Only zooming out needs the margin, and only as far as this gesture
        // could actually zoom out from where it started.
        let overdraw = min(maxPreviewOverdraw, max(1, pinchStartZoom / minZoom))
        pinchOverdraw = overdraw
        let margin = CGSize(
            width: viewport.width * (overdraw - 1) / 2,
            height: viewport.height * (overdraw - 1) / 2)
        let slice = CGRect(
            x: scrollView.contentOffset.x - margin.width,
            y: scrollView.contentOffset.y - margin.height,
            width: viewport.width * overdraw,
            height: viewport.height * overdraw)

        let format = UIGraphicsImageRendererFormat.preferred()
        // The preview is bitmap-scaled and short-lived; full device scale
        // would multiply the cost of the oversized repaint for no real gain.
        format.scale = min(2, traitCollection.displayScale)
        format.opaque = true
        let ground = renderView.backgroundColor ?? .white
        let image = UIGraphicsImageRenderer(size: slice.size, format: format)
            .image { context in
                ground.setFill()
                context.fill(CGRect(origin: .zero, size: slice.size))
                self.bridge.draw(into: context.cgContext, visibleRect: slice)
            }

        // Clipped to exactly the render area so the oversized image cannot
        // spill over the chrome, and non-interactive so the scroll view keeps
        // receiving the gesture.
        let clip = UIView(frame: renderView.frame)
        clip.clipsToBounds = true
        clip.isUserInteractionEnabled = false
        clip.backgroundColor = ground

        let preview = UIImageView(image: image)
        preview.frame = CGRect(
            origin: CGPoint(x: -margin.width, y: -margin.height),
            size: slice.size)
        clip.addSubview(preview)
        view.addSubview(clip)

        // Otherwise the crisp, un-zoomed render view shows through around the
        // preview as it shrinks.
        renderView.isHidden = true
        pinchPreviewClip = clip
        pinchPreviewImage = preview
    }

    private func endPinchPreview() {
        pinchPreviewClip?.removeFromSuperview()
        pinchPreviewClip = nil
        pinchPreviewImage = nil
        pinchOverdraw = 1
        renderView.isHidden = false
        scrollView.isScrollEnabled = true
    }

    private func setClampedContentOffset(_ proposed: CGPoint) {
        let maxX = max(0, scrollView.contentSize.width
            - scrollView.bounds.width)
        let maxY = max(0, scrollView.contentSize.height
            - scrollView.bounds.height)
        scrollView.setContentOffset(CGPoint(
            x: min(max(0, proposed.x), maxX),
            y: min(max(0, proposed.y), maxY)), animated: false)
    }

    private func flashStatus(_ text: String) {
        titleLabel.text = text
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.5) {
            [weak self] in
            self?.titleLabel.text = ""
        }
    }

    @objc private func keyboardChanged(_ note: Notification) {
        guard let frame = note.userInfo?[
            UIResponder.keyboardFrameEndUserInfoKey] as? CGRect
        else { return }
        let overlap = max(
            0, scrollView.bounds.maxY
                - scrollView.convert(frame, from: nil).minY)
        scrollView.contentInset.bottom = overlap
        scrollView.verticalScrollIndicatorInsets.bottom = overlap
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
        if trimmed.hasPrefix("data:") || trimmed.contains("://") {
            return trimmed
        }
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
        load(HistoryEntry(url: url), recordHistory: true)
    }

    func openAutomationURL(_ url: URL) {
        loadViewIfNeeded()
        navigate(to: url.absoluteString)
    }

    private func load(_ entry: HistoryEntry, recordHistory: Bool) {
        spinner.startAnimating()
        addressField.text = entry.url
        view.endEditing(true)
        loader.load(entry.url, method: entry.method, body: entry.body,
                    bodyContentType: entry.bodyContentType) {
            [weak self] result in
            guard let self = self else { return }
            self.spinner.stopAnimating()
            switch result {
            case .success(let page):
                if recordHistory {
                    self.history.removeSubrange(
                        (self.historyIndex + 1)..<self.history.count)
                    self.history.append(HistoryEntry(
                        url: page.finalURL, method: entry.method,
                        body: entry.body,
                        bodyContentType: entry.bodyContentType))
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
                self.showErrorPage(error, for: entry.url)
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
        <html><head><title>Error</title>
        <style>
          body { background-color: #ffffff; margin: 24px; }
          .card { background-color: #fff4f2; padding: 12px;
                  margin-top: 40px; }
          h1 { color: #a33021; font-size: 1.4em; margin: 0 0 8px 0; }
          .url { color: #666666; word-wrap: break-word; }
          .msg { margin-top: 10px; }
        </style></head><body>
        <div class="card">
        <h1>Can&#39;t load this page</h1>
        <p class="url">\(esc(urlString))</p>
        <p class="msg">\(esc(error.localizedDescription))</p>
        </div>
        <p><a href="\(esc(urlString))">Try again</a></p>
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
        // Keep the engine's scroll position in sync (scroll-into-view
        // calculations depend on it).
        bridge.reportScroll(x:
            Int32(scrollView.contentOffset.x),
            y: Int32(scrollView.contentOffset.y))
    }
}

extension BrowserViewController: UIGestureRecognizerDelegate {
    /// Form controls (subviews of the scroll view) handle their own
    /// touches; the link-tap recognizer only takes touches that land on
    /// the bare scroll view (i.e. the rendered page).
    func gestureRecognizer(
        _ gestureRecognizer: UIGestureRecognizer,
        shouldReceive touch: UITouch
    ) -> Bool {
        return touch.view === scrollView
    }

    func gestureRecognizer(
        _ gestureRecognizer: UIGestureRecognizer,
        shouldRecognizeSimultaneouslyWith otherGestureRecognizer:
            UIGestureRecognizer
    ) -> Bool {
        // A second finger landing mid-scroll must still start a pinch; the
        // pinch handler is what actually parks scrolling while it previews.
        return gestureRecognizer is UIPinchGestureRecognizer
            || otherGestureRecognizer is UIPinchGestureRecognizer
    }
}

extension BrowserViewController: UIEditMenuInteractionDelegate {
    func editMenuInteraction(
        _ interaction: UIEditMenuInteraction,
        menuFor configuration: UIEditMenuConfiguration,
        suggestedActions: [UIMenuElement]
    ) -> UIMenu? {
        guard let context = contextMenuInfo else { return nil }
        var actions: [UIMenuElement] = []

        func openAction(_ title: String, _ url: String) -> UIAction {
            UIAction(title: title, image: UIImage(systemName: "safari")) {
                [weak self] _ in
                self?.navigate(to: url)
            }
        }

        func copyAction(_ title: String, _ url: String) -> UIAction {
            UIAction(title: title, image: UIImage(systemName: "doc.on.doc")) {
                [weak self] _ in
                UIPasteboard.general.string = url
                self?.flashStatus("Copied address")
            }
        }

        switch context.kind {
        case .link:
            actions.append(openAction("Open Link", context.url))
            actions.append(copyAction("Copy Link", context.url))
        case .image:
            actions.append(openAction("Open Image", context.url))
            actions.append(copyAction("Copy Image Address", context.url))
            if let linkURL = context.linkURL {
                actions.append(UIMenu(options: .displayInline, children: [
                    openAction("Open Link", linkURL),
                    copyAction("Copy Link", linkURL),
                ]))
            }
        @unknown default:
            return nil
        }
        return UIMenu(children: actions)
    }

    func editMenuInteraction(
        _ interaction: UIEditMenuInteraction,
        targetRectFor configuration: UIEditMenuConfiguration
    ) -> CGRect {
        CGRect(origin: configuration.sourcePoint, size: CGSize(width: 1, height: 1))
    }

    func editMenuInteraction(
        _ interaction: UIEditMenuInteraction,
        willDismissMenuFor configuration: UIEditMenuConfiguration,
        animator: any UIEditMenuInteractionAnimating
    ) {
        contextMenuInfo = nil
    }
}

extension BrowserViewController: UITextFieldDelegate {
    func textFieldShouldReturn(_ textField: UITextField) -> Bool {
        if textField === findField {
            find(backwards: false)
            return true
        }
        textField.resignFirstResponder()
        if let text = textField.text, !text.isEmpty {
            navigate(to: normalizeInput(text))
        }
        return true
    }
}
