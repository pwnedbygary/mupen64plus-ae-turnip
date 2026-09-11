/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - mi_controller.c                                         *
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

#include "mi_controller.h"

#include <string.h>

#include "device/r4300/cp0.h"
#include "device/r4300/interrupt.h"
#include "device/r4300/r4300_core.h"
#include "main/main.h"   /* g_dev, for the 64DD gate */
#include "device/rcp/rsp/rsp_core.h"  /* wd_sp_stock() */

static int update_mi_init_mode(uint32_t* mi_init_mode, uint32_t w)
{
    int clear_dp = 0;

    /* set init_length */
    *mi_init_mode &= ~0x7f;
    *mi_init_mode |= w & 0x7f;
    /* clear / set init_mode */
    if (w & 0x80)  *mi_init_mode &= ~0x80;
    if (w & 0x100) *mi_init_mode |= 0x80;
    /* clear / set ebus test_mode */
    if (w & 0x200) *mi_init_mode &= ~0x100;
    if (w & 0x400) *mi_init_mode |= 0x100;
    /* clear DP interrupt */
    if (w & 0x800) clear_dp = 1;
    /* clear / set RDRAM reg_mode */
    if (w & 0x1000) *mi_init_mode &= ~0x200;
    if (w & 0x2000) *mi_init_mode |= 0x200;

    return clear_dp;
}

static void update_mi_intr_mask(uint32_t* mi_intr_mask, uint32_t w)
{
    if (w & 0x1)   *mi_intr_mask &= ~MI_INTR_SP;
    if (w & 0x2)   *mi_intr_mask |= MI_INTR_SP;
    if (w & 0x4)   *mi_intr_mask &= ~MI_INTR_SI;
    if (w & 0x8)   *mi_intr_mask |= MI_INTR_SI;
    if (w & 0x10)  *mi_intr_mask &= ~MI_INTR_AI;
    if (w & 0x20)  *mi_intr_mask |= MI_INTR_AI;
    if (w & 0x40)  *mi_intr_mask &= ~MI_INTR_VI;
    if (w & 0x80)  *mi_intr_mask |= MI_INTR_VI;
    if (w & 0x100) *mi_intr_mask &= ~MI_INTR_PI;
    if (w & 0x200) *mi_intr_mask |= MI_INTR_PI;
    if (w & 0x400) *mi_intr_mask &= ~MI_INTR_DP;
    if (w & 0x800) *mi_intr_mask |= MI_INTR_DP;
}


void init_mi(struct mi_controller* mi, struct r4300_core* r4300)
{
    mi->r4300 = r4300;
}

void poweron_mi(struct mi_controller* mi)
{
    memset(mi->regs, 0, MI_REGS_COUNT*sizeof(uint32_t));
    mi->regs[MI_VERSION_REG] = 0x02020102;
}


/* ROUND-13 DD DIAG: the DP (RDP-done) delivery chain, guest side.

   parallel-RDP raises the DP interrupt by writing *gfx.MI_INTR_REG |= 0x20
   DIRECTLY (mupen64plus-video-parallel/upstream/parallel_imp.cpp:215), so it
   never passes through raise_rcp_interrupt/signal_rcp_interrupt and is
   invisible to wd_c_raise_bits[].  These two counters close that hole from the
   guest's end: a read of MI_INTR with the DP bit set is the guest's interrupt
   handler noticing it, and a write to MI_INIT_MODE with the clear-DP bit
   (0x800) is the guest's DP handler acknowledging it.  Audio/VI/PI/SP are the
   control: those counters climb at 60/s on the stalled machine, so the DP pair
   freezing pins the loss on the RDP completion itself. */
volatile uint32_t wd_c_mi_rd_dp = 0;   /* guest read MI_INTR with DP set   */
volatile uint32_t wd_c_dp_ack = 0;     /* guest cleared the DP interrupt   */

void read_mi_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct mi_controller* mi = (struct mi_controller*)opaque;
    uint32_t reg = mi_reg(address);

    if (g_dev.dd.idisk != NULL && reg == MI_INTR_REG && (mi->regs[MI_INTR_REG] & MI_INTR_DP))
        wd_c_mi_rd_dp++;

    *value = mi->regs[reg];
}

void write_mi_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct mi_controller* mi = (struct mi_controller*)opaque;
    uint32_t reg = mi_reg(address);

    int* cp0_cycle_count = r4300_cp0_cycle_count(&mi->r4300->cp0);

    switch(reg)
    {
    case MI_INIT_MODE_REG:
        if (update_mi_init_mode(&mi->regs[MI_INIT_MODE_REG], value & mask) != 0)
        {
            if (g_dev.dd.idisk != NULL) wd_c_dp_ack++;
            clear_rcp_interrupt(mi, MI_INTR_DP);
        }
        break;
    case MI_INTR_MASK_REG:
        update_mi_intr_mask(&mi->regs[MI_INTR_MASK_REG], value & mask);

        r4300_check_interrupt(mi->r4300, CP0_CAUSE_IP2, mi->regs[MI_INTR_REG] & mi->regs[MI_INTR_MASK_REG]);
        cp0_update_count(mi->r4300);
        if (*cp0_cycle_count >= 0) gen_interrupt(mi->r4300);

        /* Recompiler lost-interrupt fix (mirrors the cached-interpreter
           recheck in interrupt.c gen_interrupt, but for the dynarec path).
           r4300_check_interrupt only queues a CHECK_INT when the interrupt
           is deliverable AT WRITE TIME.  An RCP interrupt raised while IE=0
           or EXL/ERL set latches only the CP0 CAUSE bit and nothing re-consults
           it once the guest re-enables interrupts — the F-Zero X EK loader
           spins on `CAUSE & 0x7c` waiting for it.  When no CHECK_INT is queued,
           a pending hardware-interrupt CAUSE bit is set and the guest is
           deliverable, take the interrupt right here. */
#ifdef NEW_DYNAREC
        {
            uint32_t* cp0_regs = r4300_cp0_regs(&mi->r4300->cp0);
            if (g_dev.dd.idisk != NULL   /* 64DD combo game only */
                && wd_dd_legacy()
                && get_event(&mi->r4300->cp0.q, CHECK_INT) == NULL
                && (cp0_regs[CP0_STATUS_REG] & cp0_regs[CP0_CAUSE_REG] & UINT32_C(0xff00))
                && (cp0_regs[CP0_STATUS_REG] & (CP0_STATUS_IE | CP0_STATUS_EXL | CP0_STATUS_ERL)) == CP0_STATUS_IE)
            {
                exception_general(mi->r4300);
            }
        }
#endif
        break;
    }
}

/* interrupt execution is immediate (if not masked)
 * Should only be called inside interrupt event handlers.
 * For other cases use signal_rcp_interrupt
 */
void raise_rcp_interrupt(struct mi_controller* mi, uint32_t mi_intr)
{
    /* Round 8 DD-route attribution (see cached_interp.c): which MI_INTR bit is
       driving the observed guest exception rate.  Plain carts: one branch. */
    if (g_dev.dd.idisk != NULL) {
        extern volatile uint32_t wd_c_raise, wd_c_raise_bits[8];
        int b;
        wd_c_raise++;
        for (b = 0; b < 8; b++) if (mi_intr & (1u << b)) wd_c_raise_bits[b]++;
    }

    mi->regs[MI_INTR_REG] |= mi_intr;

    if (mi->regs[MI_INTR_REG] & mi->regs[MI_INTR_MASK_REG])
        raise_maskable_interrupt(mi->r4300, CP0_CAUSE_IP2);
}

/* interrupt execution is scheduled (if not masked) */
void signal_rcp_interrupt(struct mi_controller* mi, uint32_t mi_intr)
{
    if (g_dev.dd.idisk != NULL) {
        extern volatile uint32_t wd_c_signal, wd_c_raise_bits[8];
        int b;
        wd_c_signal++;
        for (b = 0; b < 8; b++) if (mi_intr & (1u << b)) wd_c_raise_bits[b]++;
    }

    mi->regs[MI_INTR_REG] |= mi_intr;
    r4300_check_interrupt(mi->r4300, CP0_CAUSE_IP2, mi->regs[MI_INTR_REG] & mi->regs[MI_INTR_MASK_REG]);
}

void clear_rcp_interrupt(struct mi_controller* mi, uint32_t mi_intr)
{
    mi->regs[MI_INTR_REG] &= ~mi_intr;
    r4300_check_interrupt(mi->r4300, CP0_CAUSE_IP2, mi->regs[MI_INTR_REG] & mi->regs[MI_INTR_MASK_REG]);
}

/* ROUND 42: DD-route DP-interrupt delivery.

   The parallel-RDP raises the DP interrupt by writing
   *gfx.MI_INTR_REG |= 0x20 DIRECTLY into the core's MI controller register
   (plugin.c wires gfx_info.MI_INTR_REG to &g_dev.mi.regs[MI_INTR_REG]) and
   then calls gfx.CheckInterrupts().  Upstream sets that to EmptyFunc
   (plugin.c), so the DP bit is latched in MI_INTR_REG but the RCP CAUSE bit is
   never raised and no CHECK_INT is ever queued -- the CPU's guest thread keeps
   spinning on EVENT_MESG_DP (the r36-named post-load deadlock).  A faithful
   model (Ares/Phobos MI::poll) re-consults the masked interrupt and pends the
   RCP line to the CPU at the moment the DP raises it.

   This mirrors that: re-check the masked interrupt so the RCP CAUSE bit is
   latched and a CHECK_INT event is queued for the CPU thread to consume.
   It does NOT run the guest handler here (that would be wrong on the RDP
   thread); the CPU thread takes it on its next interrupt poll.  Gated to the
   DD route (g_dev.dd.idisk != NULL) so plain carts keep the stock EmptyFunc
   behaviour byte for byte. */
void dd_check_interrupts(void)
{
    if (g_dev.dd.idisk == NULL)
        return;

    struct r4300_core* r4300 = &g_dev.r4300;
    struct mi_controller* mi = &g_dev.mi;

    r4300_check_interrupt(r4300, CP0_CAUSE_IP2,
        mi->regs[MI_INTR_REG] & mi->regs[MI_INTR_MASK_REG]);
}

