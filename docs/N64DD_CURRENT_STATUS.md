# Native N64DD: current status and orchestrator handoff

Updated 2026-09-17. Read this before the chronological
[handoff](HANDOFF_NEW.md), [checkpoint](P08_CHECKPOINT.md) and
[detailed evidence](P08_ENTRY_NATIVE_EVIDENCE.md).
This is the current coordination document, not a claim that the repair is done.

### Current next-test instructions (2026-09-17)

Use [N64DD_NEXT_TEST_RUNBOOK.md](N64DD_NEXT_TEST_RUNBOOK.md) for the next
diagnostic package. It supersedes older next-experiment paragraphs, including the
short supplied local proposal, without replacing their historical observations.
No runtime change or new native test was performed for this runbook update.

The supplied local follow-up reports no demonstrated emulator divergence and
uncommitted diagnostic defects; its working diff/private capture were not present
in the fetched `5ebdc720d` baseline for independent verification here. A wrapped
PI ring does not prove a particular populating write was captured and then lost.
The next package must join live loader/compare evidence, dynamic staging writes,
cart PI delivery and descriptor/publication observations before requesting a
new diagnostic run. Launch identity is a prerequisite: F-ZERO X (J) cartridge,
Japanese IPL and attached NDD, using Start, not direct NDD or Resume.

The emulator repair and native/persistence acceptance remain incomplete.

### Dated evidence reconciliation (2026-09-16)

Status documents are dated evidence, not guaranteed current truth. This line was
reconciled against live Git on 2026-09-16:

- HEAD advanced to `b6dfedd9d` "Document current N64DD evidence, uncertainties and
  conditional repair gates", which sits directly on top of the previously-documented
  remote tip `85dd5faf9`. The remote-history section above (which stopped at
  `85dd5faf9`) is now superseded by this commit; both are preserved.
- Local branch `dd-eos-watchdog-checkpoint` is exactly in sync with origin (`0 ahead /
  0 behind`). No diverged parallel work to preserve on this branch.
- Working-tree state at the time of writing: `docs/N64DD_CURRENT_STATUS.md`
  (this note plus the P08d draft analysis below, which has since been committed
  and is now marked as pre-review material superseded by the reviewed records)
  and `.gitignore` (+2 lines for
  local Pi runtime state, still uncommitted). Two MSVC `*.vcxproj[.filters]` re-saved with only
  whitespace/BOM differences (no content change) — unrelated churn that must NOT be
  published into the source branch. A stray untracked `./]` file is junk, not part of
  this repo's work.
- The P08d entry-vs-fetch tool is now reviewed and published: the tool and the
  reviewed correlation section are part of commit `5b0485825`, whose appended section in
  [P08_CHECKPOINT.md](P08_CHECKPOINT.md) carries the reviewed correlation
  (exactly one all-zero-at-submission entry, zero entries non-zero at submission
  read as zeros, in both captures) with its limits. The two subsections below
  keep a pre-review provenance note as a historical record; where they differ
  from `P08_CHECKPOINT.md`, the reviewed section governs.

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

This workspace's earlier sessions had no reachable Android device. Its latest
analyzed user upload was the pointer-fix capture from 2026-09-15 at 18:13:13;
the combined test bundle was then delivered, with its user-returned capture
still pending here.

CORRECTED (2026-09-16): a device IS now attached to this workspace (Retroid
Pocket 6, Android 13, adb serial `49016109`) with the diagnostic build installed,
and a cold launch plus a root-free `run-as` + `/proc/<pid>/mem` sample of the
live emulation process were performed here — no root, no device-side script, no
APK change, no reinstall. That run reproduced the transition in its DDSTART
records with the same arguments, head samples and sites, and the same all-zero
submission hash (generation counters and some retained store `before` values do
differ; details and limits in [P09](P09_LOAD_CLEAR_PROVENANCE.md)). Statements elsewhere in this document that
assume no device is reachable describe the earlier sessions only.

The newer remote P08d handoff independently reports a run built from `6cbcae2d2`,
including four DDSTART15 call/entry records and the same zero-buffer failure.

NOTE (corrected 2026-09-16): the P08d raw log IS now present in this workspace
at `.fzxwork/p08d-capture/logcat-p08d-run1.txt` (3,130,929 bytes; sha256
c20ec502162d957ec2a09a01c316be3ecfe9e0b2039712bb98f3b25273c32321, matching the
`85dd5faf9` record). It was analyzed on 2026-09-16 with
`tools/analyze-p08d-entry-vs-fetch.py`; that analysis was reviewed and published
as commit `5b0485825`, and the reviewed result lives in the "P08d entry-vs-fetch
correlation" section of [P08_CHECKPOINT.md](P08_CHECKPOINT.md) with its limits.
The raw captures themselves remain private, untracked artifacts. Its APK
identity still differs from
the earlier bundle (hash `6f8a46d7…` vs the bundle's `fa33d400…`): do not merge
build/capture identities.
The remote note also reports log rotation, an earlier-run screenshot/CPU artifact,
and host-specific startup-suite compilation trouble. Preserve these limitations.
Before asking for another capture, obtain and analyze the available combined
records; do not duplicate a device run merely because a different session did it.
The remote document body still contains review/commit placeholders, but the
`85dd5faf9` commit metadata records reviewed snapshot hashes and an independent
PASS. That review record does not mean the raw capture was reanalyzed here.

## Newly analyzed P08d capture (written 2026-09-16; superseded by reviewed records)

Draft provenance, kept for the record: this subsection was written before
review. It is now superseded for its counts and conclusions by the reviewed
[P08_CHECKPOINT.md](P08_CHECKPOINT.md) "P08d entry-vs-fetch correlation" section
and by [P09](P09_LOAD_CLEAR_PROVENANCE.md), and the plan/BUILD guidance below
still applies. It sharpens, but does not resolve, the open root-cause
question: is the guest clear itself wrong, or legitimate with a downstream
producer/publication/consumer divergence?

### Direct observations (from `.fzxwork/p08d-capture/logcat-p08d-run1.txt`)

- 401 command-entry lines, 1604 fetch records, 4 `load_clear` records.
- Buffer `0x00411910` (KSEG0 `0x80411910`) was submitted NONZERO (~101 words)
  repeatedly from 19:34:05.305 through 19:34:12.104, then submitted ALL ZERO at
  19:34:12.130 (`data_size=0x1a0`, `hash=0x8d0350be04626145`, `nonzero_words=0`,
  `words=104`; task gen 880, `clear_halt=1`). The tool reports exactly ONE
  all-zero-at-submission entry among 401 and ZERO entries that were nonzero at
  submission yet fetched as zeros.
- The immediately preceding same-buffer entry (12.104) was nonzero (101). So the
  buffer's transition to zero is a single clean step in a long stable nonzero
  pattern, not an intermittent flip. This argues against "random corruption"
  and for a state-dependent event at this scene transition.
- Two adjacent clear-routine entries bracket the transition (all at entry PC
  `0x80747240`, expected RA `0x800aea14`): #1 at 19:34:12.102 with
  `a0=0x8012b520, a1=0x460020` (end `0x8058b540`); #2 at 19:34:12.126 with
  `a0=0x8058b540, a1=0x460000` (start == #1's end). They are contiguous:
  together they nominally clear `0x8012b520..0x809eb540`.
- `0x80411910` lies inside clear #1's nominal range at offset `0x2E63F0`.
- Exact aligned stores into `0x00411910` are at PCs `0x80747278..0x80747298`
  (the clear routine); 31 of the 32 change a non-zero word to zero and all 32
  have after-value 0 (one has before-value 0). This is direct writer evidence for
  those stores, not the full write
  history (store_summary gen 878: recent=32, dropped=592, replaced_events=46618).
- The buffer was still nonzero (101) at 12.104, ~2 ms after clear #1 was
  probed at entry. Clearing a 4.5 MB range takes many cycles, so clear #1 can
  still reach `0x80411910` (3 MB in) after that sample; this is consistent with,
  but does not prove, clear #1 being the zeroing clear.
- Retained PI DMA history: ring `overwritten=10345`; no captured transfer targets
  `dram_addr=0x00411910`. Captured cart-to-RDRAM loads landed at `0x00405xxx`
  (game assets). The PI/DD write that originally populated the audio buffer is
  lost to ring overwrite.

### Interpretation (provisional, not accepted)

- The failure is a single deterministic transition from a stable nonzero pattern
  to an all-zero submission at the gameplay-demo boundary. The two adjacent clear
  entries are the leading candidates for the zeroing clear; clear #1's range
  covers the audio buffer, clear #2's starts just past it.
- This does NOT establish that clear #1 is the defect. Two possibilities remain
  open and are NOT discriminated by this capture alone:
  (a) the clear is wrong/over-wide and clobbers the audio buffer, or
  (b) the clear is legitimate and the audio buffer should not be reused from
      that region, or a load that should have re-populated it did not run.
- SUPERSEDED (2026-09-16, see [P09](P09_LOAD_CLEAR_PROVENANCE.md)): the earlier
  reading of this subsection — that these are MIO0 decompression/workspace
  clears whose length came from an "MIO0-style length" field — is wrong. The
  executed routine is a plain `memset` called from the resource loader's
  **"head is not MIO0" fallback**, and the length passed to it is `word1` of the
  bytes read from the resource head, i.e. resource data used as a size (live:
  `a1 = 0x00460020` with head word1 `0x00460020`; `a1 = 0x00460000` with head
  word1 `0x00460000`). The adjacent-clear pairing and the ranges in the table
  above stand; the interpretation of *why* they are that wide does not.

### Newly correlated store-writer + launch chain (re-analyzed 2026-09-16)

Draft provenance, kept for the record: this subsection was written before
review and its values were re-extracted from the same `.fzxwork/p08d-capture`
log listed above. It is now superseded for counts and conclusions by the
reviewed "P08d entry-vs-fetch correlation" section of
[P08_CHECKPOINT.md](P08_CHECKPOINT.md) and by
[P09](P09_LOAD_CLEAR_PROVENANCE.md).

Direct observations (all timestamps 2026-09-15 19:34:12, DDSTART markers):

- Exactly ONE `cmd_entry` is all-zero among 401: `data_ptr=0x00411910`,
  `data_size=0x1a0`, `hash=0x8d0350be04626145`, `nonzero_words=0`, `words=104`
  at 12.130. The other 400 entries are nonzero (101 or 109 words). The two
  command buffers alternate across the run: `0x00411910` (201 entries) and
  `0x004132d0` (200 entries). The zeroed buffer is the one the game is
  currently consuming.
- The RSP launch at 12.130 (DDSTART13, sequence 880, type 2, `clear_halt=1`,
  status 0x43->0x40) carries descriptor `w12=0x00411910, w13=0x1a0` and reports
  the same all-zero buffer. The immediately preceding launch 879 (12.112) uses
  the alternate buffer `0x004132d0` and is nonzero (109 words). So the failure
  is specific to buffer `0x00411910` at this transition, not a global launch
  fault.
- The RSP source DMA that consumes this buffer (DDSTART11, record 3524, 12.130)
  reads `raw_dma_dram=0x00411910`, `requested_length=64`, with all four
  `imem_before_samples`/`imem_after_samples` = 0 and `payload_hash=0x88201fb9…`.
  The RSP read the zeroed buffer as a task source and got zeros.
- The 32 retained aligned CPU stores into `0x00411910` (DDSTART14, generation
  878, 12.130) are the zeroing writes: PCs `0x80747278..0x80747298` (the clear
  routine, including the delay-slot SW at 0x98), addresses `0x80411a30..0x80411aac`,
  31 of them changing a non-zero word to zero (one has `before=0`); all 32 have
  `after=0`. `store_summary`: recent=32, dropped=592,
  replaced_events=46618. This is direct writer evidence for those stores, not a
  complete write history.
- The two `load_clear` entries bracket the failure. Clear #1 (12.102):
  `call_pc=0x800aea0c -> target=0x80747240`, `a0=0x8012b520`, `a1=0x460020`
  (exclusive end `0x8058b540`), RA `0x800aea14`, header at `0x803da5f0=0x00460026`.
  Clear #2 (12.126): `a0=0x8058b540`, `a1=0x460000` (end `0x809eb540`), header
  `0x00460000`. They are contiguous (#1 end == #2 start) and together nominally
  clear `0x8012b520..0x809eb540` (~4.5 MB). `0x80411910` sits at offset
  `0x2E63F0` inside clear #1's range. Both headers are the resource-loader head
  words described in [P09](P09_LOAD_CLEAR_PROVENANCE.md) (`word0`, `word1`),
  whose `word1` is passed as the memset length, and both come from caller
  `0x800aea0c` -> `0x80747240` with RA `0x800aea14`.
- The store `before` values in `0x00411910` (e.g. `0x14160c80`, `0x804154d0`,
  `0x0c340000`, `0x0c800940`) are audio-command-shaped words of the same kind
  seen live in the alternate buffer `0x004132d0` fetches. The buffer held real,
  actively-launched audio data moments before it was zeroed.

Interpretation (provisional, not accepted):

- The failure is a single deterministic transition: a wide, contiguous ~4.5 MB
  resource-loader fallback `memset` (clear #1, `a0=0x8012b520`, `a1=0x460020`,
  length taken from the resource head's second word per
  [P09](P09_LOAD_CLEAR_PROVENANCE.md)) runs at the gameplay-demo transition and
  its store path lands on the live
  audio command buffer `0x00411910`, zeroing it; the guest then submits and the
  RSP consumes that buffer all-zero. The 400+ earlier nonzero launches show the
  buffer is normally intact, so the clear is a NEW/over-reaching event at this
  scene boundary rather than a constant condition.
- This does NOT yet establish that clear #1 is the emulator defect. It is
  consistent with both (a) the destination/range being wrong before the guest
  call and (b) the destination/range being correct guest state whose region
  legitimately contains an audio buffer the allocator placed inside it. Note the
  correction above: the executed routine is the resource loader's "head is not
  MIO0" fallback, so "workspace decompression clear" is no longer the right
  description. A large length alone does not prove the emulator's behaviour is
  wider or different than hardware (see the "Out-of-range clear" prohibited
  claim).

### Exact missing evidence (first target before another APK)

- The store window between the 12.104 nonzero sample and the 12.130 zero
  submission is now directly observed (32 aligned stores, PCs
  `0x80747278..0x80747298`, 31 of them before=nonzero to after=0 and all 32
  after=0, generation 878). The remaining join-key gap is NOT the
  stores themselves but confirming, WITHOUT relying on interchangeable
  generation counters (per the join-key caveat in this doc), that these 878
  stores belong to clear #1 (`a0=0x8012b520`) rather than clear #2 (`a0=0x8058b540`)
  or a later pass. The store `before` values being audio-command words (not the
  MIO0 source bytes of either clear) is supportive but not a proof of origin.
- The PI/DD write that originally populated `0x00411910` is now CONFIRMED LOST
  to ring overwrite: the retained PI history (`DDSTART16`) shows `ring_capacity=16,
  ring_wrapped=1, overwritten=10345`; all 16 retained transfers land at
  `0x00405xxx` (game assets) or `0x007c5620`, never `0x00411910`. Confirming the
  nonzero data was a real resource load therefore requires a fresh, higher-
  retention capture, not a re-read of this one. (This is exactly P09's instrumented capture: the PI DMA read path is now instrumented (unconditional on reads, address-gated on writes) so every DD-ROM and cart-origin transfer records `source_region` plus its full dram range behind the shared callback double-gate; P09 documents why the unconditional read hook is safe off-DD — implemented and host-tested, but blocked on a device NDD rom + manual **Start**, since neither is available in an unattended session. See P09 in HANDOFF_NEW.md.)
- The discriminating check remains: does clear #1's guest range (or the
  emulator's bank-mask/row-clamp result) legitimately include `0x80411910` on
  hardware, or is the emulator's clear over-wide? This needs a hardware or
  reference comparison of the clear's intended destination, which is not
  available in this workspace.

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