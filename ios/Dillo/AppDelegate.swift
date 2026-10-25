import UIKit

@main
class AppDelegate: UIResponder, UIApplicationDelegate {
    var window: UIWindow?

    func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions:
            [UIApplication.LaunchOptionsKey: Any]?
    ) -> Bool {
        let incomingURL = launchOptions?[.url] as? URL
        let initialURL = incomingURL.flatMap {
            DilloAutomation.shared.target(from: $0)
        }
        let window = UIWindow(frame: UIScreen.main.bounds)
        let browser = BrowserViewController(initialURL: initialURL)
        window.rootViewController = browser
        window.makeKeyAndVisible()
        self.window = window
        DilloAutomation.shared.connect(browser)
        return true
    }

    func application(
        _ app: UIApplication,
        open url: URL,
        options: [UIApplication.OpenURLOptionsKey: Any] = [:]
    ) -> Bool {
        return DilloAutomation.shared.handle(url)
    }
}
