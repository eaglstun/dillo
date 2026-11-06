import UIKit

/// Owns the browser window. A dillo: URL that launches the app arrives in
/// the connection options; later ones arrive via openURLContexts.
final class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?

    func scene(
        _ scene: UIScene,
        willConnectTo session: UISceneSession,
        options connectionOptions: UIScene.ConnectionOptions
    ) {
        guard let windowScene = scene as? UIWindowScene else { return }

        let initialURL = connectionOptions.urlContexts.first.flatMap {
            DilloAutomation.shared.target(from: $0.url)
        }
        let window = UIWindow(windowScene: windowScene)
        let browser = BrowserViewController(initialURL: initialURL)
        window.rootViewController = browser
        window.makeKeyAndVisible()
        self.window = window
        DilloAutomation.shared.connect(browser)
    }

    func scene(_ scene: UIScene,
               openURLContexts URLContexts: Set<UIOpenURLContext>) {
        for context in URLContexts {
            DilloAutomation.shared.handle(context.url)
        }
    }
}
