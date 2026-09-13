# P01 — Complete internal RSP DMA read policy ledger

Analysis package: **no emulator behavior changed**. Performed under the work-package
guide's evidence-based diagnostic prompt. Baseline: branch `dd-eos-watchdog-checkpoint`
at `017ef41ca` (P00 record published), tracked tree clean.

## 1. Inputs and pinned references

| Source | Revision / location |
|---|---|
| Production legacy | `mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp` `rsp_dma_read` (217–320), `rsp_dma_write` (322–371), `RSP_MTC0` (374+), `state.hpp:10-13`, `rsp_jit.cpp:46-95`, `parallel.cpp:236` |
| Pinned Ares reference | `~/LLM-Projects/phobos` (local checkout path; pinned by commit, archive identity in `docs/P00_BASELINE.md` §3) commit `f1174e7654141accad40b9ffc2c7d978e93a00f0`: `ares/n64/rsp/io.cpp` (`ioWrite` 80+, `ioRead` 8–32), `rsp.hpp` (DMA struct 274–296), `dma.cpp` (full) |
| CXD4 (disagreement only) | `mupen64plus-rsp-cxd4/upstream/su.c` `SP_DMA_READ`/`SP_DMA_WRITE` (213–297) |
| Core (scope contrast) | `mupen64plus-core/upstream/src/device/rcp/rsp/rsp_core.c` `do_sp_dma` (512–565) |
| Captured case | DDSTART11 record: `docs/DDSTART11_NATIVE_ANALYSIS.md` (raw `0xfb0 / 0 / 0xffffffff`) |

Register entry conditions (verified, cp0.cpp:380-400): MTC0 stores
`DMA_CACHE = val & 0x1fff`, `DMA_DRAM = val & 0xffffff`, length registers unmasked;
a write to `DMA_READ_LENGTH` executes `rsp_dma_read` synchronously.

## 2. Legacy behavior write-out (Direct, from source)

Decode (`cp0.cpp:219-223`): `length = (reg & 0xFFF) + 1`, aligned to 8
(`(length + 7) & ~7`); `skip = (reg >> 20) & 0xFFF` (raw); `rows = ((reg >> 12) & 0xFF) + 1`.

Pre-copy register mutation (`cp0.cpp:244-245`): `DMA_CACHE &= ~0x3` (4-byte SP
alignment), `DMA_DRAM &= ~0x7` (8-byte DRAM alignment) — the live registers are
modified before the transfer.

Bank-boundary clamp (`cp0.cpp:248-249`): if `(dest & 0xFFF) + length > 0x1000`,
`length = 0x1000 - (dest & 0xFFF)` — applied **once, before the row loop**, using
the (already masked) `DMA_CACHE` register value; the clamped length is then used
unchanged for every row (the row loop never re-evaluates it).

Copy (`cp0.cpp:273-304`): per word, `source_addr = (source + j) & 0x7FFFFC`
(8 MB bus, word-floored), `dest_addr = (dest + j) & 0x1FFC` (13-bit SP space,
4-byte granularity — permits DMEM→IMEM crossing mid-row and across rows).
`source += length + skip` and `dest += length` after **every** row, including the
last (visible trailing skip on the DRAM side only).

Poststate (`cp0.cpp:306-307`): `DMA_DRAM = source`, `DMA_CACHE = dest` — full raw
accumulated values (dest may exceed 13 bits, e.g. `0x5fb0` for the captured case).
RD_LEN/WR_LEN registers are never modified. Dirty/JIT (`cp0.cpp:288-295`): each IMEM
word write sets `dirty_blocks |= (0x3 << block) >> 1` with 256-byte blocks
(`state.hpp:10-13`, 16 blocks); `rsp_jit.cpp:46-95` recomputes and clears those
blocks. Return (`cp0.cpp:319`): `dirty_blocks ? MODE_CHECK_FLAGS : MODE_CONTINUE`.

`rsp_dma_write` (SP→RDRAM) is a separate implementation (13-bit SP *source* mask
`0x1FFC`, same clamp, trailing skip lands on DRAM, cross-writeback) — untouched by
this policy (plan constraint 8). Core `do_sp_dma` and CXD4 are separate engines,
also untouched.

## 3. Reference models

### 3.1 Ares (pinned `f1174e765`) — address-level model

Decode (io.cpp:84-104): into `pending`, then `current` when started:

| Field | Register bits taken | Effect |
|---|---|---|
| `pbusRegion` | bit 12 | bank, latched for the whole transfer |
| `pbusAddress` | bits 3–11 | 8-byte-aligned 12-bit offset (bits 0–2 dropped) |
| `dramAddress` | bits 3–23 | 8-byte-aligned 24-bit DRAM address |
| `length` | bits 3–11 | row bytes = `length + 8` = round-up-8(raw+1) |
| `count` | bits 12–19 | rows = count + 1 |
| `skip` | bits 23–31 → skip bits 3–11 | skip low 3 bits dropped (8-byte aligned) |

Transfer (dma.cpp:26-46): per row, `recompiler.invalidate(pbusAddress, length + 8)`
once when `pbusRegion`; then 8-byte beats: IMEM (u64 RDRAM read) or DMEM (2×u32);
`pbusAddress += 8` (n12 wraps within the bank; region never changes),
`dramAddress += 8` (n24). After a row: `count ? (count--, dramAddress += skip,
requeue) : (busy = 0, current.length = 0xFF8, start pending)`.

Derived consequences: bank latched; per-row DRAM stride = rowbytes + skip; skip
added **only between rows** (no trailing skip); final SP offset = start + rows×rowbytes
wrapped in 12 bits; live readback exposes region+offset (`io.cpp:15-16`), dramAddress
24-bit (`:21`), and length repacked (`:26-28`) with the `0xFF8` length poststate after
completion.

### 3.2 CXD4 (su.c:213-297) — disagreements

Row bytes = `(raw|7)+1` (equals Ares/legacy algebra); rows = count+1. Destination
`(row×stride + memaddr + i) & 0x1FF8` — **13-bit mask, crossings allowed** (agrees
with legacy Parallel, disagrees with Ares's latched bank). DRAM side
`& 0xFFFFF8` per 8-byte beat (floors misalignment; raw 12-bit skip). Out-of-range
DRAM zero-fills reads and skips writes. Rows execute in reverse count order: the
per-row source→destination mapping is unchanged, but with overlapping destinations
the final memory content depends on that order (as in the captured geometry).
**No post-transfer register writeback** (DMA registers keep their written values;
RD_LEN keeps `raw|7` because MTC0 stores the length ORed with 7, su.c:87-90);
busy/status bits cleared only.

### 3.3 Core `do_sp_dma` (rsp_core.c:512-565) — scope contrast

Row bytes `(raw|7)+1`, rows count+1, raw skip. `memaddr = raw & 0xff8`
(8-byte-aligned start, bank taken from bit 12 separately), then byte-wise
increment without wrap — a row may cross the bank boundary contiguously into the
other bank (like CXD4/legacy, unlike Ares). DRAM `& 0xfffff8` start, byte-wise.
No DMA-register writeback. Completion via core interrupt event. Contrast only —
**not** a model to adopt; P04 must not modify it.

### 3.4 Agreement / divergence matrix

| Behavior | Parallel legacy | Ares | CXD4 | Core |
|---|---|---|---|---|
| Bank | crosses (13-bit walk) | **latched, 12-bit wrap** | crosses | crosses (contiguous walk) |
| Row bytes | round-up-8(raw+1), clamped at 0x1000 | same value, no clamp | same value, no clamp | same value, no clamp |
| SP start align | 4-byte | 8-byte | 8-byte (per-beat floor) | 8-byte |
| DRAM start align | 8-byte | 8-byte | 8-byte (per-beat floor) | 8-byte |
| Skip | raw 12-bit | **8-byte aligned at decode** | raw + per-beat floor | raw (byte granularity) |
| Trailing skip | yes (DRAM writeback) | **no** | n/a (no writeback) | added but not visible (no writeback) |
| Final reg writeback | full accumulated values | live 12+1/24-bit readback | none | none |
| RD_LEN poststate | unchanged | `0xFF8` after completion | unchanged | unchanged |

## 4. Completed decision table (repair-plan §6.2)

Chosen model: the pinned Ares semantics applied coherently to the corrected
internal-read path. This is a REFERENCE-based decision (single coherent pinned
model), not a claim of hardware certainty; CXD4/core divergences are kept and
documented. Legacy (DD-off) policy is byte-identical to current code.

| # | Policy field | Legacy (current) | Corrected (selected) | Reference / justification | Fixtures |
|---|---|---|---|---|---|
| 1 | Initial bank | implied by 13-bit walk | latch bit 12 once per transfer | Ares io.cpp:86-87; dma.cpp (pbusRegion never written) | D02, D08, D14 |
| 2 | Within-bank address | `(dest+j) & 0x1FFC` per word | 12-bit offset `(start + advance) & 0xfff`, wrap per beat/row; bank bit preserved | Ares n12 wrap; core `& 0xff8`; CXD4 beat floor | D06, D07, D16 |
| 3 | Row length | clamp to `0x1000 - (dest & 0xFFF)` | full `((raw & 0xfff) \| 7) + 1` — the clamp is removed on this path only | Ares length+8 ≡ (raw\|7)+1; core; CXD4 | D01, D02, D09 |
| 4 | Count | rows = raw+1 (`0xff`→256) | unchanged | unanimous | D10 |
| 5 | SP alignment | register `&= ~0x3` | 8-byte (bits 3–11) via snapshot; legacy path keeps `~0x3` | Ares io.cpp:86; core `0xff8`; CXD4 floor. Observed request is 8-aligned → no delta for the captured case | D11 |
| 6 | Initial DRAM alignment | register `&= ~0x7` | unchanged (8-byte) | Ares io.cpp:92; core `0xfffff8`; CXD4 floor | D12 |
| 7 | Skip low bits | raw 12-bit | **aligned to 8 at decode** (`skip & 0xff8`); legacy keeps raw | Ares io.cpp:99 (`skip.bit(3,11) = data.bit(23,31)`); keeps every DRAM beat an aligned 8-byte read. Divergence from CXD4 (raw+floor) and core (raw) is explicit; native P06 validates the choice | D13, D02 |
| 8 | DRAM bus/storage | per-word `& 0x7FFFFC` into `rsp->rdram` | unchanged | 8 MB bus mask stays within the 8 MB backing (`0x7FFFFC >> 2 = 0x1FFFFF < 2²¹` words; `parallel.cpp:236` supplies core RDRAM); Ares n24/CXD4 16 MB address width is a bus-vs-backing distinction already handled by the mask | D15 |
| 9 | Final SP address | raw accumulated writeback (may exceed 13 bits) | `(latched_region << 12) \| ((start + rows × rowbytes) & 0xfff)` | Ares ioRead:15-16 exposes region+12-bit offset only; wrap follows row advance | D19, D14 |
| 10 | Final DRAM address | `source` after per-row `+= length + skip` (trailing skip visible) | `(start + rows × rowbytes + (rows − 1) × skip) & 0xffffff` — no trailing skip | Ares dma.cpp:64-67 (skip only when `count` nonzero); legacy/CXD4/core kept for their paths | D19, D02 |
| 11 | Length/status registers | RD_LEN/WR_LEN never modified | unchanged (no `0xFF8` poststate) | Ares `0xFF8` poststate (dma.cpp:70) documented as known divergence; adopting it would add unobserved behavior (plan §6.2 "do not quietly add new behavior") | D19 |
| 12 | Dirty flags | per-IMEM-word `(0x3 << block) >> 1` when `dest & 0x1000` | same mechanism, applied only when latched region is IMEM, covering wrapped offsets | Actual-destination-only marking (plan invariant); wrap makes mid-row seam coverage required | D04, D17, D18 |
| 13 | Completion/return | synchronous, `dirty_blocks ? MODE_CHECK_FLAGS : MODE_CONTINUE` | unchanged | No evidence for scheduling change; plan says preserve unless separately scoped | D20, D22 |

No implicit alignment/skip/poststate choice remains: every row above states the
selected value, its reference, and the fixture that will exercise it.

## 5. The observed request, explicitly calculated

Raw operands `SP 0xfb0, RDRAM 0x00000000, length 0xffffffff`
(`length` field 0xfff, `count` field 0xff, `skip` field 0xfff).

**Legacy** (matches the DDSTART11 record; replay values were independently
verified in the P00 review): row bytes clamp `0x1000 − 0xfb0 = 0x50` (80), rows 256,
skip 4095, per-row DRAM advance 80 + 4095 = 4175; 5,120 words copied; 3,052 IMEM
word writes (bank crossings); IMEM hash changes; final `DMA_CACHE = 0xfb0 + 256×80
= 0x5fb0`, final `DMA_DRAM = 256 × 4175 = 0x104f00`; row 205 performs the final
write to IMEM offset 0 from RDRAM `0x000d0f80`, row 255 writes offset `0xf60` from
`0x00103eb0` (DDSTART11_NATIVE_ANALYSIS.md, algorithm-derived).

**Corrected (Ares model)**: row bytes `((0xfff \| 7) + 1) = 0x1000` (4096); rows 256;
skip `0xfff & 0xff8 = 0xff8` (4088); latched bank = DMEM (`0xfb0 & 0x1000 = 0`).

- Every row writes the identical circular DMEM region starting at `0xfb0`
  (4096 ≡ 0 mod 4096): offsets `0xfb0..0xfff` then `0x000..0xfaf`. Final DMEM
  content = row 255's source, i.e. RDRAM `0x1fd808..0x1fe807` placed circularly
  from `0xfb0`.
- Copied bytes: 256 × 4096 = **1,048,576** (262,144 word writes, all DMEM).
  **Zero IMEM writes, zero dirty blocks, IMEM bytes and JIT state unchanged.**
- Row j DRAM source start = j × (4096 + 4088) = j × 8184 (all 8-aligned); max
  byte touched = 255×8184 + 4096 = 2,091,016 = `0x1fe808` < 8 MB — every beat in
  backing bounds.
- Poststate: `DMA_CACHE = (0 << 12) | (0xfb0 + 256×4096) & 0xfff = 0x0fb0`;
  `DMA_DRAM = (256×4096 + 255×4088) & 0xffffff = 0x1fe808`. No trailing skip.
- D02 oracle resolution: total copy count is independent of the skip choice;
  expected source bytes and the final DRAM register now follow from decision row 7
  (Ares-aligned skip). D01 legacy values are skip-independent.

## 6. Deliberately unchanged behaviors and consequences

1. `rsp_dma_write`, core `do_sp_dma`, CXD4, and all CPU-originated DMA: untouched
   (plan constraint 8). D23 guards this.
2. DDSTART9's block-boundary correction and all task/interrupt/yield scheduling:
   untouched.
3. DRAM mask/backing, MTC0 register masking, RD_LEN poststate, SP_STATUS bits:
   unchanged under both policies.
4. **Diagnostic interaction (feeds P05):** the existing observer's eligibility is
   keyed on legacy crossing geometry; once the corrected policy stops IMEM
   crossings, that observer goes quiet by design. P05 must add the raw-request-shape
   trigger (`0xfb0/0/0xffffffff` shape, decoded-zero audio condition) so the suspect
   request stays observable. Silence alone must not be read as success (diagnostic-prompt rule 5).
5. The corrected path snapshots raw operands before any register mutation; legacy
   keeps its current pre-copy register masking (visible difference confined to the
   legacy path, unchanged).

## 7. Proposed P02 fixture set

P02 remains governed by the work-package and validation docs (which call for the
full D01–D24 matrix); this section only identifies the correction-critical subset
to prioritize: D01, D02 (expected values per §5), D03–D13, D14, D19, D22 (legacy
differential corpus), D23 (write/core paths untouched), D24 (guards/sanitizers).
Strongly recommended same package: D15–D18, D20, D21.
The independent oracle must implement §4 rows 1–3, 5, 7, 9–10, 12 directly from
this ledger, not from production constants.

## 8. Acceptance self-check and limits

- Acceptance: every §6.2 row resolved with pinned citations; DD-off legacy policy
  = current code exactly; observed request calculated under both policies; fixture
  expectations determined. **Met by this document.**
- Limits (stated, not hidden): hardware truth for skip low bits, 4-vs-8-byte SP
  start alignment, and post-transfer register readback is not verifiable here.
  The corrected policy is the pinned Ares model applied coherently, chosen over
  the CXD4/core crossing behavior because the crossing is the demonstrated defect.
  Validation of the choice is native (P06), per the plan's first-run decision table.
- P03 dependency: the corrected/legacy selector must be explicit DD runtime state,
  independent of logging (P03 owns the seam; P04 consumes it).
