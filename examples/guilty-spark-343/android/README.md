# Guilty Spark 343 for Android

An installable Android companion for the Guilty Spark 343 desktop server. Version 0.1.0 provides a native connection screen and runs the existing model/chat workspace in an Android WebView. Model inference and conversation storage remain on the server. It does not contain model weights or run a model on the phone.

The app can be installed before a network or server is ready. It opens to setup without contacting a default host. Save a server address for later, or connect immediately. Nothing in this project enables a laptop listener, changes firewall rules, installs a VPN, or configures Nebula.

## Using the app

1. Install the supplied APK on Android 8.0 or newer.
2. When your connection is ready, start the separately configured Nebula client on your phone and host.
3. Enter the host's Guilty Spark address, including its port. For example, `http://10.42.0.2:34343/`. This is an example, not a preconfigured endpoint. A trusted HTTPS origin is also supported.
4. Tap **Connect**. Enter the current access code from the desktop app's **Phone connection** screen directly into the server's login page.
5. Select a model and use the server's chat workspace. The top-right menu provides reload, connection settings, opening the workspace in a browser, and sign out.

HTTP connections are accepted only for literal private IPv4, CGNAT, loopback or IPv6 ULA addresses. Public hosts must use HTTPS with a trusted certificate. A Nebula connection can use an IPv4 address from its configured private range; this app uses the Android network route and does not create its own VPN. The desktop server must eventually listen on its Nebula interface and its host firewall must allow that path. Those deployment steps are separate from this build.

The server generates a fresh access code after restarting or changing connection settings. A previous phone session may therefore need to sign in again. A sleeping or powered-off host cannot serve the workspace.

## Client behavior and limits

- Saves only the selected origin in application-private preferences. Android backup is disabled. Access codes are not embedded in the APK, URLs, preferences, or application logs.
- Uses the server's HttpOnly cookie for its WebView session. Sign out clears this app's WebView cookies without deleting server conversations.
- Checks the direct server connection before loading the workspace. Redirects, wrong services, HTTP failures and unreachable hosts produce useful setup-screen errors. The page-title check is a readiness check, not proof of server identity.
- Restricts workspace navigation to the selected origin. User-activated external links require confirmation before opening a browser. No JavaScript-to-native bridge is exposed; WebView filesystem access and third-party cookies are disabled. External network subresources are blocked by the client, so third-party extensions/assets may not work.
- Supports the system file picker for attachments. File exports and downloads should use **Open in browser**; blob-download integration is not implemented in this version. The browser has a separate login session.
- Does not add Android background model jobs, notification forwarding, a VPN service, or an offline chat engine. Server/browser scheduling limits still apply when the workspace is paused or closed.
- Back navigation, keyboard/system insets, rotation, connection cancellation, saved-address restoration and renderer/network failures are handled by the native shell. An unsent web draft can be lost when leaving or recreating the workspace; the native confirmation calls this out.

## Build

Requirements: JDK 17 or newer, Python 3, Android SDK platform 36/build tools, and network access on the first Gradle dependency download. No third-party libraries are packaged in the client.

Set `JAVA_HOME` and `ANDROID_SDK_ROOT` (or configure an ignored `local.properties` through Android Studio), then run:

```text
python build.py
```

This runs standalone connection/HTTP tests, builds debug and non-debuggable release APKs, builds the Android instrumentation APK, and runs Android lint. `python build.py --unit-only` needs only a JDK and Python; `--offline` uses cached Gradle dependencies. The entry point discovers the project from its own location and uses argument arrays with `shell=False`.

APK outputs:

```text
app/build/outputs/apk/release/app-release.apk
app/build/outputs/apk/debug/app-debug.apk
app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
```

The release variant is **development-signed for personal sideloading**, with debugging disabled. It uses the local Android development key outside the source tree. Before publishing to a store or distributing durable updates, configure an operator-owned release key and release process. No signing keys are checked into this project.

## Verification

`tests/ConnectionTests.java` covers server-address validation, private and HTTPS origins, origin changes, oversized responses, redirects, wrong services, HTTP errors and cancellation. `SmokeInstrumentation` tests the actual Android activity and WebView with an isolated synthetic HTTP fixture. Synthetic replies are explicitly labeled and are not model inference.

To run integration checks on an owned test emulator:

```text
python tests/fixture_server.py --port 34344
adb -s <test-emulator> reverse tcp:34344 tcp:34344
adb -s <test-emulator> install -r app/build/outputs/apk/debug/app-debug.apk
adb -s <test-emulator> install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb -s <test-emulator> shell am instrument -w -e fixture http://127.0.0.1:34344/ com.guiltyspark343.android.test/com.guiltyspark343.android.SmokeInstrumentation
```

Use a test emulator: instrumentation clears this app's test preferences/session before and after checking. Remove the reverse mapping and stop the fixture afterwards. Physical-phone/Nebula reachability is a separate acceptance check once that network and server are configured.

Implementation references: [Android WebView](https://developer.android.com/develop/ui/views/layout/webapps/webview), [WebView lifecycle](https://developer.android.com/develop/ui/views/layout/webapps/managing-webview), and [Android Gradle Plugin 8.11 compatibility](https://developer.android.com/build/releases/agp-8-11-0-release-notes).
