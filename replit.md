# Replit setup

## Native N64DD investigation baseline

Use `docs/HANDOFF_NEW.md` as the current investigation handoff. The application
source has been reset to release tag `v336` (`dc955483a`); that tag matches
remote `master`. Keep the release's WritableROM saving. The baseline's IPL-first
policy conflicts with LuigiBlood's cartridge-first combo boot documentation;
do not call it a proven fix. See the audit in the handoff before changing it.
All new runtime behavior and diagnostics must require explicit per-game N64DD
activation and leave DD-disabled games unchanged. Existing WritableROM remains
independently enabled by its per-ROM setting, including DD-disabled cart hacks.

Evidence-led re-engineering for hardware fidelity, accuracy, or emulation
performance is authorized when needed. This does not waive the DD-only
boundary or save/regression requirements. Distinguish reference-source
comparisons and synthetic diagnostic checks from actual device results;
measure performance before claiming an improvement.

Working native DD under dynarec is required for speed. Cached interpreter is
a diagnostic comparison, not an acceptable final workaround or substitute
for dynarec acceptance. Correct operation under both engines is the goal,
with dynarec the required target and cached interpreter a secondary target.

The active GitHub branch is `dd-eos-watchdog-checkpoint`. Its previous
experimental history is preserved under
`dd-eos-watchdog-archive-pre-v336-20260911`. Do not merge that history back
into the clean baseline. Old diagnoses and watchdog instructions are not
current evidence. New work must separate source facts, device observations,
and hypotheses, and preserve Mario Kart Amped Up, Mario Tennis, and EK Cart
Hack save behavior.

For every test iteration, update `docs/HANDOFF_NEW.md` before committing and
publishing the focused source/test/documentation changes to the current debug
branch (`dd-eos-watchdog-checkpoint` unless the user changes it). The user
feeds this branch into a local LLM. Include the purpose, changes, observations,
verification, limitations and next test instructions; provide the published
commit ID with each test APK. Preserve upstream history with non-force
updates. Never publish raw device logs, signing material or workspace metadata.

This workspace retains Replit-only metadata/build helpers alongside the
release application tree. The old generated APK may still exist in ignored
build outputs; do not distribute it as a baseline build. Local device builds
use the original signing setup, with their commit, dirty diff, and APK hash
recorded. No fresh baseline native boot or regression tests have run here.

This repository is a native, multi-module Android application. It does not run
in Replit's browser preview.

## Build a debug APK

Run:

```bash
./tools/replit-build.sh
```

The first build downloads Android SDK Platform 34, Build Tools 34.0.0,
NDK 26.1.10909125, and CMake 3.22.1 into the ignored `.android-sdk` directory.
Later builds reuse that installation.

The resulting APK is:

```text
app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk
```

The debug build needs no signing secrets, but a Replit-generated debug key
does not match the existing handheld installation. Use the original local
signing setup for updates; do not uninstall or clear app data to bypass a
signature mismatch. Release signing uses the project's existing
`keystore.properties` mechanism and is not configured by this setup.

## After task merges

The post-merge hook runs `tools/post-merge.sh`, which reuses the SDK setup and
incremental debug APK build. It runs without interactive input and allows up to
20 minutes for a cold native build. Build errors fail setup rather than being
silently ignored.