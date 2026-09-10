# Handoff Summary: F-Zero X EK on 64DD (mupen64plus-ae-turnip)

> Auto-generated checkpoint. Read top-to-bottom. The single biggest concrete progress this
> session: **the missing-DD-ROM root cause is fixed and verified** — the DD ROM and disk now
> load, so the remaining black-screen fault is a *different, later* problem that needs fresh
> diagnosis rather than the prior MI-interrupt fix.

**ROUND 8 (the machine was RSP-starved: 100 % of the emulation thread inside the RSP):**
commit `3602d76cf`. The DD route handed the RSP a **100 ms (audio) / 50 ms (everything else)**
host-run budget. Measured live: every `DoRspCycles` call took a full 50-100 ms of **wall** time and
they ran strictly back-to-back (~20/s) — 100 % of the emulation thread inside the RSP — while the
dynarec's interrupt hook froze (`wd_c_sample` delta 0) because the CPU never got a slice. On real
hardware a spinning RSP does **not** block the CPU, so the budget must interleave: now **2 ms**
(still millions of RSP cycles, far more than any real task slice). The budget-yield path is
unchanged and still emits the clean `HALT|INTR_BREAK|irq` boundary.

**Effect: the boot ADVANCED past the deadlock that had held it for 12+ minutes.** Before, the guest
sat forever at `sys_gfx.c:198` (`while (osViGetCurrentFramebuffer() != gFrameBuffers[i]) {}`) with
`guest viCurr.framep` pinned at `0x80200000` and the libultra `viEventQueue` empty. Now the swap
completes (`viCurr.framep = 0x801D9800` = framebuffer1) and the screen reaches the F-Zero X EK
**"DD LOADING" screen with its progress bar**, instead of the bare Nintendo 64DD logo.

**New diagnostic instrumentation, all DD-gated (`g_dev.dd.idisk != NULL`), all pure counter
increments or watchdog-thread reads — no hot-path I/O:**
- `wd_c_exc_total/int/nested` in `exception_general()`, classified **before** EXL is set, so
  `nested` really means "an exception taken while EXL/ERL was already set" — i.e. a synchronous
  `exception_general()` from `raise_maskable_interrupt()` stomping an in-flight exception frame.
- `wd_c_raise` / `wd_c_signal` / `wd_c_raise_bits[8]` (per `MI_INTR` source), `wd_c_cmp_int`,
  `wd_c_vi_evt`, `wd_c_vi_ack`.
- `wd_stall.txt` gains **DELTA2** (exc/raise/VI rates, per-source raise deltas) and a **CP0 EVENT
  QUEUE dump** (`type`/`count`/`delta` per node) — `gen_interrupt` dispatches on
  `cp0.q.first->data.type` and the `VI_INT` handler is what re-arms the next vertical interrupt, so
  a lost VI shows up exactly there. It also dumps the guest's own libultra VI state
  (`__osViCurr`/`__osViNext->framep`, `state`, `retraceCount`, `viEventQueue` validCount/msgCount,
  and the `gFrameBuffers` index).
- `wd_thread` now polls `files/wd_force.flag` **itself** (capped at 8 dumps/run). The old trigger
  lived in `dynarec_sample_hook`, which stops running exactly when the CPU is starved — precisely
  when a dump is wanted.

**What the probe found (first run with it):** `c_vi_evt` froze at **272** and the **VI event is
absent from the CP0 event queue**, whose head is `COMPARE_INT`:

    CP0Q count=0b6c6651
      EV0 type=1  count=0b752fcd delta=575868      (COMPARE_INT)
      EV1 type=32 count=80000000 delta=1955830191  (unexpected type)
      EV2 type=2  count=00000000 delta=-191653457  (CHECK_INT, already due)

With no `VI_INT` node, the guest's `viMgrMain` thread (prio 254, blocked on the viEventQueue) never
receives a retrace and `__osViSwapContext()` never runs. **That is the next bug to fix.**

**Current end state (NOT the objective):** the CPU falls into an infinite **nested** exception loop
— `c_exc_nested` +16.7 M per 300 ms sample (**~55 M/s**), `epc=0x80000408`, `cause=0x9000002c`
(BD|CE|Coprocessor-Unusable), `badvaddr=ffffffff` — while every other counter (`c_task`, `c_genint`,
`c_pi`, `c_sample`) is frozen at zero delta. `epc=0x80000408` is inside the decomp's
`framebuffer_unused` (vram `0x80000400`, filled with `0x0001000100010001` by the game's own
framebuffer fill), i.e. the guest is executing framebuffer fill data. The screen still presents the
DD LOADING screen at 60 FPS because the frontend re-presents the last frame. Objective (load fully,
**reach the menu, clean audio**) is NOT met.

**Round 9 next steps:** (1) make the VI event survive — find who drops it (the `VI_INT` case in
`gen_interrupt` deliberately does *not* remove the event; `vi_vertical_interrupt_event` removes the
queue **head** and re-adds with `add_interrupt_event_count`, so any intervening head insertion
desynchronises it) and re-arm it on the DD route when it goes missing; (2) trace how the PC reached
`0x80000408` (the round-5 stale-code / descramble class again, or a jump through the fill value) —
`wd_pc_ring` + `iplram_force.bin` via the now-working `wd_force.flag` are the tools; (3) re-check the
SP re-dispatch rate now that the budget is 2 ms: `c_task` went 20/s → ~500/s, so each budget yield is
reported to the guest as a task completion; that rate may need a different yield model.

**ROUND 7 (ROOT CAUSE of the post-load stall: the DD SP DMA lost the IMEM bank selector):**
commit `283a01c67`. The round-6 "wrap fix" (244245731) replaced the bank base with the flat RSP
memory base, so `memaddr` -- already stripped of bit 12 by `& 0xff8` -- could never select IMEM:

    spmem = (unsigned char*)sp->mem;   /* no bank base */
    ... spmem[(memaddr & 0x1fff) ^ S8] /* bit 12 already gone -> can never be IMEM */

Upstream re-adds the bank via `spmem = sp->mem + (dma->memaddr & 0x1000)`. Net effect on the DD
route: **EVERY SP DMA landed in DMEM**, IMEM stayed zero, the RSP executed NOPs, and the guest's
audio task never completed -> the F-Zero X EK init spin (see below). Evidence is a new DD-gated
DMA trace (`files/wd_dma.txt`, DMEM/IMEM words after every transfer):

    n=1 mem=04000fc0 dram=007c1c00 len=3f  (osSpTaskLoad header DMA)  after: fc0=00000002 imem0=0
    n=2 mem=04001000 dram=00768e60 len=fff (boot-ucode load, 4096 B)  after: fc0=00010001 imem0=0 dmem0=340a0fc0

i.e. the whole audio ucode (first word 0x340A0FC0) was written over DMEM 0x000..0xFFF, wiping the
OSTask header DMA #1 had just loaded, while IMEM stayed 0. `do_SP_Task` then read
DMEM[0xFC0/4]=0x00010001, classified "other", RSP ran garbage. **After the fix** (same run):
`fc0=00000002` survives, `imem0=340a0fc0`, header DMA `same=64 diff=0`, 48+ header/ucode pairs
instead of 1.

**Also fixed: the budget-yield livelock.** The DD host-budget yield sets HALT|INTR_BREAK, but
`DoRspCycles` then unconditionally cleared HALT (stock CXD4 "task finished" handshake, only valid
when the ucode really finished). With HALT clear and BROKE clear, `do_SP_Task` reads "RSP still
running" -> sets `rsp_task_locked` + raises MI_INTR_SP -> libultra's `exceptasm` acknowledges with
`SP_STATUS=0x8008` (SP_CLR_SIG3|SP_CLR_INTR; verified from the RSP's pc/ra/a0..a3 at
`0x80746AE4`) -> that passes `update_sp_status`' gate because `rsp_task_locked` is set and HALT is
clear -> another full 50 ms run. Measured: **1362 RSP runs in 68 s, each 50 ms = 100 % of the
emulation thread, with only 2 SP DMAs in the whole run.** Now HALT is left set after a budget
yield; a second new trace (`files/wd_spw.txt`, guest pc/ra/sp/a0..a3 at every SP_STATUS write)
shows all 132 interrupt-ack writes with `st_before=0x243` (HALT set) so they no longer re-enter.

**Where the guest actually was (decomp-assisted, `.fzxwork/fzerox-decomp`):** the CPU spun at
`0x806F40B4` in the **EK's own `sys/sys_gfx.c`** (`while (func_80742790() != 2) {}`, the
`#ifdef EXPANSION_KIT` block right after `Arena_DefaultStartInit()`), waiting for a DD-BGM/audio
status that only advances when the audio-disk state machine (`external.c` + `audio/disk/lib/load`)
completes. Its `func_80742790` is `return *(u8*)0x80771C88;`, set to 2 only after three
`audio/disk/lib/load.c` calls all return 1. The exception-state epc `0x8074651C` is inside
`libultra/io/sptask.c` (`osSpTaskStartGo`'s `while (__osSpDeviceBusy())` + `__osSpSetStatus(0x125)`),
which is what identified the `w=0x2B00`/`0x125`/`0x8008` writes.

**Verified after the fix:** 132 loads <-> 132 `osSpTaskStartGo` <-> 132 acks (balanced, healthy);
RSP task types observed 195x M_AUDTASK + 8x M_GFXTASK (before: none ever); `c_asic` 5234 -> 10604
and `c_pi` 5786 -> 11650 (roughly twice the DD work); the screen advanced from the F-Zero X EK
loading bar to the **Nintendo 64DD logo at 59 FPS**.
**Plain-cart regression PASSES:** Mario Kart 64 Amped Up v3.21 renders the "real N64 console?"
screen at 60 FPS with no crash, and the DD diagnostics are provably inert (`files/wd_dma.txt` not
created, `files/wd_smc.txt` = 22-byte header only). The non-DD branches of `do_sp_dma` are now
byte-identical to upstream again (the bank base is the same expression on both routes); the only
DD-gated difference is the `& 0xfff` offset wrap inside the selected bank.

**Known remaining issues (NOT regressions - both were previously unreachable because the machine
livelocked before getting there):** (1) one run died with **SIGILL (ILL_ILLOPC) in the dynarec JIT
arena** (`[anon:.bss]`) ~50 s in, with `wd_smc.txt` showing the self-modifying region
`0x800bba34..0x800bbd40` active - the same descrambling/stale-code class as round 5's
`stop_after_jal` fix (c12de6262); (2) a later run stayed alive 100 s and rendered the 64DD logo but
still wrote a stall dump. The objective (reach the menu with clean audio on the RP6) is NOT yet met.

**Next (round 8):** (1) reproduce the SIGILL and capture the guest state at the fault (the
`iplram_*` force dumps + `wd_smc.txt` ring are the tools) - if it is another speculative-JAL /
descramble stale-code case, extend the round-5 gate; (2) re-check what the 20/s `ttype=65537` RSP
entries are now that tasks are balanced (audio ucode clobbering DMEM 0xFC0 mid-task is expected
F3DEX/audio behaviour, so this may be benign); (3) once the boot completes, verify audio quality and
the menu.

**ROUND 6 (audio budget 100ms → 10ms):** REAL structural progress. The machine now schedules properly: guest CPU reaches the IDLE thread (snapshot header pc=ra=0x806f32ec, sp=0x80795a40) instead of the IRQ storm; the DD-loading bar ADVANCED a segment (5.5/8 → 6.5/8) in one run before settling at the LEO-completion wait. Remaining chain state (wd_state5.bin): sSLLeoMesgQueue (0x8079F978) valid=0, gDmaMesgQueue valid=0, LEOcommand_que valid=0, LEOblock_que valid=1 (manager between commands — the INQUIRY completed, the loader's read command never dispatched); the LEO cmd ring (0x807c6f10, 7-word entries) contains stale boot-fill (0x88776655 markers) — the read command was never cleanly issued. The guest's LEO-lib chain is blocked at every queue layer; the DD controller still sees zero activity (k=5/7/4 = 0). Note: OLD (Sep-1) boot's DD activity (k=7 status read + reads) = the disk-first/old boot-strategy; current = combo/cart-first — the guest's DD chain reaches a different point. 10ms audio budget stays in the tree (nice win: the machine's scheduler works, the bar moves).

**Next (round 7):** (1) trace the guest's LEO-lib command send (which queue/where the loader's read command is dropped — the worker's case-0 SLLeoReadWrite vs the manager's LEOcommand_que); (2) compare the DD-drive's required state at the cart-boot against the mupen poweron (dd_dv_sleep = drive sleeping; command → drive wake/mecha sequence vs ares's DD_Clock_Tick); (3) the boot-strategy (cart-first vs disk-first) impact on the guest's DD chain.

**ROUND 12 (final — isolation attempts):** stashed ALL session changes (baseline = prior-session tree) and built; the android VIEW game-launch route proved unreliable after the crash (app process alive but game never spawned — no EmulationProcess), so the baseline Mario Tennis comparison is INCONCLUSIVE. Session changes restored (stash pop). The crash from round 11 was on the session tree; the culprit isolation remains OPEN: (a) the adb game-launch needs the app's gallery/UI tap or a stable re-launch sequence after a crash; (b) the isolated candidates in priority order: 10 ms audio budget (dd-agnostic, ttype==2), parallel.cpp b8-capture + flush batching, then the committed aeb31cea1 ForceSynchronize (never plain-game tested), then the keep-assert (DD-gated — should be inert on plain). The F-Zero X EK overall goal is NOT complete: the machine now schedules and the DD-loading bar advances with the session fixes, but the DD data-load chain still blocks at the guest's LEO command send (before any DD command execution), and the plain-game regression must be isolated/fixed before completing.

**ROUND 11 (plain-game regression CONFIRMED — crash):** the plain-game run (Mario Tennis via the app's Gallery launch — note: the VIEW intent needs `%28`/`%29` escaping for parentheses and the app must be foregrounded first) **crashed the EmulationProcess** (SIGSEGV at 11:34:22): backtrace `libmupen64plus-rsp-parallel.so RSP_MTC0+732 → libmupen64plus-video-glide64mk2.so ProcessRDPList+216` — the parallel-RSP's MTC0 path dispatching into the glide64mk2 RDP-list processor. This is the mandatory regression check failing on the current tree (keep-assert + 10 ms audio budget + diagnostics + the Sep-6/7 committed ForceSynchronize etc.). The audio budget change is dd-agnostic (10 ms only for ttype==2 audio, 50 ms otherwise), so the crash may be the committed handshake (aeb31cea1 ForceSynchronize never tested on plain games) rather than my session edits. ISOLATION PLAN (round 12): git-stash my session's changes one at a time (10ms budget, b8-capture, keep-assert is dd-gated=off for plain) and re-run Mario Tennis until the crash is isolated; then fix + re-verify EK + plain games.

**ROUND 10 (leoCommand path + regression attempt):** the guest's `leoCommand` (leofunc.c:62) gates every command on the **LEOblock_que token** (`osRecvMesg(&LEOblock_que, BLOCK)` before queueing, then `osSendMesg(&LEOblock_que, …)` after) — at the freeze the token IS present (LEOblock_que valid=1), consistent with the INQUIRY having completed. The critical guest expectation found in leocmdex.c: for non-INQUIRY commands it first reads LEO_ID_REG and **spins in `while(1){}` if `(id & 0x70000) != 0x40000`** (the dev-drive ID) — while mupen's poweron sets ID=0x00030000 (retail; 0x00040000 only for `disk->development`). The machine's zero DD activity means the guest never reached even this check; the missing link is upstream (the manager's init `osEPiReadIo(LEO_STATUS)` and the createleomanager's command send). Possible emulator-side mismatch worth testing: the guest's ID check expects 0x40000 — consider setting the drive ID accordingly (DD-gated) IF the guest ever reaches the check.
- **Plain-game regression check: BLOCKED on launch** — the app does not open via the VIEW intent for Mario Tennis from the SD (ROMs are on volume EBFF-F6C0; the app bounced to the launcher). Must be verified via the app's gallery/UI or the established launch sequence before finishing. ALSO unverified: the dd-agnostic 10 ms audio-budget change on plain games (consider DD-gating it if any regression appears).

**ROUND 9 (guest LEO command state + routing):** decoded the guest's LEO command manager (leocmdex.c `leomain`: `osRecvMesg(&LEOcommand_que, &LEOcur_command)` → dispatches via `leo_cmd_tbl[]` — leoInquiry/leoRead/leoTest_unit_rdy…; the INQUIRY (leoinquiry.c) reads LEO_ID_REG + issues ASIC_READ_PROGRAM_VERSION — a REAL DD register access that should produce k=7). At the freeze, **`LEOcur_command` (0x807C5428 → 0x80798580) = command 0x0** (the drive-reset path), NOT the INQUIRY (0x2) — the boot's INQUIRY was never processed; the manager sits waiting. The guest's `osEPiReadIo(0x05000500)`-style LEO register reads are routed via the memory map (device.c mapping[14] MM_DOM2_ADDR1 → read_dd_regs/write_dd_regs → k=7/k=5) — so the trace's zero DD activity means the guest truly never reached the DD register path. Conclusion: the guest's LEO chain stops BEFORE any DD command execution (the manager processed the reset; the INQUIRY/read commands never enter the manager's queue), so no emulator-side DD event can fix it — the missing link is in the guest-side command send (createleomanager's leoCommand chain) or an earlier guest lock/wait that our emulation doesn't satisfy.

**Next (round 10):** (1) trace the guest's `leoCommand` call path (who sends the INQUIRY/read; what blocks it — probably a lock/queue-full at the lib's command layer, or the boot's LEO-creation waiting for a drive status that should be non-zero — leomain's init does `osEPiReadIo(LEOPiInfo, LEO_STATUS, …)` — the FIRST drive read, absent in the trace; verify the drive's poweron status bits vs what the guest expects); (2) the drive's power-on status (read_dd_regs would show reset-state/presence bits — check if the guest's initial status-read should happen at a different emulated time); (3) plain-game/CI regression check + DD gating of the 10ms budget.

**ROUND 8 (IPL-first boot experiment — tested + reverted):** switched the boot strategy for the combo (device.c) to boot via the 64DD IPL (the Sep-1-style path with the drive's own init). **Result: the machine stays ALIVE at the IPL splash (~6 min, VI + frames still firing, no freeze) but crawls in the IPL's C2S per-0x80 cart copy (k=6 reads at 0x13d7xxxx every 0x80 bytes) and never reaches the DD; the drive still shows zero DD activity.** Reverted to cart-first (the comment documents the experiment). Net: the IPL path exchanges the LEO-chain deadlock for the C2S crawl — neither completes; the cart-first + keep-assert + 10ms budget remains the best state (machine schedules; bar advances; LEO chain blocked at the manager/executor).

**Next (round 9):** (1) the C2S-verified hypothesis suggests the DD chain needs the drive's power-on/init events at the RIGHT time (not a one-shot at 0.66 s) — check the guest's LEO executor wake semantics precisely (what the 2.0J libultra's CART int must look like at the command boundary; whether the INQUIRY should be a REAL DD inquiry, not lib-local); (2) consider making the INQUIRY/read commands drive the DD when the guest issues them (verify the guest's command flow actually reaches the LEOcommand path — the missing link might be a guest-side watchdog/lock); (3) plain-game/CI regression check of the keep-assert + 10ms budget + (pending) DD gating of the budget change.

**ROUND 7 (drive power-on MECHA experiment + boot-strategy analysis):** implemented an ares-style power-on MC/MECHA event (poweron_dd schedules DD_MC_INT at ~0.66 s when disk present → dd_mecha_int_handler → signal_dd_interrupt(MECHA) → CART IP3). **No effect:** the DD chain still shows zero DD activity (k=5/7/4=0, only k=6 CART PI reads at 0x13d7xxxx); the machine still settles at the same LEO wait (snapshot header again at the exception vector in this run). The guest's LEO executor never reaches the DD because its second command needs the drive's CART/DD interrupt, which the command-driven mupen DD only produces from commands the executor never issues (deadlock); the power-on event timing (0.66 s) or the __osLeoInterrupt guard likely misses the guest's window.
- Boot strategy confirmed in device.c:195-210: combo (cart+disk) boots the CART directly; disk-only (dummy 1MB cart, rom_size ≤ 0x100000) boots via the 64DD IPL. The Sep-1 baseline's DD activity (k=7 status read + DD disk reads) came from the pre-combo state — the cart-first path reaches a different point of the guest's DD chain. The disk-only/IPL boot is the untested alternative.
- The 10 ms audio budget remains the strongest structural gain (machine schedules; the bar advances).

**Next (round 8):** (1) test the disk-only/IPL boot path (or the SEP-1 pre-combo configuration) to see the guest's DD chain complete; (2) deep-dive the guest's LEO executor wake logic (leoint: how the second command's CART int should arrive on real HW — the INQUIRY's real DD response vs the lib-local completion); (3) then the plain-game/CI regression check of the keep-assert + 10ms-budget changes (the RSP budget change is currently dd-agnostic — consider gating to DD-only).

## UPDATE 2026-09-07 (goal round 5) — flush batching + host-saturation analysis
- Batched the per-line fflushes in the RSP EXIT log and the VI FIRE/WRITE/ADD logs (they were a 17.5 GB flush backlog on the RP6 flash). The stall is UNCHANGED — the freeze is NOT log-I/O.
- In the freeze, the last RSPBUDGET marker (rsp_jit.cpp:2090) shows the audio ucode budget-preempted at pc=0x0098, status=0x40, imem=0x00010001… (the ucode's wait/data words). ALL RSP tasks end in the yield state (status=0x41 = HALT+INTR_BREAK, never BROKE) — the audio ucode never completes in its slices; it keeps polling without finishing, and each task burns the full host budget (50-100 ms). With ~20 tasks/s × up to 100 ms the emulation thread is effectively RSP-saturated and the guest CPU starves.
- Guest chain (verified again): worker id=6 waits on the LEO cmd ring (idx 7 = the loader's read, never dispatched because the game thread never runs); LEO manager prio 149 blocked on LEOcommand_que (valid=0); LEOblock_que valid=1 (the first command's done message pending — the manager is BETWEEN commands); SLLeoReadWrite = LeoReadWrite + osRecvMesg(&sSLLeoMesgQueue) (the worker's real wait).

**Next (round 6):** the emulator-side RSP budget/poll-yield logic — make the audio task yield on its SP_STATUS poll (the 256-poll yield, aa16f997a) instead of running the full fixed budget while waiting for audio data (no audio at the DD-loading phase); reduce/adapt the audio task budget so the emu thread isn't RSP-saturated; verify with the ring. Also re-check the mupen-side re-dispatch cadence (do_SP_Task re-entry per guest task-start).

## UPDATE 2026-09-07 (goal round 4) — recompiler test + LEO chain map
- The LEO manager = thread at 0x807C4838 (prio 149, id=1) — BLOCKED on **LEOcommand_que (0x807C5398), valid=0** — the loader's read command never reached it.
- The LEO interrupt thread = 0x807C49E8 (prio 150) — osStopThread'ed (state=1), waiting for the CART/DD interrupt.
- **LEOblock_que (0x807C53F8) has valid=1** — the previous command's done message is sitting there (the manager's first command completed!).
- LEO control/event/dma queues all empty; gDmaMesgQueue (0x8079A0A8) valid=0; drive state gLeoDriveConnectionState=0x2 (drive connected, sys6 thread started); D_800CD2B0="EFZE" (disk ID read OK).
- The game thread (prio 10, the command issuer) is READY but starved: the ring shows the CPU essentially always in the exception cycle (vector + `__osRestoreInt` MTC0), i.e. ~91% of CPU time inside RCP-interrupt handling (VI 60 Hz + SP ~20-60 Hz). Every guest exception costs megabytes of the interpreter/dynarec emulated time; the machine's interrupt handling saturates the emulated CPU and the lowest-prio game thread never gets a turn.
- Old-vs-new boot trace diff: the Sep-1 working baseline's first events include a k=7 DD-register status read (0x05000508 → 0x01580000) at boot; the current runs show NO DD reg access at all (only k=6 CART PI completions). The current boot path never even queries the drive — the manager's INQUIRY/command flow stalls before any DD MMIO.

**Next (round 5):** (1) quantify the exception/interrupt rate and its emulated cost (add a raise/re-exception counter to the ring); (2) find the redundant re-raise source (SP per RSP task + VI + counter) and reduce the RCP raise rate to the real HW cadence; (3) check why the current boot never does the DD-status query (compare the boot's first events against the Sep-1 trace; verify the drive-presence check path under the combo/cart-first boot).

## UPDATE 2026-09-07 (goal round 3) — keep-assert only; healthier state, final chain

**New decisive evidence (safe ring + DD-trace dump wired into the dumper thread, emumode=1):**
- The stall's data loads are **CART reads** (PI-DMA completions k=6, a=0x1049xxxx — the .z64 image!), 2160 of them, ALL completed. **No DD-disk reads, no DD reg-writes/reads, no DD BM-ints (k=1/2/4/5/7 all absent)** — the DD disk isn't involved at the stall anymore; the loader's reads are the cart's own data.
- But the final read's completion never reaches the guest: `gDmaMesgQueue` (0x8079A0A8) shows **validCount=0** (msgCount=1) in the freeze; the LEO thread (id=6) is idle blocked on its cmd queue (0x807c6e90).
- Freeze snapshot header at the new build: `pc=0x8074cd9c (__osRestoreInt+0xc)`, **`intr=0`, `mask=0`** — the guest's MI mask is fully disabled at the stall, all event queues empty (SP/DP/VI valid=0). The machine is completely interrupt-silent: the guest waits for a DMA completion whose PI interrupt is masked/lost.
- Confirmed earlier: the guest's `__osException` EC==0 dispatch for IP2/IP3 uses class+handler tables at 0x80789680/0x807896A0, ALL-ZERO in every dump, executing `JR 0` → the RAM preamble at 0x0 → re-enter `__osException` (saved EPC=0, saved cause=0 in the kernel's context area prove the re-entry); `__osHwIntTable` (0x80771DE0) has [1]=LEO only, no RCP/SP handler installed; no writer of 0x80789680 exists in RAM; the cart image at the table's true offset (image base 0x80267000, offset 0x522680) contains noise, not a table — so the tables never got their init data (kernel .data), and no SP/RCP handler is ever installed.
- The pn64-style MI_INTR_SP keep-assert + locked-task pump (round-1 fix, still in tree) demonstrably changes the RSP task lifecycle (ucode runs deeper: pc=0xfc4/0x0a68) but does NOT fix the guest loop.

**Where the remaining problem lives (priority order for round 3):**
1. **MI mask=0 + lost PI-int**: the guest's MI_INTR_MASK is fully off; the PI/CART completion interrupt delivery (raise vs masked/lost-raise semantics; the UPDATE-11 "lost interrupt" class). Check WHY the guest's kernel has mask=0 at the stall, and whether the PI completion raise is deliverable. Test: re-enable/relay the PI-completion interrupt correctly (DD-gated).
2. The guest dispatch tables / SP handler install (kernel .data origin) — likely a red herring IF the real path is the inline MI dispatch (0x46ab4+, a0=event codes 32/40/48/56/64/72) — verify which path executes via the dispatch probe (emumode=1 + probe fires at pc window 0x807469f0-0x80746a34).
3. If the kernel genuinely can't dispatch, service the guest completion via the event/poll path.

**Working safe-diag kit (all DD-gated, no hot-path I/O):**
- `interrupt.c`: 512×2 s CPU ring + one-shot dispatch probe (g_dd_disp[16], cpu_pc window 0x807469f0-0x80746a34, reads class/handler via memory map) → flushed by dumper thread → files/wd_cpu.txt (RING + DISPATCH lines).
- `vi_controller.c` dumper thread → files/wd_state.bin (RDRAM + header + **DD trace ring** via dd_trace_dump() — k=1/2 DMA reads, k=4 ints, k=5/7 regs, k=6 PI completions) + files/wd_cpu.txt.
- `parallel.cpp`: one-shot wd_ucode_b8.bin audio-task capture.
- Trigger: `adb shell run-as <pkg> touch files/dd_snap_go`.

## UPDATE 2026-09-07 (goal round 1) — fix attempt + deeper root cause

**Mechanism (now fully documented):** the freeze = a guest-side **null-interrupt exception loop**:
the RSP audio task completes normally (its ucode runs the stack loop and BREAKs at 0xb4; the
task header at DMEM 0xfc0 is VALID: type=2, ucode=0x80768e60, stack=0x794e90/0x2df). mupen
raises the SP interrupt, the guest takes the exception at 0x80000180 → `__osException`
(0x80746800) → in the EC==0 dispatch (disassembled from live RDRAM: `AND s0,STATUS&CAUSE;
SRL/SRL/ADDI 16 → LBU [0x80789680+idx] class; LW [0x807896a0+class] handler; JR handler`)
→ **both tables are ALL-ZERO in every dump (working and stalled, all emumodes)** → `JR 0` →
the RAM vector at 0x0 (`lui k0; addiu; jr 0x80746800`) → re-enter `__osException` → infinite.
Guest `__osHwIntTable` (0x80771DE0) IS populated (index 1 = LEO 0x80759950), but no SP/RCP
handler is ever installed (`__osSetHWIntrRoutine` only called for the LEO). So the first real
RCP interrupt (the 60 Hz audio completion) sends the guest into the loop; earlier boot works
because it polls (no CP0 interrupts). CI (emumode=1) freezes identically — not recompiler-
specific. Ring sampler (safe, memory-only, flushed by dumper thread): 201/205 samples at
pc=0x80000180, intr=0x0001, ca=0x10000400, st=0xff03/ff00 toggling (EXL toggles = re-entry).

**Fix attempted (DD-gated, in tree — rsp_core.c):** ported the parallel-n64 reference model:
(a) do_SP_Task no longer clears MI_INTR_SP after scheduling SP_INT when idisk != NULL (the
guest's handler must find the source; the guest clears it via SP_STATUS write 0x8, which
update_sp_status already routes); (b) rsp_interrupt_event re-dispatches locked tasks
(self-sustaining pump). **Result: the RSP lifecycle changed (ucode now runs to pc=0xfc4/0x0a68
instead of parking at 0xb8) but the guest loop persists** — the guest dispatch wall is deeper
(the zero tables / no installed SP handler).

**Diagnostics now available (all safe, DD-gated, no hot-path I/O):**
- `interrupt.c` cpu ring (512×2 s records) flushed by the dumper thread → files/wd_cpu.txt
  (RING lines: pc/st/ca/intr/mask/sp); triggered with `run-as <pkg> touch files/dd_snap_go`.
- vi_controller.c dumper thread → files/wd_state.bin (RDRAM + header, freeze_state.py-ready).
- parallel.cpp one-shot capture of the pc=0xb8 audio task → files/wd_ucode_b8.bin.

**Next step (priority order):** (1) why the guest's IP2 dispatch tables are zero — check the
image load: does the .z64/.ndd hold init data for 0x80789680 (offset 0x789680 read RANDOM, so
likely the kernel data is loaded elsewhere/later); (2) try the HLE RSP (`mupen64plus-rsp-cxd4`)
for the audio path (fast experiment); (3) compare ares `n64/{cpu,rsp,mi}` interrupt model for
the correct guest contract; (4) if the kernel truly never installs the SP handler, the
emulator must deliver the SP completion through the guest's polling/event path instead of the
CP0 interrupt.

## UPDATE 2026-09-07 — the current stall precisely characterized (see doc notes below)

**State now visible on-device (emumode=2, parallel RDP+RSP, DD ROM+disk loaded):**
the game reaches the **DD LOADING progress screen** (64DD logo, bar stalls at ~6.5/8 segments,
falcon frozen) — NOT a black screen. VI fires at 60 Hz for a while, then the emulation thread
**hangs host-side** (103% CPU, all traces stop, screen frozen).

**Root-cause evidence (new diagnostics, all DD-gated):**
- `wd_cpu.txt` SAMPLE lines (my 500 ms CPU sampler, see note on hot-path I/O below): the CPU
  sits at **pc=0x80000180 (exception vector) 91% of samples**, `status=0x0000ff03`
  (IE+EXL, IM=0xff), `cause=0x10000400` (IP2 = RCP pending), **`intr=0x0001` (MI_INTR_SP set,
  NEVER cleared)**, `mask=0x003f`.
- Guest-thread snapshot (decoded by `.fzxwork/freeze_state.py` from `files/wd_state.bin`, format
  restored by the new dumper thread): at the stall the **main thread (id=3) is RUNNING and was
  interrupted inside `osRecvMesg` (ra=0x80746120, a1=0x04001000=SP IMEM, a2=`__osSetHWIntrRoutine`+0x70)**;
  game thread id=5 READY at `0x8071eca0` (`func_8008D97C`+0x188); audio id=4 READY at
  `osStartThread`+0x134; LEO id=6 blocked on its cmd queue; **all event queues EMPTY
  (SP/DP/VI/CART valid=0)**; run queue = audio, game, idle.
- Interpretation: the guest takes the **RSP interrupt** (audio task completion) and then the CPU
  **never gets through `__osException`** — it is stuck re-entering the exception vector because
  `MI_INTR_SP` stays pending. The parallel-RSP leaves the audio task in its yield state
  (HALT+INTR_BREAK+cp0.irq), and mupen's `rsp_core.c` re-raises `MI_INTR_SP` on every task
  re-entry → infinite exception loop; the guest's SP handler can never run/clear it.
- The parallel-RSP `wd_rsp.txt` shows the audio task (ttype=2) **breaking immediately at
  pc=0x00b8** every ~15 ms, 70k+ task entries, `EXIT status=0x243 irq=1`.
- This is the same class the handoff already suspected ("missing DP/AI/SP completion signal
  delivery") — now pinned to SP-int / RSP yield semantics.

**Correction to prior conclusions:**
- `wd_freeze.txt` is the **RSP heartbeat** (writer = `mupen64plus-rsp-parallel/upstream/parallel.cpp`
  `rsp_watchdog_tick`, host ms + RSP pc/busy/full/status/irq), NOT the CPU trace. The handoff's
  "CPU executing various blocks — not stuck in a poll loop" was reading the RSP, not the CPU.
  The CPU *is* stuck (at the exception vector).
- The recompiler early-boot "black screen at bootproc" (UPDATE 20) and the current vector-freeze
  are the same class: the Sep 6-7 commits (ForceSynchronize + ERET/mi rechecks) advanced the
  recompiler to the DD-LOADING screen, but the SP-int completion problem remains.

**Diagnostics lessons (IMPORTANT — violates shown by this session):**
- **No file I/O in the CPU/RSP hot path.** A 500 ms `fopen/fprintf/fclose` sampler inside
  `r4300_check_interrupt` and a per-frame `access()` check changed timing and made the boot
  freeze EARLIER (before VI). The previous session already stripped high-rate diag writes
  (0daeb28de) for the same reason. Correct design = a **dedicated dumper thread** that polls
  for a trigger file (`files/dd_snap_go`) and dumps RDRAM+state once; the hot path only does
  `pthread_create` once (proven zero-impact).
- Write-state dumps in the old `iplram_wd` format: RDRAM first (dram bytes), then
  `WD pc=...` header + `--- DD regs ---` block; `freeze_state.py` decodes threads/events.
  New snapshot capture: `adb shell run-as <pkg> touch files/dd_snap_go` → wait → pull
  `files/wd_state.bin`.

**Uncommitted tree:** `interrupt.c` = session-start state (cpu_stall only; my sampler removed);
`vi_controller.c` = prior VI FIRE diag + safe dumper thread (new); `r4300_core.c` = prior SPSCAN.

**Next step (right track, per ares comparison in doc UPDATE 19):** fix the RSP-completion SP
interrupt semantics between `rsp_core.c` (do_SP_Task / SP_STATUS handling) and the
parallel-RSP plugin — the guest never gets a *clearable* task-complete SP event; ares delivers
it via CPU/RSP concurrency (`forceSynchronize`, `io.cpp` SP_STATUS reads, `mi/*`). Compare
ares n64 `rsp/io.cpp` + `mi/mi_controller` + `rsp/dma.cpp` against mupen
`rsp_core.c:334-437` and the parallel plugin's yield path (`parallel.cpp` budget-yield →
HALT+INTR_BREAK+irq) to make the audio task's completion raise exactly one clearable
`MI_INTR_SP` per task. All changes stay DD-gated (`g_dev.dd.idisk != NULL`).

## Project & Goal
- **Repo**: `/home/garyb/LLM-Projects/mupen64plus-ae-turnip` (Android N64 emulator, Gradle). Branch `dd-eos-watchpoint`.
- **Goal** (`goal-0400d98c`, rev 2, max 12 rounds, currently **paused/disarmed**): Make **F-Zero X Expansion Kit on 64DD** fully boot and run in the **RECOMPILER** (`emumode=2`, parallel-RDP Vulkan) with **clean audio** and **no plain-game regression** (CI `emumode=1` baseline must keep working).
- **Mandatory user rule**: ALL DD-specific changes **gated on `g_dev.dd.idisk != NULL`** (3 prior plain-game SIGSEGVs from ungated changes).
- **EMUMODE set ONLY by** app profile `files/Profiles/emulation.cfg` (`r4300Emulator=1|2`, currently `2`). Per-game `CoreConfig/mupen64plus.cfg` does NOT override it.

## The Breakthrough (this session)
The FZX EK black screen was **NOT a recompiler bug** — the **64DD IPL ROM (`dd_rom.n64`) was never copied into `cache/WorkingPath/`**.

**Root-cause chain (verified in code):**
- `CoreService.java:608-622` — cart+disk games call `mCoreInterface.setDdRomPath(ctx, mGamePrefs.idlPath64Dd)`.
- `CoreInterface.java:457-486` — `setDdRomPath` returns early if `ddRomUri` empty; else copies URI → `mWorkingPath + "/dd_rom.n64"`.
- `GamePrefs.java:352` — prefs filename = `romMd5.replace(' ', '_') + "_preferences"`.
- **KEY DISCOVERY**: `romMd5` is the **bare 32-char MD5**, not the "name (country) md5" string. Confirmed by existing device prefs files (`58D200D4…_preferences.xml`, `4C407DF2…_preferences.xml`).
- FZX EK had **no prefs file** → `idlPath64Dd` empty → `setDdRomPath` early-returned → DD ROM never copied → black screen.
- Correct FZX EK prefs filename = **`793F68B0A8F405D471E4D4F1BAAAC549_preferences.xml`** (bare MD5). First attempt wrongly used `Y01NM_(U)_793F68…` (full-string) → still not copied.

## Actions taken
1. Built FZX EK prefs XML from the working FZX J prefs: `idlPath64dd` (URI → `N64DD IPLROM [Japan].n64`), `diskPath64dd` (URI → `F-Zero X.ndd`), `support64dd=true`, `emulationProfile=Parallel`.
2. Wrote it into `shared_prefs/` under the **correct bare-MD5 name** (parenthesis-in-path shell issues worked around by pushing a script to `/data/local/tmp/` and running via `run-as … /system/bin/sh <script>`).
3. Removed the wrongly-named file.
4. Relaunched FZX EK → **DD ROM + DD disk now copied**: `dd_rom.n64` (4,194,304 B) + `Y01NM` (64,931,840 B NDD) in `cache/WorkingPath/`. Logcat: `Copying DD ROM: …N64DD IPLROM [Japan].n64`, `Copying DD Disk: …/cache/WorkingPath/Y01NM`.

## Current state (after DD ROM loaded)
- App **alive** (no crash).
- Core resumes; uses **parallel RDP (Vulkan/Granite/Turnip)** + **android audio (Oboe/AAudio 48 kHz stereo)**.
- **VI diag** (`wd_vi.txt`, active ~64k lines): VI fires **16,089×**, calls `gfx.updateScreen()` + `raise_rcp_interrupt(MI_INTR_VI)` each fire. VI regs constant: `VI_V_SYNC=525`, `VI_V_INTR=2`, `VI_STATUS=0x3102`, `count_per_scanline=1542`, `delay=811092`. VI hardware working.
- **CPU trace** (`wd_freeze.txt`, ~118k lines): CPU **executing various blocks** (pc changes), `status=0x40` (**IE=1**), `irq=0` — **not** stuck in a single poll loop.
- **CPU stall diag** (`wd_cpu.txt`): **EMPTY** — CPU not stuck on one PC ≥2000 `r4300_check_interrupt` calls.
- **But screen still black** — no visible menu/title.

## Critical re-assessment
The **prior hypothesis** (boot kernel spins in a `MI_INTR_MASK` poll loop at `pc 0x800bcd9c` waiting for a VI/PI RCP interrupt that `r4300_check_interrupt` never queues as CHECK_INT when IE=0, vs ares/Phobos setting the CAUSE pending bit unconditionally) **does NOT hold now** — that was pre-DD-ROM. Now the DD ROM is loaded, CPU is running (IE=1), VI fires and raises interrupts. So the **black screen is a different fault**; next step is to determine what the CPU is actually doing (executing but not drawing a visible frame) and whether the RSP/audio path is healthy.

## Diagnostic files on device (`files/`)
- `wd_vi.txt` — VI diag (active). Source: `mupen64plus-core/upstream/src/device/rcp/vi/vi_controller.c` (DD-gated).
- `wd_freeze.txt` — CPU trace (active), format `ms=… pc=… busy=0 full=0 status=… irq=…` + `EXIT ok pc=… dur=…`. **Source not located** in obvious `.c` files (see open Q5).
- `wd_rsp.txt`, `wd_lba.txt`, `wd_dd_raise.txt`, `wd_sp_break.bin`, `wd_ucode*.bin` — RSP/DD/SP-MMIO dumps (`wd_dd_raise.txt` source = `dd_controller.c`).
- `wd_cpu.txt` — CPU stall + SP-MMIO scan (currently empty). Sources: `interrupt.c` (`cpu_stall`), `r4300_core.c` (SPSCAN).

## Working commands
- **Build**: `cd /home/garyb/LLM-Projects/mupen64plus-ae-turnip && export GRADLE_USER_HOME=$PWD/.gradle_home && $PWD/.gradle_home/gradle-8.4/bin/gradle :app:assembleDebug`
- **Install**: `adb -s 49016109 install -r app/build/outputs/apk/debug/Mupen64PlusAE-debug.apk`
- **Stop+run FZX EK**:
  `adb -s 49016109 shell am force-stop org.mupen64plusae.turnip.pwnedbygary.debug`
  then
  `adb -s 49016109 shell "am start -a android.intent.action.VIEW -d 'content://com.android.externalstorage.documents/tree/EBFF-F6C0%3AROMs%2Fn64/document/EBFF-F6C0%3AROMs%2Fn64%2FF-Zero%20X%20Expansion%20Kit%20%5BEnglish%20v2.1%20by%20LuigiBlood%5D%20%5BCart%20Hack%5D.zip' org.mupen64plusae.turnip.pwnedbygary.debug"`
- **Pull trace**: `adb -s 49016109 shell run-as org.mupen64plusae.turnip.pwnedbygary.debug cat files/<trace>` (or pushed script for parenthesis paths).
- **Set emumode**: `adb -s 49016109 shell run-as org.mupen64plusae.turnip.pwnedbygary.debug sed -i 's/r4300Emulator=1/r4300Emulator=2/' files/Profiles/emulation.cfg`
- **Device**: `adb -s 49016109`; package `org.mupen64plusae.turnip.pwnedbygary.debug`.

## Game IDs
- **FZX EK** = GameData dir `Y01NM (U) 793F68B0A8F405D471E4D4F1BAAAC549`; **prefs = bare-MD5 `793F68B0A8F405D471E4D4F1BAAAC549_preferences.xml`** (now created).
- **FZX J** = `58D200D43620007314304F4E6C9E6528` (HAS prefs, working template — source of FZX EK prefs content).
- **FZX J2** = `4C407DF2282E771E9C3CFA6FF680D0BB` (prefs present, `support64dd=false`).

## Open questions / next steps
1. **What causes the black screen now that the DD ROM is loaded?** CPU executing + VI firing, but no visible frame. Map the `wd_freeze.txt` pc values to game code; determine if the game is (a) stuck in a late boot phase, (b) waiting on RSP/DD, or (c) drawing a black framebuffer.
2. **Is the RSP/audio path healthy?** Goal requires clean audio — inspect `wd_rsp.txt`, `wd_sp_break.bin`, `wd_ucode*.bin`.
3. **Verify CI `emumode=1` baseline still works** (plain games, OoT) — no plain-game regression.
4. **Re-check `cached_interp_MTC0`** (take-time IE recheck) — still needed?
5. **Find the source writing `wd_freeze.txt`/`wd_rsp.txt`** (`EXIT ok`/`busy=`/`full=`/`irq=` format) — not located in obvious `.c` files; confirm which build is installed and whether these are current-build or leftover diagnostics.

## Key code locations (if the interrupt-delivery question is revisited)
- `mupen64plus-core/upstream/src/device/r4300/interrupt.c` — `r4300_check_interrupt` (deliverability gate) + `cpu_stall` diag (lines 51–91, 386).
- `mupen64plus-core/upstream/src/device/rcp/vi/vi_controller.c` — `set_vi_vertical_interrupt` (83), `vi_vertical_interrupt_event` (217: `gfx.updateScreen()` + `raise_rcp_interrupt(MI_INTR_VI)`).
- `mupen64plus-core/upstream/src/device/rcp/rsp/rsp_core.c` — RSP dispatch (stall note ~line 402).
- `mupen64plus-core/upstream/src/device/dd/dd_controller.c` — DD raise diag + freeze ring.

## Gotchas
- Parentheses in game-ID paths break `adb shell run-as … sh -c '…'` — use a pushed script.
- `sleep > 55s` hits the tool timeout — use `sleep ≤ 55` (or `timeoutMs`).
- `/tmp` files may be cleared between `bash` calls — pull+analyze in one command.
- Uncommitted DD-gated diagnostics (interrupt.c `cpu_stall`, r4300_core.c SPSCAN, vi_controller.c VI diag, dd_controller.c) remain in the tree, all gated on `g_dev.dd.idisk != NULL`.

## UPDATE 2026-09-07 (round 13) — MAJOR REFRAME: the "Cart Hack" is a PURE CART game, NOT a 64DD game

**Decisive evidence (verified by disassembling the LIVE RDRAM in `wd_state.bin`, not the decomp):**
- Cart ID at 0x3B = `CFZE` (custom "Cart Hack" ID), NOT a 64DD ID (`NDDJ`/`NDDE`/`NDXJ`).
- `LeoDriveExist` (0x8075AA60) reads the **CART** (`lw $t3,0x1010($t9)` with `lui $t9,0xb000` → 0xB0001010 = KSEG1 of 0x10001010 cart) and checks **0x6C788490** (the Cart Hack's cart signature at offset 0x1010), NOT the DD ROM (0xA6001010 / 0x2129FFF8).
- `leomain` (0x80757000) has **NO `osEPiReadIo(LEO_STATUS)` call** — it goes straight from `osLeoDiskInit` + DMA-param setup to `osRecvMesg(LEOcommand_que)`. The decomp's line-40 DD-status read was **patched out** by the hack.
- DD trace ring has **only k=6 (PI cart-DMA completions, addresses 0x10xxxxxx)**; **zero k=1/2 (DD disk DMA)** and zero k=5/7 (DD regs). The game reads all its data from the CART.
- `read_dd_rom` and `read_dd_regs` are **NEVER called** in the whole boot.

**Conclusion:** the "Cart Hack" (66.5MB, self-contained ~64MB cart) runs WITHOUT the 64DD. The earlier "R 0x508 + W 0x510" was the **spurious premature mecha handler** (`__osLeoInterrupt`), not the LEO flow. Rounds 9-12's "DD handshake stall" was a red herring built on that misread. The DD disk (Y01NM) + dd_rom.n64 are loaded but **unused**.

**Actual stall:** the game's main/loading thread stalls in a cart-loading freeze. Main thread (prio 0, the post-main idle) sits in an infinite `b .` loop at 0x806f32ec (normal idle after `main()`); the boot reached LEO init (leomain blocked on `osRecvMesg`, leointerrupt STOPPED) but the cart data load chain freezes after ~966 PI blocks (~1MB). This is the SP/RSP/recompiler class the handoff's rounds 1-8 pinned (MI_INTR_SP / task-completion), NOT DD.

**Fixed this round (keep):** removed the premature power-on MECHA event in `poweron_dd` (ares/Phobos `DD::power` fires NO mecha at power-on; mecha only from ASIC-command responses), added `DD_STATUS_DISK_CHNG` (ares-accurate). Added `read_dd_rom` + `read_dd_regs`/`write_dd_regs` PC-annotated access logs (`wd_dd_rom.txt`, `wd_dd_access.txt`).

**Next step:** pivot from DD to the real cart-load stall — the SP/RSP task-completion / recompiler issue from rounds 1-8, or determine exactly which thread freezes the ~1MB cart load.

## UPDATE 2026-09-07 (round 13b) — real 64DD combo: DD handshake WORKS, game reads ~497 sectors then stalls at seek→BM

**Correct rom identified:** the base cart is `F-Zero X (Japan).z64` (MD5 `58d200d4…` = FZX J prefs, which already have `support64dd=true` + `diskPath64dd`→F-Zero X.ndd + `idlPath64dd`→N64DD IPLROM [Japan].n64). Launch it with the VIEW **tree/document** URI (parens `%28`/`%29`), app foregrounded first:
`adb shell am start -a VIEW -d 'content://com.android.externalstorage.documents/tree/EBFF-F6C0%3AROMs%2Fn64/document/EBFF-F6C0%3AROMs%2Fn64%2FF-Zero%20X%20%28Japan%29.z64' org.mupen64plusae.turnip.pwnedbygary.debug`

**DD handshake now fully works** (the premature-mecha removal in `poweron_dd` was correct):
- country check reads DD ROM `0x0609ff00` = `0xc3dbfe61` (top byte 0xC3 = JPN, passes CJ check)
- `W 0x508=0x00090000` (ASIC_CLR_RSTFLG) → mecha ack (`W 0x510=0x01000000` @ pc 0x80000180)
- `R 0x540` (ASIC ID) → `W 0x508=0x001b0000` (ASIC_READ_PROGRAM_VERSION/INQUIRY)
- `W 0x508=0x00010001` (ASIC_RD_SEEK) → BM register setup (`SEQ_CTL`/`SEC_BYTE`/`BM_CTL` 0x50/0x40/0xc05a0000 START)

**Then it DMA-reads ~497 sectors (~100KB) of EK data** (k=2 `dd_dom_dma_write`, DS buffer 0x05000400 → RDRAM, +0xd0 each; ends with a C2S read 0x05000000 len 0x340 + PI completion k=6). VI keeps firing (rendering), main/idx thread idles at `b .` 0x800679f8 (normal post-`main()`).

**Remaining stall:** after the read, the game seeks to track 0x151 (`W 0x500=0x01510000` + ASIC_RD_SEEK) then the CART handler (`pc=0x80000180`) polls `BM_STATUS` (0x05000510) thousands of times and STOPS. DD snapshot: `STATUS=0x01180000` (DISK_PRES|MTR_N_SPIN|HEAD_RTRCT = drive SLEEP), `BM_STATUS_CTL=0` (BM not running). So the post-seek BM transfer never starts and the drive goes to sleep. **Next: fix the seek→BM transition / drive-sleep so the BM read resumes** (compare ares `dd/controller.cpp` command()+mechaResponse() and `drive.cpp` motor/BM against mupen `write_dd_regs` case 0x01 + `dd_bm_int_handler`/`dd_update_bm` + `dd_dv_int_handler`).

## UPDATE 2026-09-07 (round 13c) — mecha fires for EVERY seek; stall is post-seek, not mecha

Added `wd_dd_mecha.txt` (SCHED/FIRE of `DD_MC_INT`): the mecha response FIRES for all 24 seeks (cmd=01, cycles 253075≈5.4ms after the motor is up; 2453275 for the spin-up seek). So the handshake/mecha path is fine. The game stalls AFTER the 24th seek (track 0x151) + its FIRE: DD regs `CUR_TK=0x61510000` (track 0x151 locked), `CUR_SECTOR=0xb2` (block1/sector88), `BM_STATUS_CTL=0` (not running), `STATUS=0x01180000` (drive SLEEP). The drive-sleep is a SYMPTOM (dd_dv_int_handler idles to sleep while BM isn't running), not the cause — the game's LEO read thread blocks (~>4s) so the motor idles out. Next: find what the read thread blocks on (mecha EVENT on LEOevent_que vs the post-seek BM_CTL START) — needs the base-FZX-J symbol addrs for `LEOevent_que`/`LEOcur_command` (freeze_state.py's Cart-Hack addrs don't apply to this rom).

## UPDATE 2026-09-07 (round 13d) — DD data-load COMPLETES; remaining stall is a guest message-queue deadlock (SP/RSP class)

Decoded the base-FZX-J thread/queue state (its own addresses; freeze_state.py's Cart-Hack addrs are wrong for this rom). At the stall the 497-sector DD load is DONE (last BM transfer ends C2S read 0x05000000 len 0x340 + PI completion; mecha fires for all 23 seeks). The guest's threads are ALL blocked on EMPTY mesg queues:
- leomain prio149 WAITING on LEOcommand_que 0x8042dca8
- game thread prio150 WAITING on 64-slot game queue 0x800dca40
- game thread prio100 WAITING on 0x800dcae8
- leointerrupt prio150 STOPPED; main prio0 idle `b .` 0x800679f8
- RSP HALTED (SP_STATUS=1), CAUSE=0, MI_INTR=0000

=> The DD is NOT the blocker anymore. The stall is the game's post-load processing: the main loop waits on the game queue for an event that never gets posted (rounds 1-8 SP/RSP event-delivery class). Base-FZX-J addresses: LEOcommand_que=0x8042dca8, LEOevent_que=0x8042dcc0, LEOcontrol_que=0x8042dcd8, LEOdma_que=0x8042dcf0, __osThreadTail=0x800d1d80, __osException=0x800bc4c0. NEXT: trace which event the prio150 game thread is waiting for (VI vs SP/RSP vs PI) and fix that delivery.

## UPDATE 2026-09-07 (round 13e) — deadlock is a game-internal queue, NOT an OS event

__osEventStateTab @0x800faed0 (base FZX J): CART→0x8042dcc0, SP→0x800dcad0, DP→0x800dcad0, SI→0x800dca70, VI/COUNTER→0x800fc290, PI→0x800fae90. The prio-150 game thread blocks on queue 0x800dca40 (64 slots, empty) which is NOT in the event table → it's a GAME-INTERNAL message queue. The primary game loop waits for a message no producer sends (post-load audio/RSP/decompression path). Next: find the producer thread of 0x800dca40 (likely the audio/RSP task thread) and why it never posts — this is the rounds 1-8 RSP/SP class, now as a queue deadlock rather than a CAUSE spin.

## UPDATE 2026-09-07 (round 13f) — KEY: SP/DP event queue FULL (16 msgs, unconsumed); game post-load queues empty

Decoded ALL guest queues at the stall:
- SP/DP queue 0x800dcad0: valid=16 msgcount=16 (FULL!) mtqueue=sentinel — RSP completions are POSTED but NO thread consumes them.
- SI queue 0x800dca70: valid=1 msgcount=1. LEOpost_que 0x8042dd08: valid=1.
- game queue 0x800dca40 (prio150 thread): valid=0 (empty). prio100 queue 0x800dcae8: valid=0. VI/COUNTER 0x800fc290: valid=0. PI 0x800fae90: valid=0.
- prio99 thread 0x800dc1d0: STOPPED (state=1). RSP HALTED (SP_STATUS=0x0001 = HALT only, no INTR_BREAK/BROKE/TASKDONE).

=> The RSP/audio completion chain works up to the SP event POST but the guest's SP consumer never RUNS (prio99 STOPPED), so SP events pile up (16) and the game's main loop waits on the empty game queue. This is the guest's post-load audio/RSP deadlock — the rounds 1-8 class. The mupen side delivers the SP interrupt (keep-assert + SP_INT) and the guest posts SP events fine; the break is the guest thread never consuming them. Next: compare mupen's RSP task end (parallel.cpp task type + SP_STATUS/HALT/INTR_BREAK semantics) against ares (RSP::instruction → status.halted → interpreter-ipu BREAK → if interruptOnBreak mi.raise(MI::IRQ::SP)) and the parallel-n64 reference — the RSP task completion must leave INTR_BREAK|HALT so the guest's SP handler consumes it and re-dispatches the next task.

## UPDATE 2026-09-08 (afternoon session) — DD/plain-game isolation, Cart Hack fix, menu-FPS diagnosis

### What was done
1. **Root cause of the plain-game regression:** the 7 committed DD commits (836d05da4→aeb31cea1) left the ares RSP work (cp0.cpp yield, parallel.cpp budget/clean-yield, rsp_jit.cpp intra-loop budget insertion, rsp_core.c ares completion blocks) UN-gated. Gated everything behind a RUNTIME `IsDDPresent()` query (new RSP_INFO field, set in plugin.c to `rsp_is_dd_present()` = `g_dev.dd.idisk != NULL`). Key: `plugin_start_rsp` runs BEFORE `init_device` (CoreAttachPlugin happens before emuStart→main_run→init_device), so a static `(g_dev.dd.idisk != NULL)` wiring check at plugin-start ALWAYS sees NULL — the gate must be evaluated at task time (hence the callback). Also `rsp_force_synchronize` itself no-ops without a disk (belt-and-suspenders).
2. **IsDDPresent wiring:** `m64p_plugin.h` (both core api/ + rsp-parallel api/ + rsp_1.1.h) gained `int (*IsDDPresent)(void);` after `ForceSynchronize`. plugin.c sets both callbacks. parallel.cpp `dd_mode` + `rsp_ares_budget_enabled()` and cp0.cpp ares path all key off `RSP::rsp.IsDDPresent && RSP::rsp.IsDDPresent()`.
3. **A/B proof (no plain-game regression):** built TRUE baseline (dc955483a core+rsp) and compared F-Zero X (USA) — the Select Course "OK?" screen runs ~25 FPS on baseline TOO; Select Machine ~41 FPS on both. The slow menus are a VIDEO-PROFILE artifact (Parallel (2x): Upscaling=2 + VIAA + VIBilerp + GammaDither + SuperscaledDither + Divot + VIDither), NOT an emulator regression. Race runs 60 FPS on both. **The user confirmed: 2x profile = 30 FPS on course select, plain "Parallel" (1x, only r4300Emulator=2+videoPlugin+rspSetting) = 60 FPS.** → todo added to optimize parallel renderer upscaled performance.
4. **Cart Hack "regression" — actually config:** the Cart Hack (MD5 793F68B0, header Y01NM, Disk baked into ROM) had `support64dd=true` + `diskPath64dd` in per-rom prefs → core loads the .ndd → `dd.idisk != NULL` → IsDDPresent()=1 → ares RSP path wrongly engages for a pure cart game → staticy audio + stuck loading bar. FIX: set `support64dd=false` for 793F68B0 (verified: boots, saves work, 60 FPS). The disk data is baked in, so the external .ndd/IPL are never needed.
5. **DD game (F-Zero X Japan base, MD5 58D200D4):** still black screen after load, but the ares RSP path IS active (wd_rsp.txt grew: ms=8343002 seq=1181+, EXIT pc=0098/089c/01e3, status=0x61). The DD handshake + 497-sector load complete; the remaining blocker is the guest post-load deadlock (SP/DP event queue 0x800dcad0 FULL, prio-99 SP consumer STOPPED — rounds 13e/13f class).

### Files changed (uncommitted, working tree)
- `mupen64plus-core/upstream/src/api/m64p_plugin.h` — IsDDPresent field
- `mupen64plus-core/upstream/src/plugin/plugin.c` — rsp_is_dd_present(), both callbacks wired, rsp_force_synchronize idisk no-op
- `mupen64plus-core/upstream/src/device/rcp/rsp/rsp_core.c` — ares completion blocks wrapped in `if (g_dev.dd.idisk != NULL)`
- `mupen64plus-rsp-parallel/upstream/api/m64p_plugin.h` + `upstream/rsp_1.1.h` — IsDDPresent field
- `mupen64plus-rsp-parallel/upstream/parallel.cpp` — dd_mode + rsp_ares_budget_enabled() via IsDDPresent()
- `mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp` — ares path keyed off IsDDPresent()
- `mupen64plus-rsp-parallel/upstream/rsp_jit.cpp` — budget emission + rsp_enter + run() gates via rsp_ares_budget_enabled()
- `mupen64plus-rsp-parallel/upstream/parallel.cpp` — InitiateRSP diag logs IsDDPresent/ForceSynchronize pointers (DebugMessage M64MSG_ERROR)

### Device config state (49016109)
- Profiles: only [Parallel (2x)] custom (r4300Emulator=2, videoPlugin=parallel, rspSetting=rsp-parallel); builtin [Parallel] = minimal (no upscaling, no VIAA).
- Per-rom profile map: FZX USA (753437D0)=Global Default→Parallel (2x); FZX J Japan (58D200D4)=Parallel; Cart Hack (793F68B0)=Parallel + support64dd=false (FIXED).
- Build: `export GRADLE_USER_HOME=$PWD/.gradle_home && $PWD/.gradle_home/gradle-8.4/bin/gradle :app:assembleDebug --offline`; AGP 8.2.2; do NOT let Android Studio sync AGP 8.13/9.3.
- ddiag trace grows only when dd_mode=1 (ares path): `files/wd_rsp.txt`.

## UPDATE 2026-09-10 (goal round 1) — ROOT LOCALIZED: odd $sp inside the 64DD boot hand-off; stores are being LOST

**Committed this round:** `9b3fcfabc` (DD-gated watchdog + first-fault dump + NEW first-odd-SP dump).

### New tool: `iplram_spodd.bin` (SPODD tag)
`wd_pc_record()` (get_addr_ht) now dumps the block ring the *first* time `$sp & 3 != 0`.
An odd `$sp` is never legal MIPS and it is the **first** corruption in the EK boot
failure, so this pins the failure without guessing.

### What the probe proved (device 49016109, F-Zero X (Japan).z64 + F-Zero X.ndd, emumode=2)
```
WDSPODD vaddr=80000180 pc=80000180 ra=800bb6a4 sp=800d4203
        a2=800bb9a0 a3=800bb67c v1=000000bd t9=000000bd t0=800bba00 t1=800bb9e0 t3=00004000
WDCP0   cause=9000002c status=0000ff03 epc=800ac01c
WDPCS   ... 800fc44c 800d8730 800fc44c   <- last good block entry (sp even)
        80000180 800d4203 800bb6a4       <- first odd $sp (exception vector)
```
1. `0x800bb6a4` is the **delay-slot return address of the `jal 0x800bcf10` (bzero)** inside
   `__LeoBootGame2` (0x800bb67c). `a2=0x800bb9a0`/`a3=0x800bb67c`/`v1=t9=0xbd` (descramble key)
   are the leftovers of `LeoBootGame`'s descramble loops. => **the CPU really is executing the
   descrambled `__LeoBootGame2`, immediately after `bzero(LeoBootGame, 0x13C)`.**
2. RDRAM at `0x800bb67c` matches the FZX decomp **byte-for-byte** (`addiu sp,sp,-320`,
   `jal 0x800bcf10`, `addiu a1,zero,316`, `sb t8,16(t9)` ...), so the descrambling is *correct*
   and the executed code is the correct code.
3. `sp = 0x800d4203 = 0x800d4200 ^ 3` — `0x800d4200` is exactly `0x800d4340 - 320` (the intended
   frame). **3 is the byte-lane XOR constant** used by mupen's byte-access codegen
   (`new_dynarec.c:2101 bshift()`, and the `(const+offset)^3` forms at 6235/6243/6275/6282/6424).
4. **The frame stores are missing**: `sw s0,24(sp)`, `sw ra,28(sp)`, `sw a0,40(sp)`,
   `sw zero,308(sp)` should have written 0x800d421b/0x800d421f/0x800d422b/0x800d4337, but
   0x800d4218 still holds `0x80079b08` and 0x800d421c still holds `0x80079bb0` (pristine).
5. **`bzero`'s stores are missing too**: the whole range 0x800bb540..0x800bb67c is *fully intact*
   (79 non-zero words) even though `bzero` returned. No 0x13C zero-run exists anywhere near it.
6. The ring's last good entry is `0x800fc44c` — and `0x800fc460` is *another* `jal 0x800bcf10`
   (bzero) call site in the game/LEO code — i.e. the same bzero is used on a data buffer there.

### Consequence / next step
Both "the odd `$sp`" and "the missing stores" are consistent with **one** defect: a byte/word
access whose address computation carries the `^3` lane transform (`addr^3` instead of `addr`,
so stores land 1-3 bytes off and the odd-address reads later produce `rotl8()`-style garbage —
exactly the `ra = 0x079b0880 = rotl8(0x80079b08)` seen in the earlier fault dump).
Priority for the next round:
  1. Audit the dynarec STORE path for stores to pages that already have compiled code
     (`invalid_code`/`memory_map` WRITE_PROTECT + `invalidate_addr_reg` stub, `new_dynarec.c`
     ~6448-6462) — a store on that path must still be *performed*, not only invalidated.
  2. Audit `new_dynarec.c:6424` (`x = (const^3) - const`) and the `emit_xorimm(addr,3,temp)`
     byte-store form: verify the delta is applied to the **address**, never to the data/guest reg.
  3. Re-check the arm64 `emit_movzbl/sbl_indexed_tlb` constant-address forms.
Then re-run the same DD combo: success = `iplram_spodd.bin` never appears.

## UPDATE 2026-09-10 (goal round 3) — fault chain decoded; ROUND 1's ENGINE CLAIM RETRACTED

**Committed this round:** `fa8aa67ed` (DD-gated interpreter-level `$sp` parity probe `wd_spodd2`).

### 1. CORRECTION: the "cached-interpreter control run" was NOT the interpreter
The round-2/3 "CI control" (setting `r4300Emulator=1` in `[Parallel (2x)]`) **did not take
effect**: `wd_pc_record` is called *only* from the dynarec's `get_addr_ht`, yet it still
recorded a full 2048-entry block ring — so the **dynarec ran**. Ring tails of the two runs are
byte-identical. `[Parallel (2x)]` is not the profile the DD combo uses (it resolves to the
builtin `[Parallel]`). Fix applied on-device: append a `[Parallel]` section with
`r4300Emulator=1` to `files/Profiles/emulation.cfg`.
=> **"identical in the interpreter" is NOT established.** Do not cite it.

### 2. The full fault chain (from `iplram_fault.bin` / `iplram_spodd.bin`)
```
WDFAULT vaddr=079b0880 w=2(EXEC) pc=800ad744 ra=079b0880 sp=800d4243
        a1=a2=t9=079bb080   WDCP0 cause=9000002c epc=800ac01c
```
* `pc=0x800ad744` disassembles to **`lw ra,20(sp)` / `addiu sp,sp,64` / `jr ra`** — a plain
  function epilogue.
* With `sp=0x800d4203` the load lands on **`0x800d4217`**, which holds `0x079b0880` = the
  **byte-rotation (rotl8) of the aligned word `0x80079b08` at `0x800d4218`** — verified by
  searching RDRAM for the byte sequence: `80 08 9b 07` occurs only at `0x800d4217`/`0x800d42b7`,
  i.e. *only* as misaligned views of aligned `0x80079b08` words.
* `addiu sp,sp,64` -> `0x800d4243`; `jr ra` -> **execute fault at `0x079b0880`**.
* `a1=a2=t9=0x079bb080` = rotl8 of `0x80079bb0`, i.e. **more misaligned frame loads**.
=> **`sp` being odd by exactly `^3` IS the whole failure.** Everything else follows from it.
   (`cause=9000002c`: ExcCode=11, **CE=1 -> COP1 unusable**, BD=1; the delay slot at
   `0x800ac020` is `46006006` = COP1 `add.s`, and `status=0xff03` has CU1 clear. That is a
   *downstream* symptom: the bad `jr ra` lands in FPU code while CU1 is still off.)

### 3. `^3` is mupen's `S8` byte-lane constant
`osal/preproc.h`: `S8 3` (little-endian host) / `0` (big-endian). The same constant appears in
the DD controller's sector-buffer swizzle (`dd_controller.c:350,356,399,411,426`) and in the
dynarec's byte-access codegen. So `sp = X ^ 3` means **something applied a byte-lane address
transform to a word-aligned address**.

### 4. The 64DD is EXONERATED as the source of the stack corruption
Parsed all 4096 `dd_trace` entries out of the freeze dump: **zero** DMAs target
`dram_addr` in `0x000d0000-0x000dffff`. Also verified the DD DMA swizzle is *arithmetically
correct*: `read_sector`'s `ds_buf[i^3] = disk_sec[i]` plus `dd_dom_dma_write`'s
`dram[(dram_addr+i)^S8] = mem[(cart_addr+i)^S8]` (S8=3) composes to exactly the disk's
big-endian words in mupen's native-LE RDRAM.

### 5. RDRAM content itself is HEALTHY
Fingerprinting RDRAM against the images shows RDRAM[v] == cart[v - 0x80000000 - 0x66000] with a
**constant 0x66000 for 7 different addresses** — that is exactly IPL3's code-segment relocation
(ROM 0x66000 -> RDRAM 0x80000000), so the boot, the descramble and the loaded image are all
correct. (Byte order matters: RDRAM stores guest words native-LE, the ROMs are big-endian —
compare only after swapping.)

### 6. Where the corruption happens (block-level boundary)
The ring's last entry is `800fc44c 800d8730 800fc44c` and the next *recorded* entry is the
exception vector with `sp=800d4203`. Blocks between them were reached by **constant** `jal`
targets (`0x800fc44c -> LeoBootGame 0x800bb540 -> __LeoBootGame2 0x800bb67c -> bzero
0x800bcf10`), which the dynarec emits as direct linked calls and which therefore never call
`get_addr_ht`. So the corrupting instruction is **inside that chain** and is invisible to the
existing ring. Note `sp` at `0x800fc44c` was `0x800d8730` while the fault ran at `0x800d4243` —
a stack switch, not a normal call depth change.

### Next step
`run_cached_interpreter` + `iplram_spodd` -> `files/wd_spodd2.txt` gives the **exact** instruction
that makes `$sp` odd (pc + opcode + a 96-entry (pc, opcode, sp) ring). Then re-run in emumode=2.
Success criterion is unchanged: `iplram_spodd.bin` never appears.

---

## UPDATE 2026-09-10 (goal round 4) — **ROOT CAUSE FOUND: the ARM64 dynarec executes STALE,
## PRE-DECRYPTION code of the boot stub. The round-3 "S8 byte-lane `^3`" theory is WRONG.**

### 0. Engine A/B is now real (round 3's attempt silently did not apply)
`main.c` gained a DD-gated, marker-file-gated engine override (`g_wd_dd_route` is set only when the
64DD IPL ROM loaded; the override additionally needs `files/wd_emumode1.flag`). With that flag set
the run is **provably** the cached interpreter: `dynarec_sample_hook` (called *only* from
`arm64/linkage_arm64.S:do_interrupt`) consumes `files/wd_force.flag`; the flag was **not** consumed
over 25 s of a running game => the dynarec was not running.

| run | engine | odd `$sp` | guest fault | result |
|---|---|---|---|---|
| dynarec | emumode=2 | yes, every run | `WDFAULT vaddr=079b0880` | boot dies |
| control | **cached interpreter** | **never** | **never** | boots, stalls on the DD-loading screen |

(Interpreter run: ~15 min, 60 FPS, DD-loading bar identical to the dynarec run, **zero**
`iplram_fault.bin` / `iplram_spodd.bin` / `wd_spodd2.txt` written.)

### 1. The guest code at 0x800bb540 SELF-MODIFIES
`0x800bb540` is **not** a checksum routine — it is an **in-place decryptor**. Its loop
(`0x800bb58c..0x800bb5b0`) does `lbu t8,2(v0); lbu t9,3(v0); subu t7,t8,v1; addu t1,t9,t3;
sb t7,2(v0); sb t1,3(v0)` over `a3 = 0x800bb67c`, 804 bytes. Only the **low halfword** of every word
changes. Verified byte-for-byte against the cart ROM (`ROM 0x5567c` vs RDRAM `0x800bb67c`):

```
0x800bb67c: ROM 27bdbb03  ->  RDRAM 27bdfec0     addiu sp,sp,-320
0x800bb69c: ROM 0c02b007  ->  RDRAM 0c02f3c4     jal   0x800bcf10 (bzero)
0x800bb6a0: ROM 2405be7f  ->  RDRAM 2405013c     addiu a1,zero,316
```
The decrypted `__LeoBootGame2` then calls `bzero(0x800bb540, 316)` — i.e. it **erases its own
decryptor** (`0x800bb540 + 316 = 0x800bb67c`, exact). **In the fault dump `0x800bb540` is still
byte-identical to the ROM, so that `bzero` never ran.**

Relocation confirmed again: `RDRAM[v] == cart.z64[v - 0x80000000 - 0x66000]`, byte-swapped.

### 2. The dynarec ran the ENCRYPTED form — three independent, exact matches
```
ENCRYPTED 0x27bdbb03 = addiu sp,sp,-17661   -> prior sp must be 0x800d8700  (8-byte aligned ✓)
DECRYPTED 0x27bdfec0 = addiu sp,sp,-320     -> prior sp would be 0x800d4343 (ODD, impossible ✗)
observed $sp at the fault                = 0x800d4203  (= 0x800d8700 - 17661, exact)
```
```
ENCRYPTED word @0x800bb69c = 0x0c02b007 = jal 0x800ac01c
observed CP0 EPC                                    = 0x800ac01c   ✓ exact
jal return address (0x800bb69c + 8)                 = 0x800bb6a4   ✓ = observed $ra
observed cause = 0x9000002c (COP1 unusable, CE=1, BD=1) — 0x800ac01c is `jr ra`
   with a COP1 `add.s` delay slot, so landing there with CU1 clear is exactly this exception.
```
`0x800ac01c` is **only** reachable from the encrypted `jal` — the decrypted code has
`jal 0x800bcf10` in that slot. This is airtight.

### 3. So the odd `$sp` was never a byte-lane bug
`0x800d4203` is **not** `0x800d4200 ^ 3`; it is `0x800d8700 - 17661`. The low byte `0x03` comes from
`0x08 - 0x05` in the subtraction (`0xBB03` sign-extended). The S8/`^3` reading in the round-3 section
was a **coincidence**. The misaligned `lw ra,20(sp)` -> `0x079b0880` and the execute-fault chain are
merely downstream of any odd `$sp`.

### 4. Where the emulator is wrong
mupen's new_dynarec reuses a cached translation of `0x800bb67c` compiled **before** the decryptor
wrote there, and never invalidates it. Concrete suspects, in order:
1. `new_dynarec.c:store_assemble` calls `do_tlb_w_branch` (the only WRITE_PROTECT test) **only under
   `using_tlb`**. In the `!using_tlb` path stores to RDRAM take the inline fast path with **no
   write-protect check at all** — `INTERPRET_STORE` is commented out (`new_dynarec.c:81`).
2. `new_dynarec_init` maps `0x80000000..0x807FFFFF` to `ram_offset` **without** WRITE_PROTECT, and
   WRITE_PROTECT is only ever added by `get_dirty()` (`new_dynarec.c:2539`, i.e. only for blocks
   registered in `jump_dirty`) and by `invalidate_all_pages`/`tlb_speed_hacks`. A block that is
   compiled and then direct-linked is never write-protected => its page is freely writable.
In both cases the guest's `sb` simply lands in RDRAM and the stale block survives. Note the DD
controller *does* call `invalidate_r4300_cached_code` after DD DMAs — that is why DMA-driven SMC
(cart/DD loads) works while **CPU-store-driven SMC does not**.

### 5. Tools added this round
`main.c`: DD-gated `R4300Emulator=1` override (`g_wd_dd_route` + `files/wd_emumode1.flag`).
Inert for plain carts and for the cart-hack; remove or keep as a diagnostic.

### Next step (round 5)
Make CPU stores to a page holding cached code invalidate it on the DD route — the two candidate
sites above. Success criterion is unchanged and is now a *fault* criterion:
**`iplram_fault.bin` never appears and the DD-loading bar passes 8/8.** Recommended probe before
editing: log `using_tlb` and `memory_map[0x800bb]` at the moment `new_recompile_block` compiles
`0x800bb67c`, and log every `invalidate_addr` call, to confirm which of (1)/(2) fires.
Also still open (independent of this bug): the post-load SP/RSP deadlock — the **interpreter run
did not get past the DD-loading screen either**, so the round-13 queue-full/RSP-halted analysis
still needs its own fix.

---

## UPDATE 2026-09-10 (goal round 5) — **FIXED: the recompiler now boots the 64DD game.**
## Root cause was NOT write-protect/byte-lane — it was *speculative compilation past a JAL*.

### 1. The instrument that cracked it
`new_dynarec.c` gained a **doubly-gated** SMC trace: 64DD route only
(`g_dev.dd.idisk != NULL`) **and** a page window (`0x800bb000-0x800bbfff`). It logs
`COMPILE`/`BLOCK`/`LINK`/`CHK_*`/`GA_*`/`HT*`/`INVAL_BLOCK` events, capped at 2000, to
`files/wd_smc.txt`. One 50 s run was enough. Proof it is inert for plain carts: the
Mario Tennis run produced a 22-byte file (header only, zero events).

### 2. The smoking gun (`.fzxwork/wd_smc_run1.txt`)
```
162: COMPILE a=800bb540 b=3c07800c c=27bdbb03    <- c = mem[0x800bb67c] = SCRAMBLED
180: BLOCK   a=800bb540 b=00000133 c=800bba0c    <- slen=0x133=307 insns, ends 0x800bba0c
181: INVAL_BLOCK a=000800bb                      <- descrambler's 1st store — TOO LATE
```
`LeoBootGame` (0x800bb540) descrambles `__LeoBootGame2` (0x800bb67c) and `__LeoBootGame3`
(0x800bb9a0) in place and then **calls** the fresh code. The dynarec's block for
`0x800bb540` was **307 instructions long and ran to 0x800bba0c** — it swallowed the call
target *and* the next function, i.e. **`__LeoBootGame2` was translated INLINE, from the
still-scrambled bytes, into LeoBootGame's own host block.**

### 3. Why (the exact scan logic)
`new_recompile_block`'s Pass 1 has "speculative precompilation": for a JAL
(`itype==UJUMP && rt1==31`) `done` stays 0, so the scan continues past the call to
compile the *return* path. It stops at the next `jr ra` (`rt1==0` → `done=1`) — **unless**
this test finds a branch into the delay-slot area:
```c
for(j=i-1;j>=0;j--) {
  if(ba[j]==start+i*4)   done=j=0;   // branch into delay slot
  if(ba[j]==start+i*4+4) done=j=0;   // <<< the killer
  if(ba[j]==start+i*4+8) done=j=0;
}
```
For FZX: `jr ra` at 0x800bb674, delay slot 0x800bb678 → `start+i*4+4 = 0x800bb67c`, and the
**JAL at 0x800bb664 calls exactly 0x800bb67c**. The test (meant for "some branch targets the
code right after the delay slot") matches a *call* target, `done` is cleared, and the block
grows straight through the callee. `internal_branch()` then reports the JAL target as
internal, the JAL is linked as an internal jump, and the scrambled bytes are emitted inline.

Page invalidation cannot save this: `invalidate_addr()` kills the block's **entry points**,
but the block that is *already executing* keeps running to its internal target. The cached
interpreter never had the bug because it re-reads memory for every instruction.

### 4. The fix — `new_recompile_block()`, DD-gated, 2 lines
```c
  if (g_dev.dd.idisk != NULL) stop_after_jal = 1;
```
Every block now ends at its JAL, so the call target is resolved through `get_addr_ht()` at
run time — which recompiles it from the **descrambled** bytes. Same mechanism the stock
"Disabled speculative precompilation" path already uses.

### 5. Verified on RP6 (F-Zero X (Japan).z64 + F-Zero X.ndd, emumode=2)
| | before | after |
|---|---|---|
| `iplram_fault.bin` / `iplram_spodd.bin` | written every run | **never written** |
| guest fault | `WDFAULT vaddr=079b0880`, odd `$sp`=0x800d4203 | **none** |
| boot | dies before the DD screen | **reaches 64DD "DD LOADING", 59-60 FPS** |
Blocks are now short (slen 2..0x29 instead of 0x133) and `INVAL_BLOCK` no longer appears on
that page. **The recompiler now agrees with the cached interpreter**, which is the intended
outcome. Plain-cart regression: Mario Tennis (USA).zip boots to an in-game match at 59 FPS,
process alive, zero trace events. Committed as `c12de6262`.

One-off note: the very first run after installing the fixed APK died with **SIGILL
(ILL_ILLOPC) inside the JIT buffer** (pc in `[anon:.bss]`, i.e. `extra_memory`). It did not
reproduce on two subsequent runs of the same build (>4 min each). Keep an eye on it.

### 6. REMAINING BLOCKER — the post-load stall (unchanged, and now the *only* thing left)
The bar freezes at ~6.5-7/8 segments on "DD LOADING". Evidence from this round:
* `wd_rsp.txt` (RSP plugin trace) is the live signal: **the same task (`seq=2764`) exits over
  and over, ~60×/s, at `pc=0x00b8`, `status=0x243` (HALT|BROKE|INTR_BREAK|SIG3),
  `irq=1`, `sem=00000000`, `timed=32767`** — the audio ucode's SP_STATUS poll times out
  every time and the task never completes. `seq` never advances ⇒ no new task is ever
  started by the guest.
* `files/wd_force.flag` was **not consumed in 4 minutes** ⇒ `dynarec_sample_hook` (called
  only from `arm64/linkage_arm64.S:do_interrupt`) never runs ⇒ **the guest CPU takes no
  interrupts at all**; the emulation thread is effectively 100 % inside the RSP.
* The audio budget in `parallel.cpp` is currently **100 ms for ttype==2** (`dsp_task_type == 2
  ? 100000 : 50000`). Round 6 earlier found the **10 ms** audio budget was what made the machine
  schedule at all and advanced the loading bar — that value is NOT in the tree now. Re-trying
  10 ms (or bounding the wedged audio wait) is the cheapest next experiment.
* Round 13's chain still stands behind it: SP/DP event queue `0x800dcad0` full (16 msgs,
  unconsumed), prio-99 SP-consumer thread STOPPED, guest game queue `0x800dca40` empty.

### Next step (round 6)
1. Re-check the audio budget value (100 ms → 10 ms) and re-run; confirm with `wd_rsp.txt`
   whether `seq` starts advancing and the bar passes 8/8.
2. Get a guest-CPU dump at the stall: the force flag needs `(d_sample & 0x1FFF)==0`, which
   only advances when `do_interrupt` runs — if the CPU takes no interrupts it never fires.
   Either widen that trigger (e.g. also check from the RSP-side hook, which runs 60×/s) or
   accept `wd_rsp.txt` as the primary instrument.
3. Then the `do_SP_Task` completion contract: at the stall the ucode BREAKs
   (`status` bit 0x2) and the core's DD-gated completion path should raise MI_INTR_SP and set
   TASKDONE — verify the guest actually observes it (`rsp_interrupt_event`, DD-gated only).

### Round-5 addendum — the RSP-side saturation experiment (NEGATIVE result, reverted)
`parallel.cpp` sets `RSP::SP_STATUS_TIMEOUT = dd_mode ? 0x7fff : 16` (the SP_STATUS
poll budget before a ucode yields). The DD value means each yield costs **~17 ms of host
time** (32767 polls), and `do_SP_Task` re-schedules with `sp_delay_time` ~60×/s, so the
emulation thread is ~100 % inside the RSP — which is why `wd_force.flag` is never consumed.

Tried `SP_STATUS_TIMEOUT = 256` for the DD route (16× the stock 16 polls):
* it made the yields ~130× cheaper, but **the game still stalls at exactly the same
  DD-loading bar position, and `wd_force.flag` is still not consumed** — the short poll
  budget alone does not wake the guest CPU;
* the side effect was severe: with cheap slices the DIAG trace `wd_rsp.txt` reached
  **4.5 GB in 75 s** and dominated the thread (FPS 60 → 31). That made the measurement
  invalid on its own.
**Reverted the poll-budget change.** Kept a hard cap on the RSP trace
(`WD_RSP_LOG_MAX = 40000` lines) because the trace can otherwise balloon regardless.
Before that cap, the DD trace was already writing ~5 MB/s, which is itself contention on
the emulation thread — worth remembering when reading any RSP-side timing.

**So the post-load stall is NOT RSP throughput.** It is a guest-side wait: no new RSP task
is ever started (`seq=2764` never advances) and the CPU never dispatches an interrupt.
Next round should go after *why the guest stops starting tasks* — the round-13 chain
(SP/DP event queue `0x800dcad0` full and unconsumed, prio-99 SP-consumer thread STOPPED,
guest game queue `0x800dca40` empty) is the place to look, and the `wd_force.flag`
mechanism needs a wider trigger (`dynarec_sample_hook` needs `(d_sample & 0x1FFF)==0`,
which never comes when the CPU takes no interrupts — consider also arming the dump from the
RSP hook, which runs ~60×/s).

---

## UPDATE 2026-09-10 (goal round 6) — the watchdog was lying; the real stall is an RSP livelock on a 0x00010001-filled DMA

### 0. The instrument was broken (this invalidated every previous "stall dump")
The stall thread fires when `wd_hb` has not advanced for 4 s, but `wd_hb` was only
incremented every `1<<20` `dynarec_sample_hook` calls — at the observed 10–300 calls/s that
is **never**. So `iplram_wd.bin` was written **~4 s after emulation start, every run**, and
all round-3/4/5 "stall" dumps were really early-boot dumps. Fixed: on the 64DD route
`wd_hb++` on every sample (plain carts keep the old rule).

New probe `files/wd_stall.txt` (written by `wd_full_dump`, DD-gated) samples the machine
**twice 300 ms apart** plus a host-PC ring, the dynarec block ring, the live RSP
IMEM+DMEM and per-thread `/proc/self/task/<tid>/stat`:
```
A/B  cause status epc badvaddr count | mi_intr mi_mask sp_status sp_pc …
A/B  c_task c_spint c_genint c_sample c_asic c_pi vi_cur …
DELTA …        ← what is still moving
RING  <2048 last dynarec block entries: vaddr sp ra>
SPMEM <8192 bytes: DMEM then IMEM, same buffers the plugin gets>
THREADS/HOSTPC …
```
HOSTPC is a `SIGPROF`/`ITIMER_PROF` sampler (armed by the watchdog thread once `dd.idisk`
is set) that grabs host pc/lr/fp **on whichever thread is burning CPU**. Note: it stores
64-bit values now — the first version truncated to 32-bit and every PC classified as
UNMAPPED.

### 1. Measured state at the real stall (F-Zero X (Japan).z64 + F-Zero X.ndd, emumode=2)
* **Not a hard freeze.** `DELTA c_task=+6 c_spint=+6 c_genint=+6 c_sample=+0 c_asic=0 c_pi=0`
  over 300 ms → the RSP completes **20 tasks/s**, and `c_asic`/`c_pi` are frozen.
* `SP_STATUS = 0x243` (TASKDONE|INTR_BREAK|BROKE|HALT) or `0xc0` (INTR_BREAK|SIG0=yield),
  `SP_PC=0x98`, `MI_INTR=0`, `CAUSE=0x10000000` (no IP bits), `STATUS=0xff00` (**IE=0**),
  `EPC` = the guest's `b .` idle loop.
* `wd_rsp.txt`: **every** RSP task exits with the *same* pc `0x00b8`, status `0x243`, and
  `wd_freeze.txt` shows `dur=50` — i.e. each task burns its **entire 50 ms host budget**.
  20 × 50 ms = 100 % of the emulation thread. **That is the throttle.**
* `ps`/`/proc`: exactly one thread (16001, the emulation thread) is in state **R** with
  `utime` +451 jiffies per 4 s = 100 % of a core.

### 2. Root cause found: the guest submits an RSP task header of `0x00010001`
New `RSPHDR` log in `parallel.cpp` prints the DMEM task header the guest submitted:
```
RSPHDR … type=65537 flags=65537 boot=00010001 bootsz=00010001 ucode=00010001
        ucosz=00010001 udata=00010001 udsz=00010001 stack=00010001 stksz=00010001
        obuf=00010001 obsz=00010001
```
`0x00010001` is **this emulator's uninitialised-RDRAM fill** — it is what lives at
`0x80000400` upwards (RDRAM runs of it: `0x80000400..0x8000432c`, `0x800067d0..0x80010158`, …).
So `do_SP_Task` reads `ttype = 0x00010001` (neither 1 nor 2), the RSP's boot ucode reads
`ucode = 0x00010001` from `DMEM[0xFD0]` and DMA-reads **4 KiB of fill from RDRAM
`0x00010000`** into IMEM, and the RSP then executes `0x00010001` no-ops until the 50 ms
budget expires — 20×/s, forever. **The loading bar can never advance in that state.**
`DMEM[0xFC0..0xFFF]` is entirely `0x00010001` and `IMEM` is entirely `0x00010001`.

### 3. Two genuine emulation bugs found on the way (both DD-gated, both hardware-accurate)
**(a) plugin SP DMA clamped instead of wrapping** — `mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp`
```c
if (((*cr[CP0_REGISTER_DMA_CACHE] & 0xFFF) + length) > 0x1000)
    length = 0x1000 - (*cr[CP0_REGISTER_DMA_CACHE] & 0xFFF);
```
The 64DD boot ucode issues `SP_MEM_ADDR=0x1080 / SP_RD_LEN=0xF7F` — a **4096-byte wrap
transfer** that loads the whole main ucode into IMEM starting at 0x80 and wrapping into
0x000..0x07F. Clamping truncated it to 3968 bytes. Proven live: in the stall IMEM,
`imem[(0x80+k)&1023] == ucode[k]` for 890/1024 words, the 134 mismatches being exactly
`imem[0x000..0x217] = 0x00010001`. Now wraps on the DD route (`!rsp_ares_budget_enabled()`).

**(b) core SP DMA ran off the end of the RSP memory** — `device/rcp/rsp/rsp_core.c:do_sp_dma`
indexes `spmem[memaddr^S8]` from a base fixed by the *initial* `SP_MEM_ADDR & 0x1000` and
**increments `memaddr` without masking**. For the same 0x1080/0xF7F transfer the counter
runs 0x080..0x107F, i.e. **128 bytes past the end of the 8 KiB `sp->mem`**: the wrapped tail
(`IMEM[0x000..0x07F]`, which is the ucode's last 0x80 bytes) is never written and 128 bytes
of the neighbouring arena are clobbered. Now masks each access (`memaddr & 0x1fff`,
`dramaddr & 0x7fffff`) on the DD route.

### 4. Measured effect of the fixes
| | before | after |
|---|---|---|
| RSP task exits | **all** `pc=00b8 status=243` (identical) | varying pcs `0x180..0xfe6`, status `0x41` (clean budget yield) |
| `SP_PC` across the 300 ms probe | frozen | moving (`0x98` → `0xf94`) |
| IMEM | 134 words of `0x00010001` | no fill (`(b)` alone) |
| screen | "DD LOADING", bar at **7/8**, 59 FPS | "DD LOADING", bar **reset to 0**, 96 FPS |

**The bar reset came specifically from the core `do_sp_dma` wrap (`(b)`)** — the plugin-only
fix `(a)` still showed the bar at 7/8. The RSP is unambiguously executing the ucode now
instead of walking fill, but the game is still stuck because the task header it submits is
still all `0x00010001` (§2) and it still burns 50 ms per task.

### 5. Next step (round 7) — who fills the guest's OSTask with `0x00010001`?
The guest's `OSTask` (and everything it points at) reads as uninitialised RDRAM fill. That is
the single remaining blocker. Concretely:
1. **Log the CPU-side SP DMA** (`do_sp_dma`, DD-gated): memaddr/dramaddr/length/dir for every
   transfer, correlated with the `RSPHDR` lines. That will show whether the guest ever DMAs a
   *real* OSTask into `DMEM[0xFC0]`, or whether a later DMA from a fill region overwrites it.
2. Find the guest's OSTask in RDRAM (the region that should hold it) and check whether the
   game ever wrote it — i.e. whether the game's task struct is at a RDRAM address the EK data
   load should have filled (⇒ DD load incomplete) or whether an unrelated DMA clobbered it.
3. Only then reconsider the 50 ms non-audio budget: with a fill task it is pure waste
   (20 × 50 ms = 100 % of the thread), but lowering it does not fix the fill header.
4. If the answer is "the game never received that data", go back to the DD read path
   (round 13's seek→BM stall) with the new `c_asic`/`c_pi`/DD-trace instruments.

**Regression status: NOT YET VERIFIED for this round.** Both fixes are runtime-gated
(`g_dev.dd.idisk != NULL` / `!rsp_ares_budget_enabled()`), so plain carts take the original
code paths byte-for-byte — but a Mario Tennis (`support64dd=false`) run must still be done
before round 7 ends, per the standing rule.

### 6. Committed in round 6
`wd_stall.txt` probe + fixed heartbeat + SIGPROF host-PC ring + progress counters + DD/PI
counters (`cached_interp.c`, `interrupt.c`, `dd_controller.c`, `pi_controller.c`),
`RSPHDR` task-header log (`parallel.cpp`), plugin DMA wrap (`cp0.cpp`), core DMA wrap
(`rsp_core.c`). Watch out: `.fzxwork/build*.log` and the `.fzxwork/r6/` evidence dir are
untracked — the raw evidence for this round lives in `.fzxwork/r6/`.
