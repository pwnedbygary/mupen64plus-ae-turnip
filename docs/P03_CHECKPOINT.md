# P03 checkpoint — explicit DD runtime-policy seam (2026-09-13)

Published from [HANDOFF_NEW.md](HANDOFF_NEW.md) (P03 section);
related record: commit `ae3388a9b` on `dd-eos-watchdog-checkpoint`.

## Review record (backfilled)

- Review loop: first review PASS with two nonblocking findings -> applied -> delta re-verified PASS; verdicts recorded in commit `ae3388a9b`'s
  message (the protocol-designated location for the full review
  narrative and the reviewed-snapshot hashes).
- The Checkpoint body below is extracted verbatim from the handoff
  block with only the two pending fields replaced.
- Snapshot table below is recomputed from the published commit
  itself (`git show ae3388a9b:<path>`), so it cannot drift from what
  was reviewed and committed.

| File (at this commit) | Status | SHA-256 (git show recomputed) |
|---|---|---|
| `app/src/main/java/paulscode/android/mupen64plusae/jni/CoreInterface.java` | M | `abcb190ab712dd7919aeedacb5077468bb2712fcce153044bc0bc94387d6f939` |
| `app/src/main/java/paulscode/android/mupen64plusae/jni/CoreTypes.java` | M | `48b5ffbd582833f849181b02971df61556316ca7ba6f67b60b8ae7ebb05c81be` |
| `docs/HANDOFF_NEW.md` | M | `7babdd0d790d85c7f4a9f929051371ca0d965544e04e2c7f8b40ec5ac7a84211` |
| `docs/N64DD_DMA_AUDIO_VALIDATION.md` | M | `e615bd1ceb031bd27d97a56f8d79c1a0c0384953115cc4c59cac0b23faac2324` |
| `mupen64plus-core/upstream/src/api/callbacks.c` | M | `a4be02bfd411b82a3f1e805f1394b900463938fc8c297990c15501bd1a721e37` |
| `mupen64plus-core/upstream/src/api/callbacks.h` | M | `35d24133d6c9d0d24f97793e5bad423806a360ad74f95a766d4810b2cb340d79` |
| `mupen64plus-core/upstream/src/api/frontend.c` | M | `124b27f7577b331eda05bef3dfb8a6996a960c0ea6a8998d329ba43f05dbff14` |
| `mupen64plus-core/upstream/src/api/m64p_types.h` | M | `5ec043350001a49ee3816917c5a793166388fa5c46fdf70be5e3faec557a3af4` |
| `mupen64plus-core/upstream/src/plugin/plugin.c` | M | `7eb89aed9d66381f470949eaa3ca204b4dfff46aa760cbde13809b7ccc715194` |
| `mupen64plus-core/upstream/src/plugin/plugin.h` | M | `8357d3862c5c3d00cc1feca2cf869691d2370af7f23a701b19e7301db9a4b51c` |
| `mupen64plus-rsp-parallel/upstream/dd_policy.cpp` | A | `faf15e50fbc68363c75f0de4ab7117024eb9c4d3dd1f4262e9b85edc48acfb92` |
| `mupen64plus-rsp-parallel/upstream/dd_policy.hpp` | A | `0aeb24fb56d7c78ba99999bfbaa70ccdca07ef347a01722c0062fadd06273d9a` |
| `mupen64plus-rsp-parallel/upstream/parallel.cpp` | M | `2d1a2745119eff5ab3b36a1e8e50e1781a6441d1e83312f907a618802e01a5a3` |
| `tools/test-dd-policy.sh` | A | `9af6b68fe18ad6f8274e27a490194126a9456effe3ee6f494db96cd013d72b04` |
| `tools/tests/dd-runtime-policy-test.c` | A | `ce4edb155d9b84e255c40d6aae91669d271a47de6c83b7aa37f815ea7ef0b84f` |
| `tools/tests/rsp-dd-policy-test.cpp` | A | `7fbbd05a59edbd4d9c789f8ebb13098e46b016ac57f7c56765dc439a32d34257` |

## Checkpoint

Package: P03 — Introduce the explicit DD runtime-policy seam (P03a+P03b+P03c
  delivered together; channel + lifecycle only; no DMA behavior changed)
Baseline / Reviewed snapshot: dd-eos-watchdog-checkpoint @ 47a2d8ce3; changed
  core TUs syntax-checked with clang (host gcc cc1 spawn broke mid-session,
  environment issue; callbacks.c additionally compiled+run by the new suite
  with gcc before that) and app Java verified by a successful
  ./gradlew :app:compileDebugJavaWithJavac --offline build.
Verified observations: preference trace recorded — GamePrefs.enable64dd
  (key "support64dd") -> CoreService launch -> CoreInterface.coreStartup
  (enable64DdSupport, isNdd). The pre-existing gates are unusable as policy
  authority, exactly as the plan warned: dd_startup_diagnostics is computed
  inside SetDebugCallback from pFunc != NULL && getenv(M64P_DD_STARTUP_DIAG-
  NOSTICS)=="1", and M64P_DD_COMBO_CART_BOOT is a device.c boot-order
  experiment, not DD activation. The core already had the optional-symbol
  pattern for RSP plugins (plugin.c resolved DdStartupDiagnosticsSetCallback).
Derived results and inputs: new channel M64CMD_DD_RUNTIME_POLICY_SET (enum
  appended after M64CMD_ROM_SET_SETTINGS; CoreTypes.java also gained the two
  previously missing PIF_OPEN/ROM_SET_SETTINGS entries to keep ordinals
  aligned). Core state SetDdRuntimePolicy/DdRuntimePolicyGet in api/callbacks
  .c (default off, strict 0/1, atomics, independent of debug callback and
  budgets). frontend.c: new case (rejects while emulator running ->
  serialized with emulation) + clear-on-ROM-CLOSE that also pushes 0 to a
  still-attached plugin. plugin.c: optional DdRspRuntimePolicySet resolved at
  RSP connect, current policy pushed on connect and on change
  (plugin_update_dd_runtime_policy), detach pushes 0; absence while policy
  enabled logs a plain "corrected DMA policy cannot be applied" warning
  (G13). Parallel-RSP: new dd_policy.hpp/.cpp atomic receiver +
  DdRspRuntimePolicySet export in parallel.cpp; P04 will consume
  RSP::DdRuntimePolicyEnabled() in cp0.cpp (untouched here).
Remaining hypotheses: none material; the connect-time propagation path is
  verified by code review only until P06 exercises it natively (stated gap);
  G08 reset, G11/G12 savestates, G14 budgets and G15 warm-JIT are satisfied
  by construction (nothing else reads or serializes the policy) and will be
  re-verified in the P06/P10 native runs.
Changed files: api/m64p_types.h, api/callbacks.h, api/callbacks.c,
  api/frontend.c, plugin/plugin.h, plugin/plugin.c,
  mupen64plus-rsp-parallel/upstream/{dd_policy.hpp,dd_policy.cpp,parallel.cpp},
  app CoreTypes.java + CoreInterface.java, tools/tests/
  {dd-runtime-policy-test.c,rsp-dd-policy-test.cpp}, tools/test-dd-policy.sh,
  docs/N64DD_DMA_AUDIO_VALIDATION.md (§7), docs/HANDOFF_NEW.md (this section).
Checks actually run and why: bash tools/test-dd-policy.sh (core-state truth
  table + receiver readback, both pass); bash tools/test-dd-dma-transfer.sh
  re-run (legacy suite all-pass, corrected XFAILs unchanged -> P03 changed no
  DMA behavior); test-dd-startup.sh callbacks step, test-dd-core-imem-dma.sh
  and test-dd-root-stacks.sh all pass (exit 0); ./gradlew
  :app:compileDebugJavaWithJavac --offline BUILD SUCCESSFUL; clang
  -fsyntax-only on frontend.c, plugin.c, parallel.cpp.
Independent reviewer and verdict: see "Review record" above.
Commit, if approved: see "Review record" above.
Remaining blockers: none for P04.
Next eligible package and required inputs: P04 — minimal internal-read DMA
  correction behind RSP::DdRuntimePolicyEnabled() (inputs: P01 ledger §4
  decision rows, P02 fixture suite with DD_DMA_REQUIRE_CORRECTED=1 as the
  gate, cp0.cpp rsp_dma_read; the corrected mode of the P02 suite must be
  wired to drive the new seam and the runner default flipped).
