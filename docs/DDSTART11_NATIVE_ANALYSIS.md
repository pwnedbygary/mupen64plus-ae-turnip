# DDSTART11: direct evidence of the internal RSP IMEM writer

## Capture validation

The unchanged helper's `ddstart10-rsp` filename prefix is not a build identifier.
The uploaded archive `ddstart10-rsp.ZJNdWU_1789303426018.zip` contains the expected
DDSTART11 APK SHA-256:
`ffc6459fc6c7a5ae53a11f5cf54b7bc8f8c9aa880e53600a7a1633de15cb2df1`.
Archive SHA-256:
`a89cf94d4357780f54fb77551020f08c71d5938ca4eca71ae620e616957bccd3`.

Capture `dSOS6x` completed on 2026-09-13 at 08:42:29 UTC−04:00.
Both debuggerd invocations succeeded. Both samples pin PID 25811/start tick
3400385; the emulation thread is TID 25845. The log explicitly confirms DD
activation and Dynamic Recompiler.

There are 112 emitted distinct task identities, 57 core CPU→IMEM records,
8 internal IMEM-touching DMA records and 99 JIT allocation records.
No exhaustion marker occurs. These are emitted counts, not total invocation
counts; deduplication remains in effect.

## Direct writer observation

At 08:40:08.125, internal RSP DMA record 8 reports:

| Field | Value |
|---|---|
| IMEM DMA sequence | 1897 |
| Current RSP entry | 1512 |
| Entry-time task type / flags | Audio (2) / 0 |
| Raw SP memory address | `0x00000fb0` — starts in DMEM |
| Raw RDRAM address | `0x00000000` |
| Raw read-length register | `0xffffffff` |
| Requested/aligned row length | 4096 bytes |
| Plugin's clamped effective row length | 80 bytes |
| Number of rows | 256 |
| Plugin's decoded skip | 4095 bytes |
| Copied payload | 5120 words |
| Writes into IMEM | 3052 words, including repeated destinations |
| Before full-IMEM hash | `0x3aaaf0f5f121410e` |
| After full-IMEM hash | `0xef57f06bd22df2a6` |

The observer's before/after samples include IMEM offset 0 changing from
`340a0fc0` to `ffffffff`, and offset `0xf60` changing from `8c260004` to
`acf80004`. These after-values occur in the subsequent compile input.

This is a directly observed memory-writing operation, unlike the earlier
native PC samples. It identifies the internal Parallel-RSP DMA path as the
writer of this corruption, not the core CPU-originated DMA path.

The entry-time task snapshot contains audio microcode at `0x00768e60`,
microcode data at `0x00794e90` with size `0x2df`, and an audio command buffer
at `0x00411910` with size `0x1a0`. It is not a yielded graphics task.

## Native correlation

The anonymous reservation base in both stacks is `0x70a8ca2000`.

| Sample | Relative native PC | Absolute PC | Allocation / IMEM start |
|---|---|---|---|
| 1 | `0x74558` | `0x70a8d16558` | Record 99 / `0x200` |
| 2 | `0x722a4` | `0x70a8d142a4` | Record 97 / `0xf60` |

Both allocations were compiled after the observed DMA. Containment identifies
regions, not exact guest instructions corresponding to native offsets.

User ticks rose from 8436 to 12900 at CLK_TCK 100: 44.64 CPU seconds, with
system ticks unchanged. Scheduler runtime independently increased by
44.635437050 seconds across approximately 45 seconds. Both samples were running.

Replaying only the logged, unchanged address arithmetic yields exactly 3052
IMEM word writes. It predicts the final write to IMEM offset 0 from RDRAM
`0x000d0f80` in row 205, and to offset `0xf60` from `0x00103eb0` in row 255.
These are algorithm-derived source addresses, not additional memory snapshots
or an identification of the original CPU routine.

## Command-field provenance and limits

The captured compile input supports the following static call path:

1. IMEM `0xf4c` calls the helper at offset `0xa6c`.
2. That helper extracts `v1 = (k0 >> 12) & 0xff0` and
   `v0 = t9 & 0x00ffffff`.
3. The block at `0xf54` selects destination `at = s7`, calls the DMA helper
   at `0xad4`, and decrements `v1` in the JAL delay slot.
4. That helper programs SP memory/DRAM addresses and launches the read.

The DMA record is consistent with an extracted size of zero becoming
`0xffffffff`, a low-24-bit source of zero and `s7 = 0xfb0`.
It does not establish that all bits of `k0` or `t9` were zero.
The observer explicitly reports `site_pc=unknown`; this static reconstruction
is not a newly logged exact MTC0 execution site.

Why those command fields were zero remains unresolved. The capture does not
identify their CPU producer or establish whether the command should have been
skipped. Do not relabel that uncertainty as a proven upstream producer defect.

Historical yielded-graphics handoff theories do not explain this event merely
because an older capture had a similar DMA shape: the current event's own
entry snapshot identifies a non-yielded audio task.

## Reference comparison and correction boundary

The supplied Phobos/Ares implementation separates a one-bit SP memory region
from a 12-bit address, latches them into the current DMA, and advances only
the 12-bit address. See its `ares/n64/rsp/rsp.hpp` DMA register definitions,
`rsp/io.cpp` register decoding, and `rsp/dma.cpp` transfer loop.
That model preserves the initially selected DMEM/IMEM bank.

In contrast, the current Parallel-RSP implementation:

- Clamps this 4096-byte row to the 80 bytes remaining at the bank boundary.
- Advances subsequent rows through a 13-bit destination mask.
- Consequently enters IMEM despite starting in DMEM.

The new capture exercises those discrepancies directly. A bank/row correction
is justified independently of discovering why the command's size field was zero.
A mask-only change would still leave the incorrect 80-byte row length.

Do not claim all reference implementations agree: CXD4 also allows bank
crossings, while Ares masks low skip bits differently from current Parallel-RSP.
The existing web-reference document is not itself a hardware SP-DMA specification.
Any correction must explicitly document its alignment/skip/post-register choices,
remain behind explicit DD activation, and leave unrelated DMA paths unchanged.

No emulation correction was made during this analysis. Preventing IMEM destruction
would not by itself prove the audio command stream is correct or that the game
will finish booting. Native menus/gameplay/audio and plain-cart/writable-cart
persistence validation remain outstanding. No repeat capture is needed merely
to re-establish this writer.