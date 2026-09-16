# Native N64DD: current status and orchestrator handoff

Updated 2026-09-16. Read this before the chronological
[handoff](HANDOFF_NEW.md), [checkpoint](P08_CHECKPOINT.md) and
[detailed evidence](P08_ENTRY_NATIVE_EVIDENCE.md).
This is the current coordination document, not a claim that the repair is done.

## Executive state

The native DD scene transition remains unresolved. Dynarec is required;
cached interpreter is a secondary correctness comparison, not a workaround.
The reported experience is garbled menus with working music, then a dissolve,
audio cutoff and black screen at the automatic gameplay demo. Starting a race
manually reportedly fails similarly. The base cartridge works according to
the user; final regression acceptance on a repaired build remains outstanding.

The immediate zeroing mechanism is much better established than the underlying
defect: a guest clear routine executes over memory containing the audio command
buffers; exact routed stores change retained words to zero; a subsequent audio
launch presents an entirely zero buffer before RSP consumption. We do NOT yet
know whether the clear is incorrect, or legitimate and followed by stale reuse.

There is no evidence-qualified emulator fix to apply yet. The proposed strategy
is to correct the first proven emulator/reference divergence in resource loading,
address handling, producer/publication state or consumption—not to suppress the
guest clear. The diagnostic package below is intended to compare those
possibilities together, rather than request a device run for each missing field.

## Repository and parallel-work synchronization

- Repository: `pwnedbygary/mupen64plus-ae-turnip`.
- Active branch: `dd-eos-watchdog-checkpoint`; do not force-push.
- Combined diagnostic source: `6cbcae2d2223a2016458b23d69f22a49ea83091c`.
- Remote subsequently advanced to `7ee30ad26` (test executable bit) and
  `85dd5faf9` (independent native confirmation documentation). Preserve both.
- The combined implementation is already upstream: partial call snapshots,
  validated source/stack samples, PI history, audio lifecycle, production build
  entries, tests and supporting documentation.
- The imported workspace has a different history. Publish reviewed source/docs
  only on the freshly verified live remote parent. Never push its whole history.
  Preserve exact raw blob bytes, modes and the expected tree hash; use a non-force
  ref update and stop if the parent advances.
- APKs, signing files, SDK caches, private captures, game images, save files and
  workspace metadata are intentionally NOT source-repository deliverables.
  An apparently different local tree is not permission to overwrite remote work.

### Distinct native evidence streams

This workspace has no reachable Android device. Its latest analyzed user upload
was the pointer-fix capture from 2026-09-15 at 18:13:13; the combined test bundle
was then delivered, with its user-returned capture still pending here.

The newer remote P08d handoff independently reports a run built from `6cbcae2d2`,
including four DDSTART15 call/entry records and the same zero-buffer failure.
Those raw private logs are not available in this workspace. Treat that as
published parallel-session evidence, NOT as our re-analysis of a returned ZIP.
Its APK hash differs from the bundle below: do not merge build/capture identities.
The remote note also reports log rotation, an earlier-run screenshot/CPU artifact,
and host-specific startup-suite compilation trouble. Preserve these limitations.
Before asking for another capture, obtain and analyze the available combined
records; do not duplicate a device run merely because a different session did it.
The remote document body still contains review/commit placeholders, but the
`85dd5faf9` commit metadata records reviewed snapshot hashes and an independent
PASS. That review record does not mean the raw capture was reanalyzed here.

## Evidence ledger

### Direct observations from the analyzed captures

1. Launch 878 / RSP generation 1510: buffer `0x00411910`, 416 bytes,
   101/104 words nonzero. Launch 879 uses the alternate `0x004132d0`.
   Launch 880 / generation 1512 presents the first buffer entirely zero.
   Its subsequent offset-zero 64-byte fetch returns zeros. Suspect source-zero
   DMA requests follow. This places the zero content before that RSP read.
2. Exact ARM64 routed-store evidence retains 32 aligned SWs over
   `0x80411a30–0x80411aac`. All after-values are zero; 31 change nonzero to zero.
   PCs are within `0x80747278..0x80747298`, including the delay-slot SW at `98`.
   This is exact writer evidence for those stores, NOT a complete write history.
3. Earlier coherent store contexts include RA `0x800aea14`,
   SP `0x80796c10`, and A3 `0x8058b540`. A1=0 inside the bulk loop is a
   normal remainder and does not prove a zero original length.
4. Pointer-fix capture: two live clear-entry snapshots at `0x80747240` with
   expected RA `0x800aea14`:

   | A0 | A1 length | Calculated exclusive end |
   |---|---|---|
   | `0x8012b520` | `0x460020` | `0x8058b540` |
   | `0x8058b540` | `0x460000` | `0x809eb540` |

   The first range contains both audio command buffers. The second extends past
   8 MiB, but range arithmetic alone does not establish incorrect mapping.
   That capture had two entry records, no call/header records, no store-context
   records, and no repeated startup fatal signal. All 66 manifest hashes passed.
5. The prior diagnostic crash was a demonstrated host-pointer truncation:
   a 64-bit snapshot pointer was emitted through a 32-bit immediate helper.
   Both paths now use a full-width literal load. No recurrence in the analyzed
   pointer-fix run is a native observation, not a universal no-crash guarantee.

### Static facts and derived interpretation

- Correct-coordinate archived RDRAM contains a 32-byte zero loop, not 36:
  pointer increment at `0x80747274`, eight SWs ending in the branch delay slot.
- Candidate caller `0x800aea0c` contains JAL to `0x80747240`; the delay slot
  sets A1=S0. A0 was loaded from `[sp+0x3c]`. The preceding comparison uses
  `MIO0` (`0x4d494f30`), with the mismatching path reaching the clear.
  Archived code is not proof that a fresh run executed that decision.
- An older memory window was displaced by `+0x1000`. Differences at identical
  file offsets do not establish overlay evolution. Validate guest coordinates.
- Entry-only call-PC/opcode/target labels are supplied diagnostic constants,
  not an observation of the caller. Exact compiled-JAL call records have a
  different provenance. Keep these forms separate.
- Compile generation, watch generation, launch sequence and RSP/fetch generation
  are not interchangeable join keys. Writer timestamps are flush times.

## Combined test: what it captures and what it cannot prove

The source at `6cbcae2` adds:

- Partial DDSTART15 records with per-field validity and rejection telemetry.
  Invalid register fields print zero; zero is NOT a live value unless valid.
  Allocator-known constants can be materialized without reading stale hot state.
- Bounded validated 64-byte source samples and raw stack words at offsets
  `0x38/0x3c/0x4c/0x58`. Raw stack values are not automatically pointers,
  resource identifiers or semantic arguments.
- Call/entry acceptance precedes PI-history flush. Valid-but-wrong RA rejects;
  missing RA is partial evidence, not verified linkage. Full callee-entry argument
  claims require all necessary valid fields and the expected link.
- DDSTART16 recent PI-to-RDRAM history: up to 16 transfers with sequence,
  addresses, requested length, bounded guest-order before/after samples and
  explicit clipping/overwrite accounting. Requested length is not a measured
  exact completed transfer length. Prefix samples are not whole-transfer proof.
- Seven early history flushes and one separately reserved zero-audio flush.
  Later suppressed events remain coverage limitations, not absent operations.
- Last-four audio-launch descriptor history alongside DDSTART12/13 and the
  retained DDSTART14 writer observations.

PI history is chronological/address-based evidence, not ownership tracking or
a shared generation clock. It is not a complete DD resource/decompression trace.
CPU writer coverage still excludes unaligned, TLB/physical-low alias, DMA,
other-ABI and page-spanning delay-slot cases. Clear entry at internal nonzero
block indices remains a coverage gap. Rings are bounded and may overwrite data.
Do not infer absence of writes or transfers from missing records.

## Next analysis procedure — one combined pass

1. Verify archive integrity, APK/source identity and signing identity. Inspect
   fatal signals, process/session boundaries, effective CPU engine, DD activation,
   log truncation and marker coverage before interpreting data. Do not equate
   collector completion with a healthy emulator run.
2. Use the first zero-audio boundary as the correlation point. Gather its previous
   same-buffer launch, intervening alternate launch, clear calls/entries, exact
   stores, PI history and raw source/stack samples. Keep run IDs separate.
3. For each DDSTART15 record, decode validity before using any field. Establish
   the executed call and original range only where provenance supports it.
   Compare valid header bytes/comparator values against the archived branch,
   without assuming T8 necessarily still owns a particular loaded resource.
4. Compare recent PI destinations and before/after bytes to the validated
   header/source address. Record overlap, chronology, clipping and ring losses.
   A matching prefix alone does not prove a complete successful resource load.
5. Determine whether the observed clear belongs to a normal scene transition,
   whether its arguments derive from incorrect loaded state, and whether the
   audio producer republishes or retains a cleared buffer. Descriptor reuse
   alone is not a defect. The remote P08d report finds both sizes routinely
   paired with both buffer pointers; do not revive fixed pointer/size pairing
   as a defect without contrary evidence.
6. Write a table separating observations, calculations, hypotheses and missing
   proof. If evidence remains insufficient, identify the exact missing boundary
   and first exhaust the existing combined data, reference code and host
   differential fixtures. Avoid another APK merely to inspect one extra field.

## Proposed fix strategy and decision gates

| Supported finding needed | Candidate correction direction | Proof still required |
|---|---|---|
| Wrong bytes first appear at PI/DD delivery | Small correction to the proven DMA/address/length/ordering divergence | Valid source bytes, expected destination semantics, pinned reference and failing production-path fixture |
| Clear arguments are wrong before guest call | Correct the demonstrated upstream load, register or execution divergence | Live valid arguments, their provenance and a reference comparison; a large length alone is insufficient |
| Clear is legitimate but an invalid buffer is subsequently published/consumed | Correct the demonstrated producer/publication/consumer-state divergence | Ownership/lifetime and expected behavior; no forced buffer refill or skipped task |
| Core behavior outside 8 MiB differs from hardware/reference | Narrow address-semantics correction only after differential proof | Current slow/open-bus behavior and pinned hardware/reference mapping; never assume modulo-8-MiB wrapping |
| Data is correct but decode differs | Target the demonstrated decoder/JIT divergence | Correct input plus executed decode evidence; zero input alone does not establish this |

Do not patch the game-specific clear address or header branch. Do not force a
MIO0 match, suppress zeroing, fake completion, restore old speculative timing,
force yields, or mask the symptom with cached interpreter. No candidate above
is yet an approved runtime correction. A defensible minimal repair requires the
first demonstrated divergence and a failing fixture before the change.

## Non-negotiable preservation and verification

- Preserve DDSTART9 and the pinned internal Parallel-RSP IMEM bank/row correction.
- Gate diagnostics and new behavior on explicit per-game DD activation.
  Corrected behavior must work when logging/callbacks are disabled.
- Preserve independently enabled WritableROM, DD-disabled games, stored saves,
  save migration and signing identity. Never uninstall or clear app data.
- Follow [portable reference handling](N64DD_DMA_AUDIO_REPAIR_PLAN.md#portable-reference-handling),
  the evidence review prompt and validation runbook before selecting hardware
  semantics. Historical `doc/N64DD_REFERENCE_REVIEW.md` and
  `doc/N64DD_WEB_REFERENCES.md` are absent from this checkout; do not invent their
  contents or treat these historical names as current files.
- Host suites, emitter fixtures, ARM64 syntax and full Android assembly passed
  for the combined package in this workspace. Independent source review passed.
  These do not prove native menu rendering, music, gameplay or save persistence.
- After a real correction: verify fresh native DD menus/music, automatic demo
  and manual race on dynarec; plain-cart control; independently enabled
  WritableROM persistence across restart; DD-off routing and policy behavior.
  Cached-interpreter dispatcher coverage remains a secondary outstanding check
  if that mode is run. Do not mark the overall investigation complete yet.

## Delivered bundle and repeatable capture

The following identity values were recorded during workspace packaging and
reported in the delivery conversation. They are not a verification of what is
currently installed on the device; verify that separately for each capture.

- Bundle: `P08-Combined-Transition-Test.zip` (delivered separately, not in Git).
- APK: `P08-combined-6cbcae2-debug.apk`.
- APK SHA-256: `fa33d40047883b2ac7aad95df086e1fbf89d59f23607ec29766da8d2be6fb490`.
- Signing SHA-256: `311f4e35e939256ae8df53ecbf15c43235d1ee97f8a0138d332839c5e53c2bfc`.
- Package: `org.mupen64plusae.turnip.pwnedbygary.debug`.
- Build command: `./gradlew :app:assembleDebug --console=plain`.
- ARM64 syntax uses NDK `26.1.10909125` under `.android-sdk/ndk/`.

Mac ADB is `~/Downloads/platform-tools/adb`; do not assume PATH. Install with
`adb install -r`. Force-stop loses unsaved session progress but preserves stored
saves. Use `tools/capture-p08-mac.py --duration 60 --manual-start`: select
F-ZERO X (J) → Start, not Resume; leave controls alone through automatic demo.
Do not use `--memory`: the watchdog remains unreliable and memory capture is
not needed for this package. UIAutomator is unreliable on this handheld.
Present terminal commands in one copy-pastable block, one physical line each.

## Claims explicitly prohibited

- “Fixed,” “native acceptance passed,” or “task complete.”
- “The clear is the root bug” merely because it zeroes the audio buffers.
- “The MIO0 header failed in this run” from static code or entry constants.
- “A1=0 means original length zero”; “sampled PC identifies the writer.”
- “The ring contains every write”; “no record means no event.”
- “Out-of-range clear proves wrapping must be implemented.”
- “Two runs share identity/counters” because their hashes or addresses match.
- “Independent remote native evidence was verified here” without its raw data.
- “The next capture is guaranteed to identify the repair.”

The immediate deliverable is a defensible, evidence-based choice of minimal
correction—not additional diagnostic volume or a premature success claim.