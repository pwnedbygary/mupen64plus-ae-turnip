# DDSTART8 native capture analysis

## Capture and launch

- Input: `ddstart8-logcat_1789249640409.txt` (raw upload is not published).
- SHA-256:
  `407c7bf0832d2de22af9958c2de6493345d0c0684f9131481ac83f655b95c9f6`.
- 2,153 lines / 294,232 bytes; one native launch on 2026-09-12.
- Launch explicitly enables DD with configured Japanese IPL/disk, cartridge
  combo boot, and no requested save-state autoload.
- Effective engine: **Dynamic Recompiler**, with arm64 native library paths.
- Launch reports `countPerOp=1; countPerOpDen=0`, the same values as the
  preceding DDSTART7 captures. This is not a requested timing adjustment.
- The log continues for approximately 54 seconds after emulator startup.
  No Android fatal exception/signal is recorded. Screen/audio behavior was
  not described with this upload and is not inferred from the log.

## Record coverage

DDSTART8 contains 41 records:

| Class/stage | Records |
| --- | ---: |
| Full live-fault snapshot | 15 (one complete snapshot) |
| Relevant compilation | 1 |
| Relevant invalidation | 1 |
| C dirty verification | 24, all reject |
| Slow-path byte stores | 0 |
| Filter-only faults | 0 |

Fault ordinals are consecutive 1–15; coherence ordinals are consecutive
1–26. Only the C dirty-verification sub-budget is exhausted. Zero byte-store
records does not establish an absence of writes: direct generated stores
are outside that hook. The existing DDSTART4 count is exactly its 512-record
cap and must not be interpreted as the end of disk activity.

## Actual load-fault evidence

At log lines 1843–1857, the generated pending-exception hook records:

- Compiled block `800ad49c`, generation 179.
- Compiled PC = EPC = BD-derived fault PC = `800ad4ac`; `BD=0`.
- Cause `00000008` (TLBL), BadVAddr/load address `079bb080`.
- Compiled and current instruction both `8c830000`: `lw v1,0(a0)`.
- Spilled `a0 = 00000000079bb080`.
- Spilled `a1 = ffffffffffffbe7f`.
- Spilled `a3 = ffffffff800bb67c`.
- Spilled SP `ffffffff800d4203`, RA `ffffffff800bb6b0`.

This confirms the earlier saved-context fault with a post-spill live
observation. The invalid load address is aligned; it is not an address-
alignment exception. The matching instruction at the fault PC does not
exonerate earlier boot code.

## Reconstructing the stale caller

The public F-Zero X reference checkout used for comparison is revision
`4fd50c7ca6b44f996aa0fbb68ec86df75855d5b8`. Its
`src/leo/leo_bootdisk.c::LeoBootGame`
implementation derives the `__LeoBootGame2` decode key from the bytes of
the entry address, then changes bytes 2 and 3 of each instruction:

```text
key = (80 + 0b + b6 + 7c) & ff = bd
decoded_byte_2 = (encoded_byte_2 - bd) & ff
decoded_byte_3 = (encoded_byte_3 + bd) & ff
```

Reversing that transformation on the current code windows gives:

| Guest PC | Current decoded word | Reconstructed encoded word | Consequence |
| --- | --- | --- | --- |
| `800bb6a8` | `0c02f8e4` | `0c02b527` | Decoded JAL targets `800be390`; encoded JAL targets **`800ad49c`** |
| `800bb6ac` | `2405013c` | `2405be7f` | Encoded delay slot loads **`a1 = ffffffffffffbe7f`** |
| `800bb67c` | `27bdfec0` | `27bdbb03` | Encoded stack adjustment is `-17661`, rather than decoded `-320` |

The wrong JAL target exactly matches the live faulting block. Its return
address is exactly the observed `800bb6b0`. Independently, the encoded
delay-slot immediate exactly matches live `a1`. This identifies execution
of the stale, still-encoded caller, rather than merely hypothesizing it from
a later RDRAM snapshot.

The prologue also explains the unusual stack low bits. An incoming SP of
`800d8700` would produce observed `800d4203` under the encoded adjustment,
but incoming SP was not captured; that calculation is corroborative, not
an independent entry-state observation.

## Compilation and invalidation sequence

1. Line 1841: generation 175 compiles 307 words from `800bb540` through
   `800bba0b` (exclusive end `800bba0c`). This encompasses the boot routines,
   including the encoded caller. Copied/current hashes both equal
   `bd23a6d4` at compilation.
2. Line 1842: the boot page is invalidated.
3. Lines 1843–1857: the stale caller has reached the wrong function and its
   load faults. No replacement boot compilation is recorded before this
   fault; the compilation budget still has capacity.
4. Lines 1858–1881: 24 C dirty verifications reject the original boot copy.
   Its hash remains `bd23a6d4`, while current RDRAM hashes to `62e2e755`.

The later dirty-verification rejection is correct behavior. It does not
protect a transfer within an already-running translation that bypasses that
check. Source review identifies active translated continuation/internal
links as the relevant correction area, not a defect in the observed load
  instruction, TLB miss classification, or the `memcmp` comparison itself.

The relevant source path is specific: Pass 1 initially recognizes the
non-linking return and its delay slot as a block end, then a forward-target
scan reopens the block. That lets the boot function's forward call pull the
encoded next function into the same translation. Later passes classify that
call as internal and patch it directly to a translated instruction address.
The generated invalidation stub returns to the active continuation, while
invalidation removes lookup/incoming links rather than revoking an executing
internal branch. A normal dirty lookup is therefore not performed at that
internal call.

## Limits and correction criteria

- There is still no emitted exact byte-store record naming the decoding
  writer, and no explicit generated block-entry record. Attribution here
  rests on the reversible caller transformation, two independent live
  execution consequences, and the compilation/invalidation sequence.
- Do not patch register values, the specific guest addresses, disk timing,
  count-per-op, or IPL byte order to conceal the downstream fault.
- The correction must prevent the stale encoded call from executing while
  retaining the dynarec and the explicit DD-support flag passed from the
  selected game's launch settings. It is not gated on game identity.
- Build/source/host tests cannot establish native menu, gameplay, audio, or
  save behavior. A corrected device run and the required regressions remain
  necessary. The cached-interpreter gameplay failure is not resolved by
  this dynarec diagnosis.

## DDSTART9 candidate correction

For explicitly DD-enabled new-dynarec sessions, retain the non-linking
unconditional transfer boundary and its architectural delay slot. Do not
reopen it to include a forward callee. For DD-disabled sessions, keep the
original forward-target scan.

This changes the block decision in Pass 1, before register allocation and
linking, rather than incorrectly changing only the final branch patch.
The existing external-call writeback and resolver paths remain responsible
for the excluded callee. There are no game addresses, game identity,
WritableROM conditions, clock/count changes, register repairs, or forced
interpreter fallback in the correction. It applies to the new dynarec;
cached interpreter is unchanged.

The expected native distinction is compilation of the first boot function
as **79 words ending at `800bb67c`**, followed by compilation of the decoded
callee rather than execution of its old encrypted translation. This is an
acceptance criterion, not a result of the host fixtures.

Host tests exercise the actual production boundary helper, explicit gate
changes, original forward-target offsets, internal/external classification,
and an ARM64 register-writeback instruction emitted by the production code.
They do not run the entire compiler on the boot image, execute generated
ARM64 code, or substitute a miniature test-only emulator for native testing.