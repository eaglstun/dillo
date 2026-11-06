import UIKit

/// Process-level setup only. Windows, the browser, and incoming URLs live in
/// SceneDelegate: apps built with current SDKs must use the scene life cycle
/// (without it the app fails to launch).
@main
class AppDelegate: UIResponder, UIApplicationDelegate {
    func application(
        _ application: UIApplication,
        didFinishLaunchingWithOptions launchOptions:
            [UIApplication.LaunchOptionsKey: Any]?
    ) -> Bool {
        return true
    }

    func application(
        _ application: UIApplication,
        configurationForConnecting connectingSceneSession: UISceneSession,
        options: UIScene.ConnectionOptions
    ) -> UISceneConfiguration {
        UISceneConfiguration(name: "Default",
                             sessionRole: connectingSceneSession.role)
    }
}
