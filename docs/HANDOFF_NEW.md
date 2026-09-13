# N64DD investigation from the v336 release baseline

## Local-agent execution and review prompts

The [work-package guide](N64DD_DMA_AUDIO_WORK_PACKAGES.md) now includes a reusable
evidence-based diagnostic prompt and a mandatory independent review gate before
every commit. It specifies claim classification, competing explanations, bounded
discriminating experiments, exact-snapshot review, reviewer prompts and a separate
session/human fallback when subagents are unavailable. These are workflow
instructions, not an installed Git hook. No emulator correction is introduced.

## Status and scope

Started 2026-09-11. No new Android runtime test has been performed by this
workspace in this investigation. The N64DD failure is unresolved.
User-supplied Android logs were subsequently received and reviewed below;
their installed source/APK identity is not yet verified.

- Baseline: tag `v336`, commit
  `dc955483a97daa99cb1f9db06e2334464fa1664d`, also the current remote `master`.
  There is no remote branch named `Main`.
- Active investigation branch: `dd-eos-watchdog-checkpoint`.
- Previous experimental tip: `45d3e5a735951a4d3e82d027427caa555fd36122`.
  Preserved on `dd-eos-watchdog-archive-pre-v336-20260911`.
- The restart initially kept v336 application source unchanged. Subsequent
  DDSTART1 diagnostics and the DDSTART2 comparison below are explicit,
  separately documented changes; the original baseline remains the control.
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
  `dd_rom_size > 0` (around 161-171). Its v336 boot-priority policy
  chooses DD IPL as the PIF boot source whenever an IPL is loaded, even
  when a cartridge is also present (around 197-208). **Correction from the
  source audit below:** this conflicts with the wiki's cartridge-first rule;
  it is not an established hardware-correct fix. Leave source unchanged until
  a controlled DD-only candidate can be compared with a fresh baseline.
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

These external pages were subsequently revalidated in the audit below.

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

## 2026-09-11 — complete wiki inventory and baseline source audit

### Scope, provenance, and evidence limits

User requirement: **every new runtime change, including diagnostics, must
require explicitly enabled N64DD support. Ordinary cartridge loading must
retain baseline behavior.** Documentation changes do not execute in games.
No runtime code, ROM database, settings default, save file, or build artifact
was changed in this audit. No APK was built or installed.

Reviewed the complete available Git wiki tree at
https://github.com/LuigiBlood/64dd/wiki, revision
`76e8e074806589c9ed5d9b08ea1ead078f608f2d`: 30 Markdown files,
including Home and sidebar, approximately 1,787 newline-counted lines.
Reproduce with a read-only clone of
`https://github.com/LuigiBlood/64dd.wiki.git` and that revision. Source
citations below refer to the unchanged v336 application source; `core/`
means `mupen64plus-core/upstream/src/`, and `java/` means
`app/src/main/java/paulscode/android/mupen64plusae/`.

Two sidebar links, H8 Memory Map and Japan Pro Golf Tour 64 Service, have no
page in this revision. Direct web fetches displayed GitHub's Create new page
screen, not technical content. They cannot be analyzed as existing specs.
Linked external implementations/manuals are additional references, not a
claim that every linked repository, binary, historical website, or hardware
schematic was audited. No game/IPL/disk downloads were made.

Method: separate Android launch, DD controller/interrupt, disk/boot, and
cartridge/save reviews; compare each wiki page; cross-check important claims
against actual code before accepting them. An initial launch explorer failed
to return; its work was covered by the game/Android review instead.
`adb devices -l` returned no attached devices in this workspace. Therefore:
no fresh runtime traces, exact context writer, successful native boot, or
regression acceptance result exists from this pass. Hardware equivalence
cannot be certified by source review alone.

### Mandatory isolation rules for subsequent implementation

1. Carry the effective per-game `support64dd` decision to any new native
   diagnostic/behavior path explicitly. A title match, `WritableROM=True`,
   non-null DD struct pointer, or compiled DD support is NOT activation.
2. A DD-disabled launch must not allocate trace rings, write diagnostic
   files, perform periodic checks, alter CPU/RSP/PI timing, select another
   renderer, mutate DD preferences, or change cartridge/save semantics.
   Any unavoidable shared-path guard needs a documented cost and review.
3. Activation and readiness are separate: enabled support permits gated
   input-failure diagnostics; successfully loaded disk/IPL permits device
   operation. Do not suppress useful loading errors merely because loading
   failed. Clear new per-session state on shutdown/reinitialization.
4. Preserve existing WritableROM behavior under its existing per-ROM flag,
   **not** behind DD activation. The cart conversion needs that existing
   save path while DD is disabled. Do not modify it as part of a native fix.
5. No global scheduler, CPU, RSP, renderer, interrupt or ROM-database
   workaround. A shared component modification must retain the exact
   DD-disabled baseline branch and include a reviewable gating argument.
6. Test native cart+disk, disk-only if supported by the candidate, ordinary
   carts, and DD-disabled EK conversion separately. Source gating is
   necessary, not sufficient evidence of zero regressions.

### Finding A — boot-priority contradiction (highest investigation priority)

**Documented hardware rule:** [Emulation Info](https://github.com/LuigiBlood/64dd/wiki/Emulation-Info),
combo-game paragraphs, says a cartridge takes boot priority over the DD IPL.
The cartridge then handles its expansion. [F-Zero X](https://github.com/LuigiBlood/64dd/wiki/F-Zero-X)
describes a rebooter: read LBA 833 into `0x80600000`, use that descriptor's
LBA range/load address, decrypt the first `0x100` bytes, then execute the
loaded image through LeoBootGame. Its function addresses are explicitly for
the Japanese version; do not apply them to US/patched/loaded images blindly.

**Observed source:** `core/device/device.c:197-209` instead chooses
`MM_DD_ROM` whenever `dd_rom_size > 0`, even with a cartridge. The adjacent
comment asserts disk-first behavior on hardware, contrary to the wiki.
This condition tests loaded IPL size, not actual disk insertion.
`core/device/pif/pif.c:125-173` detects CIC from the selected IPL3 and sets
PIF boot ROM type; `core/device/pif/bootrom_hle.c:93-123,148-150` uses that
type to choose/copy IPL3 into RSP DMEM and continue at `0xa4000040`.
Thus this is a meaningful boot-source decision, not just an incorrect comment.

**Correction to earlier notes:** calling this policy a “boot-priority
correction” and instructing collaborators to preserve it as a proven fix was
unjustified. Preserve the baseline for comparison, not the assumption that
this behavior is correct. The earlier disk-review interpretation that this
“matches conceptually” is also withdrawn.

**Hypothesis, not diagnosis:** native cart+EK may enter the wrong startup
path before the cartridge's expansion detection/reboot. This is an earlier
boundary than a presumed RSP/context corruption. Unknown: actual selected
inputs, CIC, boot path, and last successful stage in the user's new run.
Record cart size/presence, disk load result, IPL size/CIC, selected source,
PIF ROM type and executed image before proposing a change.
Any future cartridge-first candidate must distinguish a genuine combo cart
from a frontend disk-launch placeholder; `rom_size > 0` alone has not been
validated as that discriminator. Keep standalone disk boot working.

### Finding B — EK cartridge conversion is not the native expansion path

External sources checked:

- https://64dd.org/database.html — distinguishes cartridge ports from disk
  images and Combo formats; cartridge ports are adapted for flashcarts.
  Its general warning says emulator saves do not work for those ports.
- https://64dd.org/news/2020-05-24-fzxeng.html — author explains the English
  cartridge port versus English disk conversion; US disk pairs with US
  F-Zero X, with a development format alternative. Its 32-bit-engine and
  Fixed Audio Timing advice is **Project64-specific**, not authorization
  to change similarly named Mupen settings.
- https://mutecity.org/wiki/F-Zero_X_Expansion_Kit — corroborates original
  expansion versus cartridge distinction; not the primary register spec.
- A guessed `https://64dd.org/tutorial.html` returned 404; it was not used
  as evidence. Do not repeat that URL as a working tutorial.

These establish the distinction, not the exact identity of the user's ROM.
For a confirmed self-contained EK **cartridge conversion**, use the
DD-disabled cartridge path and preserve baseline WritableROM persistence.
Do not attach a separate IPL/disk merely because the title contains F-Zero.
The separate **Combo** format may still need an original disk; do not
confuse it with the self-contained conversion.

Source supports the user's configuration concern:
`java/persistent/GamePrefs.java:592-613` defaults support off and clears
effective IPL/disk paths when off. When enabled, it accepts saved paths
except directory paths. `:705-714` classifies header names containing
F-ZERO as DD-capable; this is not proof DD is required or that a ROM is a
native cart rather than a conversion. UI availability must not be read as
automatic compatibility detection. `GamePrefsActivity` removes DD controls
for titles not classified as DD games; it does not prove old stored values
were deleted. `app/src/main/res/xml/preferences_game.xml:101-116` exposes
support/IPL/disk controls.

`core/main/main.c:1630-1644` attempts disk loading before IPL. If enabling
support supplies valid disk+IPL with a cart hack, it can invoke the
disk-first policy in Finding A. Merely switching support on without valid
inputs does not prove DD actually became active. **Plausible configuration
explanation, not established cause of the earlier cart-hack failure.**

Record exact legal ROM hash, header, effective database entry, DD checkbox,
effective empty paths, and fresh launch without autoload before concluding.
The website's general save warning does not refute this fork's explicit
WritableROM implementation or the user's reported success; it warns against
assuming generic emulator support. Controlled persistence testing remains
required. Do not “fix” the warning by disabling existing writable saves.

### Finding C — configuration/input boundary precedes execution diagnosis

- `GamePrefs.java:642-650`: default CountPerOp becomes 1 for the known NDD
  CRC or a nonempty effective disk path. NDD CRC classification can act
  independently of the support checkbox; this is existing baseline behavior,
  not a new gate to copy. Capture actual CountPerOp/denominator and PI
  alignment (`:658-659`), not just profile names.
- `core/main/main.c:1080-1138`: IPL filename callback, load/format checks,
  disable-on-error. `:1147-1318`: disk size/format/load/save-image handling.
  A saved disk may replace the original image at startup; record which
  source was actually used without deleting it.
- Existing useful messages: `DD IPL ROM:`, `DD Disk:`, `Loading a saved disk`,
  `Can't get DD disk file size`, `Failed to load DD Disk`, `Wrong disk format`,
  and `Disabling 64DD`. An IPL metadata message alone does not establish
  guest IPL execution.
- `core/device/r4300/r4300_core.c:142,149,176` has actual-engine startup
  messages for Pure Interpreter, Dynamic Recompiler, Cached Interpreter.
  `core/main/main.c:1523` reads `R4300Emulator`. Capture the executed engine,
  not only the requested setting.
- Existing copied ROM database may differ from APK assets. Verify the
  effective entry without overwriting the installation's settings/saves.

### Finding D — DD/PI/interrupt fidelity and remaining gaps

**Matches / rejected false leads**

- Memory-map DD windows agree: C2 `0x05000000`, DS `0x05000400`,
  registers `0x05000500`, MSEQ `0x05000580`, IPL `0x06000000`
  (`core/device/device.h:73-78` and wiki Memory Map).
- CART interrupt is correctly represented by CP0 IP3:
  `core/device/r4300/cp0.h:94` defines `0x00000800`.
  `dd_controller.c:120-130` asserts/clears that source. It is not an MI
  source; absence of a CART bit in the MI register is not a defect.
- `core/device/rcp/pi/pi_controller.c:54-130` marks DMA busy and schedules
  `PI_INT` with handler cycles; `:225-239` completes it and raises MI PI.
  The wiki's warning against instantaneous PI DMA is not proof this code
  violates it. Synchronous buffer-manager advancement is a separate question.
- Wiki Index Lock Retry word `0x000E0001` versus a native `0x000E0000`
  constant is not a demonstrated decoding failure: command dispatch uses
  `(value >> 16) & 0xff` (`dd_controller.c:394`). The command body being
  unsupported is the actual issue, not its unused low bits.

**Source-defined simplifications / discrepancies requiring targeted tests**

- Command switch (`dd_controller.c:369-452`) implements seek, flag clears,
  RTC reads, feature inquiry, and logs disk type. Many commands listed in
  Commands (drive modes/delays, RTC writes, retry, LED, writer/calibration,
  hidden memory access) have no implementing body and fall to a warning.
  Completion still signals MECHA. Feature inquiry returns zero. Need the
  actual command stream to determine relevance to F-Zero.
- SET_DISK_TYPE only logs (`:425-428`); wiki Disk Specification describes
  disk-type/write-lock semantics. `:181-196` writes through storage.
  There is no demonstrated full controller ROM-zone write protection.
  This is a fidelity/save-safety issue, not a demonstrated boot cause.
  Never test write protection on original disks/saves.
- Wiki Registers says BM interrupt is acknowledged by reading status;
  `read_dd_regs` (`:329-366`) clears/advances only under a sector condition.
  PI cart-address writes also clear DATA_RQ/BM interrupt (`:635-647`).
  Compare full read/PI/ack order, not an isolated snapshot.
- Seek immediately sets track/lock and command completion; no explicit
  seek delay/Busy lifecycle is visible (`:401-412,450-451`). The wiki does
  not supply an exact delay here. Do not invent one.
- BM starts synchronously (`:488-498`), advances sector state (`:198-284`),
  and signals requests/interrupts. C2 is zero-filled (`:132-147`) and
  progression is simplified (`:253-275`) versus documented per-C2-sector
  interrupts. Trace sector counts, C2 request bits, stopping, and guest
  acknowledgement before changing the model.
- MSEQ RAM exists (`dd_controller.h:65-67`), but regular BM operations
  directly read/write sectors rather than interpret uploaded sequences.
  Full MSEQ execution/error semantics are not implemented. Wiki opcode
  meanings marked uncertain remain uncertain; no invented interpreter.
- Hard-reset register write sets reset state (`:501-506`); full Busy/reset
  completion behavior is not modeled in that branch. Fresh start and
  application reset are distinct: `device.c:269-279` schedules CPU reset.
- Missing sector mapping sets BM MICRO (`:149-163`); read_sector returns
  without copying on NULL (`:166-179`). Retail track-6 special handling
  (`:244-251`) follows the documented detection quirk in broad intent,
  not proven exact error timing.
- DD DMA buffer loops (`:550-632`) need bounds auditing for malformed
  guest addresses/lengths. No out-of-bounds event has been observed.
- Unknown PI handler logs and returns without scheduling completion
  (`pi_controller.c:67-71,102-109`). Only relevant as a stall hypothesis if
  a trace shows that address and the guest waiting for completion.
- `device.c:189-192` always passes a DD struct pointer to PI. A non-null
  pointer is therefore not an activation gate. Mapping at `:161-171`
  depends on loaded IPL. New shared PI diagnostics must not rely on pointer
  existence to claim ordinary-game isolation.

### Finding E — disk layout, validation, persistence, and CIC

- `core/device/dd/disk.h:30-40,64-89,109-137` represents SDK/NDD size
  `0x03DEC800`, MAME size `0x0435B0C0`, 85 user sectors/block, two
  blocks/track, variable zone sector sizes and 4,316 LBAs. Broad match
  to Disk Specification and Disk Image Formats.
- `disk.c:92-220` builds LBA mappings, interleaves blocks, reverses head-1
  traversal and consumes defect lists. Physical MAME versus logical
  SDK/NDD/D64 sector routing is separate (`:252-383`).
- System-area candidates, region/type/load-boundary checks, repeated
  sectors, and development 192-byte handling appear at `:402-481`.
  Disk ID repeated-data checks are not proof of full guest Disk ID validity.
- D64 validation/expansion appears at `:483-576`; its path sets development
  mode (`:608-614`). This is emulator format policy, not proof that the
  user's source disk or drive was development hardware.
- Read-only/full-save/RAM-only backends (`:44-88`) are distinct; RAM-only
  persistence limits do not alone establish ROM write protection in memory.
  Inspect effective selected backend before disk writes.
- System Area and Formatting Process describe protected/error-prone LBAs
  and formatting tools. Code has selected NULL/error behavior; exact
  physical error reproduction and formatting-tool operation are untested.
- The old Disk Format (SDK) page explicitly disclaims its own accuracy.
  Its conflicting track counts must not override Disk Specification and
  format-specific code without stronger evidence.
- `core/device/pif/cic.c:49-72` recognizes DD CIC checksums/seeds including
  8303/8501; `pif.c:154` classifies DD boot CICs. The wiki CIC page has
  tentative mappings, not a complete verified test suite. Capture the
  detected IPL identity; do not infer one solely from file extension.
- Wiki IPL region bytes and F-Zero drive/disk game-code requirements matter.
  Wrong retail region can hang guest drive detection without a friendly
  error. Use a known matching original set or the documented translation's
  requirements, never mix arbitrary US/Japan/development components.

### Finding F — preserve writable cartridge controls; no native-DD inference

`core/device/cart/cart_rom.c:39-98,111-212,235-290` implements per-ROM
WritableROM persistence/restoration for CPU and PI writes using `.cart_ram`
and `.idx`. `core/main/rom.c:190-223,453,556,772-774` defaults/parses the
flag. Existing database entries, not DD enable, select this capability.

Review-only robustness notes (not new regressions or requested fixes):
process-global file handles depend on clean close between games; write I/O
results are not fully checked; abrupt termination is not equivalent to a
clean save; index records use native integer representation. None identifies
the native DD stall. Keep these out of the native-fix scope and preserve
both save files. A ROM read ending exactly at its size takes a fallback
copy/zero-fill branch; that alone is not a demonstrated boundary bug.

### Page-by-page coverage ledger

All links below are relative to the wiki root cited above. “Guest behavior”
means emulated guest software should implement it; absence of a bespoke
emulator parser/hook is **not** itself a missing hardware feature.

| Page | Coverage / result |
| --- | --- |
| Home | Scope and provenance; no hardware rules. |
| _Sidebar | Navigation checked; two absent linked pages identified above. |
| Memory Map | DD/I/O windows match; PI timing recommendations require effective domain-register traces. Unknown/test registers stay unknown. |
| Commands | All basic/RTC/other/writer/hidden command groups compared; partial command implementation, see D. |
| Registers | Address/status/BM masks compared; acknowledgement, reset, MSEQ and error fidelity unresolved, see D. Question-mark bits not asserted as facts. |
| Micro Sequencer | Read/write regular/long sequences reviewed; direct BM model is not a full sequencer. Tentative opcode fields preserved. |
| CIC | Tentative DD types/seeds compared to CIC detection, see E; no hardware checksum test. |
| 64DD IPL | Region/magic/font/sound map and launch protocol reviewed; guest boot unverified, see A/E. |
| Disk Format (SDK) | Read in full but explicitly obsolete/unreliable; conflicts recorded rather than copied into implementation. |
| Disk Specification | Geometry, defects, ROM/RAM zones, disk types/write protection compared, see E. |
| System Area | Retail/development placement, fields, protection, Disk ID reviewed; uncertain read-error descriptions retained. |
| Formatting Process | leowrite/dn procedures and system fields reviewed; no claimed formatting tool support or destructive test. |
| Disk Access Process | Seek/read/write, BM/CART/PI/C2 order reviewed; LBA-conversion heading has no substantive content. |
| Emulation Info | Non-instant DMA, CART, retail quirk, formats, combo boot, hot swap, RAM saves, regions, peripherals reviewed. Cartridge-priority conflict identified. Disk-swap timing suggestions are implementation advice, not measured hardware constants. |
| Disk Image Formats | D64/logical NDD/physical MAME reviewed; D64 development policy and warned conversion limitations recorded. |
| Capture Cartridge | Register/command/video/audio map reviewed; no matching capture-device implementation found. Several fields explicitly unknown. |
| Modem Cartridge | ROM libraries/register bits reviewed; no matching modem device implementation found. |
| F-Zero X | Region/game-code checks and reboot/decryption sequence reviewed; principal boot hypothesis A. |
| Dezaemon 3D | Non-rebooter disk storage/sample use and identity checks; guest behavior, generic DD fidelity required. |
| Mario Party | Reboot/LBA load/decrypt sequence; guest behavior, no bespoke core hook required. |
| Pokemon Stadium (J) | POKE check/load/reboot sequence; guest behavior; version-specific addresses not portable. |
| Pokemon Stadium 2 (J) | Short expansion-check/US-drive freeze note; reinforces region controls, not proof of current defect. |
| Ocarina of Time | Non-rebooter expansion/file/scene hooks; guest behavior, not a requirement to patch game code in the emulator. |
| Games with libleo | Library/version inventory reviewed; no requirement for emulator per-version dispatch inferred. |
| Randnet Service | Historical dial-up/HTTP/service catalog reviewed; not evidence services still exist or that emulator implements a modem. No endpoints contacted. |
| Mario Artist Service | Historical CGI/gallery/menu formats reviewed; not live service support. |
| File Formats | Index reviewed; guest/application data, not a requirement for core parsers. |
| Paint Studio Formats | Named image/animation families reviewed; incomplete entries preserved as incomplete. |
| Talent Studio Formats | Talent/animation/model/optional audio structures reviewed; no reason to alter generic DD loading. |
| Polygon Studio Formats | Short model/header notes and external-header pointer reviewed; linked external parser not audited. |

### Next evidence and falsifiable decisions

1. Obtain one unchanged-baseline fresh native run with exact inputs,
   effective DD setting/paths, source/APK hashes, engine/plugins, memory/
   CountPerOp/PI settings, saved-disk choice, and visible failure time.
   Preserve full local logcat; share only a privacy-reviewed copy.
2. Confirm last known stage using existing logs. In particular distinguish
   input failure, wrong boot source/region, cart expansion detection,
   disk descriptor/code reads, guest reboot, and later menu/audio failure.
3. If insufficient, add a **DD-activation-gated diagnostic-only** change
   at the first unknown boundary. First candidate observations: selected
   boot source/CIC/ROM type; bounded DD command/PI/BM event tuples with
   sequence, sector, status, addresses, lengths, completion and IRQ.
   Record ring wrap/drop counts. No global verbose/per-instruction logging.
4. For the boot-priority hypothesis, compare fresh cart+disk startup with
   cartridge-first behavior only after actual combo versus disk-only
   classification is established. Reject the hypothesis if the original
   trace already reaches the expected cart detection and disk-code boot.
5. For a transfer hypothesis, compare BM start -> sector request/CART ->
   PI DMA -> completion/BM advance -> guest acknowledgement. Include
   C2/gap/end conditions; do not assume that simplified sequence describes
   every hardware phase. Record the first actual divergence.
6. Only if fresh evidence reaches a context-corruption boundary, restore
   minimal exact-write coverage. Verify effective engine; cached interpreter
   coverage includes dispatcher and recursive delay slots. Dynarec samples
   establish intervals, not writer PCs. No exact writer is established yet.
7. A behavioral candidate is accepted only with native interactive menu/
   audio plus DD-disabled Amped Up cursor/gameplay, Mario Tennis comparable
   performance/input/audio, and disposable-copy EK writable-save persistence.
   No acceptance cell changed to passed during this audit.

### Audit verification and change ledger

- Application source remained unchanged; changes are investigation text only.
- All 30 available wiki Markdown files assigned coverage; absent pages and
  obsolete/uncertain material explicitly recorded.
- Boot selection, CP0 CART bit, PI scheduled completion, command decoding,
  and Android DD/header classification cross-checked directly.
- Rejected false leads and corrected earlier handoff claims recorded above.
- No archived diagnostic markers, watchdogs, renderer/RSP hacks, signing
  changes, app uninstall, data clearing, or save overwrite performed.
- Publication must be a focused non-force documentation commit on the live
  debugging branch; abort/reconcile if another contributor has advanced it.
- Native failure remains unresolved. Highest-priority concrete discrepancy
  is boot-source policy; highest-priority missing evidence is a fresh
  unchanged-baseline device capture with verified configuration.

## 2026-09-11 — first uploaded Android capture: logging suppression confirmed

### Evidence identity and privacy

Received `attached_assets/native-dd-logcat_1789181995975.txt` (15,730 lines,
1,953,796 bytes), SHA-256
`a981ee5941cddec07e098e300b0c758b5ba0e959c7f3c5e47183b6ea13c4659b`,
and `attached_assets/installed-package_1789181995978.txt` (67 bytes),
SHA-256 `84ca9f29c434e22e2ee242e3814f1eaccfbf02a2c16f8a79e3f8fb1d0535d199`.
Line references below refer to the original log upload, not a filtered copy.
Raw logs include unrelated device/app information: do not publish them to
GitHub. Only this selected technical analysis is intended for publication.

The package report contains only:
`Unable to find package: org.mupen64plusae.turnip.pwnedbygary.debug`.
The two relevant late launches use
`org.mupen64plusae.turnip.pwnedbygary` (without `.debug`). Correct the prior
capture command; do not uninstall/reinstall anything to match its old name.
The log also contains earlier debug-package history. Package name alone does
not prove release/debug compiler settings, source revision or APK hash.

### Separate the launch attempts

Logcat includes buffered history from 20:25 through 22:59 in device-reported
time; these are not all one test. Two late DD-configured attempts are visible:

| Device time / PID | Observed evidence |
| --- | --- |
| 22:55:53 / 24315 | IPL copy request at line 13059; separate disk copy at 13069; native logging disabled at 13073; cartridge file opened at 13074; parallel graphics/RSP selected at 13111/13125. |
| 22:59:06 / 24679 | IPL copy request at 14677; separate disk copy at 14690; native logging disabled at 14695; cartridge file opened at 14696; parallel graphics/RSP selected at 14744/14750. |

Selected filenames indicate `F-Zero X.z64`, `F-Zero X.ndd`, and
`N64DD IPLROM [Proto] [USA].n64`; autosave labels identify F-ZERO X (U).
This supports attempted native cart-plus-disk setup, not successful core
validation or verified matching disk/IPL regions. Filenames are not hashes.
Disk copy uses the cartridge header-derived working filename; an absent
`.ndd` suffix on that cache target is not itself evidence of an incorrect copy.

Earlier PIDs 23224, 23527 and 23671 report a missing DDROM file, attempted
disk load from the working directory, and Dynamic Recompiler startup
(lines 6638–6851, 8546–8819, 10005–10287). **Do not attribute those CPU
messages or disk-load failures to PIDs 24315/24679.** The earlier sessions
are not evidence of the effective CPU engine for the late DD attempts.

### Confirmed observation blocker, not stall cause

Both DD attempts explicitly report
`Disable core debug due to 64DD ROM found`.
This matches `java/jni/CoreInterface.java:537-556`: if the cached DD IPL
file exists, CoreStartup receives a null native debug callback. Otherwise
it receives the normal logging callback. This suppresses INFO, WARNING and
ERROR as well as verbose core output, hiding the expected disk/IPL checks
and actual-engine startup messages.

This explains **missing diagnostic evidence**, not why the emulated game
fails. Another identical logcat capture cannot restore messages the app
never emits. Do not ask the user to repeatedly reproduce without addressing
this observation boundary. The callback condition is file existence, not
explicit support activation; any new diagnostics need the stricter gate.

`java/jni/CoreService.java:579-627` has two distinct paths: a directly
launched NDD uses global Japan IPL selection, whereas a cartridge launch
uses the per-game configured IPL/disk. Therefore a proposed logging gate
of `enable64DdSupport && isNdd` would **exclude this cart-plus-disk case**
and is not acceptable. Gate on explicit per-game support for this
investigation, independently of the cart file's `isNdd` classification.

### Other observations and limits

- The final run initializes Vulkan/Turnip and opens/starts Android audio
  (e.g. lines 14792–14915). That proves host subsystem activity, not visible
  guest frames, correct menu output, non-silent audio or guest progress.
- Exit/autosave operations are visible for both attempts (14348–14400 and
  15579–15637). A label at ERROR severity saying “Save completed” is not a
  reported save failure. This is emulator save-state activity, not proof of
  native DD RAM persistence or EK WritableROM persistence.
- No affirmative save-state *load* marker was identified for these launches.
  With core logging suppressed, absence is not proof autoload was disabled.
- Bridge unresolved-symbol messages include the JNA invokePointer symbol
  and SendVRUWord (13122/13126 and 14747/14751); plugin/host initialization
  continues afterward. These need context, not immediate causal attribution.
- Repeated external-app-directory creation warnings also exist; internal
  cache operations and autosave completion coexist with them. No demonstrated
  disk read failure follows from those warnings alone.
- No fatal native signal/Java FATAL EXCEPTION was identified for the two late
  attempts. Their exits include UI exit/autosave/shutdown. This does not
  establish either successful gameplay or an exact point of guest stall.
- An independent review incorrectly reported no DD copy/suppression markers
  and equated the F-ZERO X (U) autosave label with a non-DD run. Direct line
  inspection disproved both assertions; they are rejected, not findings.

### Next action, deliberately narrow

**User-reported visible boundary, received after log review:** the application
freezes on the IPL loading menu, before the screen where the IPL would report
that the time has not been set. This is reported visual evidence, not a
decoded trace or independently inspected screenshot. It supports reaching
IPL presentation and focuses investigation on early IPL progression. It does
not establish which instruction stopped, successful disk-code loading, or an
RTC defect: execution could stop before the clock check. Do not describe
this run as a proven post-LeoBootGame/game-context stall. The cartridge-first
hardware discrepancy remains relevant for the intended combo launch.
No audio observation or elapsed-to-freeze measurement has yet been supplied.

**Screenshot subsequently received:** `attached_assets/image_1789182282862.png`
shows a white 3D logo on a pale/lavender background, touchscreen controls and
an FPS overlay; no unset-clock message or game menu is visible. This visually
corroborates the displayed screen described by the user. A still image cannot
establish a freeze duration, continuing animation, input response or guest
CPU/RSP progress. The FPS overlay is not evidence that guest logic advances.
Do not infer an RTC failure, graphics failure, or exact IPL execution address
from the image. Raw image remains an uploaded evidence asset, not a runtime
change or a published game asset.

1. Obtain package metadata for the actual non-suffixed package, local build
   commit/dirty diff, and audio/timing details if needed to distinguish the
   two late attempts. Visible behavior is now reported above; do not ask
   the user to repeat it. Do not infer a verified baseline from filenames.
2. Prepare a separate diagnostic-only candidate that enables bounded
   startup/load/engine information only when N64DD support is explicitly
   enabled. Preserve the existing DD-disabled callback behavior exactly.
   Do not simply enable all verbose logging: DD register/DMA messages can
   flood output and perturb timing. Filtering after JNA delivery still incurs
   callback overhead; prefer filtering at the native emission boundary.
3. Verify the exact packaged build carries the diagnostic change, then
   collect one fresh DD attempt. Keep original signing and all saves.
4. Retain boot-priority mismatch as a hypothesis. This upload does not
   expose boot-source/CIC/guest execution sufficiently to confirm or reject it.

No runtime code, configuration defaults, APKs or saves changed in this
capture-review pass. Task remains unresolved; no acceptance test is marked
passed. The earlier “no fresh traces” wording is now superseded by this
received capture, while “no source-verified baseline trace” remains true.

### Corrected installed-package report

Received `attached_assets/installed-package-corrected_1789182363125.txt`.
Selected fields (original report line numbers):

- Line 258: package `org.mupen64plusae.turnip.pwnedbygary`.
- Lines 265–266: primary ABI `arm64-v8a`, no secondary ABI.
- Lines 268–270: versionCode 335; versionName
  `3.0.335 (beta) dc955483`; minSdk 23, targetSdk 34.
- Lines 274/292: package flags do not include DEBUGGABLE. Do not assume
  `run-as` access from the previously installed debug app carries over.
- Line 287: last update `2026-09-11 22:49:50` in device-reported time,
  preceding the two late DD attempts.

The embedded revision `dc955483` matches the v336 baseline commit prefix,
and version 335 matches that tag's known Gradle metadata. This is positive
baseline-identification evidence, not a wrong-release finding. A version
string is not a cryptographic verification of the packaged native libraries,
nor does it prove a clean local working tree. Exact APK hash/dirty-diff
verification remains unavailable; do not block the diagnostic design merely
to ask again for identity already supplied by this report.

This supersedes the package-identity uncertainty from the failed `.debug`
lookup. The native core's DD logging suppression remains the next concrete
observation blocker. Keep original signing and the non-suffixed package for
any update; do not switch package variants, uninstall, or clear data to gain
diagnostic access. No raw package dump is published with this summary.

## 2026-09-11 — DDSTART1 diagnostic-only candidate

The user explicitly confirmed the tested APK is the v336 release. No more
baseline identity confirmation is required to proceed with this observation
change. This candidate does **not** fix the freeze or change boot priority.

### Exact change and activation boundary

- `CoreService` passes the existing per-game `enable64DdSupport` boolean
  into `CoreInterface.coreStartup`. New launch metadata is emitted only
  under that boolean: direct-NDD classification, configured input presence,
  CountPerOp and denominator. Disk-only metadata uses the actual global IPL
  selection rather than incorrectly reporting the cart-specific paths.
  `autoLoadRequested` reports the existing `!mIsRestarting` decision, without
  changing it; an attempted load is not proof a state file was restored.
- `CoreInterface` sets process-local `M64P_DD_STARTUP_DIAGNOSTICS` to exactly
  `1` or `0` at every startup, preventing a previous session's activation
  from carrying forward. This is app process state, not a workspace secret
  or an instruction for users to set an environment variable on Android.
  Failure to set it aborts startup with an explicit error rather than risking
  stale diagnostic activation.
- With support enabled, register the existing core callback even if the IPL
  exists, so load errors remain observable. With support disabled, retain
  the original IPL-exists/null-callback selection and ordinary logging.
  Direct-NDD classification alone does not enable these diagnostics.
- Native `callbacks.c` snapshots the option at callback registration, filters
  out STATUS/VERBOSE before formatting and JNA delivery, and allows a maximum
  of **256 native callback messages total per CoreStartup**. This includes
  the identity marker and a final coverage-limit message in place of the
  256th ordinary record. The final marker is not a guest stall indication.
  Atomic budget updates prevent concurrent producers exceeding the cap;
  delivery ordering across threads is not an exact guest event sequence.
- `device.c` emits one gated selection record: cartridge/IPL byte sizes and
  chosen CART/DD_IPL source. Existing load/CIC/engine messages supply the
  surrounding context. The original IPL-first selection expression is
  unchanged, including its documented hardware discrepancy.
- After core shutdown quiesces producers, clear callback/context/diagnostic
  activation only for a diagnostic session. The baseline non-DD shutdown
  callback lifecycle remains unchanged.
- On native startup failure, discard added diagnostic callback state before
  returning the original error. On Java setup failure after successful native
  initialization, explicitly DD-gated teardown detaches plugins, closes ROM
  and shuts down the core; it does not prune/export/sync saves. This closes
  the diagnostic lifetime without changing DD-disabled failure handling.

Plain-cart overhead is confined to per-session option initialization and
simple false guards in existing logging/device initialization/shutdown code.
There is no new instruction hook, polling timer, buffer-manager transition,
RSP/renderer/timing change, file trace, ROM database change, or save mutation.
DD-enabled callback logging has finite but nonzero overhead; a differing
visible result under diagnostics would need an uninstrumented comparison.
Plugin/Android logs outside this native core callback are not capped by it.
This is a startup observation, not a sustained PI/DD or exact-writer trace.

### Review corrections and reproducible verification

Initial review found the proposed cap allowed 257 crossings, shutdown did
not clear the new state, and direct-NDD metadata reported cart preferences.
All three were corrected before publication. Tests cover baseline verbose
logging, DD level filtering, exact cap, single limit marker, error/warning
delivery, null-callback teardown and subsequent plain-session reset.
The teardown unit test covers callback reset; Android lifecycle invocation
and actual game behavior still require device confirmation.
Follow-up review found startup/setup failure lifetimes also needed cleanup;
the native error returns and DD-only Java failure teardown now handle these.

From repository root:

```sh
bash tools/test-dd-startup.sh
./gradlew :app:assembleDebug --console=plain
bash tools/verify-dd-startup-apk.sh app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk
```

The APK verifier checks all three DDSTART1 markers in the actual packaged
arm64-v8a core and prints the APK SHA-256. A Java “requested” message alone
does not prove the native change was packaged. An old locally cached core
must not be accepted just because Gradle reports success.

### Local signed build and capture

Use the current debugging branch with a clean or documented working diff.
The workspace debug APK is for build/package verification only: it is not
the user's release signing identity and is not being distributed.
Build locally with the original signing setup, same non-suffixed package
and compatible version code; verify its packaged core with the helper above.
Do not accept a fallback debug signing identity for a release update.
Do not upload keystores or credentials. Do not uninstall or clear data.
If an update is rejected for signing/version reasons, stop and report it.

On the Mac, the prerequisite versions are Java 17, SDK 34, build tools
34.0.0, NDK 26.1.10909125 and CMake 3.22.1. The prior chat supplied an
M1-compatible setup using pinned command-line tools and Rosetta. Use the
local SDK path; do not copy Linux `local.properties` paths to macOS.
The repository Gradle wrapper supplies Gradle itself.

After the locally signed diagnostic APK is installed, from any Mac directory:

```sh
export PATH="$HOME/Downloads/platform-tools:$PATH"
mkdir -p "$HOME/Desktop/n64dd-ddstart1"
cd "$HOME/Desktop/n64dd-ddstart1"
adb shell dumpsys package org.mupen64plusae.turnip.pwnedbygary > installed-package.txt
adb logcat -T 1 -v threadtime > ddstart1-logcat.txt
```

Start recording before opening the game. `-T 1` limits historical backlog
without clearing device logs. Use the same native cart/disk/IPL and settings,
support enabled, fresh launch without loading a state. Reproduce the logo
freeze, allow about 30 seconds, then Control-C. Share privacy-reviewed logs,
the APK verifier output and whether screen/audio behavior changed. Preserve
all saves and do not use the cart hack as this native test.

Expected new records: `DDSTART1 launch`, `DDSTART1 requested`,
`DDSTART1 native`, then disk/IPL/CIC/engine messages as the core reaches them,
and `DDSTART1 boot selection`. Missing stages may be loading failures,
budget exhaustion, or packaging/activation failures; distinguish these.
In DD-disabled controls there must be no DDSTART1 runtime records.
Native menu/audio, ordinary-cart regressions and writable-save acceptance
remain pending; the project task is not complete.

### Workspace verification result

- `bash tools/test-dd-startup.sh`: passed.
- `./gradlew :app:assembleDebug --offline --console=plain`: passed after
  all review corrections (27 seconds, 417 actionable tasks). This is compilation/
  packaging verification, not a device boot result.
- `bash tools/verify-dd-startup-apk.sh ...`: all three native arm64 markers
  present in the resulting APK.
- Workspace-only verification APK SHA-256:
  `b27201ba2e08f63b33a313a92396817297ab5836a35de8ac38388b7a61ef304d`.
  The locally signed release build will have a different hash; record its own.
- `git diff --check`: passed. No running web app exists for a browser
  screenshot check; the supplied Android screenshot remains visual evidence.

## 2026-09-11 — DDSTART1 device capture: combo boot selection confirmed

Evidence: `attached_assets/ddstart1-logcat_1789183322734.txt`, 907 lines,
SHA-256 `149d0a7bab205c5a062bb09825020d3375767bce8ba836bb74cab32d4c4e993e`;
package report `attached_assets/installed-package-debug_1789183322735.txt`.
Keep raw uploads private; only selected technical findings are published.

One relevant launch, PID 27775, starts around device time 23:21:18.
The log ends at 23:21:36.821, roughly 18 seconds after startup, not a verified
30-second stalled interval. The package report confirms the `.debug` package,
arm64-v8a, DEBUGGABLE, version `3.0.335 (beta) c836a422`, updated 23:19:12.
That version label matches the supplied workspace diagnostic build; native
markers establish that this is not the old logging-suppressed capture.

### Confirmed records (original log line numbers)

- 203–205: explicit support enabled, directNdd=false, configured IPL/disk
  present, autoLoadRequested=false, requested CountPerOp=1/denominator=0;
  both Java and native DDSTART1 markers present.
- 249–257: F-ZERO X (U) [!], MD5
  `753437D0D8ADA1D12F3F9CF0F0A5171F`, CRC `B30ED978 3003C9F9`,
  native .z64, 16,777,216 bytes, USA. This is a real cartridge input, not
  the frontend's dummy ROM for direct disk launch.
- 278: saved full-disk `.ndr` load fails. The subsequent recognized disk at
  291 is 64,931,840 bytes, SDK format. `load_dd_disk` tries original media
  after saved-file failure; no fatal original-file/format failure follows.
- 294,299–300: IPL loaded, 4,194,304 bytes; boot selection explicitly
  `source=DD_IPL` with the real cartridge also loaded; CIC 8501 detected.
- 262,270,276: parallel graphics, Android audio and parallel RSP plugins.
- 475: four RDRAM modules, total 8 MiB.
- 514: actual engine **Dynamic Recompiler**. This is now runtime evidence,
  not an inference from another PID or the requested profile.

No DDSTART1 limit marker appears; the 256-message cap is not the explanation
for silence after the existing startup records. No exact guest PC, command/
DMA sequence, disk-code entry or context writer is recorded by this probe.
The log does not contain a native fatal signal/Java FATAL EXCEPTION for this
run. Host graphics/audio startup does not establish guest menu/audio success.
Current run's visible/animation/audio outcome still needs the user's report;
the prior uploaded screenshot belongs to the earlier observation.

### Correction: “Loading a saved disk” is not evidence of save loading

Line 292 contains that warning, but source inspection of
`core/main/main.c` near the end of `load_dd_disk` shows it is emitted whenever
the first region word matches JP, US or development, irrespective of the
file source. It does **not** establish a saved image was used. Here line 278
shows saved-file failure and the loader's original-file fallback succeeds.
Do not delete saves or attribute the freeze to save contamination based on
this wording. The warning does establish that one of those region constants
matched, but does not print which one. Matching IPL/disk region remains
unverified; USA in a filename alone is insufficient.

### Interpretation and next decision

The boot-priority discrepancy is now observed for this exact native combo:
real cartridge + disk + IPL -> DD IPL startup, contrary to the wiki's
cartridge-first combo rule. This is the strongest established configuration/
boot mismatch so far, **not proof it causes the logo freeze**. Input presence,
recognized disk format, 8 MiB memory, actual CPU engine and no requested
autoload are no longer the first unknown boundary.

Before a behavior candidate, preserve this diagnostic baseline and confirm
whether the same static logo occurs in this run. A narrowly scoped
cartridge-first comparison must require explicit DD activation and a real
combo cartridge, preserve direct-disk dummy-ROM handling, and leave all
DD-disabled games unchanged. Do not use the diagnostic flag as an implicit
permanent hardware-behavior setting. Also capture the actual disk region
and game code so a region mismatch is not confused with boot priority.
No scheduler/context/RTC/renderer correction is justified by this capture.
No runtime code changed in this analysis pass; task and acceptance remain
open.

## 2026-09-11 — stopped-logo confirmation and DDSTART2 comparison candidate

The user confirmed the DDSTART1 run shows the **same logo screen with the
logo stopped moving**. In a subsequent clarification, the user reported
approximately half a second of audio as the 3D N64 logo starts, followed by
audio and video freezing together. This is reported playback, not a measured
audio trace. It does not identify CPU, RSP, graphics or RTC as the cause.
This establishes reproduction
of the visible symptom under bounded diagnostics, not an exact stalled PC.

### Why this comparison, rather than a context or RTC patch

The native trace establishes a real US F-Zero cartridge is loaded but DD IPL
is selected. LuigiBlood's Emulation Info explicitly describes cartridge-first
boot for combo games. Testing that documented mismatch is justified before
assuming the guest reached expansion reboot or a corrupting context writer.
This is a deliberate change from the earlier plan's context-first premise:
the current evidence identifies an earlier boot-policy discrepancy.
It remains a comparison candidate, not a root-cause claim or accepted fix.

### Precise gate and changes

- Java passes the actual `isNdd` launch classification into coreStartup.
- Process-local `M64P_DD_COMBO_CART_BOOT` is set to `1` only for
  `enable64DdSupport && !directNdd`; otherwise it is overwritten with `0`.
  This is independent of the diagnostic-output flag. Users should not set
  either flag manually. A setenv error aborts explicitly.
- Native boot selection preserves the baseline expression, then selects
  cartridge IPL3 only when the exact option is `1` and both cartridge and
  DD IPL sizes are nonzero. The helper rejects missing/empty/malformed flags.
- Direct-NDD launches still pass the dummy ROM through the original
  disk-first path because their frontend option is `0`. A nonempty dummy
  ROM alone cannot activate the comparison.
- DD-disabled ordinary carts and DD-disabled EK cart hacks keep original
  boot selection. No ROM-name heuristic, WritableROM flag, timing, IRQ,
  RSP, renderer, disk write or save format was changed.
- The false hardware assertion in the old boot comment was replaced with
  an explanation of the controlled policy. `DDSTART2 boot selection` reports
  selected source and whether the combo override applied.
- Gated metadata now prints parsed disk region/development classification
  and the first six disk-ID bytes in explicitly labeled stored byte order.
  Bounds are checked; these bytes are not a hash or automatic proof of a
  valid cart/disk/IPL combination. The old “Loading a saved disk” warning
  remains unchanged and retains the caveat documented above.

Expected comparison result: the same real combo should now report
`source=CART combo_cart_boot=1`, with cartridge CIC rather than the DD IPL's
CIC at PIF boot. Whether it then detects/loads the expansion or reaches a
menu remains unknown. Reaching only ordinary F-Zero without the expansion
would not count as native EK success. A continued freeze after cartridge
selection would reject the simple claim that IPL priority alone explains it.

### Verification and device instructions

Host helper tests cover disabled/null/malformed flags, missing cart/IPL,
and enabled real combo selection, alongside existing callback cap tests.
These do not replace actual DD-disabled game regression tests.
The APK verifier now requires DDSTART2 boot/region markers plus the native
DDSTART1 identity/coverage markers. DDSTART1 is still the logging mechanism;
DDSTART2 identifies the behavior comparison.

Update the previously supplied diagnostic debug app using `adb install -r`;
do not uninstall, clear data or change the release app. Keep all input and
plugin settings identical. Start a fresh launch without a save state and
record logcat before opening the game. For clarity, save this capture as
`ddstart2-logcat.txt`. Report whether it remains on the IPL logo, boots
ordinary F-Zero only, detects the Expansion Kit, or fails elsewhere; also
report audio/input response if a menu appears. Keep original disk/save
copies intact. Native menu/audio and all regression acceptance remain pending.

DDSTART2 verification: host callback/boot-policy tests passed; final
`:app:assembleDebug` passed in 40 seconds; packaged arm64 comparison and
logging markers verified; focused code review passed with no actionable
blocker. Device execution and regressions are not yet verified.
Comparison APK SHA-256:
`0dcec790f4a452d0788a31088e6ffc1231b92e4119777415bc63cc9d30a43eeb`.
This is a separately signed `.debug` build compatible with the prior supplied
debug installation, not an update for the original release package.

## 2026-09-12 — DDSTART2 attempt blocked by cartridge read permission

User reported “Failed to open ROM for reading.” Uploaded
`attached_assets/ddstart2-logcat_1789223108862.txt` contains 2,440 lines,
SHA-256 `62a5a5e8af2ae77aaceb0f2f6bfaee5d0e909092bf6db19b929b6ee1fb1ae72d`.
Raw logs remain private.

Two attempts (PIDs 29526 and 29774) reach native DDSTART1 registration,
then fail opening the cartridge through Android's external-storage provider.
Lines 1194–1223 and 2181–2207 explicitly report SecurityException/Permission
Denial for the existing `ROMs/n64/F-Zero X.z64` document URI, followed by
the frontend launch failure. Android requests ACTION_OPEN_DOCUMENT or a
related grant. No DDSTART2 disk-region or boot-selection record appears.
Thus this capture does not exercise or refute the cartridge-first comparison.
Shared DDSTART1 identity alone cannot prove which comparison binary ran.

Remedy: in the debug app use Refresh ROMs -> Select Folder and reselect
the SD-card `ROMs/n64` folder using Android's system picker, granting access.
Leave “Clear gallery before adding” unchecked. The scanner requests a
persistable read grant via ACTION_OPEN_DOCUMENT_TREE. Do not uninstall,
clear app data, delete saves, or alter boot code to address this failure.
Regrant IPL/disk file access only if subsequent logs show those files denied.
The log does not establish why the prior cartridge grant became unavailable.
After restoring access, capture a fresh no-savestate launch of the same
comparison APK; native stall diagnosis and acceptance remain pending.

## 2026-09-12 — DDSTART2 retry loads media but produces black screen

User reports the retry “just sits at a black screen.” Evidence:
`attached_assets/ddstart2-retry-logcat_1789223335971.txt`, 3,067 lines,
SHA-256 `291787046dc954e5eefa4fadc7b91e7b1a7a64dc2f912ac5811faecce3d1143d`.
Raw upload remains private. This file contains **two distinct launches**:

| Record | Japanese cart attempt | US cart attempt |
|---|---|---|
| PID / engine startup | 30309 / 10:27:01.618 | 30725 / 10:28:15.046 |
| Cart identity | F-ZERO X (J), MD5 58D200D43620007314304F4E6C9E6528 | F-ZERO X (U), MD5 753437D0D8ADA1D12F3F9CF0F0A5171F |
| Disk | SDK, 64,931,840 bytes, JAPAN | Same reported format/size/region |
| Disk ID bytes | 45 46 5a 4a 00 00 (`EFZJ` prefix) | Same reported bytes |
| Boot | CART, combo_cart_boot=1, CIC X106 | CART, combo_cart_boot=1, CIC X106 |
| CPU / memory | Dynamic Recompiler / 8 MiB | Dynamic Recompiler / 8 MiB |
| Requested autoload | false | false |

Relevant line groups: 232–269 and 460–507; 2334–2363 and 2536–2609.
IPL URI names Japan in both attempts (179 and 2280), unlike the earlier
USA-prototype-named IPL. This identifies a selection change, not an IPL hash
verification. Do not compare this file as if only boot policy changed.
Both cartridge regions were tried against a Japanese disk; the first attempt
is the appropriate cart-region pairing for further native comparison.

The access problem is resolved for these launches: cart, disk and IPL all
load, and the DDSTART2 override actually runs. Missing `.ndr` messages are
the same original-disk fallback described above, not failed disk loading.
No native fatal signal, Java FATAL EXCEPTION or coverage-limit marker is
present. The emulator-finished messages at 1001 and 3038 follow UI shutdown
and save-completion records; they are not evidence of a spontaneous crash.
Observed execution windows are approximately 16 and 12 seconds.
Host startup and successful autosave do not establish guest progress/menu.
Current black-screen audio was not separately reported.

### Boot consistency audit and next control

A read-only audit checked device selection, PIF/CIC initialization, boot HLE,
IPL3 copy, CPU start address, hard reset and NMI. X106 selects cartridge in
the HLE path as well: there is no demonstrated cart-CIC/DD-IPL3 split.
Do not add an entry-PC or reset patch based on that rejected hypothesis.
Cartridge-first boot alone has **not** delivered native success.

Next useful existing-APK control: launch the same Japanese cart with its
per-game N64DD support disabled, same emulation profile/plugins, fresh
start with no savestate. Do not clear configured media or saves. GamePrefs
supplies empty DD paths while disabled and the combo override is reset to 0.
A normal-cart failure would need investigation before interpreting another
DD experiment. Normal-cart success would narrow the difference to the
DD-enabled path, without proving a specific faulty register or writer.
No new runtime code or APK was produced in this analysis pass.

## 2026-09-12 — Japanese cartridge DD-disabled control captured

Evidence: `attached_assets/fzero-j-dd-off-logcat_1789223591720.txt`,
SHA-256 `5d9cde410598931db617a5c1d5ec4991838bd5e2558a9bfeea323e43e0fafdef`.
PID 31360 starts around 10:32:38; capture ends around 10:32:51.
Lines 685–720 identify missing cached DD IPL, the same Japanese cartridge
MD5 `58D200D43620007314304F4E6C9E6528`, and CIC X106. No DDSTART markers
are present; ordinary VERBOSE/DEBUG core logging is restored.
Lines 1007–1076 show 8 MiB and Dynamic Recompiler initialization.
Pause/resume records at 1320–1368 end in “Emulation continued.”
No Java FATAL EXCEPTION, native fatal signal or frontend launch failure is
shown. These observations do not prove that a game screen rendered.

The plain-cart route emits “Failed to load DD Disk” for the WorkingPath
directory and a `.ndr` path without a disk name (714–715). This is the
existing optional-media directory probe, not a cartridge-open failure:
core startup continues without a loaded DD disk/IPL. Do not conflate it
with the earlier Android read-permission failure or patch ordinary-cart
behavior merely to silence this message.

User subsequently confirmed the Japanese cartridge reaches its title/menu
and “works perfectly fine,” in response to the audio/controls question.
This is a passing user-observed ordinary Japanese-cart control for this
installation/profile, not acceptance for every other cartridge or persistence.
The DD-enabled black screen remains unresolved. Do not repeat this control
without a change that invalidates it. Next evidence needs to locate guest
progress or the first divergent DD interaction, not repeat startup metadata.
Task remains incomplete.

The user also switched to Replit Desktop App expecting direct device access.
Official desktop documentation and a live workspace ADB check were consulted:
the agent still runs in a Linux cloud workspace and `adb devices -l` lists no
attached devices. Desktop login alone has not exposed the handheld here.
Continue using user-run local ADB captures unless actual connectivity changes;
do not claim direct device validation or expose ADB publicly as a workaround.

## 2026-09-12 — DDSTART3 bounded runtime-observation candidate

This is a diagnostic candidate, not task completion and not a behavioral fix.
It preserves DDSTART2 boot selection and metadata, the existing DDSTART1
callback behavior, and the DD-disabled baseline. No timing, IRQ delivery,
renderer, RSP, or save behavior was changed. No APK was built or installed by
this change.

### Exact observation coverage

- The existing Java `enable64DdSupport` per-game decision remains the native
  activation boundary through `M64P_DD_STARTUP_DIAGNOSTICS=1`. Every
  DDSTART3 call checks `DdStartupDiagnosticsEnabled()`; a DD struct pointer,
  direct-NDD classification, ROM name, or compiled DD support is not enough.
  Callback registration resets all DDSTART3 counters on startup and teardown
  resets the activation through the existing callback lifecycle.
- DD register writes (including command/status and buffer-manager writes) are
  recorded as early `DDSTART3 reg write` events, with address, register index,
  value, mask and command byte. The register-command class has a strict
  24-event budget. DD register responses record the returned value and status
  as `DDSTART3 reg read`; invalid accesses are also candidates for this class.
  Reads use the sparse threshold: the first eight candidate reads, then only
  power-of-two candidate counts. The read class has a separate 24-event
  budget. This is a response observation before the existing status-read
  acknowledgement/update, not a register-model change.
- DD-related PI DMA starts record exact direction, DRAM address, DD cart
  address and effective length. PI completion and PI-raise observations are
  only current-register/interrupt-boundary samples and explicitly carry
  `dma_identity=unknown`; they do not claim to complete or pair with an
  earlier start. There is no persistent DMA correlation slot or latched DD
  filter, so reset, queued-event, and savestate boundaries cannot
  misattribute a prior transfer. PI boundary observations are emitted only in
  a DD-enabled session. PI DMA-start observations have a strict 20-event
  budget and interrupt observations a strict 16-event budget. Unpaired PI
  completion boundaries have their own strict 20-event class and cannot
  consume the separate 20-event DD DMA-start class. DD CART interrupt
  assert/clear plus PI buffer acknowledgements remain separate observations;
  none alter PI scheduling or completion.
- Guest progress is sampled at the existing `gen_interrupt` emulation-thread
  boundary, including VI event dispatch and other interrupt events. Samples
  contain an ordinal, CP0 count/cause, event type and a `pc_sample`; the
  message explicitly labels `pc_is_exact_writer=false`. Progress sampling
  emits ordinal 1 and then only powers of four (4, 16, ... 4^11), with a
  strict 12-event progress budget. Thus an interrupt boundary at ordinal 128
  is intentionally not emitted, and the progress budget is not exhausted
  during the first few hundred boundaries. There is no per-instruction hook,
  dynarec writer probe, or claim that a sampled PC wrote DD state.

DDSTART3 has an independent aggregate callback budget of 628 records
(24+24+20+20+16+12+512), including the separate 512-record DDSTART4 BM
handshake class, so the existing 256-total `DebugMessage` DDSTART1 cap
cannot consume the later sparse observations. Formatting uses the existing
synchronous callback and a fixed stack buffer only: no allocation, trace
file, timer, polling loop, or new thread is introduced. Atomic reservations
keep each class and the aggregate budget bounded if a callback boundary is
re-entered. Callback delivery order across emulator/plugin threads remains
non-authoritative.

### Verification and limitations

`bash tools/test-dd-startup.sh` passes focused disabled-session, independent
DDSTART1/DDSTART3-budget, separate PI-boundary versus DD-DMA classes, bounded
DDSTART4 ordinal, sparse read/progress-threshold, callback reset, and
aggregate budget checks. Host `-fsyntax-only` checks pass for the changed PI,
interrupt, DD-controller and callback sources (the baseline's existing
unused-parameter warnings remain when `-Werror` is applied to standalone DD
and interrupt files). The APK marker helper now requires DDSTART3 register
read, PI DMA-start and progress strings in addition to the DDSTART1/DDSTART2
markers and DDSTART4 BM entry/ack strings; packaged-core verification and
device capture are still pending.

The records show emulator-side observations only. A missing record can mean a
budget threshold, callback suppression, an unexecuted path, or packaging
failure; it is not proof that hardware/guest activity did not occur. Sparse
PC values identify an interval/boundary, never an exact guest writer.
DDSTART3 therefore narrows command/response, PI/interrupt, and guest-progress
ordering but does not establish a root cause, native boot success, menu/audio
success, or a fix for the DD-enabled black screen. A fresh locally signed APK
with the verified packaged core and a matching DD-enabled device capture are
required next. The bounded callback still has finite host overhead; any
behavioral comparison must retain an uninstrumented control and must not
interpret a changed timing outcome as an emulator correction.

Final workspace verification: the callback/trace budget tests and diff
whitespace check passed. Android `:app:assembleDebug` passed after all review
corrections; all required DDSTART1/DDSTART2/DDSTART3 markers were verified in
the packaged arm64 core. APK SHA-256:
`96f9ea86e1ff7dbb5dbc808430aa3c3d6affb0a056ae4fc80f6ce142d3b20748`.
Review identified ambiguous PI completion association after reset or state
load; persistent DMA correlation was removed rather than changing scheduler
behavior. Progress sampling was extended to powers of four to avoid consuming
all twelve records within the first 128 interrupt boundaries.
Device results are pending. Install as an update to the existing debug app,
never uninstall the release or clear data; stop if signature verification
rejects the update. Use the same Japanese cart/disk/IPL, DD enabled, existing
profile and no autoload. Capture to `ddstart3-logcat.txt`.

## 2026-09-12 — DDSTART4 targeted BM handshake diagnostic

This pass preserves DD behavior and adds only an explicitly enabled,
bounded observation path. No timing, interrupt delivery, guest, renderer, or
save behavior was changed. Build verification is recorded below.

### DDSTART3 upload evidence and interpretation

The DDSTART3 device artifact is
`attached_assets/ddstart3-logcat_1789224996356.txt`, SHA-256
`e87c2e44706cb2d5a7f75c54b72de6b66db95946c0980be7c1cfc88772ee19c6`.
Exact marker counts in that file are: 24 register writes, 17 register reads,
20 PI boundaries, 0 `DDSTART3 PI DMA start` records, 16 interrupt records,
and 11 progress records (88 DDSTART3 records total). The 20 PI-boundary
records therefore exhausted the old shared 20-record PI class before a DD
DMA-start marker could be emitted; this is a coverage limit, not proof that
the guest issued no DD DMA.

The command writes include `09`, `1b`, and `01`, consistent with the observed
Japanese DD CART boot sequence, followed by BM register writes while IRQ
observations continue. The ordinary boundary cart registers beginning
`0x10...` (for example `0x10101004` and `0x10278d78`) are cartridge
addresses, not DD addresses: the source classifier accepts only
`0x05000000 <= address < 0x08000000`. Separately, the BM value `0x50000000`
is `MNGRMODE` (`0x40000000`) plus `RESET` (`0x10000000`), not `START`
(`0x80000000`). A `0xc0000000` value is the corresponding MNGRMODE+START
combination. These distinctions prevent ordinary cartridge PI traffic or a
BM reset write from being labeled as a DD DMA start.

### DDSTART4 coverage

PI completion boundaries now use their own DDSTART3 class and 20-record
budget; the actual DD DMA-start class retains its separate 20-record budget.
This prevents ordinary cartridge boundaries from starving a later
DD-address DMA-start observation without pairing a completion to a start.

The new `DDSTART4 BM` class has a strict 512-record budget and is emitted
only while `DdStartupDiagnosticsEnabled()` is true. It records
`ordinal`, phase/action, current sector, head/track, command/status, and BM
status at `dd_update_bm` entry and after each update, plus the post-side-effect
DS and C2 acknowledgements in `dd_on_pi_cart_addr_write`. The ordinal is a
monotonically increasing diagnostic observation index; it is not a fabricated
DMA identity. The budget is sized to observe a block's 85 data sectors,
four C2 sectors, and gap without adding polling read loops, but earlier
attempts, resets and retries consume this same session budget. It is not a
guarantee of a complete first valid block. The total independent trace bound
is now 628 records:
`24+24+20+20+16+12+512`.

The focused callback test verifies the separate PI classes, the 512-record
DDSTART4 bound and contiguous emitted ordinals, aggregate bounds, disabled
sessions, sparse thresholds, and callback reset. The packaged-core marker
helper now also requires `DDSTART4 BM entry:` and `DDSTART4 BM ack:`. Device
capture with this diagnostic remains pending; no result should be read as a
behavioral fix for the DD-enabled black screen.

Final checks: focused tests passed; Android debug build passed in 40 seconds;
packaged arm64 DDSTART4 entry/ack and earlier diagnostic markers verified.
APK SHA-256:
`f1a590be8e334d423d43ac921eec90bf19c44005887f32c8699d3cc30bdb7122`.
Read-only code review found no runtime implementation blocker; its coverage
wording correction is incorporated above and in the source comment. Raw
Android logs are excluded from selective GitHub publication.

## 2026-09-12 — DDSTART5 late guest-context snapshot candidate

This pass adds an explicitly gated, diagnostic-only late context observation.
It does not alter DDSTART4, the DD controller, interrupt scheduling, guest
state, memory handlers, or any device behavior. No APK was built, installed,
or published by this pass.

### Snapshot coverage and source reasoning

- `dd_trace_interrupt_progress` remains the only call site and still runs at
  the existing `gen_interrupt` emulation-thread boundary. It predicts the
  next DDSTART3 progress ordinal and captures only candidates `65536` and
  `1048576`; there is no per-instruction hook.
- The sampled PC uses the existing `r4300_pc` accessor after the same
  null-safe `r4300_pc_struct` check used by the progress observation. Source
  inspection of `device/r4300/r4300_core.c` confirms that NEW_DYNAREC selects
  `new_dynarec_hot_state.pcaddr` in dynarec mode and the current
  precompiled-instruction address in interpreter modes. GPRs use the existing
  `r4300_regs` accessor; its NEW_DYNAREC path is
  `new_dynarec_hot_state.regs[32]`. No direct platform-specific hot-state
  access was added.
- The snapshot copies all 32 GPRs into fixed stack storage before emission.
  Code storage is also fixed stack storage: 32 words, with slots `0..7`
  representing eight words before the sampled PC and slots `8..31`
  representing the PC and the following 24 words. Each virtual slot is
  independently checked for arithmetic overflow/underflow, KSEG0/KSEG1
  membership, and crossing the sampled segment. Physical access uses only
  `r4300->rdram->dram` after a real `dram_size` byte-bound check; it does not
  call generic memory handlers and therefore does not read MMIO or trigger
  read side effects.
- Unavailable PC/GPR state, misalignment, non-KSEG addresses, segment
  crossings, missing RDRAM, and RDRAM bounds failures are emitted as explicit
  state/status values. `pc_is_exact_writer=false` remains explicit: this is
  a boundary sample and cannot identify the instruction that wrote guest
  state.

DDSTART5 context records use a separate 24-record callback class, outside
the DDSTART3/DDSTART4 aggregate and per-kind budgets. A complete candidate
snapshot uses at most nine records (header, four GPR records, four code
records), so the two candidates fit within the class bound. The class
counter and remaining budget are reset by every `SetDebugCallback`, and the
host gate remains exactly `M64P_DD_STARTUP_DIAGNOSTICS=1`. The APK verifier
now requires the `DDSTART5 context:` marker. The focused host test adds
disabled-gate, aggregate-independent budget, and callback-reset assertions;
the owning agent should run it and perform the normal source/package checks.

### Retained DDSTART4 handoff findings

The supplied DDSTART4 evidence SHA-256 is
`a72ac07e9ae0b358ff5f5ffd475e4f36727cec6273ecd202645eeced9a408b`.
The reported block 1 at track 464 covered 85 data sectors plus four C2
sectors, followed by stop and acknowledgement. The 512-record cap reached
track 316 sector 41, so it is a coverage limit rather than a complete-block
claim. The track 6 special case is an intentional retail failure path, not
the root cause. Earlier `0x06` DMA evidence uses the IPL-to-RDRAM direction
write convention; it does not establish that the guest wrote the IPL.
These are retained observations only: no code-symbol guesses and no
guaranteed root-cause claim.

Files changed for this candidate:
`mupen64plus-core/upstream/src/api/callbacks.c`,
`mupen64plus-core/upstream/src/api/callbacks.h`,
`mupen64plus-core/upstream/src/device/r4300/interrupt.c`,
`mupen64plus-core/upstream/src/device/r4300/dd_fault_layout.h`,
`tools/tests/dd-startup-callbacks-test.c`,
`tools/tests/dd-startup-fault-layout-test.c`,
`tools/test-dd-startup.sh`, `tools/verify-dd-startup-apk.sh`, and this
handoff.

## 2026-09-12 — DDSTART6 signature-gated scheduler snapshot candidate

This pass extends the same late progress candidates (`65536` and `1048576`)
used by DDSTART5. It is diagnostic-only and explicitly gated by
`M64P_DD_STARTUP_DIAGNOSTICS=1`; DDSTART2 boot selection and all ordinary
DD behavior are unchanged. No APK was built, installed, or published by
this pass.

### Exact evidence and signature gate

- The retained DDSTART5 evidence is SHA-256
  `a4b042448d0b22dce51427a79de78bdd1999dda745fd45f12bd24054938a161e`.
  Both sampled `PC=0x800679f8` code windows were identical: previous slot
  3 is raw `0x0c031dc4` (`jal 0x800c7710`), slot 6 is raw
  `0x0c030a98` (`jal 0x800c2a60`), slot 8 is `0x1000ffff`, and slot 9 is
  `nop` (`0x00000000`).
- The public source is `/tmp/fzerox-reference`, revision
  `4fd50c7ca6b44f996aa0fbb68ec86df75855d5b8`. `src/sys/sys_main.c`
  `Idle_ThreadEntry` calls `osSetThreadPri(NULL, OS_PRIORITY_IDLE)` and then
  spins in `while (true) {}`; this identifies the late sampling site but is
  not executable control in the emulator.
- `/tmp/fzerox-reference/linker_scripts/jp/rev0/symbol_addrs.txt` maps
  `osSetThreadPri=0x800c2a60`, `osStartThread=0x800c7710`,
  `__osRunQueue=0x800d1d88`, and `__osRunningThread=0x800d1d90`.
  These identities are evidence only. The implementation first requires the
  exact sampled PC and all four raw fingerprint words above; on any mismatch
  it emits `mapped_roots=unavailable` and never reads those mapped roots.

### Snapshot safety and decoded layout

- The scheduler snapshot runs only at the existing `gen_interrupt` boundary
  and only at ordinals `65536` and `1048576`; there is no instruction hook.
  It reads `r4300->rdram->dram` directly after KSEG0/KSEG1, alignment, and
  `dram_size` checks. It does not invoke generic memory handlers, read MMIO,
  mutate RDRAM, or alter timing, interrupts, guest state, or DDSTART2.
- Root words and a contiguous nearby neighborhood are emitted as explicitly
  labeled `raw` words. No undocumented `__osActiveQueue` address is used.
- Thread records use explicit guest offsets matched to public
  `include/PR/os_thread.h` in the cited revision, not a host-ABI cast:
  `next=0x00`, `priority=0x04`, `queue=0x08`, `tlnext=0x0c`,
  `state=0x10` (high halfword), `id=0x14`, and context
  `savedSP=0xf0`, `savedRA=0x100`, `savedPC=0x11c`. Guest 64-bit values
  are assembled from direct high/low words. The `queue` pointer is only
  reported as a raw wait-queue head plus nearby raw words; no OSMesgQueue
  offset or queue semantics are inferred.
- The verified `runningThread` `tlnext` chain and `runQueue` `next` chain
  each stop at eight records. Aligned KSEG RDRAM pointers are required,
  global visited tracking detects shared nodes/cycles, and invalid pointers,
  read failures, and traversal limits are explicitly labeled. There are at
  most 64 DDSTART6 callback records per session in an independent budget;
  callback registration resets that budget and its counter. The budget is not
  a guarantee that either late candidate has a complete chain.

The callback test now covers DDSTART6 disabled gating, the independent
64-record bound, and callback-reset behavior. The packaged-core marker
verifier requires `DDSTART6 scheduler:` in addition to all earlier markers.
Raw device logs remain excluded; no device result or root-cause claim is made
by this candidate.

Files changed for this candidate:
`mupen64plus-core/upstream/src/api/callbacks.c`,
`mupen64plus-core/upstream/src/api/callbacks.h`,
`mupen64plus-core/upstream/src/device/r4300/interrupt.c`,
`mupen64plus-core/upstream/src/device/r4300/dd_fault_layout.h`,
`tools/tests/dd-startup-callbacks-test.c`,
`tools/tests/dd-startup-fault-layout-test.c`,
`tools/test-dd-startup.sh`, `tools/verify-dd-startup-apk.sh`, and this
handoff.

## DDSTART6 build verification

Focused callback gate/budget/reset tests passed; Android debug build passed
in 43 seconds; arm64 scheduler marker and prior diagnostic markers verified
in the packaged core. Focused layout/bounds review passed. APK SHA-256:
`ae664fd68582a5e9759f9aea483811dcea8a70a36e92febf4196da9e6ac130cb`.
No runtime outcome is claimed. Use the same Japanese native combination,
DD enabled, no autoload, unchanged profile, and record at least 30 seconds to
`ddstart6-logcat.txt`. Update the debug installation without clearing data.

## Test publication requirement

DDSTART6 final verification before publication: callback/budget/reset tests
passed locally; no Android debug build or package verification was performed
by this pass. The owning agent must perform the normal source/package checks
and verify the packaged arm64 scheduler marker before publication. The prior
DDSTART5 APK SHA-256 was:
`986a59a11895f0c3f4e42cfb96a6f18a20015f746111a4a0154b2dfa2d95010d`.
This is diagnostic-only and device results remain pending. Update the existing
debug app, preserve saves, use the same Japanese native combination with DD
enabled and no autoload, then capture at least 30 seconds to
`ddstart6-logcat.txt`. Expected output is at most two signature-gated
late-candidate snapshots within the 64-record session budget, not a
guarantee of a particular guest PC, root mapping, complete chain, or
successful boot.

For every test iteration, update this handoff **before** creating and pushing
the test commit to the current debug branch, `dd-eos-watchdog-checkpoint`.
The user uses that branch as input to a local LLM. Each handoff entry should
include the test's purpose, changes, device evidence, verification results,
coverage limits and next capture instructions. Include the published commit
ID with the APK delivery. Publish focused changes without force or private
raw logs/signing material. This requirement also appears in `replit.md`.

The DDSTART4 test source and handoff were already published together in
`379af8175ad20a3c1174ef94a6930c640e53ead2`; this documentation update
records the standing requirement and does not require another APK.

## 2026-09-12 — working Phobos/Ares reference comparison

User reports their Ares port boots and plays these games and recalls fixing
initial date/time seeding. This is user-observed reference success, not a
new device validation here. Archive `phobos-master_1789226371619.zip`:
SHA-256 `676f4efdf20a0a19565e0a731c04bd31e4e2cac462d759bf086ca439741fa979`.
Selected sources were extracted into an isolated temporary directory after
checking archive paths/symlinks. Neither archive nor reference tree is
published; only this comparison is shared with the user's local LLM.

### Remembered RTC fix located

Reference `ares/n64/dd/rtc.cpp` loads a 16-byte `time.rtc`. If the first
eight bytes are all FF **or all zero**, `seedCurrentTime()` fills BCD
year/month/day/hour/minute/second from host local time and records an epoch
timestamp for elapsed-time updates. Existing valid RTC data is advanced,
not overwritten. `android/app/src/main/cpp/PhobosRunner.cpp` creates the
system-pak `time.rtc` node and handles root-pak flushing so saves have a
durable target. These concrete fixes match the user's recollection.

Current `device/dd/dd_controller.c` uses a different design. Power-on resets
`now` and `last_update_rtc` to zero, but before command processing,
`update_rtc()` adds `clock_now - last_update_rtc`: the first command therefore
makes `now` equal the backend's current host time, not Unix epoch.
`backends/clock_ctime_plus_delta.c` supplies `time(NULL)` plus a configured
delta. Commands 12/13/14 return BCD date/time pairs. Both cores effectively
use the low two decimal year digits; no year-conversion defect is established.

Current RTC **write commands 0F/10/11 and persistent guest-set DD time are
not implemented**. This is a fidelity gap, not proof of the black-screen
cause. Early captured commands are 09/1B/01; trace-budget exhaustion means
absence of later RTC records cannot prove no RTC accesses occurred.
Do not transplant file-seeding logic into a core with no equivalent RTC file.

### Other concrete differences

- Reference `ares/n64/dd/io.cpp` acknowledges BM IRQ on ASIC status reads
  and schedules another BM request with a delay. Its hardware-testing comment
  is reference evidence, not hardware validation performed in this project.
- Current controller acknowledges exact DS/C2 PI-address writes; status-read
  acknowledgement/advancement is limited to the sector gap.
  `device/rcp/pi/pi_controller.c` advances BM on PI completion before raising
  MI PI; reference `ares/n64/pi/dma.cpp` does not advance BM at that point.
- Ares maps full DD buffer ranges, whereas current DMA/ack paths include
  exact DS/C2 base-address tests. Offset accesses could behave differently.
- PI length normalization, timing and disk-format mapping differ. They are
  not interchangeable implementations; copying timings or large code paths
  would not constitute an evidence-based correction.

The complete track-464 block/C2 acknowledgement remains counterevidence to
“all disk handshakes fail.” The existing DDSTART6 scheduler capture remains
the next relevant test: identify blocked/stopped threads and correlate their
wait with these differences. No new runtime code/APK was made for this
reference comparison. Root cause and acceptance remain unresolved.

## DDSTART6 device evidence — main-thread fault pointer

Input SHA-256:
`afe407991a5170946fb747238088e23020ed14e04527caa005780812c2c33527`.
Both samples (65536 and 1048576, 11:38:41.313 and 11:38:51.902)
match on scheduler payload; effective engine is Dynamic Recompiler,
explicit DD support is enabled, and autoload was not requested.

The JP/rev0 reference map at revision
`4fd50c7ca6b44f996aa0fbb68ec86df75855d5b8` names
`__osFaultedThread = 0x800D1D94`. DDSTART6 raw root base is
`0x800d1d80`; **raw5 is the word at base + 0x14**, i.e. that mapped
fault-pointer slot. Its value is `0x800dc1d0` in both snapshots.
The corresponding real thread has:

| Field | Value |
| --- | --- |
| ID / priority | 3 / 99 |
| State | 1 (stopped) |
| Saved PC | `800ad4ac` |
| Saved SP | `ffffffff800d4203` |
| Saved RA | `ffffffff800bb6b0` |

Reference `include/fzx_thread.h` explicitly identifies ID 3 as MAIN and
ID 1 as IDLE; `src/sys/sys_main.c` creates those threads accordingly.
Thus the **mapped guest fault pointer names the stopped main thread**,
while the idle thread runs and the run queue points to its sentinel.
This is substantially more specific than "all useful threads are waiting."
The saved SP is unaligned, but this alone does not establish the exception
type, the faulting instruction, or the writer of that value. The sparse
symbol map does not safely identify the saved-PC/RA functions.

### Diagnostic limitation found in this capture

`800d1d80` is the eight-byte fake queue-tail object, not a full OSThread.
DDSTART6 erroneously decodes it as a thread and follows its supposed
tlnext, which is actually the adjacent active-list pointer. Its nonsensical
ID/state/context fields are a **diagnostic interpretation defect, not
guest corruption**. The subsequently visited real objects can still be
read individually; do not call the displayed walk a valid complete thread
list. It is also truncated at eight objects.

Next diagnostic should terminate at the sentinel, explicitly identify the
mapped fault-pointer slot, and capture that thread's saved flags,
Cause/BadVAddr/Status and instruction words around saved PC, with bounded
read-only checks. Saved context is necessary: live CP0 state sampled while
idle is not necessarily the earlier main-thread exception. Until then,
do not diagnose RTC, a missed DD interrupt, or an address-error exception
as the established cause, and do not patch the saved SP or idle loop.
No runtime correction or new APK accompanies this analysis.

## 2026-09-12 — DDSTART7 selected-thread exception-context candidate

This pass adds DDSTART7, a read-only diagnostic observation at the same
signature-gated progress candidates (`65536` and `1048576`) used by DDSTART5
and DDSTART6. It is enabled only by the existing
`M64P_DD_STARTUP_DIAGNOSTICS=1` gate. No DD controller, scheduler, interrupt,
guest memory, context, timing, or hook behavior is changed, and prior logging
classes remain in place.

### DDSTART6 sentinel/list correction

The JP/rev0 reference is `/tmp/fzerox-reference`, revision
`4fd50c7ca6b44f996aa0fbb68ec86df75855d5b8`. The adjacent thread globals are
signature-gated at the existing map:

- `__osRunQueue = 0x800d1d88`
- active-list head (`thread.c` adjacent global) = `0x800d1d8c`
- `__osRunningThread = 0x800d1d90`
- `__osFaultedThread = 0x800d1d94`

`0x800d1d80` is only an eight-byte queue-tail object (`next`, `priority`).
DDSTART6 now reads those two raw words and stops with an explicit sentinel
record; it never decodes that address as an OSThread or follows its apparent
`tlnext`. The running-thread `tlnext` walk therefore terminates at the
sentinel. After the same exact sampled idle PC and four-word code fingerprint
match, a separate active-list `tlnext` walk starts from `0x800d1d8c` and is
bounded at eight real thread objects. The active walk has independent visited
state and does not expand the list limit.

### DDSTART7 selected fault thread

After the same exact gate (`PC=0x800679f8`, raw words
`0x0c031dc4`, `0x0c030a98`, `0x1000ffff`, `0x00000000`), DDSTART7 reads the
fault-selected pointer from `0x800d1d94` independently of either DDSTART6
list limit. A null, invalid, unreadable, or signature-mismatched selection is
reported as `structured=unavailable`; no thread fields are decoded in those
cases. A valid selection is decoded using explicit guest offsets from the
public `include/PR/os_thread.h`, not a host struct cast:

| Field | Guest offset |
| --- | --- |
| next / priority / queue / tlnext | `0x00 / 0x04 / 0x08 / 0x0c` |
| state / flags / id | state+flags word `0x10`; id `0x14` |
| fault-context prefix / saved integer context start | `0x128 / 0x20` |
| saved SP / RA | `0xf0 / 0x100` |
| saved LO / HI | `0x108 / 0x110` |
| saved SR / PC / Cause / BadVAddr | `0x118 / 0x11c / 0x120 / 0x124` |

The snapshot emits state and flags, all 29 saved fault-context integer
register slots (in guest order `at,v0,v1,a0-a3,t0-t7,s0-s7,t8,t9,gp,sp,s8,ra`;
the output deliberately labels them `slot00` through `slot28`), explicit saved
SP/RA and LO/HI, and the saved SR/Cause/BadVAddr/PC.
`saved_cause_source=thread_context` is emitted deliberately: live CP0 Cause
at the late idle boundary is not substituted for the selected thread's saved
exception Cause. The saved-PC code window starts eight words before the saved
PC, then contains 24 words starting at PC (32 words total). Each word
independently requires alignment,
KSEG0/KSEG1 membership, the same segment as the saved PC, and a real RDRAM
`dram_size` bound. Reads use the direct RDRAM array only; generic memory
handlers and MMIO are never touched.

DDSTART7 has an independent 18-record callback budget. A complete candidate
uses nine records (one header, four register records, four code records), so
the two candidates have the exact worst-case bound `2 × 9 = 18`. Invalid
pointer or missing-signature candidates consume one structured-unavailable
record. The budget and ordinal reset on every callback registration and are
disabled with the existing host gate. No outcome or root-cause claim follows
from this diagnostic.

### Capture instructions and limits

The owning agent should perform the normal source and packaged-marker checks,
then update the existing debug installation without clearing data. Use the
same Japanese native combination, DD enabled, no autoload, and unchanged
profile. Capture at least **45 seconds** to `ddstart7-logcat.txt` so both
late candidates can be observed. Verify `DDSTART7 fault:` in the packaged
arm64 native core before requesting a device run. Device outcome remains
pending; this is not an emulation correction.

Source review rejected an initial incorrect saved-register layout before
delivery. The corrected layout includes `at` and uses saved SR/Cause/BadVAddr
at `0x118/0x120/0x124`. Host fixtures now test the **production** RDRAM
reader with independent numeric offsets, known high/low word halves,
KSEG0/KSEG1 aliases, exact-end acceptance, and invalid-range rejection.
Both focused test suites and the final layout/bounds review passed.
No rejected-layout APK was delivered.

Files changed for this candidate:
`mupen64plus-core/upstream/src/api/callbacks.c`,
`mupen64plus-core/upstream/src/api/callbacks.h`,
`mupen64plus-core/upstream/src/device/r4300/interrupt.c`,
`mupen64plus-core/upstream/src/device/r4300/dd_fault_layout.h`,
`tools/test-dd-startup.sh`,
`tools/tests/dd-startup-callbacks-test.c`,
`tools/tests/dd-startup-fault-layout-test.c`,
`tools/verify-dd-startup-apk.sh`, and this handoff.

### Final pre-delivery verification

- Android debug build passed in 1m21s (all configured ABIs). The renewed
  environment needed the Gradle distribution/dependency cache restored
  online; the earlier download failure produced no new APK.
- Packaged arm64 native core contains DDSTART7 and all previous diagnostic
  markers. The focused callback and shared production-reader tests passed;
  final code review passed after the ABI corrections described above.
- The existing debug key was selected locally without changing project
  signing configuration or publishing signing material. APK certificate
  SHA-256 matches DDSTART6 exactly:
  `311f4e35e939256ae8df53ecbf15c43235d1ee97f8a0138d332839c5e53c2bfc`.
  Update with `adb install -r`; stop on any installation/signature error,
  rather than uninstalling or clearing data.
- Delivered file: `build-downloads/DDSTART7-debug.apk`, SHA-256
  `fb03906cb84de98a4362b71f246e2f65afeec82075146ae75bfc34337ad08f9f`.
- No connected Android device or browser-runnable version exists here.
  The requested native capture, cause identification, exact corrupting
  writer/reuse attribution, correction and acceptance remain unresolved.

## DDSTART7 device evidence — saved TLB-load fault and boot-code lead

Input SHA-256:
`3ecca4932d7363b037638b0b04784b92914cd75cdd8a6424c8675a6d57df0198`.
One native launch reports explicit DD support, cart-plus-disk mode, no
autoload request, and effective **Dynamic Recompiler**. Both complete
nine-record fault snapshots are present at ordinals 65536 and 1048576
(16:48:01.007 and 16:48:11.590). All saved fields, 29 register slots, and
32 code words are identical. The corrected active-list walk reaches its
sentinel after eight real threads; no fake sentinel context is decoded.

### Direct observations and instruction decoding

| Field | Value / interpretation |
| --- | --- |
| Fault-selected thread | `800dc1d0`, ID 3 (MAIN), priority 99 |
| State / flags | `0001` stopped / `0002` fault flag |
| Saved Cause | `00000008`: ExcCode `(cause >> 2) & 31 = 2`, TLB load exception |
| Branch-delay bit | 0 |
| Saved PC | `800ad4ac` |
| Saved BadVAddr | `079bb080` |
| Saved a0 (slot03) | `00000000079bb080` |
| Saved SP / RA | `ffffffff800d4203` / `ffffffff800bb6b0` |
| RDRAM word at saved PC | `8c830000` = `lw v1, 0(a0)` |

The decoded load's effective address equals saved BadVAddr exactly and is
word-aligned. This is **not an AdEL/alignment exception** merely because the
saved SP is unaligned. The evidence is an internally consistent saved
TLB-load fault, not an exception-entry trace. It does not prove when those
saved fields were written, which translated instruction executed, or why
the address lacked a usable translation. Continuing interrupt-sampler
progress does not mean that the main thread resumed or faulted repeatedly.

### Reference mapping, with explicit address arithmetic

The checked reference revision remains
`4fd50c7ca6b44f996aa0fbb68ec86df75855d5b8`. Its JP/rev0 main YAML sets
ROM start `1060` and VRAM start `80067060`, so the mapping bias is
**`80066000`**. Subsegment classifications (not exact function symbols):

| Address | ROM offset | Reference subsegment |
| --- | --- | --- |
| `800ad4ac` saved PC | `474ac` | `audio/rom/lib/seqplayer`, VRAM `800ac050..800aea90` |
| `800ac9c0` nearby JAL target | `469c0` | same seqplayer subsegment |
| `800bb6b0` saved RA | `556b0` | `leo/leo_bootdisk`, VRAM `800bb540..800bb9a0` |

These mappings describe the reference cartridge layout, not proof of which
overlay or translated block was executing. Exact function attribution is
not established.

`src/leo/leo_bootdisk.c::LeoBootGame` descrambles boot functions in place
using an address-derived byte key, then calls D-cache writeback and I-cache
invalidation before entering the next boot stage. A further correlation:
saved a3 is `800bb67c`, within the boot region, and the low-byte sum of that
address's bytes is `bd`, matching saved v1 and t9. This is a boot-path lead,
not proof that a3 is a particular function symbol or that descrambling failed.

Current new_dynarec treats guest CACHE as NOP; its guest-code coherence
instead relies on write/block invalidation. That fact alone is not a bug
diagnosis. DD DMA-to-RDRAM already explicitly invalidates both KSEG aliases,
as cartridge ROM DMA does. No missing DD DMA invalidation call was found.
Do not patch CACHE or change DMA timing on this evidence alone.

### Next controlled comparison — same APK, cached interpreter

Use DDSTART7 unchanged. Copy the current emulation profile, change only
**R4300 emulator → Cached interpreter**, and assign that copy only to the
DD-enabled Japanese F-Zero test entry. Preserve the original profile,
count-per-op settings, cart/disk/IPL, no-autoload setting, saves and other
games. Capture a fresh launch for **90 seconds** (slower mode) to
`ddstart7-cached-logcat.txt`; report screen/audio behavior. Verify the actual
native engine marker in the uploaded log, not just the chosen UI value.

This comparison tests whether the failure depends on the execution engine.
Success would narrow investigation toward engine-specific execution or
timing, not prove a particular cache fix. Failure could still differ in
cause/PC. An incomplete late snapshot in the slower mode is not evidence
that no fault occurred. These bounded snapshots do **not** constitute the
full cached-interpreter dispatcher/writer coverage required for final
attribution.

If exact tracing is needed next, record the live exception-entry state and
the boot-region instruction changes/invalidation around the observed
return-address region. A later RDRAM code window is not a transcript of
executed translated code. No emulation correction or new APK was produced
for this analysis. Native success and final acceptance remain pending.

## DDSTART7 cached-interpreter result — menu corruption and gameplay black screen

Input log SHA-256:
`56d26b16a08bc13111fbaf129d1029ac54bc1cb7265e07089ae34a4b1e68691e`.
The 12,997-line capture contains **three native launches**, not one
continuous 90-second game session:

| Launch / native engine timestamp | Effective engine | Last emitted progress sample |
| --- | --- | --- |
| 16:58:39.101 / 16:58:39.362 | Cached Interpreter | ordinal 16384 at 16:58:40.626 |
| 16:59:19.028 / 16:59:19.252 | Cached Interpreter | ordinal 16384 at 16:59:20.499 |
| 17:00:22.706 / 17:00:22.952 | Cached Interpreter | ordinal 16384 at 17:00:24.184 |

Each launch reports explicit DD support, cart-plus-disk (not direct NDD),
IPL/disk configured, no requested autoload, count-per-op 1 and denominator
0, Japan disk, and cartridge-first combo boot. These logged settings agree
with the preceding dynarec capture except for the execution engine. This
is not a complete comparison of every profile/renderer setting.

### Observed behavior and trace coverage

The user reports the same cached-interpreter symptoms seen in earlier local
work: title/menu loads, text is garbled, and starting either a race or attract
gameplay produces a black screen. Supplied screenshots show the Expansion
Kit title with N64DD branding, noisy text over recognizable menu art/course
graphics, and a black screen. Audio was **not reported** for this run.
This is partial menu boot, not successful native gameplay or acceptance.

All three sessions have exactly 512 DDSTART4 records, matching the independent
BM budget. Each includes track-464 C2 processing and acknowledgement, then
track-316 activity before the cap. Ending at BM ordinal 512 is **not** a
stalled disk transaction. There are **zero DDSTART5, DDSTART6, or DDSTART7
records** in this file. The late probes require interrupt candidates 65536
and 1048576; neither appears. Their absence does not establish that the
gameplay black screen is fault-free or shares the dynarec exception.

All three last emitted progress samples have `pc_sample=8012b844`. They are
startup interrupt-boundary observations roughly one second after engine
start, not exact writers or gameplay-failure PCs. No full cached-interpreter
dispatcher pass was captured. Do not treat this file's final core log line
as the point at which emulation stopped.

### IPL/font-data review and conditional format discrepancy

The trace records 128-byte IPL-to-RDRAM transfers such as
`060a4900 -> 00400008`. The public LEO header defines DDROM_FONT_START as
`000a0000`. This makes IPL font data relevant to the corrupt text, but the
log contains neither the transferred payload nor evidence that this
particular transfer produced a displayed corrupt glyph.

Current DD DMA uses the same `^ S8` byte-lane copying as cartridge ROM DMA
and explicitly invalidates both KSEG code aliases. MMIO's word index
`(address & 3fffff) >> 2` and DMA's byte index agree for these addresses.
`direction=write` in this core means **DD/cart to RDRAM**, not the reverse.
No missing normal DD DMA invalidation caller was found.

Separately, `main.c::load_dd_rom` handles the raw V64 header `27 80 40 07`
by swapping each 16-bit pair only. On a little-endian host, that leaves
`80 27 07 40`, whereas the normalized word backing needs `40 07 27 80`.
A host check using the unchanged byte-swap function definitions extracted
from production `util.c`, with their production header, produced:

| Raw input format | Host word after the corresponding loader helper call |
| --- | --- |
| Z64 | `80270740` — expected |
| N64 | `80270740` — expected |
| V64 | `40072780` — differs |

This verifies the helper-level format discrepancy, not a device cause or
an end-to-end loader correction. No runtime change was made. In particular,
the log does **not** establish V64 input. Android copies/extracts the chosen
IPL to the fixed cache name `dd_rom.n64`; that suffix is not format evidence.
The logged MD5 beginning `58D200D4` belongs to the **cartridge**, not the IPL.

### Immediate next check — cached IPL metadata only

Obtain size, first four raw bytes, and SHA-256 of the cache file actually
loaded, without exporting the IPL or changing saves:

```sh
~/Downloads/platform-tools/adb shell run-as org.mupen64plusae.turnip.pwnedbygary.debug toybox stat -c %s cache/WorkingPath/dd_rom.n64 > "$HOME/Desktop/ddstart7-ipl-info.txt"
~/Downloads/platform-tools/adb shell run-as org.mupen64plusae.turnip.pwnedbygary.debug toybox od -An -tx1 -N4 cache/WorkingPath/dd_rom.n64 >> "$HOME/Desktop/ddstart7-ipl-info.txt"
~/Downloads/platform-tools/adb shell run-as org.mupen64plusae.turnip.pwnedbygary.debug toybox sha256sum cache/WorkingPath/dd_rom.n64 >> "$HOME/Desktop/ddstart7-ipl-info.txt"
```

If a command fails, retain the error; do not clear data, uninstall, or change
inputs to bypass it. These commands have not run here because no Android
device is connected. Request the small metadata text file, not the full IPL.
Do not apply the V64 correction to this investigation unless input evidence
makes it relevant. If input normalization is excluded, the next diagnostic
must cover the actual post-menu transition and live exception paths rather
than repeating the unchanged startup-only capture. Preserve the separate
dynarec boot-fault investigation; a common cause has not been established.

## Cached IPL metadata result — V64 path excluded for this input

The user supplied the metadata of the cached file actually loaded:

- Size: **4,194,304 bytes**.
- Raw first four bytes: **`80 27 07 40`** (Z64/big-endian).
- SHA-256:
  **`806400ec0df94b0755de6c5b8249d6b6a9866124c5ddbdac198bde22499bfb8b`**.

This input selects the Z64 normalization branch, **not the V64 branch**.
The V64 helper discrepancy cannot explain this run via that branch; do not
apply a V64 correction as a proposed fix for this capture. The header and
size are not a full known-good validation of all IPL/font bytes. No firmware
was uploaded or downloaded for this metadata check.

The user clarified that **dynarec must work for speed**; cached interpreter
is not an acceptable final workaround. Working correctly under both engines
remains the goal, with dynarec required and cached interpreter secondary.
Next diagnostic work returns to the dynarec boot exception and boot-code
transformations, with explicit verification of ARM64 data-TLB fast-path
coverage before describing a capture as live exception evidence.

## DDSTART8 — ARM64 post-spill fault and bounded boot-code evidence

This is a **diagnostic APK, not a correction**. It leaves the Z64 IPL loader,
guest CACHE/TLB behavior, disk timing, and existing invalidation semantics
unchanged. All new probes require explicit per-game DD activation; the
generated instrumentation and core-local state are ARM64-new-dynarec-only.
Other engines receive no DDSTART8 execution hooks.

### Live load-fault hook

The observed dynamic `lw` path reaches `do_readstub`, through its C read
helper, address translation and common TLB exception handler. Common C
exception entry is too early to trust the guest GPR array: dirty registers
may still be in ARM64 host registers.

DDSTART8 instead emits an observer after the existing host restore,
constant materialization, and `wb_dirtys`, on the pending-exception branch
before `do_interrupt`. Its four compilation immediates identify guest PC,
instruction word, block start and compilation generation. The observer is
registered in the ARM64 trampoline table for out-of-range C calls; allocated
caller-save registers are preserved around the call.

The observer accepts TLBL from KSEG0 game code targeting low/TLB addresses,
reserving full snapshots for the known PC `800ad4ac`, derived fault PC
`800ad4ac`, or BadVAddr `079bb080`. At most two other eligible faults get
filter-only records. Each full snapshot has **15 records**:

- Final CP0 EPC/Cause/BadVAddr/Status/EntryHi/Context, load address,
  compiled PC/block/generation, explicit BD and `pc_agreement`.
- Compiled instruction versus the current, directly read RDRAM word.
- All 32 spilled 64-bit GPRs and LO/HI.
- Five-word direct-RDRAM windows at compiled PC, `800bb648`, the current
  spilled RA, and candidate entry `800bb67c`. Each window carries its
  snapshot number. The RA window includes RA-8, the usual JAL location.

Two full snapshots plus two filter records exactly fit the independent
**32-record fault budget**. No interrupt-ordinal trigger is required.
EPC+BD-derived PC must agree with the compile-time PC before using that
agreement as exception-PC evidence; nested exceptions may retain an older
EPC. `current=unavailable` is not evidence of an instruction mismatch.

### Boot-code and exact slow-path byte-store evidence

The independent **64-record coherence budget** is divided into local caps:
16 compilation, 24 C dirty-verification, 12 invalidation and 12 byte-store
records. Only the relevant boot region/page is observed. Compilation emits
generation/block plus copied/current hashes. Dirty verification reports
the actual `memcmp` result. Invalidation records do not invent a writer PC.

Byte-store observations wrap the existing masked RDRAM write in
`write_byte_new`, using validated direct reads before and after. For
KSEG0/KSEG1 targets in physical `[000bb540,000bb9a4)`, the record contains
the target, byte, before/after aligned word, pending-exception flag, and
writer PC derived from the **generated call argument**:
`writer_pc=(pcarg & ~1)-4`, with the low bit identifying the delay slot.
Both generated fallback call sites use that argument convention.

For each emitted successful store, verify `pending_exception=0` and:
`after = (before & ~(ff << shift)) | (byte << shift)`,
where `shift=((address & 3) ^ 3)*8`.

This is exact evidence for emitted **ARM64 `write_byte_new` slow-path
stores**, including the C fallback called by `inline_writestub`. It is
**not** coverage of direct inline generated writes, other store widths, or
DMA. There is no generated block-entry hook, so compilation alone is not
proof that a particular generation executed. Hash equality alone is not
byte-for-byte proof; the separate dirty-verification result is a `memcmp`.
Constant-address load fallbacks, non-ARM64 engines and the cached-interpreter
gameplay stall remain outside the new live-fault hook's coverage.
Coherence records are not a complete trace of every validation/reuse path;
KSEG1-only invalidation events may also be absent.

Callback budgets reset on callback registration; core-local caps and
compilation generation reset at dynarec initialization. DD-disabled games
emit no new records and perform no diagnostic RDRAM reads. Some cold C paths
have an additional disabled-gate check; no performance improvement is claimed.

### Verification and delivered artifact

- All focused host tests passed: callback gates/caps/reset, the existing
  production guest-word/ABI readers, and new fixtures including the actual
  production dynarec C observers. They cover complete/filtered snapshots,
  64-bit register values, BD/PC interpretation, invalid RAM/RA bounds,
  instruction comparison, and independent per-stage limits.
- The observer fixtures do **not** execute generated ARM64 instructions.
  Source review checked the generated ABI, trampoline, observed dynamic-load
  path, and byte-store PC provenance. Native execution remains pending.
- All-ABI Android debug assembly passed in **39 seconds**.
- Packaged arm64 core contains DDSTART1–7 and both DDSTART8 markers.
- APK: `build-downloads/DDSTART8-debug.apk`.
- APK SHA-256:
  `5a283dfa2839d4d8e5293677793def5dec97e764e04141ec7b8e5b45bdc87255`.
- Package remains `org.mupen64plusae.turnip.pwnedbygary.debug`; the upstream
  displayed version is still `3.0.335 (beta)`. Identify this iteration by
  APK hash and native DDSTART8 markers, not the displayed version.
- Signing certificate SHA-256 remains
  `311f4e35e939256ae8df53ecbf15c43235d1ee97f8a0138d332839c5e53c2bfc`,
  matching DDSTART7. No signing material or raw inputs are published.

### Next device capture

Update the existing debug installation with `adb install -r`; stop on any
installation/signature error, never uninstall or clear data. Restore the
**original dynarec profile** for the Japanese DD test entry, leaving the
cached-profile copy and other games unchanged. Keep the same cart/disk/IPL,
explicit DD enablement, count-per-op 1, and no save-state autoload.

Capture one fresh launch for **45 seconds** to `ddstart8-logcat.txt`, then
stop logcat and supply the file. Verify the effective Dynamic Recompiler
marker, DDSTART8 fault/byte-store records, PC agreement and mask equations;
retain the old DDSTART7 snapshots as a separate comparison. If DDSTART8
records do not appear, establish which hook/filter was reached before
assuming the fault disappeared. Root-cause attribution, correction,
dynarec gameplay/audio acceptance and save regressions are still pending.

## DDSTART8 native result and DDSTART9 candidate correction — 2026-09-12

The new upload is `ddstart8-logcat_1789249640409.txt`, SHA-256
`407c7bf0832d2de22af9958c2de6493345d0c0684f9131481ac83f655b95c9f6`
(2,153 lines / 294,232 bytes). Full bounded analysis and arithmetic are in
[DDSTART8_RUNTIME_ANALYSIS.md](DDSTART8_RUNTIME_ANALYSIS.md). Raw device logs
are not published.

### Native findings

- One arm64 **Dynamic Recompiler** launch, explicit DD support, the same
  cart/disk/IPL configuration, cartridge combo boot and no requested autoload.
- Launch again reports count-per-op 1 / denominator 0. These were already
  present in the preceding captures. **Do not manually change timing settings
  for the next comparison**; preserve the original dynarec profile.
- A complete 15-record live fault confirms PC/EPC `800ad4ac`, BD 0, TLBL,
  BadVAddr/a0 `079bb080`, SP `800d4203`, RA `800bb6b0`. Both the compiled and
  current instruction are `8c830000`, `lw v1,0(a0)`.
- The stale caller is identified by the game's reversible BootGame2 decode
  with key `bd`: current JAL `0c02f8e4` reverses to `0c02b527`, which calls
  **`800ad49c`**, exactly the actual faulting block. Current delay slot
  `2405013c` reverses to `2405be7f`, exactly matching live `a1=...be7f`.
  Observed RA is the return address of that same call.
- Compilation generation 175 spans 307 words from `800bb540` to exclusive
  end `800bba0c`, including the still-encoded next function. Its initial
  copied/current hash is `bd23a6d4`.
- Invalidation occurs before the fault, but the 24 C dirty verifications
  occur afterward. All reject the old copy against current hash `62e2e755`.
  The active internal call did not pass through that validation.
- There are zero slow-path byte-store records. Do not fabricate an exact
  writer from the invalidation event. The diagnosis uses reconstructed caller
  instructions with independent live target/argument/RA corroboration.

This establishes stale encoded boot-code execution as the cause of this
dynarec fault. It does not establish native success after a correction, fix
the separate cached-interpreter gameplay failure, or prove audio/save behavior.

### DDSTART9 correction

Pass 1 previously recognized a non-linking unconditional transfer and its
delay slot as a block end, then reopened that boundary if an earlier branch
targeted nearby following code. This pulled encoded BootGame2 into the
currently executing translation and allowed a direct internal call after
the guest rewrote it.

The candidate retains that boundary under the explicit DD-support flag
provided by the selected game's launch settings. DD-disabled sessions keep
the original forward-target scan. This is an early block-formation change:
the existing allocator, dirty-register writeback, external resolver and
linker consistently treat the excluded callee as external. It is not a
late branch-patching workaround.

No guest addresses, title checks, WritableROM conditions, timing/count
changes, CACHE/TLB changes, register repairs or interpreter fallback were
introduced. The policy applies to the new dynarec; cached interpreter is
unchanged. A gated `DDSTART9 boundary:` initialization marker identifies it.

Expected native evidence: the first boot block is **79 words**, ending at
`800bb67c`, followed by a current/decoded compilation of that callee instead
of the old call into `800ad49c`. This remains a device-test criterion.

### Verification and artifact

- Focused callback, production observer/reader and boundary-policy tests
  pass. New tests check DD off/on/off, all three historical forward-target
  offsets, external classification, and an actual ARM64 register-store
  instruction emitted by the production writeback code.
- Review approved the correction for native validation. The fixtures do
  not execute the full compiler on the boot image or generated ARM64 code.
- All-ABI debug assembly passed in 39 seconds. Packaged arm64 markers
  DDSTART1–8 and DDSTART9 are present.
- Artifact: `build-downloads/DDSTART9-debug.apk`.
- APK SHA-256:
  `6d49acbb76c91e7576cc1dbbaa80fd49c8ccabef3d8830ef7c0193dc36d5590a`.
- Package: `org.mupen64plusae.turnip.pwnedbygary.debug`; upstream manifest
  label `3.0.335 (beta) 1e0e1bfa`.
- The native dirty diff from that local build base is confined to
  `new_dynarec.c`: the boundary helper/call and gated initialization marker.
  The accompanying regression and verifier changes are host-side only.
- Certificate SHA-256:
  `311f4e35e939256ae8df53ecbf15c43235d1ee97f8a0138d332839c5e53c2bfc`,
  unchanged from DDSTART7/8. Keep signing material local.

### Next capture and remaining acceptance

Update with `adb install -r`, never uninstall or clear data. Keep the original
dynarec profile, unchanged timing settings, the same inputs, explicit DD
support and no save-state autoload.

Capture one launch to `ddstart9-logcat.txt`. If the menu appears, try starting
a race and continue capture for roughly a minute. If it remains black, keep
the capture running for at least 45 seconds. Supply the log plus the observed
screen state and audio behavior; include a screenshot if text is corrupted.
Check the new boundary marker and compilation sequence before changing any
other setting or broadening the correction.

Native DD menu/gameplay/audio acceptance, the full cached-interpreter
dispatcher comparison, and required plain-cart/WritableROM persistence
regressions remain open. This task remains **IN_PROGRESS**.

## DDSTART9 native confirmation and remaining transition freeze — 2026-09-12

Capture `ddstart9-logcat_1789251365596.txt`, SHA-256
`7d9bfd2b1cc896a7f34dde4cbcaebbf68d9d1e6493868998122c50cbe280bc02`,
contains **five Dynamic Recompiler launches: four Japanese cartridges and
one USA cartridge**. All have the DDSTART9 marker. No cached-interpreter
fallback was introduced. The last Japanese run explicitly enables Parallel
2x upscaling. See [DDSTART9_RUNTIME_ANALYSIS.md](DDSTART9_RUNTIME_ANALYSIS.md)
for per-session identity, evidence and limits.

The four Japanese runs confirm the predicted correction natively: the first
boot block is 79 words ending at `800bb67c`, followed by a separately compiled,
current-matching 199-word callee. Later boot-tail compilations also match
current code. The old exact-fault observer remains enabled, and the
`800ad49c`/`800ad4ac`/`079bb080` fault does not recur. The user confirms
Japanese menu progression. **Retain the DDSTART9 correction unchanged.**

The user still sees menu/text corruption and a gameplay-transition freeze,
similar to cached interpreter. This is not complete native DD acceptance.
The USA run is a distinct cart/disk/boot path and does not demonstrate the
same Japanese failure; per-session IPL byte identity is not in the log.
An initial chat statement that all five cartridges were Japanese was
incorrect and was corrected after isolating the USA session.

Existing startup probes do not cover the later transition. Japanese BM
logging reaches its cap; the last sampled progress ordinal is 16,384, without
the higher-threshold context snapshots. Unimplemented command warnings are
candidates, not evidence sufficient for another correction.

In two Japanese runs, queued exit/autosave never completes. Repeated frontend
RUNNING/PAUSED callbacks afterward are explained by the existing shutdown
and pause retry timers, not ongoing guest CPU progress. This makes a shared
synchronous native/plugin blockage worth checking, without assigning the
cause to RSP, graphics, disk or timing yet.

### Next capture, using the existing APK

No new APK or timing/profile change is required. Reproduce the Japanese
gameplay freeze, leave the frozen emulation view open without Pause/Exit,
and capture two stack-only `debuggerd -b` samples of the
`:EmulationProcess` process. Android/device permissions may deny this;
preserve errors and do not root, uninstall or clear data to bypass them.

Obtain the corrupted-menu screenshot and audio behavior as separate evidence.
Use native stack PCs/build IDs to distinguish RSP/video/driver blocking from
CPU/generated-code execution. If stack access is unavailable, the fallback
is a minimal DD-gated atomic phase/heartbeat diagnostic observable while
the emulation thread is blocked, not another startup-only logging stream.

The delivered APK remains DDSTART9, SHA-256
`6d49acbb76c91e7576cc1dbbaa80fd49c8ccabef3d8830ef7c0193dc36d5590a`.
No runtime code changed in this analysis iteration. The task remains
**IN_PROGRESS** for gameplay/display/audio, full cached-dispatcher coverage
and required cart/WritableROM persistence regressions.

## Screenshot/audio follow-up and Retroid root-script capture — 2026-09-12

The user supplied title/menu/cup/vehicle screenshots. Backgrounds, cup icons,
and vehicle models render, while text/texture regions are noisy or corrupted.
They confirm music plays before stopping at either a manual race transition
or an attract-mode race. Do not treat a vehicle preview as successful gameplay
or infer an IPL/renderer root cause solely from the corrupted text.

The rootless stack upload is six lines / 164 bytes, SHA-256
`4977e66ddf8d5d2a8d25c7b2b04f90c0f8a9a3690e6987a33f2af44566f63dd8`.
Both normal shell and same-UID `run-as` report `debuggerd: root is required`
for each of two samples. **No stacks were collected.**

The user has now offered the existing Retroid settings **Run script as Root**
facility. This supersedes immediately building another phase-observer APK:
first obtain scoped native thread stacks using that facility. Leave the
Force SELinux switch and emulator configuration unchanged. No permanent
rooting/unlocking, app reinstall, permission workaround, or save deletion
is required by this procedure.

The capture helper and procedure are documented in
[DD_NATIVE_STACK_CAPTURE.md](DD_NATIVE_STACK_CAPTURE.md). It delays capture
so the user can leave Settings, return to the game, and reproduce the freeze,
then requests two stack-only dumps of the exact emulation process.
Only diagnostic files are created; emulator source and the DDSTART9 APK are
unchanged. Helper host tests do not establish that the vendor root runner
or native backtrace collection succeeded on the device.

Verification: both helper/test scripts pass POSIX shell syntax checks;
52 mocked host assertions pass. Review-required fixes enforce one unique
process with stable PID/start time across both samples and test the actual
root gate through host `sh`, rather than mistaking a missing Android shebang
interpreter for a successful refusal. Detached launch errors are retained
in the capture's `launcher.log`. No emulator build was needed.

The overall task remains **IN_PROGRESS** pending those native results and
the existing gameplay/display/audio and cart/save regression requirements.

### Retroid runner entry-point follow-up

The user reports that selecting the multiline helper displayed **538 steps**
and **Script ran**, but no capture appeared. This is consistent with the
vendor treating physical lines as commands rather than parsing a complete
shell program; that implementation detail is not yet confirmed. No native
stacks or new emulator-fault evidence resulted from this attempt.

Keep the helper and DDSTART9 APK unchanged. Add a one-line menu entry point
that explicitly invokes `/system/bin/sh /sdcard/Download/ddstart9-root-stacks.sh`
and appends stdout/stderr to `ddstart9-root-launch.log` in Download. Select
**ddstart9-run-as-root.sh**, not the multiline helper. Startup errors are
therefore retained even when no capture directory is created. Existing
diagnostic files, SELinux, app settings, and saves remain untouched.

The host suite now includes a fresh-shell-per-line runner fixture, a
multiline helper stub, failure-status/stderr checks, and prior-log
preservation. The 58 host assertions and shell syntax checks pass; actual
Retroid invocation and stack collection remain unverified. Next: run the
one-line launcher and reproduce the freeze during the same 90-second delay;
if no capture appears, retrieve the top-level startup log before retrying.

### Successful root captures: anonymous execution on the emulation thread

The uploaded diagnostics now prove that both root captures completed, with four
successful snapshots of the same PID/start time and 31 threads per snapshot.
See [DDSTART9_NATIVE_STACK_ANALYSIS.md](DDSTART9_NATIVE_STACK_ANALYSIS.md) for
archive identity, timestamps, address table, symbol verification, and limits.
The Terminal wait/download issue did not prevent the detached native captures.

The primary emulation-service thread has a single anonymous-code frame at four
different offsets across about 48.5 seconds. The video workers resolve against
the preserved matching DDSTART9 library to queue condition waits. This does not
show a graphics-driver blocking call on the emulation thread, but neither does
it establish a particular CPU/RSP loop or the cause of corrupt graphics.

CPU dynarec and Parallel-RSP generated code can execute on the same thread.
Next obtain same-process memory-map metadata and thread CPU-accounting deltas,
not another identical stack-only capture. If the process has restarted, gather
maps and stacks together rather than using old anonymous addresses. No emulator
source, APK, save, SELinux, or timing changes were made. Task remains IN_PROGRESS.

### Restarted process: collect fresh stacks and metadata together

The user confirms the captured game process was closed or restarted. Do not
request maps for old PID 14559 or reuse its anonymous-code addresses.

The next root-helper iteration retains the working one-line menu entry and
90-second preparation delay. It collects process maps and bounded per-thread
accounting metadata before each of two fresh stack dumps, with exact-process
identity/start-time checks. These are metadata reads, not process-memory dumps.
Read failures must remain explicit and must not masquerade as complete evidence.

The Mac collection instructions poll completion using separate short ADB reads
and retrieve available diagnostics even after a reported timeout. This avoids
relying on the previous long-running remote wait, whose failure cause remains
unestablished. Earlier device captures are preserved.

No emulator source or APK change is required. Reproduce the same race/attract
freeze under DDSTART9/dynarec, leave the frozen view open, and return the complete
new capture folder so stacks and map/CPU-accounting samples can be correlated.

Verification: helper format v2, shell syntax checks passed, and 73 mocked host
assertions passed. Review identified a proc-stat parser weakness for names with
spaces/parentheses; the parser now locates start time after the final comm
delimiter and regression fixtures cover changed start times with those names.
The new maps/thread files use explicit read outcomes, bounded reads and a
128-thread cap. No claim of device success is made for this enhanced version.

### Enhanced capture retrieved before its completion

Archive `ddstart9-metadata.h8qFXw_1789273779262.zip` has SHA-256
`3d3d8abe922bf811ac817a9834fa53d6f8dd580d362f7ada8c61c5ced7ab65d1`.
Its `capture.g1vOZS` targets PID 26570/start time 437212 with root UID 0
and CLK_TCK 100. Sample 1 maps completed successfully; thread metadata reaches
00:28:45 UTC−04:00 on 2026-09-13 but is partial. Its own status is still
`running`; native stacks and both second-sample files are empty.

The archive's shared `latest-complete` instead names `capture.SXe0e8`,
completed at 00:28:42. That directory is not included in this archive.
A shared-marker change therefore does not establish completion of the intended
run. The Mac wait can return prematurely with another attempt outstanding.
This is incomplete evidence, not proof that the enhanced worker failed.

Retrieve existing `capture.g1vOZS` after its own terminal status, without starting
another root capture. Do not pair these new maps with the previous process's
anonymous addresses. A sampled ServiceStartArg thread is running (TID 26612),
but matching fresh stacks and the second accounting sample remain necessary.

### Completed metadata identifies Parallel-RSP generated code

The completed archive for the same capture now includes both fresh stacks,
both maps and both accounting sweeps; its own final state is `complete`.
See [DDSTART9_RSP_OWNERSHIP_ANALYSIS.md](DDSTART9_RSP_OWNERSHIP_ANALYSIS.md)
for integrity, addresses, exact allocator comparison and accounting values.

Both sampled PCs fall in a `0x76000` execute-only VMA followed by a PROT_NONE
remainder, together exactly 1 GiB: Parallel-RSP's ARM64 JIT allocation layout.
The distinct CPU dynarec cache is independently mapped as 32 MiB RWX elsewhere.
TID 26612 accumulated 44.60 user CPU seconds across about 45 seconds, corroborated
by 44.642214347 scheduler-runtime seconds. This identifies busy execution on the
emulation thread with sampled PCs in RSP-generated code, not a native waiting
thread. It does not establish the exact RSP loop or a compiler bug.

Prepare DDSTART10 as diagnostic-only RSP host-range/IMEM provenance, gated by
explicit per-game DD activation. Retain DDSTART9's correction and all baseline
execution semantics. No additional generic root capture is needed before that
observer exists; the next sample must be paired with its range logs.

### DDSTART10 diagnostic build prepared

[DDSTART10_RSP_PROVENANCE.md](DDSTART10_RSP_PROVENANCE.md) records the optional
DD-gated core/RSP bridge, exact host ranges, compile-input instruction chunks,
bounded distinct task identities and actual entry/return counters. DDSTART9
behavior remains unchanged; no speculative RSP correction is included.

Host tests, focused review fixes and final all-ABI APK assembly passed.
APK SHA-256 is
`bcd78f4d0126c8d2fc65fef0f60fb6c6230bf531b3732cf17c964fcfef2139ba`.
Package and signing identity match DDSTART9. Matching ARM64 symbols are
preserved locally; no device gameplay or save-regression result is claimed.

The new Mac orchestrator validates that APK, uses `install -r`, starts Core
logcat before launch, identifies a unique newly launched root capture against
a pre-launch directory snapshot, and waits on its own status. The already
installed device helper/one-line entry are unchanged. Return its fresh Desktop
ZIP for same-process host-range/IMEM correlation; do not mix it with old PCs.

### DDSTART10 native result and diagnostic limitations

The new Mac orchestration completed correctly and retrieved fresh
`capture.ZveDAv`, with Dynamic Recompiler and explicit DD support confirmed.
See [DDSTART10_NATIVE_ANALYSIS.md](DDSTART10_NATIVE_ANALYSIS.md).

The first native sample maps to allocation record 97 and IMEM `0xf60..0xfff`;
the second is in Parallel-RSP's JIT block lookup via its dispatcher. The
emulation thread accumulated approximately 45.66 user CPU seconds across
approximately 46 seconds. Compile words at `0xf60`, `0xb20`, `0x000` and
`0x200` strongly resemble CPU rendering code/data rather than expected RSP
microcode, but their precise source RAM address and corrupting writer remain
unknown.

Correct two earlier diagnostic claims: the recorded host extent is allocation
capacity, not an exact emitted-byte interval; and the separate full-IMEM hash
can remain stale after internal DMA refreshes the JIT's cached IMEM. The last
distinct task identity is not necessarily the stalled invocation. Do not infer
the write's timing or origin from absent subsequent identity lines.

Prepare DDSTART11 as observation only, covering both CPU/core and RSP-internal
IMEM DMA writers with raw/derived transfer parameters and before/after evidence.
Keep normal DMEM traffic and shared startup log caps from exhausting that
coverage. Preserve DDSTART9 and all DMA/CPU/RSP execution semantics.

### DDSTART11 ready for native writer correlation

Implemented and reviewed both IMEM DMA observer paths; see
[DDSTART11_IMEM_DMA_PROVENANCE.md](DDSTART11_IMEM_DMA_PROVENANCE.md).
Neither DMA implementation was corrected or otherwise changed. The separate
diagnostic hash now invalidates on dirty-code refresh, and region extents are
labeled allocation ranges.

Review closed three coverage hazards: ordinary internal DMEM traffic consuming
the IMEM budget, core observations being suppressed by the shared DDSTART1
logger cap, and core eligibility missing DMEM-to-IMEM crossings. Tests include
these cases. Core and Parallel-RSP hashes use different algorithms/coverage;
compare addresses, samples and within-path changes, not their numeric equality.

The final all-ABI APK passed packaging/signature/symbol checks:
`build-downloads/DDSTART11-debug.apk`, SHA-256
`ffc6459fc6c7a5ae53a11f5cf54b7bc8f8c9aa880e53600a7a1633de15cb2df1`.
Package/signing identity match previous debug builds. The Mac/root helpers
are unchanged; pass the new APK path/hash. Their Desktop ZIP prefix remains
`ddstart10-rsp`, so identify this build by its verified APK hash.

Await native evidence assigning the suspicious IMEM contents to a transfer and
its task/input state. Exact writer/cause, a minimal correction and gameplay/audio/
plain-cart/writable-cart validation are not yet complete.

### DDSTART11 native result: internal DMA writer established

The new upload's old filename prefix is harmless: its APK hash matches DDSTART11,
and fresh capture `dSOS6x` completed with dynarec enabled. See
[DDSTART11_NATIVE_ANALYSIS.md](DDSTART11_NATIVE_ANALYSIS.md).

Internal DMA record 8 directly captures the IMEM overwrite during audio entry
1512: SP destination `0xfb0`, RDRAM source zero, length register `0xffffffff`.
The plugin clamps 4096-byte rows to 80 bytes, performs 256 rows with raw skip
4095, and crosses from DMEM into IMEM. It records 3052 IMEM word writes and
before/after samples matching the subsequently compiled suspect words.
Both native samples fall within allocations compiled after that event.

Static microcode analysis explains the register values as an extracted zero
command length decremented in a call delay slot and a zero low-24-bit source.
The upstream origin of those fields remains unknown. The task is audio type 2,
flags zero; do not resurrect an older yielded-graphics handoff explanation.

Ares's latched-bank/12-bit-address model supports correcting the demonstrated
bank and row-length discrepancy; a destination-mask-only patch is insufficient.
Skip alignment and post-transfer register behavior need explicit choices because
the compared implementations differ. No emulation correction was made in this
analysis, and no new capture is needed solely to prove the same writer again.
The overall task remains open for a minimal DD-gated correction and native
gameplay/audio/cart/save validation.

### Detailed implementation plan for local-LLM handoff

Planning documents now separate the demonstrated DMA defect from the unresolved
audio-command origin:

- [Repair plan](N64DD_DMA_AUDIO_REPAIR_PLAN.md): evidence, invariants, reference
  decisions, explicit DD policy and logging separation, and completion criteria.
- [Work packages](N64DD_DMA_AUDIO_WORK_PACKAGES.md): P00–P12, with conditional
  audio work and smaller subdivisions for policy wiring and exact producer tracing.
- [Validation runbook](N64DD_DMA_AUDIO_VALIDATION.md): production-path fixtures,
  mode/lifecycle checks, audio provenance, native acceptance and save persistence.

Recommended execution: complete the reference-policy gate, failing production
fixtures, explicit DD policy seam, minimal internal-read correction and bounded
post-correction evidence; then obtain the first native result before selecting an
audio correction. Corrected DMA must work with logging disabled. Diagnostics must
still observe the suspect request when it no longer writes IMEM.

This update supplies a plan only. No DMA/audio correction was implemented, no new
APK was built, and the overall native repair remains open.

## P00 baseline established on the local Linux host — 2026-09-13

The investigation resumed on a new local host (CachyOS Linux, Retroid Pocket 6
attached by USB). P00 was executed read-only; see
[P00_BASELINE.md](P00_BASELINE.md) for the full record. Verified:

- Branch `dd-eos-watchdog-checkpoint` at `9df1b637d`, in sync with origin, clean
  tracked tree; both documented baseline publications present. Untracked paths
  are accounted scratch (`.fzxwork/`, `.gradle_home/`, `files/`, generated
  Gradle daemon properties).
- `rsp_dma_read` still implements the documented legacy behavior: bank-boundary
  80-byte clamp, 13-bit destination progression, raw 12-bit skip, 4/8-byte
  register masking. The captured request decodes exactly as DDSTART11 recorded.
- The device still installs exactly DDSTART11 (pulled `base.apk` re-hashes to
  `ffc6459f…cb2df1`). The local stale APK output (`384b673a…`) is not that build.
- The Phobos reference is locally available as a clean checkout pinned at
  `f1174e7654141accad40b9ffc2c7d978e93a00f0` with all four plan-named Ares RSP
  files; the latched-bank/12-bit-offset model was spot-checked. Its DMA-relevant
  equivalence to the plan's original archive must be confirmed in P01. The raw
  capture archive is not on this host; the committed analysis carries the evidence.
- Host suites: callbacks, core IMEM DMA (DDSTART11) and root-stack suites pass.
  The dynarec-observer test fails to compile under gcc 16 `-Werror`
  (`new_dynarec.c` unused-variable warnings) and the Mac capture-helper mock
  suite fails 8/14 on this host — both are toolchain/portability issues of the
  test harness, recorded as findings, not emulator defects and not baseline
  changes.

No emulator behavior changed. P01 (complete DMA policy decision table against
the pinned reference and CXD4) is the next package.

## P01 DMA policy ledger completed — 2026-09-13

```markdown
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
Independent reviewer and verdict: (pending — filled before commit)
Commit, if approved: (pending)
Remaining blockers: none for P02/P03. P02 needs the §4 table as its oracle
  specification; P03 needs the corrected-vs-legacy selector requirement only.
Next eligible package and required inputs: P02 (production-path failing fixtures;
  inputs: this ledger §4/§5/§7, cp0.cpp, tools/tests layout) — may run in
  parallel with P03 (explicit DD runtime-policy seam) with disjoint file
  ownership. P04 waits for both.
```

## P02 production-path DMA fixtures — 2026-09-13

```markdown
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
Independent reviewer and verdict: (pending — filled in commit message per
  protocol; the reviewed snapshot hash is recorded there)
Commit, if approved: (pending)
Remaining blockers: none. P03 and P04 can consume this suite; P04 must wire
  the test's corrected mode to the real policy seam and flip
  DD_DMA_REQUIRE_CORRECTED.
Next eligible package and required inputs: P03 (explicit DD runtime-policy
  seam; inputs: repair plan §6.1 properties, validation §3 G01–G16, file
  ownership: core callbacks.c/plugin.c/h + app CoreInterface.java +
  parallel.cpp setter) — analysis/implementation independent of P02's files.
```
## P03 explicit DD runtime-policy seam — 2026-09-13

```markdown
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
Independent reviewer and verdict: (pending — filled in commit message per
  protocol; reviewed snapshot hashes recorded there)
Commit, if approved: (pending)
Remaining blockers: none for P04.
Next eligible package and required inputs: P04 — minimal internal-read DMA
  correction behind RSP::DdRuntimePolicyEnabled() (inputs: P01 ledger §4
  decision rows, P02 fixture suite with DD_DMA_REQUIRE_CORRECTED=1 as the
  gate, cp0.cpp rsp_dma_read; the corrected mode of the P02 suite must be
  wired to drive the new seam and the runner default flipped).
```

## P04 internal-read DMA correction — 2026-09-13

```markdown
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
Independent reviewer and verdict: (pending — filled in commit message per
  protocol; reviewed snapshot hashes recorded there)
Commit, if approved: (pending)
Remaining blockers: none for P05.
Next eligible package and required inputs: P05 — bounded post-correction
  evidence (inputs: rsp_diag.cpp/.hpp schema, corrected-arm observation
  points in cp0.cpp, the raw-request-shape trigger requirement 0xfb0/0/0x
  ffffffff shape, validation §4 A01-A11; keep DDSTART11 legacy observer
  behavior for DD-off sessions).
```

## P05 post-correction evidence contract — 2026-09-13

```markdown
Package: P05 — Preserve useful evidence after the overwrite is fixed (bounded
  diagnostics; no new emulation policy; guest behavior unchanged)
Baseline / Reviewed snapshot: dd-eos-watchdog-checkpoint @ 731694b58; changed
  files: mupen64plus-rsp-parallel/upstream/rsp_diag.hpp, rsp_diag.cpp,
  rsp/cp0.cpp (observation-only captures in both arms),
  tools/tests/rsp-dd-dma-transfer-test.cpp, docs/HANDOFF_NEW.md.
Verified observations: the DDSTART11 dedup would have made the recurring
  suspect request invisible under the corrected policy (identical operands,
  IMEM now unchanged), so the P05 trigger path is dedup-exempt with its own
  4-snapshot-per-session budget, separate from the 512-record DMA budget and
  all startup logs. Trigger shape = the raw DDSTART11 evidence: length
  register 0xffffffff (zero extracted size after the microcode decrement)
  with a zero 24-bit DRAM address. Logging-only; the T-dup fixture proves
  guest state is byte-identical across 6 consecutive triggered runs.
Derived results and inputs: schema version 2 appends policy, trigger reason,
  capture_eligible, probe_skipped/reason, actual_imem_writes, skip_effective
  (raw 0xfff vs aligned 0xff8), dirty_after, final_cache/final_dram,
  entry_generation, sp_pc, t9/k0 and the full 32-GPR snapshot to every
  emitted dma_read record — the GPR capture is taken at the MTC0 launch from
  the live registers (origin: exact, per the DDSTART11 static helper
  analysis), so the operands that produced the raw request survive any later
  DMEM overwrite. Duplicates beyond the budget are counted, reported once as
  exhaustion, and summarized (snapshots/duplicates) when diagnostics are
  re-registered or detached. LEGACY_CROSSING is recorded as an informational
  reason on ordinary legacy records only when the transfer STARTED in DMEM
  (start bank captured before the row advance mutates the destination) and
  actually wrote IMEM; fixture pins X1 (plain full-IMEM load, trigger=0) and
  X2 (genuine two-row crossing, trigger=2) guard the classification in both
  directions. The legacy observer's DDSTART11 dedup and budget semantics are
  otherwise untouched. Trigger snapshots share the ordinary record/sequence
  counters, so ordinary record numbers can gap when triggers fire
  (monotonic, harmless). Deferred with reasons: the
  bounded recent-command ring and task-buffer snapshot (no verified fetch
  cursor exists — that is P07's boundary; the plan gates both on it), and
  A05/A06/A07 command-buffer provenance fixtures (same dependency).
Remaining hypotheses: none added. The first corrected native run can now
  answer: does the suspect command still occur (trigger snapshots), with
  which exact register operands (GPR capture), and does the corrected
  transfer preserve IMEM (before/after hashes + zero actual writes).
Changed files: rsp_diag.hpp, rsp_diag.cpp, rsp/cp0.cpp, transfer fixture,
  this handoff.
Checks actually run and why: bash tools/test-dd-dma-transfer.sh (both modes
  all-pass; D01 contract fields policy=0/trigger=1/actual=3052/
  skip_effective=4095; D02diag policy=1/actual=0/skip_effective=4088/final
  regs 0xfb0-0x1fe808; T-dup: 4 snapshots + exhaustion + summary 4/2 + exact
  t9/k0 capture + unchanged guest state); test-dd-policy.sh,
  test-dd-core-imem-dma.sh, test-dd-root-stacks.sh all pass; startup
  callbacks step passes (dynarec step still blocked by the pre-existing
  new_dynarec.c warning-as-error issue); clang syntax checks clean on
  rsp_diag.cpp, cp0.cpp, parallel.cpp.
Independent reviewer and verdict: (pending — filled in commit message per
  protocol; reviewed snapshot hashes recorded there)
Commit, if approved: (pending)
Remaining blockers: none for P06.
Next eligible package and required inputs: P06 — first native decision test.
  Build all ABIs with the original signing identity, record source revision/
  APK hash/certificate/Build IDs, install -r (never uninstall), confirm
  dynarec + policy active, reproduce the DDSTART11 route, capture, and
  classify by the validation §5 decision table. Diagnostics may be enabled
  or disabled; the correction is policy-driven either way.
```
