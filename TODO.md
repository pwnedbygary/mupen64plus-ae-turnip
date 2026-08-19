# TODO

- [x] **Turnip driver support for remaining rendering plugins** — ~~the custom Vulkan driver (AdrenoTools) currently only works with the Parallel (ParaLLEl-RDP) plugin~~ — NOT IMPLEMENTABLE with the in-tree GLideN64: the vendored `mupen64plus-video-gliden64` has no Vulkan backend at all (no `Graphics/Vulkan`, no Vulkan references in its build files). Would require vendoring a GLideN64 mainline port with the Vulkan backend, which is a large separate effort.
- [x] **On-disk Vulkan shader/pipeline cache for the Parallel plugin** — implemented as a persisted `VkPipelineCache` (instead of a full Fossilize port, which needs the absent Fossilize/Granite `Filesystem`/`ShaderManager` machinery). The cache file is written to the core's user cache path (`ConfigGetUserCachePath`, backed by `XDG_CACHE_HOME`) as `pipeline_cache_<pipelineCacheUUID>.bin`, so it is automatically keyed per GPU driver (Turnip vs. system driver have distinct UUIDs). Build flag: `PLUGIN_VULKAN_PIPELINE_CACHE` in `mupen64plus-video-parallel`.

### Driver picker

- [x] **Driver uninstall + version info** — show the installed version (read from each driver's `meta.json`) in the picker and add a "Delete" action per installed driver.
- [x] **Per-game driver override** — add a GamePrefs entry so individual games can use a different driver than the global one.
- [x] **Device GPU info in the picker** — display the device's Adreno model/generation so users know which driver variant (A6xx vs A7xx) to download.
- [x] **Compatibility check on import** — refuse or warn when the driver's `meta.json` `minApi` exceeds the device API level.
- [x] **Driver update check** — reuse the downloader's GitHub API to notify when the selected source has a newer release than the installed driver.

### App / CI

- [x] **CI release builds** — build a signed release APK on tags and upload it as a GitHub release (self-hosted distribution channel).
- [x] **In-app update checker** — compare `versionName` against the repo's latest release and link to the download page.

### Emulation

- [x] **Driver benchmark mode** — timed FPS comparison (system driver vs. custom Turnip driver) on a reference scene, reported via the OSD.