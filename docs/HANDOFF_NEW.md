# N64DD investigation from the v336 release baseline

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