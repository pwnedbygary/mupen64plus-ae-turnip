# N64DD investigation from the v336 release baseline

## Status and scope

Started 2026-09-11. No new Android runtime test has been performed by this
workspace in this investigation. The N64DD failure is unresolved.

- Baseline: tag `v336`, commit
  `dc955483a97daa99cb1f9db06e2334464fa1664d`, also the current remote `master`.
  There is no remote branch named `Main`.
- Active investigation branch: `dd-eos-watchdog-checkpoint`.
- Previous experimental tip: `45d3e5a735951a4d3e82d027427caa555fd36122`.
  Preserved on `dd-eos-watchdog-archive-pre-v336-20260911`.
- This restart keeps the v336 application source unchanged. This handoff is
  the only intended difference from that baseline in the remote branch.
- The tag is called v336, but committed `build_common/version_common.gradle`
  still declares versionCode 335 / versionName 3.0.335. Record the installed
  APK hash and local build overrides; do not identify a build by label alone.

The user reports that the experimental DD branch regressed Mario Kart
Amped Up (missing menu cursor, black screen after aspect-ratio selection)
and Mario Tennis performance. The user reports that the v336 release does
not exhibit those regressions, retains the F-Zero X EK Cart Hack save fix,
and still fails to boot native N64DD correctly. These are user observations,
not reproductions performed here. The exact native failure stage on this
baseline still needs a fresh capture.

## One-time local resync after the branch reset

For local tooling and local LLMs: run from the repository root, not the
Android platform-tools directory. Pause other tools that might push while
resyncing. Run `git status --short` first. If it lists changes, stop and
preserve them with a local commit or stash (including relevant untracked
files) before proceeding. Do not commit signing material or game/save files.

The following replaces the local debugging branch pointer with the current
remote branch and checks out that source. It first preserves the old local
branch tip under a timestamped backup name. That backup protects committed
history, not uncommitted or ignored files.

```sh
git fetch origin &&
git branch "backup/dd-before-v336-$(date +%Y%m%d-%H%M%S)" dd-eos-watchdog-checkpoint &&
git switch -C dd-eos-watchdog-checkpoint origin/dd-eos-watchdog-checkpoint &&
git log -1 --oneline
```

If any command fails, stop and report the error. Do not force-push, merge the
old experimental history, or reapply an old stash wholesale onto the baseline.
Review saved changes individually before carrying anything forward.

This procedure assumes the local `dd-eos-watchdog-checkpoint` branch already
exists. If it does not, fetch and use
`git switch --track origin/dd-eos-watchdog-checkpoint` instead.

After this one-time resync, use normal fast-forward pulls for new commits.
Read this handoff from the updated checkout before building. Keep the original
local signing setup and do not use old generated APKs as baseline builds.

## Rules for this investigation

1. Keep observations, source facts, and hypotheses separate. Record negative
   results and corrections, not just apparent progress.
2. Preserve the v336 WritableROM save fix, indexed restore/compaction, clean
   shutdown, per-ROM gating, and save migration. Do not change save formats,
   clear app data, or overwrite the user's original saves for a DD experiment.
3. Do not merge or replay the archived DD branch. It differs from v336 in
   68 paths with over 19,000 added lines, including CPU/JIT, RSP, interrupts,
   DD, plugin interfaces, and renderer changes.
4. No forced guest memory/thread repairs, synthetic completion signals, or
   title-specific timing changes without evidence and an explicit rationale.
   Prefer a hardware-supported correction at the first proven divergence.
5. Make diagnostic and behavioral changes separate, small commits. Each
   behavioral commit must state its evidence, predicted result, and regressions
   checked. Do not stack speculative fixes.
6. Use the existing local signing setup. Never commit signing keys, APKs,
   game/IPL/disk files, saves, or raw memory dumps. Preserve a known-good APK.
7. A build, menu screenshot, or counter increase alone is not boot success,
   clean audio, correct saves, or a root-cause diagnosis.

## Baseline source findings

References below refer to dc955483a, not the archived experimental tree.

### Input loading and IPL selection

- `app/src/main/java/paulscode/android/mupen64plusae/persistent/GamePrefs.java`
  reads DD enable/IPL/disk preferences and clears DD paths when disabled
  (around lines 592-613). DD disk configuration also affects CountPerOp
  (around 642-649). Record effective settings; selecting a profile alone
  does not prove which inputs or CPU engine ran.
- `mupen64plus-core/upstream/src/main/main.c` loads disk storage and format
  information, then attempts the IPL load when a disk was loaded
  (`load_dd_disk`, around 1147-1310; startup around 1633-1643).
- `mupen64plus-core/upstream/src/device/device.c` maps/initializes DD when
  `dd_rom_size > 0` (around 161-171). Its v336 boot-priority correction
  chooses DD IPL as the PIF boot source whenever an IPL is loaded, even
  when a cartridge is also present (around 197-208). Preserve this fix.
- Startup initializes/powers on the device and runs PIF HLE before CPU
  execution. Hard/soft reset functions schedule CPU reset/NMI rather than
  repeating all initialization. Treat fresh boot and reset as separate tests.

These facts do not prove that the installed run successfully loaded a disk,
selected the right IPL, reached IPL code, or completed a disk transfer.

### RSP and rendering

- Baseline `mupen64plus-core/upstream/src/device/rcp/rsp/rsp_core.c`
  has the original task dispatch and SP interrupt handling. The graphics
  branch processes DP completion; audio has a separate task branch.
- The archived branch changes RSP scheduling, task/yield handling, CPU
  hooks, and renderer command-window handling. DD guards in some locations
  do not establish that every added path is isolated from ordinary carts.
- Missing Mario Kart cursor/black-screen and Mario Tennis slowdown do not,
  by themselves, identify a renderer, RSP, CPU, or save-system defect.
  The exact regression-causing commit has not been isolated.

### WritableROM boundary

- `mupen64plus-core/upstream/src/device/cart/cart_rom.c` gates writable
  cartridge writes/persistence on `ROM_SETTINGS.writablecartrom`, bounds
  writes, restores indexed ranges, and retains normal cart-read behavior.
- `mupen64plus-core/upstream/src/main/rom.c` defaults the option off and
  reads it from the per-ROM database, including inherited settings.
- Existing `.cart_ram` and `.idx` files are part of the working feature.
  Preserve both. Verify the effective ROM database, not only packaged INI
  content: the existing installation can retain an older copied database.

## What is retained from the previous investigation

Retain questions and evidence-handling lessons, not assumed fixes:

- The previous branch reported scheduler activity and later RSP task
  completion followed by another stall. This does not establish the failure
  mechanism on v336.
- A stopped RSP, task header, signal bits, and interrupt delivery should be
  correlated if fresh baseline evidence points there. Do not assume a final
  BREAK, malformed task header, or missing SIG2 is the starting defect.
- Record which code image is executing after a reboot. Cartridge symbols
  may not describe loaded disk code at the same guest addresses.
- A sampled PC is not an exact writer. Queue snapshots/buffer contents are
  not send/receive event traces. Bounded startup rings cannot prove long-run
  progress; record coverage, wrap/drop counts, and capture timestamps.
- The archived `DDDIAG`, `WD71TR`, `WD73RATE`, and `wd_force.flag` workflow
  are not present in this clean baseline. Do not ask the user to collect
  those markers or transplant the old watchdog to make instructions work.

Reference material, not proof of this emulator's behavior:

- https://github.com/LuigiBlood/64dd/wiki
- https://github.com/LuigiBlood/64dd/wiki/Registers
- https://github.com/LuigiBlood/64dd/wiki/Disk-Access-Process
- https://github.com/LuigiBlood/64dd/wiki/F-Zero-X
- The archived branch's reference review and Phobos comparisons, only after
  checking that cited code and assumptions apply to this baseline.

These external pages were not revalidated during this restart.

## Plan and decision points

### 1. Establish the exact baseline run

Use the original signing setup and baseline source, with any local build
overrides recorded. Back up settings, disk/save files, and WritableROM data.
Do not change the CPU/RSP/renderer and source simultaneously.

Record: source commit and dirty diff; installed APK SHA-256/version;
device/Android/ABI; effective CPU/RSP/renderer and driver; CountPerOp, memory
size and speed settings; legal cart/disk/IPL hashes, region, format and byte
order; save/autoload configuration.

Capture startup logcat before a fresh native cart+disk+IPL launch (no old
save state), then the visible failure and elapsed time. Keep logs unfiltered
locally and review private information before sharing. Use existing disk-load
errors/metadata and core/plugin startup messages first.

**Decision:** identify the last confirmed stage: input loading, IPL execution,
DD command/PI transfer, loaded code, RSP task execution, or display/audio.
If current logs cannot distinguish stages, add only the observation needed.

### 2. Add the smallest observation at the unknown boundary

Start with a diagnostic-only commit. Prefer bounded in-memory event records
and counters over per-instruction logging or synchronous file I/O. Keep the
plain-cart path inactive and measure instrumentation overhead.

Depending on stage, capture selected IPL/entry, CPU progress, PI/DD
command/status/DMA/interrupt ordering, or RSP task/ucode identity and
SP status/BREAK/MI interrupt/guest-handler progression. Correlate events
with sequence numbers and task identity; distinguish exact events from
samples. For a display-only failure, include DPC command-window progression.

**Decision:** state one falsifiable hypothesis with its expected event order.
Reject or revise it when the trace disagrees. A missing event in incomplete
coverage is not evidence that the hardware action never occurred.

### 3. Test one minimal correction

Change only the proven boundary. Do not globally change RSP slice size,
IRQ semantics, renderer submission, or guest scheduling as a boot workaround.
Compare against the baseline with identical settings and inputs.

**Decision:** reject a change that breaks a plain-cart control or the working
save path, even if native DD advances further. If a workaround is unavoidable,
label it as such, describe limits, and seek agreement before carrying it.

### 4. Acceptance matrix

| Path | Required check | Current evidence |
| --- | --- | --- |
| Native F-Zero X + EK disk + IPL | Fresh boot to interactive menu, clean audio, repeatable behavior; distinguish reset/autoload | User reports failure; new capture pending |
| Mario Kart Amped Up, no DD | Menu cursor visible/moves; aspect-ratio selection reaches gameplay; no black screen | User reports v336 good; independent capture pending |
| Mario Tennis, no DD | Same match/settings; frame rate/frame-time consistency, input and audio versus v336, comparable duration/thermal conditions | User reports v336 good; performance numbers pending |
| F-Zero X EK Cart Hack | Disposable save edit, normal exit, full relaunch, retained custom data; preserve original .cart_ram/.idx | User reports v336 save fix working; controlled repeat pending |
| Ordinary cart/save boundary | DD disabled; no new WritableROM persistence for unflagged ROMs | Source gating checked; runtime comparison pending |

Run the relevant plain-cart controls with each behavioral candidate. Reserve
longer endurance tests until a candidate passes the short controlled checks.

## Findings log

### 2026-09-11 — baseline restart

- Verified that v336 and master resolve to the same baseline commit.
- Archived the previous debugging branch before replacing its history.
- Compared the old branch with the baseline; no experimental fixes were
  selected for carryover.
- Inspected baseline loading, IPL priority, RSP/task and WritableROM boundaries.
- No fresh baseline trace, native boot/menu/audio test, regression
  reproduction, save mutation, or APK build was performed in this workspace.
- Next required evidence: startup log and visible failure from the unchanged,
  locally signed baseline with recorded inputs and effective settings.

Append each new run with build/hash, inputs/settings, method, raw evidence
location, observations, interpretation, limitations, and the next decision.
Correct obsolete conclusions explicitly; do not leave competing claims
presented as current fact.