# Wave Race Shindou English translation: logo stall

## Update — 2026-09-22: measured identity and automatic-default candidate

The user copied their existing ZIP from the device with ADB, extracted it on
their Mac, and supplied these command results. No ROM was uploaded, acquired,
modified, or checksum-repaired:

| Extracted cartridge property | Measured value |
| --- | --- |
| Size | 8,202,240 bytes |
| MD5 | `EFDE606C824DAACF715928B914AC26E0` |
| SHA-256 | `0c50998feebe1dce41b1a6c5517dfc1f6db4c751d0b654a95ebbfb363b7a607e` |
| Byte-order marker | `80 37 12 40` (native big-endian; no normalization needed) |
| Header CRC | `57AF88CE EDE723DA` |

This independently measured MD5 matches the prior save-directory suffix.
The earlier sections below retain the historical distinction between candidate
identity and measured identity.

Reported patch provenance:
[Zoinkity's Wave Race 64 English translation](https://romhack.ing/database/content/entry/f9Nv5JQBNs8FWu0C34KQ/wave-race-64-english-translation).
The page, checked on 2026-09-22, lists version **1.1**, released 2023-08-08,
while the supplied filename says English v1.2. Do not equate the filename's
version with the patch release or claim the patch was independently reproduced.
The compatibility record deliberately labels this measured copy “English
translation” without asserting a translation version.

The user also supplied ADB foreground activity/package data, installed version
data, and a SHA-256 of the pulled base APK:

| Installed APK property | User-measured value |
| --- | --- |
| Package | `org.mupen64plusae.turnip.pwnedbygary` |
| Version code | `337` |
| Version name | `3.0.337 bb6716bd` |
| APK size | 33,234,167 bytes |
| SHA-256 | `bae8138b1232802d635ad10f931a2b3500b797a18bb5336f27a2463fb8a91e6a` |

This is **not** the documented public v337 APK hash. The version label alone
does not establish the exact source/diff, build origin, or signing certificate.
No replacement APK has been built or published for this change.

### Candidate source change

- The Android bundled database adds the measured MD5 and header CRC, with
  `RefMD5=FF67DF97476C210D158779AE6142F239`. This inherits original Shindou
  CountPerOp=3, two players, 4KB EEPROM, Mempak and rumble.
- Existing native/frontend lookup remains MD5-first, then CRC fallback. This
  is not a new MD5-only matching policy: another unknown hash with the same
  unique CRC will also match, as existing database semantics require.
- The extracted database asset revision increases from 13 to 14 so existing
  installations replace the cached database even at the same APK version code.
- Explicit Count per Op overrides still win. Game preference and save-path
  identity remain based on the original ROM identity/header, not the new
  display name. No DD or writable-cart code is changed.

### Remaining acceptance

Host checks passed on the candidate:

- `NODE=/nix/store/0akvkk9k1a7z5vjp34yz6dr91j776jhv-nodejs-20.11.1/bin/node bash tools/test-wave-race.sh`
  (Node is available in the environment but not on the default PATH).
  Executes the native database parser/open-ROM path with synthetic headers
  and injected digests; executes production Java database and asset-extraction
  classes with Android host stubs. Covers translation/original timing 3,
  unknown timing 2, CRC fallback, inherited metadata, explicit overrides,
  asset revision 13 → 14 refresh and no redundant refresh at 14. Save identity
  and Splash lifecycle gate checks are source-contract checks, not device tests.
- `CC=clang CXX=clang++ bash tools/test-dd-startup.sh` passed, including DD
  policy, dynarec boundary, legacy/corrected Parallel RSP DMA and core RSP DMA.

The host suite requires a C compiler, Node >=16, and JDK >=12. It does not
emulate this ROM or verify Android preference persistence, full Splash
lifecycle, gameplay, or save/reopen.

The manual setting of 3 is already device-confirmed; do not repeat that test.
The new automatic-default candidate is **not yet device-accepted**. It needs
an update signed compatibly with the existing installation, exact source/diff,
APK hash/version/signer records, and verification that the installed database
refreshes. Never uninstall, clear app data, rename save folders, or replace
ROM bytes to bypass a problem.

After such an update, enable this game's default Count per Op setting, keep
Dynamic Recompiler and the known-working renderer/driver unchanged, and use
fresh **Start**, not Resume or a save state. Confirm title, menu, and race.
If startup fails, stop after 60 seconds and restore the manual setting of 3.
Save/reopen acceptance remains a separate pending check.

## Status — 2026-09-17

**User-confirmed fresh Start with Dynamic Recompiler reaches gameplay after
setting Count per Op to 3.** The user
reports a displayed 20 FPS that looks smoother than expected. This confirms a
useful per-game setting on their installation, not yet a verified automatic
compatibility fix. No emulator or ROM database changes have been made.

The user also reports both **GlideN-Very Accurate and ParaLLEl** work at 3.
The follow-up capture confirms separate GLideN64/HLE RSP and ParaLLEl/ParaLLEl
RSP launches. This supports a timing-dependent issue rather than a defect unique
to one renderer; the capture alone does not demonstrate gameplay.

Still unknown: installed APK version/hash/signer/source, extracted ROM MD5 and
size, patch provenance, effective native timing/overclock values, and
save/reopen behavior. Do not infer these from the
reported gameplay success. Do not require repeating the failed setting merely
to reproduce the original symptom.

## Initial evidence: observations, not diagnoses

The private supplied log and screenshot were inspected without copying their
contents into this document or publishing them.

- Frontend lookup reports no ROM metadata entry for the English v1.2 filename,
  with header CRC `57AF88CE EDE723DA`.
- The save-directory suffix is `EFDE606C824DAACF715928B914AC26E0`. This is a
  **candidate** identity, not an independently verified extracted-ROM MD5.
- The log selects ParaLLEl graphics and RSP, and loads custom
  `Turnip_v26.3.0-R5`; Vulkan identifies Adreno 740.
- Native DD loading reports a save ending in `SramData/.ndr` and a disk path
  ending in `cache/WorkingPath/`. Initialization subsequently continues.
- No emulator fatal/ANR was found in this capture. That does not exclude a guest
  stall or prove healthy emulation.
- The screenshot shows the Nintendo 64 logo and a 59 counter. A single image
  cannot establish guest progress or the duration of a stall.
- Later `resume emulator` messages describe an activity/emulation resume. They
  do not establish whether the original gallery launch used Start or Resume.

## Source-backed timing hypothesis and current evidence

Checked on local development branch `main`, starting at `efffb2f0`.
This is not an APK source-provenance assertion. The current handoff records that
the former GitHub DD investigation branch was archived after v337; do not reuse
that branch or its old diagnostics. No remote branch was changed for this work.

The bundled Android database contains:

| Identity | Timing and relevant settings |
| --- | --- |
| Original Shindou `FF67DF97476C210D158779AE6142F239`, CRC `535DF3E2 609789F1` | CountPerOp=3, Players=2, Eeprom 4KB, Mempak=Yes, Rumble=Yes |
| Ordinary unknown cartridge | Native CountPerOp=2, players=4, rumble/mempak enabled, 4K EEPROM fallback (except supported homebrew save declarations) |

Neither the candidate translation MD5 nor the reported CRC has a record in the
bundled Android database. Unknown metadata does **not** mean rumble or EEPROM
support is missing.

Source chain:

- `app/src/main/assets/mupen64plus_data/mupen64plus.ini`: original Shindou record.
- `mupen64plus-core/upstream/src/main/rom.c`, `open_rom`: normalizes cartridge
  byte order, computes MD5, looks up MD5 then CRC, and applies unknown defaults.
- `GamePrefs.java`: ordinary cartridge default writes CountPerOp=0; explicit
  per-game settings use the selected value. NDD/DD-path timing logic is separate
  and unchanged.
- `NativeConfigFiles.java`, `syncConfigFiles`: writes CountPerOp,
  CountPerOpDenomPot and R4300Emulator into the native Core configuration.
- `mupen64plus-core/upstream/src/main/main.c`: resolves CountPerOp<=0 from
  ROM_SETTINGS; positive explicit settings win. The overclock denominator is
  separate, and netplay can synchronize settings.

The initial hypothesis was that an unrecognized translation lost the original
Shindou timing default. The user's success with 3 supports that hypothesis.
The original effective value was not recorded, so this is not yet a measured
native 2-versus-3 comparison. The user subsequently explicitly confirmed that
the successful run used fresh Start and Dynamic Recompiler. This establishes
the manual workaround on their device, not acceptance of a new binary or
automatic database setting.

Count per Op is a CPU timing approximation, not a graphics-quality switch.
The CP0 implementation advances its timer using the instruction count multiplied
by count_per_op (with the separate overclock denominator when enabled).
Changing this changes timer/interrupt timing relative to executed game code.
It is therefore plausible for a timing-sensitive startup to work at 3 and wait
indefinitely at 2. The exact guest wait or scheduling dependency has not been
traced; do not claim a particular guest loop, copy-protection check or CPU
instruction defect. The strongest current explanation is loss of the existing
Shindou compatibility setting when the patched ROM is not recognized.

## Follow-up capture

The supplied follow-up log spans approximately 19:42:46–19:44:23:

- It selects GLideN64 with HLE RSP, then ParaLLEl with ParaLLEl RSP.
- Frontend metadata still reports the same unrecognized translation CRC.
- Both launches retain the same candidate save-directory suffix.
- Both still emit the directory-as-DD-media errors despite the user's reported
  successful gameplay. This further argues against treating those warnings as
  the cause of the logo stall.
- A `Save completed` callback appears for an **AutoSaves save state**. The
  callback's log line precedes its result check in CoreService, so it is not
  independent proof of a valid saved state. No matching failure was found, but
  neither this callback nor a save state establishes in-game EEPROM persistence
  or successful reopen.
- No emulator fatal/ANR, native effective CountPerOp, native CPU-engine identity
  or APK version/hash was found. CPU and cold-start confirmation remain sourced
  from the user's explicit report, not inferred from missing log lines.

## Counter interpretation

`ae-bridge/src/ae_bridge.cpp`, `VidExtFuncGLSwapBuf`, counts video swap calls
over monotonic elapsed time. `GameActivity.onFpsChanged` normally displays the
lower of that rate and the separate shader-thread rate at baseline speed.
It is not a direct VI interrupt counter or emulation-speed percentage.

Consequently 20 on this overlay does not by itself mean one-third emulation
speed. Repeated display refreshes and evenly paced new frames can look smooth.
This source tree does not establish a Wave Race-specific expected frame rate;
do not claim a measured full-speed result or performance improvement. Normal
game-clock progression and audio are useful additional observations.

## Separate loader/logging issues — not the demonstrated stall cause

`CoreInterface.setWorkingPath` initializes the DD disk path to WorkingPath plus
a slash. The disk callback uses `File.exists()`, so it accepts that directory
without an actual disk filename. This explains the empty DD-path errors without
requiring that the user enabled DD. Do not describe the directory as necessarily
empty; the problem is accepting a directory as media. No DD change is justified
by the current Wave Race evidence.

The byte-buffer ROM-open wrapper discards the native command result. This is a
source-level weakness, not evidence that this ROM failed to open. No broad loader
changes were made.

The current `app/proguard-rules.pro` removes Java Log.i/d/v calls in optimized
builds, including calls reached through the native core's Java debug callback.
Native libraries can still emit INFO directly. Missing core INFO identity/CPU
lines are therefore compatible with release logging; they do not prove a
failed ROM open. Historical DD launch diagnostics are not present-day cartridge
diagnostics. Effective native timing remains unobserved, not silently assumed.

## One bounded next device test

Keep Count per Op **3** for this game only. Preserve existing saves, filename,
ROM bytes and game preferences. Do not reinstall, clear data, repair checksums,
rename the save folder, or enable DD tracing.

1. At the confirmed working setting (3, Dynamic Recompiler), let the game save
   through its normal in-game mechanism; note the progress/result saved.
2. Exit normally and choose **Start**, not Resume or an in-session reset.
   Check that the expected in-game data persists. Do not load a save state.
   Keep the renderer, driver and other settings unchanged.

This is the remaining persistence check, not a request to repeat the already
confirmed dynarec boot or a new renderer/CPU test matrix. If it fails, stop and
report the exact stage and configuration; bound a still-stalled logo to 60 seconds.
Extracted-versus-archive and other controls remain conditional, not requested yet.

Before adding an automatic translation entry, independently fingerprint the
already-owned extracted cartridge (not its ZIP) and record patch provenance if
known. No ROM upload is needed. On macOS this read-only block selects the file
and prints byte count, MD5, SHA-256 and header CRC bytes:

```bash
ROM="$(osascript -e 'POSIX path of (choose file with prompt "Select the extracted Wave Race cartridge, not the ZIP")')" && test -f "$ROM" && printf 'Bytes: ' && wc -c < "$ROM" && printf 'MD5: ' && md5 -q "$ROM" && shasum -a 256 "$ROM" && printf 'Header CRC bytes (big-endian .z64 only): ' && od -An -tx1 -j16 -N8 "$ROM"
```

For the reported .z64, header CRC bytes should read
`57 af 88 ce ed e7 23 da`; a mismatch is evidence to investigate, not permission
to rewrite the header. A byte-swapped cartridge needs byte-order normalization
for comparison with native MD5, without modifying the user's original.

If an APK later becomes necessary, build with Java 17 and record package,
version, SHA-256, signer and exact source/diff. Use an update-compatible signer,
never uninstall to bypass a mismatch. A new APK is not needed just to try the
existing per-game setting.

## Validation and correction gate

Existing host checks passed on the unchanged runtime:

- `CC=clang CXX=clang++ bash tools/test-dd-policy.sh`
- `CC=clang CXX=clang++ bash tools/test-dd-startup.sh` (includes policy, dynarec
  boundary, legacy/corrected Parallel RSP DMA, and core RSP DMA checks)

These are synthetic regression checks, not Android gameplay or persistence tests.
No runtime, database, save identity, user override, native DD or independent
writable-cart policy was changed. No APK was built or distributed.

Only after identity verification should a narrow translation database entry be
considered. It must retain explicit user overrides and save locations, preserve
original Shindou and ordinary unknown defaults, and be tested through installed
database refresh plus native/frontend lookup. Title/menu, race and save/reopen
acceptance on dynarec remain required before calling an automatic fix complete.