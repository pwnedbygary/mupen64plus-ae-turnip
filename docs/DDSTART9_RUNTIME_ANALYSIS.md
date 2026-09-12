# DDSTART9 native result: boot fault resolved, later freeze remains

## Input and user observations

Capture: `ddstart9-logcat_1789251365596.txt`, 20,475 lines / 2,705,972 bytes.
SHA-256:
`7d9bfd2b1cc896a7f34dde4cbcaebbf68d9d1e6493868998122c50cbe280bc02`.
Raw device logs are not published.

The user reports that the Japanese cartridge + Japanese IPL + Expansion Kit
disk now reaches the menu, still with visual/text corruption, and freezes
at the same gameplay transition as cached interpreter. They also tested
USA cartridge/IPL combinations that stayed black, then switched to the
Parallel 2x profile to verify dynarec was selected.

Audio behavior and a new menu screenshot were not supplied with this capture.

## Engines and session identity

**All five native launches explicitly report Dynamic Recompiler**, followed
by the new-dynarec initialization and DDSTART9 boundary marker. There was
no forced cached-interpreter fallback.

| PID | Core start | Cartridge | DDSTART8 coherence | DDSTART4 |
| --- | --- | --- | ---: | ---: |
| 16082 | 18:06:26.149 | F-ZERO X (J) | 32 | 512 |
| 16293 | 18:07:01.361 | F-ZERO X (J) | 32 | 512 |
| 17191 | 18:08:51.176 | F-ZERO X (U) | 0 | 61 |
| 17385 | 18:09:26.833 | F-ZERO X (J) | 32 | 512 |
| 17517 | 18:09:43.372 | F-ZERO X (J) | 32 | 512 |

Times are the device's 2026-09-12 log times. The Japanese cartridge MD5 is
`58D200D43620007314304F4E6C9E6528`; USA is
`753437D0D8ADA1D12F3F9CF0F0A5171F`. An initial chat summary incorrectly said
all five cartridges were Japanese; the table above is the corrected count.

All five launches use Parallel video and Parallel RSP, explicit DD support,
configured IPL/disk, no requested autoload, and reported count-per-op 1 /
denominator 0. These are observed settings, not instructions to change them.
PID 17517 explicitly logs `Enabling upscaling: 2x`; an upscaling/profile name
does not determine the effective CPU engine.

The USA run has a Japanese disk but does not reach the same Japanese boot
addresses. Its shorter workload is separate from the four Japanese menu/
gameplay cases. The capture does not identify which cached IPL bytes belong
to each reported IPL selection, so it cannot validate both USA IPL trials
or establish their compatibility from filenames alone.

## Native confirmation of the DDSTART9 correction

Every Japanese run reproduces the same sequence:

1. Generation 395 compiles `800bb540` as **79 words**, ending at `800bb67c`.
   Copied/current hash: `e3fc8a5a`, matching.
2. The boot page is invalidated.
3. Generation 397 compiles the rewritten callee at `800bb67c`, 199 words.
   Copied/current hash: `e5d7aaf3`, matching.
4. Subsequent modification is followed by dirty-copy rejection and new
   compilation at `800bb970` (generation 407, 10 words, hash `9540b8dc`) and
   `800bb9a0` (generation 408, 20 words, hash `0f16227c`).

Each Japanese coherence set comprises four compilations, four invalidations
and 24 dirty verifications (nine matches / fifteen rejects). These are
native results, not merely the earlier host boundary fixture.

No run contains the old `800ad49c` / `800ad4ac` / `079bb080` fault path,
DDSTART8 fault snapshot, or filter record. The old exact-fault observer and
its independent budget remain enabled. Together with the positive corrected
compilation sequence, this confirms resolution of the previously identified
stale encoded boot-call fault. Retain DDSTART9 unchanged.

This does **not** establish successful native gameplay, audio, complete
display correctness, or save regressions.

## What the later freeze evidence does and does not show

The existing detailed startup probes do not cover the reported transition.
Japanese DDSTART4 records exhaust their 512-record budget; late DDSTART8
coherence records end around the first second. DDSTART3's last emitted
progress ordinal is 16,384. There are no DDSTART5–7 later context records.
None of those absences demonstrates that disk/CPU activity ceased there.

The new boot progress exposes warnings for ASIC commands `00060000`,
`00070000`, and `00150000`. They are not proof of the next cause. In the core,
the first two are standby/sleep commands; command completion, error sense,
mapping/bounds, and interrupt behavior would need transition-specific
evidence before any corrective change.

Both engines now exhibiting similar user-visible symptoms points toward a
shared path but does not prove a renderer, RSP, disk, IPL/font, or CPU-timing
defect.

### Shutdown retries are not guest progress

Japanese runs 16082 and 17385 complete their exit saves and stop. Runs 16293
and 17517 start autosave after the user's Pause/Exit sequence but do not
complete it or terminate natively in the recorded interval.

In the latter runs, frontend state callbacks repeatedly alternate parameter
1 between 2 and 3 (RUNNING / PAUSED). The source explains these without guest
execution:

- `CoreService.autoSaveState(true)` queues a save and starts the FPS-change
  shutdown checker.
- The checker retries shutdown/stop every 500 ms; a separate frontend task
  also reapplies pause.
- `main_stop()` clears pause and publishes RUNNING before setting the
  emulation stop flag; the pause task subsequently publishes PAUSED.
- The emulation thread must return to an interrupt/safe boundary to process
  the stop/save request.

Thus the alternating callbacks are shutdown retries after the freeze, not
its established cause or a valid CPU heartbeat. Persistent failure to process
the stop/save makes a shared synchronous native/plugin blockage a stronger
candidate than an ordinary guest wait loop, but a CPU dispatch/interrupt
problem remains possible.

## Next observation: native stacks, no new APK

Reproduce with the same Japanese cart/IPL/disk and unchanged dynarec profile.
While the gameplay transition is frozen, **before pressing Pause or Exit**,
capture two all-thread native backtraces about two seconds apart.

The actual process name in this capture is:
`org.mupen64plusae.turnip.pwnedbygary.debug:EmulationProcess`.
Do not accidentally target only the launcher process.

Android documents `debuggerd -b PID` as a stack-only dump of a running
process, rather than a full tombstone:
<https://source.android.com/docs/core/tests/debug#tombstone>.
Device permissions can block it; retain any error rather than rooting,
reinstalling, clearing data, or changing emulation settings.

The decisive distinction is whether the emulation thread remains inside
Parallel RSP, a graphics/Vulkan/futex wait, or CPU/generated-code execution.
Preserve PCs and module build IDs for symbolization. Also obtain a screenshot
of the corrupted menu and a description of audio behavior.

Matching ARM64 unstripped libraries with DWARF were preserved locally at
`build-downloads/DDSTART9-symbols/arm64-v8a/`. Their build IDs were checked
against the libraries inside the delivered DDSTART9 APK:

- Core: `33d7a5d3249a26db91ba0769617c85ee13322176`
- Parallel RSP: `0019cf78c5242d92cd0d92b362b2e3e8e9715c88`
- Parallel video: `ec46070d8a937f09dba31840afcae98129b8d615`

These local binaries are not part of the source publication.

Only if native stacks cannot be obtained should a new DD-gated diagnostic
publish atomic RSP/video entry/exit state plus an interrupt heartbeat for
reading outside the blocked emulation thread. Do not asynchronously read
dirty guest registers or replace this with another exhausted startup log.