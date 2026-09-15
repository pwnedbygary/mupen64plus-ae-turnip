# P08 checkpoint — writer investigation infrastructure (P08a)

Published from [HANDOFF_NEW.md](HANDOFF_NEW.md) (the P08a section). The
snapshot table below is recomputed from the published commits
(`git show <commit>:<path>`) once P08a is committed; until then the
reviewed-snapshot hashes and the reviewer verdict live in the P08a commit
message, the protocol-designated location.

## Review record

- P08a followed the full loop: scope first (infrastructure only — a dynamic
  watched range, a generation model and a bounded ring, no call site and no
  generated-code change), implementation, proportionate checks (host suite,
  NDK single-translation-unit compile, full APK build with object-level
  verification that the new unit was compiled for all four ABIs), then
  independent read-only review of the exact snapshot before commit.
- The first independent review returned NEEDS CHANGES — one MEDIUM
  (blocking): the record described the single-unit compile as "the real
  build configuration" while using the host suite's flags (`-std=gnu11
  -Wall -Wextra -Werror`, API 21), not the NDK build's COMMON_CFLAGS/
  LOCAL_CFLAGS at minSdk 23; plus LOW items (the policy guarantee stated
  more strongly than the arm-time check enforces, the unstated KUSEG
  limitation next to the sibling helper that rejects non-KSEG segments, two
  test gaps where mutations escaped, the oldest-vs-recent ring divergence
  left unrecorded, the unwatched pre-alignment byte, and the
  `dd_watch_total` label). Every finding was resolved — the build claim was
  re-measured with the real flags for two ABIs, the header and the test
  suite were corrected, and mutations that previously escaped now fail the
  suite — and the delta was re-reviewed before commit.
- The host suite caught two of my own errors before review — a ring-fill
  arithmetic slip that ignored a pre-existing event, and a wrap-guard
  expectation that ignored arm-time normalization — which is the reason the
  infrastructure lands with its synthetic-event tests rather than after the
  first writer hook.
- The five-point hardware-evidence checklist (pinned wiki revision, P01 DMA
  ledger and its pinned Ares reference, source/class/disagreement/validation
  statement) does not apply to this subdivision: no emulator-visible value,
  timing or ordering changes, as stated in the module header. P08b, the
  first package that touches an emulation path, must carry it.

## P08a checkpoint (watched-range observation infrastructure)

Package: P08a — infrastructure for the P08 writer investigation: dynamic
  watched range, generation model and bounded ring, exercised with
  synthetic events and no generated-code changes.
Baseline / Reviewed snapshot: d115b7c82 (P07-C), clean tree.
Verified observations: the module's contract holds under synthetic events —
  the per-game DD policy gate refuses to arm and counts every refused call
  (it is consulted at arm time only; a session armed while the policy was
  enabled keeps recording until disarmed, which the record now states);
  arming normalizes a KSEG0/KSEG1 base; range membership is exact at both
  ends of the documented aligned zero run `[0x3DA9F0, 0x6ECA10)` (both
  command buffers inside, `gAudioCtx` excluded, KSEG0 mirror of a member
  inside; the documented run's pre-alignment start byte `0x3DA9EF` is
  outside the armed range and unwatched — immaterial for the buffers, stated
  so coverage is not overstated); stored addresses are normalized rather
  than passed through, and the caller's struct is never modified; the stored
  generation is the module's, not the caller's; the bounded ring retains the
  oldest events (300 calls → 256 stored, 44 dropped) and the accounting
  identity `total + dropped + rejected == calls` holds; rejection takes
  precedence over a full ring; a rejected retrieval index leaves the
  caller's struct untouched. The suite compiles the real
  `device/dd/dd_watch.c` with the real core policy state
  (`api/callbacks.c`) — no production logic stubbed — and the previously
  escaping mutations (stored-address normalization removed; range test
  ignoring the armed flag) now fail it. The translation unit compiles with
  the project's ACTUAL build flags (NDK 26.1.10909125 clang 17.0.2, targets
  `aarch64-linux-android23`/`armv7a-linux-androideabi23`, COMMON_CFLAGS plus
  the core's LOCAL_CFLAGS, no `-std` flag ⇒ `__STDC_VERSION__` 201710L) for
  both ABIs, and the full debug APK build produced its object for all four
  ABIs.
Derived results and inputs: P08b can be written against a fixed, tested
  contract (policy-gated arming, static bounded storage, oldest-retention
  with coverage counters, module-stamped generations — the generation
  counter the fetch-provenance trace needs). Recorded divergences and
  limitations P08b must respect: oldest-retention differs from the
  work-packages wording "fixed recent-event ring" (P08 step 7) and loses
  events after 256 stored in-range ones — arm narrowly, re-arm per
  generation; the KSEG0/KSEG1 mask is a core-convention reference
  (`r4300_core.c`, `api/debugger.c`; the sibling `dd_fault_layout.h`
  instead rejects non-KSEG segments) and KUSEG/TLB addresses are not
  translated, so a recording site must pass the address its writer path
  already resolved.
Remaining hypotheses: producer-side zeroing (game heap clear / DD load
  delivering zeros / emulator-side clear), consumer-side fetch/decode error,
  stale-buffer reuse; the fetch provenance is still the missing link.
Changed files: `mupen64plus-core/upstream/src/device/dd/dd_watch.h` (new),
  `mupen64plus-core/upstream/src/device/dd/dd_watch.c` (new),
  `mupen64plus-core/mupen64plus-core.mk`, `tools/tests/dd-watch-test.c`
  (new), `tools/test-dd-watch.sh` (new), `docs/HANDOFF_NEW.md`,
  `docs/P08_CHECKPOINT.md` (new).
Checks actually run and why: `tools/test-dd-watch.sh` (pass, host default
  `cc` = GCC 16.2.1; the module's contract under synthetic events, plus
  mutation checks on `/tmp` copies showing the two review-found test gaps
  are closed); NDK single-unit compile with the real build flags for
  arm64-v8a and armv7a (clean); full debug APK build (successful, new object
  present for all four ABIs). The
  other host suites are unaffected by an unlinked unit and were last run at
  P07-C: `test-dd-core-imem-dma`, `test-dd-policy`, `test-dd-dma-transfer`
  and `test-dd-root-stacks` pass; `test-dd-startup` fails in its compile
  step and `test-dd-rsp-mac` reports 6 passed / 8 failed, both pre-existing.
Checks not run and why: no device run — an unlinked observer cannot be
  observed natively; P08b is the package that wires a writer and needs the
  native capture.
Independent reviewer and verdict: see the P08a commit message.
Commit, if approved: (this file's commit).
Remaining blockers: none.
Next eligible package and required inputs: P08b — the fetch-provenance
  capture (at task submission and at the microcode's command fetch: address
  fetched, bytes returned, buffer generation), reusing this generation
  model; the writer-family watch follows only if that trace does not
  separate the competing hypotheses. Requires per-game DD activation gating
  on every new observation and one native run (user-driven device step).

## P08b checkpoint (fetch provenance: the command-buffer watch)

Package: P08b — record what the microcode actually fetched: the command
  buffer it read, the offset of the first byte it read inside that buffer,
  the row that reached it, the bytes returned and the
  task generation, in the plugin where the RSP's reads are visible.
  Diagnostics only: no correction, register, timing, ordering or
  generated-code change, and no transfer bytes altered.
Baseline / Reviewed snapshot: 0565fe00d (P08a), clean tree.
Verified observations: the watch arms only for an audio task under the
  per-game DD policy with a registered callback (policy off, no callback or
  a zero-length buffer ⇒ never armed, never recorded); classification is
  ROW-EXACT — a read is recorded only when one of its row spans really reads
  a byte inside the buffer, and the record carries
  `fetch_buffer_offset` (the first byte read inside the range) and
  `fetch_first_row`, so the review's two counterexamples (a skipped-over
  head falsely reported as covered, and an intersecting row a linear span
  missed) are now regression cases; a DMEM-destined in-buffer read is
  recorded with schema=3, `fetch_watched=1`, the entry generation and the
  payload hash of the bytes actually read (computed independently in the
  fixture); out-of-buffer reads, read-only skip gaps and the freeze-shaped
  read (whose rows top out near 0x1fe7c8, short of the P07 buffer at
  0x411910) produce no fetch record; the per-generation budget bounds fetch
  records to 4 with the fifth counted as a duplicate, one exhaustion line,
  and a summary `records=4 duplicates=1 trigger_observations=…` at any callback
  change; a new generation re-exposes its own fetches, a graphics task does
  not arm, and a trigger-shaped watched read keeps its trigger snapshot while
  being counted. Limits recorded: `fetch_generation` is the ARM generation;
  an unarmed generation is indistinguishable from a fetch-free one in the
  log; and the arming call site has no host coverage. The P02 (legacy and
  corrected), P03, P05 and P08a host suites all pass with these changes, and
  the real build recompiled the three changed units for all four ABIs.
Derived results and inputs: the plugin can now answer, per audio task
  generation, what the microcode fetched (which buffer, the first byte it
  read inside that buffer, which row reached it, the payload bytes and the
  arm generation), completing the discriminating trace alongside the
  captured launch operands and the suspect-request trigger. The five-point
  hardware-evidence checklist is answered (not waived): no hardware
  behavior changes; the diagnostic asserts no hardware semantics beyond the
  P07-derived range and the P01 address model.
Remaining hypotheses: producer-side zeroing (game heap clear / DD load
  delivering zeros / emulator-side clear), consumer-side fetch/decode
  error, stale-buffer reuse.
Changed files: `mupen64plus-rsp-parallel/upstream/rsp_diag.hpp`,
  `rsp_diag.cpp`, `rsp/cp0.cpp`, `parallel.cpp`,
  `tools/tests/rsp-dd-fetch-provenance-test.cpp` (new),
  `tools/test-dd-fetch-provenance.sh` (new), `docs/HANDOFF_NEW.md`,
  `docs/P08_CHECKPOINT.md`.
Checks actually run and why: the new fetch-provenance fixture (pass), the
  four neighbouring host suites (no regressions), syntax checks of the
  changed units, and the full debug APK build (successful, the three
  objects rebuilt for all four ABIs by timestamp).
Checks not run and why: no device run — the instrumentation is this round;
  the native run that produces the evidence is the next step, and overhead
  is explicitly not claimed as zero (capture only when DD-enabled, armed
  and intersecting; the native sampler provides the comparative measure).
Independent reviewer and verdict: see the P08b commit message.
Commit, if approved: (this file's commit).
Remaining blockers: none.
Next eligible package and required inputs: the native fetch-provenance run
  (user-driven): install the built APK with the existing signing identity,
  reproduce the freeze with DD enabled, pull the logcat and analyze the
  fetch records against the P07 evidence — the generation, address, offset
  and payload it reports decide between the competing hypotheses. Carried
  from P08a: the wrap-guard boundary assertion for the dd-watch suite (L1).
