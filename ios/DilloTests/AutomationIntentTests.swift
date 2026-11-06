import AppIntents
import XCTest
@testable import Dillo

/// Runs the App Intents against the live browser in the host app, using
/// local pages so the results don't depend on the network.
@MainActor
final class AutomationIntentTests: XCTestCase {
    private var dir: URL!
    private var page1: URL!

    override func setUp() async throws {
        dir = FileManager.default.temporaryDirectory
            .appendingPathComponent("dillo-intents-\(UUID().uuidString)")
        try FileManager.default.createDirectory(
            at: dir, withIntermediateDirectories: true)
        page1 = dir.appendingPathComponent("page1.html")
        try """
        <html><head><title>Intent test page</title></head><body>
        <p><a href="page2.html">relative</a>
        <a href="https://example.com/x">absolute</a>
        <a href="#frag">fragment</a></p>
        </body></html>
        """.write(to: page1, atomically: true, encoding: .utf8)
        try "<html><body><p>No title here.</p></body></html>".write(
            to: dir.appendingPathComponent("untitled.html"),
            atomically: true, encoding: .utf8)
    }

    override func tearDown() async throws {
        try? FileManager.default.removeItem(at: dir)
    }

    private func open(_ url: URL) async throws -> URL {
        var intent = OpenURLInDilloIntent()
        intent.url = url
        _ = try await intent.perform()
        // The intent returns the final URL; read the same state it reports.
        let browser = try XCTUnwrap(DilloAutomation.shared.currentBrowser)
        return try XCTUnwrap(browser.currentPageURL.flatMap(URL.init(string:)))
    }

    func testOpenThenReadURLTitleSourceAndLinks() async throws {
        let final = try await open(page1)
        XCTAssertEqual(final.standardizedFileURL, page1.standardizedFileURL)

        let browser = try XCTUnwrap(DilloAutomation.shared.currentBrowser)
        XCTAssertEqual(browser.currentPageTitle, "Intent test page")
        XCTAssertTrue(browser.currentPageSource?.contains("Intent test page")
                      ?? false)

        let links = browser.currentPageLinks
        XCTAssertEqual(links.count, 3, "\(links)")
        XCTAssertTrue(links[0].hasSuffix("/page2.html"), links[0])
        XCTAssertEqual(links[1], "https://example.com/x")
        XCTAssertTrue(links[2].hasSuffix("/page1.html#frag"), links[2])

        // The read intents succeed on a loaded page.
        _ = try await GetCurrentPageURLIntent().perform()
        _ = try await GetCurrentPageTitleIntent().perform()
        _ = try await GetPageSourceIntent().perform()
        _ = try await GetPageLinksIntent().perform()
    }

    func testPageWithoutTitleReportsNoTitle() async throws {
        _ = try await open(dir.appendingPathComponent("untitled.html"))
        let browser = try XCTUnwrap(DilloAutomation.shared.currentBrowser)
        XCTAssertNil(browser.currentPageTitle)
        _ = try await GetCurrentPageTitleIntent().perform()   // returns ""
    }

    func testFailedLoadThrowsAndClearsPage() async throws {
        _ = try await open(page1)
        var intent = OpenURLInDilloIntent()
        intent.url = dir.appendingPathComponent("does-not-exist.html")
        do {
            _ = try await intent.perform()
            XCTFail("opening a missing file should throw")
        } catch {}

        // An error page is showing now, so there is no current page.
        do {
            _ = try await GetCurrentPageURLIntent().perform()
            XCTFail("expected noPage")
        } catch DilloIntentError.noPage {}
    }

    func testDilloURLsAreRejected() async throws {
        var intent = OpenURLInDilloIntent()
        intent.url = URL(string: "dillo:https://example.com")!
        do {
            _ = try await intent.perform()
            XCTFail("expected unsupportedURL")
        } catch DilloIntentError.unsupportedURL {}
    }

    func testOnlyTheNewestLoadWins() async throws {
        let browser = try XCTUnwrap(DilloAutomation.shared.currentBrowser)
        var results: [Result<String, Error>] = []
        let done = expectation(description: "both loads finished")
        done.expectedFulfillmentCount = 2
        browser.openAutomationURL(dir.appendingPathComponent("untitled.html")) {
            results.append($0); done.fulfill()
        }
        browser.openAutomationURL(page1) {
            results.append($0); done.fulfill()
        }
        await fulfillment(of: [done], timeout: 20)

        XCTAssertEqual(browser.currentPageTitle, "Intent test page")
        let superseded = results.filter {
            if case .failure(DilloIntentError.superseded) = $0 { return true }
            return false
        }
        XCTAssertEqual(superseded.count, 1, "the first load must be reported as superseded")
    }
}
