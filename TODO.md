# TODO

## TOP PRIORITY

- [ ] **F-Zero X Expansion Kit (N64DD) does not work — investigate ndd loading path** — F-Zero X: Expansion Kit does not load/play in M64P, despite the N64DD IPL file being present and native `.ndd` support supposedly existing. Investigate the full ndd loading path (ROM add → IPL/DMA/DD register emulation → what actually breaks) and record findings here.
  - _Findings:_

### Backport from Phobos — parallel-RDP robustness & performance

Improvements identified from comparing the turnip fork against `pwnedbygary/phobos` (which uses the same parallel-RDP library). Phobos has accumulated several Turnip/Adreno-specific workarounds and pipeline optimisations that apply here too.

- [x] **Bounded fence waits in scanout** — `parallel_imp.cpp:79` calls `scanout.fence->wait()` (infinite). Replace with `scanout.fence->wait_timeout(100'000'000ull)` (100ms). If the fence times out (GPU driver wedge), skip the frame instead of hanging the emulation thread. Pattern from Phobos `vulkan.cpp::scanoutAsync()` / `mapScanoutRead()`. _Verified on-device 2026-08-21 (Retroid Pocket 6, Turnip Adreno 740): T1 regression ~20 min @2x — clean, zero timeouts; T2 1ms probe — 3 real timeouts (GPU-busy bursts at load), all skipped cleanly (rate-limited log, no hang, no leak); final fresh-install smoke clean. User confirms "game runs absolutely fine"._
  - _Test plan:_
    - Instrumentation: rate-limited (5s) `LOGW` on timeout → logcat tag `Granite`, msg "scanout fence wait timed out (100ms), skipping frame". Capture via `adb logcat -c && adb logcat Granite:W System.err:W AndroidRuntime:E` (device 49016109, Retroid Pocket 6). App clears logcat on launch anyway.
    - **T1 Regression:** build+install, enable FPS overlay, play 2–3 games (e.g. OoT moderate, F-Zero X heavy) ≥10 min each at 1× and 2× upscale. PASS: FPS/visuals match pre-change baseline, no blank frames, zero `Granite` timeout lines. If timeouts appear in normal play, 100ms is too tight → raise it.
    - **T2 Skip-path (forced):** temporary debug build with timeout = 1ms. FINDING: scanout fence signals well under 1ms in normal play (usually already-signaled by wait time) — timeouts fired only 3× total, at load/shader-compile bursts (GPU busy), not "nearly every frame". A 100µs build gave **0** timeouts, confirming steady-state fence completion is sub-100µs. PASS: each of the 3 timeouts hit the skip path → rate-limited `Granite` log (~5s) + frame skipped, no hang/ANR/crash, memory flat (fence pool returns holders via `scanout` destructor). Reverted to 100ms afterwards. NOTE: to force continuous skips for pool-leak stress you must poll (`wait_timeout(0)`) — short non-zero timeouts under-shoot because the fence is pre-signaled.
    - **T3 Real wedge (opportunistic):** sustained heavy/thermal session; on a real wedge expect blank frame + warning + auto-recovery, not a permanent hang.
    - **Rollback:** new blank frames or stutter in T1 → revert or raise timeout.

- [ ] **Non-fatal RDP validation** — Set a `ValidationInterface` on the `CommandProcessor` (currently none is set) that logs malformed RDP commands but does NOT call `RDP::crash()`. Prevents save-state restore hangs and game edge cases where a malformed command would otherwise freeze the RDP permanently. Pattern from Phobos `Validation` struct in `vulkan.cpp`.

- [ ] **Skip-idle-on-destroy for GPU wedge recovery** — Add `processor->set_skip_idle_on_destroy(true)` in the teardown path when a GPU hang is detected (e.g., after a fence timeout). Prevents the `CommandProcessor` destructor from blocking on fences that will never signal. Pattern from Phobos `Vulkan::unload()`.

- [ ] **Pipeline cache UUID validation** — Persist the GPU `pipelineCacheUUID` alongside the cache file and validate it on init. If the driver changed (Turnip version swap, system update), discard the stale cache so stale binaries don't poison pipeline creation. The current filename-based keying (`pipeline_cache_<UUID>.bin`) handles distinct files but doesn't detect in-place driver upgrades. Phobos uses a `.uuid` sidecar file.

- [ ] **Reset scanout fence on VI reconfig** — When the VI registers change (width/height/origin mid-game), reset the stale scanout fence so the next `scanout_async_buffer()` doesn't wait on a fence from the old mode that may never signal. Pattern from Phobos `resetScanoutFence()`.

- [ ] **CPU affinity pinning** — Pin the emulation thread and the parallel-RDP internal worker threads (CommandRing, WorkerThread) to the device's performance cores. Prevents kernel migration to little cores and improves L2/L3 cache locality. Phobos pins to the last-4 cores + sets `setpriority(-10)` in `PhobosRunner.cpp::emulationLoop()`.

- [ ] **Direct surface presentation (skip GLES middleman)** — Current path: Vulkan → CPU memcpy → GLES texture upload → GLES blit → screen compositor. Eliminate the GLES step by writing directly to `ANativeWindow` (already available in `ae-bridge/src/ae_bridge.cpp`) or via Vulkan WSI `VK_KHR_android_surface`. Saves one full-frame copy + GLES context overhead. Bigger effort, but removes the #2 bottleneck after async-RDP mode.

- [ ] **Rate-limited debug logging** — Add toggle-gated `__android_log_print` diagnostics for the parallel-RDP plugin (scanout timing, fence timeouts, pipeline cache events, RDP command counts) with rate-limiting to prevent logcat flooding. Pattern from Phobos's `PhobosVulkan`/`PhobosRSP`/`PhobosCPU` tags with 5-second rate limits.