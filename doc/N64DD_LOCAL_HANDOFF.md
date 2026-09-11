# Local signed build and Android trace handoff

## Collaboration

Use `dd-eos-watchdog-checkpoint` for focused, non-force-pushed diagnostic
commits. Fetch and check for local changes before pulling. Keep original
signing keys and passwords on the local build machine. Do not commit APKs,
game/IPL/disk files, saves, raw RAM captures, or signing material.

The Replit workspace is an imported snapshot with separate Git history.
Transfer reviewed source changes onto the remote branch's current tip;
do not replace its tree wholesale or merge the snapshot history.

## Current evidence

The handheld is a Retroid Pocket 6 running Android 13, ARM64. Local Mac ADB
is authorized, and `run-as` works for
`org.mupen64plusae.turnip.pwnedbygary.debug`.

The supplied existing watchdog dump contains historical `WD_DISPDSP` and
`WD_ERET` records, but no `DDDIAG`, `DDDISP`, or `DDSTORE` records. It samples
dispatcher PC `0x80747094`, run queue `0x807999d0`, and audio saved PC
`0x80750384`; VI/AI activity continues while the sampled frame pointer stays
unchanged. These observations do not establish an exact writer, effective
engine, or fresh native boot outcome.

The installed ARM64 core was checked and lacks `DDDIAG_COUNTS`. An attempted
Replit-built update was rejected with `INSTALL_FAILED_UPDATE_INCOMPATIBLE`;
the installed app was not replaced. Rebuild locally using the original
signing setup. Do not uninstall or clear app data to bypass the mismatch.

## Build and verify locally

From the repository root, using the existing local SDK and signing setup:

```sh
./gradlew :mupen64plus-core:externalNativeBuildDebug :app:assembleDebug --console=plain
python3 mupen64plus-core/upstream/tools/test_n64dd_dispatch_diag.py
python3 tools/verify-n64dd-apk.py app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk
```

Do not accept build success alone: verify markers in the exact packaged
ARM64 library. The checker prints the APK hash for the capture record.
Use the locally signed APK, not the Replit-generated APK. Preserve backups
of saves and configuration before testing, including external save paths.
Close the game normally before updating. `adb install -r` retains app data
and enforces matching signatures; stop if it rejects the update.

## Next evidence required

Follow `N64DD_DEVICE_VALIDATION.md` and `N64DD_DISPATCH_DIAGNOSTICS.md`.
Cold-boot legally supplied native cart+disk+IPL inputs, not the writable-cart
hack or a saved state. Preserve full startup logcat, chosen CPU/RSP/renderer,
effective engine evidence, APK/input hashes, and timestamped watchdog output.
Record what actually happens on screen and whether audio works.

For dynarec, request a fresh watchdog dump using the documented force flag.
Interpreter modes do not poll that flag; use their normal watchdog trigger.
If cached interpreter successfully runs, collect a complete dispatcher pass
including recursive delay slots. Never label a sampled dynarec boundary as
the exact writer of a shadow-detected change.

Compare findings with `N64DD_REFERENCE_REVIEW.md` and
`N64DD_WEB_REFERENCES.md`. Establish the context writer or improper reuse
before changing emulation behavior. After a minimal evidence-backed fix,
verify native DD menu/audio, plain-cart play, and writable-cart persistence
using disposable copies while retaining original saves.

The native stall diagnosis and regression verification remain incomplete.