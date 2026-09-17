# N64DD next test: execution contract for the local coordinator

Updated 2026-09-17. Reviewed publication baseline for this plan: `5ebdc720d`.

**Purpose:** obtain enough connected evidence to choose a minimal correction,
not release another single-field diagnostic or force the game past its clear.
This is a prospective plan. Its new records, fixtures and gates are requirements,
not features already implemented or tests already passed.

Read this with [current status](N64DD_CURRENT_STATUS.md) and
[local instructions](LOCAL_AGENT_INSTRUCTIONS.md). This document replaces the
*next-experiment instructions* in the older P09 proposal and the supplied
“fix 1–3, add the id/source record” message. It does not replace historical
evidence, authorize a speculative runtime fix, or waive independent review.

## 1. Assessment of the supplied proposal

The proposal is directionally sound: inspect existing evidence, repair diagnostic
defects, obtain live loader ID/source evidence, and review before committing.
Its conclusion **“no emulator divergence demonstrated; do not patch the clear”**
is appropriate. It is not a complete implementation-and-testing contract.

| Item | Required correction or clarification |
|---|---|
| Final “Next” includes ID/source but omits staging writes | Deliver ID/source **and** dynamic staging observation **and** cart PI coverage in one diagnostic package. Do not quietly defer the second item. |
| Legitimate clear followed by stale reuse remains open | Retain evidence at the actual descriptor/publication boundary too, not just a later launch hash. Otherwise explicitly leave ownership/reuse unresolved. |
| “The populating write is confirmed lost” | Ring wrap proves incomplete retained history. It does not prove a particular write entered that ring and was overwritten; cart transfers were excluded. |
| Comparator T1 unavailable | A valid memory sample is not the register value used by an earlier branch. An observed non-MIO0 value still does not establish that this resource **should** be MIO0. |
| “Gate the read path identically” | Require explicit DD-session policy and diagnostics, but do not preserve a DD-address-only filter on the newly required cart-to-RDRAM coverage. Check actual transfer direction, not the word “read.” |
| Staging window described through `word[0x807C70C0]` | A resource-base value, cart offset, guest pointer and physical RDRAM destination are different things. Derive the watched RAM destination from an executed operation; do not pick a similarly numbered address. |
| Capture called “verified” using PID/time | Verify the selected cartridge, loaded IPL/disk, engine, build and launch route. PID/time only identifies a process interval. |
| Reported staged bugs and passing local test | The staged diff and that test script were not in the fetched baseline. They are supplied-only findings until the exact local diff and test output are reviewed. |

Keep the useful diagnostic fixes: correct hexadecimal formatting, accurate
source/destination/end labels, compatible log parsing, strong compiler warnings
and named enum assertions. Those are instrumentation/tooling repairs, **not**
the N64DD emulator repair.

### Evidence already available versus still needed

- Retained aligned CPU stores change audio-buffer words to zero; a later
  core entry and watched RSP fetch see zero content. This is not merely a PC sample.
- The observed clear-entry ranges include the audio buffers. Whether the
  arguments are wrong or the clear is legitimate remains unresolved.
- P09 derives the reader ID/source association from static code and sampled
  state. The supplied report still lacks an executed ID-to-read transaction.
- The report describes two clear calls/entries, a bounded DD-only PI tail and
  one zero-buffer transition. Treat its numbers as supplied until recomputed
  from the private raw capture; this runbook does not independently certify them.
- A generic zero-fill routine may serve a legitimate transition. Calling it
  `memset`, observing large lengths or finding non-MIO0 data does not settle intent.

## 2. Non-negotiable scope and stop rules

1. **Control launch:** select the **F-ZERO X (J) cartridge**, with the correct
   Japanese IPL configured and the legally supplied NDD attached to that game.
   Use **Start**, never Resume. Do not launch the NDD itself as this control.
2. Leave the accepted emulator boot policy unchanged. Cartridge + IPL + attached
   disk does **not** mean forcing a particular initial boot source. Record
   `source` and `combo_cart_boot`; do not toggle IPL-first/cart-first options just
   to make a marker look acceptable.
3. Preserve dynarec as the required engine, the selected Parallel-RSP plugin,
   DDSTART9, and the pinned internal RSP DMA bank/row correction.
4. New diagnostics require explicit per-game DD policy **and** diagnostics.
   A callback, non-null DD object, `.ndd` filename or PI address alone is not
   authorization. DD-off must not sample memory or mutate observer histories.
5. No guest patch, forced MIO0 result, skipped clear/task, synthetic completion,
   forced yield, speculative timing, save-format change or memory-wrap “fix.”
6. Do not change game images, patches, graphics/audio options or unrelated
   settings between the control and candidate. Record any necessary difference.
7. Preserve saves, writable-cart outputs and signing identity. Never uninstall,
   clear application data, delete saves or erase Recently Played to hide evidence.
8. No new APK/device run merely for this documentation update. No new diagnostic
   run until the complete package passes the delivery gate below.
9. The coordinator may complete the already-authorized diagnostic package without
   asking after each substep. A failed prerequisite stops the dependent action,
   not independent analysis. Report the exact failure instead of silently relaxing
   the gate or requesting another speculative build.

**Do not equate** collector completion, populated logs, host PASS, reviewer PASS,
correct signing or successful installation with native repair acceptance.

## 3. Phase A — reconcile the real local state before editing

### A1. Repository and working diff

Run from the repository root. These commands inspect/fetch; they do not switch,
reset, merge, stash or discard local work.

```bash
git status --short
git branch --show-current
git fetch origin dd-eos-watchdog-checkpoint
git log -6 --oneline origin/dd-eos-watchdog-checkpoint
git log --left-right --oneline HEAD...origin/dd-eos-watchdog-checkpoint
git diff --stat
git diff --cached --stat
```

Record the live remote parent and the exact intended source/test/doc diff,
including untracked source files explicitly. Do not assume this workspace and
the local machine share an unpublished patch. Review the actual PI changes
described in the supplied message before fixing or approving them.

Keep SDK contents, MSVC whitespace/BOM churn, private capture directories and
unrelated work outside the intended patch. If local history differs from remote,
preserve it; do not publish that history wholesale or reset it away. A clean
worktree or a selective commit on the live parent is an option, not a mandatory
destructive resynchronization.

### A2. Recover the existing run before asking for another

Use the existing private capture, identity file, immutable logs and analysis
outputs. Record full hashes, lengths and exact process/session boundaries.
Recompute the pivotal results directly, independently of the summary script.

For the supplied session, check:

- Were there actually two accepted call/entry pairs? Decode each validity field.
  Same values, RA and close timestamps support a link; inspect exact compiled
  provenance, order and intervening records before calling the pair established.
- Is the first zero entry preceded by a valid same-buffer entry and the alternate
  buffer? Keep launch sequence, compile generation and RSP generation separate.
- Which PI records were retained, which address/direction filters were active,
  and what do the counters actually count? Do not infer a missing transfer's type.
- Did the log rotate, reconnect, change PID, restart/reset the core, or exhaust
  a per-family budget? Missing data must appear in the analysis result.
- Is the bare-NDD Recently Played entry from this run, another experiment, or
  unknown? Preserve separate identities. A tile alone proves neither validity
  nor invalidity of this capture.

If existing evidence meets all required decision gates, analyze it rather than
building again. If not, list the exact missing fields that the combined package
will supply. Do not replace missing runtime evidence with recollection.

**A exit:** a claim ledger with OBSERVED, DERIVED, SUPPLIED-ONLY, HYPOTHESIS or
UNKNOWN labels, plus an explicit required-versus-available evidence table.

## 4. Phase B — prepare identity verification; finalize it after build/launch

Prepare one private run manifest and define how every field will be verified.
For an existing capture, fill only what its evidence supports. For a future
candidate, mark build/install/runtime fields **PENDING_BY_DESIGN**, not VERIFIED
and not a failed prerequisite for instrumentation. No future PID or APK exists
yet; do not fabricate one or launch early to fill this table.

| Identity | Evidence required | When finalized |
|---|---|---|
| Code/build | Source commit, intended dirty diff if applicable, build result, APK SHA-256, package ID, certificate fingerprint, packaged core/RSP Build IDs | D, after build/publication |
| Installed app | Installed-package identity consistent with that candidate; a host APK alone does not prove installation | E, before launch |
| Process/session | Capture start, timezone, PID and process-start identity, reset/relaunch boundaries, capture end | E, from the run |
| Cartridge | Selected gallery item is F-ZERO X (J); private asset hash/size and runtime cartridge identity correspond | B: available asset/config; E: actual selection/load |
| IPL | Japanese IPL selection and successful runtime load, with private asset identity; mere file existence is insufficient | B: available asset/config; E: actual load |
| Disk | Correct NDD attached in the cartridge's per-game settings and runtime disk-open/region evidence, with private asset identity | B: available asset/config; E: actual attachment/load |
| Configuration | Explicit DD enabled, effective dynarec, actual RSP plugin, existing boot policy and unchanged settings | B: intended/current settings; E: effective runtime |
| Route | Fresh cartridge **Start**, untouched controls through automatic demo, no direct-NDD or Resume launch | B: specified procedure; E: observed route |
| Preservation | Existing clean-save backup method and matching signer; no active save before force-stop/install | B: preservation method; D/E: candidate signer and safe install state |

Existing `DDSTART2 boot selection` fields (`cart_bytes`, `ipl_bytes`, `source`,
`combo_cart_boot`) and disk-region records are useful but not sufficient alone
to identify every asset. Combine them with the launch/settings and loaded-input
evidence. Contradictions invalidate the configuration gate.

If current records cannot establish an identity item, mark the old run
CONFIGURATION_UNVERIFIED. Add the smallest safe, DD-gated launch-identity summary
to the *same* diagnostic package if needed; do not run a separate identification
APK. Keep full paths, device identifiers and raw settings private.

**B exit allowing C/D:** the available configuration/assets and preservation
method are recorded, each required identity has a verification method, and
candidate-only fields are explicitly pending their stated phase. An old run with
missing identity remains unverified; that does not prohibit implementing the
missing identity observation in the combined package.

**Run-rejection rules, enforced on existing runs now and on the candidate in E:**
wrong title, direct NDD, wrong/missing IPL or disk, wrong engine, signature
mismatch, mixed runs or unexplained boot-policy difference. Retain such evidence
but do not use it as the authoritative combination reproduction. Unavailable
required assets or preservation arrangements block device testing, not independent
source analysis.

## 5. Phase C — specify one combined observation contract

Write the schema and evidence table **before** code changes. Give each field an
address space, width, validity, observation phase and provenance. The points below
are required capabilities, not names of APIs that already exist.

### C0. Event identity and honest validity

- Give new core records a reset/session epoch and monotonic observation sequence.
  Assign a loader transaction ID at the actual invocation, and a transfer ID at
  PI begin. Track active/nested operations without confusing reuse of SP or A0.
- Join a completion only to its accepted begin in the same epoch. An asynchronous
  transfer is not attributed merely because it completed inside a loader interval.
- Carry existing DDSTART generations as separately named source fields. Do not
  convert unrelated counters into a common clock or correlate across PIDs.
- Record exact compiled-site provenance where used. A compile-time opcode
  constant is not a runtime memory sample; a memory sample is not an executed
  branch operand.
- Sanitize unavailable values and retain validity/rejection reasons. A zero value
  with validity false is UNKNOWN. Preserve useful partial records, but fail the
  required evidence gate when a mandatory field is missing.

### C1. Loader selection and resource read

At the executed loader/reader operations, before relevant registers are clobbered,
capture:

1. Actual ID argument, caller/link, SP, argument validity and invocation ID.
2. Selection/cache result where relevant: observed ID lookup, hit/miss and returned
   pointer. A later `(id,pointer)` table snapshot is not the executed lookup.
3. Actual resource-base value and its address space, offset calculation and
   **executed computed source operand**. Log the raw operands separately from any
   independently calculated formula; report overflow or disagreement.
4. Actual destination and requested size for the small head read and subsequent
   body/scratch read. Distinguish the two reads inside the same loader transaction.
5. Bounded pre-read and post-read contents of the head/scratch destination:
   complete eight-byte head when valid, and up to 64 guest-order bytes of the
   later sample. Record requested, available and captured lengths.
6. Reader return/completion status where actually observable, and cache/publication
   result. Do not label a call issued or a PI handler returned as the guest reader
   having completed unless that boundary is observed.

Use P09's addresses/formula as candidate instrumentation sites, verified against
the exact executing/compiled instructions. Do not hardcode the inferred ID/source
pair as the answer, or restrict capture so a different actual ID is invisible.
Observe selector/table changes relevant to the actual lookup if the candidate
source depends on them; this supports the wrong-selection alternative.

### C2. Compared value, allocation and clear boundary

- Capture both live branch operands and the observed decision at the comparison
  point, before a register becomes dead. Do not “repair” missing T1 by rereading
  RAM later and calling that the compared value.
- Preserve actual head word1, derived length operand and allocator return where
  available. Record the relation as observed or derived, not assumed.
- Retain original clear arguments at a proven call/entry pair, normalized range
  calculations and overlap with protected audio/loader windows.
- Retain a clear-return boundary or equivalent exact watched-range progress
  evidence. Entry into a large clear does not prove it has completed every store
  before another event. Do not infer sweep order solely from flush timestamps.
- Never change the guest's branch, argument, link, return, count, exception or
  delay-slot behavior to make the probe easier to implement.

The current partial DDSTART15 records remain useful. They do not by themselves
fulfill this stronger live-decision and transaction contract.

### C3. PI delivery: include the cartridge while DD is explicitly enabled

At the current baseline, `dma_pi_write` / `handler->dma_write` / `PI_WR_LEN`
is the **cart/device-to-RDRAM** operation. `dma_pi_read` / `handler->dma_read` /
`PI_RD_LEN` is the reverse, **RDRAM-to-cart/device** operation. Confirm this in
handler bodies before editing; do not infer direction from the English name.

Extend the cart/device-to-RDRAM observation to the actual cartridge-ROM handler
as well as the existing DD handler during an authorized DD diagnostic session.
Classify save-domain, DD, ROM and other handlers explicitly. “Outside the DD
window” is not synonymous with “cartridge ROM.”

Required fields:

- Transfer ID, direction, handler/region class and overlap with a watched window.
- Raw PI cart/RDRAM registers and raw length register, normalized addresses,
  decoded requested length and handler-effective length as separate quantities.
- Address space for each value: cart bus address, file/image offset if established,
  physical RDRAM address, or guest virtual address. Do not compare them unconverted.
- Before/after samples centered on the **overlap**, even when the transfer begins
  before the watched window. Retain the first relevant head bytes, not just the
  beginning of an unrelated large transfer.
- Begin, handler-return and, if needed for an ordering claim, PI interrupt/status
  completion as distinct phases. The handler's cycle result is not bytes copied.
- Measured bytes only where the handler exposes them; otherwise completed length
  is UNKNOWN. Never relabel requested length as an exact completed length.

Observation gates must precede sampling/state work, with a fail-closed module
gate too. The current observer checks policy internally, but calls made outside
an outer gate may still do preparation work; review both levels. Do not claim
zero performance overhead just because no line was logged.

Keep existing `source=DD_PI_CART_TO_RDRAM` semantics for its existing records.
Use a new/versioned record for incompatible additions, or update every parser,
fixture and document in the same reviewed snapshot. Use `PRIx32` for hexadecimal
values, named direction/region enums and `_end_exclusive` for derived ends.

### C4. Dynamic staging-window writes, armed before population

“Staging” must mean a validated RAM destination observed at the actual
reader/chunk-copy operation. It must not mean whichever numeric address happens
to resemble the resource offset.

- Determine and arm the head, scratch or chunk destination **before** its first
  relevant read/write. Arming only at the fallback clear is too late.
- For P09's `word[0x807C70C0]` value and derived `0x5193D0`, establish the address
  space first. Neither is automatically the physical RAM staging target.
  Likewise, a nearby SP value or a post-transition RAM sample is not a transfer
  destination. Derive the window from the executed operation.
- Track a bounded prefix sufficient for the eight-byte head and 64-byte sample;
  report the full operation range separately without dumping all RAM.
- Record overlapping CPU stores and PI completions separately. Retain exact
  address/width/before/after/PC for routes actually observed, plus first/latest
  relevant changes and the arm/disarm boundaries.
- Preserve existing audio-buffer watches; do not replace them with a staging-only
  watch and lose the original zero-writer evidence.
- Cover any CPU route used by the target population/clear, or explicitly report
  that route as missing. Aligned slow-path instrumentation does not cover SWL/SWR,
  byte/halfword, TLB aliases, all delay slots or all DMA writers automatically.
  If a required writer uses an unsupported path, address that gap before delivery.
- Page/block/internal-entry gaps, rejected mappings, invalid pointers and
  overlapping/reused windows must be visible and tested. Never force KSEG mapping
  or 8-MiB wrapping to make a diagnostic read succeed.

### C5. Buffer production and descriptor publication

Existing launch/entry/fetch records are necessary but are not ownership tracking.
To compare legitimate-clear/stale-reuse with bad loading, also identify and
observe the real descriptor construction or queue/publication operation:

- Guest descriptor/task identity, selected data pointer/size and relevant queue or
  publication operation, with an observation sequence and bounded buffer sample.
- Relevant descriptor-field writes or observed producer finalization before
  submission; exact provenance rather than a label inferred from nonzero bytes.
- Previous publication of the same buffer, clear/writer interval, new publication,
  core launch, core entry and watched fetch. Buffer addresses may legitimately
  alternate and lengths may change. Do not require one fixed size per pointer.
- A producer snapshot is not proof the commands are semantically correct; a
  repeated descriptor address is not by itself proof of stale ownership.

Locate this boundary by inspecting the executed guest/bridge path before adding
a probe. Do not invent a “publish” address or add an unbounded whole-memory
shadow system. If the boundary cannot safely be instrumented, record
**PUBLICATION_COVERAGE_BLOCKED**. Resolve it or explicitly re-scope with independent
review before device delivery; do not silently claim the reuse alternative is
covered by the old last-four-launch ring.

### C6. Retention and trigger contract

Do not merely enlarge the existing global “last 16” PI ring. Unrelated boot/DD
traffic can still evict the one transfer needed to explain the head.

Before implementation, document numeric caps for active transactions, watched
windows/bytes, retained CPU events, PI events, frozen trigger bundles, sample
bytes and output lines. This bounded-memory design is part of review.

The minimum behavior to test is:

1. Arm before relevant resource reads, retaining first population and latest
   pre-comparison samples independently of rolling background history.
2. Retain the two offending loader transactions if they recur, their small-head
   and body reads, relevant PI overlaps and first/latest watched CPU changes.
3. Freeze their critical records so later background traffic cannot replace them.
4. Reserve final capacity for the first zero-audio boundary, recent publications,
   both buffer identities and associated coverage summaries. Early or rejected
   probes must not consume this reservation.
5. Maintain explicit observed/retained/overwritten/rejected/suppressed counters.
   Test counter meanings and accounting; lifetime totals are not per-flush counts.
6. Exhaustion yields INCOMPLETE for affected conclusions, never silent success.
   Preserve useful partial evidence rather than dropping the entire capture.
7. Reset all observer state on the appropriate reset/close/new-session paths.
   Do not carry a frozen ID, window or validity bit into DD-off or a new game.

## 6. Phase D — verify the whole package before a device run

### D1. Source and emitter safety

Review production code, not only recorder metadata tests:

- ARM64 full-width pointers must use the proven literal-loading path; include the
  previous pointer-truncation regression for every new snapshot emitter.
- Materialize allocator-backed live values/known constants coherently. Do not read
  dirty values from stale core arrays. Test partial validity and sign extension.
- Preserve live host registers, flags where required, link/delay semantics,
  exception paths, cycles, handler arguments and interrupt ordering.
- Test exact-site/opcode acceptance and false matches. Unknown/internal entries
  must fail closed diagnostically, not alter guest execution.
- Bounds-check every guest read with overflow-safe arithmetic, explicit endian
  conversion and checked normalization. No silent alias/mirroring assumptions.
- Keep hardware correction policy independent of diagnostics/callback availability.
  No unrelated change to SP DMA, CPU DMA semantics or save handling is authorized.

### D2. Required fixtures — use production paths

| Fixture | Must demonstrate |
|---|---|
| DD off; diagnostics off; callback null | No new sampling/history mutations; ordinary transfers and guest behavior unchanged; independent correction policy preserved |
| DD-enabled cart ROM transfer | Cart-to-RDRAM observation occurs with correct direction/region, not just DD-window traffic |
| Reverse PI and save/unknown regions | Not mislabeled as incoming ROM data; invalid/unhandled requests have explicit status |
| Raw/requested/effective lengths | Units, alignment/clipping, wrap/overflow and derived ends are not conflated |
| Partial overlap | A transfer starting before or ending inside a watched window is retained correctly |
| Endian and samples | Eight-byte head and 64-byte sample decode correctly; invalid/short reads stay invalid |
| First-write retention under load | More than the supplied 10,000 unrelated transfers cannot evict protected head evidence or reserved final records |
| Reuse/reset/nesting | Same pointer in a later transaction/session is not joined to the earlier one; overflow is explicit |
| CPU write coverage | Real target store widths/paths, delay slots and rejected routes match the advertised coverage |
| Clear/publication chronology | Correct classification for write-before-clear, write-after-clear, descriptor-only reuse, and legitimate buffer reuse |
| Invalid fields | Each unavailable register/header/selector field separately fails its claim gate without suppressing other valid evidence |
| Generated ARM64 code | Actual emitter fixtures exercise full-width addresses, dirty/constant registers, save/restore and negative opcode gates |

### D3. Analyzer contract and adversarial inputs

Parser/schema tests are part of this package, not an optional follow-up.

- Preserve legacy fixtures and parse any new schema by explicit version.
- Detect malformed required records, missing launch/identity records, short samples,
  repeated/restarted sessions, unmatched operations and exhausted budgets.
- Scope all results to retained records. The existing entry/fetch tool does not
  establish complete coverage: repair or explicitly supplement its treatment of
  per-generation fetch caps, partial overlaps and session boundaries.
- Check launch, entry and fetch phases separately. Normalize only documented
  aliases; do not compare a raw DMA source to a normalized pointer by accident.
- Use nonzero counts/raw sample bytes where available. Hash equality alone is
  weaker than full-byte comparison, and prefix equality is not a full transfer.
- Test two reused-address sessions, a DMA starting before the buffer, four
  retained zero fetches plus budget exhaustion, a missing comparator, changed
  bytes, wrong byte order, truncated logs and absent startup identity.
- A required-field/coverage failure must yield a machine-detectable INCOMPLETE
  result, not an unconditional overall PASS or zero-count headline.

### D4. Commands and results ledger

Run the relevant maintained suites once for the coherent final implementation.
These scripts exist at the baseline; new transaction/staging/schema tests must
also be provided by the implementation. Verify paths before invoking them.

```bash
CC=clang CXX=clang++ bash tools/test-dd-cmd-watch.sh
CC=clang CXX=clang++ bash tools/test-dd-watch.sh
CC=clang CXX=clang++ bash tools/test-dd-policy.sh
CC=clang CXX=clang++ bash tools/test-dd-startup.sh
CC=clang CXX=clang++ bash tools/test-dd-core-imem-dma.sh
bash mupen64plus-core/upstream/tools/tests/run_dd_load_history_test.sh
python3 -m unittest discover -s tools/tests -p 'test_capture_p08_mac.py'
git diff --check
```

The supplied `tools/test-dd-pi-dma-source-region.sh` was not present at the
reviewed remote baseline. If added locally, include its exact reviewed source,
commands and results; do not cite it as an already-verified repository test.

Run `tools/test-dd-arm64-syntax.sh` with the verified local NDK path, then build
the complete candidate using `./gradlew :app:assembleDebug --console=plain`.
Do not assume a Replit SDK path exists on the Mac or reuse an old APK as output.
Verify packaged ABI/markers and preserve matching symbols privately.

Mac compiler/linker portability is not permission to drop `-Werror` globally,
replace production objects with mocks, or silently omit assertions. Make a narrow,
reviewed adaptation or run the same fixture in a supported environment; report
BLOCKED if neither is possible. “All other suites use it” must not replace
inspection of the actual flags.

### D5. Single delivery gate

Before publication/install, give an independent read-only reviewer:

- Exact final diff/new files, baseline, schema/coverage table, address conversions,
  observer caps, event joins, boot identity plan and claim ledger.
- Complete relevant host/emitter/parser results, failures and portability changes.
- Proof that C1–C5 and reserved retention are included, or the explicitly reviewed
  coverage re-scope. A cart-PI-only change is not this combined package.
- Native command/route, signing/build identity and save-preservation procedure.

Resolve blockers and obtain PASS for the exact snapshot before every source or
documentation commit. Re-review changes made after approval. Publish only selected
source/tests/docs on the verified live parent, non-force; stop on remote advance.
Build provenance must correspond to the reviewed published source. No capture,
game image, signing material, SDK, symbols or APK goes into the source branch.
After the build and source publication, finalize Phase B's candidate code/APK/
certificate/Build-ID fields. Installed identity and process/runtime fields stay
PENDING_BY_DESIGN until E; the delivery gate approves their verification procedure,
not a claim that a future native run has already passed.

## 7. Phase E — one primary diagnostic capture

This phase becomes eligible after A–D pass their own gates, including B's
manifest/configuration preparation and D's candidate build identity. B's future
installed/process/runtime fields are completed here, not required before E.
It is not authorization to repeat the old build now.

1. Finish any active save normally and use the established backup procedure.
2. Verify candidate hash, matching package/certificate and loaded-input settings.
   Install with `adb install -r`, never uninstall or clear data. Verify the
   installed candidate identity before launching.
3. Start continuous log capture before the fresh launch. Do not rely on a
   post-freeze `logcat -d` tail; the observed traffic can rotate the ring.
4. Use the existing helper in manual mode, from the repo containing it:

   ```bash
   python3 tools/capture-p08-mac.py --duration 60 --manual-start
   ```

   The documented Mac ADB is `~/Downloads/platform-tools/adb`. Confirm the
   helper's actual options first. It does not prove the human selected the
   cartridge; the identity evidence in Phase B is still mandatory.
5. Select F-ZERO X (J) **cartridge → Start** with its IPL/NDD attachment intact.
   Do not select the disk tile, Resume, a savestate or the writable-cart hack.
6. Let the automatic demo transition occur without input for the full 60 seconds.
   Preserve garbled-menu/music/dissolve/black-screen observations separately.
   Do not replace this route with manual race selection halfway through.
7. Do not use `--memory`, process suspension or a root-free memory watchdog for
   this package. Existing private dumps may be analyzed offline; no new memory
   capture is a prerequisite or a substitute for missing instrumentation.
8. Complete Phase B's process, loaded-media, effective-configuration and route
   evidence from this run. Preserve the private archive, manifest and completion
   status. Check fatal signals, log loss and trigger coverage before interpreting
   content. Collector success with a crashed process is not native verification.

A different route, direct-NDD launch or extra experiment gets a separate identity
and purpose. It cannot silently become this control. Do not repeat the diagnostic
run just to obtain the expected sequence number: sequence counts can vary.

For this observational build, retain a short DD-off/plain-cart smoke check
after the DD run to check isolation. It is a separate control run with the same
cartridge, explicit DD disabled, no effective attached disk/IPL use, and original
preferences preserved for restoration. It is not a second diagnostic hunt or a
claim of complete persistence/regression acceptance.

**E exit:** configuration-verifiable capture and retained coverage sufficient
for at least the intended claim, or a specific CONFIGURATION/CRASH/COVERAGE
failure. Required identity fields must now be verified before accepting this as
the control in F; PENDING is no longer an acceptable substitute. Do not build
again before analyzing that failure.

## 8. Phase F — one combined analysis and independent reference comparison

Construct a per-transaction table:

```text
run/epoch | loader ID and lookup | base/source operand + address space
head/body destination and size | PI begin/return/IRQ | CPU staging changes
post-read bytes | actual comparator/decision | allocation and clear entry/return
descriptor production/publication | launch | core entry | watched fetch
validity/provenance | losses and unsupported paths | claim status
```

Analyze in this order:

1. Validate identities and completeness; isolate each run/reset.
2. Establish ID/selection and the actual read destination/source, not a later table.
3. Compare delivery samples with the same operation's source, including byte order,
   addressing, cache/staging copies and PI completion timing where relevant.
4. Follow those bytes to the actual branch operands and clear arguments.
5. Follow the relevant clear progress and subsequent producer/publication events
   into the zero submission/fetch. Do not conclude absence of intervening writes
   where the advertised coverage cannot observe them.
6. Use the [reference process](N64DD_DMA_AUDIO_REPAIR_PLAN.md#portable-reference-handling)
   to compare the earliest suspected divergence against pinned hardware/reference
   semantics or a known-good matching cartridge/IPL/disk execution.

For a reference run/replay, record exact revisions, asset identities, configuration,
engine and operation inputs. Keep byte-order and cart-bus/file-offset/disk-sector
conversions explicit. A raw disk-file offset is not automatically a PI address.
Use legitimate local assets; do not fetch substitute copyrighted images.

Compare the same logical resource/operation, not matching counter numbers or two
screenshots at vaguely similar times. A base-cart run or EK cart hack is a useful
control, not the reference for the DD combination's resource selection.

Matching bytes in a cart file prove a byte match at that offset, not that the
guest requested that cart resource. Conversely, matching delivered and requested
bytes may move the question upstream to ID/base selection rather than establish
all loading behavior correct. A scan for nearby valid MIO0 blocks does not prove
the failing input ought to be one. A missing reference yields REFERENCE_BLOCKED,
not license to choose guessed semantics.

### Decision table

| Finding | Permitted next action | Not justified |
|---|---|---|
| Required identity/record missing, critical window lost | Report exact missing phase; first repair analysis/retention using fixtures and existing data | Treat missing events as proof or ship another one-field APK automatically |
| Correct ID/source selection, delivered bytes differ from pinned expected operation | Isolate the earliest transfer/copy/visibility divergence; create failing production-path fixture | Patch the clear or infer a whole DMA-engine defect |
| Delivered bytes match the requested operation, but ID/base selection differs from a matched reference | Trace the demonstrated selector/table/state divergence and its writer | Substitute the “nearest MIO0” address or force a known ID |
| Clear agrees with reference but stale/invalid publication is directly demonstrated | Correct the demonstrated ownership/ordering/descriptor divergence, after a failing fixture | Skip every empty task or repopulate a buffer manually |
| Correct published bytes but a later observed read/decode differs | Isolate that exact consumer/decode operation | Revive a generic RSP explanation based on the final black screen |
| Large range crosses 8 MiB, no mapping differential yet | Compare exact CPU access semantics separately | Assume modulo wrapping or that range arithmetic alone is the defect |
| All measured operations agree, key ownership/reference proof unavailable | State UNRESOLVED and the precise remaining boundary | Claim the guest/game is broken or the next proposed patch is a fix |

## 9. Phase G — only after a demonstrated divergence

Before changing runtime behavior, write a short correction proposal containing:

1. First incorrect operation, exact values, run identity and pinned expectation.
2. Competing explanation refuted by the combined evidence.
3. Minimal affected code path and DD/WritableROM/logging-off implications.
4. A production-path fixture that fails before the correction and passes after it,
   plus a neighboring normal/refutation case. If such a fixture is genuinely
   infeasible, independent review must approve the precise alternative proof.
5. Reproduction and preservation checklist, rollback/recovery implications without
   deleting data, and the exact claims the candidate would justify.

Keep the correction separate from diagnostic changes. Obtain independent review.
Then verify fresh DD menus/rendering, recognizable music/effects, automatic demo,
manual race/gameplay, clean restart and diagnostics-off behavior on dynarec.
Run the [native acceptance matrix](N64DD_DMA_AUDIO_VALIDATION.md#6-final-native-acceptance-matrix):
plain cart, DD-to-DD-off isolation, normal saves, independently enabled WritableROM
persistence and relevant Amped Up/Mario Tennis/EK Cart Hack controls.

Persistence means a small authorized in-game change, clean save/exit, cold
relaunch and in-game verification; a savestate or force-stop is not that test.
Cached interpreter, if used, requires its own effective-engine and full dispatcher
coverage accounting and does not replace dynarec acceptance.

The repair remains incomplete until its applicable native and persistence gates
are satisfied. Host results or success on a different launch configuration cannot
close the investigation.

## 10. Required coordinator output and handoff

At each phase boundary, provide this concise record rather than a progress story:

```text
Phase / exact baseline and patch:
Required gate:
PASS / BLOCKED / INCOMPLETE (with reason):
Verified observations and raw citations:
Supplied-only claims:
Derived calculations and address spaces:
Missing or invalid fields / budget losses / uncovered paths:
Files changed and checks actually run:
Independent reviewer and exact snapshot, when required:
Next already-authorized action:
What remains prohibited:
```

Update current status and the chronological handoff with substantive new evidence
and the next eligible phase. Never hide a failure under a historical PASS.

**Copy-pastable continuation instruction:**

> Follow docs/N64DD_NEXT_TEST_RUNBOOK.md, starting by reconciling the actual local
> diff and existing capture. Do not restart completed historical work or create
> a runtime workaround. Verify cartridge + Japanese IPL + attached NDD and fresh
> Start; never substitute direct NDD. Deliver the combined loader/compare,
> PI/staging and publication evidence contract with explicit validity and reserved
> retention. Pass its production/emitter/parser fixtures and independent review
> before a device run. Analyze the capture and reference differential together.
> Stop at a named failed gate, not at every successful substep; do not silently
> omit a required channel or ask for another one-field APK. No repair claim until
> a demonstrated divergence and the documented native/persistence acceptance.