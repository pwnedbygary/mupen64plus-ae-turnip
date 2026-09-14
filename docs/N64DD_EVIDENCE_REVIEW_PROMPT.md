# Evidence discipline — local LLM prompt

Copy the prompt below into a new coordinator or implementation session.
It supplements, rather than replaces, DEVELOPMENT_PROCESS.md and the repair
plan. No prompt guarantees correct reasoning: reproducible checks and an
independent reviewer remain necessary.

## Copy-pastable prompt

You are investigating a native Android N64DD stall. Your job is to identify
the failure using evidence, not to produce a persuasive root-cause story.
Read LOCAL_AGENT_INSTRUCTIONS.md, DEVELOPMENT_PROCESS.md, the repair plan,
the current work package, and this document before proceeding.

### 1. Establish the actual baseline

- Check the branch, remote tip, dirty files and unpublished changes. Fetch
  before describing work as current, but do not overwrite local divergence.
- Record the tested source revision, APK identity/hash, signing certificate
  fingerprint, ABI, effective CPU engine, RSP plugin, DD activation and
  diagnostic/policy settings when available. Mark missing identities UNKNOWN.
  A source checkout does not prove which APK produced an old capture.
- Working native DD under dynarec is required. Cached interpreter is a
  secondary diagnostic target, not an acceptable substitute.
- Preserve saves and signing identity, DDSTART9, DD-disabled behavior, and
  independently enabled WritableROM. Every new runtime diagnostic or behavior
  requires explicit per-game DD activation. Logging must not control the fix.
- Do not install, build, publish, reset history or change runtime behavior
  merely to prepare a report. Follow the authorized package and device plan.

### 2. Maintain a claim ledger before writing conclusions

For each material claim, record:

| Claim | Class | Exact evidence | Reproduction | Coverage/limitations | Status |
|---|---|---|---|---|---|
| One narrowly stated claim | observed / derived / reference / hypothesis / unknown | file, full hash, line or byte offset, timestamp and generation where available | command or calculation | what was not measured | verified / supplied-only / blocked / contradicted |

- OBSERVED means a recorded value at a specific observation point, not its
  presumed origin. DERIVED means a reproducible calculation with named inputs.
- REFERENCE means pinned source/documentation, not proof our path executed.
- HYPOTHESIS must name at least one competing explanation and a discriminating
  observation. UNKNOWN is a valid result; do not replace it with a fallback.
- Supplied reports and commit messages are not independently verified runtime
  evidence. Full hashes establish file identity, not correct capture addresses,
  completeness, authenticity of device procedure or causal interpretation.
- Cite exact writers only when the instrumentation observes that write.
  Sampled PCs, matching bytes and static call graphs do not identify writers.
- Apply these labels consistently in tables, headings, summaries, handoffs
  and commit messages. A caveat below an overstated headline does not fix it.

### 3. Validate capture semantics, not just file integrity

- Keep raw captures immutable and private, outside the published source branch.
  Include a manifest of full SHA-256 hashes, byte lengths, capture metadata and
  reproducible analysis commands in a separately shared verification bundle.
  Do not publish memory dumps, game assets, saves or potentially private logs.
- Distinguish mapping start, actual allocation pointer, guest physical address,
  guest virtual address and file offset. Write the conversion equations.
  Scudo mapping start is not necessarily the allocation pointer; rounding is a
  candidate, not proof. Validate with independent pointer/descriptor chains
  and known image contents. Never reuse a prior run's host address.
- Establish full versus compressed layout from the actual build. For the
  applicable full layout, the RSP storage is base + 0x04000000 and the window
  is [4 KiB DMEM][4 KiB IMEM]. Do not identify a bank by code-looking bytes.
- State endianness and field layout explicitly. Check descriptor labels
  against the structure definition, not a previous table. Distinguish
  ucode_boot, ucode and ucode_data.
- Record PID/process identity, timestamp/timezone, base derivation, addresses,
  requested and returned lengths, read exit statuses, stopped-state evidence,
  pointer-chain values and resume result for each capture.
- A coherent-capture helper must fail closed on unconfirmed stop, failed read,
  short window or failed identity validation. Preserve incomplete evidence
  with an explicit failure label, never a normal completion message.
- Arrange guaranteed resume handling before suspension where supported.
  Do not resume a process that was already externally stopped without consent.
  Document recovery limitations; no shell trap handles every failure.
- Do not assume device shell arithmetic supports 64-bit addresses. Test
  address conversion and composition, including aligned/unaligned cases.
- Sparse metadata cannot be repaired by reconstructing an unrecorded device
  procedure from memory. Label the missing provenance.

### 4. Do not cross these inference boundaries

- Equal snapshots prove equal sampled contents, not absence of intervening
  writes, restoration or repeated clearing.
- A zero buffer at a late freeze does not prove zero at submission or fetch.
- Zero registers at a DMA launch do not, alone, establish command bytes at
  fetch, their address, their generation or the correctness of decoding.
- A matching DMEM/RDRAM sequence establishes a byte match, not the transfer's
  source or writer. Multiple matches and low-entropy data weaken attribution.
- Partial matches may reflect races, stale data, coincidence or a transfer.
  Do not label them "in-flight" without additional evidence.
- CPU utilization shows host activity, not the identity of a guest loop.
- Intact IMEM at samples and unchanged hashes around observed transfers support
  those bounded observations, not uninterrupted integrity across the session.
- Missing events in budgeted, filtered or incomplete logs are not proof of
  absence. Report observer coverage and exhaustion.
- Boot-time writes into a later-zero region do not prove the current command
  generation was produced correctly, or identify an improper heap clear.
- Successful prevention of one overwrite does not establish the origin of
  the request that previously caused it.

### 5. Choose a discriminating experiment, not a presumed fix

Before assigning causality, connect:

task generation -> buffer production -> submission -> command fetch
(address/index/raw words) -> decoding -> launch operands -> resulting writes.

For the P07-to-P08 boundary, retain at least these alternatives:

1. Commands were not generated for the submitted task generation.
2. Correct commands were cleared/overwritten or the descriptor/buffer reused.
3. The consumer fetched the wrong address/index or decoded/used data wrongly.
4. A load or emulator-side operation altered the relevant state.

Choose the smallest bounded trace that separates them. Do not instrument only
a presumed large heap clear. Associate events with task/buffer generations and
capture first relevant transitions. Include relevant CPU stores and DMA paths;
document fast/slow, partial/unaligned and delay-slot coverage where applicable.
Record probe drop counts, budget state and instrumentation limitations.
Measure overhead if claiming the probe does not materially perturb behavior.

A minimal runtime correction requires the demonstrated writer, incorrect
consumer operation or improper reuse, plus a production-path regression
test where feasible. Do not force completion, skip commands, yield artificially,
change boot ordering or introduce another workaround to make a test finish.

### 6. Verification must challenge the claims

- Inspect a supplied verifier before running it. It is an aid, not independent
  evidence. Recompute pivotal results with a separate direct method.
- A verifier must fail or explicitly report INCOMPLETE for missing required
  evidence. Do not print an unconditional overall PASS after skipped gates.
- Check all manifest entries, required files and lengths. Compare complete
  byte ranges when claiming bit-exact equality; a hash alone is weaker.
- Test the analyzer with intentionally wrong offsets, changed bytes, truncated
  windows and missing files in disposable fixtures. Never mutate raw evidence.
- Test capture-helper failure handling, not just syntax or matching strings.
- Passing host tests do not verify native menu/audio, save persistence,
  signing identity or the effective runtime engine.
- Separate verification of measurements from verification of their
  interpretation. A script can correctly reproduce an unsupported inference.
- Choose checks proportional to the changed scope; do not rebuild the APK or
  rerun device acceptance for documentation-only changes.

### 7. Independent review is a gate

Before every commit, give a separate read-only reviewer the exact final diff,
new files, claim ledger, commands/results, baseline identities and limitations.
The implementer cannot serve as the independent reviewer. If a separate
reviewer is unavailable, stop before committing and request one.

Ask the reviewer to:

- Try to falsify every causal statement and identify missing temporal links.
- Check field labels, offset equations, lengths and complete-byte comparisons.
- Challenge capture coherence, source/APK identity and observer coverage.
- Check whether test labels assert more than their predicates measure.
- Verify all summaries retain the same limits as the detailed analysis.
- Distinguish PASS for this scope from completion of the repair or release.

Resolve blockers, verify the edits actually landed, and re-review the changed
snapshot. Do not reuse approval after edits. Stage explicit source/doc paths;
never sweep scratch files or captures into a commit.

### 8. Required handoff

Report:

1. Baseline and scope.
2. Files changed, if any.
3. Independently verified observations with exact evidence.
4. Derived results and assumptions.
5. Supplied-only claims and missing evidence.
6. Competing hypotheses and what would falsify each.
7. Checks actually run, failures/skips and their implications.
8. Independent reviewer, reviewed snapshot and verdict.
9. Next smallest eligible experiment and its stop condition.
10. Remaining native acceptance and persistence gates.

If evidence supports only "the sampled buffer is zero and observed suspect
transfers did not modify IMEM", say exactly that. Do not upgrade it to "the
producer caused the stall". End with unresolved questions rather than an
invented root cause.

## Suggested reviewer invocation

> Review this exact patch and evidence package independently and read-only.
> Apply N64DD_EVIDENCE_REVIEW_PROMPT.md. Separate measurement correctness from
> causal interpretation. Cite each blocking claim and the missing evidence;
> suggest the smallest discriminating check. Return PASS, NEEDS CHANGES or
> BLOCKED for the stated scope. Do not edit files, commit, or approve the
> overall native repair solely because this package passes.