# N64DD dispatcher diagnostics

## Purpose and evidence boundary

This probe is an observational diagnostic for the F-Zero X Expansion Kit
64DD scheduler freeze.  It records the resident guest dispatcher, the
libultra run-queue/running-thread words, `sAudioThread`, selected-thread
context/state writes, and `ERET` targets.  It does **not** repair queue links,
change guest memory, clear CP0 bits, synthesize interrupts, or alter thread
state.

The addresses and OSThread layout are source evidence already present in this
tree:

* `doc/HANDOFF.md` identifies the resident dispatcher span beginning at
  `0x80746f64`, `__osRunQueue` at `0x80771e18`, `__osRunningThread` at
  `0x80771e20`, and `sAudioThread` at `0x807999d0`.
* `.fzxwork/freeze_state.py` describes the stock OSThread layout: state at
  `+0x10`, context at `+0x20`, context SP at `+0xf0`, RA at `+0x100`, SR at
  `+0x118`, and saved PC/EPC at `+0x11c`.
* Existing `WD_DISPDSP`/`WD_ERET` output in `cached_interp.c` is historical
  watchdog evidence.  The `DDDIAG`/`DDSTORE` records described here are the
  new bounded probe; no new runtime conclusion is implied until a fresh dump
  has been collected.

The source intentionally keeps the historical addresses in one place:
`upstream/src/device/r4300/n64dd_dispatch_diag.c`.

## Coverage by CPU mode

All call sites are gated by `g_dev.dd.idisk != NULL`, so plain carts and
non-DD configurations do not collect this probe.

| CPU mode | Coverage | Known gap |
| --- | --- | --- |
| Pure interpreter | A pre/post event for every guest instruction, including explicit recursive delay-slot `delay-pre`/`delay-post` events for taken and taken-likely branches; C memory writes are recorded after the memory handler; `ERET` records its source PC and EPC target. | The dump is bounded to the most recent 512 dispatcher events and 256 writes. A not-taken likely branch correctly has no delay-slot event because that instruction is architecturally annulled. |
| Cached interpreter (`emumode=1`) | Same per-instruction and recursive delay-slot coverage, exact C memory-write coverage, and direct `ERET` coverage. | The bounded rings retain the newest records if a run contains more than the ring capacity. |
| New dynarec | The translated-block handoff (`get_addr_ht`) records boundaries on every dynarec architecture; ARM64 also supplies `dynarec_sample_hook` samples. C/fallback stores are recorded exactly; a shadow of all `sAudioThread` words catches changed inline-JIT stores at the next block/sample boundary; `ERET` records its exact EPC target. | The dynarec can inline a store and then restore the old value before the next boundary, or execute several instructions between boundaries. Such transient writes and exact instruction ordering are not observable without instrumenting generated code. The boundary PC is therefore not necessarily the PC of the inline store. The generated `jump_eret` ABI does not carry the ERET instruction PC, so dynarec ERET `pc=ffffffff` means source label unknown, not a stale inferred PC. |

The dynarec limitation is deliberate: this pass must remain diagnostic-only
and must not add ABI-sensitive calls to every generated instruction.  The
cached-interpreter run is the reference pass when instruction-by-instruction
ordering is required.  In particular, the dynarec shadow is for the audio
thread window; selected-thread fields are sampled in `DDDISP` snapshots and
can have the same transient-write gap.

## What is captured

`DDDISP` event records contain:

* event kind: `pre`, `post`, `delay-pre`, `delay-post`, `boundary`, or
  `eret`; delay events are emitted by the recursive branch helper immediately
  around the delay-slot operation, not inferred from the outer run loop;
* CPU mode, guest PC, opcode (when the PC is in RDRAM);
* run-queue head and running-thread pointers;
* selected-thread state, SP, RA, and saved PC;
* selected-thread saved SR plus `sAudioThread` state, queue, SP, RA, saved
  SR, and saved PC/EPC;
* CP0 EPC/status, plus explicit `ERET` EPC/target fields.

`DDSTORE` records contain the source PC/mode, virtual and physical address,
provenance (`source=direct` for a post-memory-handler write or
`source=shadow` for an inline-JIT change noticed at a boundary), width,
value/mask supplied to the memory handler, and the resulting RDRAM word(s).
For an 8-byte record, `value=`, `mask=`, and `after=` print the architectural
high word at the lower address followed by the low word at `address+4`.
The store classes are `run-queue`, `running-thread`,
`audio-state`, `audio-context`, `audio-other`, `selected-state`, and
`selected-context`, `running-state`, and `running-context`.  The audio watch
window is the full `sAudioThread` object window
`[0x807999d0, 0x80799b10)`; selected-thread classes use the current run-queue
head or running-thread pointer and the same OSThread-sized window.

Direct audio writes update the shadow immediately while holding the same
publication lock, so a later boundary does not report the same write again as
shadow evidence.  A direct write before the first shadow baseline is folded
into that baseline rather than being misattributed.  The dispatcher event
ring has 512 entries and the store ring has 256 entries.
There is no per-event file I/O.  The rings are appended to the existing
watchdog dump only when a dump is requested.

## Reproducing and collecting a dump

Build the normal debug core, run the EK 64DD route, and request a watchdog
dump.  The existing force-dump mechanism is sufficient; it does not require a
new command-line switch:

```sh
# On the device, with the package-specific path substituted:
P=org.mupen64plusae.turnip.pwnedbygary.debug
D=/data/data/$P/files
run-as "$P" touch "$D/wd_force.flag"
run-as "$P" cat "$D/wd_stall.txt" > wd_stall.txt
```

On the new dynarec, the existing sample hook polls `wd_force.flag` and emits
the normal `iplram_force.bin`/`wd_stall.txt` artifacts.  Pure and cached
interpreter runs use the same watchdog but do not poll that dynarec-only flag;
wait for their normal watchdog stall trigger (or use an existing CPU-side
dump trigger).  The new section in `wd_stall.txt` is delimited by:

```text
DDDIAG v1 ...
DDDIAG_COUNTS ...
DDDISP ...
DDSTORE ...
DDDIAG_END
```

The existing `doc/HANDOFF.md` operational recipe has the complete build,
install, and package-specific collection commands.  The full-RDRAM dump uses
the byte-swapped representation documented there; `DDDISP`/`DDSTORE` lines
are plain text and do not require byte swapping.

The cheap source-level regression checks for the delay hooks, lock
publication, dynarec unknown-source label, provenance reconciliation, and
64-bit word order are run without a ROM:

```sh
python3 mupen64plus-core/upstream/tools/test_n64dd_dispatch_diag.py
```

## Reading one pass

For a cached-interpreter reference run, filter the section by
`kind=pre|post|delay-pre|delay-post`, then follow `pc` from `0x80746f64`
through the end of the dispatcher span (`0x807470e4` exclusive).  Compare
each normal or recursive delay-slot pre/post pair with the following fields:

1. `runq`, `selected_*`: queue selection and candidate thread;
2. `running`, `audio_state`, `audio_*`: running-thread and audio-thread
   state/context writes;
3. `kind=eret`, `epc=... target=...`: restored exception target;
4. `DDSTORE` records: exact C-path stores and their resulting words.

For a dynarec run, treat `kind=boundary` and shadow-generated
`DDSTORE` records as interval evidence rather than a complete instruction
trace.  A missing transient inline write is a stated instrumentation gap, not
evidence that the guest did not execute that write.