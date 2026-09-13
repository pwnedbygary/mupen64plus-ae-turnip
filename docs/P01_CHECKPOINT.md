# P01 checkpoint — DMA policy ledger completed (2026-09-13)

Published from [HANDOFF_NEW.md](HANDOFF_NEW.md) (P01 section);
related record: commit `de4b63026` on `dd-eos-watchdog-checkpoint`.

## Review record (backfilled)

- Review loop: first review NEEDS CHANGES (four arithmetic/wording blockers) -> fixed and simulation-verified -> fresh reviewer delta PASS; verdicts recorded in commit `de4b63026`'s
  message (the protocol-designated location for the full review
  narrative and the reviewed-snapshot hashes).
- The Checkpoint body below is extracted verbatim from the handoff
  block with only the two pending fields replaced.
- Snapshot table below is recomputed from the published commit
  itself (`git show de4b63026:<path>`), so it cannot drift from what
  was reviewed and committed.

| File (at this commit) | Status | SHA-256 (git show recomputed) |
|---|---|---|
| `docs/HANDOFF_NEW.md` | M | `aa0275c8cb0ee54e61c0aeac9feead760dbf4f261661fb2a5c7c04268301900b` |
| `docs/P01_DMA_POLICY_LEDGER.md` | A | `90422d9d04c5df99ebbd546f7ef8921c44959af0037329bce4bc9f9dff11a617` |

## Checkpoint

Package: P01 — Decide the complete DMA policy (analysis only; no emulator changes)
Baseline / Reviewed snapshot: branch dd-eos-watchdog-checkpoint at 017ef41ca
  (P00 record), clean tracked tree; pinned Ares reference
  ~/LLM-Projects/phobos @ f1174e7654141accad40b9ffc2c7d978e93a00f0.
Verified observations: legacy rsp_dma_read decode/clamp/13-bit-walk/raw-skip/
  trailing-skip/writeback/dirty/return behavior transcribed with line citations;
  Ares decodes 8-byte-aligned SP/DRAM/skip fields, latches pbusRegion (bit 12),
  wraps a 12-bit offset, adds skip only between rows, and forces a 0xFF8 length
  poststate; CXD4 allows 13-bit crossings and floors addresses per 8-byte beat
  with no register writeback; core do_sp_dma walks contiguously, no writeback.
Derived results and inputs: completed §6.2 decision table (docs/P01_DMA_POLICY_LEDGER.md
  §4) selecting the pinned Ares model coherently for the corrected internal-read
  path: latched bank, 12-bit wrap, full row length (clamp removed), 8-byte SP/DRAM
  alignment, skip aligned to 8 at decode, no trailing skip, defined 12+1-bit final
  SP poststate, 24-bit final DRAM poststate, dirty flags only for actual IMEM
  destinations, scheduling unchanged. Observed request calculated under both
  policies (legacy: 80-byte rows/5120 words/3052 IMEM writes/final DRAM 0x104f00;
  corrected: 4096-byte rows × 256 = 1,048,576 bytes, zero IMEM writes, final DRAM
  0x1fe808 — that final value follows from decision row 7, the Ares-aligned skip).
Remaining hypotheses: hardware truth for skip alignment, 4-vs-8-byte SP start,
  and register poststates is not verifiable locally — the choice is the pinned
  reference model, to be validated natively (P06). CXD4/core divergences are
  documented, not adopted.
Changed files: docs/P01_DMA_POLICY_LEDGER.md (new), docs/HANDOFF_NEW.md (this section).
Checks actually run and why: source/reference line-level verification of every
  decision row; algebraic identity round-down-8(x)+8 == round-up-8(x+1) checked
  for row-length equivalence; backing-bounds check 0x7FFFFC>>2 < 2^21 words
  against the 8 MB RDRAM backing; no tests claimed beyond that.
Independent reviewer and verdict: see "Review record" above.
Commit, if approved: see "Review record" above.
Remaining blockers: none for P02/P03. P02 needs the §4 table as its oracle
  specification; P03 needs the corrected-vs-legacy selector requirement only.
Next eligible package and required inputs: P02 (production-path failing fixtures;
  inputs: this ledger §4/§5/§7, cp0.cpp, tools/tests layout) — may run in
  parallel with P03 (explicit DD runtime-policy seam) with disjoint file
  ownership. P04 waits for both.
