# P07 — command evidence and the next observation

Analysis package (work packages P07). No emulator change. Baseline: P06 native
decision record (`docs/HANDOFF_NEW.md`, commit `2450922a2`); the DDSTART11
static reconstruction (`docs/DDSTART11_NATIVE_ANALYSIS.md`); the P06 run's
logcat (local-only, `.fzxwork/p06-capture/logcat-p06-route.txt`, sha256
`72c70668…`) including 106 `jit_compile_input` records that let the audio
microcode be reconstructed from the run itself.

## 1. Reconstructed microcode (exact, from the run's compile inputs)

The P06 run's `DDSTART10 RSP jit_compile_input` records carry the IMEM words
the JIT compiled. Reconstructing them into an IMEM image covers 716/1024
words. Records span multiple microcode generations (347 writes differed from
the previously stored value at the same address; 154 addresses carried more
than one distinct value), so only words from the audio-task generation
(records timestamped 08:02:51, immediately before the suspect burst) are
used below. RSP `jal` encodes a 26-bit word target whose low 12 bits select
the IMEM PC; the decoded targets below are IMEM offsets.

### 1.1 Call site 1 — the `0xf54` block (trigger record 8)

| IMEM | word | instruction | note |
|---|---|---|---|
| `0xf4c` | `0d00069b` | `jal 0xa6c` | calls the size/source-derivation helper |
| `0xf50` | `00000000` | `nop` | delay slot |
| `0xf54` | `22e10000` | `addi at, s7, 0` | **selects `at = s7`** (DMEM base) |
| `0xf58` | `0d0006b5` | `jal 0xad4` | calls the DMA-launch helper |
| `0xf5c` | `2063ffff` | `addi v1, v1, -1` | **size decremented in the delay slot** |
| `0xf60` | `8c260004` | `lw a2, 4(at)` | reads a per-DMA counter at `s7+4` |
| `0xf64` | `20c60001` | `addi a2, a2, 1` | increments it |
| `0xf68` | `ac260004` | `sw a2, 4(at)` | writes it back |
| `0xf6c` | `ac3b0008` | `sw k1, 8(at)` | stores `k1` at `s7+8` |
| `0xf70` | `ac3e000c` | `sw fp, 12(at)` | stores `fp` at `s7+12` |
| `0xf74` | `3346ffff` | `andi a2, k0, 0xffff` | masks `k0` low 16 bits into `a2` |

The DDSTART11 static reconstruction describes the same structure (in
substance: "The block at `0xf54` selects destination `at = s7`, calls the
DMA helper at `0xad4`, and decrements `v1` in the JAL delay slot"; "IMEM
`0xf4c` calls the helper at offset `0xa6c`"). The reconstructed block adds
per-DMA bookkeeping at `s7+4/8/12`.

### 1.2 Call site 2 — the `0x0c4` block (trigger records 9–11)

| IMEM | word | instruction | note |
|---|---|---|---|
| `0x0c4` | `23e50000` | `addi a1, ra, 0` | saves the incoming return address |
| `0x0c8` | `001c1020` | `add v0, zero, gp` | move `v0, gp` (SPECIAL, funct 0x20) |
| `0x0cc` | `23630000` | `addi v1, k1, 0` | **size candidate = `k1`** |
| `0x0d0` | `2064ffc0` | `addi a0, v1, -64` | compare `k1` against 64 |
| `0x0d4` | `18800002` | `blez a0, +2` | branch when `k1 <= 64` |
| `0x0d8` | `200102f0` | `addi at, zero, 0x2f0` | delay slot: **`at = 0x2f0` fixed** |
| `0x0dc` | `20030040` | `addi v1, zero, 0x40` | taken only when `k1 > 64` |
| `0x0e0` | `207e0000` | `addi fp, v1, 0` | move `fp, v1` |
| `0x0e4` | `0d0006b5` | `jal 0xad4` | calls the DMA-launch helper |
| `0x0e8` | `2063ffff` | `addi v1, v1, -1` | size decremented in the delay slot |
| — | — | return `0xec` | matches `ra = 0xec` in records 9–11 |

## 2. Register evidence at the launch (exact, P05 capture)

GPR values from the four trigger snapshots, read from `gprs_full`
(0-based indices: r1=at, r3=v1, r23=s7, r25=t9, r26=k0, r27=k1, r28=gp,
r31=ra):

| | rec 8 | recs 9–11 |
|---|---|---|
| `at` (r1) | `0x00000fb0` (copied from `s7` by `0xf54`) | `0x000002f0` (set by `0x0d8`) |
| `s7` (r23) | `0x00000fb0` | `0x00000fb0` (unused by this site) |
| `k0` (r26) | `0x00000000` | `0x00000000` |
| `k1` (r27) | `0x00000198` | `0x00000000` |
| `t9` (r25) | `0x00000000` | `0x00000000` |
| `v1` (r3) | `0xffffffff` (0 − 1) | `0xffffffff` (0 − 1) |
| `ra` (r31) | `0x00000f60` → call site 1 (`jal` at `0xf58`) | `0x000000ec` → call site 2 (`jal` at `0x0e4`) |
| `raw dma_cache` | `0x00000fb0` | `0x000002f0` |
| `payload_hash` | `0x98583d1b9766c8e5` | `0x9b3647be75aae946` (identical in all three) |

Both call sites issue the DMA with zero size (call site 1: `v1 = 0` derived
from `k0` by the `0xa6c` helper; call site 2: `v1 = k1 = 0` through the
branch path) and zero DRAM source (`t9 = 0`; the `0xad4` helper's source
register). The records 9–11 repeats carry identical operands and identical
payload hashes, i.e. no source advance between them (record 8 is a one-shot
first occurrence).

## 3. Compile history of the helper regions

`0xf4c/0xf54/0xf60` were committed at 08:02:51 (the audio-task
generation); `0xa6c` and `0xad4` have records only at 08:02:36, and the
10-word `0x0c4` block (the one records 9–11 execute) was likewise compiled
at 08:02:36 and reused at the audio generation without a new commit, while
a different 2-word `0x0c4` variant was committed at 08:02:40 for the route
generation. The capture begins at 08:02:35, so absence of earlier records
proves nothing by itself. Consistent interpretation (candidate, not
evidence): the reused regions were byte-identical across the audio ucode
reloads, so the JIT's compare-and-recompile found no change there. The frozen-memory dump below resolves this directly by comparing the
live IMEM against the RDRAM ucode image. `existing_region_hash` is not used
as evidence here (it records the compiled block's own hash).

## 4. Evidence table

| Claim | Status | Artifact | Limits |
|---|---|---|---|
| Call site 1 decode (`0xf4c→0xa6c`; `0xf54→0xad4`; delay-slot decrement; `s7` bookkeeping) | EXACT | reconstructed compile inputs (08:02:51); DDSTART11 static analysis; `ra=0xf60` in record 8 | decode of captured words only |
| Call site 2 decode (`0x0c4` block: `k1`-sized, `at=0x2f0` fixed, `jal 0xad4`) | EXACT | reconstructed compile inputs (the 08:02:36 10-word `0x0c4` record, reused at the audio generation); `ra=0xec` and `k1=0` in records 9–11 | decode of captured words only |
| Helper inputs zero at launch: `k0=0`, `t9=0`; call site 2 additionally `k1=0` | EXACT | P05 GPR snapshots (records 8–11) | one audio task generation; 4 snapshots before budget exhaustion |
| Size register becomes `0xffffffff` (0−1) at both sites | EXACT | register captures + decodes | — |
| Destination base: `at=0xfb0` (site 1, from `s7`), `at=0x2f0` (site 2, fixed) | EXACT | `raw dma_cache` + GPR `at` | `s7` itself stays `0xfb0` in all four |
| Per-DMA counter at `s7+4` (site 1 only) | EXACT | block decode | counter value not captured |
| The command/opcode that set `k0`/`k1`/`t9`, its index in the 0x411910 buffer | UNKNOWN | — | needs command bytes / fetch path |
| Whether zero size/source is legal for this command | UNKNOWN | — | needs command identity + ABI |
| Whether the command bytes themselves are zero | UNKNOWN | — | needs the buffer dump |
| The role of the two blocks (chunk loader vs command handler) | CANDIDATE | counter/bookkeeping decode; two sites share the zero inputs | needs surrounding microcode |

## 5. Selected hypothesis for the next observation

Two structurally different call sites, with different size sources
(`k0`-derived and `k1`), both execute with zero size and zero source in the
same audio task. The leading hypothesis is that the zero values reflect
shared upstream state — the command (or the DMEM state derived from it)
genuinely carrying zero size/source — rather than two independent
microcode-block anomalies; the alternative, an upstream mis-load/decode
zeroing the inputs before the call, is less likely precisely because the
two sites consume different registers. The smallest discriminating
observation is the frozen process's own memory: the command buffer bytes,
the RSP memory (DMEM state including the counter, and the executing IMEM),
and the RDRAM microcode image.

## 6. Next observation (prepared, no build required)

### 6.1 Files and device installation

| Item | Path |
|---|---|
| Source helper | `tools/p07-frozen-memdump.sh` |
| Device installation path | `/sdcard/Download/p07-memdump.sh` |
| One-line menu launcher | `tools/launch-p07-memdump.sh` |
| Launcher device path | `/sdcard/Download/p07-run-as-root.sh` |
| Appended startup output | `/sdcard/Download/p07-launch.log` |
| Diagnostic directory | `/sdcard/Download/p07-memdump-<timestamp>/` (per run) |

Both files are already pushed. Select the **one-line launcher**
(`p07-run-as-root.sh`) in the vendor **Handheld Settings → Advanced → Run
script as Root** menu, not the multiline helper: that runner treats every
line of a selected file as a separate command, so the launcher exists to
start `/system/bin/sh` once with the complete helper as its script argument
(same convention as the DDSTART9 stack-capture helper). The helper exits
with a clear message when no frozen `:EmulationProcess` is present, so it is
safe to invoke before reproducing the freeze; re-run it while the freeze is
in place. No settings, SELinux, app data or saves are touched; the helper
only reads `/proc/<pid>/mem` at the fixed addresses below and writes under
a per-run `/sdcard/Download/p07-memdump-<timestamp>/` directory.

### 6.2 Windows read

The device script computes no host address with shell arithmetic: mksh's
arithmetic is 32-bit and `$((0x6fc2c5f000))` evaluates to -1027215360 on
this device (the failure that motivated the windowed design — every probed
address wrapped). It passes raw map hex strings to `dd` (which parses
64-bit skip values) and uses an awk helper for its hex additions and size subtraction
(double arithmetic is exact below 2^53). Discovery is OFFLINE on the
host from the dumped windows:

- `rdram-window.bin` — `[mem_base, mem_base + 8 MiB)`: covers every RDRAM
  structure of interest (gCurAudioTask at 0x771D68, the rspTask slots at
  0x6EEAA0/0x6EEAF0, both command buffers 0x411910/0x4132d0, the aspMain
  image at 0x768e60).
- `rspmem.bin` — `[mem_base + 0x04000000, +8 KiB)`: DMEM + IMEM. The app
  uses the full 512 MiB `mem_base` allocation and `mem_base_u32()` in full
  mode is identity (`mem = mem_base + guest address`; `MEM_BASE_MODE == 0`),
  so guest `MM_RSP_MEM` 0x04000000 maps directly; `MB_RSP_MEM = 0x5000000`
  is the *compressed*-mode offset and does not apply (the host test rejects
  a compressed-mode address in code).

Each run writes to a fresh timestamped directory; both windows' exact
lengths are validated and `dd`'s exit status recorded; the process is
briefly stopped (`SIGSTOP`, restored by an `EXIT` trap) for coherent reads —
a spinning RSP loop keeps rewriting DMEM/IMEM, and a frozen display does
not imply a static memory image — with the active-task pointer word sampled
before and after the stop as a race check, and `stopped_state` recorded
(with an explicit warning if the stop did not take effect). The
memory-read canary performs a real `/proc/<pid>/mem` read at the first
mapping's start keeping `dd`'s exit status, byte count and stderr (reading
`/proc/<pid>/stat` would not prove `/proc/<pid>/mem` access). mem_base is
located as the first qualifying `[anon:scudo:secondary]` read-write mapping
of at least 500 MB (sizes computed and compared in awk, because the device
`[` wraps decimal operands at 2^31); no candidate probing happens on the
device.

(The audio task's own RDRAM address was previously mis-stated as `0x7504f0`;
that address is `rspbootTextStart` 0x807504F0 in the running image, while
task_words[2] is the task's ucode pointer, not the descriptor's address. The
correct chain is `gCurAudioTask` 0x80771D68 → the active OSTask; the
`rspTask` slots corroborate it.)

## 7. Acceptance and stop check

- Acceptance (P07): the input is not yet fully explained, but a specific
  boundary is identified and the smallest missing observation is prepared —
  **met by this document plus the dump script**.
- Stop conditions checked: opcode/ABI still unknown (recorded as UNKNOWN, not
  guessed); no stale snapshot is used as proof; no emulator change was made;
  the historical yielded-graphics explanation is not substituted for this
  non-yielded audio task.

## 8. Native findings — the command is a zero word in a zeroed heap (2026-09-14)

Capture `p07-memdump-20260914-113959` (device, root; coherence from
`stopped_state=T` and both windows reading at exact lengths with rc=0 — the
curtask race-check sample used the pre-rounding base, so it does not
establish pointer identity across the stop). Local copy (never published):
`.fzxwork/p07-final/`. The host analysis compensates for a +0x1000 interior
offset of `mem_base` inside the scoped mapping (the mapping start is not
64 KiB-aligned); the script now rounds the mapping start up to the 64 KiB
alignment for future runs, and the missing IMEM portion of the RSP window
(the 8 KiB read started 0x1000 below `mem_base + 0x04000000`) is noted as a
gap that P08's runs will cover with the corrected base.

Direct observations (exact):

| Item | Value |
|---|---|
| `gCurAudioTask` (RDRAM 0x771D68) | `0x806EEAA0` = `rspTask[0]` (slot 1 also holds a type-2 audio task) |
| Active descriptor | type 2, flags 0, ucode `0x80768e60`/0x1000, ucode_data `0x80768e60`/0x1000, `data_ptr 0x80411910`, `data_size 0x1a0`; matches the P05 task words word-for-word except the two KSEG0-bit forms (stored virtual `0x80768e60`/`0x80411910` vs the P05 capture's converted `0x00768e60`/`0x00411910`) |
| Command buffer at 0x00411910 (0x1a0 = 416 bytes) | **every word is 0x00000000** |
| `rspTask[1]` buffer at 0x004132d0 (size 0x1c0) | also zeros |
| Zero region containing both buffers | bytewise zero run `0x3DA9EF`–`0x6ECA10` = 3,219,489 bytes (~3.07 MB; word-aligned start `0x3DA9F0`); the region ends exactly at `gAudioCtx` (0x806ECA10, jp/ek decompilation symbol; `0x6EEAA0 − 0x2090`), which is intact, as are the `rspTask` slots at +0x2090 |
| aspMain image at 0x768e60 (4 KiB) | present and non-zero |
| DMEM | populated (992/1024 non-zero words; microcode/audio data state intact) |

Boot-time context: `DDSTART3 PI DMA` records at 08:02:36 show cart-to-RAM
copies into `dram=0x400008…` — the same region that is now zero. The region
therefore had content earlier in the session; the zeroing happened between
boot and the freeze, and the bounded PI-DMA trace budget was exhausted at
boot, so this capture does not identify the zeroing event.

Offset validation (content check against an independent observation): the
aspMain image at RDRAM 0x768e60 begins `340a0fc0 8d420018 8d43001c 40803800`,
and `340a0fc0` is word 0 of the IMEM sample recorded by the P06 run's
observer (`imem_before_samples=[340a0fc0,…]`) — word 0 of the image at both
locations, which confirms the *shifted* interpretation of this window: the
unshifted look-up is file offset 0x768e60, whose words do not match, while
the image actually sits at file offset 0x769e60. A second content check: the
word at file offset 0x772D68 is `0x806EEAA0` = `gCurAudioTask` (guest
0x771D68). The interior offset is derived per run and never hard-coded; this
capture predates the rounding — its raw mapping start was unaligned and the
+0x1000 was compensated here in host analysis — while the current script
rounds each mapping start up to the 64 KiB alignment at capture time.
Descriptor and pointer-chain validation remains an offline host-side step
over the dumped windows; on-device the script only samples the active-task
pointer word as a race check.

Scope of this snapshot (do not over-read it): the capture shows the buffer
contents at the *frozen* state, and the P05 GPR captures independently show
the operands were already zero at *consumption* time
(`t9=0`/`k0=0` at the MTC0 launch). What this snapshot alone cannot
distinguish is whether the buffer was never written for this generation or
was written and then zeroed before consumption — no producer or clearing
operation is identified from this snapshot.

Conclusion: the consumed audio command is a genuine zero word. The
microcode's zero size/source extraction, the `0xffffffff` length register
and the repeated DMA-helper calls are consequences of consuming a
zero-filled command list; the loop then never terminates and the audio task
never returns. The divergence boundary is the producer/loader side: the
command list was never (re)generated, or the ~3 MB heap was cleared after
generation, before the task was consumed. The DMA correction remains
necessary (it is why this no longer corrupts IMEM) but is not the origin of
the freeze.

Selected next hypothesis and smallest discriminating observation (P08):
watch the bounded physical range `[0x3DA9F0, 0x6ECA10)` (or a narrowed
subrange around the two buffer addresses) for writers with task-generation
tracking, covering CPU stores (dynarec fast/slow, partial/unaligned), core
DMA and RSP DMA, and catch the event that zeroes the heap and its timing
relative to the task submission. The competing mechanisms to separate:
game-side heap clear without a completed rebuild; an emulated disk/cart load
delivering zeros (DD-specific); an emulator-side RAM clear.

## 9. Frozen-state recapture via run-as (2026-09-14, root-free)

External review asked for the 8192 RSP bytes at
`validated_mem_base + 0x04000000`, because the 11:39 window started 0x1000
low and contained DMEM only. The emulation process was still alive and
still spinning, so the window was recaptured without a device-side root
run: the debug package is debuggable and `run-as <package>` executes as the
app uid, which may read its own `/proc/<pid>/mem`.

Method (read-only; no emulator change, no root):
- `adb shell run-as <pkg> kill -STOP 11604` (same-uid signal for
  coherence), `dd if=/proc/11604/mem iflag=skip_bytes,count_bytes
  skip=<decimal> count=8192` through `adb exec-out`, then `kill -CONT`.
- Base: `/proc/<pid>/maps` is readable through `run-as` and shows the
  emulation buffer mapping at `0x6fc2c5f000`, with a `---p` region
  immediately below it (provenance not established); the buffer's
  64 KiB-aligned base is `0x6fc2c60000` (= mapping start + 0x1000),
  matching the derivation the root script now performs.
  Addresses used: mem_base `480009125888`, DMEM `480076234752`,
  `gCurAudioTask` `480016932200`. The pointer word read back `0x806EEAA0`,
  validating the base on-device before the windows were read.

Verified from the recaptures (local copies under `.fzxwork/p07-final/`,
never published; `rspmem.bin` 8192 B sha256 `c64edefa…`, `rdram-window.bin`
sha256 `7e1fbcdd…`):

| Observation | Evidence |
|---|---|
| Frozen IMEM is the aspMain image, bit-exact | FNV (multiply-then-xor over 1024 words, the P05 observer's function) of the IMEM half = `0x3aaaf0f5f121410e`, identical to the FNV of the RDRAM aspMain image at guest 0x768E60 and to the observer's in-process `imem_before_hash`/`imem_after_hash` (4 records each at 08:02:51). Head words `340a0fc0 8d420018 8d43001c 40803800`; the observer's four sample offsets (0x000/0x3fc/0xf60/0xffc) return exactly its recorded samples `[340a0fc0,4bfba08f,8c260004,00010001]`. |
| Window layout | The 8 KiB read at mem_base + 0x04000000 is `[DMEM][IMEM]`: CPU-side DMEM = 0x04000000, IMEM = 0x04001000 (in-process DD trace: `m=0x04001000` decodes to `dest={bank=IMEM …}`). The 11:39 capture's second half was DMEM; IMEM was its missing 4 KiB. |
| RDRAM is static while frozen | Guest-aligned diff of the 8 MiB window (guest 0..0x7FF000) between 11:39 and 12:02: 0 differing bytes. The zeroed heap, the zero buffer at 0x411910, `gAudioCtx` and both task slots are unchanged; the zero run is still exactly guest [0x3DA9EF, 0x6ECA10). |
| The process is still looping | utime advances ~100 ticks/s (one full host core) through 16:06; IMEM never changes across captures. |
| DMEM is continuously rewritten | 30 samples (10 at ~13 s, 20 at ~62 ms) plus the two coherent snapshots: DMEM[0:752] equals RDRAM[src:src+752] in 10 of the 32 states (nine matches end exactly at 752, one at 754) — the snapshots (guest 0x5EB98 at 11:39, 0x44C00 at 12:02) and eight samples (0x28C70, 0x82B08, 0xB6A38, 0xE8970, 0xFA928 in the 62 ms series; 0x92AC8, 0xACA60, 0x1068F8 in the 13 s series). 752 = 0x2F0 is the length constant the §8 call-site decode found (`addi at,zero,0x2f0`). Four samples show shorter partial matches (712/696/160/48 bytes) consistent with catching a write in flight. Two states are full 4 KiB copies of sparse regions (0x8EAD8 with 19 non-zero words; 0x85AF8 with 20), 14 samples caught DMEM fully zeroed (FNV `0x51d88627df287325`) and two are dominated by a low-entropy repeating pattern whose match address is not reliable. Between consecutive samples that both hold a block, 3.0–3.7 KB of DMEM differ in 300–700 fragments, so a full DMEM rewrite is faster than the sampling interval. |

Descriptor cross-check: slot 0 (guest 0x6EEAA0) matches the P05 task-word
record on every word except 4, 6 and 12, which carry the KSEG0 bit in
RDRAM (`0x80768e60`, `0x80794e90`, `0x80411910`); its words 12-13 are the
record's `00411910`/`000001a0` pair on that basis — the zeroed buffer §8's
table lists at guest 0x411910. Slot 1 (guest 0x6EEAF0) matches the same way
except that its words 12-13 are a different pair, `804132d0`/`000001c0` —
the buffer §8's table records at guest 0x4132d0. Words 6-7
(0x794e90/0x2df) are a buffer §8's table does not list.

Not established by this evidence (do not over-read):
- The writer of the DMEM traffic is not identified by these snapshots. The
  in-process P06 records (the DMA-helper loop with zero operands at the
  freeze onset) are the evidence that the RSP is executing; DMEM could in
  principle also be written by the emulated CPU through the 0x04000000
  window. No producer of the heap zeroing is identified.
- The scattered `src` addresses are not explained by snapshots; they show
  what the loop emits, not which instruction emits it.
- The heap zeroing is historic, not ongoing: RDRAM does not change while
  frozen, so the zeroing cannot be caught in the current process. P08 must
  reproduce the freeze — and can now do so root-free, with this method used
  alongside (or instead of) an instrumented build.
