import Foundation
import UIKit
import AppIntents

/// Routes system URL events and App Intents into the active browser.
@MainActor
final class DilloAutomation {
    static let shared = DilloAutomation()

    typealias OpenCompletion = (Result<String, Error>) -> Void

    private weak var browser: BrowserViewController?
    private var pending: (target: URL, completion: OpenCompletion?)?

    private init() {}

    /// The browser, once the app has finished launching.
    var currentBrowser: BrowserViewController? { browser }

    func connect(_ browser: BrowserViewController) {
        self.browser = browser
        if let pending = pending {
            self.pending = nil
            browser.openAutomationURL(pending.target,
                                      completion: pending.completion)
        }
    }

    /// Open target; completion gets the final URL once the page is parsed.
    func open(_ target: URL, completion: OpenCompletion? = nil) {
        guard target.scheme?.lowercased() != "dillo" else {
            completion?(.failure(DilloIntentError.unsupportedURL))
            return
        }
        if let browser = browser {
            browser.openAutomationURL(target, completion: completion)
        } else {
            // Only the newest request survives a cold start; tell the
            // older one rather than leaving its caller waiting forever.
            pending?.completion?(.failure(DilloIntentError.superseded))
            pending = (target, completion)
        }
    }

    /// Async form for App Intents: returns the final URL after redirects.
    func open(_ target: URL) async throws -> String {
        try await withCheckedThrowingContinuation { continuation in
            open(target) { continuation.resume(with: $0) }
        }
    }

    /// Decode `dillo:https://example.com` and the query-based fallback.
    func target(from automationURL: URL) -> URL? {
        guard automationURL.scheme?.lowercased() == "dillo" else {
            return nil
        }

        if automationURL.host?.lowercased() == "open",
           let components = URLComponents(
                url: automationURL, resolvingAgainstBaseURL: false),
           let value = components.queryItems?.first(where: {
                $0.name.lowercased() == "url"
           })?.value,
           let target = URL(string: value), target.scheme != nil {
            return target
        }

        let absolute = automationURL.absoluteString
        guard let colon = absolute.firstIndex(of: ":") else { return nil }
        let value = String(absolute[absolute.index(after: colon)...])
        guard let target = URL(string: value), target.scheme != nil else {
            return nil
        }
        return target
    }

    @discardableResult
    func handle(_ automationURL: URL) -> Bool {
        guard let target = target(from: automationURL) else { return false }
        open(target)
        return true
    }
}

enum DilloIntentError: Error, CustomLocalizedStringResourceConvertible {
    case noPage
    case unsupportedURL
    case superseded

    var localizedStringResource: LocalizedStringResource {
        switch self {
        case .noPage:
            return "No page is open in Dillo. Use Open URL in Dillo first."
        case .unsupportedURL:
            return "Dillo can only open web addresses, not dillo: links."
        case .superseded:
            return "Another URL was opened in Dillo before this one loaded."
        }
    }
}

/// The browser with a loaded page, or the reason there isn't one.
@MainActor
private func browserWithPage() throws -> BrowserViewController {
    guard let browser = DilloAutomation.shared.currentBrowser,
          browser.currentPageURL != nil else {
        throw DilloIntentError.noPage
    }
    return browser
}

struct OpenURLInDilloIntent: AppIntent {
    static let title: LocalizedStringResource = "Open URL in Dillo"
    static let description = IntentDescription(
        "Opens a web address in Dillo and waits for the page to load. Returns the page's final address, after any redirects.")
    static let openAppWhenRun = true

    @Parameter(title: "URL")
    var url: URL

    static var parameterSummary: some ParameterSummary {
        Summary("Open \(\.$url) in Dillo")
    }

    @MainActor
    func perform() async throws -> some IntentResult & ReturnsValue<URL> {
        let finalURL = try await DilloAutomation.shared.open(url)
        return .result(value: URL(string: finalURL) ?? url)
    }
}

struct GetCurrentPageURLIntent: AppIntent {
    static let title: LocalizedStringResource = "Get Current Page URL"
    static let description = IntentDescription(
        "Returns the address of the page open in Dillo.")

    @MainActor
    func perform() async throws -> some IntentResult & ReturnsValue<URL> {
        let browser = try browserWithPage()
        guard let url = browser.currentPageURL.flatMap(URL.init(string:)) else {
            throw DilloIntentError.noPage
        }
        return .result(value: url)
    }
}

struct GetCurrentPageTitleIntent: AppIntent {
    static let title: LocalizedStringResource = "Get Current Page Title"
    static let description = IntentDescription(
        "Returns the title of the page open in Dillo, or nothing if the page has no title.")

    @MainActor
    func perform() async throws -> some IntentResult & ReturnsValue<String> {
        let browser = try browserWithPage()
        return .result(value: browser.currentPageTitle ?? "")
    }
}

struct GetPageSourceIntent: AppIntent {
    static let title: LocalizedStringResource = "Get Page Source"
    static let description = IntentDescription(
        "Returns the HTML of the page open in Dillo. Pair it with Make Rich Text from HTML to get readable text.")

    @MainActor
    func perform() async throws -> some IntentResult & ReturnsValue<String> {
        let browser = try browserWithPage()
        return .result(value: browser.currentPageSource ?? "")
    }
}

struct GetPageLinksIntent: AppIntent {
    static let title: LocalizedStringResource = "Get Links on Page"
    static let description = IntentDescription(
        "Returns the address of every link on the page open in Dillo, in page order.")

    @MainActor
    func perform() async throws -> some IntentResult & ReturnsValue<[URL]> {
        let browser = try browserWithPage()
        return .result(value: browser.currentPageLinks.compactMap(
            URL.init(string:)))
    }
}

struct DilloShortcuts: AppShortcutsProvider {
    static var appShortcuts: [AppShortcut] {
        AppShortcut(
            intent: OpenURLInDilloIntent(),
            phrases: [
                "Open a URL in \(.applicationName)",
                "Browse with \(.applicationName)",
            ],
            shortTitle: "Open URL in Dillo",
            systemImageName: "globe"
        )
        AppShortcut(
            intent: GetCurrentPageURLIntent(),
            phrases: [
                "Get the page address from \(.applicationName)",
            ],
            shortTitle: "Get Current Page URL",
            systemImageName: "link"
        )
        AppShortcut(
            intent: GetCurrentPageTitleIntent(),
            phrases: [
                "Get the page title from \(.applicationName)",
            ],
            shortTitle: "Get Current Page Title",
            systemImageName: "textformat"
        )
    }
}
