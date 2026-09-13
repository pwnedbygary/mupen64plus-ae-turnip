# Native DD freeze stacks using the existing Retroid root runner

## Purpose and limits

Use the existing **Handheld Settings → Advanced → Run script as Root**
facility to collect native thread stacks from the installed DDSTART9 APK.
The ordinary shell and `run-as` attempts on this device both returned
`debuggerd: root is required`; those files contained no stack frames.

This is a diagnostic procedure, not another emulator correction. Do not
change Force SELinux, unlock/root the device, reinstall the app, change CPU/
timing/rendering settings, clear app data, or delete saves for this capture.
The user has offered an already available privileged script runner.

The helper only requests stack-only `debuggerd -b` dumps from the exact
debug app's `:EmulationProcess`, after checking its PID and command line.
It requires a unique match and the same PID/process start time for both
samples; ambiguous, replaced, or reused PIDs are rejected.
It does not request a full tombstone, memory dump, broad system log or
backtraces from unrelated apps. Taking stacks can briefly pause the target.

## Files

- Source helper: `tools/capture-dd-root-stacks.sh`
- Device installation path: `/sdcard/Download/ddstart9-root-stacks.sh`
- Diagnostic directory: `/sdcard/Download/ddstart9-root-capture`

Install the script at the specified path. The background worker uses that
known path rather than assuming how the vendor runner invokes the script.
The source is supplied through a commit-pinned GitHub download with a SHA-256
check in the accompanying chat instructions.

Each invocation creates a separate capture directory, preserving earlier
logs. Only the helper's own diagnostic directories/files are made readable
for `adb pull`; it does not modify permissions on emulator files.
The `latest-complete` marker changes only after a capture finishes, including
a completed attempt that reports an error instead of stack frames.

## Device procedure

1. Download/check the helper and push it to the specified device path using
   the supplied Mac command block. ADB is at
   `~/Downloads/platform-tools/adb`; it need not be on PATH.
2. In the existing root-script menu, select `ddstart9-root-stacks.sh` from
   Download. It schedules a background capture and returns; it does not need
   to keep the Settings screen open.
3. Return to DDSTART9 with the same Japanese cart/IPL/disk and dynarec
   profile. Reproduce the manual race freeze or let attract mode reach its
   race. Once frozen, leave the emulation view open without pressing Pause,
   Exit or Home.
4. The worker waits **90 seconds** from launch, then requests two native
   stack samples separated by two seconds. A dump can take additional time.
   The Mac command waits for a new completion marker before pulling the
   diagnostic directory to Desktop.
5. Upload the new capture's `native-stacks.txt` and any accompanying status/
   error information. An empty, denied or missing-process result is a failed
   observation, not proof of a particular emulation fault.

If the script menu does not return or shows an error, preserve the message.
Do not try permission, SELinux or firmware changes as a workaround. If the
capture occurs before the game freezes, label it accordingly; do not
describe a normal-running stack as a frozen one.

The Mac wait is bounded. If no completion arrives within ten minutes, it
reports a timeout rather than silently pulling an older successful capture.
Older diagnostic captures are not deleted.

## Interpretation

Retain raw frame PCs, thread names and module build IDs. Compare the two
samples to determine whether the emulation thread is in Parallel RSP,
graphics/Vulkan synchronization, a native lock, or CPU/generated code.
A single sampled frame alone is not proof of a permanent hang.

Matching unstripped ARM64 DDSTART9 libraries are preserved locally in
`build-downloads/DDSTART9-symbols/arm64-v8a/`; build IDs are recorded in
`docs/DDSTART9_RUNTIME_ANALYSIS.md`. Do not symbolize against a later build
merely because it has the same upstream version label.

Host helper tests exercise mocked Android commands and isolated diagnostic
paths; they never dump real processes as root. They establish helper logic,
not success of the vendor runner, stack collection, or native DD gameplay.
The delivered helper passed POSIX shell syntax checks and 52 host assertions,
including both root gates, identity changes/reuse/ambiguity, bounded dump
calls, failure output, detached launch, and completion-marker timing.