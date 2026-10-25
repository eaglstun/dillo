import Foundation
import UIKit
import AppIntents

/// Routes system URL events and App Intents into the active browser.
@MainActor
final class DilloAutomation {
    static let shared = DilloAutomation()

    private weak var browser: BrowserViewController?
    private var pendingTarget: URL?

    private init() {}

    func connect(_ browser: BrowserViewController) {
        self.browser = browser
        if let target = pendingTarget {
            pendingTarget = nil
            browser.openAutomationURL(target)
        }
    }

    func open(_ target: URL) {
        guard target.scheme?.lowercased() != "dillo" else { return }
        if let browser = browser {
            browser.openAutomationURL(target)
        } else {
            pendingTarget = target
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

struct OpenURLInDilloIntent: AppIntent {
    static let title: LocalizedStringResource = "Open URL in Dillo"
    static let description = IntentDescription(
        "Opens a web address in the Dillo browser.")
    static let openAppWhenRun = true

    @Parameter(title: "URL")
    var url: URL

    static var parameterSummary: some ParameterSummary {
        Summary("Open URL in Dillo") {
            \.$url
        }
    }

    func perform() async throws -> some IntentResult {
        await DilloAutomation.shared.open(url)
        return .result()
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
    }
}
