# P05 checkpoint — post-correction evidence contract (2026-09-13)

Published from [HANDOFF_NEW.md](HANDOFF_NEW.md) (P05 section);
related record: commit `46242bf55` on `dd-eos-watchdog-checkpoint`.

## Review record (backfilled)

- Review loop: first review NEEDS CHANGES (LEGACY_CROSSING start-bank blocker) -> fixed with X1/X2 pins and unconditional init -> delta re-verified PASS; verdicts recorded in commit `46242bf55`'s
  message (the protocol-designated location for the full review
  narrative and the reviewed-snapshot hashes).
- The Checkpoint body below is extracted verbatim from the handoff
  block with only the two pending fields replaced.
- Snapshot table below is recomputed from the published commit
  itself (`git show 46242bf55:<path>`), so it cannot drift from what
  was reviewed and committed.

| File (at this commit) | Status | SHA-256 (git show recomputed) |
|---|---|---|
| `docs/HANDOFF_NEW.md` | M | `b02a6577d73dc9057bf61076e7501e6b0e8600a38d2830e1116e2455cd09842d` |
| `mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp` | M | `954d52014cf36895275cea7254481e3040845bdcd6469c789cf88ffbbb5532ef` |
| `mupen64plus-rsp-parallel/upstream/rsp_diag.cpp` | M | `0a58c5c9b3b8f58f68fc4a376a61e9045845d481c5bf556f71c56043be145395` |
| `mupen64plus-rsp-parallel/upstream/rsp_diag.hpp` | M | `d8ceac1617ed052acb2c3aa7b73e1597ee27178999fa4711388d0ee32c484e6b` |
| `tools/tests/rsp-dd-dma-transfer-test.cpp` | M | `dde60417d63e0e6870c0ce09f0eda49a5a7ae4a33cec1795e75b2bbeddd0d0c1` |

## Checkpoint

Package: P05 — Preserve useful evidence after the overwrite is fixed (bounded
  diagnostics; no new emulation policy; guest behavior unchanged)
Baseline / Reviewed snapshot: dd-eos-watchdog-checkpoint @ 731694b58; changed
  files: mupen64plus-rsp-parallel/upstream/rsp_diag.hpp, rsp_diag.cpp,
  rsp/cp0.cpp (observation-only captures in both arms),
  tools/tests/rsp-dd-dma-transfer-test.cpp, docs/HANDOFF_NEW.md.
Verified observations: the DDSTART11 dedup would have made the recurring
  suspect request invisible under the corrected policy (identical operands,
  IMEM now unchanged), so the P05 trigger path is dedup-exempt with its own
  4-snapshot-per-session budget, separate from the 512-record DMA budget and
  all startup logs. Trigger shape = the raw DDSTART11 evidence: length
  register 0xffffffff (zero extracted size after the microcode decrement)
  with a zero 24-bit DRAM address. Logging-only; the T-dup fixture proves
  guest state is byte-identical across 6 consecutive triggered runs.
Derived results and inputs: schema version 2 appends policy, trigger reason,
  capture_eligible, probe_skipped/reason, actual_imem_writes, skip_effective
  (raw 0xfff vs aligned 0xff8), dirty_after, final_cache/final_dram,
  entry_generation, sp_pc, t9/k0 and the full 32-GPR snapshot to every
  emitted dma_read record — the GPR capture is taken at the MTC0 launch from
  the live registers (origin: exact, per the DDSTART11 static helper
  analysis), so the operands that produced the raw request survive any later
  DMEM overwrite. Duplicates beyond the budget are counted, reported once as
  exhaustion, and summarized (snapshots/duplicates) when diagnostics are
  re-registered or detached. LEGACY_CROSSING is recorded as an informational
  reason on ordinary legacy records only when the transfer STARTED in DMEM
  (start bank captured before the row advance mutates the destination) and
  actually wrote IMEM; fixture pins X1 (plain full-IMEM load, trigger=0) and
  X2 (genuine two-row crossing, trigger=2) guard the classification in both
  directions. The legacy observer's DDSTART11 dedup and budget semantics are
  otherwise untouched. Trigger snapshots share the ordinary record/sequence
  counters, so ordinary record numbers can gap when triggers fire
  (monotonic, harmless). Deferred with reasons: the
  bounded recent-command ring and task-buffer snapshot (no verified fetch
  cursor exists — that is P07's boundary; the plan gates both on it), and
  A05/A06/A07 command-buffer provenance fixtures (same dependency).
Remaining hypotheses: none added. The first corrected native run can now
  answer: does the suspect command still occur (trigger snapshots), with
  which exact register operands (GPR capture), and does the corrected
  transfer preserve IMEM (before/after hashes + zero actual writes).
Changed files: rsp_diag.hpp, rsp_diag.cpp, rsp/cp0.cpp, transfer fixture,
  this handoff.
Checks actually run and why: bash tools/test-dd-dma-transfer.sh (both modes
  all-pass; D01 contract fields policy=0/trigger=1/actual=3052/
  skip_effective=4095; D02diag policy=1/actual=0/skip_effective=4088/final
  regs 0xfb0-0x1fe808; T-dup: 4 snapshots + exhaustion + summary 4/2 + exact
  t9/k0 capture + unchanged guest state); test-dd-policy.sh,
  test-dd-core-imem-dma.sh, test-dd-root-stacks.sh all pass; startup
  callbacks step passes (dynarec step still blocked by the pre-existing
  new_dynarec.c warning-as-error issue); clang syntax checks clean on
  rsp_diag.cpp, cp0.cpp, parallel.cpp.
Independent reviewer and verdict: see "Review record" above.
Commit, if approved: see "Review record" above.
Remaining blockers: none for P06.
Next eligible package and required inputs: P06 — first native decision test.
  Build all ABIs with the original signing identity, record source revision/
  APK hash/certificate/Build IDs, install -r (never uninstall), confirm
  dynarec + policy active, reproduce the DDSTART11 route, capture, and
  classify by the validation §5 decision table. Diagnostics may be enabled
  or disabled; the correction is policy-driven either way.
