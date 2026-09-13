# DMA/audio repair validation and release runbook

Use with [the repair plan](N64DD_DMA_AUDIO_REPAIR_PLAN.md) and
[work packages](N64DD_DMA_AUDIO_WORK_PACKAGES.md).

## 1. Separate the kinds of proof

| Verification | What it establishes | What it does not establish |
|---|---|---|
| Address-level oracle | Expected bytes/addresses for the selected documented model | Actual production loop correctness |
| Production host fixture | Real copy path, masks, dirty flags and registers | Android JIT integration or gameplay |
| ABI/lifecycle tests | Policy and callback propagation in exercised paths | Every plugin/session transition on device |
| APK/package checks | Build contents, signer, ABI libraries and symbol identity | Correct native execution |
| Native transfer trace | Actual corrected/legacy transfer and policy | Complete game/audio/save behavior |
| Native acceptance matrix | Observed end-to-end behavior for tested cases | Untested games, devices or unexplored paths |

Record all of these honestly. Do not replace one row with another.

## 2. DMA fixture matrix

Use guarded DMEM/IMEM arrays and nonuniform synthetic source data.
Run fixtures against the **production path** and an independently specified oracle.
For each case assert copied bytes, untouched bytes, actual bank, dirty flags,
final registers, observation counters and guard integrity.

Unless specified otherwise, run both DD-off legacy and DD-on corrected modes.
No fixture requires proprietary ROM or microcode data.

| ID | Case | Required assertion |
|---|---|---|
| D01 | Captured request: SP `0xfb0`, DRAM 0, length `0xffffffff` | Legacy reproduces 80-byte rows, 5120 words and 3052 IMEM writes |
| D02 | Same request, candidate corrected policy; target approved by P01 (`docs/P01_DMA_POLICY_LEDGER.md` §4/§5) | Target: 4096-byte rows × 256 = 1,048,576 copied bytes; DMEM bank retained; IMEM unchanged; final registers `0xfb0`/`0x1fe808` |
| D03 | Start `0x0000`, short DMEM row | Ordinary bytes and poststate correct |
| D04 | Start `0x1000`, short IMEM row | Only intended IMEM bytes change; proper dirty/invalidation state |
| D05 | DMEM exact fit ending at `0x1000` | No off-by-one crossing or lost final beat |
| D06 | DMEM row exceeding remaining bank space | Wrap to start of DMEM, never IMEM |
| D07 | IMEM row exceeding remaining bank space | Wrap within IMEM, never DMEM |
| D08 | Several rows individually within a bank but cumulatively crossing | Bank selection remains latched |
| D09 | Raw length fields 0, 7, 8, `0xffe`, `0xfff` | Approved beat rounding and no accidental zero-byte transfer |
| D10 | Raw counts 0, 1, `0xff` | Exactly 1, 2, 256 rows |
| D11 | Start offsets near boundary: `0xff0`, `0xff8`, `0xffc`, `0xfff` | Approved SP alignment and wrap behavior, including legacy differences |
| D12 | Initial DRAM low bits 0–7 | Approved alignment; no unsafe or silent alternate mapping |
| D13 | Skip fields 0, 1, 7, 8, `0xff8`, `0xfff` | Explicit selected skip model, including the observed low-bit case |
| D14 | High bits set in raw SP address | Selected bank/address and visible post-register bits follow policy |
| D15 | RDRAM end and mapped-boundary cases | Correct bus/backing-memory distinction and no observer out-of-bounds reads |
| D16 | Multiple wraps in the selected bank | Last-writer bytes match the independent oracle |
| D17 | Wrapped IMEM block boundaries | Every required affected JIT block invalidated; unrelated blocks untouched as specified |
| D18 | DMEM-only corrected transfer after a previous IMEM transfer | No stale dirty bookkeeping attributed to the new transfer |
| D19 | Final SP/DRAM/length registers | Approved count, skip-after-final-row, mask and readback rules |
| D20 | Diagnostics enabled/disabled on the same corrected input | Identical guest-visible state and memory |
| D21 | Repeated and randomized small transfers with fixed seed | Differential agreement with the selected model |
| D22 | Legacy DD-off differential corpus | Original memory/register/return semantics retained |
| D23 | Existing RSP DMA writes and core CPU-originated DMA | No unintended source changes; relevant existing tests retained |
| D24 | Sanitizers/guards with boundary fixtures | No added invalid read/write or host arithmetic UB |

### Captured-case oracle cautions

- The corrected request is **not** a zero-byte DMA: the encoded register already
  contains `0xffffffff`, which decodes to maximum row/count fields.
- D02's 1 MiB copied count is independent of which approved skip model is selected.
  Its expected source bytes and final DRAM register are not independent of that choice.
- Do not assert one final DRAM value until P01 decides low skip bits, address
  masking and whether trailing skip is visible.
- Current before/after core hashes and RSP hashes are not directly comparable:
  algorithms and covered ranges differ. Compare bytes or normalize explicitly.
- Observer eligibility may change after correction; lack of an old IMEM-write
  record is not itself a passing D02 result.

## 3. Runtime-policy and lifecycle matrix

| ID | Configuration/transition | Expected result |
|---|---|---|
| G01 | DD off, WritableROM off, diagnostics off | Legacy transfer behavior; no new observations |
| G02 | DD off, WritableROM on | Legacy transfer behavior; WritableROM continues independently |
| G03 | DD off with debug callback available | Callback presence does not activate corrected semantics |
| G04 | DD on, diagnostics on | Corrected policy and bounded observations |
| G05 | DD on, diagnostics off or callback null | Corrected policy still active |
| G06 | DD on → clean close → DD off | No policy or diagnostic context leakage |
| G07 | DD off → DD on | Policy active before first affected task |
| G08 | Reset inside a DD session | Policy retained; observation generations/reset rules documented |
| G09 | Failed plugin/ROM initialization | No stale policy on next launch |
| G10 | Plugin detach/reattach and shutdown | Stale callbacks/context not used; capability rediscovered |
| G11 | Load state inside authorized DD session | Policy follows current session; diagnostics do not use stale generations |
| G12 | Load state in DD-off session | State cannot silently enable the new policy |
| G13 | Old/alternate plugin lacks optional capability | ABI preserved; unsupported correction reported, not falsely claimed |
| G14 | Logging budget exhausted/disconnected | Guest behavior unchanged; correction does not turn off |
| G15 | Warm JIT cache and mode lifecycle | Runtime policy correct independently of whether exact-site probes were compiled |
| G16 | Plugin attached after policy selection, and failed attachment | Real receiver state/behavior verified; missing capability explicitly blocks correction acceptance |

Do not promise literally zero extra machine instructions in DD-off mode without
measurement. Require no new policy effects, memory probes, hashing, ring updates
or retained session state beyond the minimal gate plumbing.

## 4. Diagnostic and audio-provenance fixture matrix

| ID | Case | Required evidence |
|---|---|---|
| A01 | Zero extracted size with nonzero opcode/high bits | Full command retained; not mislabeled as an all-zero command |
| A02 | Zero low-24-bit source with nonzero high byte | Extracted zero distinguished from full register zero |
| A03 | Size decrement in a delay slot | Captured operands reflect actual executed delay-slot semantics |
| A04 | Two identical command pairs at different indices | Source index not inferred by value matching |
| A05 | Task buffer bounds/overflow/wrap | Unsafe snapshot explicitly skipped/truncated, not read blindly |
| A06 | Command mapped into DMEM then overwritten | Stale provenance invalidated or labeled uncertain |
| A07 | Buffer reused by next task generation | Writer assigned to correct generation; reuse chronology visible |
| A08 | Ring wraps before trigger | Overwrite count visible; lost history not described as complete |
| A09 | Trigger repeats after detailed budget exhausted | Explicit exhaustion; behavior remains unchanged |
| A10 | Corrected suspect DMA causes no IMEM write | Pre-copy trigger still records context; eligibility, actual zero IMEM writes and skipped-probe status reported separately |
| A11 | Diagnostics disabled | No heavy snapshots/hashes/writer-ring work; corrected policy retained |
| A12 | CPU SW/SH/SB/SD as implemented by target engine | Correct width, endian ordering, effective address and before/after bytes |
| A13 | Unaligned/partial stores supported by engine | Correct byte mask/merge; adjacent bytes preserved |
| A14 | Conditional-store failure and faulting store | Not logged as successful memory writes |
| A15 | Cached/uncached aliases and fast/slow store paths | Same physical range recognized; coverage declared |
| A16 | Exact site unavailable | `unknown`/`block` reported; no fabricated exact PC |
| A17 | JIT observer callout | Live host/guest registers and delay-slot state preserved |
| A18 | Non-CPU write into watched range | Writer classified separately; not attributed to last CPU PC |

A12–A18 are required only if P08 introduces that producer instrumentation.
They are not an instruction to implement every hook before the first corrected APK.

## 5. First corrected native run

### Before installation

1. Preserve existing game and save files using the established backup method.
   Back up only after clean emulator exit so the copy is consistent.
2. Verify package and certificate match the existing debug installation.
3. Record the new APK hash and packaged ARM64 core/RSP Build IDs.
4. Preserve matching unstripped symbols separately.
5. Record explicit DD setting, selected RSP plugin and selected CPU engine.
6. Do not change game assets, patches, graphics settings or unrelated timing options
   between baseline and candidate without recording that as a separate variable.

### Installation rule

Use `install -r`. If Android reports incompatible signing or another install
failure, stop. **Do not solve it with uninstall, clear-data, or deleting saves.**
Resolve signing/build identity using the established local setup.

### During the run

1. Verify the native log actually says Dynamic Recompiler.
2. Verify a startup record/capability states corrected DD policy is active
   independently of diagnostic availability.
3. Follow the same boot/menu/game route as the baseline.
4. Capture the suspect command and corrected transfer if it occurs.
5. If the game freezes, keep it frozen during capture. If it does not, record the
   exact successful route and continue representative gameplay/audio testing.
6. Retrieve only after this run's own terminal completion status.
7. Confirm process start identity and timestamps; do not mix sessions or ASLR bases.

### First-run decisions

| Result | Meaning / next step |
|---|---|
| Policy absent or wrong engine | Configuration/build failure; no conclusion about the fix |
| Corrected event still writes IMEM from DMEM | Implementation/policy failure; inspect production fixture gap |
| Event no longer logged, coverage uncertain | Diagnostic failure; do not declare overwrite prevented |
| Event logged, IMEM stable, task returns, game works | DMA correction supported; proceed to regressions and classify command validity |
| IMEM stable, RSP still spins | Investigate command/control flow at P07 |
| RSP returns, CPU stalls elsewhere | Diagnose the new observed boundary; do not label it the old RSP stall |
| Game works but audio corrupt/missing | P07: verify actual audio behavior and command stream |
| Original trigger never occurs on an otherwise good run | Useful functional result, but direct edge-case native evidence still missing |

Do not force a “second audio bug” if the corrected hardware behavior explains
the symptoms and the command is valid. Conversely, do not call audio verified
merely because menus now render.

## 6. Final native acceptance matrix

Record the actual route, approximate duration, repeats, observations and artifact
identity. Suggested minimums are starting procedures, not proof of universal coverage.

| ID | Scenario | Required observation |
|---|---|---|
| N01 | DD cold boot under dynarec, repeat three times | Native menu reached; no repeat RSP corruption/spin |
| N02 | Menu navigation and loading transitions | Responsive input; no hidden task stall |
| N03 | Representative DD gameplay, at least one complete event/race where applicable | Stable progression, rendering and controls |
| N04 | Music and effects during boot, menu, gameplay and transitions | Audibly present/correct; no persistent silence, looping glitch or severe distortion |
| N05 | Clean exit then cold-process relaunch | Same working behavior and selected policy |
| N06 | DD diagnostics disabled | Same correction/functionality, without observation dependence |
| N07 | DD session followed by DD-disabled plain cart | Plain cart works; no policy leakage |
| N08 | Plain cart normal save and cold reload | Expected progress/options persist |
| N09 | WritableROM independently enabled with DD off | Authorized writes persist after clean exit/cold reload |
| N10 | Mario Kart Amped Up regression, where available/applicable | Existing boot and writable/save behavior retained |
| N11 | Mario Tennis regression, where available/applicable | Existing boot and save behavior retained |
| N12 | EK Cart Hack regression, where available/applicable | Existing writable/save behavior retained |
| N13 | DD disk overlay/save path, where supported | Expected writes survive clean restart; source assets preserved |
| N14 | Reset and supported state-load paths | Policy/lifecycle stable; no old task/probe generation confusion |
| N15 | Cached interpreter comparison, if run | Effective engine and required full dispatcher coverage recorded |
| N16 | Corrected build logging-on/off comparison | No material observation-induced failure; overhead measured rather than guessed |

### Audio verification details

- Listen to recognizable music and several effects, not merely one beep.
- Check transitions where tracks/effects change and overlapping sounds occur.
- Separate a device output-routing/volume problem from missing emulated samples.
- Record whether the emulator makes progress while sound fails.
- Callback counts, nonzero buffers and sample-rate logs help diagnosis but do not
  independently establish correct audible output.
- If needed and permitted, retain a short local audio/video recording for comparison.
  Do not publish copyrighted gameplay/audio captures by default.

### Persistence procedure

For each relevant save/writable case:

1. Begin from a backed-up state and record the intended small, reversible change.
2. Make the change using normal in-game behavior.
3. Exit through the emulator's normal clean-save path and confirm completion.
4. Record expected save/output file existence and relevant timestamps/hashes locally.
5. Relaunch in a fresh process and verify the change in-game.
6. Distinguish normal save files, disk overlays and actual writable-cart outputs.
7. Confirm unrelated originals were not changed.

Do not force-stop during an active save, assume a savestate proves persistent
storage, or delete existing data to make a test appear clean.
If a required game/asset/device is unavailable, mark the row **BLOCKED**.
If a feature truly does not apply, use **N/A** with a reason, not an invented pass.

### Cached-interpreter comparison, only if used

Confirm the actual engine in the native trace. Locate the historical dispatcher
diagnostics instructions and enumerate the current production dispatch entry paths,
including ordinary execution and delay-slot execution. Verify instrumentation
coverage against those paths rather than assuming one central-looking function
covers them all. Keep coverage of source paths distinct from opcodes actually
executed in a particular run. Label missing paths explicitly; a handful of sampled
PCs is not a full dispatcher pass. A successful interpreter comparison does not
replace native dynarec acceptance.

## 7. Existing commands and portability

These existing host scripts were verified to exist when the plan was written:

```bash
bash tools/test-dd-startup.sh
bash tools/test-dd-policy.sh
bash tools/test-dd-dma-transfer.sh
bash tools/test-dd-core-imem-dma.sh
bash tools/test-dd-rsp-mac.sh
bash tools/test-dd-root-stacks.sh
```

`tools/test-dd-dma-transfer.sh` (added by P02) runs the production-path SP DMA
fixtures of `tools/tests/rsp-dd-dma-transfer-test.cpp` against the oracle from
the P01 policy ledger. Its legacy suite must always pass; its corrected suite
reports the documented pre-fix divergences (XFAIL) until the P03/P04 policy
seam lands, and `DD_DMA_REQUIRE_CORRECTED=1 bash tools/test-dd-dma-transfer.sh`
must be used to gate commits after P04.

`tools/test-dd-policy.sh` (added by P03) compiles the real `api/callbacks.c`
and asserts the core-side DD runtime-policy truth table (default off, strict
values, independence from the debug callback and from the diagnostics gate),
then the Parallel-RSP receiver's readback contract (`dd_policy.cpp`) and its
independence from the diagnostics observer. The full validation §3 lifecycle
matrix (connect/detach, reset, savestates, old-plugin handling) is verified
by the reviewed code paths plus the P06 native run; the host suite covers the
state semantics only.

Run from the repository root in the supported host environment.
Some fixtures use GNU/Linux linker options such as `-Wl,--gc-sections`.
Apple's linker does not accept all of them unchanged. A macOS adaptation must
preserve the production objects/assertions, not skip the tests. Document the
adaptation as a separate tooling change.

The new transfer fixture's command is a P02 deliverable; do not claim it already
exists. Include it in the actual final command ledger.

### Building

The Replit project documents:

```bash
./tools/replit-build.sh
```

The application is native Android; there is no browser preview acceptance test.
Inspect the build helper before using it in another environment. A local Android
checkout may use its established Gradle/Android Studio signing setup instead.

Retained native outputs do not prove Gradle dependencies are cached. Use offline
mode only when available; dependency-resolution failure is not a native-code result.
Do not publish or transfer the ignored Replit signing init script as documentation.

### Mac capture: one self-contained physical line

Run from the checkout containing `tools/capture-dd-rsp-mac.sh`, after downloading
the **new candidate** and obtaining its independently recorded hash.
The prompts request a file path and public checksum, not credentials:

```bash
printf 'Full APK path: '; IFS= read -r APK; printf 'Expected APK SHA-256: '; IFS= read -r APK_SHA; /bin/bash tools/capture-dd-rsp-mac.sh "$APK" "$APK_SHA"
```

The helper uses `~/Downloads/platform-tools/adb`; do not assume `adb` is on PATH.
Follow its existing on-device root-menu instructions and per-run completion wait.
Do not replace the per-capture status with a shared “done” marker.
The old `ddstart10-rsp` ZIP prefix is harmless; always identify the installed APK
by hash. A future generic prefix is optional housekeeping, not part of the DMA fix.

## 8. Review requirements before native delivery

Every commit must first pass the
[independent commit-review protocol](N64DD_DMA_AUDIO_WORK_PACKAGES.md#independent-review-before-every-commit),
including documentation, diagnostic-only, fixup and amended commits. A native
delivery review is an additional release gate, not a substitute for per-commit
review. If delegation is unavailable, use a fresh model session or human reviewer;
otherwise committing is blocked. This document does not install an enforcement hook.

An independent reviewer should answer:

- Does the production loop implement every P01 decision?
- Is the selected bank immutable through every row and wrap?
- Is full row length retained?
- Are source progression and post-register values explicitly tested?
- Are actual IMEM writes the only reason to dirty IMEM under the correction?
- Can logging availability/budget affect corrected semantics?
- Does DD-off retain its prior behavior, including WritableROM independence?
- Can old session state/callbacks leak into a new game?
- Are probes bounded and all added reads safe?
- Does the post-correction trigger still record the suspect audio condition?
- Are exact site labels justified by actual emission/fetch sites?
- Did any unrelated timing, interrupt, yield, boot or DMA-write behavior change?

Resolve correctness blockers before asking the user to spend time on another capture.

## 9. Release and evidence ledger template

```markdown
# Candidate release

## Identity
- Source commit:
- Relevant dirty diff:
- Published branch/commit:
- APK filename and SHA-256:
- Package/version:
- Certificate SHA-256:
- Packaged native Build IDs:
- Matching preserved symbols:
- Toolchain and ABI list:

## Selected policy
- DD activation source:
- Optional plugin capability:
- Bank/row/alignment/skip/post-register policy:
- Reference revision:
- Deliberately unchanged behavior:

## Verification
- Host commands/results:
- Independent review:
- Native device/engine/settings:
- Native run/capture identity:
- DMA event and actual IMEM result:
- Audio command finding and confidence:
- Acceptance matrix:
- Persistence results:

## Limitations
- Failed/blocked/N/A checks:
- Unresolved hypotheses:
- Diagnostic gaps or overhead:

## Delivery
- Update-only install instructions:
- Save protection:
- Local-only artifacts:
- Exact next action or completed acceptance statement:
```

## 10. Stop rules

Stop and document rather than improvising when:

- The policy cannot be specified without unresolved byte-level behavior.
- The real production transfer is not exercised by tests.
- The DD correction depends on logging.
- The candidate needs uninstall/data deletion to install.
- The new trace cannot distinguish task generations or actual command origins.
- A CPU producer claim rests only on sampled/block PCs.
- A required native/save check is unavailable or fails.

Do not mark the repair complete until required evidence exists. A planning
deliverable can be complete while emulator implementation and native validation
remain open.