import UIKit
import XCTest
@testable import Dillo

/// Scaled images must be resampled at device resolution, not drawn as a
/// point-sized copy blown up into solid 3x3 blocks on a 3x screen.
@MainActor
final class ImageSmoothingTests: XCTestCase {
    private var dir: URL!

    override func setUp() async throws {
        dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("dillo-img-\(UUID().uuidString)")
        try FileManager.default.createDirectory(
            at: dir, withIntermediateDirectories: true)
    }

    override func tearDown() async throws {
        try? FileManager.default.removeItem(at: dir)
    }

    /// A 600x600 smooth diagonal gradient: every device pixel of a good
    /// downscale differs from its neighbours.
    private func writeGradientPNG(to url: URL) throws {
        let format = UIGraphicsImageRendererFormat()
        format.scale = 1
        let image = UIGraphicsImageRenderer(
            size: CGSize(width: 600, height: 600), format: format
        ).image { ctx in
            let colors = [UIColor.black.cgColor, UIColor.white.cgColor]
            let gradient = CGGradient(
                colorsSpace: CGColorSpaceCreateDeviceRGB(),
                colors: colors as CFArray, locations: [0, 1])!
            ctx.cgContext.drawLinearGradient(
                gradient, start: .zero, end: CGPoint(x: 600, y: 120),
                options: [])
        }
        try XCTUnwrap(image.pngData()).write(to: url)
    }

    /// Fraction of aligned 3x3 cells inside rect (in 3x pixels) whose nine
    /// pixels are identical. ~1 means stretched blocks.
    private func uniformCellRatio(_ cg: CGImage, _ rect: CGRect) -> Double {
        let w = cg.width, h = cg.height
        var px = [UInt8](repeating: 0, count: w * h * 4)
        let ctx = CGContext(
            data: &px, width: w, height: h, bitsPerComponent: 8,
            bytesPerRow: w * 4, space: CGColorSpaceCreateDeviceRGB(),
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        ctx.draw(cg, in: CGRect(x: 0, y: 0, width: w, height: h))
        func gray(_ x: Int, _ y: Int) -> Int {
            let i = (y * w + x) * 4   // context rows are already top-down
            return Int(px[i]) + Int(px[i + 1]) + Int(px[i + 2])
        }
        var cells = 0, uniform = 0
        var y = Int(rect.minY)
        while y + 3 <= Int(rect.maxY) {
            var x = Int(rect.minX)
            while x + 3 <= Int(rect.maxX) {
                let v = gray(x, y)
                var same = true
                for dy in 0..<3 { for dx in 0..<3 where gray(x + dx, y + dy) != v {
                    same = false
                } }
                cells += 1
                if same { uniform += 1 }
                x += 3
            }
            y += 3
        }
        return cells == 0 ? 1 : Double(uniform) / Double(cells)
    }

    private func snapshot(_ view: UIView) -> CGImage {
        let format = UIGraphicsImageRendererFormat()
        format.scale = 3
        format.opaque = true
        return UIGraphicsImageRenderer(bounds: view.bounds, format: format)
            .image { view.layer.render(in: $0.cgContext) }.cgImage!
    }

    /// Red channel of the pixel at (x, y) of a 3x snapshot.
    private func red(_ cg: CGImage, _ x: Int, _ y: Int) -> UInt8 {
        let probe = cg.cropping(to: CGRect(x: x, y: y, width: 1, height: 1))!
        var p = [UInt8](repeating: 0, count: 4)
        let pc = CGContext(data: &p, width: 1, height: 1, bitsPerComponent: 8,
                           bytesPerRow: 4, space: CGColorSpaceCreateDeviceRGB(),
                           bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        pc.draw(probe, in: CGRect(x: 0, y: 0, width: 1, height: 1))
        return p[0]
    }

    /// Open a page showing grad.png at 100pt and wait until it is drawn.
    private func openGradientPage() async throws -> BrowserViewController {
        try writeGradientPNG(to: dir.appendingPathComponent("grad.png"))
        let page = dir.appendingPathComponent("img.html")
        try """
        <html><body style="margin:0"><img src="grad.png" width="100" height="100"></body></html>
        """.write(to: page, atomically: true, encoding: .utf8)

        let browser = try XCTUnwrap(DilloAutomation.shared.currentBrowser)
        let opened = expectation(description: "page loaded")
        browser.openAutomationURL(page) { _ in opened.fulfill() }
        await fulfillment(of: [opened], timeout: 20)
        for _ in 0..<50 {
            try await Task.sleep(nanoseconds: 100_000_000)
            if red(snapshot(browser.renderView), 30, 30) < 200 { return browser }
        }
        XCTFail("image never appeared")
        return browser
    }

    /// Zooming re-parses the page; images must come from the page's
    /// downloaded bytes, not be fetched again (here the file is gone, so a
    /// re-fetch would fail and leave the grey placeholder).
    func testZoomDoesNotRefetchImages() async throws {
        let browser = try await openGradientPage()
        try FileManager.default.removeItem(
            at: dir.appendingPathComponent("grad.png"))

        let zoom = browser.bridge.zoomFactor
        _ = browser.bridge.applyZoom(zoom * 1.5)
        defer { _ = browser.bridge.applyZoom(zoom) }

        // No waiting: the very first frame after the zoom must have it.
        browser.renderView.setNeedsDisplay()
        XCTAssertLessThan(red(snapshot(browser.renderView), 30, 30), 200,
                          "image shows the placeholder after zoom")
    }

    func testScaledImageIsNotBlocky() async throws {
        try writeGradientPNG(to: dir.appendingPathComponent("grad.png"))
        let page = dir.appendingPathComponent("img.html")
        try """
        <html><body style="margin:0"><img src="grad.png" width="100" height="100"></body></html>
        """.write(to: page, atomically: true, encoding: .utf8)

        let browser = try XCTUnwrap(DilloAutomation.shared.currentBrowser)
        let opened = expectation(description: "page loaded")
        browser.openAutomationURL(page) { _ in opened.fulfill() }
        await fulfillment(of: [opened], timeout: 20)

        // The image itself decodes asynchronously: wait until its middle
        // stops being the grey placeholder.
        let view = browser.renderView
        let imageRect = CGRect(x: 0, y: 0, width: 300, height: 300)  // 100pt @3x
        var ratio = 1.0
        for _ in 0..<50 {
            try await Task.sleep(nanoseconds: 100_000_000)
            view.setNeedsDisplay()
            view.layoutIfNeeded()
            let shot = snapshot(view)
            let probe = shot.cropping(to: CGRect(x: 30, y: 30, width: 1, height: 1))!
            var p = [UInt8](repeating: 0, count: 4)
            let pc = CGContext(data: &p, width: 1, height: 1, bitsPerComponent: 8,
                               bytesPerRow: 4, space: CGColorSpaceCreateDeviceRGB(),
                               bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            pc.draw(probe, in: CGRect(x: 0, y: 0, width: 1, height: 1))
            if p[0] < 200 {   // gradient is dark at top-left; placeholder is 222
                ratio = uniformCellRatio(shot, imageRect)
                break
            }
        }
        print("uniform 3x3 cell ratio: \(ratio)")
        XCTAssertLessThan(ratio, 0.5,
            "image looks stretched into 3x3 blocks (ratio \(ratio))")
    }
}
