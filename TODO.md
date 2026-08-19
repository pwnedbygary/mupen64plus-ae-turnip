# TODO

- [ ] **Turnip driver support for remaining rendering plugins** — the custom Vulkan driver (AdrenoTools) currently only works with the Parallel (ParaLLEl-RDP) plugin. Add Vulkan-backed rendering to the other video plugins (e.g. GLideN64's Vulkan backend) and wire them into the same `setCustomVulkanDriver` hookup so the Turnip driver applies across all cores.
- [ ] **On-disk Vulkan shader/pipeline cache for the Parallel plugin** — neither Vulkan plugin persists shaders today: Granite's Fossilize disk cache (`GRANITE_VULKAN_FOSSILIZE`, `device_fossilize.cpp`) is compiled out and no `VkPipelineCache` is created, so pipelines recompile on every game launch (first-launch stutter). Build the Fossilize library into `mupen64plus-video-parallel` and persist a `.foz`/pipeline cache in the app cache dir keyed by driver.

### Driver picker

- [ ] **Driver uninstall + version info** — show the installed version (read from each driver's `meta.json`) in the picker and add a "Delete" action per installed driver.
- [ ] **Per-game driver override** — add a GamePrefs entry so individual games can use a different driver than the global one.
- [ ] **Device GPU info in the picker** — display the device's Adreno model/generation so users know which driver variant (A6xx vs A7xx) to download.
- [ ] **Compatibility check on import** — refuse or warn when the driver's `meta.json` `minApi` exceeds the device API level.
- [ ] **Driver update check** — reuse the downloader's GitHub API to notify when the selected source has a newer release than the installed driver.

### App / CI

- [ ] **CI release builds** — build a signed release APK on tags and upload it as a GitHub release (self-hosted distribution channel).
- [ ] **In-app update checker** — compare `versionName` against the repo's latest release and link to the download page.

### Emulation

- [ ] **Driver benchmark mode** — timed FPS comparison (system driver vs. custom Turnip driver) on a reference scene, reported via the OSD.