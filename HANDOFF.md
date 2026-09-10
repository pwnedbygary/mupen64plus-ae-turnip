# Handoff Summary: F-Zero X EK on 64DD (mupen64plus-ae-turnip)

> Auto-generated checkpoint. Read top-to-bottom. The single biggest concrete progress this
> session: **the missing-DD-ROM root cause is fixed and verified** — the DD ROM and disk now
> load, so the remaining black-screen fault is a *different, later* problem that needs fresh
> diagnosis rather than the prior MI-interrupt fix.

**ROUND 20 (goal round 20) — THE LIVE UCODE IS PROPERLY IDENTIFIED (gspF3DEX2_fifo), AND THE
RDP IS BEING KICKED AT A RING BUFFER THAT IS NEVER WRITTEN.** Branch
`dd-eos-watchdog-checkpoint`. Evidence: `.fzxwork/r20a` (SIG0 experiment), `.fzxwork/r20c`
(control = same build with the experiment off), `.fzxwork/r20d` (+ write-side counters),
`.fzxwork/r20e` (early-timing probe).

**1. Address ground truth (this fixes several earlier rounds' instruction-level conclusions).**
The decomp's `linker_scripts/jp/ek/symbol_addrs_nlib_vars.txt` maps every RSP blob:
`rspbootTextStart=0x807504F0`, `aspMain=0x80768E60`, `gspF3DEX2_fifoText=0x807505C0`,
`gspF3DLX2_Rej_fifo=0x80751950`, `gspF3DFLX2_Rej_fifo=0x80752AE0`, `gspL3DEX2_fifo=0x80753C70`,
`gspF3DEX2_Rej_fifo=0x80754E00`. `wd_hdr15.txt` reports the guest's `OSTask.ucode` =
**0x7505C0 = gspF3DEX2_fifo**, i.e. the game is in `GFXMODE_F3DEX`. Its text is DMA'd to IMEM
0x080 with length **0xF80** (`wd_imem.txt` n=1), so **PC = 0x080 + (rdram - 0x7505C0)** and the
text ends exactly at RDRAM 0x751540; the two overlays follow at ucode+0xF80 (0x98 B, loaded over
IMEM 0x000 = the yield/completion handler) and ucode+0x1018 (0x170 B, the command dispatcher).
Rounds 15-19 disassembled the *0x752AE0* copy (gspF3DFLX2_Rej_fifo, a different ucode) and/or
used a PC base 0x1000 too high, so some of their per-instruction claims describe the wrong
bytes; `.fzxwork/rsp_dis.py`'s CP0 name table also called registers 8/9 "SP_PC"/"SP_IBIST"
(hiding every RDP kick) — both are fixed in the tool now.

**2. The ucode's own yield handler exists, and round 17 was clobbering its result.**
`mtc0` scan + disassembly: entry at IMEM 0x098, main loop at 0x160-0x1B4, DMA macro at
0xFD8/0xFC8, and the overlay's yield handler at PC 0x0060:

    0x0060 lw  t3,0xFD0(r0)   # ucode base      0x0064 sw  k0,0xBF8(r0)   # >>> SAVE k0
    0x0068 sw  t3,0xBFC(r0)   # --------------- 0x0070 lw  t8,0xFF8(r0)   # yield_data_ptr
    0x0074 addi s4,r0,0x8000  # write DMA       0x0078 addi s3,r0,0x0BFF
    0x007C j   0x0FD8         # DMA DMEM[0..0xBFF] -> yield buffer
    0x0080 addi ra,r0,0x1088  # -> PC 0x088: mtc0 SIG1|SIG2 ; break

and the YIELDED resume takes `k0` from exactly that word (`j 0x0164` with `lw k0,0xBF8(r0)` in
the delay slot). Round 17's `hdr[0xBF8/4] = data_ptr` therefore **destroys the resume pointer the
ucode itself wrote**. Removed (parallel.cpp; measured behaviourally neutral on its own:
t1gfx=6/aud=198 both ways, RDPKICK 3171 vs 2538).

**3. "Ask the ucode for a yield" (host sets SIG0) was TRIED AND MEASURED BADLY.**
Rationale: the ucode only suspends coherently at its SIG0 test (0x1A8), so a host preemption
should set SIG0 and let it run into 0x60. Measured (r20a vs the r20c control, same tree, only
this switch different): gfx task loads **6 -> 1**, audio loads 198 -> 193, RDP kicks
**2537 -> 28091** with the walk running away to `start=fffffff8`, and the ucode still never ran
its yield handler (`save=0`). The guest never clears a plugin-set SIG0, so the ucode's main loop
diverts to the yield path on every command. Kept in the tree as `R20_SIG0_YIELD` **default 0**,
with the measurement recorded next to it.

**4. NEW, SHARP LEAD: the RDP is kicked at a 320 KiB ring that the ucode never fills.**
New DD-only counters (`wd_r20.txt`, rewritten every 4 s from `DoRspCycles`) classify every
ucode-issued transfer. At 50 s: reads `dma=109696` of which `datalist=197` (source == the
header's data_ptr -> the real display list, so the walk *does* start correctly), `outbuf=40386`
(source inside 0x2D9CD0..0x32DCD0), `low=102592` (below 1 MiB = cleared memory); writes
`wr=2950` of which `outbuf=16` and `datalist=1272`. The kick code (PC 0x250-0x2BC) is a **ring**
protocol: `t8 = DMEM[0xF0]` is the ring write pointer, `mtc0 t8,DPC_END` kicks, and when
`DMEM[0xFEC] (output_buff END = 0x32DCD0) - (end+len) < 0` it waits for `DPC_CURRENT ==
DMEM[0xFE8] (0x2D9CD0)` and re-points `DPC_START` there. `wd_cmd.txt` matches that decode
kick-for-kick (START 0x32DCD0 -> 0x2D9CD0, END 0x2D9E30/0x2D9F98/... climbing by ~0x168).
**But the ring is 100% zeros at 50 s (0 nonzero words of 86016) while only 16 write transfers
ever land in it** -- so every window the RDP is handed is all-G_NOOP, `wd_c_rdp_empty` is 2033 of
2537 kicks, **`MI_INTR_DP` is never raised** (`raise_bits DP=0`, `dp_seen=0`), and the guest's
gfx thread stays in `osRecvMesg(&D_800DCAC8)` (sys_gfx.c:203 `func_80067D64`) -> the 64DD bar
never advances (screen identical to r19d: YAVG 5.3, bar 657..1257 px on row 573).

Early probe (`.fzxwork/r20e`, dump at 12 s instead of 50 s): the ring is **still 0 nonzero
words / 0 FULL_SYNCs**, `dp_seen=0`, `DP=0`, `empty=1524/1902` kicks, with reads
`dma=29600 datalist=934 outbuf=2233 low=2697` and `save=0`. So the ring is never written at any
point in the run -- this is not a late degradation but the state from the first frames on (the
DD loading screen the RP6 shows is drawn by the guest CPU, not by the RDP; `wd_c_rdp_empty`
was 2033/2537 at 50 s and 1524/1902 at 12 s).

**5. THE UCODE'S PROTOCOL IS NOW READ FROM ITS DOCUMENTED SOURCE, NOT INFERRED.**
`github.com/Mr-Wiseguy/f3dex2` (CC0, "matching and mostly documented disassemblies of the
F3DEX2/F3DZEX2 family") is the same microcode this ROM runs. It confirms every decode above and
resolves the DMA macro: `dma_read_write` is

    mtc0    dmemAddr, SP_MEM_ADDR
    bltz    dmemAddr, dma_write
     mtc0   cmd_w1_dram, SP_DRAM_ADDR   <- DELAY SLOT: executes on BOTH paths
    jr $ra
     mtc0   dmaLen, SP_RD_LEN
 dma_write:
    jr $ra
     mtc0   dmaLen, SP_WR_LEN

(round 20 first read 0xFEC/SP_DRAM_ADDR as write-branch-skipped; it is the delay slot of
`bltz`, so the publish DOES set the DRAM address to `rdpFifoPos`). Its ring protocol:
DMEM 0xF0 = `rdpFifoPos` = the RDP fifo position; two 0x158-byte DMEM command buffers at 0xBA8
and 0xDB0 (`rdpCmdBuffer1/2`); `check_rdp_buffer_full_and_run_next_cmd` accumulates into them and
`flush_rdp_buffer` does, in order: `mtc0 cmd_w1_dram(rdpFifoPos), DPC_END` (the kick), the
wrap/`DPC_START = OSTask.outbuff` handling, a **back-pressure wait on DPC_CURRENT**
(`f3dzex_000012A8`: spin while `0 < DPC_CURRENT - rdpFifoPos <= dmaLen`), then
`sw rdpFifoPos+dmaLen, 0xF0` and the DMEM->RDRAM publish. So the whole protocol is built on
**DPC_CURRENT advancing**.

**5b. WHAT THE DOCUMENTED SOURCE SETTLES ABOUT THE ENTRY STATE.** `rdpFifoPos` (DMEM 0xF0) is
part of the ucode's *data section* and is written only in `task_init` (cold), by the flush, and
never zeroed on the FIFO variant's completion path -- the ring state is meant to survive between
tasks via DMEM[0xF0] plus the RDP's own DPC registers. `task_yield` stores `taskDataPtr` to
DMEM[0xBF8] and the ucode pointer to 0xBFC and the resume reads 0xBF8 back, which is independent
confirmation that round 17's `hdr[0xBF8] = data_ptr` was wrong (already reverted). Measured this
round: at every gfx task entry DMEM[0xF0] already reads 0x0A446669 (an ID-string fragment, i.e.
another ucode's data left in DMEM) -- so the ucode takes the WARM path and never runs
`task_init`, which is the only place that sets DPC_START/DPC_END/rdpFifoPos from the header. That
is consistent with the ring never being written and with the kicks drifting to garbage. Next
round must settle *when* 0xF0 becomes nonzero: is the gfx `ucode_data` (gspF3DEX2_fifoDataStart =
0x779860, 0x800 bytes) actually DMA'd to DMEM 0x000 at task load (the audio one, 0x794E90 ->
0x000, is visible in `R20F`), and is the value logged at DoRspCycles entry read before or after
the boot ucode has run.

**6. THE RDP IS SILENTLY DROPPING THE WINDOWS.** `vk_process_commands`
(`mupen64plus-video-parallel/upstream/parallel_imp.cpp:148`) finishes a call by setting
`DPC_START = DPC_CURRENT = DPC_END`, but returns early -- leaving the pointers untouched -- in
three cases: `length <= 0`, the `0x8000`-command capacity guard
(`(cmd_ptr + length) & ~(0x0003FFFF >> 3)`), and `DP_END > 0x7ffffff || DP_CURRENT > 0x7ffffff`.
New DD-gated detector in the core's `rsp_process_rdp_list` wrapper compares
`DPC_CURRENT` against the masked `DPC_END` after every kick (`wd_c_rdp_noadv` / `wd_c_rdp_bad`,
printed in `wd_stall.txt` as `RDPDP ... noadv= bad=`). Measured (`.fzxwork/r20i`, 20 s):
**RDPKICK n=3170, noadv=3030, bad=2160, empty=2540, dp_seen=0, DP=0** -- i.e. 96% of the kicks
hand parallel-RDP a window it refuses and never hands back, so the ucode's `DPC_CURRENT`
handshake can never complete, the publish never runs, the ring stays empty, no FULL_SYNC ever
reaches the RDP and `MI_INTR_DP` is never raised. That is the deadlock, measured end to end.

Concrete next step (round 21): make the RDP's early returns hardware-faithful -- a real RDP
consumes whatever window it is given and leaves `DPC_CURRENT == DPC_END` -- i.e. advance the
pointers (and drop the buffered commands) instead of returning silently, then re-measure
`noadv`, the ring content, `dp_seen` and the loading bar. This is a *video plugin* change, so
the DD gate must be a runtime one (the plugin has no `g_dev`): gate it on the game (e.g. an
explicit flag passed at RomOpen, or the presence of the DD file flag), and re-verify plain
carts (Mario Tennis, cart-hack with support64dd=false) before keeping it.

Previous round-20 note, superseded in part by (5): the ucode's kick code publishes `s6..s7` (the converted commands
assembled in DMEM) to `DMEM[0xF0]`, and one write transfer should follow each kick -- but of
2950 writes only 16 land in `[output_buff, output_buff_end)`. Latch the first eight writes whose
destination is *near* `DMEM[0xF0]` (within +/-0x1000) together with the pc and the DMA length,
plus the matching reads, and compare its pc against the kick code at PC 0x250-0x2BC: either the
publish DMA is issued at an address the plugin mis-decodes (a `SP_MEM_ADDR`/`SP_DRAM_ADDR`
mix-up would land it somewhere plausible-looking), or the ucode is not reaching the publish at
all and the END-advance comes from a stale DMEM[0xF0].

**ROUND 15 (goal round 15) — the ucode's task protocol is decoded properly, and the poisoning
event is caught red-handed: an audio-ucode DMA with an UNDERFLOWED length wipes the task
header + all of IMEM in one transfer.** Branch `dd-eos-watchdog-checkpoint`.

**The ucode protocol (disassembled from the running tree, not inferred).** Read with a
purpose-built RSP disassembler (`.fzxwork/rsp_dis.py` = file disasm, `.fzxwork/rsp_live.py` =
live IMEM/DMEM out of a `wd_ucode*.bin` capture; RSP CP0 map: 0-3 DMA regs, 4 SP_STATUS,
8/9/10/11 DPC_START/END/CURRENT/STATUS, i.e. the ucode's "kick" is `mtc0 x, DPC_END` ->
`RSP::rsp.ProcessRdpList()`, NOT a magic SP_STATUS bit).
* Task entry = IMEM 0x1080 (the boot at 0x1000 loads DATA+`0xF80` of TEXT to IMEM 0x1080, then
  `jr 0x1080`). Entry logic: `t3 = DMEM[0x0F0]` (the RDP end pointer the ucode stores on a cold
  start = "already initialised" marker), `t4 = DMEM[0x0FC4]` (the header's flags word).
  `DMEM[0xF0]==0` -> FRESH path; `!=0 && !(flags&1)` -> WARM path; `!=0 && (flags&OS_TASK_YIELDED)`
  -> **RESUME**: `k0 = DMEM[0x0BF8]` (the DL pointer the yield path saved), and libultra has
  already loaded the *yield buffer* as the task's ucode DATA. The FRESH/WARM paths take
  `k0 = DMEM[0x0FF0]` = the header's `data_ptr`. All three then `k0 += 0xA8` per fetched chunk.
* Display-list walk: read 0xA8 bytes from k0 into DMEM 0x920 (F3DEX2) / **0x9B0 (F3DLX2)** ,
  `lw t9,0xA58(k1)` (F3DEX2) / `0xA58`-equivalent, 8-byte GBI commands, dispatch through a table
  loaded from the *other* overlay; SP_STATUS is polled once per command and SIG0
  (`andi at,at,0x80`) branches to the overlay at 0x1FAC, which loads the "finish" overlay to
  IMEM 0x1000 and returns to 0x1000.
* **The finish overlay (blob offsets 0x1F80..0x2000, i.e. IMEM 0x1F80 after a text load) is the
  whole completion path**: flush the accumulated RDP commands (`s7 - s6` bytes) out of DMEM 0xBA8
  (F3DEX2) / 0xA58 (F3DLX2), kick the RDP, then either `bltz at -> 0x1084` (mtc0 0x4000 =
  SET_TASKDONE, then `break`) or `bne at,r0 -> 0x1060` (the **yield save**: `sw k0,0xBF8`,
  `sw ucode,0xBFC`, then DMA-write DMEM 0..0xC00 to the header's yield_data_ptr, then the same
  SET_TASKDONE+BREAK). The resume entry reads exactly those two saved words.
* **No ucode anywhere writes SIG1 (SP_STATUS_YIELDED).** Scanned every `mtc0 ...,SP_STATUS` in
  the F3DEX2 blob (text+overlays), the F3DLX2 blob and the audio blob (0x80768E60): the only
  writes are `0x2800` (CLR_SIG1|CLR_TASKDONE) at entry, `0x4000` (SET_TASKDONE) + `break` at
  completion. So the libultra yield answer (SIG1) never comes from the microcode, and
  `osSpTaskYielded()` should always return 0 -- **yet `wd_hdr15.txt` catches a gfx task loaded
  with `flags=0x05` (= OS_TASK_LOADABLE|OS_TASK_YIELDED, seq 201), i.e. a resume really did
  happen.** The only code in the tree that can set SIG1 is `rsp/cp0.cpp`'s round-10 fake
  (`fake_n=0` in the r14 run, so it is not proven to be the one) -- re-check per run.

**The poisoning event, captured with full state (new one-shot diag `wd_badN.txt`, written before
the copy, `.fzxwork/r15/`).** `BADRD` fires on the first RSP-side DMA whose RDRAM source is below
1 MB. Three fired in the r15 run, all with `cmd_start=cmd_end=0x002E3D18` (= the RDP output
cursor, i.e. the same range as round 14's bogus `k0 = 0x2E03C0`):
* `bad1 pc=0D04 mem=0x1584 dram=0x18B0 rdlen=0x1F` -- a 0x20-byte read from low RDRAM into
  **IMEM 0x1584** (regs: `at=t5=0x7586`, `v0=0x18B1`, `s7=0x0FB0`; the raw 0x7586 is masked by the
  hardware's 0x1FFF to 0x1586). The audio ucode loading a "segment" from address 0x18B0.
* `bad3 pc=0x14C0 mem=0x0FB0 dram=0x00000000 rdlen=0xFFFFFFFF` -- regs `at=s7=0x0FB0` (the task
  header pointer), `v0=0`, **`v1=0xFFFFFFFF`**. That is a **4096-byte DMA from RDRAM 0 into RSP
  address 0xFB0**, which runs 0xFB0->0x1FFF: **it overwrites the DMEM task header (0xFB0-0xFFF)
  AND all of IMEM (0x1000-0x1FB0) with RDRAM 0's content.** `v1 = size-1` with `size = 0` is the
  classic underflow, and `dram = 0` with `size = 0` says the ucode was handed a **zero-filled
  descriptor** (null pointer, zero length).
* `bad2` (false positive, low-RDRAM filter) is a legit 0x10-byte read from the game's audio data
  (0x42128) into DMEM 0xFE0 (the header region) -- i.e. the audio ucode *does* DMA into the header
  area on purpose, which is why one bad descriptor there is so destructive.
End state of the r15 run (`stall_90s.txt`): `imem_bad=1 word=00010001 ... pc=04001b20
status=000000c1`, `raise_bits ... DP=0`, `RDPKICK n=6 last start=002e3d18 end=002e3d18` (an
*empty* range -> parallel-RDP never sees SYNC_FULL -> no DP event -> `sGameThread` parked on
D_800DCAC8 forever), `FRAME loads t1gfx=2 t2aud=266 t3=0` (only 2 gfx tasks in the whole run),
`sp_status=000000c0/00000040`, and the audio ucode's own last write `sw_last=00004000
sw_after=000002c0` (SET_TASKDONE landed, SIG0 still set).

**Superseded from round 14**: (a) the resumed task does *not* start from a "zero DL pointer" --
the FRESH/WARM paths take k0 from the header's `data_ptr` and only the RESUME path takes it from
DMEM 0xBF8, so a bogus k0 means the *header* (or the saved word) was already wrong; (b) the
`0x00010001` stream is not "the game's framebuffer fill being parsed as a DL" by the *gfx* ucode
alone -- the same value enters RSP memory through *audio* ucode DMAs (bad1) and through a
zero-length underflow (bad3).

**ROUND 15 NEXT STEPS (in order):**
1. Find who hands the audio ucode a null/zero descriptor. The EK's audio is **disk-streamed**
   (`src/audio/disk/lib/` in the decomp, `AudioLoad_SetDmaHandler`/`SetLeoHandler`, and the game's
   ABI command buffers live at 0x80411910/0x804132D0 with segment-encoded words like
   `0x152E03C0`, `0x0D1703C0`), so the prime suspect is the 64DD audio read path delivering
   nothing/zeros. Instrument the game's LEO read results (buffer addr + first words) and the audio
   ucode's descriptor (the DMEM word it takes `v1` from) rather than guessing.
2. Test the DMA-clamp question: `rsp/cp0.cpp` disables upstream's clamp on the DD route ("real
   hardware WRAPS"). bad3 with the *clamp* would write 0x50 bytes (header only) instead of 0xFB0
   bytes (all of IMEM) -- decide with a targeted experiment whether the clamp or the wrap matches
   hardware for a transfer that crosses the DMEM/IMEM boundary, because it decides whether this
   class of bug is "one bad task" or "the whole machine dies".
3. Keep the round-10 fake SIG1 under suspicion: no ucode sets SIG1, so a resume (flags 0x05) can
   only come from that fake -- and a resume with an *unsaved* yield buffer is exactly what makes a
   ucode read a garbage saved state.

**ROUND 16 (goal round 16) — FIXED: the yield-resume now reloads the ucode's saved DMEM. The RDP
comes alive (6 -> 245 kicks, non-empty ranges, MI_INTR_DP raised, framebuffer swaps).**
Branch `dd-eos-watchdog-checkpoint`, commit `abd7e5192` over `921d439f7`.

**The boot ucode, decoded (this settles round 15's open questions).** The header's `ucode_boot`
(found in the live task struct: `0x807504F0`, size `0xD0`) is the blob right *before* the gfx text
at `0x807505C0`; disassembled from the RAM dump it is:

```
1000 j 0x1064                  1004 addi at,r0,0xFC0      # &OSTask
1008 lw  v0,16(at) # ucode     100c addi v1,r0,0xF7F      # ucode_size-1
1010 addi a3,r0,0x1080         1014/1018/101c mtc0 SP_MEM_ADDR/SP_DRAM_ADDR/SP_RD_LEN
1020..1028 wait SP_DMA_BUSY    102c jal 0x103c            1034 jr a3  # -> IMEM 0x1080
103c mfc0 t0,SP_STATUS         1040 andi t0,t0,0x80       # SIG0?
1044 bne -> 0x1050             1054 ori t0,r0,0x5200      # CLR_SIG0|SET_SIG1|SET_SIG2
1058 mtc0 t0,SP_STATUS          105c break                 # <-- THE YIELD ACK
1064 lw v0,4(at) # flags       1068 andi v0,v0,0x2        # OS_TASK_DP_WAIT
106c beq v0,r0,0x108C          1074..1084 jal 0x103c; mfc0 cp0_11 (DPC_STATUS); FREEZE? loop
108c lw v0,24(at) # ucode_data 1090 lw v1,28(at); 1094 addi v1,v1,-1
10a4 mtc0 r0,SP_MEM_ADDR       10a8/10ac mtc0 SP_DRAM_ADDR/SP_RD_LEN   # DATA -> DMEM 0
10bc jal 0x103c                10c4 j 0x1008
```
So (a) **the only place a task-side yield is acknowledged is `0x1054` (`0x5200`)** -- the task
ucode itself never writes SP_SET_SIG1, so the round-10 host fake was the only other SIG1 source;
(b) the DP_WAIT bit only selects whether the *DP-idle wait loop* runs -- the `ucode_data -> DMEM 0`
DMA at 0x108C happens either way; (c) libultra's `osSpTaskLoad()` maps `ucode_data =
yield_data_ptr` for a YIELDED task, i.e. the resumed RSP is meant to be restarted from the DMEM
image the yielding ucode saved.

**The defect that was killing the machine.** `osSpTaskYielded()` sets `OS_TASK_YIELDED` and clears
`OS_TASK_DP_WAIT` on the game's gfx task (observed: `wd_hdr15 seq=201 flags=00000005`,
`bf4=00ae00b0` = a leftover EK *audio* command word, i.e. the intervening audio task had clobbered
DMEM), and the resumed gfx task then took `k0 = DMEM[0xBF8]` = **0** as its display-list pointer,
walked RDRAM 0 as GBI, and issued the length-underflow DMA (`SP_RD_LEN=0xFFFFFFFF` -> 4096 bytes
wrapping through IMEM) that erased the header and ALL of IMEM (round 15's `wd_bad3`).

**The fix (DD-gated, `abd7e5192`).** `parallel.cpp`: at a FRESH task start (`*SP_PC_REG & 0xfff ==
0`, which slice re-entries never have) whose header is exactly the yielded shape (`type <= 2`,
`flags & OS_TASK_YIELDED`, `!(flags & OS_TASK_DP_WAIT)`, sane yield ptr/size), copy the yield
buffer into `DMEM 0..0xBFF` -- the header at 0xFC0+ is untouched. `rsp/cp0.cpp`: the host's
synthetic yield (round-10 threshold path) now writes the ucode's own ack pattern
(`|SIG1|SIG2 & ~SIG0`) instead of OR-ing SIG1 and leaving SIG0 set. Both are gated on the DD route.
Diag: `wd_yld.txt` (`YLD ... stale_f0/stale_bf8 -> rest_f0/rest_bf8/rest_bfc`).

**Measured effect (live clean run, `.fzxwork/r16/live/`).** `wd_yld.txt` fires and lands the
ucode's real state: `rest_f0=0032dcd0 rest_bf8=0024e260 rest_bfc=00752ae0`; the live RAM dump
(`ram_live.bin`) confirms the yield buffer holds `[0xF0]=0x13340 [0xBF8]=0x1208
[0xBFC]=0x752AE0`. Versus round 15 at the same point: **RDP kicks 6 -> 245 and the last kick is a
non-empty range (`0x15108..0x15340` vs `0x2E3D18..0x2E3D18`), `raise_bits DP` 0 -> 1, gfx task
loads 2 -> 5, gfx-typed slice entries 1639, and the guest swaps framebuffers
(`viCurr.framep 801d9800 -> 80200000`)**. The guest's own task structs are intact
(`ram_live.bin`: `0x2BB0C0` type 1 flags 0x04 ucode_boot `0x807504F0`/0xD0 ucode `0x80752AE0`
data `0x8077A090`/0x800 stack `0x8032E8D0` out_buff `0x802D9CD0` out_buff_size `0x8032DCD0`
yield `0x8032DCD0`/0xC00; `0x2BB100` is the same shape with ucode `0x807505C0` and data_ptr
`0x80284990`/0x18 = the 24-byte sync DL).

**Watch out -- a self-inflicted crash that looks like the known JIT SIGILL.** Round 16's first run
crashed with SIGILL in anon JIT code ~6s in *because the restore read `RSP::rsp.RDRAM` as a byte
pointer at word indices*; it copied garbage DMEM and the ucode then DMA'd a garbage overlay into
IMEM. Use `RSP::cpu.get_state().rdram` (uint32_t*) for the word view of RDRAM.

**ROUND 16 NEXT STEPS (in order):**
1. **Why is the kick range at `0x15108`?** The ucode's RDP FIFO bounds are supposed to come from
   the DATA (`ucode_data -> DMEM 0`, whose +0x28/+0x2C are `0x2D9CD0`/`0x32DCD0`) and the header
   carries the same pair (`output_buff` 0xFE8 / `output_buff_size` 0xFEC = `0x802D9CD0` /
   `0x8032DCD0`), yet `ram_live.bin` shows `0x15108` (inside the 0x00010001 framebuffer fill) and
   `0x2D9CD0` zeroed. Trace the ucode's DPC writes (`mtc0 cp0_8/9`) with pc + DMEM[0x28]/[0x2C] and
   compare with the header fields: a wrong/stale FIFO base makes every flush land in garbage, the
   RDP never reaches SYNC_FULL, and the frame protocol starves (`raise_bits DP=1` in ~2 minutes vs
   VI=3348) -- which is exactly the "loading bar does not advance" symptom.
2. The remaining low-RDRAM walk: `wd_bad1 pc=0fc4 src=000000 dst=09b0 len=0648`,
   `bad2/bad3 pc=0e1c src=000000 dst=0410 len=0098` -- the DMA into DMEM 0x9B0 ends at 0xFF8 and
   therefore **clobbers the task header in DMEM** (which is what `wd_hdr15 seq=226 ff0=fff9fff9`
   sees). Find which task starts with a null data pointer now that the resume path is sound.
3. Keep verifying the DD gate: rerun the CI interpreter baseline (`emumode=1`) after any RSP change.

**ROUND 14 (goal round 14) — the frame stall decoded: the gfx ucode runs a
YIELD/RESUME handshake the DD route fakes, and the resumed task parses low RDRAM.**
Branch `dd-eos-watchdog-checkpoint`, base `0fec82230` (instrumentation) over `baea19d80`.

**How this was measured.** Round 13 left "why is the gfx task stuck?" open; the answer needed
the ucode's own transfer stream. New DD-gated instrumentation (commit `0fec82230`): every
ucode-issued DMA (pc, dst/src/len/count/skip, first source word, SP_STATUS, the DMEM 0xFC0
word) in a 256-entry RAM ring flushed 64 at a time to `wd_dmatr.txt`, plus nonzero-word counts
for IMEM/DMEM on each DoRspCycles entry (`nzi`/`nzd` in `wd_rsp.txt`). Round-14 run:
`ram_40s.bin`, `ram_85s.bin`, `stall_40s/85s.txt`, `wd_dmatr.txt`, `wd_ucode_inv*.bin` in
`.fzxwork/r14/`.

**The gfx task's real shape** (from the DMA trace, addresses = this build):
`RD DMEM0 <- 0x80779860 len 0x800` (ucode DATA) -> `RD IMEM0x1080 <- 0x807505C0 len 0xF80` (TEXT,
linked at 0x1000) -> `RD IMEM0x1000 <- 0x807515D8 len 0x170` and `<- 0x80751540 len 0x98`
(**OVERLAYS**: the ucode copies pieces of its own blob into IMEM 0x1000, which is what the
`j 0x1008/0x1020/0x1040` targets in its text are) -> `RD DMEM0x920 <- 0x284990 len 0xA8`
(**display list**, = `G_MOVEWORD/G_DPFULLSYNC/G_ENDDL`, 24 bytes, and the header's +0x30 field)
-> `WR RDRAM0x2D9CD0 <- DMEM0xBA8 len 8` (**the RDP command flush**; +0x28/+0x2C are the RDP
FIFO bounds 0x2D9CD0/0x32DCD0, +0x38/+0x3C the yield data 0x32DCD0/0xC00).
**The first gfx task does all of this correctly** — round 13 had 1 RDP kick in 95s; round 14
measures **69 kicks, 3 gfx-task loads** (`FRAME loads t1gfx=3 t2aud=209`).

**The regression after the first task.** From `wd_ucode_inv*.bin` (DMEM snapshots): after a
resume the ucode streams **low RDRAM** into DMEM — `RD dst=0x09B0 src=0x00000000 len=0x210`,
then src advancing with a GROWING length (528, 536, 544 ... 1024) — i.e. it is parsing
RDRAM from address 0 as a display list: first the exception vector (`s0=3c1a8074`), then the
game's `0x00010001` fill. Every DMEM structure it needs is destroyed by that stream
(0x2E0/0x36E/0x410/0x920/0xBA8/0xFC0 all end up `00010001`), the ucode's own DPC end pointer
becomes `0xC118`, and the RDP kicks turn into empty/garbage ranges — so parallel-RDP never
processes a SYNC_FULL and **MI_INTR_DP is never raised**. Guest side (decoded from
`ram_85s.bin`): the gfx thread (`sGameThread` 0x80799B80, prio 10) waits on the 0x2A message
from `D_800DCAC8`, which `sys_main.c:396` sends ONLY on `EVENT_MESG_DP`; `sResetThread` waits
on PRENMI (normal, 0x1B `gResetMesgQueue`); the event queue is empty; VI runs at 60/s and is
acked; the machine is otherwise idle. `RDPKICK n=69 last start=002d9cd8 end=0000c118`,
`mi_rd_dp=1 dp_ack=3`.

**Conclusion (next round's target):** the ucode's *yield/resume* handshake is what the DD route
fakes. `osSpTaskYield()` sets SIG0; the ucode's dispatch loop polls SP_STATUS and, on SIG0,
jumps to its own save-state path (0x80750F2C region) which writes the yield data at +0x38 and
answers SIG1. The DD route instead **injects** `HALT|INTR_BREAK` on an SP_STATUS poll
threshold (`rsp/cp0.cpp` RSP_MFC0) and **fabricates SIG1** (round-10 block), so the guest
believes the task yielded and later calls `Sched_SpTaskResumeGfx()` — while the ucode never
saved anything. The resumed task therefore starts from a zero/garbage DL pointer. Fix = let
the ucode run its own yield protocol and stop synthesising the status bits (the poll-threshold
injection measured `fake_n=0` in this run, so it is not even firing; the SIG1 fake is).

**Two side findings worth keeping.** (1) The `0x00010001` fill in RDRAM 0x400-0x25800 is the
GAME's own boot code: `sys_main.c:259` fills all three framebuffers
(`*var_v1-- = 0x0001000100010001`) and this build's third entry in `gFrameBuffers`
(0x8079A330) is **0x80000400** (`= 0x801D9800, 0x80200000, 0x80000400`), i.e. its pixels land
at physical 0x400..0x25B00 — the fill's measured extent is exactly 0x400-0x25800. It is
static, present at 40s and 85s, and predates the frame stall; the same 0x80000400 is round 11's
"cart-boot handoff" address, so the *content* of that area (DD IPL3 on hardware) is still the
thing this game expects to find there. (2) Instrumentation on the RSP
hot path is not free: with per-SP_STATUS-write file I/O the emulation process died with
`SIGILL` in the RSP JIT (`pc` in an anon .bss mapping, pid 28133 `:EmulationProcess`) about 4s
in, at the gfx task; the same tree with RAM-only rings ran to 85s. Keep the RSP path file-I/O
free.

**ROUND 14 NEXT STEPS (in order):**
1. Make the yield real: remove the fabricated SIG1/HALT injection, let the ucode see SIG0 and
   run its save-state path, and give it the slices it needs to reach that point (the DL loop
   polls SP_STATUS once per command, so a short slice suffices).
2. Verify with `wd_dmatr.txt`: the resumed task must read its DL from 0x284990 (not RDRAM 0),
   and the RDP flush must write 8 real bytes (not `00010001`) so parallel-RDP raises DP.
3. If the synthetic-yield removal regresses the audio path (193 clean audio tasks today),
   compare against ares's model (RSP/CPU interleaved per instruction) — the user has already
   authorised porting `phobos/ares/n64/{rsp,dd,mi}` wholesale.

**ROUND 12 (goal round 12) — the fault decoded end-to-end: the RSP is fed its OWN corrupted memory
(the "`0x00010001` fill" is audio-ucode data, not a fill), and round 11's death spiral is one extra
step — a ucode-issued DMA that writes RSP memory over the exception vectors.** Branch
`dd-eos-watchdog-checkpoint`, base `cc977373e`.

**Method corrections (both change how every earlier dump must be read):**
* **RDRAM dumps are host-word-order: read them LITTLE-ENDIAN.** `wd_full_dump` does
  `fwrite(g_mem_base, 1, 0x800000, f)`; the guest word is the LE read of those bytes (verified against
  the guest's own `a2=0x807504f0`, and against `freeze_state.py`, which decodes thread/queue state
  correctly with `<I`). Round 11 read them big-endian, which byte-reverses every word (its
  "`0x8000041c`" is really `0x80000418`, its `0x00010001`/`0xffffffff` readouts are byte-swapped).
* **The N64 exception vector at `0x80000180` is INTACT in every dump except round 11's**: population A
  runs (FZX cart game, `__osException=0x800bc4c0`) and population B runs (`__osException=0x80746800`)
  both hold `3c1a8074 275a6800 03400008 00000000` (or the `800c/c4c0` variant) = `lui k0; addiu k0;
  jr k0; nop`. **In the round-11 dump `0x80000180` is ALL ZEROS** and `0x80000000-0x80000400` holds
  non-code data — that is the terminal fault, not a missing IPL3.
* **What is there: a copy of RSP memory.** `RDRAM[0x80000000 + k] == SPMEM[0xBA8 + k]` for the whole
  low 64 KB (419/512 words match over the first 2 KB with an exact contiguous 520-byte run) and the
  walk is **linear over the full 8 KB** (`(0xBA8+k) & 0x1FFF`, i.e. it runs off DMEM into IMEM).
  `& 0x1FFC`-style linear wrap is the **plugin's** `rsp_dma_write` (cp0.cpp); the core's `do_sp_dma`
  wraps inside a 4 KB bank with `& 0xfff`. So the damaging transfer is **ucode-issued**, and the
  "IPL3/cart-boot handoff" reading of round 11 is retired: the CPU died because an interrupt found a
  zeroed vector, flowed through the zeroed page into data at `0x80000408` and faulted
  (`c_exc_int` frozen at 12680 while `c_exc_nested` climbed 12.3 M / 300 ms).
* **The core-side SP DMA path is clean for the whole run**: `wd_dma2.txt` (DD route) has exactly four
  transfer shapes — header `← tmp_task 0x7c1c00` (`dst=0xfc0`), IMEM `← 0x768e60` ×193, IMEM
  `← 0x7504f0` ×1 — all `dir=1`, and **no transfer with `src=00010001`**.
* **`0x00010001` is not a fill pattern — it is content of the audio ucode's data tables.**
  `RDRAM 0x80768e60 + 0xf90/+0x10c8/+0x11e0/+0x12f4` hold 308/264/252/244-word runs of it, and the
  surviving live-IMEM words (`319d0043 00010001×4 6319ffff`) match **RDRAM `0x8076b268` exactly**
  (a unique 6-word match). Round 10's "a cleared RDRAM buffer was copied into RSP memory" is
  therefore refined to "audio-ucode data was copied into RSP memory".

**New instrumentation (DD-gated via `rsp_ares_budget_enabled()`, i.e. runtime `IsDDPresent()`), and
what it caught in the round-12 run (`wd_pdma.txt`):** a 128-entry RAM ring of every ucode-issued
transfer in the plugin's `rsp_dma_read`/`rsp_dma_write`, dumped (max 4 times) when a read's RDRAM
source starts with `0x00010001` or a write lands below RDRAM `0x1000`. Trigger:

```
R12DMA dump=1 idx=7317 trig=RD dst=00001000 src=00000f80 len=152 cnt=0 skip=0 s0=00010001 s1=00010001
  ... (preceding ring entries = the normal F3DEX task load)
  RD dst=00000000 src=00779860 len=2048     <- ucode_data  -> DMEM 0
  RD dst=00001080 src=007505c0 len=3968     <- main ucode  -> IMEM 0x80
  RD dst=00001000 src=00000f80 len=152      <- THE BAD ONE  (sources = the 0x00010001 tables)
  live pc=0d14 status=00000040 imem0=09000419 20010fc0 dmem0=00000000 fc0=00000001
```
The ring's two preceding transfers are exactly what the boot ucode at `0x807504f0` is written to do —
it disassembles cleanly as `addi at,zero,0xFC0; lw v0,0x10(at) [header ucode]; addi v1,zero,0xF7F;
addi a3,zero,0x1080; mtc0 a3,SP_MEM_ADDR; mtc0 v0,SP_DRAM_ADDR; mtc0 v1,SP_RD_LEN` then the
`flags&2`-gated `ucode_data` load to DMEM 0. So the *third* transfer is not in the boot ucode: some
later step DMAs **from RDRAM `0xF80`** (note: `0xF80` is the *length* `0xF7F+1`, and `0x1080/0x1000`
are the IMEM addresses of the same load) into IMEM 0 with length 152 — i.e. an address register that
holds a size/offset where an address belongs, and the source it reads is garbage-audio-ucode data.

**Round-12 run's terminal state (screenshot + `wd_stall.txt` + fresh RDRAM dump):** *different* from
round 11's death spiral — the machine is ALIVE and only the RSP is off the rails:
`cause=10000000` (a plain interrupt), `c_cop1=2`, `c_exc_nested=0`, `c_exc_int=44232`, `c_task=7766`,
`c_vi_evt=14447 / c_vi_ack=14449`, `sp_status=0x000000c0`, `sp_pc=0x04001098`, guest idle at
`epc=0x806f32ec`, exception vector at `0x80000180` INTACT, but the round-11 latches fire:
`imem_bad=1 word=00010001 … pc=04001c34 status=00000040 count=0b6145ea dmas=388`. So the RSP-memory
corruption happens **without** the vector clobber; round 11's fatal step is the extra wild *write*.
**Caveat (do not overclaim):** the ring dump does file I/O inside the RSP DMA path and the ares path
is gated by a *host-time* budget, so this run's softer terminal state cannot be assumed inert. Round
11's stash+rebuild control (bit-identical counters) covered the round-11 diagnostics only.
* **The EK data load is real:** `RDRAM 0x8076a000..0x8076c000` holds packed EK data (runs of
  `0x00010001` interleaved with `0x22894553/0x45134513/0x39e131e1`-style words) in the r11/r12 runs and
  is **all zeros** in the round-10 stalled run — the DD read path is delivering data.
* **Plain-game regression: CLEAN** — `Mario Kart 64 - Amped Up [v3.21]` (the CI emumode=1 path) renders
  its "ARE YOU PLAYING ON A REAL N64 CONSOLE?" screen at 60 FPS with the instrumented build, the app
  process stays alive, and the DD-gated instrumentation stays silent (no `wd_pdma.txt` / `wd_stall.txt`
  written by that run — mtimes unchanged, only the known harmless 22-byte `wd_smc.txt`).

**ROUND 13 NEXT STEPS (in order):**
1. **Latch the DMA-register writes** (`RSP_MTC0` for `CP0_REGISTER_DMA_DRAM/DMA_CACHE/
   DMA_READ_LENGTH/DMA_WRITE_LENGTH`) with the RSP pc + a few registers, so the instruction that
   computes `src=0xF80 / dst=0x1000 / len=152` is identified. Suspects: (a) a display-list-supplied
   `G_LOAD_UCODE`-style pointer inside the freshly loaded EK data, or (b) the plugin's register echo
   (`rsp_dma_read` writes back `DMA_DRAM += length + skip`, and `SP_DMA_FULL`/`SP_DMA_BUSY` are
   approximated) handing the ucode a size where an address belongs.
2. **Check the gfx ucode's tail.** In RDRAM the main gfx ucode (`0x807505c0`, size `0x1000` per the
   task header) has code up to ~`+0xC00` and **zeros after** (`0x807511c0` = 0, so IMEM `0xC00..0xFFF`
   is NOPs — and the RSP's pc sits at `0xC34/0xD14` exactly when things go wrong). If the real F3DEX2
   build is longer, then the load length/source is wrong; if not, the ucode is *supposed* to run off
   its own tail into IMEM 0 and the fault is upstream (the bad `src=0xF80`).
3. **Cheap defensive fix worth testing (DD-gated):** in the plugin's `rsp_dma_write`, refuse (and log)
   writes whose RDRAM destination is `< 0x1000` — RDRAM's first page is the exception-vector area and
   is never a legitimate DMA target. That converts round 11's fatal vector clobber into a survivable
   glitch and gives a clean signal in the log.
4. Keep the goal active: the game still does not reach the menu (it now gets further — EK data loaded,
   RSP tasks running, VI acking — and then the RSP is poisoned from a bad DMA source).

**ROUND 11 (goal round 11) — the post-load fault is NOT the RSP: the guest re-enters `0x80000400`
where the cart's IPL3 boot code should be, and it is not there.** Branch
`dd-eos-watchdog-checkpoint`, base `9361c3f85`.

* **The instrumentation is proven INERT.** Control experiment: `git stash` the round-11 diagnostics,
  rebuild, re-run → the watchdog counters come back **bit-identical** (`count=0b601da6`,
  `c_cop1=192583378`, `c_task=196`, `c_asic=10604`). So neither the new code nor its file I/O moves
  the emulation; the run is deterministic. (The first version did put `fopen/fprintf/fclose` in
  `write_rsp_mem` — a hot path the user explicitly warned about — and was rewritten to in-memory
  latches before that control was run; the latches cost two word-compares.)
* **Current deterministic failure (3/3 runs):** the loading bar reaches COMPLETE (screenshot at
  60-90 s) and the guest then executes **`0x8000040c`**, taking a **192 M COP1-unusable exception
  loop** (`cause=9000002c epc=80000408 sr=0000ff03` — EXL set, i.e. inside a handler) until the
  `:EmulationProcess` **dies** (`adb logcat -d -v time | grep EmulationProcess`) and the app falls
  back to its gallery. This is a *different* terminal state from round 10's (idle thread at
  `0x806f32ec`, machine alive).
* **`0x80000400` is the IPL3 load/entry address** (the PIF copies the cart's IPL3 — ROM+0x40, 4032 B —
  there and jumps to it). Neither program declares it: FZX-J cart entry `0x80267000`, DD IPL ROM
  (`ipl.n64`, "64DD IPL (JPN)") entry `0x80260000`. So the guest is doing a **cart-boot handoff**
  and finding garbage.
* **What is actually at `0x80000400`:** in round 10's dump it was **`0x00010001` fill** for 64 KB
  (`0x80000400..0x8001023C`); in the current run it holds **the FZX game's own OSTask buffer**
  (`type=1, flags=4, ucode=0x807504f0/0xd0, ucode_data=0x7505c0/0x1000` at `0x8000041c`) plus a copy
  of the F3DEX2 boot ucode at `0x80000458`. **Nothing ever put IPL3 back.**
* **The RSP is HEALTHY in the current failure** — live `SPMEM` from the stall dump:
  `DMEM[0xFC0..] = 00000001 00000004 807504f0 000000d0` (a valid gfx OSTask header) and
  `IMEM[0..3] = 02f65822 256c0157 05910094` (the main ucode text). Round 10's `IMEM=0x00010001`
  belonged to that run's different state, **not** to a corrupt DMA.
* **SP-DMA provenance is fully accounted for** (new `wd_dma2.txt`): header DMAs **194/194 from
  libultra's `tmp_task` (0x7c1c00)**, IMEM DMAs **193× 0x768e60 (audio) + 1× 0x7504f0 (gfx boot)** —
  and those source regions are **byte-identical and intact** in both dumps. No DMA ever wrote the
  fill pattern into RSP memory, and the new CPU-write latches show no such write either (only DMEM
  `0x0FD` and `0x7A7-0x7C3`, values `0` / `0xA400xxxx` / `0xFFFFFFFF`).
* **COP1 register snapshot** (round-9 in-memory capture, now printed by the stall dump) dates the bad
  jump: `COP1SNAP3 pc=8000040c epc=80000408 ds=1` with `a0=0x125` (SP_RD_LEN), `a1=0x04001000`
  (SP_MEM_ADDR = IMEM bank), `a2=0x807504f0`, `a3=0xd0`, `s0-s3/k0/k1 = 0xa4040000` (SP regs),
  `ra=0x8074651c` — i.e. **the guest was inside `__osSpTaskLoad` for a GFX task** when control
  landed on `0x80000400`.
* **Plain-game regression: CLEAN** with the instrumented tree (`Mario Kart 64 Amped Up` renders at
  ~59 FPS; `files/` holds only the harmless 22-byte `wd_smc.txt`, no `wd_stall`/`wd_freeze`/`wd_crash`).

**ROUND 12 NEXT STEPS (in order):**
1. Instrument the **PI/cart-ROM DMA path on the DD route** (source, dest, length + first words) and
   check whether the IPL3 copy (`cart ROM +0x40` → `0x80000400`, 4032 B) runs at all in the
   cart-boot handoff, and what it delivers. The first boot goes through the core's ROM/IPL3 path;
   the DD re-boot is a plain PI DMA + jump, which nothing in this tree has ever verified.
2. Log `c_pi`/PI-DMA destinations around the transition (the current run's `c_pi=11504` vs round
   10's `13547`), plus `write_ri_regs`/PI source addresses, to catch a wrong ROM offset or a
   KSEG-vs-physical mismatch on the DD route (note `do_sp_dma`'s DD-only `& 0x7fffff` source mask).
3. Re-check watchdog survival: the EmulationProcess dies seconds-to-minutes in; add a small
   "session alive" line to `wd_init.txt`/a heartbeat file so a dying run is distinguishable from a
   stalled one without pulling screenshots.

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

## UPDATE 2026-09-10 (goal round 9) — **CPU now runs at full hardware speed; the stall is no longer "the RSP owns the thread"**

Commit `e205bfc7a`. Two coupled bugs in the 64DD host-run-budget model, both
found from live RP6 measurements, both DD-gated.

### 1. The sticky RSP-side `irq` flag — a 524k fake-completion/s livelock
`rsp/cp0.cpp:171` returns `MODE_CHECK_FLAGS` from **any** `mtc0 SP_STATUS` while
`(cp0.irq & 1) || (status & HALT)`. `parallel.cpp`'s DD branch treated *any*
`MODE_CHECK_FLAGS` as a task yield, and the yield path did
`*cp0.irq |= 1` and then `break` — skipping the `CheckInterrupts()` call that is
the only thing that consumes that flag (the guest's `SP_CLR_INTR` write clears
`MI_INTR_SP` in the core, not the plugin's flag). So from the first yield on,
**every** `cpu.run()` returned immediately and the RSP never executed another
instruction.

Live proof, `files/wd_rsp.txt`: 13406 "task completions" inside **one
millisecond**, every one `ttype=1048576` with a garbage OSTask
(`boot=00100010 ucode=7ffffffc ucosz=2fd0f8f0`), driving `c_exc_int` at 524k/s.
A second, quieter consequence: the guest's libultra SP handler ran 500x/s
(`wd_spw.txt`: 414 writes of `SP_STATUS=0x8008` at `pc=80746afc`, the MI
dispatch in `__osException`), and each ack re-entered `do_SP_Task`.

Fix: only a genuine `rsp_budget_expired_now()` is a yield, and the irq flag is
cleared both on a false alarm (`continue`) and on the yield.

### 2. A budget yield must not fabricate a task completion
The yield set `HALT|INTR_BREAK`, which `do_SP_Task` reports as a finished task;
the guest acks, and `update_sp_status` re-enters `do_SP_Task` for a locked,
un-halted task — handing the RSP another full 2 ms slice. **The guest's entire
CPU share was its own SP handler while the RSP still owned the emulation
thread.** Measured with `files/wd_stall.txt`:

| | before (round 8 model) | after |
|---|---|---|
| CP0 COUNT advance | 40 184 / 300 ms (134 k/s = **0.3 %**) | 14 588 142 / 300 ms (**48.6 M/s, ~real speed**) |
| VI interrupts | **0** | 18 / 300 ms (60/s) |
| AI interrupts | 0 | 18 / 300 ms |
| `c_task` | 500/s | 30/s (background slices only) |
| guest exceptions | 150 (100 % SP handler) | 40 (AI/VI — real work) |
| guest run queue | **EMPTY**, run thread = prio-0 idle spinning `b .` at 0x806f32ec | prio-20 thread RUNNABLE, executing the frame sync |

A yield is now **silent**: `SP_STATUS` is left exactly as the ucode left it
(unfinished, resumable), `do_SP_Task` marks the task `rsp_task_locked` **without**
raising `MI_INTR_SP` on the DD route, and a new `rsp_dd_background_pump()`
(`rsp_core.c`, called from `dynarec_gen_interrupt` — the recompiler's
`cc_interrupt` hook — with a coarse 3 ms time gate) feeds the RSP bounded slices
from the CPU side. `do_SP_Task` is used for the slice so a slice that finally
reaches the ucode's `BREAK` delivers the stock completion
(`SP_INT` → `rsp_interrupt_event` → `MI_INTR_SP`), which is what wakes the guest.

### 3. New instruments (all DD-gated, inert on plain carts)
* `wd_ht_ring` — a dispatch ring written **only** by `get_addr_ht`. `wd_pc_ring`
  is polluted by `dynarec_sample_hook`, so 2048 identical `0x80000180` entries
  in round 8 could not distinguish "the guest is executing the exception
  vector" from "the dispatcher keeps re-entering the vector block without it
  ever running". `DELTA3 c_ht=/c_cop1=` are in `wd_stall.txt`.
* `cop1_unusable()`: fault counter + last-16 pc ring + a 4-deep full-GPR
  snapshot (`COP1SNAP`). Total COP1 faults on a whole run: **2**.
* `HTVEC`: the hash-table entry (`vaddr/start/length/addr`) for `0x80000180`
  and for the `0x80000400` fill block — exposes a stale/overlapping entry.
* A SIGILL/SIGSEGV/SIGBUS handler writing `files/wd_crash.txt` with the host pc
  /lr and the guest pcaddr/GPRs/CP0: a JIT SIGILL leaves no C backtrace.
  (Runs with the round-9 *probe* build did crash with `SIGILL` inside
  `anon:.bss` — the JIT arena — but the crash did not reproduce with the plain
  round-8/round-9 fix builds, so it is a probe-timing artefact until proven
  otherwise.)

### 4. Where the machine stands now (DD LOADING screen, 59-60 FPS, bar ~6.5/8)
The guest is **alive and executing**: the PC ring shows the EK's DD-loader
message loop (`0x806f2f6c`: `osRecvMesg(0x8079A120)` + dispatch on
`msg == 0x18` (SP event) / `0x1a`), the libultra exception handler, the VI
manager and `__osViSwapContext` (`0x8074cf24`), and the prio-20 thread in
`osViGetCurrentFramebuffer`-based frame sync.

Remaining blocker: **the DD loader never issues another DD ASIC command**
(`c_asic` frozen at 10604, `c_pi` +4/300 ms). `__osViCurr`/`__osViNext` both
hold `framep=0x801d9800` with `state=0x0001` — that is the post-swap invariant
of `__osViSwapContext` (`__osViNext = __osViCurr; __osViCurr = vc;
*__osViNext = *__osViCurr;`), so the last completed swap was for
`gFrameBuffers[0]`; the frame-index variable the loader compares against needs
to be pinned down (`D_800DCD00` per the decomp vs `0x8079A360` per round 8 —
check both). Next round: trace which of the `sys_gfx.c:198` /
`sys_main.c:190/268/291/302` framebuffer waits the prio-20 thread is in, and
what the loader state machine is waiting on at `0x806f2f88+`.

### 5. Plain-cart regression — VERIFIED CLEAN
Mario Kart 64 Amped Up v3.21, emumode=1 baseline: renders the "A REAL N64
CONSOLE?" screen at **59 FPS**, no crash, and only the harmless 22-byte
`wd_smc.txt` header exists (`wd_stall/wd_dma/wd_freeze/wd_crash` all absent),
i.e. every DD path is inert.

### 6. Evidence
`.fzxwork/r9/`: `stall_head.txt` (round-8 build: SP-interrupt storm),
`stall_fix1.txt` (fix 1 only: 500 tasks/s, CPU 0.3 %), `stall_pump1.txt`
(fix 2: CPU 48.6 M/s, VI 60/s), `ram_pump1.bin` + `freeze_state.py` output
(guest OS thread/queue dump), `spw_fix1.txt`, `rsp_head.txt`, `bar1-3.png`,
`plain_r9.png`.

### Round-9 addendum — the EK's frame-sync sites, nailed down from `iplram_force.bin`
Scanning the forced RDRAM dump for `jal 0x80750400` (osViGetCurrentFramebuffer)
gives every framebuffer wait in the EK build:

| site | what it is |
|---|---|
| `0x806f2d34-0x806f2d60` | `osViSwapBuffer(gFrameBuffers[0]); while (osViGetCurrentFramebuffer() != gFrameBuffers[0]) {}; osViBlack(0)` |
| `0x806f2db8-0x806f2de8` | `osViSwapBuffer(gFrameBuffers[1]); wait fb1; func_806F33D0(fb0/fb1/fb2);` |
| `0x806f2e00-0x806f2e2c` | `osViSwapBuffer(gFrameBuffers[0]); wait fb0; osViBlack(0)` |
| `0x806f38a0-0x806f3904` | **`sys_gfx.c:198`**: `s1 = &0x8079A360` (the frame index), `s0 = 0x8079A330` (gFrameBuffers), `osViSwapBuffer(gFrameBuffers[*s1]); while (osViGetCurrentFramebuffer() != gFrameBuffers[*s1]) {}` |
| `0x80710b48` | the EK's fault handler (`Fault_SetFrameBuffer`) |

Two corrections to earlier rounds:
* the frame index really is **0x8079A360** (the decomp's `D_800DCD00` name maps
  elsewhere in this build);
* `0x0001000100010001` is **not** uninitialised RDRAM — it is
  `func_806F33D0` (`0x806f33d0`), the EK's own framebuffer clear
  (`*var_v1-- = 0x0001000100010001` in `sys_main.c`, `for (i=0;i<3;i++)`), which
  is exactly why `0x80000400` (gFrameBuffer3) is full of it.

At the round-9 stall: index `0x8079A360 = 1` (wants `0x80200000`) while
`__osViCurr->framep == __osViNext->framep == 0x801d9800`. Since
`__osViSwapContext` restores exactly that invariant
(`__osViNext = __osViCurr; __osViCurr = vc; *__osViNext = *__osViCurr;`), the
last *completed* swap was for `gFrameBuffers[0]` — i.e. something swapped back
to fb0 after the `osViSwapBuffer(gFrameBuffers[1])`, so the two waits fight.
The next step is to instrument `osViSwapBuffer`/`__osViSwapContext`
(DD-gated: log guest pc, the requested framep and the resulting
`__osViCurr->framep`) and see which thread wins.

---

## UPDATE 2026-09-10 (goal round 10) — **the RDP/DP interrupt now fires; the DD loading bar COMPLETES**

Commit: `217490766` (rsp+dd: deliver the RDP (DP) interrupt on the 64DD route).

### 1. The whole post-load stall was the EK's DP-interrupt frame protocol

The Expansion Kit's frame loop is (`src/sys/sys_gfx.c`):

```c
void func_80067D64(void) {
    osRecvMesg(&D_800DCAB0, &D_800DCD10, OS_MESG_BLOCK);   /* 0x29 token from VI */
    ... build display list ...  Gfx_FullSync();
    osRecvMesg(&D_800DCAC8, &D_800DCD10, OS_MESG_BLOCK);   /* waits for 0x2A  */
    while (osDpGetStatus() & (DMA|CMD|PIPE|TMEM_BUSY)) {}
    osViSwapBuffer(gFrameBuffers[D_800DCD00]);
    while (osViGetCurrentFramebuffer() != gFrameBuffers[D_800DCD00]) {}
    Gfx_SetTask(sGfxTask);                                  /* next gfx task  */
}
```

and `D_800DCAC8` is written **only** by `Main_ThreadEntry`'s
`EVENT_MESG_DP` case — i.e. only by a real `MI_INTR_DP`. Two independent
pieces of evidence pinned the hang to that interrupt:

* `wd_stall.txt`: `raise_bits ... DP=0` for a **whole run** (SP=193, VI=2945,
  PI=12220 all fired);
* the guest's message variable `D_800DCD10` (`0x8079A370`) still held **0x29**
  — the last `D_800DCAB0` handoff — so the gfx thread had never completed the
  `D_800DCAC8` receive.

Where `MI_INTR_DP` comes from: `parallel_rdp` raises it itself
(`mupen64plus-video-parallel/upstream/parallel_imp.cpp`:
`*gfx.MI_INTR_REG |= DP_INTERRUPT` when it reaches the guest's `gDPFullSync`),
and it is reached from the RSP's `mtc0 CMD_END` → `ProcessRdpList()` —
`PARALLEL_INTEGRATION` **is** defined for this build
(`upstream/CMakeLists.txt:11`), so that path is live.  The core then has to
turn the bit into a CP0 event; it only did so inside one branch.

### 2. The three DD-gated defects fixed

1. **DP consumption was unreachable for a sliced task.**  The stock
   consumption in `do_SP_Task` sits INSIDE `if (sp->mem[0xfc0/4] == 1)`, and
   that branch is chosen from `DMEM[0xFC0]` at **entry** — but F3DEX clobbers
   the task header as soon as it runs.  Harmless on the plain route (one
   `DoRspCycles` call runs the task to completion, so the branch that entered
   is the one that sees the kick); fatal on the 64DD route, where the task is
   executed in bounded slices fed by `rsp_dd_background_pump()`: the slice that
   finally reaches `DPC_END` reads a clobbered `0xFC0` and takes the
   audio/other branch, leaving `MI_INTR_DP` set and never turned into a
   `add_interrupt_event(DP_INT)` event.  → consume it after the branch
   dispatch (no-op when the gfx branch already handled it).

2. **A forced handover never acknowledged a pending libultra YIELD.**
   `osSpTaskYield()` asks with SIG0 (`SP_STATUS_YIELD`) and
   `osSpTaskYielded()` reports `OS_TASK_YIELDED` only when the ucode answers
   with SIG1 (`SP_STATUS_YIELDED`).  The DD force-yield in
   `rsp_status_read` (cp0.cpp) set `INTR_BREAK|HALT` + irq but never SIG1, so
   `osSpTaskYielded()` returned 0, the game never set `sGfxTaskYielded`, never
   called `Sched_SpTaskResumeGfx()`, and the interrupted GFX task was
   **abandoned** (measured in round 8: 193 audio task entries vs 28 gfx).
   → set SIG1 when SIG0 is pending, as real F3DEX2 does at its yield point.

3. **Defensive: re-publish the RSP memory base when a session MOVED it.**
   The RSP plugin is handed host pointers once (`RSP_INFO` at
   `plugin_start_rsp`) and keeps them for the process lifetime; the register
   pointers stay valid (they point into `g_dev`, a static global) but the
   memory base comes from `init_mem_base()` in `CoreStartup` and is
   per-session.  A second session in the same process would leave the plugin
   on the previous session's buffer — silently (`SP_STATUS` reads live, DMEM
   0xFC0 reads 0, IMEM reads all-zero).  New
   `plugin_refresh_rsp_memory_if_moved()` (called after `init_device`,
   self-gating: it acts only when the base moved, so plain carts take the
   early return).  **A pointer probe proved it does NOT fire on the current
   64DD flow** — `pimem=0x7930581000 pdmem=0x7930580000 pram=0x792c580000`
   matched the `plugin_start_rsp` log exactly and `cur_dmem == published`, i.e.
   the plugin's memory was correct all along and the "all-zero IMEM" seen in
   the RSPTASK log was simply the **background pump re-entering `do_SP_Task`
   with no task loaded** (`ttype=0`, empty IMEM).  Kept as a safety net.

### 3. Measured effect (FZX+EK, emumode=2, parallel-RDP Vulkan, RP6)

| | round 9 | round 10 |
|---|---|---|
| `raise_bits DP` | **0** | **≥1 (first DP interrupt ever)** |
| DD LOADING bar | 6.5/8 | **complete, then cleared** |
| guest `viCurr/viNext.framep` | `801d9800` (fb[0]) | `80200000` (**fb[1]**) |
| SP DMAs | 42 | 264 |
| task header loads | 21 | 132 |
| SP interrupts | 193 | 210 |
| gfx task entries | 0–28 | 7 (with valid F3DEX IMEM) |
| audio task entries | 193 | 208 |

The bar completing and clearing is the visible milestone of this round:
the DD LOADING screen finished.

### 4. Remaining blocker — only ONE DP interrupt per run

The EK's frame protocol still stalls after a single frame: the gfx thread is
back in `osRecvMesg(&D_800DCAC8)` (its `D_800DCD10` is still `0x29`, and
`__osViNext->framep` is untouched at fb[1], so it never reached the
`osViSwapBuffer` wait).  The run queue is EMPTY with the prio-0 idle thread at
`0x806f32ec` (`b .`, `sys_main+0xa8c` = `Idle_ThreadEntry` after
`osSetThreadPri(NULL, OS_PRIORITY_IDLE)`); Main_ThreadEntry is prio 99 and
waiting normally on `gMainThreadMesgQueue` (`0x8079A120`), VI still 60/s.

Bootstrap analysis: a gfx task is started only by
`Sched_SpTaskClearStartGfx()` (from `EVENT_MESG_GFX_TASK_SET`, which
`Gfx_SetTask` sends) or by `Sched_SpTaskResumeGfx()` (from `sGfxTaskYielded`,
set only by a successful `osSpTaskYielded()`).  `Gfx_SetTask` is the LAST line
of `func_80067D64`, past the DP wait — so the loop is closed by the DP
interrupt, which is why one DP bootstrap ran and then stopped.

**Prime suspect for the next round:** `Sched_SpTaskResumeGfx()` calls
`osSpTaskStart(gCurGfxTask)` = `osSpTaskLoad` + `osSpTaskStartGo`, i.e. the gfx
task **restarts**, and it only continues rather than restarts if the ucode
actually saved its yield state through the `yield_data` protocol.  Our
force-yield at the SP_STATUS poll is a *fabricated* handover — the ucode never
runs its own yield sequence — so each resume may restart the display list from
the beginning, and the task then never reaches `gDPFullSync` (only 1 of the 7
gfx entries produced a DP).  The DD-gated experiment to run: make that
force-yield **transparent** like the round-9 budget yield (exit `DoRspCycles`
without setting `INTR_BREAK|HALT` and without raising the irq, letting
`rsp_dd_background_pump()` resume the RSP exactly where it stopped and letting
the CPU service pending DMA/interrupts in between) and see whether DP reaches
~60/s and the frame loop starts turning.

Secondary instrument still worth adding: log `DPC_START/END/CURRENT` at the
RSP's `mtc0 CMD_END` and at `write_dpc_regs` — `parallel_imp.cpp` bails out of
`processRDPList` entirely when `DP_END > 0x7ffffff || DP_CURRENT > 0x7ffffff`
(physical-address expectation), which would also suppress every `SyncFull`.

### 5. Plain-cart regression — VERIFIED CLEAN (round 10)

Mario Kart 64 Amped Up v3.21, emumode=1 baseline: renders the "ARE YOU PLAYING
ON A REAL N64 CONSOLE?" screen at **60 FPS**, and the files dir contains only
the harmless 22-byte `wd_smc.txt` — no `wd_stall`/`wd_freeze`/`wd_crash`, i.e.
every DD path (including the new DP consumption and the SIG1 yield ack) stays
inert.  Evidence: `.fzxwork/r10/plain_mk64.png`.

### 6. Evidence (round 10)

`.fzxwork/r10/`: `run60.png` (bar present, near full), `now2.png`/`late1.png`
**(bar gone)**, `plain_mk64.png`, `stall_run1.txt` (DP=0 baseline),
`stall_run3.txt` (**DP=1**), `spw_run1.txt` (last write = `osSpTaskYield`
0x400), `spw_*.txt`, `rsp_run1.txt`, `rsp_run3.txt` (ttype histogram:
5267×0x10001, 208×2, 7×1), `ram_run3.bin` (guest thread/queue state),
`dma_run1.txt` (the 21 repeated audio loads).

## UPDATE 2026-09-10 (goal round 13) — **HARD DEADLOCK FIXED: the RSP was left un-HALTed, so the guest spun forever in `osSpTaskLoad`**

Commit `aefa63caa`.

### 1. What round 12 was actually looking at

Round 12's evidence was rerun and decoded **with the EK symbol table**
(`.fzxwork/fzerox-decomp/linker_scripts/jp/ek/symbol_addrs.txt` — verified to
match this build exactly: `__osRunQueue=0x80771E18`,
`gspF3DEX2_fifoTextStart=0x807505C0`, `AudioSeq_SequencePlayerProcessSequence=
0x8073FB28`, `sAudioThread/sGameThread/sMainThread/sIdleThread`, `LEOevent_que=
0x807C53B0`, `LEOcommand_que=0x807C5398`).  What that gives, from
`.fzxwork/r12/ram_r12.bin` + `wd_stall.txt`:

* `__osRunningThread` = **`sAudioThread`**; `sGameThread` (prio 10) blocked in
  `osRecvMesg(&D_800DCAC8)`; `sMainThread` (99) on `gMainThreadMesgQueue`
  (`0x8079A120`, the queue the SP/DP events post to); `sIdleThread` spinning at
  `0x806f32ec` = `Idle_ThreadEntry+0x134` (`b .`).
* The frame protocol is `sys_gfx.c func_80067D64()`:
  recv 0x29 (VI tick) -> build -> **recv 0x2A on `D_800DCAC8`** -> `osViSwapBuffer`
  -> spin on `osViGetCurrentFramebuffer()` -> `Gfx_SetTask()`.  The 0x2A is sent
  only by `sys_main.c:396`, on `EVENT_MESG_DP`, i.e. only from the guest's DP
  handler.  **Everything waits on `MI_INTR_DP`.**
* The hot block ring was audio (`AudioSeq_*`, `AudioSynth_*`, `AudioLoad_Dma`)
  plus OS queue code — i.e. a machine that is *running*, not a machine that is
  stuck, which is why round 12's "RSP poisoned" reading was incomplete.

### 2. The round-13 stall, decoded exactly

`wd_stall.txt` of the round-13 run (`.fzxwork/r13/`):

```
A epc=8074cd9c count=80000ffa  sp_status=00000040 sp_pc=040015c0
A c_vi_evt=291 c_vi_ack=291 ... DELTA everything = 0   (VI/AI/SP/PI all stop)
DELTA3 c_ht=5099515            (the CPU still executes 17M blocks/s)
RING: 2048/2048 blocks at 0x80746418
0x80746418 = osSpTaskLoad+0xbc = `beq $v0,-1,0x8074640c` of
    while (__osSpSetPc(SP_IMEM_START) == -1) {}
    s32 __osSpSetPc(u32 pc) { if (!(IO_READ(SP_STATUS_REG) & SP_STATUS_HALT)) return -1; }
```

**The guest deadlocked in libultra's task-load handshake because `SP_STATUS_HALT`
was clear.**  `do_SP_Task()` clears HALT|BROKE|TASKDONE unconditionally at exit
(stock mupen64plus), and the only thing that sets HALT back is
`rsp_interrupt_event()` — guarded by `if (!sp->rsp_task_locked)`.  The 64DD route
locks every *incomplete* task by design (host-budget yield in the plugin, ucode
yield), so HALT stayed clear after every preempted slice.  The frozen CP0 COUNT
is cause and effect at once: no event ever became due again, so
`rsp_dd_background_pump()` (called from the recompiler's interrupt hook) stopped
too — the RSP could never finish the task that would have set HALT.

### 3. The fix (both halves DD-gated; plain route bit-for-bit unchanged)

* `do_SP_Task()`: when `rsp_task_locked`, leave `SP_STATUS_HALT` **set** with
  TASKDONE/BROKE clear — "RSP stopped between slices, task resumable".
* `rsp_dd_background_pump()`: a suspended task (HALT set, locked, not BROKE) is
  resumable, so clear HALT for the slice; `do_SP_Task` re-sets it on the way out.

### 4. Verified

| | before (r13 pre-fix) | after (`fix_45s` -> `fix_95s`) |
|---|---|---|
| dynarec ring | 2048/2048 blocks in the spin | normal |
| VI events | 291, frozen | 2684 -> **5914** (60/s, real time) |
| CP0 COUNT | frozen at 0x80000ffa | advancing |
| guest framebuffer | 0x80200000, never moves | **0x801d9800** (fb[0]) |
| screen | middle element only | + bottom status band now drawn |
| plain cart | — | **clean** (MK64 renders, no stall dump, only 22-byte `wd_smc.txt`) |

### 5. Where it now parks (round 14 target)

`fix_95s.bin`: `__osRunningThread = sIdleThread` (running), run queue empty,
`sGameThread` still blocked on `D_800DCAC8`, and the new counters say:

```
loads gfx=1 aud=193   rdpkick=1   (kick_last DPC_START==DPC_END==0x0032DCD0)
dp_rd=0  dp_ack=2  dp_consumed=0  rb DP=0
viCurr.framep=801d9800  viNext.framep=801d9800  vievtq=0/5
```

So: only **one** gfx task is ever loaded and only one RSP-side RDP kick happens
(and that one has `DPC_START == DPC_END`, i.e. an empty command list — see
`parallel_imp.cpp`'s expectation that START/END are *physical* addresses), the
guest acks DP twice, but the guest never sees the DP bit in `MI_INTR` itself
(`dp_rd=0`), and the VI manager thread is parked on an empty `viEventQueue`
(`0x807C46C0`) so `viCurr.framep` never changes.  Next: instrument the DP event
-> guest handler -> `gMainThreadMesgQueue` -> 0x2A chain (and the DPC_START/END
values of *every* kick, not just the last), and check whether the gfx task is
being restarted rather than resumed (`Sched_SpTaskResumeGfx()` ->
`osSpTaskLoad` re-DMAs the boot ucode; the ucode only continues if its own
yield-save ran — our force-yield may be fabricating that).

### 6. Round-13 instrumentation (all counters, no file I/O on hot paths)

* `wd_hdr_type_n[4]`, `wd_c_gfx_load/aud`, `wd_hdr_ring[32][8]` — task loads by
  the type word read from the **DMA source** (ucode/ucode_data/data pointers
  name the task: `0x807505C0`/`0x80779860` = gspF3DEX2_fifo, `0x80768E60` = aspMain).
* `wd_c_rdp_kick`, `wd_rdp_last_{start,end,mi,sp}` — the RSP's `mtc0 DPC_END`
  reaching the core's `rsp_info.ProcessRdpList` wrapper.  **This is the only way
  to count RDP kicks**: parallel-RDP raises DP by writing `*gfx.MI_INTR_REG`
  directly, so it is invisible to the MI raise/signal counters
  (`wd_c_raise_bits[5]` read DP=0 for a whole run while the guest was acking DP).
* `wd_c_mi_rd_dp` (guest read `MI_INTR` with DP set), `wd_c_dp_ack` (guest
  cleared DP), `wd_c_dp_consumed` (round-10 block conversions).
* `wd_spw_ring[16][2]`, `wd_c_sp_status_wr`, `wd_c_sp_sig_wr` — guest SP_STATUS
  write stream and SIG0/SIG1 (yield) traffic.
* Dumped as `FRAME`/`RDPKICK`/`DPCHAIN` lines in each `wd_stall.txt` snapshot and
  as `FRAMEPROTO` in the 8MB dump header, plus `TASKRING`/`SPWRING` sections.

### 7. Evidence (round 13)

`.fzxwork/r13/`: `wd_stall.txt` + `ram_r13.bin` (pre-fix hard deadlock),
`fix_45s.bin` / `fix_95s.bin` / `fix_stall_45s.txt` / `fix_stall_95s.txt`
(post-fix, 45 s apart), `shot_50s.png` (pre-fix screen), `shot_fix_45s.png` /
`shot_fix_95s.png` (post-fix screen), `plain_60s.png` (plain-cart regression),
`run_r13*.log`, `run_r13*.sh`, `plain.log`.

## UPDATE 2026-09-10 (goal round 17) — the ucode's entry logic decoded, the k0 fix landed, and the DD route measured to be non-deterministic

Commit `a04d2c3d9`.

### 1. Which microcode this ROM actually submits (verified, not inferred)

The gfx task's `ucode` field in the live task struct is
`0x80752AE0` = **`gspF3DFLX2_Rej_fifoTextStart`** (`GFXMODE_F3DFLX`), data
`0x8077A090` = its `fifoDataStart`; the earlier logo/loading segments submit
`GFXMODE_F3DEX` (`gspF3DEX2_fifo` = text `0x807505C0`, data `0x80779860`).
Both match `linker_scripts/jp/ek/symbol_addrs_nlib_vars.txt` **and** the live
IMEM byte for byte (48/64 sampled words from `ram_live.bin` at RDRAM
`0x752AE0` -> IMEM `0x080`; the remainder is the 0x170-byte overlay block at
RDRAM `0x753AF8` that the ucode swaps into IMEM `0x000`).

Buffer layout, from the decomp (`src/sys/sys_gfx.c:167-172`,
`src/sys/buffers.c`): `gTaskOutputBuffer` = `0x802D9CD0` (0x54000),
`output_buff_size` = `gTaskOutputBuffer + 0x54000` = `0x8032DCD0`, and
`gOSYieldData` = **the same address** — so the RDP command FIFO and the libultra
yield buffer are adjacent, not aliased.

### 2. The entry logic, disassembled (this is the whole ball game)

```
IMEM 0098  lw   t3,0xF0(r0)      # DMEM[0xF0] = FIFO end pointer (0 = cold)
IMEM 009C  lw   t4,0xFC4(r0)     # DMEM[0xFC4] = the OSTask flags word
IMEM 00A4  beq  t3,r0,0x00C0     # cold: init RDP state, DPC_END=1, ...
IMEM 00AC  andi t4,t4,0x1        # OS_TASK_YIELDED
IMEM 00B0  beq  t4,r0,0x0144     # warm, not yielded
IMEM 00B4  sw   r0,0xFC4(r0)     # consume the flag
IMEM 00B8  j    0x0164           # ---- YIELDED RESUME ----
IMEM 00BC  lw   k0,0xBF8(r0)     #      k0 = DMEM[0xBF8]   <-- saved scratch
IMEM 0144  (adds `ucode` to the DMEM 0x280/0x288 overlay descriptors)
IMEM 0160  lw   k0,0xFF0(r0)     # k0 = header data_ptr  (cold AND warm)
IMEM 0164  main loop; per command it issues the 0xA8-byte DL chunk DMA into
           DMEM 0x9B0 via the shared helper at IMEM 0x0FAC/0x0FC8/0x0FD8
           (t8 = RDRAM, s3 = len-1, s4 = DMEM addr with bit31 = write), polls
           SIG0 at 0198, and branches to 0x0FAC (DMA + jump to the IMEM 0x000
           overlay) when SIG0 is set.
```

Two consequences that decided the round:

* **Only a yielded resume takes `k0` from `DMEM[0xBF8]`.** Everything else
  walks from the header's `data_ptr`.
* **`DMEM[0xBF8]` is scratch.** No store to `0xBF8` exists anywhere in the
  0xF80-byte text *or* in the 0x170-byte IMEM-0x000 overlay, so the saved k0 is
  whatever command word happened to sit there when the save ran — the live
  traces read `0xE6000000`, `0x10000003`, `0x1208`. With `[0xBF8]=0x1208` (round
  16's `wd_dmatr` tail) the ucode walked framebuffer memory full of
  `0x00010001`, read it as `G_NOOP` (opcode `0x00`) all the way into
  `gTaskOutputBuffer`, and kicked the RDP on an effectively empty list.

### 3. Round 16's premise corrected, and the fix

Round 16 claimed the boot ucode's `ucode_data -> DMEM 0` DMA is gated on
`OS_TASK_DP_WAIT`. It is not: IMEM `106C`'s `beq v0,r0,0x108C` jumps *to* the
DMA when DP_WAIT is clear, and the trace shows the transfer happening on every
yielded start (`D7982`/`D8944` = `RD dst=0000 src=32dcd0 len=0c00`, i.e. the
whole 0xC00 yield buffer over DMEM 0). So the round-16 copy duplicates a
transfer the guest performs by itself; the only thing the guest cannot express
is a word fix-up, which is now the whole fix:

```c
hdr[0xbf8 / 4] = hdr[0xff0 / 4];      /* k0 := header data_ptr */
```

Same DD gate as before (`dd_mode && (*SP_PC_REG & 0xfff) == 0`, header type
<= 2, `flags & OS_TASK_YIELDED`, `!DP_WAIT`). Measured firing once in the
round-17 baseline run (`wd_yld.txt`:
`rest_f0=002d9e58 saved_k0=0024e260 fixed_k0=0024e260 ff0=0024e260`) — there
the saved k0 already equalled `data_ptr`, so the fix was a no-op and the run
walked the real list; the failure mode it removes is the stale-scratch one.

### 4. New diagnostics: the RDP conversation, both sides of the call

`rsp_process_rdp_list()` (core `plugin.c`) now samples `MI_INTR_REG` before
*and* after the plugin call and records `DPC_START/CURRENT/END/STATUS`. The
watchdog dump grows:

```
RDPDP dp_seen=<n> dp_hot=<n> empty=<n> ring_n=<n>
RDPR  <i> start=… cur=… end=… st=… mib=… mia=…
```

`empty` counts kicks where `CURRENT..END` was non-positive — parallel-RDP's
`vk_process_commands()` processes **`DPC_CURRENT`..`DPC_END`** (not START) and
`return`s immediately when `length <= 0`, running no command and raising no DP.
First measurement (run r17c):
`RDPDP dp_seen=0 dp_hot=0 empty=2` with `START==CURRENT==END==0x32DCD0`, i.e.
the ucode kicked twice and the RDP was handed nothing both times.

### 5. Tried and reverted (both DD-gated, both documented in place)

Relaxing the 2 ms host budget **and** the 256-poll yield threshold for gfx
tasks, so the ucode could finish its list unprompted:

| build | RDPKICK | FRAME t1gfx | raise_bits VI / AI | screen |
|---|---|---|---|---|
| kept (2 ms + 256 polls) | 140 | 3 | 6089 / 6007 | running |
| relaxed (unbounded gfx) | **0** | 1 | **271 / 192** | black |

The gfx task parked and never reached `DPC_END`. **The host-side preemption is
load-bearing for this 64DD path** — do not remove it again without a
replacement interleaving mechanism.

### 6. The headline problem: this route is not reproducible

Five runs of near-identical builds, five outcomes:

| run | RDP kicks | gfx loads | DP raised | screen (YAVG) | end state |
|---|---|---|---|---|---|
| r16 (`abd7e5192`) | 245 | 5 | 1 | 61.8 (content) | frame protocol starves |
| r17 (`a04d2c3d9`) | 3 | 1 | 0 | 5.3 (black) | guest idle on gMainThreadMesgQueue |
| r17b | — | — | — | 61.8 then dead | app gone before 50 s |
| r17c | 2 (both empty) | 1 | 0 | 5.3 | ucode livelock: `RD src=ea1b00 -> IMEM 0x000` forever |
| r17e | 140 | 3 | 1 | 4.3 | `imem_bad=1`, guest idle |

The one mechanical cause we can name: **the 2 ms budget is wall-clock**
(`rsp_set_budget_deadline_us`), so how many slices a task takes, and *where*
inside the ucode each forced yield lands, changes run to run. Every forced
yield can fabricate a libultra yield acknowledgement for a task the ucode never
saved, which then resumes from scratch DMEM. That is the structure behind the
variance.

### 7. Round-18 target, in order

1. **Make the preemption deterministic.** Budget on RSP cycles (or a JIT
   instruction count) instead of wall-clock time, so the same ROM produces the
   same slice boundaries and runs become comparable. This is a prerequisite for
   evaluating anything else.
2. Re-measure with `RDPDP`: the goal is `empty=0` and `dp_seen` tracking kicks.
   An empty kick means the ucode's FIFO pointers (`DMEM[0xF0]`/`[0xF4]`) were
   resumed from a scratch image — the same class of fault as the k0 one, and
   the same fix shape applies (`DMEM[0xF0]` must be a live FIFO pointer).
3. Only then go back to the frame protocol: `sMainThread` parks on
   `gMainThreadMesgQueue` (`valid=0/16`) waiting for the SP/DP events in every
   run; the DP event needs a non-empty RDP list *and* `MI_INTR_DP` surviving to
   the core's DD-gated conversion block (`rsp_core.c`, `wd_c_dp_consumed` is
   still 0 in every run measured so far).

### 8. Evidence (round 17)

`.fzxwork/r17/`: `analyze.py`, `run_r17.sh`, `run_plain.sh`,
`ucode_dis.txt` (the entry/main-loop/overlay disassembly),
`rdpdp_evidence.txt`, `stall_50s/95s.txt`, `ram_50s/95s.bin`, `wd_dmatr.txt`.
`.fzxwork/r17c/` (empty kicks), `.fzxwork/r17d/` (the reverted budget
experiment), `.fzxwork/r17e/` (baseline restored + `wd_yld.txt` showing the k0
fix firing). Round-16 artefacts stay in `.fzxwork/r16/`.

Plain-game regression: every round-17 change is inside a runtime DD gate
(`g_dev.dd.idisk != NULL` / `IsDDPresent()`); `plugin.c`'s plain branch is
still the original single `gfx.processRDPList()` call and the new core globals
are only written on the DD path. See `run_plain.sh` output in
`.fzxwork/r17plain/`.

## UPDATE 2026-09-10 (goal round 18) — **preemption is deterministic now, and the memory wipe that ends every run is a 30-million-transfer RSP runaway**

### 1. Goal 1 of round 17 is done: the RSP slice budget is emulated work, not wall time

`rsp_jit.cpp`'s host-run budget counted *checks* now (`RSP_BUDGET_SLICE_UNITS`,
decremented once per JIT-emitted budget check -- one per 32 instructions and
one per intra-block label).  The wall-clock backstop is still there as a hang
guard but is set far above any real slice, and `rsp_budget_wall_hit` records
whether it ever fired: **in every run measured this round it never did**
(`.fzxwork/r18a/slice_stats.py`: 2044 slices, 0 wall hits).

Result, two runs of the same binary 50 s in (`r17f` vs `r17g`, then `r18d` vs
`r18e`): `c_spint` 21/21, `c_exc` 18635/18635, `c_exc_int` 18633/18633,
`c_raise` 18129/18129, identical `imem_bad` latch, identical `FRAME`/`RDPKICK`
counters.  The five-run scatter of round 17 (245/3/2/140 RDP kicks) is gone.

Calibration (`slice_stats.py`): a full 1024-check slice costs **100 us** of wall
time, i.e. the round-17 slice was ~0.1 ms of RSP work, **twenty times shorter**
than the 2 ms the DD route was tuned on.  The default is now **20480 checks**
(measured 12.1 ms median -- the checks/ms ratio varies ~6x with the code mix,
so the slice is sized by emulated work, deliberately, and the wall time follows).

### 2. That one change put the machine in its best state yet

| @50 s, same ROM/disk | r18a (1024 checks) | r18b (20480 checks) |
|---|---|---|
| `raise_bits SP` / `c_spint` | 21 | **194** |
| `raise_bits VI` | 2993 | **9947** |
| `dmas` (SP DMA count) | 44 | **388** |
| gfx task loads (`t1gfx`) | 0 | **1** |
| `RDPKICK n` | 0 | **229** (last kick `start=0x00010000 end=0x00010870`, a real list) |

### 3. The real killer: a runaway RSP DMA that wipes all of RDRAM

r18b's watchdog RAM dump came back with **1023 of 1024 8 KB blocks identical** --
all 8 MB of RDRAM holding one repeated 8 KB block.  That block is 64DD disk data
(its ` RSP Gfx ucode F3DLX.Rej ... 1998 Nintendo` banner matches `F-Zero X.ndd`
at `0xB22000` in word-swapped form).  The guest then executed the fill as
instructions -- `0x00010001` is a COP1 `MOVF` -- and burned **32.5 million
nested COP1-unusable exceptions** at pc `0x80000664` (`COP1SNAP2/3`, guest
`__osException` vector `0x80000180` also overwritten by the fill).

The RSP-side DMA trace (`r14_ring` -> `wd_dmatr.txt`) names the mechanism: the
run issued **29 923 608** SP transfers in one session.  Once the ucode's
display-list pointer walks into the fill, its DMA length register climbs
without bound (`len=0800,0808,0810,0818,...`) and the transfers mask into RDRAM
through the `& 0x7FFFFC` wrap in `rsp_dma_write()`, stamping the same source
data over the whole address space.  The trace's last entries write
`dst=281b2e80` -- an address register that has left RDRAM entirely, which the
plugin's own `MTC0` mask never clamps on the accumulated side
(`*cr[DMA_DRAM] = dest` after `dest += length + skip`).

### 4. Round-18 fix: refuse transfers whose address has left RDRAM (DD-gated)

Verified (`r18d` vs `r18e`, same guarded build, two runs):

| @50 s | r18b (no guard) | r18d | r18e |
|---|---|---|---|
| RDRAM dump | **wiped** (1023/1024 blocks identical) | varied | varied |
| `c_exc` (nested COP1 storm) | **32 551 330** | 18 744 | 18 766 |
| `c_vi_evt` / `c_vi_ack` | 9947 / 277 (starved) | 2945 / 2946 | 2954 / 2955 |
| `RDPKICK n` | 229 | **607** | **607** |
| screen (`shotstat.py`) | YAVG 32 (the fill) | YAVG 5.3 == round 13's known-good `shot_fix_95s.png` profile | same |

and the latch itself is byte-identical in both guarded runs:

```
WILD dir=RD pc=0fc4 dram=00fffff8 mem=00000920 len=06d8 cnt=0 skip=0
     status=00000040 imem=900100de 001913c0 0c000487 035b1820
     dmem0=00000000 fc0=def7ffff ff0=00000000 f0=000006cf
```

i.e. the ucode is at IMEM `0x0fc4` -- inside F3DEX2's display-list fetch helper --
holding real ucode code, while its DMA address register has walked to the end of
the 16 MB address space, and DMEM `0xFC0` reads `0xdef7ffff` (a clobbered
header, not a task type).  So the walk is already lost by the time it leaves
RDRAM; refusing the transfer is containment, and `wd_wild.txt` is now the exact
falsifiable record of *where* it is lost.

`rsp/cp0.cpp`: `r14_wild_check()` latches the first transfer with
`dram_addr > 0x7FFFFF` into `wd_wild.txt` (pc, both addresses, length/count/skip,
SP_STATUS, IMEM[0..3], DMEM 0/0xF0/0xFC0/0xFF0) and refuses it; the read path
returns `MODE_CHECK_FLAGS` so the slice ends immediately, the write path simply
does not transfer.  Gated on the runtime presence callback
(`rsp_ares_budget_enabled()`), so plain carts keep the original code path.
This does not make the game work -- it stops one bad walk from destroying the
machine, and it names the exact ucode pc where the walk goes wild.

### 5. Next

The machine is reproducible again and the failure is named, so the display-list
walk can be attacked directly: resume from `wd_wild.txt`'s wild pc `0x0fc4`,
recover the k0/data_ptr the ucode was walking (DMEM `0xF0` is `0x6cf` -- a
DMEM-internal FIFO pointer -- while `0xFF0` reads 0, i.e. the header's
`data_ptr` is *gone*), and compare against what the guest's task struct actually
held.  Determinism note: the *slice* is deterministic (0 wall-backstop hits, an
identical latch in both runs) but the number of host clock-ms between two
pumps is not, so counters can drift by a few units between runs (1643 vs 1644
`c_task`, 607 vs 607 `RDPKICK`) -- treat runs as comparable, not bit-identical.
The frame protocol (goal 3) is unchanged: `MI_INTR_DP` still never survives to
the core's DD-gated conversion (`wd_c_dp_consumed` 0, `raise_bits DP=0`).

## UPDATE 2026-09-10 (goal round 19) — the k0 that walks an empty buffer, and a build trap that invalidated two runs

### 1. Instruments added this round (all DD-gated; RAM-only apart from first-time latches)

* `rsp/cp0.cpp` `r19_imem_note` -- the first 8 **RSP-side DMA reads whose
  destination is in the IMEM bank** (`dst & 0x1000`), with pc/dst/src/len and
  the DMEM header words.  This is the instrument that answers "who loads IMEM".
* `rsp/cp0.cpp` `r19_cmd_latch` -- the first 16 `mtc0 CMD_START` / `CMD_END`
  writes, with the value the ucode programmed and the DMEM header fields it
  computed it from (`output_buff`, `output_buff_size`, `data_ptr`,
  `data_size`, the descriptor at 0x2E0).
* `rsp/cp0.cpp` `r19_bankwrap_note` -- counts transfers whose *stock*
  `& 0x1FFC` destination mask would carry them across the 4 KiB bank boundary
  (DMEM->IMEM / IMEM->DMEM), first of each direction on record; plus a
  DD-gated bank-limited wrap in `rsp_dma_read` (`dest & 0x1000 | (dest+j) &
  0xFFC`), which is what real hardware and this core's own `do_sp_dma`
  (ROUND-7 fix) already do.
* `plugin.c` + `rsp_core.c` -- a **first-16 RDP kick ring** (`RDPF` lines) next
  to the round-17 last-16 ring, so the two ends of the run can be compared.
* `rsp_core.c` `wd_imem_probe()` -- latches the *first* operation that leaves
  `IMEM[0..1] == 0x00010001` **with the path that did it** (1 = CPU direct
  SP-memory write, 2 = CPU-side SP DMA, 5 = only ever seen by the pump), plus
  its parameters and the CP0 count.
* **Fixed a structurally dead counter**: `wd_cpuw`'s IMEM test compared a
  *word index* against 0x1000, so `wd_cpuw_imem_n` read 0 in every run of every
  round since round 11.  The IMEM bank is word index 0x400+.  Corrected, the
  real number is **2319 direct CPU writes into the IMEM bank per run** (first at
  address 0x04001FD8, 47 of them carrying a fill value, first at 0x04001FDC).

### 2. Measured: the gfx task's whole preamble is *correct*

From the RSP-side DMA trace (`.fzxwork/r19a/dmatr_head.txt`, 40 492 transfers
of the first 6 MB -- the file grows to tens of GB, never pull it whole):

```
D7324 RD dst=0000 src=779860 len=0800   fc0=00000001 ff0=00284990 fc4=00000004   ucode_data -> DMEM 0
D7325 RD dst=1080 src=7505c0 len=0f80   f0=00000000                            text      -> IMEM 0x080
D7326 RD dst=1000 src=7515d8 len=0170   f0=0032dcd0                            overlay   -> IMEM 0x000
D7327 RD dst=0920 src=2c03c0 len=00a8   <-- the FIRST display-list fetch, from the WRONG address
```

The guest's OSTask is intact (`type=1 flags=0x4 OS_TASK_LOADABLE`,
`ucode 0x7505c0`, `ucode_data 0x779860`, `data_ptr 0x284990` = a real
three-command list `gSPSegment(0,0) / gDPFullSync / gDPEndDisplayList`), the
task-header DMA into DMEM 0xFC0 comes from the same static struct as every
audio task (`RDRAM 0x7C1C00`, `wd_hdr.txt`), and the CPU-side loads are exactly
the two that `osSpTaskLoad` issues.

### 3. The first wrong pointer: k0 = 0x2C03C0 instead of data_ptr = 0x284990

`0x284990` never appears as a DMA source anywhere in the run (no
`src=284990` line in the whole trace): the walk starts at **0x2C03C0** -- all
zero in RDRAM -- and steps by the 168-byte chunk size.  Zero words are
`G_NOOP` (opcode 0x00), so the list never ends:

* the FIFO/output pointer climbs (`DMEM[0xF0] = 0x32dcd0, 0x2d9e30, 0x2d9f98,
  ...`, +8 per fetch),
* the fetch length register climbs with it (`len=0168, 0170, 0178, ...`, the
  round-18 `wd_dmatr.txt` pattern),
* the source walks out of the RDP output buffer into RDRAM and finally past the
  end of it -- which is the transfer round 18's guard latches
  (`WILD dir=RD pc=fc4 dram=00fffff8 mem=00000920 len=06d8`),
* and the machine spends the rest of the run re-walking `0x00010001` fill.

**Corrected decode.** Round 17's "entry logic" (`IMEM 0x098..0x118`) is *text*
code, but the ucode loads a 368-byte **overlay** from RDRAM 0x7515D8 over
`IMEM 0x000..0x16F` *before* the walk starts (D7326), so at run time that range
holds the overlay, not the text.  The main loop is at IMEM 0x170.., which is
outside the overlay:

```
0170: addi s3,r0,167      # len-1 = 167 -> 168 bytes per chunk
0174: ori  t8,k0,0        # DMA source = k0
0178: jal  0x0FD8         # issue the read DMA
017C: addiu s4,r0,0x920   #   into DMEM 0x920
0180: addiu k0,k0,0xA8    # k0 += 168
0184: addi k1,r0,-0xA8    # buffer cursor
018C..: per-command dispatch, SIG0 poll -> 0x0FAC = load the overlay + jump to
        IMEM 0x000;  the overlay's first basic block ends `ori k0,t8,0` ->
        `j 0x0170`, i.e. the overlay hands the fetch routine whatever t8 held,
        and t8 is left over from the overlay *loader* (`lw t8,0(t3)` of the
        descriptor at DMEM 0x2E0 / 0x2E8).
```

So the initial k0 is whatever the overlay loader's descriptor held -- it must
be `data_ptr` (DMEM 0xFF0) for a fresh task and the saved pointer for a yielded
resume, and round 17's `DMEM[0xBF8] = data_ptr` fix-up only fires for
`flags & OS_TASK_YIELDED` (this task's flags are 0x4 = `OS_TASK_LOADABLE`, so
it never applies).  **This is the next thing to make right.**

### 4. The frame protocol: DPC is garbage from the very first kick

`RDPF` (first 16 kicks, in order) and `RDPR` (last 16) from the same run:

```
RDPF 0..1  start=0032dcd0 cur=0032dcd0 end=0032dcd0   <-- empty, and START is
                                                          the *yield* pointer,
                                                          not output_buff
RDPF 2..15 start=002d9cd0 cur=002d9cd0 end=002d9e30 (+0x168, +0x178, ...)
RDPR *     start=fffffff8 cur=fffffff8 end=00001438   st=00000008
RDPDP      dp_seen=0 dp_hot=0 empty=482 ring_n=607
```

The ucode's **first** `mtc0 CMD_START` writes 0x32DCD0 (the task's
`yield_data_ptr` / `output_buff_size` end pointer) and only the second writes
`output_buff` 0x2D9CD0; `parallel-RDP` raises `MI_INTR_DP` only for an
`RDP::Op::SyncFull` inside `CURRENT..END`, and by 50 s `START` is 0xFFFFFFF8
with `CURRENT > END`, so **every** kick is discarded before a command is looked
at (`empty=482/607`) and `MI_INTR_DP` is never raised (`raise_bits DP=0`,
`wd_c_dp_consumed=0`).  The guest's `OS_TASK_DP_WAIT`-less gfx thread blocks in
`osRecvMesg(&D_800DCAC8)` for exactly that event, so the frame protocol still
cannot advance.

### 5. What is *not* the cause (measured, negative results)

* The watchdog's IMEM/DMEM poisoning is **not** a cross-bank DMA and **not** a
  transfer any instrumented writer issues: `IMEMKILL path=5` (the pump sees the
  fill before any CPU write, CPU SP DMA or RSP DMA does), only **two**
  IMEM-writing RSP DMAs happen in the entire run (both legitimate, above), and
  the header DMA always comes from the guest's own OSTask.
* The poison *content* is a verbatim copy of RDRAM: the poisoned
  `DMEM 0xFC0..0xFEF` (`def7ffff ffffffff ffffffff ce731085 00010001 x5
  00014211 ffffffff ffffffff`) is byte-identical to `RDRAM 0x12218..0x12247`
  (copies of that record also exist at RDRAM 0x1EB618, 0x211E18, 0x76AD3C), and
  the fill run around it starts at RDRAM 0x11258 -- i.e. **8 KiB of RDRAM were
  copied into SP memory 0x0000..0x1FFF**, DMEM 0x000 <- RDRAM 0x11258 and
  IMEM 0x000 <- RDRAM 0x12258.  Which copy, and from which side, is the open
  question; a snapshot-diff of SP memory between pump calls is the instrument
  to answer it (the `wd_spkill.txt` dump at slice entry was added this round
  but never ran -- see below).
* The RSP JIT (`jit_andi(..., 0xfff)`) and the interpreter (`ls.cpp`, all VU
  stores `& 0xfff`, all 16-byte stores bounded to `addr & 0xf == 0`) keep every
  store inside its own 4 KiB bank, so an RSP *store* cannot be the IMEM writer.

### 6. Build trap (important): gradle silently skipped the native rebuild

Two device runs this round (`r19b`, `r19c`) were made with a **stale
libmupen64plus-rsp-parallel.so**: `:app:assembleDebug` reported success and
produced a 10:32 APK, but the C++ changes from 10:40 and 10:45 were not in it
(`strings` on the packaged .so: `R19IMEM`/`R19CMD` present from the 10:32 build,
`R19WRAP`/`SPKILL` absent).  Both runs are therefore **void** -- in particular
`wd_wrap.txt` never appearing does *not* yet prove "no transfer crossed banks".
Fix: `rm -rf mupen64plus-rsp-parallel/build/intermediates/cxx
mupen64plus-rsp-parallel/.cxx app/build/intermediates/{merged,stripped}_native_libs`
before the build, and **verify a new marker string is in the packaged .so**
(`unzip -p app-debug.apk lib/arm64-v8a/libmupen64plus-rsp-parallel.so | strings
| grep <marker>`) before trusting any device run.  Round 18's and round 17's
results are unaffected (each of those rounds verified a *new* diagnostic file
that only the new code writes).

### 7. RESULT (r19d, forced rebuild -- the round's real outcome)

With the native rebuild actually applied (`strings` on the packaged .so now has
`R19WRAP`, `SPKILL`), the same 50 s run gives:

| @50 s, DD route | r19a (before) | **r19d (bank-limited wrap)** |
|---|---|---|
| SP memory | `imem_bad=1`, IMEM 0x000-0x00F **and** DMEM 0xFC0-0xFFF = 0x00010001, `IMEMKILL path=5` | **`imem_bad=0`, `IMEMKILL path=0`** -- intact |
| `WDWRAP` latch | (instrument absent) | **`DMEM->IMEM dst=0920 len=06e8 cnt=0 skip=0 end=01008 pc=18c`** |
| gfx task loads (`t1gfx`) | 1 | **6** |
| `RDPKICK n` | 607 | **2538** |
| `c_exc` | 18737 | 18789 (no exception storm either way) |
| screen (`shotstat.py`) | YAVG 5.3, brightest y=572-576(max 45) | identical profile |

The `WDWRAP` line is the exact mechanism, caught in one line: a **read DMA to
DMEM 0x920 whose length (0x6e8) carries the destination to 0x1008** -- the stock
`& 0x1FFC` mask kept going past 0x1000 and wrote the last 8 bytes into **IMEM
0x000**, and with the runaway's ever-growing length register every later
transfer crossed further.  Round 18 stopped the *RDRAM* wipe; this stops the
**SP-memory** wipe, and unlike round 18's refusal it is not containment: the
transfer still happens, it just wraps inside its own 4 KiB bank, which is what
hardware and this core's CPU-side SP DMA already do.

Measured effect: the RSP keeps its program, the game submits **6 gfx tasks**
instead of one, and the RDP is kicked 4x as often.  The picture is identical
(still the 64DD loading screen, YAVG 5.3), so this is a *state* improvement, not
yet a visible one -- and the frame protocol is still unfixed (`raise_bits DP=0`,
`dp_seen=0`, and the last 16 kicks read `start=cur=end=0x00010000`).
`wd_spkill.txt` is absent because IMEM is never poisoned any more.

### 8. Plain-game rule (user, 2026-09-05): re-checked clean

The whole round-19 change set is inside runtime DD gates -- the wrap branch in
`rsp_dma_read` keys off `rsp_ares_budget_enabled()` (the runtime
`IsDDPresent()` callback), every new latch is inside `if (g_dev.dd.idisk !=
NULL)`, and the first-kick ring only fills on the DD path.  Mario Tennis (USA)
re-run with the r19d build: renders (YAVG 39.3 on an animated menu frame,
maxluma 224), **no** `wd_stall.txt` (the same absent-file result as round 18)
and an unchanged 22-byte `wd_smc.txt`.  Evidence `.fzxwork/r19plain/`.

### 9. Next (round 20)

1. `wd_wrap.txt` answered the SP-memory question (see 7).  The whole-8-KiB
   `wd_spkill.txt` dump stays armed for the next regression.
2. Fix the initial k0: give the overlay loader's `data_ptr`/saved-pointer choice
   the header it needs (the fetch must start at DMEM[0xFF0] for a fresh task).
   With the walk starting on the real three-command list, `gDPFullSync` reaches
   `parallel-RDP`'s `SyncFull` branch, which is the only path that raises
   `MI_INTR_DP`.
3. Frame protocol: after (2), `DPC_START` must be `output_buff` (0x2D9CD0) for
   the *first* kick; the 0x32DCD0 first-kick value is the symptom to re-check.
