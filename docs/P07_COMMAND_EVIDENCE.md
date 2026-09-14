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
| Diagnostic directory | `/sdcard/Download/p07-memdump` |

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
`/sdcard/Download/p07-memdump/`.

### 6.2 Regions read

The helper reads bounded, fixed regions of the frozen emulation process as
root:

- the active audio task pointer `gCurAudioTask` at RDRAM `0x771D68`
  (physical; 4 B), then the OSTask it points to (64 B) — to be verified
  against the P05 task words. Note the stored `data_ptr` is the *virtual*
  form (0x80411910; the P05 capture's 0x00411910 is the physical form
  `osSpTaskLoad` derives in its converted copy), so the script compares the
  masked physical form `(ptr & 0x7fffff) == 0x411910` and a plausible
  `data_size` (1..4096; the frozen P06 state is 0x1a0, archived dumps of
  other states show 0x1c0/0x240).
- the audio task slots `gAudioCtx.rspTask[2]` at RDRAM `0x6EEAA0` and
  `0x6EEAF0` (0x50 B each);
- the command buffer at RDRAM `0x411910` (0x1a0 B, the task's `data_ptr`);
- the RSP memory at `mem_base+0x5000000` (DMEM + IMEM, 8 KiB;
  `MB_RSP_MEM = RDRAM_16MB_SIZE + CART_ROM_MAX_SIZE = 0x1000000 + 0x4000000`
  per `device/memory/memory.c`);
- the audio microcode image at RDRAM `0x768e60` (4 KiB, `aspMainTextStart`
  0x80768E60 per the decompilation symbol map).

`mem_base` is located by probing the app's one 512 MiB anonymous mapping:
for each 4 KiB-aligned candidate within the mapping's first 64 KiB
(64 KiB `posix_memalign` alignment bounds the interior offset), the script
reads the `gCurAudioTask` pointer and accepts the candidate whose target
descriptor shows `type = 2` and `data_ptr = 0x00411910`… (exact values as
listed above); it fails closed if none matches. Every other read is a fixed
address; nothing is written to the process; outputs and SHA-256s land in
`/sdcard/Download/p07-memdump/`.

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
