# Native DD evidence and device validation

## Status (2026-09-11)

The latest section of `HANDOFF.md` is the historical starting point, not a
fresh reproduction. It reports loader completion following the DD RSP
PC-persistence fix, then a logo/silence stall with repeated dispatch of the
audio-thread context. The corrupting writer or improper context reuse remains
unproven. Older claims of a missing disk interrupt or stalled loader are not
the current diagnosis.

This workspace has only the two supplied source archives in `attached_assets`.
No game, IPL, device trace, or saved-state input was supplied separately.
The archive's historical references to device dumps do not make those dumps
available. A local `adb devices -l` check returned an empty device list.
No native boot, menu, audio, or save-persistence behavior was reproduced here.
APK compilation is not evidence of those behaviors.

The user subsequently connected the Android device to a local MacBook.
It is still not visible to workspace ADB; the Mac reported `adb: command not
found`. Install Google's SDK Platform Tools for Mac, unzip, and run
`./adb devices -l` from the extracted `platform-tools` directory. This avoids
requiring a PATH change. Enable USB debugging and accept the device prompt.
Keep ADB local; do not expose its server or the device to the public internet.

The final instrumented `./tools/replit-build.sh` also completed successfully
(exit 0). The four source-invariant diagnostic tests passed. These checks
verify buildability and instrumentation structure, not actual guest execution.

## Required inputs

- An authorized Android device (the historical handoff calls it RP6), with
  Android version, SoC/ABI, and chosen renderer/RSP recorded.
- Legally supplied F-Zero X Japanese cartridge, Expansion Kit disk, and
  matching DD IPL. Record hashes, region, disk format, and byte order locally;
  do not substitute downloaded game files or assume filenames establish identity.
- Backed-up saves and configuration, plus the actual installed APK hash.
  Do not uninstall or clear app data merely to run a comparison.

## Capture procedure

1. Install the debug APK as an update only when package/signature compatibility
   has been checked. Determine its package from the built manifest rather than
   using inconsistent package spellings in historical notes.
2. Cold-boot the native cart+disk path, not the writable-cart hack and not an
   old save state. Record all CPU/RSP/renderer choices and confirm the effective
   mode in the logs. Source enum values are pure interpreter=0, cached
   interpreter=1, dynarec=2 (`r4300_core.h`); that is not proof that every mode
   successfully executes this title. Historical target runs used mode 2.
3. Enable and collect the bounded diagnostics described in
   `N64DD_DISPATCH_DIAGNOSTICS.md`, retaining full startup logs and the complete
   dispatcher pass, not just the last stall line. Capture context/state changes,
   queue selection, running-thread changes, EPC restore and ERET destination
   together. Distinguish sampled change intervals from exact writer evidence.
4. Retain logcat, diagnostic output and any authorized RDRAM capture with the
   APK/input hashes and timestamps. Record whether loader completion, menu
   input and clean audio actually occur.
5. Compare a different engine only after confirming its effective mode and
   successful startup. A crash or silent fallback is not an engine comparison.

## Regression checks before accepting a future fix

Native DD: cold boot through loading to interactive menu and clean audio.
Plain cart: boot/play with DD absent. Writable-cart: modify a disposable copy's
save, close normally, reopen and verify persistence; compare save hashes and
preserve the original backup. Keep the working writable-ROM repair separate
from native DD diagnosis.

No guest state repair or DD controller change is justified by compilation or
source comparison alone. First identify the relevant writer/reuse sequence in
fresh evidence, then test the smallest correction against all three paths.