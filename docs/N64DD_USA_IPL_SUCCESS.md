# Native EK working control: USA cartridge, USA disk, prototype USA IPL

## Result (2026-09-17)

After changing the selected IPL from the Japan-named file to the prototype
USA-named file, the user reports that EK “seems to work perfectly.”
No replacement APK, emulator code patch, driver change or save deletion was
requested for this comparison. Preserve this working configuration.

The new Android log independently establishes two fresh DD-enabled launches,
disk block-manager activity and sustained nonzero audio-command submissions.
It does not independently show the screen or measure audible output.
This is a configuration recovery, not proof of a new emulator repair or an
exact guest instruction implementing the incompatibility.

## Capture and launch identities

Input: `attached_assets/0_logcat_1789666224187.txt`, 71,384,765 bytes,
70,844 lines, SHA-256
`04af7811adf172115c857e73fbe00fc18c88d52d971be312c915a281cd0323ac`.
Do not publish the raw Android log; it contains unrelated application data.

The earlier failed comparison is documented in
[N64DD_USA_CAPTURE_ANALYSIS.md](N64DD_USA_CAPTURE_ANALYSIS.md).

| Setting | Failed EK run | Working comparison |
| --- | --- | --- |
| Cartridge | USA, MD5 `753437D0D8ADA1D12F3F9CF0F0A5171F` | Same |
| Disk | English-named NDD, USA, `development=0`, region `2263ee56`, ID `45 46 5a 45 00 00` | Same logged identity |
| IPL selection | Japan-named, 4 MiB | Prototype USA-named, 4 MiB |
| CPU / RSP | Dynamic Recompiler / Parallel RSP | Same |
| DD / boot | Enabled / CART, combo cart boot | Same |
| Autoload request | False | False in both new EK launches |
| Count-per-op | 1, denominator 0 | Same |
| DDSTART9 | Retain non-link terminators, preserve delay slot | Same |

IPL/disk byte hashes and APK version/source hash are not logged. Filenames and
disk header identity are not full content identity. The installed package remains
the debug package; neither ROM metadata `Version: 1449` nor its package suffix
establishes v336. No publication or release-build claim follows from this test.

The new file also contains an earlier DD-off base run (PID 6206). Separate it
from the two EK processes:

| Evidence | First EK: PID 6584 | Second EK: PID 6746 |
| --- | --- | --- |
| Prototype USA IPL copied | line 3041 | line 21482 |
| Fresh DD launch settings | line 3054 | line 21495 |
| USA cartridge MD5 | line 3111 | line 21550 |
| USA disk region/ID | lines 3132–3133 | lines 21568–21569 |
| CART boot selection | line 3139 | line 21575 |
| Effective dynarec | line 3392 | line 21762 |
| DDSTART9 active | line 3396 | line 21830 |
| First DD block-manager record | line 3500 | line 21935 |
| Normal R4300 finish | line 21146 | line 70784 |

## What changed in native execution

Unlike the failed run's limited startup exchanges and sparse interrupt samples,
both new EK processes emit 512 DDSTART4 block-manager records, including sector
advancement, then sustained RSP task/command activity. The record limits prevent
a complete disk-transfer reconstruction; this is not a bit-exact sector audit.

Streaming counts partitioned by PID find:

| Recorded evidence | PID 6584 | PID 6746 |
| --- | ---: | ---: |
| DDSTART12 command entries | 2,288 | 7,343 |
| Entries with nonzero words | 2,288 | 7,343 |
| Entirely zero command entries | 0 | 0 |
| DDSTART13 records | 2,049 | 2,049 |

Each DDSTART13 count includes 2,048 launches plus the budget-exhausted record;
it is not a claim of only 2,048 tasks in the run. DDSTART11 also has bounded
fetch coverage. The historical all-zero-at-submission symptom is absent from
these 9,631 recorded command entries, not proven impossible at every instant.

The first core runs for about 43 seconds and the second for about 126 seconds.
Both exit normally after UI shutdown. There is no recorded native fatal signal
or Android fatal exception for these EK processes.

Second-run DDSTART5/6/7 records at lines 41161–41171 are a sampled context and
unavailable structured diagnosis due to a PC-signature mismatch. They do not
establish a new fault or an exact executing/writing PC. Do not use old fixed-PC
diagnostics to contradict the working result without revalidating their scope.

## Interpretation and remaining acceptance

### Subsequent published-v336 device comparison

The user then tested the actual **release-tag v336**, not the working beta.
The supplied screenshots (`image_1789668745497.png` and
`image_1789668756866.png`) show the 3D N logo followed by:

> F-ZERO X cannot be played with this disk alone.
> Please turn off the NINTENDO 64 Control Deck and insert the F-ZERO X Game Pak.

This supplies the previously missing published-release failure result. The
public APK identifies itself as `3.0.335 (beta) dc955483`, code 335; the working
installed beta was reported as `3.0.336 (beta) 5ebdc720`.

Source comparison establishes that `dc955483` selects the DD IPL as the boot
source whenever one is loaded. The working beta and cleanup candidate retain
the explicit cart-first override for a DD-enabled real-cartridge combo;
direct-NDD and DD-disabled routes retain their baseline boot selection. This is
a functional difference consistent with the observed disk-alone path, not a
diagnostic-print dependency or proof of an exact guest corruption writer.
The test does not isolate every other core/RSP fix's necessity.

### Working-beta interpretation

The IPL-only intervention, matching logged settings and two fresh launches
strongly support IPL selection as the remaining blocker in the preceding
USA-cartridge test. The old Japanese-cartridge/US-texture-offset evidence is
separate: this result does not identify the exact guest region-check instruction
or retroactively invalidate independently demonstrated core defects.

The metadata/reference review still governs the cartridge requirement:
this English release requires USA. The IPL is now established by the working
Mupen comparison rather than borrowed from assumptions about another emulator.
Do not generalize this triple to every translation or every DD game.

Native functionality is user-reported as working, with supporting disk/RSP
progress. Plain-cart success was already reported in the earlier control.
Cached interpreter did not run; no dispatcher pass is claimed or needed as a
workaround. No new emulator correction is justified by this result.

Writable-cart persistence and a deliberate native disk edit/save/reopen test
remain unverified. The logged emulator autosave completion is not proof of either.
Keep these as explicit remaining acceptance work, alongside archiving the exact
working APK identity. Existing unreviewed diagnostic changes are not certified
or authorized for publication by this working installed-build result.