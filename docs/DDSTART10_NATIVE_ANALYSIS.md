# DDSTART10 native result: suspect RSP instruction-memory contents

## Capture integrity and execution

Archive: `ddstart10-rsp.8v2Zy2_1789276323186.zip`.
SHA-256: `e2c523b2c2d6ab263f083e10dfde0f090d2f513c017a4fa5c31cbe5e70020bf6`.
Raw uploads remain local.

The Mac helper verified the expected DDSTART10 APK hash, used `install -r`,
identified fresh `capture.ZveDAv` against four pre-existing directories, and
retrieved it after its own terminal status. The capture completed at
01:05:58 UTC−04:00 on 2026-09-13; both debuggerd calls returned 0.
The text in `adb-errors.txt` is an ordinary successful pull summary, not a
capture failure.

The pinned process was PID 15024/start time 659973; the emulation thread was
TID 15058/start time 660010. Startup explicitly reported `support64dd=true`
and `Starting R4300 emulator: Dynamic Recompiler`. DDSTART9's boundary
correction remained present.

The thread's user ticks rose from 9,437 to 14,003, with system ticks unchanged
at 91. At measured CLK_TCK 100, that is 45.66 user CPU seconds. The associated
reads were approximately 46 seconds apart. Scheduler runtime independently
rose from 95,529,048,958 to 141,184,284,046 ns, a 45.655235088-second delta.
Both sampled states were running. This confirms substantial CPU consumption,
not a thread sleeping throughout the observation window.

## Native-to-RSP correlation

First stack:

- Anonymous allocation base: `0x70a8632000`.
- Relative PC: `0x721cc`; absolute PC: `0x70a86a41cc`.
- Matching JIT record: 97, allocation `0x70a86a4000..0x70a86a5000`.
- Compile input: IMEM `0xf60..0xfff`, 40 instruction words.
- Existing region hash: `0x144aff6ca91b3edd`.

Second stack:

- `libmupen64plus-rsp-parallel.so`, relative PC `0x2d10c`,
  `RSP::JIT::CPU::get_jit_block(unsigned int)+20`.
- Caller in the same anonymous reservation at relative `0x80`, the dispatcher.
- Static library Build ID matches preserved DDSTART10 symbols:
  `d6067d8280dfe01ebea0e3080f2eb8bd47f9e183`.

These are sampled locations, not a complete execution trace. The first sample
identifies the active allocation/guest region, not the particular guest
instruction corresponding to native offset `0x1cc`.

## Suspect compile input

Records 94–99 appeared at 01:03:24.094–095. The later records include:

- Region 96 at IMEM `0xb20`: scalar code constructing and storing graphics
  command words.
- Region 97 at IMEM `0xf60`: similar code, including a load based on the
  KSEG0 address `0x8009e1b4`.
- Region 98 at IMEM `0x000`: four `ffffffff` words, extensive zeros, then
  more scalar construction/store sequences.
- Region 99 at IMEM `0x200`: more graphics-command construction and a
  conventional stack-frame return sequence.

These patterns strongly resemble R4300 game rendering code rather than the
expected RSP microcode. In particular, RSP scalar loads mask addresses into
12-bit DMEM; CPU-style KSEG0 table accesses do not have their CPU meaning there.
An earlier PC-0 region contained different, normal-looking RSP code.

No exact match was found in the older DDSTART7/8/9 diagnostic word windows.
Those captures do not contain the complete game image. Consequently the
classification is structurally strong, but no exact R4300 source PC or source
RAM address is established. Do not publish the raw instruction stream.

## Two diagnostic limitations discovered

1. **The full-IMEM diagnostic hash can become stale.** Internal RSP DMA marks
   dirty blocks. `CPU::invalidate_code()` refreshes `cached_imem` without
   invalidating DDSTART10's separate diagnostic hash. A later entry comparison
   can therefore see equal cached/live IMEM while retaining an older hash.
   Repeated task-entry hashes do not prove unchanged instruction memory.
2. **The region range is allocation capacity.** The code-size variable used
   for the recorded end is affected by allocator sizing. Earlier documentation
   incorrectly described it as exact emitted-code length. Treat it as the
   allocation range, not an instruction-by-instruction native map.

There were 108 emitted distinct task identities and no diagnostic exhaustion
markers. Nevertheless the last identity line at 01:03:23.671 is not necessarily
the invocation that stalled: repeated identities intentionally are not logged.
The stale-hash issue further prevents dating the write from task-entry hashes.

Thus this capture does **not** prove when instruction memory changed, which
DMA writer changed it, or that one particular invocation never returned.

## Fault boundary and next observation

Both CPU-originated core SP DMA and Parallel-RSP's internal CP0 DMA can write
instruction memory. There are no direct DMA records in this capture.

Source comparison exposes differences worth testing, not yet correcting:
Parallel-RSP uses 4-byte SP-address alignment, clamps a row at the 4 KiB
boundary, and advances/masks rows with 13-bit SP addressing. Core/CXD4 paths
are not interchangeable with that implementation. Raw source/destination,
length/count/skip and actual written ranges are required to establish which
rules were exercised.

DDSTART11 should retain all execution semantics while:

- Refreshing diagnostic hashes after dirty-block cache refresh.
- Labeling allocation ranges accurately.
- Recording both IMEM DMA writer paths, including transfers that start in
  DMEM but cross into IMEM under the existing implementation.
- Capturing raw/derived transfer parameters, actual copied payload evidence,
  before/after IMEM evidence, and entry-time task descriptor context.
- Giving IMEM observations independent budgets so ordinary DMEM traffic and
  DDSTART1 startup limits cannot hide the later transition.
- Reporting an unknown MTC0 site honestly unless it is supplied directly by
  the compiler, rather than substituting `state.pc`.

No alignment, clamp, wrap, cache-reuse, scheduling or timing correction is
justified yet. The overall task remains in progress, including native gameplay,
audio, plain-cart and writable-cart persistence validation.