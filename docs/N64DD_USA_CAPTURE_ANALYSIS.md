# Fresh USA-cartridge comparison: 2026-09-17

Input: `attached_assets/0_logcat_1789665590764.txt`, 887,763 bytes / 6,715 lines.
SHA-256: `b4969673d176ed9394cf3151424f6b05b161b16881b08278dea9e8d0e77a165e`.
Raw Android logs remain private; this report records only relevant evidence.
User result: base game passes; EK fails or does not start. The visible EK failure
stage was not supplied. This is not a native repair or writer-attribution claim.

## Separate the launches

| Process | Interval | Evidence |
| --- | --- | --- |
| 4481 | 13:16:31–13:16:32 | Failed earlier launch: missing old cartridge path, explicit ROM-open failure, lines 902–925. Not the later EK run. |
| 4738 | 13:17:03–13:17:25 | USA cartridge, DD policy 0, effective Dynamic Recompiler and Parallel RSP. User reports base passes. Normal stop, lines 3492 and 3580. |
| 5228 | 13:18:29–13:19:01 | Same USA cartridge with DD enabled, disk and IPL loaded; user reports failure. Normal exit after continuing interrupt observations. |

The earlier failed picker probes and the DD-disabled run's empty disk-path
warnings must not be attributed to the successful disk load in process 5228.
`resumeEmulator` is a lifecycle callback, not evidence of save-state loading.

## Established EK launch identity

- Lines 5129–5137: `F-ZERO X (U) [!]`, country USA, 16 MiB cartridge,
  MD5 `753437D0D8ADA1D12F3F9CF0F0A5171F`, CRC `B30ED978 3003C9F9`.
  These match the base-run identity at lines 2407–2419.
- Lines 5059 and 5198: the Japan-named IPL is copied and loaded. Its size is
  4,194,304 bytes. The log does not provide an IPL hash or independently decoded
  IPL region; the filename is not equivalent to a verified image identity.
- Lines 5066–5068, 5190–5193: English-named NDD copied and loaded, 64,931,840 bytes,
  SDK format, region USA, `development=0`, region word `2263ee56`, ID bytes
  `45 46 5a 45 00 00`. SDK image layout is not proof of a development-region disk.
- Line 5072: DD enabled, not direct NDD, IPL/disk configured,
  `autoLoadRequested=false`, count-per-op `1`, denominator `0`.
- Lines 5076, 5211: effective DD policy 1, `source=CART`, `combo_cart_boot=1`.
- Lines 5145 and 5457: Parallel RSP and effective Dynamic Recompiler.
- Line 5460: DDSTART9 retains non-linking boundaries and the delay slot.
- The launched package is `org.mupen64plusae.turnip.pwnedbygary.debug`;
  Android also reports a debug build. There is no app versionCode/versionName
  or APK/native source hash in the capture. v336 is not verified. The cartridge
  metadata's `Version: 1449` is not an application release number.

This eliminates a Japanese **cartridge** selection as the explanation for this
run. It does not prove equivalence of every IPL variant or reproduce P09's
texture-header/clear fault.

## Native observations and limits

Lines 5599–5611 show startup command/status exchanges:

- `01400000` = disk present + reset state, not DATA_RQ.
- Command `09` is Clear Reset; `03000000` is disk present + MECHA interrupt.
- BM control write `01000000` acknowledges MECHA; subsequent status is
  `01000000`, disk present.
- ASIC ID reads `00030000`.
- Command `1b` is Feature Inquiry; the logged MECHA status and acknowledge
  match the current source's command path. There is no later logged DATA read
  establishing the caller's interpretation of its result.

Source: `dd_controller.c` command constants, status definitions, command switch
and BM acknowledge handling; `interrupt.h` identifies event 1 as VI.
These exchanges do not establish a stuck DD interrupt or defective command 1b.

Lines 5674, 5708 and 6185 show interrupt ordinals 1024, 4096 and 16384, event 1,
with advancing CP0 count and `pc_sample=80414dc0`. Every record explicitly says
`pc_is_exact_writer=false`. This proves later interrupt callbacks occurred; it
does not establish an exact executing instruction, corrupting writer, guest
forward progress or a specific polling loop. Sparse observations are not a
continuous trace, and lack of later command records is not proof of inactivity.

No native fatal signal, exception backtrace or Android fatal exception is
recorded for the EK process. Shutdown begins through the UI at line 6534,
autosave completes at 6627, R4300 finishes at 6628, and process SIGKILL at 6655
follows teardown. That final kill is not evidence of the initial failure.

The capture has DDSTART1/2/3/9 but no DDSTART8 fault snapshot, later loader/clear
records or DDPI2 records. Absence of those probes is not proof their corresponding
faults did not occur. Cached interpreter did not run, so no dispatcher pass is
claimed.

## Do not misread disk-save warnings

The missing `.ndr` warning precedes successful source-disk loading. Production
`main.c` falls back from the absent disk-save file to the regular disk image.
Its later “Loading a saved disk” message is emitted for recognized region words,
not exclusively for successful save-file loading. Neither line justifies deleting
saves or claiming this run resumed a saved disk/state.

## Bounded check proposed at the time — now completed

The user performed the IPL-only comparison and reported success. Two fresh
prototype-USA-IPL EK runs are analyzed in
[N64DD_USA_IPL_SUCCESS.md](N64DD_USA_IPL_SUCCESS.md). The instructions below are
historical; they are not a request to repeat the successful test.

Identify the installed debug-package version before deciding whether to replace
it. Compare one EK Start with the prototype USA IPL, if available, changing only
that IPL selection and preserving the USA cartridge, translated NDD, engine,
plugins, driver and saves. The existing log records a Japan-named IPL alongside
a USA retail-region disk; this is a controlled remaining-variable check, not a
proven cause. Preserve the Japanese IPL file rather than modifying it.

Record whether EK displays a menu or remains black from startup, and retain
Android logs across the new launch. If unchanged, the next instrumented build
must establish the actual boot/guest failure boundary; the old JP fixed-PC
probes or the still-partial C3 package cannot silently be declared sufficient.