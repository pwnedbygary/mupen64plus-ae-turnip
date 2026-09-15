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

## Follow-up: SP launch observation

The private `p08-capture-20260915-132331.zip` completed successfully with
memory capture intentionally disabled. All 66 manifest-listed payloads
validated. There are 880 DDSTART13 launch observations and 880 DDSTART12
core entry observations.

At log line 8697 (13:23:58.169, PID/TID 20977/21012), launch sequence 880
records a valid 416-byte buffer at `0x00411910`, zero nonzero words out of
104, and hash `0x8d0350be04626145`. HALT/BROKE clear from status `0x43`
to `0x40`; neither DMA_BUSY nor DMA_FULL is set.

Line 8698 observes the same zero buffer at core entry. Line 8699 fetches
64 bytes from offset zero with matching entry/fetch generation 1512 and
zero-payload hash `0x88201fb960ff6465`. Launch sequence numbers count only
the qualifying audio launch observations, not all RSP generations: 880
must not be equated numerically with generation 1512.

This places the zero-buffer state at the observed guest SP launch write,
before the core entry observer. It still identifies neither the responsible
writer nor improper reuse. Tracing must retain history between launches and
cover CPU stores emitted inline by dynarec; another launch hash alone will
not establish either cause.

## Bounded writer probe implementation

### Positive writer capture and transition context

The private `p08-capture-20260915-144814.zip` completed; all 66 manifest
payloads validated. DDSTART14 retains 32 consecutive successful word stores
to KSEG0 `0x80411a30..0x80411aac`, covering the final 128 bytes of the
416-byte buffer. All after-values are zero; 31 before-values are nonzero.
The record PCs are `0x80747278/7c/80/84/88/8c/90/98`; `0x80747298` is
consistently a delay-slot store. These are routed-store observations, not
sampled execution PCs.

The records carry launch-owner sequence 878, not RSP generation 878.
Launch 878 has 101/104 nonzero words; launch 880, core entry and RSP fetch
generation 1512 reproduce the zero buffer. Log timestamps on writer lines
are flush times, not individual write times. The 32-entry ring reports
592 overwritten older events and lifetime replacement counters; it does
not provide the complete clearing history or prove which operation cleared
the first 288 bytes.

The user reports menu music and garbled menus, followed by a dissolve,
audio cutoff and black screen when the automatic gameplay demonstration
starts. Manually selecting a ship and starting a race appears to reach a
similar black screen. The base cartridge works on this emulator. These are
user observations, not independently verified equivalence of both failure
paths. Scene-transition clearing may be legitimate: caller, range, ownership
and later reuse must be established before suppressing or modifying it.

### Static zero-fill routine cross-check

### Native caller-context follow-up

The private `p08-capture-20260915-153756.zip` completed and all 66 manifest
payloads validated. First/latest successful zeroing contexts agree on
sign-extended guest RA `0x800aea14`, SP `0x80796c10` and loop A3
`0x8058b540`. First: store PC `0x80747288`, address `0x80411910`,
A0 `0x80411920`. Latest: PC `0x80747284`, address `0x80411aac`,
A0 `0x80411ac0`. These address differences match the earlier static
store offsets -16 and -20. A1 is zero in both contexts.

A1 zero is compatible with an active bulk clear: the static routine
subtracts the rounded 32-byte bulk length from A1 before entering the
bulk loop. At these stores it is a remainder, not the original length.
Do not reject coherent store context on that basis or infer zero-length
entry arguments.

RA minus eight yields candidate link site `0x800aea0c`. Both corrected
older windows contain `jal 0x80747240` there, with `or a1,s0,zero`
in the delay slot. The preceding static code loads A0 from `[sp+0x3c]`
at `0x800ae9f0`, compares a loaded word against `0x4d494f30` (MIO0),
and branches to the clear call on mismatch. On the other path it calls
`0x8072e3b0`. Earlier A0 assignments before an intervening call are not
the clear call's immediate argument setup.

This is consistent with the live retained return address and a resource
loading/clearing branch, but it does not prove the fresh caller opcodes,
header value, branch decision, original clear base/length, or the purpose
of that branch. The fresh trace still has no executed call-site record.
Next evidence should capture the call/entry arguments and compared header
from the current execution, preserving the distinction from archived
static instruction bytes.

The earlier P07 archive contains full 8 MiB windows. In its `1202` and
`1427` windows, guest `0x80747278..0x80747298` contains eight
`sw zero,offset(a0)` instructions with offsets -32 through -4, with the
last in the delay slot of `bne a0,a3,0x80747274`. At `0x80747274`,
`addiu a0,a0,32` advances the pointer. Thus this is a **32-byte**, not
36-byte, static zero-fill loop consistent with the observed writer PCs.
The surrounding code handles alignment and smaller remainders.

Do not infer overlay evolution from the `113959` window's different words
at the same file offset. That window places the known ucode marker at
`0x769e60`, rather than `0x768e60`. Its routine bytes at file offset
`0x748240` exactly match the corrected window at `0x747240`: the observed
difference is a 0x1000 coordinate displacement.

A scan of the corrected older window finds 28 aligned words encoding JAL
to candidate routine entry `0x80747240`. These are static call candidates,
not proof that any one executed in the current transition. Archived
instruction bytes are not the fresh run's translated instruction stream.
Next evidence must identify the actual caller and clear arguments using
coherent guest register state, not stale core register arrays while live
values remain in dynarec host registers.

The follow-up probe is implemented in the separate `dd_cmd_watch` module. It
keeps recent events independently for the two most recently identified
command-buffer ranges, accepts only their unmapped KSEG0/KSEG1 writer
aliases, and updates those ranges only at valid audio-task launch boundaries.
It does not reinterpret a TLB virtual label (or physical label) as a matching
writer. Non-audio launches therefore do not disarm an active range. A zero
command-buffer launch flushes the preceding range's bounded records and
summary before its generation is updated, including a no-event summary.

ARM64 dynarec blocks compiled while startup diagnostics and DD policy are
enabled emit a dynamic route predicate for aligned SB/SH/SW/SD stores. The
predicate checks
the current DD policy and range state on every execution; disabling policy
therefore fails closed even for previously compiled blocks. A matching store
uses the established write stub, while the slow aligned helper snapshots
validated before/after RDRAM values only after a successful write. Events
include the effective KSEG writer address, width, values, writer PC,
delay-slot bit, and `ROUTED_ALIGNED` source.

The first pass intentionally reports coverage gaps for TLB-routed stores,
unaligned SWL/SWR/SDL/SDR and storelr fragments, DMA writers, and page-span
delay-slot entry blocks. These paths are not routed or inferred from aligned
events. Host contract coverage is in
`mupen64plus-core/upstream/tools/tests/dd_cmd_watch_test.c`; it exercises
rolling overwrite,
non-audio gaps, policy-off fail-closed behavior, and zero-only flushing.

### ARM64 live store context

The routed ARM64 write stub now captures the live dynarec values before it
calls the existing `write_*_new` helper. When the selected allocator mappings
do not overlap the ABI argument registers, it extracts them directly from
host registers; otherwise it spills only the selected mappings (`ra`/r31,
`sp`/r29, `a0`/r4, `a1`/r5 and `a3`/r7) to the hot state before materializing
the two halves of each value reported valid. Mapped values therefore win over
a stale architectural array; known 32-bit values receive their sign-extended
upper half, and an allocator-marked unmaterialized half is marked invalid
rather than guessed. A JAL/JALR (or branch-and-link) delay-slot store also
marks `ra` invalid because link materialization may be delayed; this changes
only observer validity, not guest link execution. The known-zero loop BNE
delay-slot case is unaffected.
The C recorder receives the exact store instruction PC and delay-slot bit
from the stub, not a corrected or sampled execution PC.

Context is attached only after a successful nonzero-to-zero write. Each
watched slot retains the first qualifying context and the latest qualifying
context independently of its 32-event ring. A same-base buffer-size change
may carry the latest record when its address and width remain inside the new
range; the record keeps its original generation and range metadata. Context
output is separately capped at 16 lines and labels `store_a0`, `store_a1` and
`store_a3` as store-time values. `entry_arguments=not-inferred` is
intentional: the diagnostic never treats the loop-mutated `a0`/`a1` as entry
arguments and never reads `g_dev.r4300.regs` to reconstruct them.

The live-context route remains limited to aligned, unmapped KSEG stores in
ordinary compile blocks. TLB-routed, unaligned/SWL/SWR/SDL/SDR,
storelr-fragment, DMA, and page-span delay-slot entry blocks remain explicit
coverage gaps. Page-span compilation carries its architectural delay-slot
marker in the low bit of the compile address, while `pagespan_ds()` assembles
through `regs[0]` with `is_delayslot` clear; routing is therefore disabled for
that compile rather than fabricating context. This is an unknown-branch
context gap, separate from the known-zero loop BNE delay-slot case. The host
contract and allocator/codegen fixtures cover first/latest retention,
nonzero-to-zero qualification, same-base resizing, mapped-value precedence,
32-bit extension, link-delay-slot `ra` fail-closed behavior, generated ARM64
argument words and exact PC provenance, plus page-span route suppression
without a context stub.

### Executed load/clear decision probe

The next diagnostic is implemented as a separate, bounded `DDSTART15` probe
for the explicit per-game DD diagnostics path. It does not change the caller
branch, the JAL target, guest register writeback, memory handlers, or the
clear routine. ARM64 dynarec emission recognizes only the compiled
instruction words `jal 0x80747240` at `0x800aea0c` (`0x0c1d1c90`) followed by
`or a1,s0,zero` (`0x02002825`). The generated call is placed after the
architectural delay-slot sequence and after the JAL link has been
materialized. Consequently its `a1` is labeled
`a1_provenance=compiled-delay-or-a1-s0`; no pre-delay `a1` is presented as
the clear argument.

The call record carries the exact compiled call PC, opcode, delay opcode,
target and compile generation. It requires coherent live allocator values
for `a0`, `a1`, `t8`, `t1`, `ra` and `sp`. Selected mappings are spilled to
the diagnostic hot-state slot only when needed, then reloaded before the
observational C call; an explicitly unmaterialized or dirty-but-unmapped
half rejects the probe.
The wrapper validates `t8` as an aligned unmapped KSEG0/KSEG1 address and
reads exactly one word through the existing bounded direct-RDRAM reader.
The record therefore reports both `source_header_address` and the
validated `source_header_value`, plus the live `comparator_t1`. A missing
mapping, non-KSEG address, failed bounds check, disabled callback, disabled
DD policy, or source-word mismatch emits no executed-call claim.

An independent entry probe is emitted only for a non-page-span compiled block
starting at `0x80747240`. It records that block's compiled first word and
requires `ra & 0xffffffff == 0x800aea14`, in addition to coherent entry
`a0/a1/ra/sp`. Its call-site opcode/delay/target fields are fixed compiled
provenance, so a current-RDRAM snapshot cannot manufacture a match. The
entry record is the only output labeled `original_arguments=callee-entry`;
the call record remains an after-delay observation. A matching
`0x80747240` address found internally in an already compiled block is an
explicit fail-closed entry-coverage gap; the post-delay callsite probe is the
primary executed-path observation and block generation is not broadened.
Page-span blocks and link-delay-slot uncertainty fail closed.

The probe has an independent eight-record session budget and remains bounded
by the existing diagnostics and runtime DD policy gates. Host coverage adds
production emitter machine-word fixtures for exact-word rejection and
hot-state field stores, plus recorder records for the validated call and
callee-entry forms. ARM64 syntax verification remains the NDK
26.1.10909125 check. This is a specialized address diagnostic, not a
runtime fix or a claim that the compared MIO0/header branch is understood;
device evidence is still required.

### DDSTART15 native-crash regression triage

The private `p08-capture-20260915-175156` archive was extracted at
`/tmp/p08-loading-crash/p08-capture-20260915-175156-run-j5y5gu90`. Its
`logcat-threadtime.txt` reports SIGSEGV `SEGV_MAPERR` at fault address
`0x00000000f43d0500`, with `x19=0x00000000f43d04e8`, `x21=0x0000001bd13ac000`,
and core PC offset `0x000000000007c50c`. The matching unstripped
`mupen64plus-core/build/intermediates/cxx/Debug/53u1r1q1/obj/local/arm64-v8a/libmupen64plus-core.so`
has Build ID
`04a0e025ddb0eeb7673ee8a2102957b15b804684`.

NDK 26.1.10909125 LLVM symbolization maps PC `0x7c50c` to
`dd_dynarec_capture_probe+0x48`, where the instruction is
`ldr w8, [x19, #0x18]` (`b9401a68`). Offset `0x18` is
`dd_cmd_watch_probe_snapshot.kind`; the fault is therefore the first
snapshot-field dereference, not a bad hot-state field offset. The generated
probe stores fields through full host addresses, then the final
`emit_movimm(base, ARG1_REG)` passed the diagnostic snapshot address through
the ARM64 emitter's `u_int` parameter. On this 64-bit process that truncated
the callback argument to `0xf43d04e8`; adding `0x18` produces the logged
`0xf43d0500`. `x21` is consistent with the separate generated-code mapping,
not the corrupted snapshot pointer.

The layout audit is consistent with that conclusion. With `NEW_DYNAREC=4`,
the current C layout puts `branch_target` at `0x4e0`, the probe slot at
`0x4e8`, and `probe.kind` at `0x500`; the current generated
`asm_defines_gas.h` independently reports `pc=0x540`, `fake_pc=0x548`,
`rs=0x608`, `mini_ht=0x628`, and `memory_map=0x828`, which include the
probe slot's size before those later members. `mupen64plus-core.mk` generates
that header by compiling `asm_defines.c` with the selected ARM64 target and
`NEW_DYNAREC=4`, then extracting the object markers with `gen_asm_defines.awk`.
The new probe stores use C `offsetof` directly, so neither a stale generated
header nor an ABI-specific hot-state offset explains the fault.

The minimal repair replaces both probe callback pointer materializations with
`emit_loadlp((uintptr_t)base, ARG1_REG)`, preserving all 64 bits via the
existing literal-pool mechanism. No guest branch, hot-state layout, memory
handler, or diagnostic gate changes. The production ARM64 fixture now
requires an `LDR`-literal for the callback pointer and checks that the
materialized literal equals the complete `&hot_state.dd_cmd_watch_probe`
pointer; the pre-fix 32-bit immediate sequence fails this discriminating
fixture. This is source/build evidence only: no repeat crash run, device
validation, APK, commit, or push is claimed here.
