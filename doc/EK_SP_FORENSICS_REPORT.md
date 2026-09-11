# F-Zero X EK boot freeze — forensics of `$sp = 0x800d4203`

Read-only analysis. No source file was modified. Evidence = RDRAM dumps in `.fzxwork/`, the
JP-rev0 cart ROM `.fzxwork/cart.z64`, and `fzerox-decomp` sources/symbol tables (revision-checked below).

## Headline (refutes the `^3` premise)

`$sp = 0x800d4203` is **not** `addr ^ S8`. It is a plain 32-bit sum:
`0x800d8700 − 0x44FD = 0x800d4203`. `0x800d8700` is `LeoBootGame`'s frame base and `0x44FD`
is the **descramble-mangled immediate** of `addiu sp,sp,-320` (`0x27bdbb03` = the *cart-ROM*
word at `0x800bb67c`, vs the descrambled `0x27bdfec0`). `0x44FD` is odd, so an even `sp`
yields an odd result; `0x800d4203 == 0x800d4200|3` is arithmetic coincidence.
The guest was **executing the still-scrambled `__LeoBootGame2`**. Consequences, all verified:
`0x44FD`-mangled `jal 0x800bcf10` (`0c02b007`) targets **`0x800ac01c`** (= CP0 `epc`); its delay
slot mangles to `addiu a1,zero,0xbe7f` → `a1=0xffffbe7f`; the two preceding mangled words give
`lui a0,0x3d4f` / `addiu a0,a0,0x7283` → `a0=0x3d4f7283`. Both appear verbatim in the WD records.
So `0x800ac01c`'s `jr ra` + delay-slot `46006006` (`add.s f0,f12,f0`) faults with CU1 clear →
`cause=0x9000002c` (ExcCode 11, CE=1, BD=1) with `ra=0x800bb6a4` (the delay-slot address of the
mangled `jal`, identical in both scrambles).

## Function identification

| vaddr | name | evidence | conf. |
|---|---|---|---|
| `0x800bb540` | `LeoBootGame` (libleo) | `.fzxwork/fzerox-decomp/src/leo/leo_bootdisk.c:19-49`; `jp/ek/symbol_addrs.txt:898` `LeoBootGame=0x8075A590`, PAL `0x800AE290`; RDRAM: `27bdf…`→ key `v1=0xbd` from `&__LeoBootGame2`, two descramble loops of **804** and **96** bytes = `LEO_BOOT_GAME_2_SIZE 0x324`/`3_SIZE 0x60` (`leo_bootdisk.c:15-16`) | high |
| `0x800bb67c` | `__LeoBootGame2` | = `LeoBootGame+0x13C` in all three symbol tables (`jp/ek:899`, PAL `0x800AE3CC`) — `0x13C` is exactly the `bzero(LeoBootGame,0x13C)` arg the RDRAM code passes at `0x800bb69c`; body = `leo_bootdisk.c:59-138` (`bzero`, `__osSetSR`, SP/VI/AI regs, `osResetType=2`, IMEM/DMEM wipe, `bzero(__LeoBootGame2,SIZE-0x40)`, `__LeoBootGame3(entry)`) | high |
| `0x800bb9a0` | `__LeoBootGame3` | `LeoBootGame+0x460` (PAL `0x800AE6F0`, `jp/ek:900`); = `0x800bb67c+804`, i.e. phase-2 target and the `a2`/`0x800bb9a0` value left in LeoBootGame's frame | high |
| `0x800bcf10` | `bzero` | `jp/rev0/symbol_addrs.txt:23` `bzero = 0x800BCF10`; code `slti at,a1,12` / `swl zero,0(a0)` / 32-byte `sw zero` loop | certain |
| `0x800fc44c` | EK "boot disk game" path (in a **runtime-loaded overlay**) | RDRAM `0x800fc400-0x800fc5b4`: `lbu t9,0x800fc5c0` flag, `jal 0x80075d10`, `jal 0x800bbc20`=**osRecvMesg** (`jp/rev0:10`), `jal 0x800bcf10`=**bzero**, `sw t9,780(at)` with t9=2 and at=`0x8000`→**`osResetType=2`** (`pif_syms.ld: osResetType=0x8000030C`), `jal 0x800bb540`=LeoBootGame, epilogue `addiu sp,sp,128`. ROM/src: an overlay, not the linear image | high (behaviour), name unknown |
| `0x800ac01c` | FP helper tail in libultra (`jr ra`, delay `add.s`) — reached **only** because of the mangled `jal` | disassembly `0x800ac000-0x800ac02c`; `jp/rev0/symbol_addrs.txt` has no symbol at that exact vaddr (`__ll_div=0x800BC2FC`, … region holds 64-bit/FP conversion helpers) | med |
| `0x800ad744` | epilogue (`lw ra,20(sp); addiu sp,sp,64; jr ra`) of some game/audio function — the crash site of the bad `jr ra` | disassembly `0x800ad720-0x800ad750`; **name unknown**: `jp/rev0/symbol_addrs.txt` lists no symbol in `0x800ab000-0x800ae000` (that build's table is sparse), so only the PAL table's `Audio_TriggerSystemSE=0x800AD688` is in the same *kind* of neighbourhood | low |

Note the RDRAM body of `LeoBootGame` uses the **non-EK sizes (804/96)** — the running image is the
plain (`EXPANSION_KIT` undefined) build, i.e. the JP-rev0 libleo, not the EK build (0x344/0x50).

## Relocation map

`RDRAM[v] == cart.z64[v − 0x80066000]` (guest words are native-LE in RDRAM, big-endian in the ROM;
compare after per-word swap). Word-for-word match over the whole `0x80060000-0x80100000` region
except three windows. Verified pairs: `0x800bb540↔0x55540`, `0x800bcf10↔0x56f10`,
`0x800ac01c↔0x4601c`, `0x800ad744↔0x47744`, `0x800c0000↔0x5a000`, `0x800d0000↔0x6a000`,
`0x800ff000↔0x99000`. Exceptions:
1. `0x800bb000` page: **225 words differ — `[0x800bb67c, 0x800bba00)`** = exactly
   `0x324+0x60 = 0x384` bytes = `__LeoBootGame2`+`__LeoBootGame3`. The ROM holds them **scrambled**
   (guest bytes `+2 −= 0xbd`, `+3 += 0xbd`), RDRAM holds them descrambled — i.e. `LeoBootGame` ran.
2. `0x800f9000-0x800fd000`: runtime-loaded overlay; e.g. `0x800fc44c↔ROM 0x51f59c`,
   `0x800fc4c0↔0x51f610`, `0x800fc5a0↔0x51f6f0` (constant delta `0x800ACEB0`); the linear slot
   `0x9644c` is zeros.
3. `0x800d8000-0x800eb000`: runtime data/BSS/stack (thread structs live there).
ROM offsets of the six addresses asked about: `0x55540`, `0x5567c`, `0x56f10`, `0x4601c`,
`0x47744`, and `0x51f59c` (overlay).

## `$sp=0x800d4203` origin

Search of `.fzxwork/ci_iplram_fault.bin` (8 MiB RDRAM): **exactly one** 32-bit little-endian
occurrence of `0x800d4203`, at host/guest word offset **`0x800dc2c4`**; zero occurrences of
`0x800d4200` (any form) and of `0x800d4243`; no big-endian or stray byte-sequence hits.
`ci_iplram_spodd.bin` (dump written at the *first* odd `$sp`, before the exception saved anything):
**zero** occurrences of both — consistent with the value arriving from the register file.
`ci_iplram_wd.bin` / older `iplram_fault.bin`: one occurrence at the same address
(`0x800d4243` / `0x800d4203`) respectively.

`0x800dc2c4` is inside the **running thread**: `__osRunningThread = [0x800d1d90] = 0x800dc1d0`
(`jp/rev0/symbol_addrs.txt:154-155`). Layout derived from `__osDispatchThread` (`0x800bcc34`)
itself: 8 bytes/register from `+0x20` in the order `at,v0,v1,a0..a3,t0..t7,s0..s7,t8,t9,gp,sp,s8,ra`
⇒ `ld sp,240(k0)` `0x800bccfc` → **sp = thread+0xF0**; `ld ra,256(k0)` `0x800bcd04` → ra = +0x100;
`lw 280(k0)`→Status `0x800bcc58`, `lw 284(k0)`→EPC `0x800bcd08-0c`, `ld 264/272(k0)`→lo/hi
`0x800bcc84-94`. So `0x800dc2c0/0x800dc2c4` are the **low/high words of the 64-bit `sp`**
(`sp = 0xffffffff_800d4203`, sign-extended — the "word-swapped" look of the whole context is just
mupen's native-LE word layout, not a defect). Same context holds `ra=0x800bb6a4` (+0x100),
`s0=0x80600000`, `s1=0x800dca58`, `a3=0x800bb67c`, `a2=0x800bb9a0`, `v1=0xbd`, `sr=0x2000ff03`
(+0x118), `pc=0x800ac01c` (+0x11C), `cause=0x9000002c` (+0x120) — all matching the WD records.

**It is a recording, not the source.** `__osException` (`0x800bc4c0`) begins
`k0 = 0x80100000−26304 = __osThreadSave (0x800f9940)`, `mfc0 k1,Status`/`sw k1,280(k0)`,
`lw k0,7568(k0)` = `__osRunningThread`, then copies into that thread and saves the live registers:
`sd sp,240(k0)` at **`0x800bc594`**, `sd ra,256(k0)` `0x800bc59c`, `sd t0..t9,gp` etc. The block ring
shows the exception vector `0x80000180` entered with `sp=0x800d4203` **before** `__osException` ran,
i.e. the odd value was already in the register. Memory at `0x800d4200` never existed anywhere.
No guest code loads `sp` from this struct in this window (the only `ld sp,240(k0)` is
`__osDispatchThread`, which did not run between the last good block and the fault).

## Frame at `0x800d8718`

It is **LeoBootGame's own frame**, base `0x800d8700` (= `0x800d8730 − 48`, its `addiu sp,sp,-48`),
where `0x800d8730` is the overlay caller's `sp` recorded by the ring for block `0x800fc44c`:
`+20 → 0x800d8714 = 0x800fc5a0` = return address of `jal 0x800bb540` at `0x800fc598`;
`+24 → 0x800d8718 = 0xbd` = `sw v1,24(sp)` (the descramble key); `+32 → 0x800d8720 = 0x800bb9a0`
= `sw a2,32(sp)` from phase 2 (it overwrote the earlier `sw a3,32(sp)` = `0x800bb67c`);
`+48 → 0x800d8730 = 0x806f2800` = its caller's `a0` (`sw a0,48(sp)`, passed in by
`lw a0,64(sp)` in the `jal` delay slot). So the frame proves the descramble phases had *completed*
(their final locals are live) and ties the chain: `0x800fc44c → jal 0x800bb540 (0x800fc598)
→ LeoBootGame (sp 0x800d8700) → jal 0x800bb67c (0x800bb664) → scrambled prologue`.
The neighbouring game blocks `0x80075f34/4c/70` (`sp=0x800d86c0`) are the caller's caller; in the
caller's frame sit `0x800d8724=0x800fc45c` (the resume point after `jal 0x800bbc20`/osRecvMesg at
`0x800fc454`) and `0x800d872c=0x800fc44c` (the ring's entry `ra`). `0x800bb9a0` in the frame is
`__LeoBootGame3`, not a code address of the fault.

## Ranked mechanisms

**(a) guest-logic** — *excluded as the trigger, present as the arithmetic*: the odd `sp` **is**
guest arithmetic (`0x800d8700 − 0x44FD`), but no guest structure is corrupted: `_osRunQueue/
_osRunningThread` are intact, the thread context is a faithful record, and (prior round) the
4096-entry `dd_trace` in the freeze dump contains **no** DMA into `0x000d0000-0x000dffff`, with the
64DD sector swizzle composing correctly. Nothing in the guest computes a bad stack pointer from
data; the guest's own descrambler is correct and its two phases cover exactly `[0x800bb67c,
0x800bba00)`.

**(b) emulator** — *the root cause*, ranked:
1. **Stale code view for the freshly descrambled page (most likely).** Timing proof: the SPODD dump
   is written *at* the first odd-`sp` block entry, i.e. ~5 instructions after the scrambled
   prologue, and it already contains the **descrambled** word `27bdfec0` at `0x800bb67c`; the loops
   that wrote it (`LeoBootGame`, same invocation — its frame locals are live) precede the `jal`.
   Therefore memory was correct while the fetched instruction was the ROM's scrambled one.
   `osInvalICache`/`osWritebackDCacheAll` are guest no-ops in mupen, so freshness depends entirely
   on the store path: `r4300_write_aligned_word` (`r4300_core.c:366-393`) →
   `invalidate_cached_code_hacktarux`/`invalidate_cached_code_new_dynarec`
   (`new_dynarec.c:2948-2966`). Two concrete suspect sites: the cached interpreter only invalidates
   when the slot is already compiled (`cached_interp.c` ~971-987) while it marks a page "valid code
   even if it's not compiled yet" (`cached_interp.c:806`), and this fork's **xxhash page
   re-validation** can set `invalid_code[i] = 0` purely because the page hash matches
   (`mips_instructions.def:1094-1124` snapshot, `:1166-1184` re-validate) without knowing which
   content the cached translation was built from. Instruction address responsible for the fault is
   the *call* `jal 0x800bb67c` at **`0x800bb664`** (LeoBootGame) fetching `0x27bdbb03` at
   `0x800bb67c`; confirm by logging, at the first odd `sp`, the pair (executed word at
   `0x800bb67c`, `RDRAM32(0x800bb67c)`) plus `invalid_code[0x800bb]` and
   `blocks[0x800bb]->block[0x19F].ops`.
2. **Silent misaligned `lw` (amplifier, definitely mupen behaviour).** After the fault the bad `sp`
   survived into `0x800ad744`: `lw ra,20(sp)` at `sp=0x800d4243` reads `0x800d4257` (not word
   aligned) and returns garbage `0x079b0880` (= `rotl8` of the aligned stack word `0x80079b08`);
   `jr ra` then executes at unmapped `0x079b0880` (WD `vaddr=079b0880 w=2`) → TLB refill. Real
   VR4300 raises AdEL here; mupen instead performs a rotated load, converting a recoverable odd-`sp`
   fault into a wild jump. Confirm by unit-testing `LW` at an odd address on this build.
3. Secondary, same event: the scrambled `sw s0,-0x42A5(sp)` etc. (`sp=0x800d4203`) wrote to
   `0x800cff5e…`, and the mangled `jal` skipped `bzero(LeoBootGame,0x13C)` — which is why
   `0x800bb540..0x800bb67c` is still intact. The earlier round's "stores are being lost" is fully
   explained by this and needs no separate defect.

## Next probe

**Cheapest decisive experiment:** extend the existing `wd_spodd2` interpreter probe
(`cached_interp.c`, `wd_i_pc/wd_i_op/wd_i_sp` ring) with one extra array
`wd_i_mem[idx] = wd_rdram32(pc)` and dump it at the first odd `sp`. If, at `pc=0x800bb67c`,
`op == 0x27bdbb03` while `mem == 0x27bdfec0`, mupen's decoded/translated code is stale ⇒ (b.1)
proven outright and the fix belongs in the store/invalidate path; also dump
`invalid_code[0x800bb]` and `blocks[0x800bb]->block[0x19F].ops` at that instant to pick between the
"only-invalidate-if-compiled" hole and the xxhash re-validation hole. Run once per engine
(`r4300Emulator=1` and `2`, verifying the forced mode via the `DebugMessage` line, since an earlier
round's silent profile mismatch invalidated an engine comparison).
