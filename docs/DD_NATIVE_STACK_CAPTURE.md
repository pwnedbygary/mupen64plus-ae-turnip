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

The helper requests stack-only `debuggerd -b` dumps plus process-map and thread
accounting metadata from the exact debug app's `:EmulationProcess`, after
checking its PID and command line.
It requires a unique match and the same PID/process start time for both
samples; ambiguous, replaced, or reused PIDs are rejected.
It does not request a full tombstone, memory dump, broad system log or
backtraces from unrelated apps. Taking stacks can briefly pause the target.
Memory maps list address ranges, permissions, and mapped file names; they do
not contain the mapped memory contents. Thread accounting records help
distinguish a CPU-consuming loop from waiting without assuming a fixed tick rate.

## Files

- Source helper: `tools/capture-dd-root-stacks.sh`
- Device installation path: `/sdcard/Download/ddstart9-root-stacks.sh`
- One-line menu launcher: `tools/launch-dd-root-stacks.sh`
- Launcher device path: `/sdcard/Download/ddstart9-run-as-root.sh`
- Appended startup output: `/sdcard/Download/ddstart9-root-launch.log`
- Diagnostic directory: `/sdcard/Download/ddstart9-root-capture`

Install both files at their specified paths. Select the **one-line launcher**
in the vendor menu, not the multiline helper. The launcher explicitly starts
`/system/bin/sh` to parse the complete helper in one shell session. The
background worker also uses the known helper path.
The source is supplied through a commit-pinned GitHub download with a SHA-256
check in the accompanying chat instructions; the one-line launcher can also
be created verbatim with the supplied Mac `printf` command.

The first device attempt displayed **538 steps** followed by **Script ran**
without producing a capture. This is consistent with a line-oriented command
runner, but its implementation has not been inspected. Do not interpret that
UI message as evidence that the full helper was parsed, a worker started,
or a stack dump succeeded.

Each invocation creates a separate capture directory, preserving earlier
logs. Only the helper's own diagnostic directories/files are made readable
for `adb pull`; it does not modify permissions on emulator files.
The `latest-complete` marker changes only after a capture finishes, including
a completed attempt that reports an error instead of stack frames.

## Device procedure

1. Download/check the helper and push both files to their device paths using
   the supplied Mac command block. ADB is at
   `~/Downloads/platform-tools/adb`; it need not be on PATH.
2. In the existing root-script menu, select **`ddstart9-run-as-root.sh`** from
   Download. This is a single command invoking the helper. A successful
   helper startup schedules a background capture and returns; it does not
   need to keep the Settings screen open.
3. Return to DDSTART9 with the same Japanese cart/IPL/disk and dynarec
   profile. Reproduce the manual race freeze or let attract mode reach its
   race. Once frozen, leave the emulation view open without pressing Pause,
   Exit or Home.
4. The worker waits **90 seconds** from launch, then takes two sample groups,
   with a two-second pause between groups. Each group gathers process maps and
   thread metadata before its native stack dump. Collection takes additional
   time; use recorded timestamps rather than assuming exactly two seconds
   between CPU-accounting readings.
5. Upload the new capture folder including stacks, both map snapshots, both
   thread-accounting snapshots, metadata, and status/error information. An
   empty, denied or missing-process result is a failed observation, not proof
   of a particular emulation fault.

If the script menu does not return or shows an error, preserve the message.
If no capture appears, retrieve `ddstart9-root-launch.log` from Download:
it retains helper startup errors even before a capture directory exists.
The capture directory's separate `launcher.log` retains worker launch errors.
Do not try permission, SELinux or firmware changes as a workaround. If the
capture occurs before the game freezes, label it accordingly; do not
describe a normal-running stack as a frozen one.

The accompanying Mac commands poll for a changed completion marker from the
Mac rather than keeping one long-running remote shell open. If no new completion
arrives within ten minutes, they report that explicitly and still retrieve
available diagnostics, rather than treating an older capture as a new success.
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
The stack-only version passed POSIX shell syntax checks and 58 host assertions,
including both root gates, identity changes/reuse/ambiguity, bounded dump
calls, failure output, detached launch, completion-marker timing, and the
one-line entry point in a simulated line-oriented runner.

Device follow-up: the uploaded archive now confirms two successful captures,
four real backtraces, and completed markers after the one-line entry was used.
See [DDSTART9_NATIVE_STACK_ANALYSIS.md](DDSTART9_NATIVE_STACK_ANALYSIS.md).
Because the user closed/restarted that process, the enhanced helper collects
fresh stacks alongside mapping metadata and thread CPU accounting. Old anonymous
addresses must not be paired with a new process's maps. No APK change is needed.

The enhanced helper reports `ddstart9-root-stacks-v2` in `run-metadata.txt`.
New files are `sample-1-maps.txt`, `sample-2-maps.txt`,
`sample-1-threads.txt`, and `sample-2-threads.txt`. Each metadata read has
timestamps and an explicit outcome. The cap is 128 threads, 1 MiB per maps
read, and 8 KiB per thread-file read; exceeding a bound or a required-read
failure marks the capture `complete_with_errors` while preserving available
stacks and metadata. Raw tick counters and reported `CLK_TCK` are retained;
the helper does not assume a tick rate or calculate CPU utilization.

Verification: the enhanced helper/test scripts pass shell syntax checks and
73 host assertions, including process identity, spaced/parenthesized proc-stat
names, metadata failures, thread bounds, and the unchanged one-line launcher.
Actual enhanced metadata collection remains to be verified on the device.