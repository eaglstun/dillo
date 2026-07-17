import UIKit

/// UIView whose contents are painted entirely by the Dillo engine.
final class DilloCanvasView: UIView {
    let bridge: SpikeBridge

    init(bridge: SpikeBridge) {
        self.bridge = bridge
        super.init(frame: .zero)
        isOpaque = true
        contentMode = .redraw
        bridge.onInvalidate = { [weak self] in
            self?.setNeedsDisplay()
        }
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func draw(_ rect: CGRect) {
        guard let ctx = UIGraphicsGetCurrentContext() else { return }
        bridge.draw(into: ctx)
    }
}

/// Hosts the engine canvas. The fixed narrow width (200 pt, same as the
/// desktop dw_example window) forces obvious word wrapping. The scroll view
/// is plain UIKit chrome — the engine draws its whole canvas and does no
/// scrolling of its own (View::usesViewport() is false).
final class SpikeViewController: UIViewController {
    private static let canvasWidth = 200

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemGray5

        let bridge = SpikeBridge(
            viewportWidth: Int32(Self.canvasWidth),
            height: Int32(view.bounds.height)
        )

        let canvas = DilloCanvasView(bridge: bridge)
        canvas.frame = CGRect(
            x: 0, y: 0,
            width: Int(bridge.canvasWidth),
            height: Int(bridge.canvasHeight)
        )

        let scrollView = UIScrollView(frame: .zero)
        scrollView.addSubview(canvas)
        scrollView.contentSize = canvas.frame.size
        view.addSubview(scrollView)

        scrollView.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(
                equalTo: view.safeAreaLayoutGuide.topAnchor),
            scrollView.leadingAnchor.constraint(
                equalTo: view.safeAreaLayoutGuide.leadingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            scrollView.widthAnchor.constraint(
                equalToConstant: CGFloat(bridge.canvasWidth)),
        ])
    }
}
