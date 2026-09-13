# DDSTART11: identify the instruction-memory DMA writer

DDSTART10 captured suspect CPU-like rendering words compiled as RSP code, but
did not capture their writer. It also exposed a stale diagnostic IMEM hash and
an overstatement of host-range precision. See
[DDSTART10_NATIVE_ANALYSIS.md](DDSTART10_NATIVE_ANALYSIS.md).

DDSTART11 changes observations only. Keep DDSTART9's boundary correction and
all existing DMA alignment, clamp, row/skip, bank/wrap, copy, scheduling and
CPU/RSP execution semantics unchanged.

## Coverage

### Parallel-RSP internal DMA

`DDSTART11 RSP dma_read` records transfers whose actual destination stream
touches IMEM. Eligibility is computed after the existing alignment/clamp:
initial IMEM qualifies; initial DMEM qualifies when the total effective
transfer exceeds the bytes remaining in DMEM. Normal DMEM-only traffic does
not perform the expensive hashing/sampling or consume this record budget.

Records include:

- Raw pre-alignment SP memory, DRAM and read-length registers.
- Requested, aligned and effective row length, count and skip.
- Actual IMEM bank/range summary under the implementation's 13-bit addressing.
- Copied source-address/payload samples and hash, with explicit sample truncation.
- Before/after full IMEM hashes and samples at offsets `000`, `3fc`, `f60`, `ffc`.
- Current entry counter and the 16 OSTask words snapshotted at that entry,
  before identity dedup—not the last newly logged task identity.

`imem_dma_sequence` counts eligible events, including duplicate identities.
The observer retains at most 512 distinct transfer identities/records with
one exhaustion marker. The identity key excludes changing entry counters
and task context. `site_pc=unknown` is intentional: the ordinary state PC
must not be substituted for the exact executing MTC0 instruction.

### Core/CPU-originated SP DMA

`DDSTART11 RSP CPU->IMEM` covers core DMA whose actual physical destination
interval intersects IMEM, including DMEM-to-IMEM crossings. It captures
programmed/transfer metadata, decoded geometry, bounded source and overlap
samples and before/after evidence.

Observer reads are bounded to safe ranges. Unsafe cases retain metadata with
`probe=skipped`; the observer does not modify the underlying transfer.
This path has its own 256-record limit and 512-entry identity table, with
explicit exhaustion. It uses the gated diagnostics callback directly at INFO
level rather than DDSTART1's shared, startup-limited `DebugMessage` path.

All expensive work, bookkeeping and resets are gated by explicit DD activation.
WritableROM alone does not enable the probe.

## Hash and range interpretation

Do **not** compare core and Parallel-RSP hash numbers directly:

- Core: byte-wise FNV-1a in logical DMA byte order, over the IMEM overlap.
- Parallel-RSP: multiply-then-XOR word-wise hashing of host `uint32_t` values;
  before/after IMEM hashes cover all 4 KiB.

Use transfer addresses, geometry, samples and within-path before→after changes
to correlate writers. Hash equality across these two algorithms is not expected.

The diagnostic IMEM hash is now invalidated when dirty code refreshes cached
IMEM. Existing region records now use `allocation_range`, not a claim of exact
emitted bytes or a host-instruction-to-guest-instruction map.

## Verification and APK

Passed:

- Startup/RSP observer host suite, including eligibility, boundaries, gate,
  task snapshot, dedup and budget assertions.
- Focused core IMEM DMA tests, including DD-off behavior, DMEM→IMEM intersection,
  independent callback delivery after the shared startup budget, and bounds.
- Core fixture AddressSanitizer/UndefinedBehaviorSanitizer checks.
- Focused source review after coverage/gating fixes.
- Final all-ABI debug APK assembly in 28 seconds.
- Packaged markers for both writer paths and retained DDSTART9 in all four ABIs.
- APK signing identity and matching preserved ARM64 library Build IDs.

APK: `build-downloads/DDSTART11-debug.apk`.

SHA-256:
`ffc6459fc6c7a5ae53a11f5cf54b7bc8f8c9aa880e53600a7a1633de15cb2df1`.

Package: `org.mupen64plusae.turnip.pwnedbygary.debug`.
Version code: 335; label `3.0.335 (beta) 17cc6111`.

Certificate SHA-256 remains
`311f4e35e939256ae8df53ecbf15c43235d1ee97f8a0138d332839c5e53c2bfc`.

Preserved locally under `build-downloads/DDSTART11-symbols/arm64-v8a`:

| Library | Build ID |
|---|---|
| Core | `2222278b76f02ac42011a62e9e9f2934fc94534b` |
| Parallel-RSP | `1ec6dc22de63e0070245fd4ff147af3ed9e40553` |
| Parallel video | `ec46070d8a937f09dba31840afcae98129b8d615` |

No Android device is reachable here. Host checks/builds do not establish a
native fix or successful menu/gameplay/audio/save regression results.
APK, symbols, raw captures and signing material are not published to the source
branch.

## Next capture

Use the unchanged `tools/capture-dd-rsp-mac.sh`, passing the DDSTART11 APK
path and SHA-256 above. Its old ZIP prefix still starts with `ddstart10-rsp`;
the verified APK hash identifies this build.

It uses `install -r`, never uninstall/clear-data, then starts Core logcat.
Run the existing device root-menu entry `ddstart9-run-as-root.sh`, immediately
open the same DD game with dynarec, reproduce the freeze, and press Return in
Terminal while leaving the game frozen. The helper waits on that fresh
capture's own status and retrieves its files.

Correlate records in sequence: CPU/core IMEM DMA, internal IMEM DMA, entry
snapshots, allocation records, compile words and native stacks. Check exhaustion
and skipped probes before asserting coverage. Identify the transfer that
introduced the suspect words and compare its raw/effective geometry and task
inputs before proposing any emulation correction.

The overall task remains in progress.