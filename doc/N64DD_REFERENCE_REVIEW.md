# N64 / 64DD Reference Review

## Scope and method

This is a static source audit of the Phobos reference tree against the
checkpoint Mupen64Plus implementation in this repository.  It covers:

* MIPS CPU exceptions, `ERET`, interrupt admission and timing;
* RSP scheduling, bounded execution, task resume/yield, DMA, and address
  wrapping;
* CPU/RSP savestate coverage; and
* the distinction between ordinary cartridge execution and 64DD execution.

Checkpoint source paths below are relative to the repository root; Phobos
paths retain their upstream `ares/...` names.  No emulator workflow or runtime
test was used for this review.  The checkpoint has diagnostic/watchdog code in
a number of these paths; those diagnostics are not treated as hardware
behavior.

## Executive summary

The checkpoint is not a cycle-equivalent implementation of the Phobos model.
Phobos schedules the CPU and RSP as separate clocked threads and models queued
RSP DMA.  The checkpoint executes the RSP plugin synchronously from the CPU
side and adds a 64DD-only background pump plus host-budget yields to keep DD
tasks from monopolizing the emulation thread.

The DD work is generally runtime-gated: the short-slice/background path checks
`IsDDPresent()`/`g_dev.dd.idisk` and ordinary cartridge tasks retain the
stock, synchronous route.  This is an important compatibility boundary, but
it does not remove the following high-impact differences:

1. Checkpoint regular exceptions always use `0x80000180`, overwrite EPC even
   when already in EXL, and do not implement Phobos's BEV/TLB-vector matrix.
2. Checkpoint `ERET` treats ERL as an error instead of returning through
   ErrorEPC and clearing ERL.
3. Checkpoint RSP DMA copies synchronously, whereas Phobos exposes pending,
   busy/full, and transfer-latency state to the RSP.
4. The checkpoint core savestate includes SP registers and SP memory but has no
   serializer for the parallel RSP plugin's hidden `CPUState`.  A save during
   an active RSP task therefore cannot restore all RSP architectural state.
5. The plugin's RDRAM DMA path masks into an 8 MiB window without checking the
   configured 4 MiB/8 MiB `dram_size`; this can differ from Phobos's bounded
   RDRAM behavior.

The highest-priority correctness work is complete parallel-RSP state
serialization/restoration, followed by CPU exception/ERL behavior and a
deliberate decision about 4 MiB RDRAM DMA semantics.

## 1. Runtime boundaries: cartridge versus 64DD

### 1.1 Checkpoint paths

There are two DD-specific RSP paths:

* **Default short-slice model.** `rsp_dd_background_pump()` runs bounded RSP
  work while the CPU continues.  `rsp_dd_slice()` calls the plugin with an
  instruction/host-time budget and keeps an unfinished task locked rather
  than reporting it as complete.
* **Legacy path.** A runtime flag selects longer slices and older forced
  HALT/yield behavior, including synthetic SP interrupt handling.  It should
  not be used as evidence that the default route has the same scheduling
  semantics.

The relevant core logic is in
`mupen64plus-core/upstream/src/device/rcp/rsp/rsp_core.c`, especially the
DD slice/background-pump code around lines 1434-1587.  The plugin side is in
`mupen64plus-rsp-parallel/upstream/parallel.cpp`.

The ordinary cartridge route remains synchronous: `do_SP_Task()` starts the
RSP plugin and ordinarily waits for the task to halt/break.  The plugin keeps
the stock short MFC0 polling timeout for this route (`SP_STATUS_TIMEOUT =
16`).  DD uses the long timeout (`0x7fff`) because host-budget preemption,
rather than the stock polling timeout, is used to return control to the CPU.

### 1.2 What is shared

CPU exception and interrupt behavior is shared by cartridge and DD runs.
Likewise, the parallel plugin's DMA implementation is shared, except for the
explicit DD SP-memory bank wrapping in the DMA address calculations.  A
reviewer should not assume that a DD fix is harmless to plain carts unless
the code is demonstrably behind the DD runtime gate.

## 2. CPU exceptions, ERET, and interrupts

### 2.1 Phobos reference behavior

Phobos `ares/n64/cpu/exceptions.cpp` selects the vector from the status BEV
state.  Ordinary exceptions use the general vector offset; TLB misses have
the special refill offset when EXL is clear.  On entry to an exception:

* EPC and the exception cause are captured on the first EXL entry;
* a fault in a branch delay slot sets BD and backs EPC up by four;
* a nested exception does not destroy the original EPC frame; and
* the pipeline is redirected and flushed.

The NMI path sets BEV and ERL, records ErrorEPC (including delay-slot
correction), and enters the reset vector.  `CPU::ERET()` in
`ares/n64/cpu/interpreter-scc.cpp` handles ERL first: it clears ERL and
returns through ErrorEPC.  Only an ordinary EXL return clears EXL and uses
EPC.  Phobos then performs its interrupt poll.  `ares/n64/cpu/cpu.cpp`
checks interrupts at instruction boundaries, gated by IE and the absence of
EXL/ERL.

Phobos serializes the corresponding CPU status, EPC/ErrorEPC, Cause, TLB,
pipeline, and CPU thread state in
`ares/n64/cpu/serialization.cpp`.

### 2.2 Checkpoint differences

The interpreter implementation in
`mupen64plus-core/upstream/src/device/r4300/cp0.c` has these material
differences:

* `exception_general()` updates Count, sets EXL, handles BD by subtracting
  four from EPC, and jumps to `0x80000180`.  It does not select the vector
  from BEV and overwrites EPC on a nested exception.
* `TLB_refill_exception()` has the normal/general-vector split used by this
  Mupen implementation, but does not match Phobos's complete BEV and
  32-bit/64-bit refill-offset matrix.
* `nmi_int_handler()` is a separate reset/PIF-HLE path in
  `interrupt.c`; it should not be conflated with regular exception entry.
* The interpreter `ERET` in `mips_instructions.def` reports ERL as an error
  and stops instead of returning through ErrorEPC.  The new dynarec's
  `ERET_new()` has the same fundamental ERL behavior, although it adds
  DD-specific pending-interrupt/lost-interrupt rechecks.

This is more than a vector-address difference.  A nested exception and an ERL
return affect the guest's saved execution frame, so the differences can
change recovery code, boot/reset handling, and DD interrupt handshakes.

### 2.3 Interrupt admission and timing

Checkpoint interrupt delivery is CP0-event-queue driven:

* `r4300_check_interrupt()` latches Cause and queues a CHECK_INT event only
  when IE is set, EXL and ERL are clear, and an enabled interrupt is pending.
* `raise_maskable_interrupt()` can call `exception_general()` immediately
  when the same conditions permit delivery.
* `gen_interrupt()` dispatches the queued CP0 event.

These are in
`mupen64plus-core/upstream/src/device/r4300/interrupt.c`.  The queue and
next-interrupt value are saved by the core savestate code, but event dispatch
is still tied to the cached-interpreter/dynarec hooks and CP0 Count timing,
not to one centralized Phobos-style pre-fetch poll.  The DD-specific
lost-interrupt rechecks in `new_dynarec.c` indicate that an interrupt can
otherwise be latched while no new event is admitted because IE/EXL/ERL was
temporarily blocking it.

## 3. RSP execution and scheduling

### 3.1 Phobos

Phobos represents the RSP as a scheduler `Thread`.  The RSP loop advances
pipeline clocks, models instruction/pipeline effects, and steps DMA as those
clocks become available.  CPU and RSP therefore have independently
serialized thread clocks and can make progress through the system scheduler.

Relevant files:

* `ares/n64/rsp/rsp.cpp` — instruction loop and stepping;
* `ares/n64/rsp/dma.cpp` — queued DMA transfer timing;
* `ares/ares/scheduler/thread.cpp` and `thread.hpp` — thread clock/stack;
* `ares/n64/rsp/serialization.cpp` — RSP state serialization.

### 3.2 Checkpoint

`DoRspCycles()` in `mupen64plus-rsp-parallel/upstream/parallel.cpp` runs the
parallel RSP JIT synchronously on the CPU/emulation thread.  The plugin does
not model the input `Cycles` argument as RSP hardware clocks.  Plain carts
run until halt/break; the DD path uses a bounded instruction budget, a host
time backstop, and explicit state handoff to let the CPU continue.

At each invocation the plugin:

1. invalidates/checks IMEM;
2. imports `SP_PC_REG & 0xfff` into the hidden JIT CPU state;
3. runs until break, halt, an interrupt condition, or the host budget; and
4. exports the current low-12-bit PC back to `SP_PC_REG`.

The core preserves the SP PC's high bank bits around the plugin call.  This
is why a budget yield can resume at the right IMEM/DMEM bank even though the
plugin itself executes with a low-12-bit PC.

The DD default path marks the task locked/unsafe while a bounded slice is in
progress.  A budget yield must not be treated as a completed RSP task:
acknowledging it as a normal HALT/SP completion can cause repeated full
reruns and starve the CPU.  The plugin's DD-only status handling in
`parallel.cpp` intentionally differs from the plain-cart completion
handshake.  The legacy route has different forced-HALT behavior and should
remain separately documented.

This is a scheduling approximation, not the same model as Phobos.  It is
appropriate to judge it by guest-visible task progress and interrupt
ordering, not by assuming `DoRspCycles(Cycles)` consumes the same amount of
RSP time as Phobos.

## 4. RSP DMA and memory semantics

### 4.1 SP memory

Phobos exposes separate 4 KiB DMEM and IMEM objects.  CPU/RSP accesses wrap
through the 4 KiB address mask; IMEM writes also invalidate compiled code.
The RSP DMA register fields are typed: a 12-bit PBUS address plus region bit,
a 24-bit RDRAM address, and length/count/skip fields.

Checkpoint's parallel JIT uses the same 4 KiB SP-memory address space, and
the plugin's DMA destination calculations apply 12-bit/bank masks.  Ordinary
cartridge DMA clamps at the bank boundary.  The DD route adds bank-limited
wrapping so a transfer does not silently cross from DMEM into IMEM (or the
reverse) when the DD task relies on the bank bit.

The plugin invalidates all RSP JIT IMEM blocks after DD IMEM DMA.  This is
needed because DD ucode can DMA new instructions and execute them within the
same host slice.

### 4.2 RDRAM address and installed-size behavior

Checkpoint RSP DMA masks RDRAM source/destination words into an 8 MiB
(`0x7ffffc`) window.  Phobos also has a 24-bit RDRAM DMA field, but its RDRAM
mapping checks the configured installed size.  The core can configure 4 MiB
or 8 MiB RDRAM; a raw plugin pointer plus an unconditional 8 MiB mask does
not express that distinction.

Consequences to resolve deliberately:

* On a 4 MiB checkpoint configuration, an address in the upper 4 MiB can
  access the backing allocation instead of behaving like an out-of-range
  RDRAM access.
* If hardware compatibility requires upper addresses to be ignored or read
  as zero for 4 MiB, the plugin must receive/enforce `dram_size`, not only
  mask to 8 MiB.
* If the checkpoint intentionally models an 8 MiB physical window even when
  the configured allocation is 4 MiB, that policy should be documented and
  tested because it differs from the Phobos reference mapping.

The relevant comparison is
`mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp` versus
`ares/n64/rdram/rdram.hpp` and `rdram.cpp`.

### 4.3 DMA latency and status

Phobos queues DMA and serializes pending/current transfer state, busy/full
flags, and DMA clock.  Ucode can observe a transfer in progress.  The
checkpoint plugin performs the data copy inside the CP0 handler, then uses
plugin/core flags and completion bookkeeping.  It has no equivalent
cycle-by-cycle transfer queue visible to the RSP.  This can affect ucode that
polls `SP_DMA_BUSY`, `SP_DMA_FULL`, or relies on DMA/CPU ordering.

## 5. Savestate completeness

### 5.1 Phobos

The Phobos system serializer includes the scheduler queue, CPU, RSP, and
device state.  RSP serialization includes, among other fields:

* thread clock/stack and execution pipeline;
* DMEM and IMEM;
* pending/current DMA, busy/full flags, and DMA clock;
* SP status/semaphore/interrupt state;
* scalar and vector registers, accumulator/divider state;
* PC, next PC, branch state, and pipeline-related state.

The RSP recompiler is reset/rebuilt after loading serialized architectural
state rather than relying on stale compiled blocks.

### 5.2 Checkpoint core and plugin

`mupen64plus-core/upstream/src/main/savestates.c` serializes SP MMIO
registers, SP PC, the 8 KiB SP memory, RDRAM, R4300 registers/CP0/TLB, and
the event queue.  On load it resets `rsp_task_locked` and the core's
`interrupt_unsafe_state`.

That is not the complete state of the parallel RSP.  The plugin's hidden
`CPUState` in `mupen64plus-rsp-parallel/upstream/state.hpp` contains the
RSP GPRs, CP2/vector state, PC/branch-delay fields, memory aliases, and JIT
dirty/cache state.  `InitiateRSP()` wires pointers to core buffers and CP0
registers, but there is no Mupen core savestate callback that serializes the
hidden CPU state.

A mid-task save/load can therefore restore SP PC and memory while retaining
stale hidden GPR/vector/branch state from before the load.  Resetting
`rsp_task_locked` makes the core willing to schedule another task, but does
not reconstruct the interrupted RSP instruction stream.  Invalidating IMEM
blocks on the next call addresses self-modifying code, not the missing
architectural register state.

Recommended implementation direction:

1. Add an explicit parallel-RSP serialize/unserialize boundary owned by the
   plugin/core integration.
2. Include RSP GPRs, CP2/vector/accumulator/divider state, PC/branch-delay
   fields, status/semaphore and any in-flight DMA/slice state.
3. Rebind DMEM/IMEM/RDRAM pointers after load and invalidate/rebuild JIT
   blocks.
4. Preserve the distinction between a genuinely halted task and a DD host
   budget yield.

## 6. Behavior matrix

| Area | Plain cartridge | 64DD checkpoint route | Phobos reference |
|---|---|---|---|
| CPU exceptions | Shared checkpoint CPU behavior | Shared checkpoint CPU behavior | BEV/ERL-aware exception model |
| CPU interrupt admission | CP0 event queue and Count hooks | Same, plus DD lost-interrupt rechecks | Instruction-boundary poll |
| RSP scheduling | Synchronous plugin task | DD background pump and bounded slices | Independent clocked RSP thread |
| `DoRspCycles` | Runs to halt/break; no hardware-cycle consumption | Host/instruction budget can yield | Scheduler clock is consumed by RSP execution |
| DMA | Immediate plugin copy | Immediate plugin copy with DD bank wrapping | Queued transfer with latency/busy/full state |
| SP PC handoff | Core/plugin low-PC handoff | Same, with task locking and high-bank preservation | Serialized RSP PC/branch pipeline |
| Savestate | Core SP/RDRAM state, hidden plugin state omitted | Same omission, plus task lock reset | Complete CPU/RSP/thread/DMA state |
| RDRAM DMA | Unconditional 8 MiB mask in plugin | Same mask | Installed RDRAM size is checked |

## 7. License and provenance review

### Supplied Phobos archive (separate from the workspace licenses below)

The archive root `phobos-master/LICENSE` begins with the ares team's
2004–2025 ISC-style permission and disclaimer. This is the relevant top-level
notice for the inspected `ares/n64` source, not the workspace's GPL license.
The same archive license file separately lists the stack-less JIT compiler
(two-clause BSD-style), libchdr (three-clause BSD-style), public-domain LZMA
SDK, and zlib, among other bundled notices. Preserve the applicable notice
for each component if code is adapted; do not label the whole archive with
one license. This investigation adapted no Phobos implementation code.

Phobos citations in this document are archive-relative paths; they are not
files imported into the workspace repository. The archive is the durable
reference input. Source comparison does not establish that this snapshot
boots the Expansion Kit.

This section records the license distinctions visible in the tree.  It is a
source-distribution checklist, not legal advice.

### 7.1 Root project versus vendored N64 sources

The repository root file `gpl-license` is the full **GNU GPL version 3**.
`README.md` also describes the application as GPLv3.  That root declaration
does not silently relicense every vendored upstream component.

The Mupen64Plus source trees carry their own notices:

* `mupen64plus-core/upstream/LICENSES` identifies Mupen64Plus-Core as
  **GPLv2** and points to `mupen64plus-core/upstream/doc/gpl-license`, which
  is GPL version 2.
* `mupen64plus-rsp-hle/upstream/LICENSES` identifies the HLE RSP as GPLv2.
* `mupen64plus-input-raphnet/upstream/LICENSES` and its `COPYING` identify
  the Raphnet input plugin as GPLv2.
* `mupen64plus-video-glide64mk2/upstream/LICENSES`,
  `mupen64plus-video-gliden64/upstream/LICENSE`, and
  `mupen64plus-video-rice/upstream/LICENSES` identify those plugin trees as
  GPLv2.

The practical distinction is important: an application-level GPLv3 notice is
not proof that a GPLv2-only upstream component can be relicensed as GPLv3.
Preserve each upstream notice and distribute the applicable license text.
Where a source file explicitly says “GPLv2 or any later version,” that
particular notice may provide additional choice; the directory-level
`LICENSES` declarations should still be treated as authoritative until the
individual file headers are audited.

### 7.2 RSP/RDP components directly relevant to this review

| Component | License observed | Distribution distinction |
|---|---|---|
| `mupen64plus-rsp-parallel/upstream` | Dual **MIT or LGPLv3** (`LICENSE`) | Choose and preserve one permitted licensing path; MIT requires retaining the copyright/permission notice, while LGPLv3 carries copyleft/library and combined-work obligations. |
| `mupen64plus-rsp-parallel/upstream/lightning` | **GPLv3 or LGPLv3** | Applies when statically linking GNU Lightning. The parallel-RSP notice says it can instead dynamically link Lightning or omit it. Do not describe the whole RSP as MIT without accounting for the selected Lightning configuration. |
| `mupen64plus-rsp-cxd4/upstream` | **CC0 1.0** (`COPYING`) | Permissive public-domain dedication/fallback for the covered work; retain provenance and do not assume this license applies to other Mupen components. |
| `mupen64plus-video-parallel/upstream` | **MIT** (`LICENSE`) | Preserve copyright and permission notice; separate from GPL-licensed classic video plugins. |
| `mupen64plus-video-angrylion-plus/upstream` | Custom **MAME license** | Prohibits sale/commercial products or activities, requires complete source for modified distributions, and restricts MAME trademark use. This is not ordinary BSD/MIT/GPL and requires a separate distribution review. |

### 7.3 Other bundled third-party distinctions

The core license inventory in
`mupen64plus-core/upstream/LICENSES` also calls out OGLFT
(GPL/LGPL-derived), minizip and the CIC 6105 source (BSD), Adler-32/libpng/
MD5 code (zlib/libpng), and the TrueType font (Bitstream license).  Other
license files in the repository include:

* `adrenotools/LICENSE`: BSD 2-Clause;
* `mupen64plus-input-raphnet/libusb/COPYING`: LGPL 2.1;
* `mupen64plus-video-gliden64/upstream/licenses/gles2n64/LICENSE`: LGPLv3;
* `mupen64plus-video-gliden64/upstream/licenses/Glow/LICENSE`: permissive
  MIT-style terms;
* `mupen64plus-video-gliden64/upstream/licenses/readerwriterqueue/LICENSE.md`:
  Simplified BSD, with the embedded semaphore identified as zlib;
* `mupen64plus-video-glide64mk2/upstream/doc/fxt1-license`: a custom 3dfx
  FXT1 source license with source/notice requirements for identifiable
  texture-compression derivatives;
* `ndkLibs/libnatpmp/LICENSE` and `ndkLibs/miniupnp/LICENSE`: BSD-style
  terms with attribution/disclaimer requirements;
* `ndkLibs/png/LICENSE`: libpng license; and
* `ndkLibs/soundtouch/COPYING.TXT`: LGPL 2.1.

These are compatible in different ways and are not interchangeable labels.
In particular, permissive MIT/BSD/zlib components do not remove GPL/LGPL
obligations from the Mupen-derived components they are combined with, and
the MAME/FXT1 terms need to be carried separately rather than summarized as
“GPLv3 app.”

### 7.4 Distribution checklist

Before distributing a build containing these N64 modules:

1. Include the root GPLv3 text for project material that is actually released
   under the root declaration.
2. Include the upstream GPLv2 text and notices for Mupen64Plus core/HLE and
   GPLv2 plugins; do not replace them with only the root GPLv3 file.
3. Include `LICENSE`, `LICENSE.MIT`, and/or `LICENSE.LESSER` for the selected
   parallel-RSP configuration, plus Lightning's applicable license if linked.
4. Keep BSD, MIT, zlib/libpng, LGPL, CC0, Bitstream, FXT1, and other notices
   with the corresponding source/binary distribution.
5. Treat Angrylion's MAME license as a separate non-commercial restriction.
   Confirm that the intended package and any store/distribution channel do
   not violate its “may not be sold/commercial product” term.
6. Do not package N64 ROMs, BIOS/IPL images, or DD disk images as though they
   were covered by any software license listed here.  Their copyright and
   redistribution status must be established independently.

## 8. Recommended priority order

1. Add complete parallel-RSP architectural/DMA/slice savestate support.
2. Correct ERL `ERET`, nested EPC preservation, and BEV/TLB vector selection,
   or document the intentional compatibility target if exact behavior is
   not desired.
3. Decide and test installed-size-aware RDRAM DMA behavior for 4 MiB systems.
4. Add targeted plain-cart and DD tests for SP DMA busy/full visibility,
   budget-yield resume, SP interrupt acknowledgment, and pending CPU
   interrupts after `ERET`.
5. Audit packaging notices against the license table above, especially the
   parallel RSP's selected MIT/LGPL/Lightning mode and Angrylion's
   non-commercial MAME terms.