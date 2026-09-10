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
volatile uint32_t wd_hdr_n = 0;        /* DMAs covering DMEM 0xFC0           */
uint32_t wd_hdr_dram = 0, wd_hdr_mem = 0, wd_hdr_len = 0, wd_hdr_seq = 0;
uint32_t wd_hdr_src[16];               /* source words, before the copy      */
uint32_t wd_hdr_dst[16];               /* DMEM 0xFC0 words, after the copy   */
volatile uint32_t wd_hdr_same = 0, wd_hdr_diff = 0;

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
       than walking into the neighbouring bank (upstream lets it run past the
       end).  Masking each access with `& 0xfff` reproduces the hardware wrap;
       for every DMA observed so far the offset stays below 0x1000 anyway, so
       this only matters for a whole-bank load that starts mid-bank. */
    unsigned char *spmem = (unsigned char*)sp->mem + (dma->memaddr & 0x1000);
    unsigned char *dram = (unsigned char*)sp->ri->rdram->dram;
    /* ROUND-7 DD DIAG (see wd_hdr_* above): a DRAM->RSP DMA whose DMEM
       destination range covers 0xFC0 is the osSpTaskLoad header copy. */
    int wd_is_hdr = 0;
    if (g_dev.dd.idisk != NULL)
    {
        wd_c_spdma++;
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

    masked_write(&sp->mem[addr], value, mask);
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
}

extern volatile uint32_t wd_c_do_sp_task;
extern volatile uint32_t wd_c_sp_int_evt;

void do_SP_Task(struct rsp_core* sp)
{
    if (g_dev.dd.idisk != NULL) wd_c_do_sp_task++;
    uint32_t save_pc = sp->regs2[SP_PC_REG] & ~0xfff;
    /* Entry SP_STATUS — distinguish "ucode yielded during THIS run" (was
       running at entry, HALT set by the plugin's poll-yield at exit) from
       "ucode already halted before entry" (doRspCycles returns 0 without
       running, so we must NOT re-deliver a stale interrupt). */
    uint32_t wd_status_entry = sp->regs[SP_STATUS_REG];

    /* ROUND-7 DD DIAG (see wd_hdr_* above): compare the header DMEM 0xFC0
       against what the last header DMA actually wrote there.  `same` means
       the copy landed and survived (so the guest handed us this header);
       `diff` means something overwrote it after the DMA (the ucode itself,
       which is the known F3DEX behaviour), and `hdr_n == 0` means no header
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

    sp->rsp_task_locked = 0;
    sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
    if ((sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)) == 0)
    {
        sp->rsp_task_locked = 1;
        sp->mi->r4300->cp0.interrupt_unsafe_state |= INTR_UNSAFE_RSP;
        sp->mi->regs[MI_INTR_REG] |= MI_INTR_SP;
    }
    /* DD-ONLY ares-derived completion/yield delivery (gated: plain cart
       games must keep the pre-ares rsp_interrupt_event path exactly). */
    if (g_dev.dd.idisk != NULL)
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

    sp->regs[SP_STATUS_REG] &=
        ~(SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT);
}

void rsp_interrupt_event(void* opaque)
{
    if (g_dev.dd.idisk != NULL) wd_c_sp_int_evt++;
    struct rsp_core* sp = (struct rsp_core*)opaque;

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
