# P08 task-entry observation — native capture, 2026-09-15

## Scope and provenance

Analysis of the privately supplied `p08-capture-20260915-124713.zip`.
Do not publish the capture itself. Native source changes were imported from
`3d868e317`; APK SHA-256:
`813d18f116ecf5cd0d25a926c812be587beae9bcb84e67dce81832141331cbc5`.
The installed APK's displayed version retains an older embedded suffix;
that suffix is not the diagnostic source revision.

Captured `logcat-threadtime.txt` SHA-256:
`b996eb2cec5f66f02ee35536bfe2b1fd7e80dc1dd5b04bc94d1ab6e583619b4c`.
References below are one-based lines in that file. Correlated records are
from process 16016, thread 16052. The manual launcher records game identity
as unverified; configuration and execution observations must come from logs,
not from its intended tile label.

All manifest-listed files other than the manifest itself match their hashes.
The helper erroneously included its own actively written hash manifest;
that self-entry does not validate. This is a packaging defect, not evidence
that the independently hashed log changed.

## Direct and derived observations

The log contains 880 DDSTART12 task-entry records. These are observations in
the core's audio-task path before calling the RSP plugin; do not equate this
boundary with the guest's original task-submission instruction.

| Time (device log) | Entry line | Buffer | Size | Nonzero words / total | Subsequent matching fetch generation |
|---|---:|---|---:|---:|---:|
| 12:47:39.969 | 7859 | 0x00411910 | 416 | 101 / 104 | 1508 |
| 12:47:40.006 | 7871 | 0x00411910 | 416 | 101 / 104 | 1510 |
| 12:47:40.026 | 7883 | 0x00411910 | 416 | 0 / 104 | 1512 |

The final entry hash is `0x8d0350be04626145`. Its explicit zero nonzero-word
count establishes that the entire observed 416-byte range was zero then;
this conclusion does not depend solely on hash equality.

Line 7884 is an offset-zero, 64-byte watched fetch:

- `entry_generation=1512`, `fetch_generation=1512`.
- Source `0x00411910`, task descriptor buffer/size `0x00411910 / 0x1a0`.
- Sixteen payload words; hash `0x88201fb960ff6465`, matching sixteen zero
  words under the observer's word-wise multiply-then-XOR FNV.
- Sampled first/last payload words are zero.

DDSTART12 has no explicit generation identifier. Correlation here uses the
immediately following watched fetch in the same PID/TID, matching task
pointer/size, equal fetch and entry generations, and no intervening entry.
Do not pair records by ordinal position alone.

The ensuing four trigger records for generation 1512 have source zero,
RD_LEN `0xffffffff`, corrected policy enabled, zero actual IMEM writes, and
unchanged before/after IMEM hash `0x3aaaf0f5f121410e`. The trigger budget then
exhausts. Further missing trigger records do not establish that execution
stopped or that no further transfers occurred.

## What this narrows

The failing buffer was already zero at the core's pre-RSP observation.
It was not first made zero solely during that invocation between this
observation and the watched fetch.

The previous use of the same physical range had nonzero contents about
20 milliseconds earlier in the log. This bounds a sampled transition, not
an exact writer, generation lifetime or hardware-cycle interval.

Still unresolved:

- Whether the next buffer generation was built, cleared, or reused improperly.
- Which CPU store, DMA or other memory operation changed the range.
- Whether a wrong descriptor selected an empty/stale buffer.
- Any independent decode-side defect.

The zero-at-entry result does not establish a producer bug or justify a
runtime workaround.

## Capture limitations and next step

The 60-second observation interval preceded the attempted memory capture.
Watchdog launch timed out before the helper sent SIGSTOP; no new RDRAM or
RSP binary snapshot was obtained. The timeout alone does not establish the
device shell's internal cause. No further memory-capture retry is required
to establish the entry-time zero-buffer result.

Next experiment: trace the last relevant writes and generation/descriptor
lifecycle for the two command buffers before the failing entry. Coverage must
include dynarec's inline fast stores; an unwired or slow-path-only writer ring
cannot exclude CPU writes. Capture bounded exact writer events with address,
width, old/new value and valid guest PC attribution, and relevant DMA paths.
Choose a minimal correction only after the responsible operation or improper
reuse is demonstrated.

This completes the narrow entry-state measurement, not the native DD repair,
menu/audio acceptance, or DD-disabled/writable-cart persistence regressions.