# DDSTART10: DD-only RSP generated-code provenance

This is a diagnostic build, not a stall correction. The completed DDSTART9
capture identifies Parallel-RSP generated-code ownership, but not the offending
guest instruction or corrupting writer. See
[DDSTART9_RSP_OWNERSHIP_ANALYSIS.md](DDSTART9_RSP_OWNERSHIP_ANALYSIS.md).

## Scope and gating

Keep DDSTART9's CPU block-boundary correction unchanged. Do not alter RSP
instruction semantics, compiler cache reuse/invalidation, memory protections,
watchdogs, timing settings, or writable-cart behavior.

The core supplies its existing debug callback to an optional RSP-plugin
diagnostic setter only when the existing explicit per-game DD diagnostic gate
is active. An older RSP plugin need not export the setter. Connect/start applies
the gate; disconnect, ROM close, plugin startup and shutdown clear/reset it.

DD-disabled execution performs no provenance-map insertion or compile-word
capture. No per-game name is hardcoded; independently enabled WritableROM does
not enable these observers.

## Records

- `DDSTART10 RSP jit_region`: exact emitted host `[start,end)`, guest IMEM start
  PC, instruction count, existing region hash, and commit/cache-hit event.
  End means emitted code end, not page-aligned allocator padding.
- `DDSTART10 RSP jit_compile_input`: instruction words from the same IMEM array
  used by that compilation. `region_host_start` identifies the region; each
  chunk has its own advancing IMEM PC. It is not a linear mapping from guest
  instructions to ARM64 instruction offsets.
- `DDSTART10 RSP entry`: task descriptor hash, full IMEM hash, task type, initial
  SP PC, and entry/return counters. The IMEM hash is cached and invalidated by
  detected IMEM changes. Every enabled, non-HALT/BROKE entry is counted, even
  when its identity is already known or the log budget has expired.
- `cache-range-unavailable`: a cached region predates diagnostic provenance.
  Do not invent its extent or force recompilation to obtain it.

Budgets per session:

- 2,048 range records, including unavailable-range observations.
- 32,768 compile-input words, at most 64 per line.
- 128 distinct task/IMEM identities. Previously seen A/B/A/B identities do not
  repeatedly consume the identity budget.
- One explicit exhaustion marker per class.

Compile words are emitted on DD-enabled compilation, not repeated on cache
hits. Capture from before game startup so those early records are retained.
Record budgets are coverage limits, not evidence that later execution stopped.
An entry record alone is not proof that its invocation never returned: unchanged
identities do not produce per-call log lines.

## APK and host verification

- File: `build-downloads/DDSTART10-debug.apk`.
- SHA-256: `bcd78f4d0126c8d2fc65fef0f60fb6c6230bf531b3732cf17c964fcfef2139ba`.
- Package: `org.mupen64plusae.turnip.pwnedbygary.debug`.
- Version code: 335; label `3.0.335 (beta) b11dfd70`.
- Signing certificate SHA-256:
  `311f4e35e939256ae8df53ecbf15c43235d1ee97f8a0138d332839c5e53c2bfc`,
  matching DDSTART7/8/9.

The startup host suite and focused RSP observer tests passed. Review identified
and corrected DD-off bookkeeping, padded rather than exact ranges, unmatched
early return counters, and compile-word chunk PC attribution. The final focused
test checks the second chunk of a 65-word region.

All-ABI debug assembly passed. The first offline attempt lacked the Gradle
dependency cache; online resolution succeeded. Final assembly after the chunk-PC
correction passed offline in 27 seconds.

All four packaged ABIs contain the unchanged DDSTART9 boundary marker, optional
core bridge and DDSTART10 RSP markers. Matching unstripped ARM64 symbols are
preserved locally under `build-downloads/DDSTART10-symbols/arm64-v8a`:

| Library | Build ID |
|---|---|
| Core | `1a2445de81d9f09b06ff75ba1de99cd806adced9` |
| Parallel-RSP | `d6067d8280dfe01ebea0e3080f2eb8bd47f9e183` |
| Parallel video | `ec46070d8a937f09dba31840afcae98129b8d615` |

No reachable Android device exists in this workspace. These checks do not
establish native DD menu/gameplay/audio success or cart/save regression results.
APK, symbols, signing material and uploaded traces are not source-publication
content.

## Mac capture

`tools/capture-dd-rsp-mac.sh` accepts the APK path and its required SHA-256.
It uses `~/Downloads/platform-tools/adb`, verifies the file, installs with
`install -r`, and starts filtered Core logcat before reproduction. It does not
uninstall, clear data, change SELinux, or change game settings.

The existing device scripts are unchanged:

- Root-menu entry: `/sdcard/Download/ddstart9-run-as-root.sh`.
- Worker: `/sdcard/Download/ddstart9-root-stacks.sh`.
- Output root: `/sdcard/Download/ddstart9-root-capture`.

The Mac helper snapshots existing capture directories before prompting the user
to run the root entry. It identifies a unique newly created capture and waits
on **that directory's own status**, never the shared `latest-complete` marker.
Discovery/status poll budgets are 10/6 minutes. Ambiguity is reported rather
than silently choosing a directory. Available files are retained on incomplete
retrieval, and only the helper's own logcat process is stopped.

On the handheld, run the existing root entry, immediately open the same DD game
with the same dynarec profile and inputs, reproduce the stall and leave it open.
The existing root worker waits 90 seconds before beginning its metadata/dumps.
The Mac helper creates a fresh Desktop ZIP containing the filtered logcat,
capture files and retrieval diagnostics.

The Mac orchestrator's mocked success/race/timeout/cleanup tests passed
(14 assertions), and both shell files pass Bash syntax checks. Actual Mac/ADB
execution remains device-side validation.

## Next analysis and acceptance

Match the new stack PC to a same-process `jit_region` host interval, collect
that region's compile-input chunks by `region_host_start`, and analyze the
guest IMEM loop with task context. Check exhausted/unavailable coverage before
drawing conclusions. A PC within a region does not identify an exact guest
instruction without examining the compilation mapping.

Only after establishing a faulty writer, improper reuse or a demonstrated
emulation error should a minimal correction follow. Native DD menu/gameplay/audio,
plain-cart behavior and writable-cart persistence remain open acceptance checks.