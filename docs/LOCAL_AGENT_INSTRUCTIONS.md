# Local agent instructions — N64DD investigation

Use this file instead of the repository-root `replit.md` when working outside
Replit. It carries the project requirements without requiring Replit, its build
helpers or its post-merge hooks. This is a native multi-module Android project,
not a browser application.

## Read first

- [Current status and orchestrator handoff](N64DD_CURRENT_STATUS.md): current
  evidence, parallel-run boundaries, next analysis, proposed-fix gates and
  prohibited claims. Read this before the chronological records below.
- [Repair plan](N64DD_DMA_AUDIO_REPAIR_PLAN.md): evidence, constraints and policy gates.
- [Work packages](N64DD_DMA_AUDIO_WORK_PACKAGES.md): assignments, diagnostic prompt
  and mandatory independent review before every commit.
- [Validation runbook](N64DD_DMA_AUDIO_VALIDATION.md): host and native acceptance.
- [DDSTART11 analysis](DDSTART11_NATIVE_ANALYSIS.md): demonstrated overwrite and
  the limits of the audio-command evidence.
- [Investigation handoff](HANDOFF_NEW.md): historical progress and subsequent
  observations. Read relevant sections; do not treat an earlier status as current.

Start with P00 and verify the actual checkout, dirty changes, source/APK identities,
available evidence, tools and device access. The investigation restarted from
release tag `v336`, but subsequent changes exist: do not reset to that tag or assume
the current checkout is still the original baseline.

## Non-negotiable constraints

- Require explicit per-game DD activation for every new runtime behavior and
  diagnostic. Preserve DD-disabled games.
- Keep WritableROM independently controlled by its per-ROM setting, including
  DD-disabled cart hacks. Preserve Mario Kart Amped Up, Mario Tennis and EK Cart
  Hack save behavior.
- Preserve DDSTART9's block-boundary correction. Do not restore earlier watchdog,
  speculative timing, fake completion or forced-yield experiments.
- Working native DD under dynarec is required. Cached interpreter is a secondary
  correctness target and diagnostic comparison, not a final workaround.
- Separate hardware correction policy from diagnostic enablement and callback
  availability. Corrected behavior must also work with logging disabled.
- Keep the initial correction scoped to internal Parallel-RSP DMA reads. Do not
  automatically change CPU-originated DMA or RSP DMA writes.
- Resolve the complete DMA policy and establish production-path fixtures before
  implementation. A bank mask alone is insufficient; the row-length clamp also
  matters. Pin references and explicitly resolve disagreements.
- A demonstrated overwrite does not establish the origin or validity of its
  audio inputs. Do not invent a separate producer defect or revive the historical
  yielded-graphics explanation without new evidence.

Evidence-led re-engineering for accuracy or performance is permitted where the
assigned scope and evidence justify it, but does not waive these boundaries.
Measure performance before claiming improvement. The historical IPL-first policy
is not a proven fix; consult the boot-order reference audit before changing it.

## Diagnostic and review discipline

Read and apply [Evidence discipline prompt](N64DD_EVIDENCE_REVIEW_PROMPT.md)
for capture work, analysis, verification and handoffs. Its claim ledger and
inference boundaries are required; successful tests do not waive them.

Use the work-package guide's copy-pastable diagnostic and reviewer prompts.
Separate direct observations, derived calculations, pinned reference comparisons,
hypotheses and unknowns. Sampled PCs are not exact writers; static paths do not
prove execution; missing diagnostic records do not prove an event did not occur.
Keep bounded post-fix evidence available even if the correction removes the old
IMEM-write trigger.

Execute one eligible package or subdivision at a time. Preserve competing
explanations and choose the smallest discriminating check. Stop at missing inputs
or failed prerequisite gates instead of making speculative fixes.

Before **every commit**, obtain PASS from an independent read-only reviewer for
the exact final patch, including new files. Resolve blocking findings and re-review
changed content. If subagents are unavailable, use a separate fresh model session
or human reviewer. If neither is available, committing is blocked. This is a
workflow requirement, not an installed Git hook. Package approval does not imply
native repair or release acceptance.

## Local builds, installation and saves

Use the repository's current Gradle configuration and the local Android toolchain.
Verify requirements rather than assuming Replit's SDK paths, caches, shell helpers
or automatic hooks are available. Existing GNU-linker host tests may require a
reviewed portability adjustment on macOS; do not claim they ran unchanged.

Use the original local signing setup for updates. An APK built with a different
debug key cannot safely replace the installed app as an update. Never uninstall
or clear app data to bypass a signature mismatch. Preserve saves and establish
backup/restore arrangements before device testing. Do not disclose or publish
keystores, signing credentials or private configuration.

Record source commit, relevant dirty diff, APK hash, package/signing identity,
effective engine and explicit DD mode for device runs. Old build outputs do not
prove a fresh build or a current fix. On the documented Mac setup, ADB is at
`~/Downloads/platform-tools/adb`; verify availability rather than assuming PATH.

Use legally supplied cart, disk and IPL assets. Follow the validation runbook for
native DD menus/gameplay/audio, plain-cart regressions and WritableROM persistence.
Distinguish host tests, reference comparisons and actual device observations.
If no device is reachable, report native verification as unavailable, not passed.

## Handoff and publication

The documented publication target is `dd-eos-watchdog-checkpoint` unless the user
changes it. Verify the actual checked-out branch and live publication target;
do not assume a local clone already has the target branch checked out.
Historical experiments are preserved on
`dd-eos-watchdog-archive-pre-v336-20260911`; do not merge them back as a shortcut.
Historical diagnoses are not current evidence.

For each test iteration, update `docs/HANDOFF_NEW.md` before review and commit:
purpose, changes, observations, actual verification, limitations and next test
instructions. Provide the published commit ID with each test APK.

Publish only reviewed, focused source/test/documentation changes. Preserve upstream
history, unrelated work and concurrent changes through non-force updates. Never
publish raw captures/logs, cart/disk/IPL assets, APKs, symbols, signing material,
SDK caches or workspace metadata to the source branch.

## Replacement line for a coordinator prompt

For the full startup prompt, use [the coordinator prompt](COORDINATOR_PROMPT.md).

```text
Read docs/LOCAL_AGENT_INSTRUCTIONS.md in place of replit.md, then follow the repair
plan, work packages and validation runbook. Use the local environment; no Replit
installation or Replit-specific build automation is required.
```