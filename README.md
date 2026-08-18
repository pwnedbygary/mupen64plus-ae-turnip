# Mupen64Plus-AE Turnip

A fork of [Mupen64Plus-AE](https://github.com/mupen64plus-ae/mupen64plus-ae) (based on the Mupen64FZ-Pro source) with built-in **Turnip GPU driver support**, letting you run a custom open-source Vulkan driver (Adreno only) without root.

This build is fully open-source and free — no ads, no analytics, no pro licensing gates. Google Drive cloud saves and netplay are included as free features.

## Features

- **Turnip GPU driver picker** — import any standard [AdrenoToolsDrivers](https://github.com/K11MCH1/AdrenoToolsDrivers) zip from Settings → Display → *GPU driver*. The driver is extracted to internal storage and loaded via [libadrenotools](https://github.com/bylaws/libadrenotools) when the **Parallel vulkan renderer** is selected.
- **ParaLLEl-RDP** Vulkan renderer with upscaling, texture filtering and modern RDP accuracy.
- All classic plugins: GLideN64, glide64mk2, GLN64, Rice, Angrylion, plus HLE/cxd4/parallel RSPs.
- Netplay (local + room-based), Google Drive cloud saves, touchscreen/controller profiles, 7z/zip ROM support.
- Automatic CI builds of the debug APK on every commit.

## Requirements for the Turnip driver

- 64-bit ARM device (arm64-v8a) running **Android 9 or newer**
- A driver zip in AdrenoToolsDrivers format (a root-level `meta.json` containing `driverName` and `libraryName`, e.g. `libvulkan_adreno.so`)

The custom driver currently applies to the **Parallel** plugin (the only Vulkan renderer). See `TODO.md` for extending it to the remaining rendering plugins.

## Nightly Builds

|Download nightly builds from continuous integration:| [![Build Status][Build]][Actions] |
|----------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------|

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

## Usage

1. Run a game and set the per-game *Emulation Profile* video plugin to **Parallel vulkan renderer**.
2. Open Settings → Display → **GPU driver**.
3. Select *Import driver…* and pick an AdrenoToolsDrivers zip (e.g. a Turnip release).
4. Select the imported driver from the list and launch your game.

If the driver fails to load (e.g. device or Android version mismatch), the app falls back to the system Vulkan driver automatically.

## License

The app itself is licensed under the GPLv3, following Mupen64Plus-AE. The vendored [libadrenotools](https://github.com/bylaws/libadrenotools) is BSD-2-Clause (see `adrenotools/LICENSE`).