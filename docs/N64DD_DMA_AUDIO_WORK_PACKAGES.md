# Bite-sized local-LLM work packages

Use with [the repair plan](N64DD_DMA_AUDIO_REPAIR_PLAN.md) and
[the validation runbook](N64DD_DMA_AUDIO_VALIDATION.md).
These are assignments to execute later; writing this document does not execute them.

## Common prompt to prepend to every assignment

Prepend the diagnostic prompt and commit-review protocol below as well. They apply
to every package, including documentation, tests, diagnostic-only patches and fixes.

> Work only on package PXX below. Read the repair plan's evidence limits and
> non-negotiable constraints. Verify named files/symbols before editing.
> Preserve DDSTART9, DD-disabled behavior, independently enabled WritableROM,
> saves and original signing identity. Do not import historical experiments.
> Separate direct observations, reference comparisons and hypotheses.
> Do not fix a later package preemptively. Do not invent an exact PC, a native
> pass, a hardware rule or a new audio diagnosis.
> If a required input is missing, return a precise blocker and the smallest
> evidence needed. Do not work around it by changing unrelated behavior.
> End with changed files, actual tests/results, unresolved questions, and the
> exact input needed by the next package. Do not claim the overall repair complete.

### Context budget and working discipline

- Supply this common prompt, one package, and its prerequisite result.
- Include only the relevant evidence and source files. Do not paste the entire
  historical handoff into every local-model session.
- Aim for one coherent reviewable change, usually one to three implementation
  files plus focused tests. If an assignment expands, split at an interface or
  evidence boundary before editing additional subsystems.
- The model should produce a patch and results, not a narrative alone, for
  implementation packages. Analysis packages must not change execution.
- Create a commit only after the independent review gate below passes. An automatic
  workspace checkpoint is not a reviewed commit or release. Do not overwrite unrelated
  working changes or force-update the shared branch.
- “Unknown” is an acceptable evidence result. An invented answer is not.
- ARM64 generated-code modifications require experienced review; delegate those
  to a stronger model/human if the local model cannot explain register/ABI safety.

## Evidence-based diagnostic prompt

Copy this prompt into the implementing agent's initial context:

```text
Act as an empirical emulator diagnostician, not an advocate for a preferred fix.
Work only on the assigned package and preserve the repair plan's constraints.
Produce concise, auditable findings and decisions, not private internal reasoning.

1. Establish identity before interpreting behavior: current source/diff, APK hash,
   effective engine, explicit DD mode, plugin/capability, capture/run identity,
   and relevant task or buffer generation. Mark unavailable fields UNKNOWN.
   A build label or frontend selection alone does not prove runtime identity.
2. Maintain a claim ledger:
   claim | DIRECT / DERIVED / REFERENCE / HYPOTHESIS / UNKNOWN |
   exact artifact and location | limits | next discriminating check.
   Cite trace event/sequence/time, source symbol and revision, or pinned reference.
   For derived values, supply inputs and a reproducible calculation.
3. Start with the earliest demonstrated divergence, not the final symptom.
   An observed overwrite establishes that operation, not why its inputs arose.
   Sampled PCs and JIT allocation containment are not exact guest writers.
   Static disassembly is not proof that a particular site executed in this run.
4. For unresolved behavior, retain competing explanations that fit the evidence.
   For each, state the predicted observation and what would contradict it.
   Choose the smallest safe experiment that distinguishes the leading alternatives.
   Do not add unrelated probes or change multiple behaviors in the same experiment.
5. Check whether a probe could miss the event: enablement, callback availability,
   eligibility, budget, deduplication, coverage, lifecycle and compiled fast paths.
   No record is not proof of no event. After a fix, retain a bounded independent
   trigger if the fix removes the old trigger condition.
6. Treat this plan as a baseline, not authority over newer evidence. Verify current
   source and pinned references. Record contradictions explicitly. Reference
   disagreement requires a documented decision; do not invent consensus.
7. Keep demonstrated DMA transfer-model defects separate from the audio input
   question. A zero extracted size/source field alone does not prove an invalid
   command or producer corruption. Distinguish producer, consumer, corruption,
   legal encoding and premature reuse until evidence separates them.
8. Correct only an established defect after the package's decision gates pass.
   Preserve DDSTART9, dynarec, DD-off behavior, independent WritableROM and saves.
   DD correction policy must not depend on logging. Do not suppress symptoms with
   watchdogs, fake completion/yield signals or timing changes.
9. Test the actual production path against independent expectations. Distinguish
   a reproduced defect, a passing host fixture and successful native acceptance.
   Record exact commands, actual results, coverage gaps and unavailable checks.
10. Stop when required evidence, capability, coverage or review is missing.
    Return the smallest specific next action. Never invent a successful run.

Before handing off, report:
- Verified observations and derived results, with citations.
- Leading conclusion and its scope; alternatives still consistent with evidence.
- Changed files and why this patch is the minimum justified change.
- Checks actually performed and their results, including failures.
- Independent reviewer verdict and exact reviewed snapshot.
- Remaining unknowns and the next discriminating observation.
Do not declare the overall repair complete from a package-level success.
```

## Independent review before EVERY commit

This is a required workflow gate, not an automatically installed Git hook.
It covers source, tests, documentation, follow-up fixes, amended commits and
publication commits. No agent may describe a self-review as independent review.

### Coordinator procedure

1. Finish one coherent patch and proportionate checks. Freeze the proposed commit
   snapshot: base revision, intended paths, exact diff, and hashes for new files.
   Include untracked files; a plain `git diff` can omit them. Keep unrelated dirty
   files outside the proposed commit. Do not stage or publish secrets/raw assets.
2. Spawn a separate read-only reviewer/architect subagent using the platform's
   actual delegation facility. Give it the reviewer prompt below, the exact
   snapshot, package requirements, relevant full source, evidence and test output.
   The reviewer must not edit, commit, push or silently run device operations.
3. If subagents are unavailable, open a separate fresh model session or obtain
   a human review of the same snapshot. A separate session of the same model is
   acceptable; a second model is optional. Do not invent tool calls or pretend a
   second persona in the implementing session provides independent review.
   If no independent reviewer is available, retain the patch and mark COMMIT
   BLOCKED. Report the missing capability; do not silently waive this requirement.
4. Resolve each finding explicitly: fix it, or provide evidence for disagreement.
   Send the changed snapshot and response to the reviewer. A disputed blocker
   remains blocking until resolved by the reviewer or an independent human.
   Request focused re-review of the changed areas and affected interactions.
5. Require PASS for the exact final snapshot, with no unresolved correctness,
   evidence, scope, safety or required-validation blockers. Nonblocking suggestions
   may remain if documented. NEEDS CHANGES and BLOCKED both prohibit committing.
6. Verify the intended commit still matches the reviewed files/diff and base.
   Any code, test or document change after review requires review of that delta.
   Stage only reviewed paths. If the remote base moved, inspect the integration
   delta and obtain review of the resulting candidate before non-force publication.
   Never reuse approval for an unexamined merge or silently overwrite newer work.
7. Keep the reviewer/session identifier, reviewed snapshot identifier, verdict,
   findings/dispositions and actual check results in a review record or commit
   message. Commit metadata can hold the final verdict without editing reviewed
   files merely to record approval. Human-created fixup/amend commits follow the
   same gate; automatic platform backups do not count as approved commits.

### Copy-pastable reviewer prompt

```text
You are an independent read-only reviewer, not the implementing agent.
Review this proposed commit against its assigned package and the N64DD plan.
Do not edit files, commit, push, or operate a device. Do not assume the author's
summary is correct. Inspect the exact diff, new files, relevant surrounding source,
evidence and actual check results. Request missing inputs rather than guessing.

Review:
- Are conclusions supported by direct/derived/reference evidence at the claimed
  scope? Are sampled PCs, static paths or missing logs overstated?
- Does the patch solve only an established defect or add justified bounded
  diagnostics? Are competing explanations and unverified assumptions preserved?
- Are all package prerequisites satisfied? In particular, are complete DMA policy
  decisions approved before correction, and production-path fixtures real?
- Are DD policy and logging independent? Are attach/detach, reset, old-plugin
  capability failure and DD-off relaunch safe?
- Are full row geometry, bank behavior, alignment/skip, memory mapping, final
  registers, dirty/JIT state and completion semantics consistent with the approved
  policy? Do not silently extend the fix into CPU DMA or RSP DMA writes.
- Can post-fix probes still see the suspect request? Are eligibility, actual IMEM
  writes, dropped/skipped probes, exact-site confidence and generations distinct?
- Are dynarec fast paths, delay slots, widths and ABI/register preservation covered
  where affected? Are independent expected values tested through production code?
- Are DDSTART9, plain carts, independently enabled WritableROM, saves and signing
  protected? Is native acceptance honestly separated from host verification?
- Are raw captures, game assets, binaries, secrets and unrelated changes excluded?

Apply checks relevant to this patch; mark the rest N/A with a reason.
For documentation-only changes, inspect consistency, evidence limits, executable
instructions and links. Do not demand an emulator build for unchanged runtime code.
For implementation changes, identify required checks that are missing or failed.

Return:
1. Snapshot reviewed: base, exact patch/file identifiers and intended paths.
2. Verdict: PASS / NEEDS CHANGES / BLOCKED.
3. Findings: severity, file/location, concrete problem, evidence and required action.
4. Validation examined and limitations; do not claim tests you did not observe/run.
5. Nonblocking suggestions, separately.

PASS means this exact commit is acceptable for its scope, not that native repair
or release is complete. Missing required inputs or checks means BLOCKED.
```

## P00 — Establish the current baseline

**Type:** read-only inventory plus documentation.

**Goal:** Give every later package a reproducible starting point.

**Inputs**

- Current checkout and live branch status.
- `docs/DDSTART11_NATIVE_ANALYSIS.md`.
- DDSTART11 APK/capture identities from the repair plan.
- Existing local signing/build arrangements; never expose private material.

**Steps**

1. Record the current commit, relevant dirty diff and any concurrent work.
2. Verify the known source/analysis publications exist in history; do not reset.
3. Confirm `rsp_dma_read` still uses the 80-byte clamp and 13-bit destination
   progression for the captured request. If already changed, analyze that delta.
4. Inventory available reference archives, native symbols and legally supplied
   game/IPL/disk assets without committing their contents.
5. Record toolchain, supported ABIs, package/signing identity and device availability.
6. Record the existing host test commands, including platform-specific linker flags.
7. Produce a compact fact/unknown ledger matching the DDSTART11 record.

**Output:** a baseline record and a source-file ownership list.

**Acceptance:** another engineer can identify the exact source, APK and capture
being compared. Missing references/device access are explicitly listed.

**Stop:** changed baseline, unaccounted dirty source, or mismatched APK identity.
Resolve identity before diagnosing new behavior.

## P01 — Decide the complete DMA policy

**Type:** analysis; no emulator changes.

**Goal:** Resolve the reference-policy table before an LLM writes convenient masks.

**Read**

- `mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp`, especially `rsp_dma_read`.
- Supplied/pinned Ares DMA register definitions, decoding and transfer loop.
- CXD4 `mupen64plus-rsp-cxd4/upstream/su.c` for disagreement, not blind authority.
- Core `mupen64plus-core/upstream/src/device/rcp/rsp/rsp_core.c` for scope contrast.

**Steps**

1. Write out the legacy decode, copies, dirty flags and final register values.
2. Write an address-level reference model with bank, offset, row length, rows,
   source stride, alignment and post-transfer state.
3. Complete every decision row in repair-plan §6.2.
4. Explicitly calculate the observed `0xfb0 / 0 / 0xffffffff` request.
5. Decide low skip bits and whether the final row includes a visible trailing skip.
6. Distinguish bus address masks from backing-array bounds and actual RAM size.
7. Identify any behavior deliberately left unchanged and its consequences.

**Output:** an approved policy ledger with pinned references and proposed fixture
IDs. Identify who reviewed it.

**Acceptance:** no implicit alignment/skip/poststate choices remain in the coding
specification. There is a clearly defined DD-off legacy policy.

**Stop:** unresolved source disagreement that changes expected bytes. Obtain a
more authoritative reference or document a narrowly scoped candidate explicitly;
do not manufacture “hardware certainty.”

## P02 — Build tests that exercise the real transfer

**Type:** test infrastructure and fixtures.

**Depends on:** P01. May run alongside P03 with separate file ownership.

**Goal:** Catch the existing fault before implementing the correction.

**Starting points**

- `tools/tests/rsp-dd-provenance-test.cpp` tests observers, not the full DMA copy.
- `tools/test-dd-startup.sh`.
- Production `rsp/cp0.cpp` and its state/register interfaces.

**Steps**

1. Add a narrowly scoped host fixture that invokes the actual production DMA
   path through an existing callable interface or a minimal test seam.
2. Stub only external plumbing. Do not replace the copy algorithm with test code.
3. Provide separate guarded 4 KiB DMEM and IMEM arrays and bounded synthetic RDRAM.
4. Fill memory with deterministic nonuniform patterns that expose bank/source
   mistakes, rather than uniform zero memory.
5. Implement an independent address/byte oracle using P01, not the production helper.
6. Add the fixtures in validation §2, including the captured geometry.
7. For the legacy case, demonstrate 5120 copied words and 3052 IMEM writes.
8. Add corrected-policy expectations that fail against the current implementation.
9. Check memory bytes, dirty flags, untouched memory and final registers—not only
   a trace message or a returned success code.

**Output:** test source, a documented runnable test command, expected pre-fix failure.

**Acceptance:** the real faulty path reproduces the overwrite; corrected expectations
fail for the intended reason. Guarded/sanitized tests do not hide unsafe accesses.

**Stop:** a test exercises only `rsp_diag.cpp` or agrees with copied production
constants without independent expectations. That is not coverage of the fix.
If the static/internal production function cannot be invoked through the chosen
fixture, fix the test seam or report a blocker; do not substitute an extracted
duplicate loop and call P02 complete.

## P03 — Introduce the explicit DD runtime-policy seam

**Type:** small configuration/lifecycle implementation.

**Depends on:** P00/P01.

**Goal:** Hardware semantics must not depend on whether logging is available.

**Inspect, then edit only what is necessary**

- `app/.../jni/CoreInterface.java`: launch-time explicit DD selection.
- `mupen64plus-core/upstream/src/api/callbacks.c`: diagnostic gate, not policy authority.
- `mupen64plus-core/upstream/src/plugin/plugin.c` and `.h`: optional RSP bridge.
- `mupen64plus-rsp-parallel/upstream/parallel.cpp`: plugin lifecycle/setter.
- Existing shared ABI declarations if required.

**Steps**

1. Trace the actual explicit per-game preference to core startup. Record it.
2. Add a default-off policy value independent of callback presence and log budgets.
3. Propagate it before RSP execution through an optional, documented capability.
4. Keep the existing diagnostic callback separate; do not rename a logging flag
   and assume that removes its callback dependency.
5. Define reset, ROM close/reopen, failed initialization, detach and savestate
   behavior. A reset inside the same authorized session must not disable the fix.
6. Ensure a subsequent DD-off session cannot inherit the mode.
7. Handle missing optional support without breaking old plugin ABI or silently
   claiming the selected plugin has been corrected.
8. Test the full mode/logging truth table and transitions in validation §3.

**Output:** policy bridge, lifecycle tests, documented capability behavior.

**Acceptance:** DD-on/logging-off selects corrected policy; DD-off/logging-on does
not. WritableROM alone never enables it.

**Stop:** implementation consults `Diagnostics::enabled()`, an environment variable
whose meaning is only logging, or debug callback existence to select semantics.
An explicit launch-policy channel must be established instead.

**Split further for a small model:**

- P03a: core policy state and frontend launch/reset wiring, with state tests.
- P03b: optional plugin capability/setter and receiver lifecycle.
- P03c: end-to-end truth-table tests and review of failure/old-plugin behavior.

Do not deliver the partial bridge as a functioning correction; P04 waits for all
three pieces.

## P04 — Correct only internal RSP DMA reads

**Type:** narrowly scoped production correction.

**Depends on:** P02 and P03.

**Goal:** Apply P01 through the real loop while preserving the unselected behavior.

**Primary scope:** `mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp`; a small private
policy helper/header only if it improves testability without creating a second engine.

**Steps**

1. Capture raw operands before alignment/mutation.
2. Select legacy or corrected policy from the explicit runtime state.
3. Under corrected policy, latch the initial bank and keep the full approved row
   length. Remove the erroneous bank-boundary clamp for this path.
4. Wrap destinations inside the selected bank, respecting approved alignment.
5. Apply the approved row source advance/skip and final-register rules.
6. Mark dirty blocks for actual IMEM writes only; maintain existing required
   invalidation behavior for wrapped IMEM writes.
7. Keep the existing scheduling/return mechanism unless P01 explicitly scoped
   a necessary, independently justified change.
8. Run the new production-path tests, legacy differential cases and existing suites.
9. Review that DD-off behavior and unrelated DMA write/core paths remain unchanged.

**Output:** minimal patch and passing fixture results, including exact observed case.

**Acceptance:** corrected DMEM-start request leaves every IMEM byte and relevant
dirty flag unchanged; legacy mode still reproduces the old semantics. Corrected
IMEM-start transfers still copy and invalidate properly.

**Stop:** a special-case skip for zero source/`0xffffffff`, mask-only fix retaining
80-byte rows, changes to both read/write/core implementations, or unexplained
changes to task flags, interrupts, timing or CPU dispatch.

## P05 — Preserve useful evidence after the overwrite is fixed

**Type:** bounded diagnostics; no new emulation policy.

**Depends on:** P04.

**Goal:** The first corrected APK must answer whether the suspect audio command
still occurs and whether the corrected transfer preserves IMEM.

**Primary scope**

- `mupen64plus-rsp-parallel/upstream/rsp_diag.cpp/.hpp`.
- `parallel.cpp` entry-time snapshot.
- Narrow `rsp/cp0.cpp` launch/completion observation.
- A compiler-supplied site hook only if needed and safely testable.

**Steps**

1. Keep entry-time task context and assign stable session/entry generations.
2. Add a trigger independent of actual IMEM writes: the observed raw request
   shape, a decoded zero-length audio condition, or precisely defined legacy
   crossing geometry. It is a logging trigger only.
3. Preserve bounded pre-DMA operands/context before DMEM is overwritten.
4. Record policy, raw/effective geometry, actual destination banks, before/after
   IMEM evidence, dirty state and final registers.
   Distinguish `trigger_reason`, `capture_eligible`, `actual_imem_write_count`
   and `probe_skipped`/reason. A captured event does not itself mean IMEM was
   touched or that every requested probe succeeded.
5. Record full consumed command words and relevant registers if their origin
   is available. Otherwise say unknown; do not reconstruct them from already
   overwritten DMEM.
6. Add a bounded recent-command ring if a narrow verified fetch hook is available.
7. Reserve the trigger budget separately from ordinary DMA and startup logs.
8. Explicitly report overflow, truncation, skipped probes and missing exact sites.
9. Test diagnostics-off behavior, duplicate triggers, limits and the corrected
   event with **zero** actual IMEM writes.

**Output:** first-candidate evidence contract and tests.

**Acceptance:** preventing the overwrite does not make the important event
invisible. Logging can be disabled without changing the selected DMA policy.

**Stop:** unbounded per-command/per-store logging, a false exact PC, observing only
post-copy memory, or changing guest control flow to trigger/flush the diagnostic.

## P06 — Build and perform the first native decision test

**Type:** packaging plus device verification.

**Depends on:** P04/P05 and independent code review.

**Steps**

1. Run the coherent host verification batch, not a new browser test.
2. Build all supported Android ABIs with the established toolchain and signing.
3. Record source revision/dirty state, APK hash, certificate and packaged Build IDs.
4. Confirm old APK/symbol files were not accidentally reused. Preserve the new
   symbols separately and never overwrite DDSTART11 evidence.
5. Update the handoff and publish focused source/tests/docs without logs or keys.
6. Install as an update, preserving application data.
7. Use the established per-capture-status helper, with the new APK path/hash.
8. Confirm the actual engine is dynarec and the explicit policy/capability is active.
9. Reproduce the same route and inspect the decision criteria in validation §5.

**Output:** a signed candidate and a native result classified by the decision table.

**Acceptance:** captured corrected policy, transfer evidence and native behavior
all correspond to the same APK. “Menu appeared” alone is not final acceptance.

**Stop:** unavailable device, signing mismatch, missed trigger, exhausted coverage,
wrong engine, or ambiguous source/APK identity. Report the gate as blocked.

**Next:** If behavior and audio recover, proceed to P10 while documenting command
validity as far as supported. If unresolved, choose P07; do not invent an audio fix.

## P07 — Identify the consumed audio command and its validity

**Type:** targeted analysis; a small observation extension only if evidence is missing.

**Depends on:** P06 showing unresolved audio/command behavior.

**Goal:** Establish opcode, full words, actual command offset and meaning before
watching all CPU stores.

**Steps**

1. Use the trigger's own audio task snapshot, not the last distinct identity log.
2. Confirm microcode identity/revision and interpret the applicable command ABI.
3. Capture full `k0`, `t9`, relevant flags and the operand transformation at
   consumption. Preserve distinctions between full registers and extracted fields.
4. Identify the actual fetch cursor or verified DMA-to-DMEM mapping.
5. Derive a command index/range only if pointer bounds, wrapping and intervening
   writes are accounted for. Matching two zeros in memory is not proof.
6. Compare task-entry command bytes, transferred bytes and consumed bytes.
7. Check whether this opcode/path permits zero length/source and how its genuine
   microcode/reference consumer handles that case.
8. Determine whether the first divergence is already visible.

**Output:** command evidence table with exact/candidate/unknown labels and one
selected hypothesis for the next package.

**Acceptance:** either the input is explained, or a specific boundary is identified
where more evidence is needed. Historical yielded-gfx behavior is not substituted
for this non-yielded audio task.

**Stop:** unknown opcode/ABI, ambiguous command origin or stale snapshots.
Add only the missing bounded observation; do not change the emulator yet.

## P08 — Find the exact producer, overwriter or premature reuse

**Type:** conditional diagnostic implementation and native evidence collection.

**Depends on:** P07 identifying a relevant byte range/generation.

**Goal:** Find the first wrong operation rather than patching the visible zeros.

**Scope selection**

- Locate existing core dynarec observation support under
  `mupen64plus-core/upstream/src/device/r4300`.
- ARM64 integration lives in the `new_dynarec` area; verify current symbols and
  fast/slow paths rather than assuming one generic memory callback covers stores.
- Use existing `tools/tests/dd-startup-dynarec-observer-test.c` as support, not
  proof that generated-code instrumentation is complete.

**Steps**

1. Derive the watched physical range from the current task and command mapping;
   normalize aliases and include only the necessary neighboring fields.
2. Watch early enough to catch production/publication. If arming at consumption
   is too late, capture the preceding generation or identified producer boundary.
3. Track task/buffer generation so a later reuse is not labeled as an earlier writer.
4. Cover observed writer classes: CPU stores, RSP writes, core DMA, and any
   demonstrated loader/copy path. Start narrow and extend only when a gap is shown.
5. For CPU stores, record exact emitting PC/instruction when available, effective
   address, width/mask, before/after bytes, success and delay-slot status.
6. Account for fast paths, unaligned/partial stores, conditional-store failure and
   faulting accesses. Preserve all required host/guest register state.
7. Use a fixed recent-event ring and trigger flush; verify disabled behavior.
8. Collect one targeted native run and identify the first wrong write or lifetime
   transition. Do not infer it from a later unchanged snapshot.

**Output:** a specific producer/overwriter/reuse event, or an explicit coverage gap.

**Acceptance:** evidence identifies what wrote or misread the relevant bytes and
when relative to their task generation.

**Stop:** inability to preserve JIT ABI/register state, absent fast-path coverage,
or only a block/sampled PC. Escalate review; do not call that an exact CPU writer.

**Split further; this is the largest and highest-risk package:**

- P08a: dynamic watched range, generation model and bounded ring, tested with
  synthetic events and no generated-code changes.
- P08b: one proven-needed writer family in the active architecture, including its
  exact-site and register-preservation tests. Handle another family in a separate
  reviewed patch if coverage requires it.
- P08c: integrate only the additional DMA/reuse boundaries shown to be relevant.
- P08d: native capture and evidence analysis; no simultaneous corrective patch.

Do not ask a small model to modify every architecture/store family at once.
Uncovered paths must remain listed as gaps until resolved or shown irrelevant.

## P09 — Apply only the demonstrated audio correction

**Type:** conditional, evidence-specific correction.

**Depends on:** P07 or P08 establishing the responsible operation.

**Choose exactly the supported branch**

- **Legal zero command:** document it; no producer patch solely to remove zeros.
- **Wrong consumer decode:** correct the proven field/sign/shift/endian/dispatch issue.
- **RSP JIT defect:** correct the proven register, delay-slot or reuse defect and
  test its instruction semantics independently.
- **Wrong CPU store/translation:** fix the exact translation/helper operation;
  include partial-store and delay-slot regression fixtures.
- **Premature buffer reuse/publication:** correct ownership/order at the demonstrated
  boundary without arbitrary sleeps or global serialization.
- **Another DMA writer:** open a separately justified narrow correction using
  that writer's captured operands, not a broad DMA rewrite.

**Steps**

1. State the failing invariant and smallest expected correction before editing.
2. Add a failing reproducer for that operation using synthetic inputs.
3. Implement only the supported branch behind explicit DD activation where required.
4. Test neighboring valid operations and independent DD-off behavior.
5. Review cache invalidation/ABI/lifecycle implications where relevant.
6. Rebuild and verify the same native route and audio behavior.

**Output:** a focused patch with causal evidence and tests, or a documented
“no independent audio defect demonstrated” finding.

**Stop:** the proposed change is simply skip-null, skip-zero, fake completion,
force-yield, disable dynarec or special-case the game title.

## P10 — Run combined native regressions and persistence tests

**Type:** final functional acceptance, using the validation matrix.

**Depends on:** P06 and P09 if a second correction was needed.

**Steps**

1. Verify DD boot/menu transitions and representative gameplay under dynarec.
2. Verify music and effects through transitions, not merely an audio callback count.
3. Exit cleanly and repeat a cold launch; check that the corrected policy persists.
4. Exercise DD-on/DD-off and logging-on/off lifecycle transitions.
5. Run a plain cart and its normal save/reload.
6. Test independently enabled WritableROM, including the named regression games
   where legally available and applicable.
7. Verify clean-exit/cold-process persistence and unchanged original assets except
   explicitly authorized writable outputs.
8. If cached interpreter is used, record the real effective engine and complete
   dispatcher-path coverage requirements; never substitute it for dynarec acceptance.

**Output:** completed matrix with pass/fail/blocked, artifact identity and evidence.

**Stop:** any save regression, DD-off policy leakage, silent fallback to another
engine, or missing assets/device. Required blocked rows prevent completion.

## P11 — Bound overhead and finalize diagnostics

**Type:** release hardening, not a new behavior experiment.

**Depends on:** P10.

**Steps**

1. Verify fixed storage bounds and no per-frame memory growth.
2. Confirm diagnostic disablement leaves correction active for DD and does not
   leave expensive hashing/ring updates active for DD-off sessions.
3. Compare corrected builds with diagnostics enabled/disabled under the same scene,
   device settings and thermal conditions.
4. Retain useful failure evidence without general high-volume tracing.
5. Verify resets, log exhaustion and disconnect do not change policy.
6. Remove temporary probes only when no longer needed and keep focused tests.
7. Rebuild/revalidate any native behavior invalidated by these changes.

**Output:** measured overhead/limits and final diagnostic defaults.

**Stop:** “performance improvement” claimed without comparable measurements or
changed timing/task semantics introduced as optimization.

## P12 — Publish the verified final state

**Type:** source/artifact/documentation release.

**Depends on:** P10/P11.

**Steps**

1. Ensure the final tested APK is built from the final source.
2. Record revision, dirty diff, APK hash, signing fingerprint and native Build IDs.
3. Preserve symbols and native test records locally.
4. Update `docs/HANDOFF_NEW.md` with purpose, causal findings, changes, tests,
   limitations and exact next instructions if anything remains blocked.
5. Publish only focused source/tests/docs atop the live branch, non-force.
6. Do not commit ROMs, disks, IPLs, raw captures, APKs, symbols, signing material,
   local SDK caches or workspace metadata.
7. Deliver the APK with its published commit and update-safe capture/install instructions.
8. Mark the overall repair complete only when all required acceptance rows pass.

**Output:** final release record or an explicitly incomplete handoff.

## Standard result template for every package

```markdown
# PXX result

## Baseline
- Source revision and relevant dirty diff:
- APK/capture identity, if used:
- References and revisions:

## Scope
- Intended invariant:
- Files actually changed:
- Deliberately unchanged:

## Evidence
- Direct observations:
- Reference-derived expectations:
- Hypotheses/unknowns:

## Verification
| Check | Command/procedure | Result | Evidence |
|---|---|---|---|

## Safety
- DD-off behavior:
- Logging-independent DD policy:
- Saves/signing/assets:
- Bounds/ABI/lifecycle:

## Handoff
- Acceptance: PASS / FAIL / BLOCKED
- Remaining questions:
- Exact next package and its required inputs:
- Published commit/artifact identity, if applicable:
```