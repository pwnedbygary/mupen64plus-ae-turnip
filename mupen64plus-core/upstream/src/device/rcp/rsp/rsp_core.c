/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rsp_core.c                                              *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "rsp_core.h"

#include <stdio.h>
#include <string.h>

#include "device/memory/memory.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rdram/rdram.h"
#include "main/main.h"
#if defined(PROFILE)
#include "main/profile.h"
#endif
#include "plugin/plugin.h"
#include "api/callbacks.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#include <unistd.h>
#endif

/* ---------------------------------------------------------------------------
   files/wd_spstock.flag -- A/B switch for the 64DD route's SP model.

   ABSENT (default) = the DD route's campaign behavior: a background RSP pump
   driven from every CPU block boundary, core-synthesized SP interrupts for
   ucode yields and breaks, and a forced HALT between slices.

   PRESENT = stock upstream semantics for the DD route as well: no pump, no
   synthesized SP interrupt (only the stock rsp_interrupt_event path), no
   forced HALT.  This exists so the DD route can be measured against the model
   every other route uses, i.e. to decide whether the campaign's SP heuristics
   help or hurt.  It changes nothing on the plain-cart route.
   --------------------------------------------------------------------------- */
int wd_sp_stock(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = 0;
#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
        cached = (access("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_spstock.flag",
                         F_OK) == 0);
#endif
    }
    return cached;
}

/* files/wd_ddlegacy.flag -- select the campaign's older DD SP model (a
   do_SP_Task-driven pump plus core-synthesized SP interrupts and a forced
   HALT between slices) instead of the default hardware-shaped one.  Kept only
   so the two can be A/B'd on the device; nothing else reads it. */
int wd_dd_legacy(void)
{
    static int cached = -1;
    if (cached < 0) {
        cached = 0;
#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
        cached = (access("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_ddlegacy.flag",
                         F_OK) == 0);
#endif
    }
    return cached;
}

/* ---------------------------------------------------------------------------
   ROUND-7 DD DIAG: who loads the RSP task header, and does it survive?
   libultra's osSpTaskLoad DMAs the 64-byte OSTask into DMEM 0xFC0 (from its
   static tmp_task; physical 0x7C1C00 in the F-Zero X EK build), and
   do_SP_Task classifies the task by reading DMEM[0xFC0/4].  Live the header
   reads 0x00010001 x16 (the game's own 0x0001 memory-fill pattern) while
   tmp_task holds a perfectly valid M_AUDTASK, so either the header DMA never
   lands in DMEM 0xFC0 or the running ucode clobbers it afterwards.  Record
   the source and the post-copy destination of every DMA covering 0xFC0, then
   re-read DMEM at the next do_SP_Task entry: dm_same != 0 means the DMA is
   fine and the ucode overwrote it; dm_same == 0 with hdr_n != 0 means the
   copy landed elsewhere.  hdr_n == 0 means no header DMA ever happened.
   --------------------------------------------------------------------------- */
volatile uint32_t wd_c_spdma = 0;      /* SP DMAs seen on the DD route       */

/* Round 35 (defined in cached_interp.c): low-RDRAM canary helpers. */
int wd_low_range(uint32_t addr, uint32_t len);
void wd_low_note(const char* who, uint32_t a, uint32_t b, uint32_t len, uint32_t pc);
void wd_lx_add(uint32_t kind, uint32_t a, uint32_t b, uint32_t len, uint32_t pc);
volatile uint32_t wd_hdr_n = 0;        /* DMAs covering DMEM 0xFC0           */
uint32_t wd_hdr_dram = 0, wd_hdr_mem = 0, wd_hdr_len = 0, wd_hdr_seq = 0;
uint32_t wd_hdr_src[16];               /* source words, before the copy      */
uint32_t wd_hdr_dst[16];               /* DMEM 0xFC0 words, after the copy   */
volatile uint32_t wd_hdr_same = 0, wd_hdr_diff = 0;

/* ---------------------------------------------------------------------------
   ROUND-13 DD DIAG: where does the gfx/DP frame protocol break?

   Round 12 decoded the stalled guest exactly: sGameThread (the EK frame/gfx
   thread, prio 10) is blocked in osRecvMesg(&D_800DCAC8) waiting for message
   0x2A, which sys_main.c:396 only sends when the main thread (prio 99)
   receives EVENT_MESG_DP -- i.e. when the guest's DP handler dispatches
   OS_EVENT_DP -- i.e. only when MI_INTR_DP reaches the CPU.  The RSP path
   delivers that bit through rsp_info.ProcessRdpList (the ares RSP's
   `mtc0 DPC_END` -> plugin cp0.cpp -> this core's wrapper -> parallel-RDP,
   which sets MI_INTR_REG |= DP).  Three numbers decide where the chain dies,
   and none of them existed before this round:

     wd_hdr_type_n[1]  -- was an M_GFXTASK (DMEM 0xFC0 type 1) ever loaded?
     wd_c_rdp_kick     -- did the ucode ever reach `mtc0 DPC_END`?
     wd_c_sp_*         -- what SP_STATUS/yield traffic does the guest emit?

   Counters only: no file I/O on any hot path.  Everything is printed by the
   watchdog dump (cached_interp.c wd_print_snap / wd_full_dump). */
volatile uint32_t wd_hdr_type_n[4] = {0, 0, 0, 0};  /* task loads by type    */
volatile uint32_t wd_c_task_etype[4] = {0, 0, 0, 0}; /* do_SP_Task entry type */
volatile uint32_t wd_c_gfx_load = 0, wd_c_audio_load = 0;
volatile uint32_t wd_hdr_ring_n = 0;
uint32_t wd_hdr_ring[32][8];  /* seq,type,ucode,ucode_data,data,status,pc,resv */
/* ROUND 22: the task header of the MOST RECENT task load, as latched from the
   DMA source.  This is published to the RSP plugin through RSP_INFO
   (TaskHeaderLatch/TaskHeaderSeq) because the plugin's forced-yield path needs
   the header's yield_data_ptr, and by the time the RSP has been running the
   DMEM copy at 0xFC0 has been overwritten by the ucode itself.  Layout is the
   libultra RSP task header: [0] type, [1] flags, [4] ucode (0xFD0),
   [14] yield_data_ptr (0xFF8), [15] yield_data_size (0xFFC). */
volatile uint32_t wd_cur_hdr_seq = 0;
uint32_t wd_cur_hdr[16];
volatile uint32_t wd_spw_ring_n = 0;
uint32_t wd_spw_ring[16][2];  /* last SP_STATUS writes: value, guest pc      */
volatile uint32_t wd_c_sp_status_wr = 0, wd_c_sp_sig_wr = 0;
/* Written by the core-side ProcessRdpList wrapper (plugin/plugin.c). */
volatile uint32_t wd_c_rdp_kick = 0;
volatile uint32_t wd_c_dp_consumed = 0;   /* DP bits converted to CP0 events */
uint32_t wd_rdp_last_start = 0, wd_rdp_last_end = 0, wd_rdp_last_mi = 0, wd_rdp_last_sp = 0;

/* ROUND-17 DIAG (DD-gated at the call site): what the RDP did with each kick.
   The round-13/16 counters could not separate "parallel-RDP never reached the
   list" from "it processed the list but the DP bit was consumed before anyone
   looked", because the wrapper only sampled MI_INTR BEFORE the call.
   wd_c_rdp_dp_seen counts kicks after which MI_INTR_DP is set -- with
   wd_c_rdp_dp_hot counting those where it was ALREADY set on entry (i.e. the
   previous raise was never consumed, which is the other failure mode), and
   wd_rdp_ring records (start, current, end, status, mi_before, mi_after) of
   the last 16 kicks so the whole conversation is visible in one dump. */
volatile uint32_t wd_c_rdp_dp_seen = 0, wd_c_rdp_dp_hot = 0, wd_c_rdp_empty = 0;
volatile uint32_t wd_c_rdp_noadv = 0, wd_c_rdp_bad = 0;
volatile uint32_t wd_rdp_ring_n = 0;
uint32_t wd_rdp_ring[16][6];
/* ROUND 19: the first 16 kicks, so the watchdog can answer "was the RDP ever
   handed a real CURRENT..END window?" (the last-16 ring is all garbage once the
   ucode's state is lost). */
volatile uint32_t wd_rdp_first_n = 0;
uint32_t wd_rdp_first[16][6];

/* ROUND 25, 64DD ROUTE ONLY.  How often the guest took the RSP over for a
   task load (__osSpSetPc(SP_IMEM_START)), and how many background-pump slices
   were refused because of it.  Printed by the watchdog so one device run
   proves the guard engaged (a plain cart never touches either counter).
   `wd_pump_call` marks the slices the pump itself issues, so do_SP_Task can
   tell "the guest is starting a task" from "we are feeding the RSP behind the
   guest's back". */
volatile uint32_t wd_c_loadguard_set = 0, wd_c_loadguard_skip = 0;
static int wd_pump_call = 0;

/* ROUND 57: measured cost of the last background-pump RSP slice, in us, and
   the duty cycle that caps how much of the emulation thread the pump may own.
   See rsp_dd_background_pump() for the measurement that made this necessary. */
static unsigned int wd_pump_last_us = 0;
#define RSP_DD_PUMP_DUTY 2   /* RSP may start a slice after 2x the last one */

/* ROUND 11 DD DIAG: CPU writes into SP memory (write_rsp_mem), counted and
   latched in memory only; and the first moment the RSP's IMEM is observed to
   hold the game's cleared-buffer fill pattern.  All of it is printed by the
   watchdog stall dump (main.c), so nothing is written from a hot path. */
volatile uint32_t wd_cpuw_n = 0, wd_cpuw_imem_n = 0, wd_cpuw_fill_n = 0;
uint32_t wd_cpuw_latch_addr = 0, wd_cpuw_latch_val = 0, wd_cpuw_latch_mask = 0;
uint32_t wd_cpuw_last_addr = 0, wd_cpuw_last_val = 0, wd_cpuw_last_mask = 0;
uint32_t wd_cpuw_imem_latch_addr = 0, wd_cpuw_imem_latch_val = 0;
volatile uint32_t wd_imem_bad = 0;             /* garbage IMEM observed      */
/* ROUND-67: the header-latch seq at the moment wd_imem_bad latched; the pump
   re-arms the RSP only when the guest loads a NEW task header (seq moves). */
volatile uint32_t wd_imem_bad_hdr_seq = 0;
uint32_t wd_imem_bad_word[4] = {0, 0, 0, 0};   /* IMEM[0..3] at that moment  */
uint32_t wd_imem_bad_fc0 = 0, wd_imem_bad_pc = 0, wd_imem_bad_status = 0;
uint32_t wd_imem_bad_count = 0, wd_imem_bad_spdma = 0;

/* ROUND 19: WHO wrote the bad IMEM.  `wd_check_imem_sane` (below) is only
   called from the background pump, i.e. up to ~12 ms after the damage, and it
   cannot say which path did it.  wd_imem_probe() is called from every path
   that can write IMEM and latches the FIRST one that observes the fill
   pattern, with the parameters of the operation that just ran:
     path 1 = CPU direct SP-memory write (write_rsp_mem, guest 0xA4001xxx)
     path 2 = CPU-side SP DMA (do_sp_dma, the ucode/task load)
     path 3 = RSP-side DMA (plugin rsp_dma_read -> wd_imem.txt)
     path 5 = the background pump (i.e. none of the above: seen too late)
   Two word compares on the DD path only; no file I/O. */
volatile uint32_t wd_imem_kill_path = 0;
uint32_t wd_imem_kill_a = 0, wd_imem_kill_b = 0, wd_imem_kill_c = 0, wd_imem_kill_d = 0;
uint32_t wd_imem_kill_pc = 0, wd_imem_kill_count = 0, wd_imem_kill_spdma = 0;
/* Round 49: the sampled fill density at the moment of the latch, and the GUEST
   pc of the write that crossed the threshold (path 1 = a CPU store, so this
   names the libultra routine doing it). */
uint32_t wd_imem_kill_fill = 0, wd_imem_kill_gpc = 0;

void wd_imem_probe(struct rsp_core* sp, uint32_t path, uint32_t a, uint32_t b,
                   uint32_t c, uint32_t d)
{
    /* ROUND 49 FIX -- WHY THIS NEVER FIRED, AND WHY THAT MATTERED.
       The old test was `im[0x1000/4] == 0x00010001 && im[0x1000/4+1] ==
       0x00010001`, i.e. it required IMEM[0] AND IMEM[1] to hold the fill
       pattern.  The live memory does not satisfy that: the new SPMEM1 probe
       reports `imem_fill=792 imem0=ffffffff` (r59/t070) and, in the r58 dump,
       IMEM as a single repeated word with a different word 0.  So the latch
       could NOT fire no matter which path did the damage -- which is exactly
       what was observed (`wd_imem_kill_path` came out 5, the pump fallback,
       whose only meaning is "nobody caught it").  Path 3 is also a phantom:
       the plugin never calls this function, and an RSP ucode cannot store to
       IMEM in the first place, so the real writers are path 1 (CPU store) and
       path 2 (SP DMA) -- both of which were already wired.

       The test is now a DENSITY TRANSITION over a sampled scan: count how many
       of 32 words spread across IMEM equal the fill pattern, and latch on the
       call that takes the count from below 20 to 20-or-more.  A real ucode
       scores ~0; the live damaged IMEM scores 24-32.  Sampling keeps this
       cheap enough to sit on every write path (32 word reads), and because the
       fill is being rewritten continuously the crossing is caught at the
       writer rather than ~12 ms later. */
    static uint32_t prev_fill = 0;
    const uint32_t* im = (const uint32_t*)sp->mem;
    uint32_t i, fill = 0;
    for (i = 0; i < 32; i++)
        if (im[(0x1000 >> 2) + i * 32] == 0x00010001u) fill++;
    if (prev_fill < 20u && fill >= 20u && !wd_imem_kill_path)
    {
        wd_imem_kill_path = path;
        wd_imem_kill_a = a; wd_imem_kill_b = b; wd_imem_kill_c = c; wd_imem_kill_d = d;
        wd_imem_kill_pc = sp->regs2[SP_PC_REG];
        wd_imem_kill_count = r4300_cp0_regs(&sp->mi->r4300->cp0)[CP0_COUNT_REG];
        wd_imem_kill_spdma = wd_c_spdma;
        wd_imem_kill_fill = fill;
        /* The guest PC of the code that performed the write: for path 1 that
           names the libultra routine doing the CPU store, which is the whole
           point of the exercise. */
        wd_imem_kill_gpc = sp->mi->r4300 != NULL ? *r4300_pc(sp->mi->r4300) : 0;
    }
    prev_fill = fill;
}

/* Cheap (two word compares) and DD-gated; called from the background pump so a
   garbage IMEM is dated against the CP0 count and the SP-DMA counter. */
static void wd_check_imem_sane(struct rsp_core* sp)
{
    const uint32_t* im = (const uint32_t*)sp->mem;
    wd_imem_probe(sp, 5, 0, 0, 0, 0);   /* round 19: fallback if no writer caught it */
    if (wd_imem_bad) return;
    if (im[0x1000 / 4] == 0x00010001u && im[0x1000 / 4 + 1] == 0x00010001u)
    {
        wd_imem_bad = 1;
        wd_imem_bad_hdr_seq = wd_cur_hdr_seq;   /* ROUND-67: re-arm on the next task load */
        wd_imem_bad_word[0] = im[0x1000 / 4];
        wd_imem_bad_word[1] = im[0x1000 / 4 + 1];
        wd_imem_bad_word[2] = im[0x1000 / 4 + 2];
        wd_imem_bad_word[3] = im[0x1000 / 4 + 3];
        wd_imem_bad_fc0 = im[0xfc0 / 4];
        wd_imem_bad_pc = sp->regs2[SP_PC_REG];
        wd_imem_bad_status = sp->regs[SP_STATUS_REG];
        wd_imem_bad_count = r4300_cp0_regs(&sp->mi->r4300->cp0)[CP0_COUNT_REG];
        wd_imem_bad_spdma = wd_c_spdma;
    }
}

/* ---------------------------------------------------------------------------
   ROUND-65 DIAG: the IMEM / DMEM-header shadow watch (files/wd_watch65.txt).

   Rounds 63/64 established WHAT the damage is (IMEM becomes a verbatim copy of
   an RDRAM region; DMEM 0xFC0..0xFFF loses its OSTask header) but not WHO
   writes it -- and the round-49 fill-signature probe cannot fire for the real
   content (it tests for 0x00010001 density; the live damage is sparse audio
   data).  This watch is signature-free: it remembers IMEM[0..3] and the whole
   DMEM 0xFC0..0xFFF header region between check points and logs any change.

   Write-path inventory (round 65 survey) so the announce lines can be matched
   against WATCH lines: the plugin's rsp_dma_read is the only plugin IMEM
   writer (bank-wrapped on this route, announces via wd_dma65.txt); the ucode
   JIT stores mask to 0xfff (DMEM only); the core's do_sp_dma is bank-contained
   (announces via wd_dmatr); the guest CPU store path (write_rsp_mem) is the
   only uncontained path and announces via wd_cpuw65.txt.  A WATCH line whose
   change has no matching announce line therefore names an uninstrumented
   writer directly.

   Sites: 0 do_SP_Task entry, 1 do_SP_Task exit, 2 rsp_dd_slice entry,
   3 rsp_dd_slice exit, 4 rsp_interrupt_event.  All DD-gated; file capped at
   200 lines; I/O only on an observed change. */
#define WD65_FILES_DIR "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/"
static uint32_t wd65_sh_imem[4];
static uint32_t wd65_sh_hdr[16];
static int wd65_armed = 0;
static unsigned wd65_watch_lines = 0;
static unsigned wd65_cpuw_lines = 0;   /* write_rsp_mem announce cap */
/* ROUND-67: RAM ring of the last 64 guest stores into IMEM / the header
   region, filled by write_rsp_mem (no I/O on the hot path) and flushed into
   wd_watch65.txt by the shadow watch when it detects a transition -- so the
   stores that immediately precede the damage are always captured, whatever
   the file cap was doing. */
uint32_t wd65w_ring[64][4];
volatile uint32_t wd65w_n = 0;

static void wd65_watch_check(struct rsp_core* sp, unsigned site)
{
    FILE* f;
    unsigned i;
    int imem_changed = 0, hdr_changed = 0;
    const uint32_t* im = (const uint32_t*)sp->mem;

    if (g_dev.dd.idisk == NULL)
        return;
    if (!wd65_armed)
    {
        for (i = 0; i < 4; i++)
            wd65_sh_imem[i] = im[(0x1000 >> 2) + i];
        for (i = 0; i < 16; i++)
            wd65_sh_hdr[i] = im[(0xfc0 >> 2) + i];
        wd65_armed = 1;
        return;
    }
    for (i = 0; i < 4; i++)
        if (wd65_sh_imem[i] != im[(0x1000 >> 2) + i]) { imem_changed = 1; break; }
    for (i = 0; i < 16; i++)
        if (wd65_sh_hdr[i] != im[(0xfc0 >> 2) + i]) { hdr_changed = 1; break; }
    if (!imem_changed && !hdr_changed)
        return;
    if (wd65_watch_lines < 200 &&
        (f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_watch65.txt", "a")) != NULL)
    {
        wd65_watch_lines++;
        fprintf(f, "WATCH site=%u gpc=%08x sppc=%04x st=%08x spdma=%u\n",
                site, (uint32_t)*r4300_pc(sp->mi->r4300),
                (uint32_t)sp->regs2[SP_PC_REG], (uint32_t)sp->regs[SP_STATUS_REG],
                wd_c_spdma);
        if (imem_changed)
        {
            fprintf(f, "  IMEM");
            for (i = 0; i < 4; i++)
                fprintf(f, " [%u]%08x->%08x", i, wd65_sh_imem[i], im[(0x1000 >> 2) + i]);
            fprintf(f, "\n");
        }
        if (hdr_changed)
        {
            fprintf(f, "  HDR");
            for (i = 0; i < 16; i++)
                if (wd65_sh_hdr[i] != im[(0xfc0 >> 2) + i])
                    fprintf(f, " [%03x]%08x->%08x", 0xfc0 + i * 4,
                            wd65_sh_hdr[i], im[(0xfc0 >> 2) + i]);
            fprintf(f, "\n");
        }
        /* ROUND-67: the last 64 qualifying guest stores (gpc/addr/val/mask),
         oldest first, so a transition is matched against its writers even
         when the CPUW file cap or filter hid them. */
        {
            uint32_t n64 = wd65w_n < 64 ? wd65w_n : 64;
            fprintf(f, "  RING");
            for (i = 0; i < n64; i++)
            {
                const uint32_t* e = wd65w_ring[(wd65w_n - n64 + i) & 63u];
                fprintf(f, "\n    gpc=%08x addr=%08x val=%08x mask=%08x",
                        e[0], e[1], e[2], e[3]);
            }
            fprintf(f, "\n");
        }
        fclose(f);
    }
    for (i = 0; i < 4; i++)
        wd65_sh_imem[i] = im[(0x1000 >> 2) + i];
    for (i = 0; i < 16; i++)
        wd65_sh_hdr[i] = im[(0xfc0 >> 2) + i];
}

static void do_sp_dma(struct rsp_core* sp, const struct sp_dma* dma)
{
    unsigned int i,j;

    unsigned int l = dma->length;

    unsigned int length = ((l & 0xfff) | 7) + 1;
    unsigned int count = ((l >> 12) & 0xff) + 1;
    unsigned int skip = ((l >> 20) & 0xfff);

    unsigned int memaddr = dma->memaddr & 0xff8;
    unsigned int dramaddr = dma->dramaddr & 0xfffff8;

    /* 64DD ROUTE ONLY (runtime gate; plain carts keep the original code path
       byte-for-byte).  Two things differ from upstream on this route:

       (1) BANK SELECTION.  `memaddr` above has already stripped the low 12
       bits of SP_MEM_ADDR (`& 0xff8`), which drops bit 12 -- the DMEM/IMEM
       bank selector.  Upstream re-adds it through `spmem`.  The round-6 edit
       set `spmem = sp->mem` and masked the offset with `& 0x1fff` instead,
       which can never select IMEM because bit 12 is already gone: EVERY
       SP DMA on the DD route landed in DMEM.  Measured live: the 64DD audio
       task's boot-ucode load (SP_MEM_ADDR=0x04001000, 4096 bytes) wrote the
       ucode over DMEM 0x000..0xFFF -- wiping the 64-byte OSTask header the
       preceding osSpTaskLoad DMA had just put at DMEM 0xFC0 (type 2 ->
       0x00010001 afterwards, DMEM[0] became the ucode's first word
       0x340A0FC0) -- while IMEM stayed zero.  do_SP_Task then classified the
       task as garbage, the RSP executed NOPs, and the guest's audio task
       never completed.

       (2) WRAP.  A transfer must wrap inside the selected 4 KiB bank rather
       than walking into the neighboring bank (upstream lets it run past the
       end).  Masking each access with `& 0xfff` reproduces the hardware wrap;
       for every DMA observed so far the offset stays below 0x1000 anyway, so
       this only matters for a whole-bank load that starts mid-bank. */
    unsigned char *spmem = (unsigned char*)sp->mem + (dma->memaddr & 0x1000);
    unsigned char *dram = (unsigned char*)sp->ri->rdram->dram;
    /* ROUND 11 DIAG: absolute RSP-memory offset of this transfer (0..0x1FFF,
       DMEM low bank / IMEM high bank) so the DMA log can name the destination
       bank without re-deriving it from the two masked values. */
    unsigned int dstbase = (dma->memaddr & 0x1000) | (memaddr & 0xfff);
    /* ROUND-7 DD DIAG (see wd_hdr_* above): a DRAM->RSP DMA whose DMEM
       destination range covers 0xFC0 is the osSpTaskLoad header copy. */
    int wd_is_hdr = 0;
    if (g_dev.dd.idisk != NULL)
    {
        wd_c_spdma++;
        /* ROUND 35: does this transfer touch the low RDRAM window?  The guest's
           exception vector lives at RDRAM 0x180 and is clobbered with RSP
           microcode in the round-33 dump (see the round-35 block in
           cached_interp.c).  A transfer whose SOURCE or DESTINATION lands in
           the window is the writer; record it with the guest PC. */
        if (wd_low_range(dma->dramaddr, length * count) ||
            wd_low_range(dramaddr, length * count))
            wd_low_note("SP", dma->dramaddr, dma->memaddr, length * count,
                        (uint32_t)*r4300_pc(sp->mi->r4300));
        wd_lx_add((dma->dir == SP_DMA_READ) ? 1u : 2u, dma->dramaddr,
                  dma->memaddr, length * count, (uint32_t)*r4300_pc(sp->mi->r4300));
        if (dma->dir != SP_DMA_READ && (dma->memaddr & 0x1000) == 0
            && (dma->memaddr & 0xfff) <= 0xfc0
            && ((dma->memaddr & 0xfff) + length) > 0xfc0)
        {
            unsigned int k;
            wd_is_hdr = 1;
            wd_hdr_n++;
            wd_hdr_dram = dma->dramaddr;
            wd_hdr_mem = dma->memaddr;
            wd_hdr_len = l;
            wd_hdr_seq = wd_c_spdma;
            for (k = 0; k < 16; k++)
                wd_hdr_src[k] = ((uint32_t*)(void*)dram)[((dma->dramaddr & 0x7fffff) >> 2) + k];
            /* ROUND 22: publish this load's header to the plugin (see
               wd_cur_hdr above).  Captured here, at the DMA, because the DMEM
               copy the ucode sees at 0xFC0 does not survive the first slice. */
            for (k = 0; k < 16; k++)
                wd_cur_hdr[k] = wd_hdr_src[k];
            wd_cur_hdr_seq++;
            /* ROUND 13: a header copy IS a task load (libultra's osSpTaskLoad
               DMAs the 64-byte OSTask into DMEM 0xFC0).  Classify by the type
               word read from the SOURCE, before the ucode can clobber it, and
               keep the last 32 loads with the ucode/data pointers that name the
               task (gspF3DEX2_fifoTextStart=0x807505C0 / gspF3DEX2_fifoDataStart
               =0x80779860 for gfx, aspMainTextStart=0x80768E60 for audio). */
            {
                uint32_t ty = wd_hdr_src[0] & 3u;
                uint32_t* e = wd_hdr_ring[wd_hdr_ring_n & 31u];
                wd_hdr_type_n[ty]++;
                if (ty == 1) wd_c_gfx_load++;
                else if (ty == 2) wd_c_audio_load++;
                e[0] = wd_hdr_seq;
                e[1] = ty;
                e[2] = wd_hdr_src[4];   /* ucode                        */
                e[3] = wd_hdr_src[6];   /* ucode_data                   */
                e[4] = wd_hdr_src[12];  /* data_ptr                     */
                e[5] = (uint32_t)sp->regs[SP_STATUS_REG];
                e[6] = (uint32_t)*r4300_pc(sp->mi->r4300);
                e[7] = wd_hdr_src[0];   /* raw type word                */
                wd_hdr_ring_n++;
            }
        }
    }
    if (dma->dir == SP_DMA_READ)
    {
        for(j=0; j<count; j++) {
            for(i=0; i<length; i++) {
                if (g_dev.dd.idisk != NULL)
                    dram[(dramaddr & 0x7fffff)^S8] = spmem[(memaddr & 0xfff)^S8];
                else
                    dram[dramaddr^S8] = spmem[memaddr^S8];
                memaddr++;
                dramaddr++;
            }

            post_framebuffer_write(&sp->dp->fb, dramaddr - length, length);
            dramaddr+=skip;
        }
    }
    else
    {
        for(j=0; j<count; j++) {
            pre_framebuffer_read(&sp->dp->fb, dramaddr);

            for(i=0; i<length; i++) {
                if (g_dev.dd.idisk != NULL)
                    spmem[(memaddr & 0xfff)^S8] = dram[(dramaddr & 0x7fffff)^S8];
                else
                    spmem[memaddr^S8] = dram[dramaddr^S8];
                memaddr++;
                dramaddr++;
            }
            dramaddr+=skip;
        }
    }

    /* ROUND 11 64DD DIAG (DD-gated): identify whatever destroys the RSP
       program on the post-load 64DD route.

       Measured at the round-10 stall (RP6): IMEM[0..] and the DMEM 0xFC0 task
       header both read as 0x00010001 -- the game's cleared-buffer fill pattern
       (16-bit value 1), i.e. a cleared RDRAM buffer was copied into RSP memory,
       or a fill ran off the end of an RDRAM buffer into the SP memory mapping.
       Log only the transfers that can do that: a DRAM->RSP DMA whose
       destination covers the task header (DMEM 0xFC0) or the low IMEM page
       (the ucode load), plus any transfer whose source starts with the fill
       pattern.  Everything else (the thousands of ordinary audio/gfx DMAs)
       stays silent. */
    if (g_dev.dd.idisk != NULL)
    {
        const uint32_t* srcw = (const uint32_t*)(const void*)dram;
        unsigned int srcoff = (dma->dramaddr & 0x7fffff) >> 2;
        uint32_t s0 = srcw[srcoff], s1 = srcw[srcoff + 1];
        int is_hdr = (dma->dir != SP_DMA_READ) && dstbase < 0x1000
                     && dstbase <= 0xfc0 && (dstbase + length) > 0xfc0;
        int is_lowimem = (dma->dir != SP_DMA_READ) && dstbase >= 0x1000
                         && dstbase < 0x1100;
        if (is_hdr || is_lowimem || s0 == 0x00010001u)
        {
            static unsigned d2_n = 0;
            if (d2_n < 4000)
            {
                static int d2_first = 1;
                FILE* df = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_dma2.txt",
                                 d2_first ? "w" : "a");
                d2_first = 0;
                if (df)
                {
                    d2_n++;
                    fprintf(df, "WDDMA2 n=%u dir=%u dst=%04x dram=%08x len=%08x src=%08x %08x"
                                " | hdr=%d imem=%d after: fc0=%08x mem0=%08x imem0=%08x\n",
                            d2_n, (unsigned)dma->dir, dstbase, (unsigned)dma->dramaddr, l, s0, s1,
                            is_hdr, is_lowimem,
                            ((const uint32_t*)sp->mem)[0xfc0 / 4],
                            ((const uint32_t*)sp->mem)[0],
                            ((const uint32_t*)sp->mem)[0x1000 / 4]);
                    if ((d2_n & 0x3f) == 0) fflush(df);
                    fclose(df);
                }
            }
        }
    }

    /* ROUND 19: did THIS transfer write the fill pattern into IMEM?  (path 2) */
    if (g_dev.dd.idisk != NULL)
        wd_imem_probe(sp, 2, dma->dir, dma->memaddr, dma->dramaddr, l);

    /* schedule end of dma event */
    cp0_update_count(sp->mi->r4300);
    add_interrupt_event(&sp->mi->r4300->cp0, RSP_DMA_EVT, (count * length) / 8);

    if (wd_is_hdr)
    {
        unsigned int k;
        for (k = 0; k < 16; k++)
            wd_hdr_dst[k] = ((uint32_t*)sp->mem)[0xfc0 / 4 + k];
    }

    /* ROUND-7 DD DIAG: full DMA history with the DMEM 0xFC0 word after each
       transfer, so it is unambiguous which DMA (if any) changes the task
       header.  Also prints the header word the PLUGIN would see through its
       own pointer (mem_base_u32(g_mem_base, MM_RSP_MEM)) in case the core's
       sp->mem and the plugin's DMEM are not the same buffer. */
    if (g_dev.dd.idisk != NULL)
    {
        static unsigned dma_log_n = 0;
        if (dma_log_n < 48)
        {
            static int dma_first = 1;
            FILE* df = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_dma.txt",
                             dma_first ? "w" : "a");
            dma_first = 0;
            if (df)
            {
                const uint32_t* plug_dmem = (const uint32_t*)mem_base_u32(g_mem_base, MM_RSP_MEM);
                dma_log_n++;
                fprintf(df, "WDDMA n=%u seq=%u dir=%u ishdr=%d mem=%08x dram=%08x len=%08x "
                            "count=%u length=%u skip=%u | after: core_fc0=%08x plug_fc0=%08x "
                            "core_imem0=%08x plug_imem0=%08x dmem0=%08x\n",
                        dma_log_n, (unsigned)wd_c_spdma, (unsigned)dma->dir, wd_is_hdr,
                        (unsigned)dma->memaddr, (unsigned)dma->dramaddr, l,
                        count, length, skip,
                        ((const uint32_t*)sp->mem)[0xfc0 / 4],
                        plug_dmem ? plug_dmem[0xfc0 / 4] : 0,
                        ((const uint32_t*)sp->mem)[0x1000 / 4],
                        plug_dmem ? plug_dmem[0x1000 / 4] : 0,
                        ((const uint32_t*)sp->mem)[0]);
                fclose(df);
            }
        }
    }
}

static void fifo_push(struct rsp_core* sp, uint32_t dir)
{
    /* ROUND-66 DIAG (DD route, files/wd_fifo66.txt, capped): who drives the
       core's guest-MMIO SP DMA fifo during the frozen phase.  Round 65 found
       the core's D-lines and the plugin's ucode-side DMA log showing the same
       repeating cycle; this tags each push with the guest PC and the fifo
       state so the two streams can be told apart (guest loader loop vs
       something re-running the fifo). */
    if (g_dev.dd.idisk != NULL)
    {
        static FILE* wd66_fifo_f = NULL;
        static unsigned wd66_fifo_lines = 0;
        if (wd66_fifo_lines < 512)
        {
            wd66_fifo_lines++;
            if (wd66_fifo_f == NULL)
                wd66_fifo_f = fopen(WD65_FILES_DIR "wd_fifo66.txt", "a");
            if (wd66_fifo_f != NULL)
            {
                fprintf(wd66_fifo_f, "PUSH dir=%u pc=%08x mem=%08x dram=%08x len=%08x busy=%u full=%u spdma=%u\n",
                        dir, (uint32_t)*r4300_pc(sp->mi->r4300),
                        sp->regs[SP_MEM_ADDR_REG], sp->regs[SP_DRAM_ADDR_REG],
                        dir == SP_DMA_READ ? sp->regs[SP_WR_LEN_REG] : sp->regs[SP_RD_LEN_REG],
                        sp->regs[SP_DMA_BUSY_REG], sp->regs[SP_DMA_FULL_REG],
                        wd_c_spdma);
                fflush(wd66_fifo_f);
            }
        }
    }
    if (sp->regs[SP_DMA_FULL_REG])
    {
        DebugMessage(M64MSG_WARNING, "RSP DMA attempted but FIFO queue already full.");
        return;
    }

    if (sp->regs[SP_DMA_BUSY_REG])
    {
        sp->fifo[1].dir = dir;
        sp->fifo[1].length = dir == SP_DMA_READ ? sp->regs[SP_WR_LEN_REG] : sp->regs[SP_RD_LEN_REG];
        sp->fifo[1].memaddr = sp->regs[SP_MEM_ADDR_REG];
        sp->fifo[1].dramaddr = sp->regs[SP_DRAM_ADDR_REG];
        sp->regs[SP_DMA_FULL_REG] = 1;
        sp->regs[SP_STATUS_REG] |= SP_STATUS_DMA_FULL;
    }
    else
    {
        sp->fifo[0].dir = dir;
        sp->fifo[0].length = dir == SP_DMA_READ ? sp->regs[SP_WR_LEN_REG] : sp->regs[SP_RD_LEN_REG];
        sp->fifo[0].memaddr = sp->regs[SP_MEM_ADDR_REG];
        sp->fifo[0].dramaddr = sp->regs[SP_DRAM_ADDR_REG];
        sp->regs[SP_DMA_BUSY_REG] = 1;
        sp->regs[SP_STATUS_REG] |= SP_STATUS_DMA_BUSY;

        do_sp_dma(sp, &sp->fifo[0]);
    }
}

static void fifo_pop(struct rsp_core* sp)
{
    if (sp->regs[SP_DMA_FULL_REG])
    {
        sp->fifo[0].dir = sp->fifo[1].dir;
        sp->fifo[0].length = sp->fifo[1].length;
        sp->fifo[0].memaddr = sp->fifo[1].memaddr;
        sp->fifo[0].dramaddr = sp->fifo[1].dramaddr;
        sp->regs[SP_DMA_FULL_REG] = 0;
        sp->regs[SP_STATUS_REG] &= ~SP_STATUS_DMA_FULL;

        do_sp_dma(sp, &sp->fifo[0]);
    }
    else
    {
        sp->regs[SP_DMA_BUSY_REG] = 0;
        sp->regs[SP_STATUS_REG] &= ~SP_STATUS_DMA_BUSY;
    }
}

static void update_sp_status(struct rsp_core* sp, uint32_t w)
{
    /* ROUND 13 DD DIAG (counters only): the EK frame protocol hands the RSP a
       gfx task, asks it to yield (SP_SET_SIG0/SIG1 forms), resumes it, and only
       then expects the DP interrupt.  Record the SP_STATUS write stream so a
       stall dump shows whether the guest ever gets past `osSpTaskStartGo`, and
       whether the SIG0/SIG1 yield handshake happens at all. */
    if (g_dev.dd.idisk != NULL)
    {
        uint32_t* e = wd_spw_ring[wd_spw_ring_n & 15u];
        wd_c_sp_status_wr++;
        if (w & 0xc00u) wd_c_sp_sig_wr++;   /* clear/set SIG0 (YIELD)   */
        if (w & 0x3000u) wd_c_sp_sig_wr++;  /* clear/set SIG1 (YIELDED) */
        e[0] = w;
        e[1] = (uint32_t)*r4300_pc(sp->mi->r4300);
        wd_spw_ring_n++;
    }
    /* ROUND-7 DD DIAG: identify the guest code that drives the 20 RSP
       starts/second.  osSpTaskLoad and osSpTaskStartGo are the only libultra
       functions that write SP_STATUS in the clear-halt/clear-broke form, and
       the SP DMA trace shows osSpTaskLoad ran exactly ONCE per run while the
       RSP is re-entered 20x/s -> either the guest is calling osSpTaskStartGo
       in a loop or something else writes this register.  Log guest pc/ra/sp/
       a0..a3 plus the register state around every SP_STATUS write, capped. */
    if (g_dev.dd.idisk != NULL)
    {
        static unsigned spw_n = 0;
        static unsigned spw_tick = 0;
        spw_tick++;
        if (spw_n < 700 && (spw_n < 400 || (spw_tick % 4000) == 0))
        {
            static int spw_first = 1;
            FILE* wf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_spw.txt",
                             spw_first ? "w" : "a");
            spw_first = 0;
            if (wf)
            {
                int64_t* gpr = r4300_regs(sp->mi->r4300);
                spw_n++;
                fprintf(wf, "WDSPW n=%u pc=%08x ra=%08x sp=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
                            "w=%08x st_before=%08x st_after=%08x dmem_fc0=%08x dmas=%u hdr=%u\n",
                        spw_n, (uint32_t)*r4300_pc(sp->mi->r4300), (uint32_t)gpr[31], (uint32_t)gpr[29],
                        (uint32_t)gpr[4], (uint32_t)gpr[5], (uint32_t)gpr[6], (uint32_t)gpr[7],
                        w, sp->regs[SP_STATUS_REG],
                        (sp->regs[SP_STATUS_REG] & ~0x1u) | ((w & 0x1) ? 0 : 1),
                        ((uint32_t*)sp->mem)[0xfc0 / 4], (unsigned)wd_c_spdma, (unsigned)wd_hdr_n);
                fclose(wf);
            }
        }
    }
    /* clear / set halt */
    if (w & 0x1) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_HALT;
    if (w & 0x2) sp->regs[SP_STATUS_REG] |= SP_STATUS_HALT;

    /* ROUND 25, 64DD ROUTE ONLY: clearing HALT is what libultra's
       osSpTaskStartGo() does (SP_SET_INTR_BREAK|SP_CLR_SSTEP|SP_CLR_BROKE|
       SP_CLR_HALT), so it ends the task-load window opened by
       __osSpSetPc(SP_IMEM_START) -- clear the guard here as well as in
       do_SP_Task, because the early returns below (a locked task with an
       SP_INT event already queued) can skip do_SP_Task entirely and the flag
       would then only expire on the pump's 250 ms backstop.  The guest has
       finished loading either way: whoever runs the RSP next is running the
       task the guest just started. */
    if ((w & 0x1) && g_dev.dd.idisk != NULL)
        sp->rsp_task_load_pending = 0;

    /* clear broke */
    if (w & 0x4) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_BROKE;

    /* clear SP interrupt */
    if (w & 0x8)
    {
        clear_rcp_interrupt(sp->mi, MI_INTR_SP);
    }
    /* set SP interrupt */
    if (w & 0x10)
    {
        signal_rcp_interrupt(sp->mi, MI_INTR_SP);
    }

    /* clear / set single step */
    if (w & 0x20) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SSTEP;
    if (w & 0x40) sp->regs[SP_STATUS_REG] |= SP_STATUS_SSTEP;

    /* clear / set interrupt on break */
    if (w & 0x80) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_INTR_BREAK;
    if (w & 0x100) sp->regs[SP_STATUS_REG] |= SP_STATUS_INTR_BREAK;

    /* clear / set signal 0 */
    if (w & 0x200) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG0;
    if (w & 0x400) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG0;

    /* clear / set signal 1 */
    if (w & 0x800) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG1;
    if (w & 0x1000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG1;

    /* clear / set signal 2 */
    if (w & 0x2000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG2;
    if (w & 0x4000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG2;

    /* clear / set signal 3 */
    if (w & 0x8000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG3;
    if (w & 0x10000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG3;

    /* clear / set signal 4 */
    if (w & 0x20000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG4;
    if (w & 0x40000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG4;

    /* clear / set signal 5 */
    if (w & 0x80000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG5;
    if (w & 0x100000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG5;

    /* clear / set signal 6 */
    if (w & 0x200000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG6;
    if (w & 0x400000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG6;

    /* clear / set signal 7 */
    if (w & 0x800000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG7;
    if (w & 0x1000000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG7;

    if (sp->rsp_task_locked && (get_event(&sp->mi->r4300->cp0.q, SP_INT))) return;
    if (!(w & 0x1) && !(w & 0x4) && !sp->rsp_task_locked)
        return;

    if (!(sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)))
        do_SP_Task(sp);
}

void init_rsp(struct rsp_core* sp,
              uint32_t* sp_mem,
              struct mi_controller* mi,
              struct rdp_core* dp,
              struct ri_controller* ri)
{
    sp->mem = sp_mem;
    sp->mi = mi;
    sp->dp = dp;
    sp->ri = ri;
}

void poweron_rsp(struct rsp_core* sp)
{
    memset(sp->mem, 0, SP_MEM_SIZE);
    memset(sp->regs, 0, SP_REGS_COUNT*sizeof(uint32_t));
    memset(sp->regs2, 0, SP_REGS2_COUNT*sizeof(uint32_t));
    memset(sp->fifo, 0, SP_DMA_FIFO_SIZE*sizeof(struct sp_dma));

    sp->rsp_task_locked = 0;
    sp->rsp_task_load_pending = 0;   /* ROUND 25 task-load guard */
    sp->rsp_task_load_since_ms = 0;
    sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
    sp->regs[SP_STATUS_REG] = 1;
}


void read_rsp_mem(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t addr = rsp_mem_address(address);

    *value = sp->mem[addr];
}

void write_rsp_mem(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t addr = rsp_mem_address(address);

    /* ROUND 11 64DD DIAG (DD-gated, IN-MEMORY ONLY -- no file I/O here: this
       handler is on the guest's hot path and a prior round measured that
       hot-path I/O changes the emulation's timing enough to move the freeze).

       The CPU can write SP DMEM/IMEM directly through 0xA4000000..0xA4001FFF
       and `addr` spans BOTH banks (0..0x1FFF).  A stray buffer fill that lands
       here destroys the RSP program: at the round-10 stall IMEM[0..] read as
       0x00010001 (the game's cleared-buffer fill pattern) with the plugin and
       core pointers provably identical, i.e. something wrote the pattern into
       SP memory without an SP DMA.  Count and latch such writes; the watchdog
       stall dump prints the latches. */
    if (g_dev.dd.idisk != NULL)
    {
        extern volatile uint32_t wd_cpuw_n, wd_cpuw_imem_n, wd_cpuw_fill_n;
        extern uint32_t wd_cpuw_latch_addr, wd_cpuw_latch_val, wd_cpuw_latch_mask;
        extern uint32_t wd_cpuw_last_addr, wd_cpuw_last_val, wd_cpuw_last_mask;
        extern uint32_t wd_cpuw_imem_latch_addr, wd_cpuw_imem_latch_val;
        uint32_t eff = value & mask;

        wd_cpuw_n++;
        wd_cpuw_last_addr = address;
        wd_cpuw_last_val = value;
        wd_cpuw_last_mask = mask;
        /* ROUND 19: `addr` is a WORD index into the 0x2000-byte SP memory
           (rsp_mem_address = (address & 0x1fff) >> 2), so the old `addr >=
           0x1000` test could never fire and wd_cpuw_imem_n stayed 0 for every
           run: the IMEM bank is word index 0x400..0x7ff. */
        if (addr >= (0x1000 >> 2))
        {
            wd_cpuw_imem_n++;
            if (wd_cpuw_imem_latch_addr == 0)
            {
                wd_cpuw_imem_latch_addr = address;
                wd_cpuw_imem_latch_val = value;
            }
        }
        /* ROUND 65/67: announce every guest store that lands in IMEM (the
           ONLY uncontained IMEM writer in the tree: the plugin's DMA
           bank-wraps, the ucode's stores mask to DMEM, and do_sp_dma is
           bank-contained on this route) or in the DMEM header region (word
           idx 0x3F0..0x3FF = DMEM 0xFC0..0xFFF).  ROUND 67: the r66 filter
           (gpc >= 0x80000000) was WRONG -- it excluded the second boot's
           IPL3-style code, which executes FROM SP DMEM at 0xA4000xxx and is
           the one guest code class proven to write IMEM (r65: 2,319 stores,
           gpc=0xa4000068..0xa40007e0).  Log everything again; the RAM ring
           below is what the WATCH transition dump reads, so the hot path
           stays I/O-free and the file write happens only on real events. */
        if (addr >= (0xfc0 >> 2))
        {
            uint32_t wd65_gpc = (uint32_t)*r4300_pc(sp->mi->r4300);
            /* ring of the last 64 qualifying stores (dumped on a WATCH
               transition by wd65_ring_flush) */
            wd65w_ring[wd65w_n & 63u][0] = wd65_gpc;
            wd65w_ring[wd65w_n & 63u][1] = address;
            wd65w_ring[wd65w_n & 63u][2] = value;
            wd65w_ring[wd65w_n & 63u][3] = mask;
            wd65w_n++;
            if (wd65_cpuw_lines < 8192)
            {
                static FILE* wd65_cpuw_f = NULL;
                wd65_cpuw_lines++;
                if (wd65_cpuw_f == NULL)
                    wd65_cpuw_f = fopen(WD65_FILES_DIR "wd_cpuw65.txt", "a");
                if (wd65_cpuw_f != NULL)
                {
                    fprintf(wd65_cpuw_f, "CPUW gpc=%08x addr=%08x widx=%03x val=%08x mask=%08x eff=%08x\n",
                            wd65_gpc, address, addr, value, mask, eff);
                    fflush(wd65_cpuw_f);
                }
            }
        }
        if (eff == 0x00000001u || eff == 0x00010000u || eff == 0x00010001u)
        {
            wd_cpuw_fill_n++;
            if (wd_cpuw_latch_addr == 0)
            {
                wd_cpuw_latch_addr = address;
                wd_cpuw_latch_val = value;
                wd_cpuw_latch_mask = mask;
            }
        }
    }

    masked_write(&sp->mem[addr], value, mask);

    /* ROUND 19: this direct CPU write is path 1 (see wd_imem_probe). */
    if (g_dev.dd.idisk != NULL)
        wd_imem_probe(sp, 1, address, value, mask, addr);
}


void read_rsp_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg(address);

    *value = sp->regs[reg];

    if (reg == SP_SEMAPHORE_REG)
    {
        sp->regs[SP_SEMAPHORE_REG] = 1;
    }
}

void write_rsp_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg(address);

    switch(reg)
    {
    case SP_STATUS_REG:
        update_sp_status(sp, value & mask);
    case SP_DMA_FULL_REG:
    case SP_DMA_BUSY_REG:
        return;
    }

    masked_write(&sp->regs[reg], value, mask);

    switch(reg)
    {
    case SP_RD_LEN_REG:
        fifo_push(sp, SP_DMA_WRITE);
        break;
    case SP_WR_LEN_REG:
        fifo_push(sp, SP_DMA_READ);
        break;
    case SP_SEMAPHORE_REG:
        sp->regs[SP_SEMAPHORE_REG] = 0;
        break;
    }
}


void read_rsp_regs2(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg2(address);

    *value = sp->regs2[reg];
}

void write_rsp_regs2(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg2(address);

    masked_write(&sp->regs2[reg], value, mask);

    /* ROUND 25, 64DD ROUTE ONLY — the guest is loading a task, so the RSP is
       NOT ours to run until the guest starts it.

       libultra's osSpTaskLoad() (decomp src/libultra/io/sptask.c, read this
       round) is, in order:

           __osSpSetStatus(SP_CLR_YIELD|SP_CLR_YIELDED|SP_CLR_TASKDONE|SP_SET_INTR_BREAK);
           while (__osSpSetPc(SP_IMEM_START) == -1) {}          <-- HALT must be set
           while (__osSpRawStartDma(1, SP_IMEM_START - 0x40, tp, 0x40) == -1) {}  <-- header -> DMEM 0xFC0
           while (__osSpDeviceBusy()) {}
           while (__osSpRawStartDma(1, SP_IMEM_START, tp->t.ucode_boot, ...) == -1) {} <-- boot ucode -> IMEM 0x000

       and osSpTaskStartGo() only then writes SP_STATUS to start it.  In that
       window the *outgoing* ucode must not execute: on hardware it cannot
       (the RSP is halted and its PC was just set), but rsp_dd_background_pump()
       below clears SP_STATUS_HALT unconditionally and calls do_SP_Task -- so
       between the boot-ucode DMA and StartGo the previous ucode was free to
       run with IMEM 0x000 already replaced by the boot ucode, i.e. to execute
       rspboot's bytes with its own pc and registers, and to keep rewriting
       DMEM 0xFC0..0xFFF (measured this round: the audio ucode at 0x768E60
       owns DMEM 0x2E0/0xFD0/0xFF0 as scratch while it runs).

       Both consequences are exactly the failures this session has chased:

         * rspboot reads the ucode base from DMEM 0xFD0 (IMEM 008 `lw $2,16($1)`)
           and loads the text from it -- a clobbered 0xFD0 is the measured
           `R19IMEM n=5 dst=1080 src=6f0000`;
         * a yielded task's base comes from the GUEST side too:
           osSpTaskLoad does, for flags&OS_TASK_YIELDED,
               t.ucode_data = t.yield_data_ptr;
               t.ucode = IO_READ(yield_data_ptr + OS_YIELD_DATA_SIZE - 4);
           i.e. from the LAST WORD of the yield buffer, which the ucode's own
           yield handler wrote from DMEM 0xBFC -- which it had loaded from
           DMEM 0xFD0.  A clobbered header therefore poisons the yield buffer
           and every later resume, permanently (the round-18 runaway DMA).

       Setting the flag here is safe: SP_PC_REG is only ever written through
       this handler by the guest (do_SP_Task pokes regs2[SP_PC_REG] directly,
       and savestates write the field), and the only guest means of writing
       0x1000|0 is __osSpSetPc(SP_IMEM_START).  Plain carts never set it:
       g_dev.dd.idisk is NULL off the DD route. */
    if (reg == SP_PC_REG && g_dev.dd.idisk != NULL && (sp->regs2[reg] & 0x1fffu) == 0x1000u)
    {
        if (!sp->rsp_task_load_pending)
        {
            extern volatile uint32_t wd_c_loadguard_set;
            struct timespec now;
            wd_c_loadguard_set++;
            clock_gettime(CLOCK_MONOTONIC, &now);
            sp->rsp_task_load_since_ms =
                (uint64_t)now.tv_sec * 1000ull + (uint64_t)(now.tv_nsec / 1000000);
        }
        sp->rsp_task_load_pending = 1;
    }
}

extern volatile uint32_t wd_c_do_sp_task;
extern volatile uint32_t wd_c_sp_int_evt;

void do_SP_Task(struct rsp_core* sp)
{
    if (g_dev.dd.idisk != NULL) wd_c_do_sp_task++;

    /* ROUND 25, 64DD ROUTE ONLY — enforce the task-load window (see
       write_rsp_regs2 above for the whole argument).

       * Called by the background pump while the guest is mid-osSpTaskLoad:
         the RSP is the guest's until StartGo, so refuse the slice outright and
         leave SP_STATUS.HALT alone (the guest's `while (__osSpSetPc(...) == -1)`
         loop depends on it).
       * Called by the guest (update_sp_status, i.e. osSpTaskStartGo or an SP
         handler ack): the load is over, clear the flag and run normally.

       The pump's caller has already checked the flag, so the refusal here is
       belt-and-braces for the one path that does not go through it. */
    if (g_dev.dd.idisk != NULL)
    {
        if (wd_pump_call && sp->rsp_task_load_pending)
        {
            wd_c_loadguard_skip++;
            return;
        }
        sp->rsp_task_load_pending = 0;
    }

    if (g_dev.dd.idisk != NULL)
    {
        /* ROUND 13: which task type the core *thinks* it is running.  The
           header is clobbered by the ucode after the first slice, so this is a
           lower bound per task -- the authoritative per-task classification is
           wd_hdr_type_n[] above (taken from the task-load DMA source). */
        uint32_t et = ((uint32_t*)sp->mem)[0xfc0 / 4];
        wd_c_task_etype[et & 3u]++;
    }
    uint32_t save_pc = sp->regs2[SP_PC_REG] & ~0xfff;
    /* Entry SP_STATUS — distinguish "ucode yielded during THIS run" (was
       running at entry, HALT set by the plugin's poll-yield at exit) from
       "ucode already halted before entry" (doRspCycles returns 0 without
       running, so we must NOT re-deliver a stale interrupt). */
    uint32_t wd_status_entry = sp->regs[SP_STATUS_REG];
    wd65_watch_check(sp, 0);   /* round 65: shadow vs. everything before this slice */

    /* ROUND-7 DD DIAG (see wd_hdr_* above): compare the header DMEM 0xFC0
       against what the last header DMA actually wrote there.  `same` means
       the copy landed and survived (so the guest handed us this header);
       `diff` means something overwrote it after the DMA (the ucode itself,
       which is the known F3DEX behavior), and `hdr_n == 0` means no header
       DMA ever reached DMEM 0xFC0 at all. */
    if (g_dev.dd.idisk != NULL)
    {
        static unsigned hdr_log_n = 0;
        static unsigned hdr_tick = 0;
        uint32_t cur[16];
        unsigned int k;
        int same = 1;
        hdr_tick++;
        for (k = 0; k < 16; k++)
        {
            cur[k] = ((uint32_t*)sp->mem)[0xfc0 / 4 + k];
            if (cur[k] != wd_hdr_dst[k]) same = 0;
        }
        if (same) wd_hdr_same++; else wd_hdr_diff++;
        if (hdr_log_n < 200 && (hdr_log_n < 64 || (hdr_tick % 2000) == 0))
        {
            static int hdr_first = 1;
            FILE* hf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_hdr.txt",
                             hdr_first ? "w" : "a");
            hdr_first = 0;
            if (hf)
            {
                hdr_log_n++;
                fprintf(hf, "WDHDR n=%u hdr_n=%u seq=%u dram=%08x mem=%08x len=%08x same=%u diff=%u dmas=%u type=%08x\n",
                        hdr_log_n, (unsigned)wd_hdr_n, (unsigned)wd_hdr_seq, wd_hdr_dram, wd_hdr_mem,
                        wd_hdr_len, (unsigned)wd_hdr_same, (unsigned)wd_hdr_diff, (unsigned)wd_c_spdma, cur[0]);
                fprintf(hf, "  dmem ");
                for (k = 0; k < 16; k++) fprintf(hf, "%08x ", cur[k]);
                fprintf(hf, "\n  dmasrc ");
                for (k = 0; k < 16; k++) fprintf(hf, "%08x ", wd_hdr_src[k]);
                fprintf(hf, "\n");
                fclose(hf);
            }
        }
    }

    uint32_t sp_delay_time;

    if (sp->mem[0xfc0/4] == 1)
    {
        unprotect_framebuffers(&sp->dp->fb);

        //gfx.processDList();
        sp->regs2[SP_PC_REG] &= 0xfff;
#if defined(PROFILE)
        timed_section_start(TIMED_SECTION_GFX);
#endif
        rsp.doRspCycles(0xffffffff);
#if defined(PROFILE)
        timed_section_end(TIMED_SECTION_GFX);
#endif
        sp->regs2[SP_PC_REG] |= save_pc;
        new_frame();

        if (sp->mi->regs[MI_INTR_REG] & MI_INTR_DP)
        {
            sp->mi->regs[MI_INTR_REG] &= ~MI_INTR_DP;
            if (sp->dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_FREEZE) {
                sp->dp->do_on_unfreeze |= DELAY_DP_INT;
            } else {
                cp0_update_count(sp->mi->r4300);
                add_interrupt_event(&sp->mi->r4300->cp0, DP_INT, 4000);
            }
        }
        sp_delay_time = 1000;

        protect_framebuffers(&sp->dp->fb);
    }
    else if (sp->mem[0xfc0/4] == 2)
    {
        //audio.processAList();
        sp->regs2[SP_PC_REG] &= 0xfff;
#if defined(PROFILE)
        timed_section_start(TIMED_SECTION_AUDIO);
#endif
        rsp.doRspCycles(0xffffffff);
#if defined(PROFILE)
        timed_section_end(TIMED_SECTION_AUDIO);
#endif
        sp->regs2[SP_PC_REG] |= save_pc;

        sp_delay_time = 4000;
    }
    else
    {
        sp->regs2[SP_PC_REG] &= 0xfff;
        rsp.doRspCycles(0xffffffff);
        sp->regs2[SP_PC_REG] |= save_pc;

        sp_delay_time = 0;
    }

    /* ROUND 10, 64DD ROUTE ONLY (g_dev.dd.idisk != NULL).

       The RDP completion interrupt must not depend on WHICH branch above ran.
       parallel-rdp raises MI_INTR_DP itself when it reaches the guest's
       gDPFullSync (mupen64plus-video-parallel/parallel_imp.cpp: `*gfx.MI_INTR_REG
       |= DP_INTERRUPT`), and it is reached from the RSP's `mtc0 CMD_END` ->
       ProcessRdpList.  The stock consumption above lives INSIDE the
       `sp->mem[0xfc0/4] == 1` branch, and that branch is chosen from DMEM 0xFC0
       at do_SP_Task ENTRY -- but F3DEX clobbers that header as soon as it runs.
       On the plain route that is harmless (one DoRspCycles call runs the task to
       completion, so the gfx branch that entered is the one that observes the
       kick).  On the 64DD route the task is executed in bounded slices fed by
       rsp_dd_background_pump(): the entry slice takes the gfx branch, the slice
       that finally reaches the ucode's DPC_END write reads a clobbered 0xFC0 and
       takes the audio/other branch -- so MI_INTR_DP is left set and NEVER turned
       into a CP0 DP_INT event.  Measured live: raise_bits DP=0 for an entire run
       while the EK's gfx thread sat blocked in osRecvMesg(&D_800DCAC8) waiting
       for exactly that DP event (its message var D_800DCD10 still held 0x29, the
       last D_800DCAB0 handoff), which froze the whole frame protocol and with it
       the DD loader's progress bar.

       Consuming it here is a no-op when the gfx branch already handled it (the
       bit is cleared there), so the stock path is bit-for-bit unchanged. */
    if (g_dev.dd.idisk != NULL && wd_dd_legacy() && (sp->mi->regs[MI_INTR_REG] & MI_INTR_DP))
    {
        /* ROUND 13: count every DP bit this block converts.  Zero here while
           wd_c_rdp_kick climbs would mean the RDP is being kicked but the
           video plugin is not reaching the guest's DPC_END. */
        wd_c_dp_consumed++;
        sp->mi->regs[MI_INTR_REG] &= ~MI_INTR_DP;
        if (sp->dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_FREEZE)
        {
            sp->dp->do_on_unfreeze |= DELAY_DP_INT;
        }
        else
        {
            cp0_update_count(sp->mi->r4300);
            add_interrupt_event(&sp->mi->r4300->cp0, DP_INT, 4000);
        }
    }

    if (g_dev.dd.idisk != NULL && !wd_dd_legacy())
    {
        /* ROUND 62 (default DD model): signal only the transition INTO
           halt/break.  With short slices the RSP is still running at the end
           of almost every call, so the stock rule ("not halted -> raise")
           fabricates one interrupt per slice -- measured on the RP6 as 6057
           SP interrupts for a single guest task, the storm the guest's SP
           handler then spins in. */
        int wd_was_running = (wd_status_entry & (SP_STATUS_HALT | SP_STATUS_BROKE)) == 0;
        int wd_stopped = (sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)) != 0;
        if (wd_stopped)
        {
            sp->rsp_task_locked = 0;
            sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
            if (wd_was_running)
                sp->mi->regs[MI_INTR_REG] |= MI_INTR_SP;
        }
        else
        {
            sp->rsp_task_locked = 1;
            sp->mi->r4300->cp0.interrupt_unsafe_state |= INTR_UNSAFE_RSP;
        }
    }
    else
    {
    sp->rsp_task_locked = 0;
    sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
    if ((sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)) == 0)
    {
        sp->rsp_task_locked = 1;
        sp->mi->r4300->cp0.interrupt_unsafe_state |= INTR_UNSAFE_RSP;
        /* ROUND 9, 64DD ONLY: an unfinished RSP task after a HOST-BUDGET yield
           must NOT interrupt the guest.  The guest's libultra SP handler
           acknowledges the interrupt and writes SP_STATUS, which re-enters
           do_SP_Task through update_sp_status and hands the RSP another full
           slice -- so the guest's entire CPU share was its own SP handler and
           the RSP still owned the emulation thread (measured on the RP6:
           c_task 500/s, guest __osRunQueue EMPTY with the prio-0 idle thread
           spinning at 0x806f32ec `b .`, CP0 COUNT advancing 134k/s = 0.3% of
           real speed, VI at 2/s).  Leaving the task locked but silent lets the
           CPU run at full speed while rsp_dd_background_pump() below feeds the
           RSP in slices -- the concurrency real hardware provides.  The genuine
           completion (ucode BREAK) still raises MI_INTR_SP via the stock
           rsp_interrupt_event path, which is what wakes the guest. */
        if (g_dev.dd.idisk == NULL)
            sp->mi->regs[MI_INTR_REG] |= MI_INTR_SP;
    }
    }
    /* DD-ONLY ares-derived completion/yield delivery (gated: plain cart
       games must keep the pre-ares rsp_interrupt_event path exactly).
       wd_sp_stock() switches the whole DD-only delivery off (see the flag's
       comment at the top of this file). */
    if (g_dev.dd.idisk != NULL && wd_dd_legacy())
    {
    /* ares-style SP_STATUS force-synchronize (ares n64/rsp/io.cpp: the RSP
       yields on its SP_STATUS signal-poll by setting INTR_BREAK|HALT + irq).
       That yield is the ucode asking the CPU to take over (post SIG0 / advance
       the task).  In the synchronous model the CPU only gets a turn if we
       RAISE the SP interrupt here.  Deliver it ONLY when the ucode actually
       yielded during this run: running at entry (HALT clear) and now
       HALT + INTR_BREAK (and NOT BROKE — a broken task is DONE, not a yield).
       Set rsp_task_locked so rsp_interrupt_event does NOT set TASKDONE (the
       ucode merely yielded, it did not finish). */
    if ((wd_status_entry & (SP_STATUS_HALT | SP_STATUS_BROKE)) == 0
        && !(sp->regs[SP_STATUS_REG] & SP_STATUS_BROKE)
        && (sp->regs[SP_STATUS_REG] & SP_STATUS_INTR_BREAK)
        && (sp->regs[SP_STATUS_REG] & SP_STATUS_HALT))
    {
        sp->rsp_task_locked = 1;
        sp->mi->r4300->cp0.interrupt_unsafe_state |= INTR_UNSAFE_RSP;
        sp->mi->regs[MI_INTR_REG] |= MI_INTR_SP;
    }
    /* ares-completion (F-Zero X EK audio task): a ucode that BROKE (not
       yielded) is genuinely DONE.  A clean break sets only BROKE|HALT, not
       INTR_BREAK, so rsp_interrupt_event's INTR_BREAK gate never fires and
       the CPU is never interrupted -> the game's audio-completion handler
       never runs and it never re-dispatches the next audio task (stall at the
       64DD IPL splash).  Set INTR_BREAK (so the raise fires), keep
       rsp_task_locked=0 (so rsp_interrupt_event sets TASKDONE for a genuinely
       done task), and raise MI_INTR_SP to deliver the completion interrupt. */
    if ((wd_status_entry & (SP_STATUS_HALT | SP_STATUS_BROKE)) == 0
        && (sp->regs[SP_STATUS_REG] & SP_STATUS_BROKE))
    {
        sp->regs[SP_STATUS_REG] |= SP_STATUS_INTR_BREAK;
        sp->rsp_task_locked = 0;
        sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
        sp->mi->regs[MI_INTR_REG] |= MI_INTR_SP;
    }
    }
    if (sp->mi->regs[MI_INTR_REG] & MI_INTR_SP)
    {
        cp0_update_count(sp->mi->r4300);
        add_interrupt_event(&sp->mi->r4300->cp0, SP_INT, sp_delay_time);
        sp->mi->regs[MI_INTR_REG] &= ~MI_INTR_SP;
    }

    /* ROUND 13, 64DD ROUTE ONLY — leave the RSP *HALTED* whenever a task is
       still in flight.

       libultra's osSpTaskLoad() begins with

           while (__osSpSetPc(SP_IMEM_START) == -1) {}
           s32 __osSpSetPc(u32 pc) { if (!(IO_READ(SP_STATUS_REG) & SP_STATUS_HALT)) return -1; ... }

       i.e. the guest will spin forever unless SP_STATUS_HALT is set.  On the
       stock route that is guaranteed by rsp_interrupt_event(), which sets
       TASKDONE|BROKE|HALT -- but only `if (!sp->rsp_task_locked)`, and on the
       64DD route every incomplete task is locked by design (host-budget yield:
       rsp_core.c above; ucode yield: the block above).  The unconditional
       stock clear below then wiped HALT and nothing ever set it again, so the
       guest deadlocked the whole machine in that one spin loop.

       Measured on the RP6 (round 13, wd_stall.txt): the dynarec block ring was
       2048/2048 blocks at 0x80746418 == osSpTaskLoad+0xbc, i.e. exactly this
       loop; CP0 COUNT froze with it, so no VI/AI/SP event ever fired again and
       rsp_dd_background_pump() (driven from the recompiler's interrupt hook)
       stopped with it -- a hard deadlock, not a slow-down.  SP_STATUS was
       0x00000040 (INTR_BREAK only, HALT clear) with the RSP parked at
       SP_PC=0x040015c0 inside the gfx ucode.

       HALT-set is also the truthful hardware state: the RSP is between slices,
       not executing.  TASKDONE stays clear (the task is NOT done) and BROKE
       stays clear (it did not break), so osSpTaskYielded()/the guest's state
       machine see an unfinished, resumable task; the resume path is the
       guest's own Sched_SpTaskResumeGfx() -> osSpTaskStart() (which clears
       HALT) or rsp_dd_background_pump(). */
    if (g_dev.dd.idisk != NULL && wd_dd_legacy() && sp->rsp_task_locked)
    {
        sp->regs[SP_STATUS_REG] &= ~(SP_STATUS_TASKDONE | SP_STATUS_BROKE);
        sp->regs[SP_STATUS_REG] |= SP_STATUS_HALT;
    }
    else
    {
        sp->regs[SP_STATUS_REG] &=
            ~(SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT);
    }
    wd65_watch_check(sp, 1);   /* round 65: did this slice's RSP run change IMEM or the header? */
}

/* 64DD ROUTE ONLY: background pump for an unfinished RSP task.

   The RSP runs synchronously inside do_SP_Task (i.e. on the emulation thread,
   inside whatever guest SP_STATUS write triggered it), so an RSP ucode that
   polls RDRAM for the CPU -- which is exactly what the F-Zero X EK's loader
   and audio ucodes do -- can only make progress if the CPU is given time to
   run, and the CPU can only run while the RSP is not running.  A host-budget
   yield alone cannot express that (the yield's completion notification traps
   the guest in its own SP handler; see do_SP_Task), so the slice has to be
   driven from the CPU side instead.

   Called from dynarec_gen_interrupt() (the recompiler's cc_interrupt hook,
   DD-gated by the caller) with a coarse time gate: the RSP gets a bounded
   slice roughly every RSP_DD_PUMP_NS of wall time, and the CPU owns the rest.
   do_SP_Task is used rather than rsp.doRspCycles() so a slice that finally
   reaches the ucode's BREAK delivers the stock completion (SP_INT event ->
   rsp_interrupt_event -> MI_INTR_SP -> the guest's RSP handler). */
/* ROUND 62 -- THE HARDWARE-SHAPED RSP SLICE (default DD model).

   On hardware the RSP is a coprocessor that keeps executing while the CPU
   executes: SP_STATUS.HALT/BROKE belong to the RSP, the CPU only writes the
   SP_STATUS command bits, and the SP interrupt is asserted by the RSP itself
   (a BREAK with INTR_BREAK).  mupen64plus runs the RSP inside do_SP_Task, on
   the emulation thread, which makes that concurrency impossible; the DD route
   has been papering over it ever since with do_SP_Task re-entries that also
   drag in the gfx-task side effects (new_frame, framebuffer protect/unprotect,
   DP-interrupt consumption) and a forced HALT.

   This slice is the concurrency without the interpretation: run the RSP for a
   bounded amount of emulated work straight through the plugin, then hand the
   thread back to the CPU.  The ucode's polls of SP_STATUS / DMEM / RDRAM then
   see CPU progress a few thousand RSP cycles later, which is what the yield
   handshake (SIG0/SIG1) needs, and nothing here fabricates a task boundary. */
static void rsp_dd_slice(void)
{
    struct rsp_core* sp = &g_dev.sp;
    uint32_t status = sp->regs[SP_STATUS_REG];
    wd65_watch_check(sp, 2);   /* round 65: changes since the last slice/check */

    /* A halted or broken RSP is not executing: nothing to run, and it is not
       holding the SP busy either. */
    if (status & (SP_STATUS_HALT | SP_STATUS_BROKE)) {
        sp->rsp_task_locked = 0;
        sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
        return;
    }

    rsp.doRspCycles(0xffffffff);   /* the plugin's emulated-work budget ends it */

    status = sp->regs[SP_STATUS_REG];
    if (status & (SP_STATUS_HALT | SP_STATUS_BROKE))
    {
        /* The RSP stopped during this slice: that is the task boundary, and
           the only thing hardware signals.  Slices end with the RSP still
           running all the time; signalling those fabricates a completion for
           a task that is merely between slices. */
        sp->rsp_task_locked = 0;
        sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
        raise_rcp_interrupt(sp->mi, MI_INTR_SP);
    }
    else
    {
        sp->rsp_task_locked = 1;
        sp->mi->r4300->cp0.interrupt_unsafe_state |= INTR_UNSAFE_RSP;
    }
    wd65_watch_check(sp, 3);   /* round 65: did this slice's RSP run change IMEM or the header? */
}

void rsp_dd_background_pump(void)
{
    static uint32_t n = 0;
    static struct timespec last;
    struct timespec now;
    long long d;
    uint32_t* cp0_regs;

    /* ROUND 57: how often is the CPU thread pulled into this, and how much
       host time does the RSP slice it runs actually consume?  The counter is
       advanced before any early-out so `n` is a true block-boundary count on
       the DD route -- there is no other counter for that in the recompiler. */
    extern volatile uint32_t wd_c_pump_n, wd_c_pump_call, wd_c_pump_us, wd_c_pump_max;
    if (g_dev.dd.idisk != NULL) wd_c_pump_n++;

    if (g_dev.dd.idisk == NULL) return;               /* plain carts: inert */
    if (wd_sp_stock()) return;                        /* stock SP semantics  */
    wd_check_imem_sane(&g_dev.sp);

    /* ROUND-67 FIX: the DD reboot's "CPU state reset" (LeoBootGame's
       IMEM/DMEM wipe, guest PC 0x800bb92c = LeoBootGame+0x3ec, measured via
       the r67 CPUW ring) destroys whatever task was in the RSP -- on
       hardware the boot owns the coprocessor from that moment and the
       machine reboots into the DD game.  Feeding the wiped (0x00010001
       fill) IMEM to the pump runs fill-as-code forever and the boot never
       completes.  So: once the fill signature is latched, stop pumping;
       the latch clears when the guest loads a NEW task header (the
       ROUND-22 latch seq), which is how the second boot re-arms the RSP. */
    if (wd_imem_bad)
    {
        if (wd_cur_hdr_seq != wd_imem_bad_hdr_seq)
            wd_imem_bad = 0;               /* a new task load re-armed the RSP */
        else
            return;                        /* the boot still owns the RSP */
    }

    if (!wd_dd_legacy()) {
        /* ROUND 62 (default): drive the RSP whenever it is enabled, i.e. by
           the same condition hardware uses -- HALT clear and not broken.  No
           task-type interpretation, no forced HALT, no synthetic interrupt. */
        if (g_dev.sp.regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE))
            return;
        /* Duty cycle: the RSP may start a slice only once RSP_DD_PUMP_DUTY
           times the *measured cost of the last slice* has elapsed, so the
           emulation thread is shared (RSP ~1/(1+DUTY), CPU the rest) no matter
           how fast or slow a slice turns out to be.  Without this the CPU is
           starved outright -- measured on the RP6 (r62/new): VI 165 in 90 s
           (1.8/s, against 60/s) with the RSP holding the whole thread. */
        if ((++n & 3u) != 0) return;                  /* amortize the clock  */
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (last.tv_sec != 0) {
            d = (long long)(now.tv_sec - last.tv_sec) * 1000000000LL
              + (long long)(now.tv_nsec - last.tv_nsec);
            if (wd_pump_last_us > 0) {
                long long need = (long long)wd_pump_last_us * 1000LL * RSP_DD_PUMP_DUTY;
                if (d < need) return;
            } else if (d < 1000000LL) {
                return;                              /* 1 ms floor before the first measurement */
            }
        }
        last = now;
        cp0_update_count(&g_dev.r4300);
        wd_pump_call = 1;
        {
            struct timespec pt0, pt1;
            unsigned int us;
            clock_gettime(CLOCK_MONOTONIC, &pt0);
            rsp_dd_slice();
            clock_gettime(CLOCK_MONOTONIC, &pt1);
            us = (unsigned int)((pt1.tv_sec - pt0.tv_sec) * 1000000
                              + (pt1.tv_nsec - pt0.tv_nsec) / 1000);
            wd_pump_last_us = us;
            wd_c_pump_call++;
            wd_c_pump_us += us;
            if (us > wd_c_pump_max) wd_c_pump_max = us;
        }
        wd_pump_call = 0;
        return;
    }

    if (!g_dev.sp.rsp_task_locked) return;            /* no task in flight   */
    if (g_dev.sp.regs[SP_STATUS_REG] & SP_STATUS_BROKE) return;

    /* ROUND 25, 64DD ROUTE ONLY — THE TASK-LOAD WINDOW IS THE GUEST'S.

       While the guest is inside osSpTaskLoad() the RSP must not run: the guest
       has already DMAd the new boot ucode over IMEM 0x000 and the new OSTask
       header over DMEM 0xFC0, and its `while (__osSpSetPc(SP_IMEM_START) == -1)`
       loop depends on HALT staying set.  Running the *outgoing* ucode here
       (which is what clearing HALT unconditionally below did) executes the
       freshly written boot ucode's bytes with the old ucode's pc and registers
       and lets it keep rewriting the new task's header -- see write_rsp_regs2
       for the two measured consequences (rspboot loading the text from a
       clobbered DMEM 0xFD0; the yield buffer's last word, which IS the resumed
       task's ucode base, being saved from that same word).

       The 250 ms backstop means a flag lost to an unusual guest sequence can
       never stall the route -- worst case this behaves exactly as before. */
    if (g_dev.sp.rsp_task_load_pending)
    {
        struct timespec ls;
        uint64_t now_ms;
        clock_gettime(CLOCK_MONOTONIC, &ls);
        now_ms = (uint64_t)ls.tv_sec * 1000ull + (uint64_t)(ls.tv_nsec / 1000000);
        if (now_ms - g_dev.sp.rsp_task_load_since_ms < 250ull)
        {
            wd_c_loadguard_skip++;
            return;
        }
        g_dev.sp.rsp_task_load_pending = 0;
    }

    /* ROUND 13: a task that do_SP_Task left suspended (HALT set, not BROKE,
       still locked) is resumable -- that HALT is what keeps the guest's
       osSpTaskLoad() from deadlocking.  Clear it for this slice only: the
       plugin's DoRspCycles() refuses to run while HALT is set, and do_SP_Task
       re-sets it on the way out while the task is still unfinished. */
    if (!wd_dd_legacy()) {                            /* ROUND 62: new model */
        return;                                       /* (handled above)     */
    }
    g_dev.sp.regs[SP_STATUS_REG] &= ~SP_STATUS_HALT;
    if ((++n & 3u) != 0) return;                      /* amortize the clock  */
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (last.tv_sec != 0) {
        d = (long long)(now.tv_sec - last.tv_sec) * 1000000000LL
          + (long long)(now.tv_nsec - last.tv_nsec);
        if (d < 3000000LL) return;                    /* ~40% RSP / 60% CPU  */
    }

    /* ROUND 57 — THE PUMP WAS EATING THE WHOLE CPU THREAD.

       The 3 ms gate above is a *fixed* floor and says nothing about what the
       slice it admits actually costs, so the intended "~40% RSP / 60% CPU"
       split was never enforced.  Measured on the device (r57b, 300 ms window,
       .fzxwork/r57b/t090_stall.txt):

           DELTA5 CORE  d_utime=19 d_stime=0      -> 190 ms of a 300 ms window
           DELTA5 PUMP  n=38 call=9 us=169870     -> 170 ms of those 190 ms
                                    max=250035    -> one slice took 250 ms!

       i.e. do_SP_Task slices issued by this pump consumed ~90-97% of the
       emulation thread, and the guest CPU was left with ~5%: CP0 COUNT
       advanced 4.61 MHz (an N64 is 93.75 MHz) while the guest clocked only
       ~71k cycles per VI.  The guest was not deadlocked at all -- it was
       being starved by its own workaround, which is why the 64DD logo frame
       never advanced and why every other counter in the machine looked idle.

       The cap below makes the pump's duty cycle proportional instead of
       fixed: a new slice may start only once RSP_DD_PUMP_DUTY times the
       *measured cost of the previous slice* has elapsed.  With DUTY=2 the RSP
       gets at most ~1/3 of the thread and the CPU keeps the rest, while every
       slice the RSP used to get it still gets -- only spread further apart --
       so the task-completion behavior this pump exists to provide (the RSP
       must keep running while SP_STATUS.HALT is re-set by do_SP_Task on the
       way out) is preserved rather than removed.  DD route only. */
    if (wd_pump_last_us > 0) {
        long long need = (long long)wd_pump_last_us * 1000LL * RSP_DD_PUMP_DUTY;
        if (last.tv_sec != 0 && d < need) return;
    }
    last = now;
    /* Same CPU counter bookkeeping do_SP_Task's other callers rely on. */
    cp0_regs = r4300_cp0_regs(&g_dev.r4300.cp0);
    cp0_update_count(&g_dev.r4300);
    if (cp0_regs[CP0_COUNT_REG] != 0) { /* keep the compiler honest */ }
    wd_pump_call = 1;
    {
        struct timespec pt0, pt1;
        unsigned int us;
        clock_gettime(CLOCK_MONOTONIC, &pt0);
        do_SP_Task(&g_dev.sp);
        clock_gettime(CLOCK_MONOTONIC, &pt1);
        us = (unsigned int)((pt1.tv_sec - pt0.tv_sec) * 1000000
                          + (pt1.tv_nsec - pt0.tv_nsec) / 1000);
        wd_pump_last_us = us;
        wd_c_pump_call++;
        wd_c_pump_us += us;
        if (us > wd_c_pump_max) wd_c_pump_max = us;
    }
    wd_pump_call = 0;
}

void rsp_interrupt_event(void* opaque)
{
    if (g_dev.dd.idisk != NULL) wd_c_sp_int_evt++;
    struct rsp_core* sp = (struct rsp_core*)opaque;
    wd65_watch_check(sp, 4);   /* round 65: SP event boundary */

    if (!sp->rsp_task_locked)
    {
        sp->regs[SP_STATUS_REG] |=
            SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT;
    }

    if ((sp->regs[SP_STATUS_REG] & SP_STATUS_INTR_BREAK) != 0)
    {
        raise_rcp_interrupt(sp->mi, MI_INTR_SP);
    }
}

void rsp_end_of_dma_event(void* opaque)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    fifo_pop(sp);
}
