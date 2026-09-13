# P02 checkpoint — production-path DMA fixtures (2026-09-13)

Published from [HANDOFF_NEW.md](HANDOFF_NEW.md) (P02 section);
related record: commit `47a2d8ce3` on `dd-eos-watchdog-checkpoint`.

## Review record (backfilled)

- Review loop: first review PASS with nonblocking findings -> fixes applied -> delta re-review caught claimed-but-unapplied edits (NEEDS CHANGES) -> applied and verified in-tree -> final PASS; verdicts recorded in commit `47a2d8ce3`'s
  message (the protocol-designated location for the full review
  narrative and the reviewed-snapshot hashes).
- The Checkpoint body below is extracted verbatim from the handoff
  block with only the two pending fields replaced.
- Snapshot table below is recomputed from the published commit
  itself (`git show 47a2d8ce3:<path>`), so it cannot drift from what
  was reviewed and committed.

| File (at this commit) | Status | SHA-256 (git show recomputed) |
|---|---|---|
| `docs/HANDOFF_NEW.md` | M | `ed71193c2ca206a69119fb1d657fa75cdb6f77ca70f4efbc02e18d18fd2437ee` |
| `docs/N64DD_DMA_AUDIO_VALIDATION.md` | M | `b62fe5c4f62f2c497f9f85c16b3bd0f34e308fb6e7320c8e74a80b26a6abf8b9` |
| `tools/test-dd-dma-transfer.sh` | A | `e56a6069b0a5831a63f088c46e1072432734fa1df65cda7693d7b1bf935c94f3` |
| `tools/tests/rsp-dd-dma-transfer-test.cpp` | A | `83b1c847680e3cd9bf125ce411d4957300c50de03d252dcb21e452dbcf76aba1` |

## Checkpoint

Package: P02 — Build tests that exercise the real transfer (test infrastructure;
  no production source changes)
Baseline / Reviewed snapshot: dd-eos-watchdog-checkpoint @ de4b63026; new files
  tools/tests/rsp-dd-dma-transfer-test.cpp and tools/test-dd-dma-transfer.sh;
  docs/N64DD_DMA_AUDIO_VALIDATION.md §2/§7 updated (command ledger + D02 status).
Verified observations: the fixture compiles the REAL production TUs
  (rsp/cp0.cpp + rsp_diag.cpp with PARALLEL_INTEGRATION/M64P_PLUGIN_API) and
  drives RSP_MTC0 — the same entry the JIT uses — with guarded DMEM/IMEM/RDRAM
  mappings (PROT_NONE guard pages, D24) and nonuniform self-identifying
  patterns. The production observer (Diagnostics::set_callback) reports
  payload=5120 / imem_writes=3052 for the captured request — matching the
  DDSTART11 record from the production side, not just the oracle.
Derived results and inputs: independent oracle implements P01 §2/§4 directly
  (legacy + corrected models); 21 table rows (D09a/b and D23a/b are paired
  sub-cases): shared D03/D09/D10/D12 (both models
  agree, cross-checked), legacy pins D01/D22a/D22b/D14L/D18/D21(128 random
  seeded)/D23a/b, corrected D02/D05/D06/D07/D08/D11/D13/D14/D16/D19. Pre-fix
  result: legacy suite passes fully; every corrected case XFAILs on exactly
  the ledger-predicted field (D05 cache_reg 0x1000-vs-0x0 poststate, D19
  dram_reg 0x10f0-vs-0x10e0 trailing skip, D02/D06/D07/D08/D16 crossing/wrap
  bytes, D11/D14 alignment, D13 skip low bits). Deferred validation-§2 sweeps
  for later packages: the remaining D09/D10/D11/D12/D13 field sets and
  standalone D04/D15/D20 rows (D17 substance is covered by exact dirty_blocks
  assertions in D07/D14/D01/D21). DD_DMA_REQUIRE_CORRECTED=1
  makes corrected divergences hard failures (verified exit 1 pre-fix); P04
  must flip the runner default.
Remaining hypotheses: none added; corrected-mode production wiring is
  intentionally absent until P03/P04 (the test binary's corrected mode
  exercises whatever production implements until the seam exists).
Changed files: tools/tests/rsp-dd-dma-transfer-test.cpp (new),
  tools/test-dd-dma-transfer.sh (new), docs/N64DD_DMA_AUDIO_VALIDATION.md
  (§2 D02 status, §7 command ledger), docs/HANDOFF_NEW.md (this section).
Checks actually run and why: bash tools/test-dd-dma-transfer.sh (legacy mode:
  all pass incl. production observer counts; corrected mode: all 10
  corrected-kind cases XFAIL as
  expected); DD_DMA_REQUIRE_CORRECTED=1 run verified to fail (exit 1) pre-fix;
  earlier harness bugs (driver dirty reset, count-field nibble errors,
  XPASS classification for shared cases, decimal-in-0x detail strings) were
  caught by the suite's own ORACLE-BUG/XPASS checks and fixed.
Independent reviewer and verdict: see "Review record" above.
Commit, if approved: see "Review record" above.
Remaining blockers: none. P03 and P04 can consume this suite; P04 must wire
  the test's corrected mode to the real policy seam and flip
  DD_DMA_REQUIRE_CORRECTED.
Next eligible package and required inputs: P03 (explicit DD runtime-policy
  seam; inputs: repair plan §6.1 properties, validation §3 G01–G16, file
  ownership: core callbacks.c/plugin.c/h + app CoreInterface.java +
  parallel.cpp setter) — analysis/implementation independent of P02's files.
