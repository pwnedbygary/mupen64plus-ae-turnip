# Development process: iterate → test → independent review → commit

This document records the per-change loop used throughout the N64DD DMA/audio
repair (packages P00–P05, 2026-09-13) and made mandatory for **all changes in
this repository** — code, tests, documentation, diagnostics, fixes, amendments
and release commits alike. It is intentionally generic so it can be copied to
other repositories unchanged.

The loop exists because it demonstrably caught real defects that self-review
missed: four arithmetic/wording errors in an analysis document (P01), two
claimed-but-unapplied edits (P02), and a semantic misclassification in new
diagnostics (P05). It is a workflow requirement, not an installed Git hook.

## 1. The loop (once per coherent change)

1. **Scope first.** One coherent change per iteration. State the intended
   invariant, the files in scope, and the stop conditions before editing.
   Verify prerequisites (earlier results, current tree state, actual branch)
   before editing. Do not fix a later concern preemptively.
2. **Implement the minimum justified change.** Production behavior changes
   stay narrowly scoped; tests exercise the real production path (never a
   parallel copy of the algorithm), with expectations from an independent
   source (a written spec/ledger), not from production constants.
3. **Run proportionate checks and record actual results** — commands and
   outcomes, including failures and environment limitations. A pass you did
   not run is a claim, not evidence. Distinguish host-test results from
   native/device results and never substitute one for the other.
4. **Update the handoff/bookkeeping for the change** (for this repo:
   [HANDOFF_NEW.md](HANDOFF_NEW.md) plus the package checkpoint format — see
   [the local agent instructions](LOCAL_AGENT_INSTRUCTIONS.md)) so the record
   exists before review and commit; carry the resulting checkpoint into the
   next session.
5. **Freeze the review snapshot**: base revision, intended paths, exact diff,
   and SHA-256 hashes for new files. Include untracked files in the snapshot
   description; a plain `git diff` omits them.
6. **Obtain independent review** (section 3) of that exact snapshot.
7. **Resolve every finding** explicitly: fix it, or answer it with evidence.
   Any change after review — code, test, or doc — requires a delta re-review
   before commit. Verify claimed edits actually landed (section 5, pitfall 1)
   before requesting re-review.
8. **Commit only after PASS** for the exact final snapshot. Stage the
   reviewed paths explicitly; keep the review record in the commit message
   (reviewer verdict + snapshot hashes + what was re-run). Never `git add -A`
   when scratch/untracked work exists.
9. **Push non-force** immediately after the commit.

## 2. Evidence discipline during the change

- Keep direct observations, derived results, reference comparisons,
  hypotheses and unknowns distinct. Cite artifacts (file:line, test output,
  pinned reference revision) for each claim.
- Treat documents as baselines, not authority over contradictory current
  source or newer observations; record contradictions explicitly.
- Prefer the smallest safe discriminating check; retain competing
  explanations until evidence separates them.
- "Unknown" is an acceptable result; an invented answer is not. Sampled PCs,
  static paths, and missing log lines are not proof of the stronger claim.

## 3. The independent review gate

- The reviewer must not be the implementing agent: a separate read-only
  subagent, a fresh model session, or a human. Self-review never counts.
- Give the reviewer the exact snapshot (base, hashes, diff), the requirement
  documents, the relevant source, and the recorded check results — and the
  instruction to **verify by re-running and recomputing, not by trusting the
  author's summary**. Read-only: no edits, commits, pushes, device actions.
- The reviewer returns: snapshot reviewed, verdict (PASS / NEEDS CHANGES /
  BLOCKED), findings with severity and evidence, what it actually verified,
  and nonblocking suggestions separately.
- NEEDS CHANGES and BLOCKED prohibit committing. Resolve findings, re-verify,
  and request a focused delta re-review (same reviewer if available; a fresh
  reviewer with the prior findings restated if it expired).
- PASS means "this exact snapshot is acceptable for its scope" — not that a
  release, device behavior, or the overall effort is complete.

## 4. Checks: what to actually run

- The repo's existing host suites (this repo: `tools/test-dd-*.sh`) plus any
  suite added by the change. New production behavior requires a failing-
  before/passing-after test through the production path where feasible.
- A compile/syntax verification for every changed translation unit, in the
  configuration the real build uses.
- For this repo specifically: `./gradlew` (the wrapper pins Gradle 8.4, the
  pairing compatible with the project's AGP 8.2.2; the system `gradle` 9.7.1
  is not) for Java, and the NDK toolchain for Android native code — the host
  `gcc` is never part of the APK build. If the host gcc is unusable, use
  `clang` for syntax checks and `CC=clang CXX=clang++` for the host suites,
  and record the substitution. Recorded instance: from 2026-09-13 the host
  gcc driver failed to exec its `cc1`/`cc1plus` backends
  (`posix_spawnp: No such file or directory`, even for a trivial program,
  with the backends present on disk and their libraries resolving); the
  cause is undiagnosed, the substitution worked, and no repo change was
  needed.
- Record which toolchain ran each check; do not silently substitute one
  compiler for another without saying so.

## 5. Known pitfalls (each one was hit and caught in practice)

1. **Silent no-op edits.** A `sed`/script replacement that matches nothing
   reports success. After every scripted edit, verify the change landed
   (grep the new text, re-hash the file) *before* claiming it in a
   re-review request. An unapplied edit reported as applied is itself a
   review blocker.
2. **Reviewer sessions expire.** If a reviewer agent cannot be resumed,
   spawn a fresh one and restate the prior findings verbatim so the delta
   is checked against them.
3. **Stale comments and counts.** When machinery changes (e.g., an XFAIL
   gate removed), grep for the old vocabulary before committing.
4. **Snapshot drift.** Any edit after review invalidates the snapshot.
   Re-hash and re-review the delta; never reuse a PASS for a different tree.
5. **Untracked sweep.** `git add -A` pulls in scratch directories. Stage
   explicit paths only.
6. **Environment limitations are findings too.** Record broken tooling
   (and the substitute used) in the handoff; never silently skip a check.

## 6. Bookkeeping artifacts

- [HANDOFF_NEW.md](HANDOFF_NEW.md): append a section per test iteration (purpose,
  changes, observations, actual verification, limitations, next
  instructions) before review/commit.
- Package checkpoint format (chat + persisted handoff): Package / Baseline /
  Verified observations / Derived results and inputs / Remaining hypotheses /
  Changed files / Checks actually run and why / Independent reviewer and
  verdict / Commit / Remaining blockers / Next eligible package and required
  inputs.
- Cross-session memory: standing instructions and per-package state so the
  next session starts from the published checkpoint, not from scratch (in
  this repo, a project-scoped memory store; when copying this document
  elsewhere, replace this item with that repo's equivalent convention).

## 7. Applying this elsewhere

For this N64DD investigation, also apply
[Evidence discipline prompt](N64DD_EVIDENCE_REVIEW_PROMPT.md). It adds mandatory
claim classification, capture-provenance checks, competing hypotheses and
causal-review checks. Package PASS is not proof of the overall repair.

Copy this file into the other repository (or its equivalent docs location),
adjust the repo-specific content — the suite names and build commands in
section 4 and the handoff/bookkeeping paths in step 4 and section 6 — and
follow sections 1–3 and 5 verbatim. The gate scales with the change: a
one-line fix still gets an independent read-only review of the exact diff
before commit — that is the point.
