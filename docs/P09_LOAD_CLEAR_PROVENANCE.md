# P09 — provenance of the DD-transition "clear" (analysis package, no source change)

Status: DRAFT for independent review. Nothing here is a repair claim, a
hardware-divergence claim, or a native acceptance claim. Read with
[N64DD_CURRENT_STATUS.md](N64DD_CURRENT_STATUS.md) and the evidence prompt
[N64DD_EVIDENCE_REVIEW_PROMPT.md](N64DD_EVIDENCE_REVIEW_PROMPT.md).

## Why this package exists

The status documents described the transition behaviour as a MIO0-style
decompression/workspace clear whose length (about 4.5 MB, twice) came from
"MIO0-style length" data fields. This package re-derives what the two calls
actually are from (a) the guest image as loaded in a coordinate-correct frozen
RDRAM window, (b) the live argument samples already captured by DDSTART15, and
(c) a fresh reproduction performed in this workspace. It corrects that
description and states precisely what remains unknown. No emulator source
behaviour was changed.

## Method and identity

- Code and data read from `.fzxwork/p07-final/recapture3/rdram-window.bin`
  (8 MiB, guest coordinates, verified: `gCurAudioTask` word at guest `0x771D68`
  = `0x806EEAA0`; aspMain image word at guest `0x768E60` = `0x340A0FC0`).
  Guest words are recovered with the documented dump rule (host-LE, `'<I'`).
- Live arguments from the already-captured DDSTART15 records in
  `.fzxwork/p08d-capture/logcat-p08d-run1.txt`
  (sha256 `c20ec502162d957ec2a09a01c316be3ecfe9e0b2039712bb98f3b25273c32321`).
- Reproduction: this workspace's attached device (Retroid Pocket 6, Android 13,
  package `org.mupen64plusae.turnip.pwnedbygary.debug`, installed `base.apk`
  sha256 `6f8a46d775c7510c81dea90ea509317eff7c54b5267da199a5a866e4058f9183`),
  launched through the exported `SplashActivity` and the gallery's `Start`
  (reset) entry. See "Reproduction" below. The run's logcat is retained
  privately at `.fzxwork/p09-live/logcat-20260916-2036.txt`
  (sha256 `52ebacbabe7954cd74a6061d66b25b3b7d60cea847e139f788ee372c0daf1dbf`)
  with the memory-sample series at `.fzxwork/p09-live/memseries.txt` and the run
  identity at `.fzxwork/p09-live/run-identity.txt`. These files are private
  workspace artifacts, not repository deliverables.
- Disk/cart cross-checks against the local private assets
  (`cart.z64`, `efzj.ndd`, `enhack.z64`). Byte order differs by artifact and
  this matters: the RDRAM dump stores guest words host-LE (recover with `'<I'`),
  while the cart and disk images store guest words in the guest's big-endian
  byte order (recover with `'>I'`). Every cross-image comparison below is made
  between recovered 32-bit words, not between raw file bytes.

## Direct observations

### O1 — the zeroing store routine is a plain `memset`, not a scene clear

Disassembly of the live-loaded image at guest `0x80747240` (the DDSTART15
`entry_pc`) shows a leaf `memset(dest = A0, fill = 0, len = A1)`: `slti`/`bnez`
short-length guard, a `negu`/`andi 3` alignment prologue with `swl`, a
 32-byte-per-iteration bulk loop of eight `sw $zero` stores ending in the branch
delay slot, then a word tail and a byte tail. There are no cache instructions
and no DMA. This is the routine whose stores DDSTART14 retains
(`PCs 0x80747278..0x80747298`): of the 32 retained stores over `0x80411a30..`,
31 change a non-zero word to zero and all 32 have `after = 0` (one has
`before = 0`).

### O2 — the executed call site is the loader's "not MIO0" fallback branch

The live path is `0x800aea0c`, whose code is exactly:

```
0x800ae9dc  lw    t8, 0x38(sp)        ; t8 = source buffer (live: 0x803da5f0)
0x800ae9e0  lui   at, 0x4d49          ; at = 'MIO0' (0x4d494f30)
0x800ae9e4  ori   at, at, 0x4f30
0x800ae9e8  lw    t1, (t8)            ; t1 = word0 of the source buffer
0x800ae9ec  lw    a1, 0x3c(sp)
0x800ae9f0  lw    a0, 0x3c(sp)        ; a0 = a1 = destination (live: 0x8012b520)
0x800ae9f4  bne   t1, at, 0x800aea0c  ; ---- not 'MIO0' -> memset path
0x800ae9fc  jal   0x8072e3b0          ; MIO0 path: decompress(a0 = source, a1 = dest)
0x800aea0c  jal   0x80747240          ; fallback: memset(a0 = dest, a1 = s0)
0x800aea10  move  a1, s0              ; delay slot: len = s0
```

`0x8072e3b0` is a standard MIO0 decompressor (it takes word+4 as the
decompressed size and word+8 / word+0x0c as the compressed and uncompressed
offsets, and starts its bitmask at word+0x10). So the executed `memset` is by
construction the branch taken **when the compared word is not `MIO0`**.

### O3 — where the length comes from

The routine at `0x800ae7c4` that contains this call site loads a resource head
and uses it as follows (abridged, addresses exact):

```
0x800ae894  alloc(a0 = 1, a1 = 8) -> [sp+0x3c]     ; 8-byte head buffer
0x800ae8a8  read_resource(a0 = id, a1 = 8, a2 = head buffer)
0x800ae8b0  jal 0x8072e44c (a0 = head buffer)      ; = "jr ra; lw v0,4(a0)"
0x800ae8bc  move s0, v0                            ; s0 = word1 of the head
0x800ae8c4  bnez t7, 0x800ae8ec                    ; t7 = [sp+0x58]; live value 0
0x800ae8cc  alloc(a0 = 0, a1 = s0) -> dest         ; destination buffer, size s0
0x800ae8dc  alloc(a0 = 1, a1 = [sp+0x4c]) -> src   ; read scratch buffer
0x800ae9d4  read_resource(a0 = id, a1 = [sp+0x4c], a2 = src)
0x800ae9f4  bne word(src), 'MIO0' -> memset(dest, 0, s0)
```

This is the branch taken for the live value `[sp+0x58] = 0`; `0x280` appears
nowhere in the routine as an immediate — it is the live `[sp+0x4c]` stack value
for the first call (`0x400` for the second), so the two read sizes differ
between the two calls.

`0x800ae100` is the resource reader; it computes
`source = word[0x807C70C0] + (id & 0x00FFFFFF)` and copies through the
0x400-byte chunked path at `0x8070818C`.

So **the length passed to the memset is `word1` of the bytes read from the
resource head**, i.e. resource *data* interpreted as a size, and not a length
the guest computed for a clearing operation. Both live calls agree exactly:

| call | `a0` (dest) | `a1` (len) | head word0 | head word1 | head word1 == `a1` |
|---|---|---|---|---|---|
| 1 | `0x8012B520` | `0x00460020` | `0x00460026` | `0x00460020` | yes |
| 2 | `0x8058B540` | `0x00460000` | `0x00460000` | `0x00460000` | yes |

`a1` is `s0` (delay slot `move a1, s0`, tagged `a1_provenance=compiled-delay-or-a1-s0`
by the diagnostic), and the head samples are live RDRAM reads
(`header_sample_provenance=validated-direct-rdram-raw`). Call 2's destination
equals call 1's destination plus call 1's length (`0x12B520 + 0x460020 =
0x58B540`), consistent with consecutive allocations from the same allocator
(`0x807084E4`).

### O4 — the read source and the loader's own cache table

The loader keeps a `(id, pointer)` table at `0x800D58B0` with the count at
`0x800D5EF0`. In the frozen window and again in this session's live sample the
table holds 16 entries and entry 15 pairs id `0x0F25F0B0` with pointer
`0x8012B520` — the failing destination. Under the reader's formula that id
implies `source = 0x002BA320 + 0x25F0B0 = 0x5193D0`; `word[0x807C70C0]` reads
`0x002BA320` in both the frozen window and the live sample.

Caveat: the DDSTART15 record does not carry the id, and the read source address
is not directly instrumented. The pairing above is derived from the loader's
table plus its static address formula. Closing that gap is part of the proposal
below; it should not be quoted as an observed value yet.

### O5 — what the read bytes are

The sampled head bytes (`00460026 00460020 0046FFF0 ...` and
`00460000 00460000 ...`) are, as 16-word sequences, identical to data in the
**cart ROM** at `0x5193D0` and `0x5194D0` respectively (`'>I'` word reads), and
the same two sequences exist in `enhack.z64` at `0x51E8A0`/`0x51E9A0`. Neither
variant is an `MIO0` header.

An `MIO0` scan of the images (magic occurrences whose headers resolve and decode
to their declared size) finds **433** headers in the cart and **10** on the disk,
with maximum decompressed sizes of `0x25800` and `0x140A0`. `0x460020` is
therefore about 30x the largest size the cart declares and about 56x the largest
the disk declares, so it cannot be a legitimate `MIO0` decompressed size. The failing read address `0x5193D0` sits inside a packed
region of small `MIO0` assets: 70 valid headers lie within 256 KiB of it and the
nearest is `0x5188CC` (declared size `0x600`, whose whole block ends well before
`0x5193D0`). The head that was read is *data in that region*, not the head of a
block at that address — consistent with O3's branch being taken on data that was
never an `MIO0` head.

### O6 — the guest's own resource directory is intact and matches the disk

The id list the game uses (`0x0F25F0B0, 0x0F25F1B0, 0x0F25F334, ...`) is
present in RAM at `0x06B378` (below the swept region, so it survives), and the
same 16 words appear on the EK disk at `0xB7C038` when the disk is read as
big-endian words (the raw byte order differs from the dump's, per the method
note above). So this directory was delivered to RAM without corruption in the
runs examined, and the ids are not random garbage.

## Reproduction (live session, evidence retained privately)

With the device now attached to this workspace, a cold `Start` (reset) launch was
performed here at 20:36:11 (logcat at
`.fzxwork/p09-live/logcat-20260916-2036.txt`). The run contains exactly two
clear calls — 20:36:27.433/.434 (destination `0x8012B520`, length `0x460020`)
and 20:36:27.457 (destination `0x8058B540`, length `0x460000`), the same
arguments, head samples and sites as the P08d capture — followed by the all-zero
submission at 20:36:27.461:

```
DDSTART12 RSP cmd_entry data_ptr=0x00411910 data_size=0x000001a0
          hash=0x8d0350be04626145 nonzero_words=0 words=104
```

This is the identical entry hash, destination, size and word count reported for
the earlier P08d capture, and the retained DD history is identical too (same
sequences `10346..10361`, same `overwritten=10345`, same addresses and
lengths). Two independent sessions therefore reproduce the same trace, and this
one was launched and captured in this workspace.

A post-transition root-free memory sample (same-UID `run-as` +
`/proc/<pid>/mem`, the P07-R method: no root, no APK change, no device-side
script) was taken in the same session, with the raw series retained at
`.fzxwork/p09-live/memseries.txt`. First-word values read in the post-transition
state: command buffers `0x411910`/`0x4132D0` zero, `0x5193D0` zero, the loader
scratch `0x3DA5F0` still holding variant-B words, the loader table present with
count 16, and `word[0x807C70C0] = 0x002BA320`.

Limits of that live sample, stated because they affect how far it can be read:
the sampler was a throwaway host script whose `dd` invocations also returned
`dd`'s own status text, so only the first-word values of the consistent
post-transition rounds are usable (the last rounds return that status text as the
first word) and its non-zero word counts are inflated and must not be quoted; a first attempt on the same
evening produced no usable run (UIAutomator selection failed, then the emulation
process was absent), so the usable run is the one identified above; and the
sample is a *post*-transition state, which is why the staging address
`0x5193D0` reads zero there.

## What this changes, and what it does not

Changed understanding:

- The transition's "clear" is not a decompression/workspace clear whose length
  was read from a valid header. It is the resource loader's fallback for a
  resource whose head is not `MIO0`, and its length is that head's second word
  (resource data). The ~4.5 MB extents are consequences of the compared data,
  not a clearing policy.
- The audio command buffers at `0x00411910`/`0x004132D0` lie inside the first
  fallback's destination range (`0x12B520..0x58B540`), which is why they are
  zeroed. That arithmetic is unambiguous; that it is a defect is not shown.

Not established here (do not claim):

- That the compared head *should* have been `MIO0`. No hardware, reference
  emulator or known-good capture was used, so whether the delivered bytes match
  what those ids yield on a correct implementation is unknown. The prohibited
  claim list still applies verbatim.
- That any emulator behaviour is divergent. No emulator code was changed, and
  no first divergence was demonstrated. Guest-side arithmetic alone produced
  the destination, the length and the read address.
- That the loader's id/read-source pairing used by the failing call is anything
  other than the derivation in O4.
- The timing/coverage relation between the two memsets and the staging region:
  the second call's head bytes are non-zero even though they lie inside the
  first call's nominal range. Whether the first sweep had reached that address,
  or the region is repopulated, is not observable with the current records.

## Proposed next step (one instrumented capture, no guessing)

Add, behind the existing per-game DD activation gate, a bounded record set in
one build:

1. the loader's `id` argument and the computed read source address, at the same
   acceptance points as DDSTART15 (they are not currently logged);
2. bounded observation of writes into the staging window that `word[0x807C70C0]`
   points at, distinguishing CPU stores from PI/DMA completions;
3. `DDSTART16` extended to *cart* PI transfers (`cart_addr` outside the DD
   window), which are currently not instrumented at all — the staging is
   populated from the cart or disk and neither path is fully visible today.

Rationale, kept separate from the observations above: the destination and the
length are guest-derived, so a repair cannot come from the memset site. If the
delivered head bytes are shown to differ from what the same id yields elsewhere,
that difference is the first divergence and the candidate correction; if they
are shown to be identical, the fallback is data-driven and the search moves to
the guest state that selected that id, with the pinned DD/DMA policy work
remaining a separate, still-unproven hypothesis. Do not implement any runtime
change before that record exists.
