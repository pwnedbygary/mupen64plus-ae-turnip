# P04 checkpoint — internal-read DMA correction (2026-09-13)

Published from [HANDOFF_NEW.md](HANDOFF_NEW.md) (P04 section);
related record: commit `731694b58` on `dd-eos-watchdog-checkpoint`.

## Review record (backfilled)

- Review loop: first review PASS with nonblocking findings and normalization suggestions -> applied -> delta re-verified PASS; verdicts recorded in commit `731694b58`'s
  message (the protocol-designated location for the full review
  narrative and the reviewed-snapshot hashes).
- The Checkpoint body below is extracted verbatim from the handoff
  block with only the two pending fields replaced.
- Snapshot table below is recomputed from the published commit
  itself (`git show 731694b58:<path>`), so it cannot drift from what
  was reviewed and committed.

| File (at this commit) | Status | SHA-256 (git show recomputed) |
|---|---|---|
| `docs/HANDOFF_NEW.md` | M | `ccd12260e35704e0e8f945f8b830c0f31199ad73893d41abf1185452576ebb2e` |
| `docs/N64DD_DMA_AUDIO_VALIDATION.md` | M | `ebc5d618f078961006b05bc45aaa8b72e3aa6ffadb77a3d1841a0fdbc67bcfc1` |
| `mupen64plus-rsp-parallel/upstream/CMakeLists.txt` | M | `7a7e8ce02251e6ad41db0c58706f13ac73169cea038997d8379aea6346e4249d` |
| `mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp` | M | `a164069dba471125ab57bda5e1dc1eda4f59da12dd542edd94f6e118dbad7a94` |
| `tools/test-dd-dma-transfer.sh` | M | `416b71db1221da0def85d8dabb53833ded863ad372155aca6a04e7f83c3ac799` |
| `tools/tests/rsp-dd-dma-transfer-test.cpp` | M | `20430daa80fe37018a9539f0b7f54c64b40615ed753bb5236836af66c2aa87df` |

## Checkpoint

Package: P04 — Correct only internal RSP DMA reads (production correction behind
  the P03 explicit policy seam; rsp_dma_write, core DMA and CXD4 untouched)
Baseline / Reviewed snapshot: dd-eos-watchdog-checkpoint @ ae3388a9b; changed
  files: mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp,
  upstream/CMakeLists.txt, tools/tests/rsp-dd-dma-transfer-test.cpp,
  tools/test-dd-dma-transfer.sh, docs/N64DD_DMA_AUDIO_VALIDATION.md §7,
  docs/HANDOFF_NEW.md.
Verified observations: the corrected arm lives in the same production
  translation unit as the legacy body, dispatches on RSP::DdRuntimePolicy
  Enabled() (the P03 receiver) after the raw-operand snapshot, and implements
  the P01 ledger §4 rows 1-3, 5-7, 9-10, 12 exactly (latched bank, 12-bit wrap,
  full row length with no clamp, 8-byte beat copies, skip aligned to 8 and
  added only between rows, final SP (region<<12)|offset and final DRAM
  &0xffffff poststates, dirty blocks only for actual IMEM writes with the
  production expression, unchanged scheduling/return). The legacy body is
  byte-for-byte the pre-P04 code and keeps its pre-copy register mutation.
  The P02 fixture's corrected mode now drives the real seam
  (RSP::DdRuntimePolicySet) instead of expecting pre-fix divergences; the
  XFAIL/XPASS machinery and DD_DMA_REQUIRE_CORRECTED gate were removed as
  obsolete, and both modes are hard gates.
Derived results and inputs: all 11 corrected-kind cases (10 pre-existing plus
  the new D02diag) pass through the production arm against the independent
  oracle, including D02 (captured
  request: 262,144 word writes, zero IMEM writes, final registers 0xfb0/
  0x1fe808) and a new D02diag case with diagnostics enabled: the production
  observer still records the suspect request (payload=262144,
  imem_write_word_count=0) — the request remains observable after the
  correction (operating rule 8); P05 still owes the full evidence contract
  (raw/effective skip separation, bounded trigger independent of actual IMEM
  writes). Legacy suite: D01 observer counts 5120/3052 unchanged, D22a/b,
  D14L, D18, D21 (128 random) and D23 write pins all pass — DD-off behavior
  is unchanged. dd_policy.cpp joined the plugin build via upstream/
  CMakeLists.txt (the Android APK build itself remains a P06 step).
Remaining hypotheses: none for the host-level correction; native acceptance
  (device dynarec run, menus/gameplay/audio, cart/save regressions) is
  entirely outstanding and gated on P05+P06. The corrected arm's diagnostics
  observation currently reuses the DDSTART11 schema with raw skip recorded;
  P05 separates raw vs effective geometry and adds the bounded trigger.
Changed files: as listed in Baseline (6 paths).
Checks actually run and why: bash tools/test-dd-dma-transfer.sh (both modes
  all-pass, incl. D02diag observer line); bash tools/test-dd-policy.sh (seam
  suites pass); bash tools/test-dd-core-imem-dma.sh (pass); test-dd-startup
  .sh callbacks step passes (dynarec compile step still fails on the
  pre-existing gcc16 -Werror issue, untouched by P04); test-dd-root-stacks
  .sh exit 0; clang -fsyntax-only clean on cp0.cpp and parallel.cpp.
Independent reviewer and verdict: see "Review record" above.
Commit, if approved: see "Review record" above.
Remaining blockers: none for P05.
Next eligible package and required inputs: P05 — bounded post-correction
  evidence (inputs: rsp_diag.cpp/.hpp schema, corrected-arm observation
  points in cp0.cpp, the raw-request-shape trigger requirement 0xfb0/0/0x
  ffffffff shape, validation §4 A01-A11; keep DDSTART11 legacy observer
  behavior for DD-off sessions).
