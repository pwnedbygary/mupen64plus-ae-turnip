# Mupen64Plus-AE Turnip

A fork of [Mupen64Plus-AE](https://github.com/mupen64plus-ae/mupen64plus-ae) (based on the Mupen64FZ-Pro source) with built-in **Turnip GPU driver support**, letting you run a custom open-source Vulkan driver (Adreno only) without root.

This build is fully open-source and free — no ads, no analytics, no pro licensing gates. Google Drive cloud saves and netplay are included as free features.

## Features

- **Turnip GPU driver picker** — import any standard [AdrenoToolsDrivers](https://github.com/K11MCH1/AdrenoToolsDrivers) zip, or **download one directly in-app** from well-known release sources (K11MCH1/AdrenoToolsDrivers, StevenMXZ, The412Banner/Banners-Turnip, MrPurple666/purple-turnip, whitebelyash, nihui/mesa-turnip-android-driver). The driver is extracted to internal storage and loaded via [libadrenotools](https://github.com/bylaws/libadrenotools) when the **Parallel vulkan renderer** is selected.
- **Driver management** — the picker shows each installed driver's version, required API level and library name, your device's GPU model, and a per-driver benchmark score. Drivers can be uninstalled, and incompatible drivers (device API level below the driver's `minApi`) are rejected on import. A background check notifies you when the driver's source has a newer release.
- **Per-game driver override** — individual games can use a different driver than the global default via the per-game preferences.
- **On-disk Vulkan pipeline cache** — the Parallel plugin persists its `VkPipelineCache` (keyed per driver by the GPU's pipeline cache UUID) to the core cache directory, eliminating first-launch shader stutter after the first run.
- **Driver benchmark mode** — toggleable timed benchmark that measures the average FPS of the active driver on your device, so you can compare Turnip vs. the system driver.
- **ParaLLEl-RDP** Vulkan renderer with upscaling, texture filtering and modern RDP accuracy.
- All classic plugins: GLideN64, glide64mk2, GLN64, Rice, Angrylion, plus HLE/cxd4/parallel RSPs.
- Netplay (local + room-based), Google Drive cloud saves, touchscreen/controller profiles, 7z/zip ROM support.
- **In-app update checker** — the About menu reports when a new release is available and links to the download page.
- Automatic CI builds: debug APK on every commit, signed release APK on version tags.

## Requirements for the Turnip driver

- 64-bit ARM device (arm64-v8a) running **Android 9 or newer**
- A driver zip in AdrenoToolsDrivers format (a root-level `meta.json` containing `name` and `libraryName`, e.g. `vulkan.adreno.so`)

The custom driver applies to the **Parallel** plugin (the only Vulkan renderer). The vendored GLideN64 has no Vulkan backend, so extending the Turnip driver to it would require porting GLideN64's mainline Vulkan renderer; see `TODO.md`.

## Nightly Builds

|Download nightly builds from continuous integration:| [![Build Status][Build]][Actions] |
|----------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|Download signed release builds:| [Releases](https://github.com/pwnedbygary/mupen64plus-ae-turnip/releases) |

[Actions]: https://github.com/pwnedbygary/mupen64plus-ae-turnip/actions/workflows/build.yml
[Build]: https://github.com/pwnedbygary/mupen64plus-ae-turnip/actions/workflows/build.yml/badge.svg

## Build Instructions

Prerequisites:

- [Android Studio](https://developer.android.com/studio/index.html) or a command-line Android SDK
- JDK 17
- SDK Platform 34, Build Tools 34.0.0, NDK 26.1.10909125, CMake 3.22.1
  (Gradle can auto-install these if the SDK licenses are accepted)

Steps:

1. Clone the repository:
   - `git clone https://github.com/pwnedbygary/mupen64plus-ae-turnip.git`
2. Open the project using Android Studio (or run `./gradlew :app:assembleDebug`)
3. Build and run the app

### Release builds

Release APKs are built and uploaded to GitHub Releases for any tag matching `v*`. For a signed release, set up a `keystore.properties` file in the repo root:

```
storeFile=/path/to/keystore.jks
storePassword=...
keyAlias=...
keyPassword=...
```

Without it, the release build falls back to debug signing. Note the in-app update checker compares the latest tag against the app's version code, so name release tags `v<versionCode>` (e.g. `v332`).

## Usage

1. Run a game and set the per-game *Emulation Profile* video plugin to **Parallel vulkan renderer**.
2. Open Settings → Display → **GPU driver**.
3. Do one of the following:
   - Select *Import driver…* and pick an AdrenoToolsDrivers zip (e.g. a Turnip release), or
   - Select *Download driver…* and choose a release source; the driver is downloaded, checked for device compatibility and installed automatically.
4. Select the imported driver from the list and launch your game.
5. (Optional) Enable the *GPU driver benchmark* under Settings → Display to compare the average FPS of the active driver.

If the driver fails to load (e.g. device or Android version mismatch), the app falls back to the system Vulkan driver automatically.

## License

The app is licensed under the GNU GPL, following Mupen64Plus-AE. The vendored [libadrenotools](https://github.com/bylaws/libadrenotools) is BSD-2-Clause (see `adrenotools/LICENSE`).