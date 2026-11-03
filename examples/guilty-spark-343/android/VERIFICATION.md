# Android 0.1.0 verification

Verified on 2026-10-23 using JDK 17, Gradle 8.14.3, Android Gradle Plugin 8.11.0 and Android API 36.

- Debug, release and instrumentation APK builds passed.
- 44 standalone Java connection/HTTP checks passed.
- 14 Android activity/WebView integration checks passed on an isolated API 36 x86_64 emulator using a loopback-only synthetic server. These cover initial setup, invalid HTTP origins, save for later, restart persistence, unreachable-server feedback, access-code rejection/acceptance, synthetic chat exchange, origin restrictions, server failure, and reconnect.
- Android lint: zero errors, three advisory warnings (layout composition and launcher icon resource/theme suggestions).
- The final non-debuggable, development-signed release APK installed and cold-launched successfully on the emulator. Its setup screen was visually inspected.
- APK v2 signature verification passed. Manifest requires Android 8.0+ (API 26), targets API 36, and requests only Internet and network-state permissions.

The emulator showed an Android System UI startup timeout before app tests; it was dismissed. No Guilty Spark crash was observed. An earlier incremental Android resource build failed after relocating the launcher resource; a clean build succeeded. These are retained in the local build evidence.

Physical Android hardware, the future Nebula path and real model generation through that path have not been tested. The synthetic test response is not evidence of model inference. Server-side deployment remains separate and was not activated during this app build.

Local APK, logs, screenshot, checksum and machine-readable receipt are under `output/guilty-spark-343/android-0.1.0/` at the repository root. The output folder is ignored by Git. No publication or Git commit was performed.
