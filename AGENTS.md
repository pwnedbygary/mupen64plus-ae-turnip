# mupen64plus-ae-turnip — repository-local agent instructions

This file holds only repo-specific details that the global protocol does not.
It strengthens the global loop; it does not waive independent review,
evidence rules, explicit staging, or the no-reuse-of-PASS rule.

For the N64DD investigation, `docs/LOCAL_AGENT_INSTRUCTIONS.md` (plus
`docs/N64DD_NEXT_TEST_RUNBOOK.md` and `docs/DEVELOPMENT_PROCESS.md`) remains
authoritative for that scope. This file adds the Turnip UI / device-test /
signing lessons mined from the theme-preset crash-fix sessions.

## 1. Canonical toolchain and builds (verify, do not invent)

- Toolchain (per `README.md` / `replit.md`): Android SDK Platform 34,
  Build Tools 34.0.0, NDK 26.1.10909125, CMake 3.22.1, JDK 17.
- Gradle: always use the wrapper (`./gradlew`); it pins Gradle 8.4 / AGP 8.2.2.
  The system `gradle` 9.7.1 is not a substitute.
- Debug APK (no signing secrets needed):
  `./gradlew assembleDebug` or `./gradlew :app:assembleDebug --console=plain`
  Output: `app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk`
  (Replit-only alternative: `./tools/replit-build.sh`.)
- Release APK: minify enabled, so allow several minutes. Requires the local
  `keystore.properties` mechanism (`app/build.gradle:31-48,77-86`).
  Without it, any `*assemble*Release*`/`*bundle*Release*` task fails fast with
  "Release signing is not configured". Verify with
  `tools/verify-release-apk.sh` where applicable.
- Host suites: `tools/test-dd-*.sh`. If host `gcc` fails to exec `cc1`/`cc1plus`
  (`posix_spawnp: No such file or directory`), the reviewed substitution is
  `CC=clang CXX=clang++`; record it per run. Full D4 ledger example is in
  `docs/N64DD_NEXT_TEST_RUNBOOK.md`.
- This is a native multi-module Android app; there is no browser preview.

## 2. Package and signing identities (mechanism only, never credentials)

- `keystore.properties` and `*.keystore` are gitignored (`.gitignore:61-62`).
  Never display, log, commit, or publish their contents.
- `app/build.gradle:24`: release `applicationId =
  "org.mupen64plusae.turnip.pwnedbygary"`; `debug` adds
  `applicationIdSuffix ".debug"` (`app/build.gradle:58`), so debug and release
  install side by side.
- A debug key cannot update an install signed with the original key. Never
  uninstall or clear app data to bypass a signature mismatch; preserve saves
  and arrange backup/restore before device testing.
- For unfixed-commit worktrees: copy the gitignored signing files with absolute
  paths (a bare `cp` run in the wrong directory silently lands in the wrong
  place), build, then delete the temp worktree + keystore copies + `/tmp`
  device scripts when done, leaving the tree clean.

## 3. Device testing — Retroid Pocket 6 + uiautomator2 gotchas

- ADB path varies (documented Mac example: `~/Downloads/platform-tools/adb`).
  Verify availability; do not assume PATH. Install with `adb install -r`;
  never uninstall/clear data to hide evidence.
- Disable auto-sleep for the session (10-min timeout used previously) and wake
  before each capture. Symptoms of a slept display: empty hierarchy + ~395-byte
  black screenshots. Distinguish sleep from crash with screen/power state + a
  fresh real framebuffer (~80 KB awake) before concluding.
- Navigate promptly; capture *immediately* after each click with retries.
  After an activity `recreate()`, re-dump the full UI to reorient — the preset
  dialog closes on recreate (expected) and must be reopened for round two.
- Hierarchy filters must include preference rows: they use `android:` ids, not
  the app package. A filter on the app package alone hides them.
- Filter the Argos frontend overlay out of dumps; it pollutes the hierarchy.
- This uiautomator2 build has no `Device.tap` and `app_start` is broken.
  Use direct `d(text=...).click()` where supported (manual bounds parsing is
  fragile) and `monkey` for launches. For tap bursts, chain raw `input tap`
  commands in a single shell call — faster and sufficient for stacking
  pressure (observed: 14 taps/0.72s, 20 taps/1.02s, 100-event monkey flood in
  ~82 ms).
- Theme navigation: drawer shows only 4 entries; Theme lives one level deeper
  under Settings. Preset row is "Color presets" under the Theme screen
  (user path was Settings → Display → Themes; verify against the live prefs
  list rather than assuming).
- First-launch gates: notifications prompt (deny by resource ID, not text),
  then a storage permission OK before reaching the gallery.

## 4. Theme-preset burst verification contract

- Clear logcat, fire the burst into the open preset dialog, then check:
  `IllegalStateException` / `FATAL` / `AndroidRuntime` / `has been destroyed`
  = 0; `top`/focused activity still ours (`ThemePrefsActivity`); dialogs still
  dismissible via coordinate tap; prefs list intact; theme hex values change
  across runs (last-wins applies).
- Platform behavior observed on this device: tap 1 dismisses the dialog and
  posts `recreate()`; the flood is then dropped as "not responsive" or arrives
  after the posted `recreate()` (Looper FIFO — a later tap cannot jump ahead).
  Expect `InputDispatcher: Not sending touch gesture ... not responsive` and
  `Input channel ... was disposed` lines under flood. A second `recreate()`
  piling onto the same instance is practically unreproducible here, so the
  stacked-`recreate()` crash claim stays theoretical (code inspection) unless a
  new repro shows otherwise.
- Workflow preference: run the negative control (unfixed build, e.g. release
  `8fec5507` alongside fixed debug `2c125771`) before calling a fix good.
  Verification-only runs change no repo files (only `/tmp` scripts); expect
  clean `git status --short` / empty `git diff --stat`.
- State hygiene: uninstall the temporary unfixed release when done if only the
  fixed debug should remain; record which package remains.

## 5. Branches, review, and handoff pointers

- Crash-fix work: branch `fix/theme-quick-switch-crash`, HEAD `2c1257717`
  ("Fix theme preset quick-switch crash from stacked activity.recreate()"),
  PR #4 → `master` at time of mining. Verify current branch/base before editing.
- N64DD work: publication target `dd-eos-watchdog-checkpoint` (archive
  `dd-eos-watchdog-archive-pre-v336-20260911` must not be merged back);
  per-iteration handoff in `docs/HANDOFF_NEW.md` before review/commit.
- Commit message keeps reviewer verdict + snapshot hashes + re-run checks;
  stage explicit paths only. Global evidence labels (OBSERVED / DERIVED /
  SUPPLIED-ONLY / HYPOTHESIS / UNKNOWN) apply to device claims too: host,
  simulated, staged, and native/device results are not interchangeable.
