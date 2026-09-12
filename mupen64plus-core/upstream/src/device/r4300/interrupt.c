/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - interrupt.c                                              *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2002 Hacktarux                                          *
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

#define M64P_CORE_PROTOTYPES 1

#include "interrupt.h"

#ifdef __MINGW32__
#define _CRT_RAND_S
#endif
#include <stdlib.h>

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "api/m64p_types.h"
#include "device/pif/bootrom_hle.h"
#include "device/r4300/cached_interp.h"
#include "device/r4300/cp0.h"
#include "device/r4300/dd_fault_layout.h"
#include "device/r4300/new_dynarec/new_dynarec.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/recomp.h"
#include "device/rdram/rdram.h"
#include "device/rcp/ai/ai_controller.h"
#include "device/rcp/vi/vi_controller.h"
#include "main/main.h"
#include "main/savestates.h"


/***************************************************************************
 * Pool of Single Linked List Nodes
 **************************************************************************/

static struct node* alloc_node(struct pool* p);
static void free_node(struct pool* p, struct node* node);
static void clear_pool(struct pool* p);


/* node allocation/deallocation on a given pool */
static struct node* alloc_node(struct pool* p)
{
    /* return NULL if pool is too small */
    if (p->index >= INTERRUPT_NODES_POOL_CAPACITY) {
        return NULL;
    }

    return p->stack[p->index++];
}

static void free_node(struct pool* p, struct node* node)
{
    if (p->index == 0 || node == NULL) {
        return;
    }

    p->stack[--p->index] = node;
}

/* release all nodes */
static void clear_pool(struct pool* p)
{
    size_t i;

    for (i = 0; i < INTERRUPT_NODES_POOL_CAPACITY; ++i) {
        p->stack[i] = &p->nodes[i];
    }

    p->index = 0;
}

/***************************************************************************
 * Interrupt Queue
 **************************************************************************/

static void clear_queue(struct interrupt_queue* q)
{
    q->first = NULL;
    clear_pool(&q->pool);
}

static int before_event(const struct cp0* cp0, unsigned int evt1, unsigned int evt2, int type2)
{
    const uint32_t* cp0_regs = r4300_cp0_regs((struct cp0*)cp0); /* OK to cast away const qualifier */
    uint32_t count = cp0_regs[CP0_COUNT_REG];
    int* cp0_cycle_count = r4300_cp0_cycle_count((struct cp0*)cp0);

    /* At least one other interrupt is pending */
    if (*cp0_cycle_count > 0)
        count -= *cp0_cycle_count;

    if ((evt1 - count) < (evt2 - count)) return 1;
    else return 0;
}

unsigned int add_random_interrupt_time(struct r4300_core* r4300)
{
    if (r4300->randomize_interrupt) {
        unsigned int value;
#ifdef __MINGW32__
        rand_s(&value);
#else
        value = rand();
#endif
        return value % 0x40;
    } else
        return 0;
}

void add_interrupt_event(struct cp0* cp0, int type, unsigned int delay)
{
    const uint32_t* cp0_regs = r4300_cp0_regs(cp0);
    add_interrupt_event_count(cp0, type, cp0_regs[CP0_COUNT_REG] + delay);
}

void add_interrupt_event_count(struct cp0* cp0, int type, unsigned int count)
{
    struct node* event;
    struct node* e;
    const uint32_t* cp0_regs = r4300_cp0_regs(cp0);
    unsigned int* cp0_next_interrupt = r4300_cp0_next_interrupt(cp0);
    int* cp0_cycle_count = r4300_cp0_cycle_count(cp0);

    if (get_event(&cp0->q, type)) {
        DebugMessage(M64MSG_WARNING, "two events of type 0x%x in interrupt queue", type);
    }

    event = alloc_node(&cp0->q.pool);
    if (event == NULL)
    {
        DebugMessage(M64MSG_ERROR, "Failed to allocate node for new interrupt event");
        return;
    }

    event->data.count = count;
    event->data.type = type;

    if (cp0->q.first == NULL)
    {
        cp0->q.first = event;
        event->next = NULL;
    }
    else if (before_event(cp0, count, cp0->q.first->data.count, cp0->q.first->data.type))
    {
        event->next = cp0->q.first;
        cp0->q.first = event;
    }
    else
    {
        for (e = cp0->q.first;
            e->next != NULL &&
            (!before_event(cp0, count, e->next->data.count, e->next->data.type));
            e = e->next);

        if (e->next == NULL)
        {
            e->next = event;
            event->next = NULL;
        }
        else
        {
            for(; e->next != NULL && e->next->data.count == count; e = e->next);

            event->next = e->next;
            e->next = event;
        }
    }
    *cp0_next_interrupt = cp0->q.first->data.count;
    *cp0_cycle_count = cp0_regs[CP0_COUNT_REG] - cp0->q.first->data.count;
}

void remove_interrupt_event(struct cp0* cp0)
{
    struct node* e;
    const uint32_t* cp0_regs = r4300_cp0_regs(cp0);
    unsigned int* cp0_next_interrupt = r4300_cp0_next_interrupt(cp0);
    int* cp0_cycle_count = r4300_cp0_cycle_count(cp0);

    e = cp0->q.first;
    cp0->q.first = e->next;
    free_node(&cp0->q.pool, e);

    *cp0_next_interrupt = (cp0->q.first != NULL)
        ? cp0->q.first->data.count
        : 0;

    *cp0_cycle_count = (cp0->q.first != NULL)
        ? (cp0_regs[CP0_COUNT_REG] - cp0->q.first->data.count)
        : 0;
}

unsigned int* get_event(const struct interrupt_queue* q, int type)
{
    struct node* e = q->first;

    if (e == NULL) {
        return NULL;
    }

    if (e->data.type == type) {
        return &e->data.count;
    }

    for (; e->next != NULL && e->next->data.type != type; e = e->next);

    return (e->next != NULL)
        ? &e->next->data.count
        : NULL;
}

int get_next_event_type(const struct interrupt_queue* q)
{
    return (q->first == NULL)
        ? 0
        : q->first->data.type;
}

void remove_event(struct interrupt_queue* q, int type)
{
    struct node* to_del;
    struct node* e = q->first;

    if (e == NULL) {
        return;
    }

    if (e->data.type == type)
    {
        q->first = e->next;
        free_node(&q->pool, e);
    }
    else
    {
        for (; e->next != NULL && e->next->data.type != type; e = e->next);

        if (e->next != NULL)
        {
            to_del = e->next;
            e->next = to_del->next;
            free_node(&q->pool, to_del);
        }
    }
}

void translate_event_queue(struct cp0* cp0, unsigned int base)
{
    struct node* e;
    uint32_t* cp0_regs = r4300_cp0_regs(cp0);
    int* cp0_cycle_count = r4300_cp0_cycle_count(cp0);

    remove_event(&cp0->q, COMPARE_INT);
    remove_event(&cp0->q, SPECIAL_INT);

    for (e = cp0->q.first; e != NULL; e = e->next)
    {
        e->data.count = (e->data.count - cp0_regs[CP0_COUNT_REG]) + base;
    }

    cp0_regs[CP0_COUNT_REG] = base;
    add_interrupt_event_count(cp0, SPECIAL_INT, ((cp0_regs[CP0_COUNT_REG] & UINT32_C(0x80000000)) ^ UINT32_C(0x80000000)));

    /* Add count_per_op to avoid wrong event order in case CP0_COUNT_REG == CP0_COMPARE_REG */
    cp0_regs[CP0_COUNT_REG] += cp0->count_per_op;
    *cp0_cycle_count += cp0->count_per_op;
    add_interrupt_event_count(cp0, COMPARE_INT, cp0_regs[CP0_COMPARE_REG]);
    cp0_regs[CP0_COUNT_REG] -= cp0->count_per_op;

    /* Update next interrupt in case first event is COMPARE_INT */
    *cp0_cycle_count = cp0_regs[CP0_COUNT_REG] - cp0->q.first->data.count;
}

int save_eventqueue_infos(const struct cp0* cp0, char *buf)
{
    int len;
    struct node* e;

    len = 0;

    for (e = cp0->q.first; e != NULL; e = e->next)
    {
        memcpy(buf + len    , &e->data.type , 4);
        memcpy(buf + len + 4, &e->data.count, 4);
        len += 8;
    }

    *((unsigned int*)&buf[len]) = 0xFFFFFFFF;
    return len+4;
}

void load_eventqueue_infos(struct cp0* cp0, const char *buf)
{
    int len = 0;
    uint32_t* cp0_regs = r4300_cp0_regs(cp0);

    clear_queue(&cp0->q);

    while (*((const unsigned int*)&buf[len]) != 0xFFFFFFFF)
    {
        int type = *((const unsigned int*)&buf[len]);
        unsigned int count = *((const unsigned int*)&buf[len+4]);
        add_interrupt_event_count(cp0, type, count);
        len += 8;
    }

    remove_event(&cp0->q, SPECIAL_INT);
    add_interrupt_event_count(cp0, SPECIAL_INT, ((cp0_regs[CP0_COUNT_REG] & UINT32_C(0x80000000)) ^ UINT32_C(0x80000000)));
}

void init_interrupt(struct cp0* cp0)
{
    clear_queue(&cp0->q);
    add_interrupt_event_count(cp0, SPECIAL_INT, 0x80000000);
    add_interrupt_event_count(cp0, COMPARE_INT, 0);
}

void r4300_check_interrupt(struct r4300_core* r4300, uint32_t cause_ip, int set_cause)
{
    struct node* event;
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);
    unsigned int* cp0_next_interrupt = r4300_cp0_next_interrupt(&r4300->cp0);
    int* cp0_cycle_count = r4300_cp0_cycle_count(&r4300->cp0);

    if (set_cause) {
        cp0_regs[CP0_CAUSE_REG] = (cp0_regs[CP0_CAUSE_REG] | cause_ip) & ~CP0_CAUSE_EXCCODE_MASK;
    }
    else {
        cp0_regs[CP0_CAUSE_REG] &= ~cause_ip;
    }

    if ((cp0_regs[CP0_STATUS_REG] & (CP0_STATUS_IE | CP0_STATUS_EXL | CP0_STATUS_ERL)) != CP0_STATUS_IE) {
        return;
    }
    if (cp0_regs[CP0_STATUS_REG] & cp0_regs[CP0_CAUSE_REG] & UINT32_C(0xFF00))
    {
        event = alloc_node(&r4300->cp0.q.pool);

        if (event == NULL)
        {
            DebugMessage(M64MSG_ERROR, "Failed to allocate node for new interrupt event");
            return;
        }

        event->data.count = *cp0_next_interrupt = cp0_regs[CP0_COUNT_REG];
        event->data.type = CHECK_INT;
        *cp0_cycle_count = 0;

        if (r4300->cp0.q.first == NULL)
        {
            r4300->cp0.q.first = event;
            event->next = NULL;
        }
        else
        {
            event->next = r4300->cp0.q.first;
            r4300->cp0.q.first = event;

        }
    }
}

void raise_maskable_interrupt(struct r4300_core* r4300, uint32_t cause_ip)
{
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);
    cp0_regs[CP0_CAUSE_REG] = (cp0_regs[CP0_CAUSE_REG] | cause_ip) & ~CP0_CAUSE_EXCCODE_MASK;

    if (!(cp0_regs[CP0_STATUS_REG] & cp0_regs[CP0_CAUSE_REG] & UINT32_C(0xff00))) {
        return;
    }

    if ((cp0_regs[CP0_STATUS_REG] & (CP0_STATUS_IE | CP0_STATUS_EXL | CP0_STATUS_ERL)) != CP0_STATUS_IE) {
        return;
    }

    exception_general(r4300);
}

void compare_int_handler(void* opaque)
{
    struct r4300_core* r4300 = (struct r4300_core*)opaque;
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);
    int* cp0_cycle_count = r4300_cp0_cycle_count(&r4300->cp0);

    /* Add count_per_op to avoid wrong event order in case CP0_COUNT_REG == CP0_COMPARE_REG */
    cp0_regs[CP0_COUNT_REG] += r4300->cp0.count_per_op;
    *cp0_cycle_count += r4300->cp0.count_per_op;
    add_interrupt_event_count(&r4300->cp0, COMPARE_INT, cp0_regs[CP0_COMPARE_REG]);
    cp0_regs[CP0_COUNT_REG] -= r4300->cp0.count_per_op;

    /* Update next interrupt in case first event is COMPARE_INT */
    *cp0_cycle_count = cp0_regs[CP0_COUNT_REG] - r4300->cp0.q.first->data.count;

    raise_maskable_interrupt(r4300, CP0_CAUSE_IP7);
}

void check_int_handler(void* opaque)
{
    exception_general((struct r4300_core*)opaque);
}

/* Special interrupt is a fake interrupt which porpose is
   to ensure the number of cycles between current cycle 
   and next interrupt will never exceed 2^31 */
void special_int_handler(void* opaque)
{
    struct cp0* cp0 = (struct cp0*)opaque;
    const uint32_t* cp0_regs = r4300_cp0_regs(cp0);

    remove_interrupt_event(cp0);
    add_interrupt_event_count(cp0, SPECIAL_INT, ((cp0_regs[CP0_COUNT_REG] & UINT32_C(0x80000000)) ^ UINT32_C(0x80000000)));
}

/* XXX: this should only require r4300 struct not device ? */
/* XXX: This is completly WTF ! */
void nmi_int_handler(void* opaque)
{
    struct device* dev = (struct device*)opaque;
    struct r4300_core* r4300 = &dev->r4300;
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);

    reset_pif(&dev->pif, 1);

    // setup r4300 Status flags: reset TS and SR, set BEV, ERL, and SR
    cp0_regs[CP0_STATUS_REG] = (cp0_regs[CP0_STATUS_REG] & ~(CP0_STATUS_SR | CP0_STATUS_TS | UINT32_C(0x00080000))) | (CP0_STATUS_ERL | CP0_STATUS_BEV | CP0_STATUS_SR);
    cp0_regs[CP0_CAUSE_REG]  = 0x00000000;
    // simulate the soft reset code which would run from the PIF ROM
    pif_bootrom_hle_execute(r4300);
    // clear all interrupts, reset interrupt counters back to 0
    cp0_regs[CP0_COUNT_REG] = 0;
    g_gs_vi_counter = 0;
    init_interrupt(&r4300->cp0);

    add_interrupt_event(&r4300->cp0, VI_INT, dev->vi.delay);

    // clear the audio status register so that subsequent write_ai() calls will work properly
    dev->ai.regs[AI_STATUS_REG] = 0;
    // set ErrorEPC with the last instruction address
    cp0_regs[CP0_ERROREPC_REG] = *r4300_pc(r4300);
    // reset the r4300 internal state
    invalidate_r4300_cached_code(r4300, 0, 0);
    // adjust ErrorEPC if we were in a delay slot, and clear the r4300->delay_slot and r4300->recomp.dyna_interp flags
    if(r4300->delay_slot==1 || r4300->delay_slot==3)
    {
        cp0_regs[CP0_ERROREPC_REG]-=4;
    }
    r4300->delay_slot = 0;
#ifndef NEW_DYNAREC
    r4300->recomp.dyna_interp = 0;
#endif
    // set next instruction address to reset vector
    r4300->cp0.last_addr = r4300->start_address;
    generic_jump_to(r4300, r4300->start_address);
}


/* XXX: needs to be properly reworked */
void reset_hard_handler(void* opaque)
{
    struct device* dev = (struct device*)opaque;
    struct r4300_core* r4300 = &dev->r4300;

#ifndef NEW_DYNAREC
#if defined(__x86_64__)
    long long save_rsp = r4300->recomp.save_rsp;
    long long save_rip = r4300->recomp.save_rip;
#else
    long save_ebp = r4300->recomp.save_ebp;
    long save_ebx = r4300->recomp.save_ebx;
    long save_esi = r4300->recomp.save_esi;
    long save_edi = r4300->recomp.save_edi;
    long save_esp = r4300->recomp.save_esp;
    long save_eip = r4300->recomp.save_eip;
#endif
#endif

    poweron_device(dev);

    pif_bootrom_hle_execute(r4300);
    r4300->cp0.last_addr = r4300->start_address;
    *r4300_cp0_next_interrupt(&r4300->cp0) = 624999;
    *r4300_cp0_cycle_count(&r4300->cp0) = 0;
    init_interrupt(&r4300->cp0);
    invalidate_r4300_cached_code(r4300, 0, 0);
    *r4300_pc_struct(r4300) = &r4300->interp_PC;
    if (r4300->emumode >= 2)
    {
#ifdef NEW_DYNAREC
        new_dynarec_cleanup();
        new_dynarec_init();
#else
#if defined(__x86_64__)
        r4300->recomp.save_rsp = save_rsp;
        r4300->recomp.save_rip = save_rip;
#else
        r4300->recomp.save_ebp = save_ebp;
        r4300->recomp.save_ebx = save_ebx;
        r4300->recomp.save_esi = save_esi;
        r4300->recomp.save_edi = save_edi;
        r4300->recomp.save_esp = save_esp;
        r4300->recomp.save_eip = save_eip;
#endif
#endif
    }
    generic_jump_to(r4300, r4300->cp0.last_addr);
}


static void call_interrupt_handler(const struct cp0* cp0, size_t index)
{
    assert(index < CP0_INTERRUPT_HANDLERS_COUNT);

    const struct interrupt_handler* handler = &cp0->interrupt_handlers[index];

    handler->callback(handler->opaque);
}

enum dd_context_code_status
{
    DD_CONTEXT_CODE_OK = 0,
    DD_CONTEXT_CODE_PC_UNAVAILABLE,
    DD_CONTEXT_CODE_PC_MISALIGNED,
    DD_CONTEXT_CODE_NOT_KSEG,
    DD_CONTEXT_CODE_ADDRESS_UNDERFLOW,
    DD_CONTEXT_CODE_ADDRESS_OVERFLOW,
    DD_CONTEXT_CODE_CROSSED_KSEG,
    DD_CONTEXT_CODE_RDRAM_UNAVAILABLE,
    DD_CONTEXT_CODE_RDRAM_OUT_OF_RANGE
};

static const char* dd_context_code_status_name(enum dd_context_code_status status)
{
    static const char* const names[] = {
        "ok",
        "pc-unavailable",
        "pc-misaligned",
        "not-kseg",
        "address-underflow",
        "address-overflow",
        "crossed-kseg",
        "rdram-unavailable",
        "rdram-out-of-range"
    };

    return (unsigned int)status
        < sizeof(names) / sizeof(names[0]) ? names[status] : "unavailable";
}

/*
 * Do not use the generic memory handlers here.  They can address MMIO and
 * may have read side effects.  The snapshot is limited to the two direct
 * cached/uncached RDRAM segments, with each word checked independently.
 */
static int dd_context_map_kseg(uint32_t address, uint32_t* physical,
        unsigned int* segment)
{
    const uint64_t kseg_size = UINT64_C(0x20000000);

    if ((uint64_t)address >= (uint64_t)R4300_KSEG0
            && (uint64_t)address < (uint64_t)R4300_KSEG0 + kseg_size) {
        if (physical != NULL)
            *physical = address - R4300_KSEG0;
        *segment = 0;
        return 1;
    }
    if ((uint64_t)address >= (uint64_t)R4300_KSEG1
            && (uint64_t)address < (uint64_t)R4300_KSEG1 + kseg_size) {
        if (physical != NULL)
            *physical = address - R4300_KSEG1;
        *segment = 1;
        return 1;
    }

    return 0;
}

static int dd_context_sample_pc(struct r4300_core* r4300, uint32_t* pc_sample)
{
    struct precomp_instr** pc_struct;
    uint32_t* pc;

    if (r4300 == NULL || pc_sample == NULL)
        return 0;

    pc_struct = r4300_pc_struct(r4300);
    if (pc_struct == NULL)
        return 0;

    /*
     * In NEW_DYNAREC mode r4300_pc() selects
     * new_dynarec_hot_state.pcaddr for a dynarec core.  For interpreter
     * modes it selects the current precompiled instruction's addr.  Do not
     * dereference the latter until its pointer is known to be present.
     */
#ifdef NEW_DYNAREC
    if (r4300->emumode != EMUMODE_DYNAREC && *pc_struct == NULL)
#else
    if (*pc_struct == NULL)
#endif
        return 0;

    pc = r4300_pc(r4300);
    if (pc == NULL)
        return 0;

    *pc_sample = *pc;
    return 1;
}

static void dd_trace_context_snapshot(struct r4300_core* r4300,
        unsigned int progress_ordinal, uint32_t pc_sample, int pc_available)
{
    int64_t gpr[32];
    uint32_t code[32];
    enum dd_context_code_status code_status[32];
    int64_t* regs;
    unsigned int pc_segment = 0;
    unsigned int valid_count = 0;
    unsigned int chunk;
    unsigned int slot;
    int pc_is_kseg = 0;
    const char* pc_state;
    const char* code_state;

    memset(gpr, 0, sizeof(gpr));
    memset(code, 0, sizeof(code));
    for (slot = 0; slot < 32; ++slot)
        code_status[slot] = DD_CONTEXT_CODE_PC_UNAVAILABLE;

    regs = r4300_regs(r4300);
    if (regs != NULL)
        memcpy(gpr, regs, sizeof(gpr));

    if (!pc_available) {
        pc_state = "unavailable";
    }
    else if ((pc_sample & UINT32_C(3)) != 0) {
        pc_state = "misaligned";
        for (slot = 0; slot < 32; ++slot)
            code_status[slot] = DD_CONTEXT_CODE_PC_MISALIGNED;
    }
    else if (!dd_context_map_kseg(pc_sample, NULL, &pc_segment)) {
        pc_state = "not-kseg";
        for (slot = 0; slot < 32; ++slot)
            code_status[slot] = DD_CONTEXT_CODE_NOT_KSEG;
    }
    else {
        pc_state = "mapped";
        pc_is_kseg = 1;
    }

    if (pc_is_kseg) {
        for (slot = 0; slot < 32; ++slot) {
            uint32_t address;
            uint32_t physical;
            unsigned int segment;
            uint32_t delta;

            if (slot < 8) {
                delta = (8 - slot) * 4;
                if (pc_sample < delta) {
                    code_status[slot] = DD_CONTEXT_CODE_ADDRESS_UNDERFLOW;
                    continue;
                }
                address = pc_sample - delta;
            }
            else {
                delta = (slot - 8) * 4;
                if (pc_sample > UINT32_MAX - delta) {
                    code_status[slot] = DD_CONTEXT_CODE_ADDRESS_OVERFLOW;
                    continue;
                }
                address = pc_sample + delta;
            }

            if (!dd_context_map_kseg(address, &physical, &segment)) {
                code_status[slot] = DD_CONTEXT_CODE_NOT_KSEG;
                continue;
            }
            if (segment != pc_segment) {
                code_status[slot] = DD_CONTEXT_CODE_CROSSED_KSEG;
                continue;
            }
            if (r4300->rdram == NULL || r4300->rdram->dram == NULL) {
                code_status[slot] = DD_CONTEXT_CODE_RDRAM_UNAVAILABLE;
                continue;
            }
            if ((size_t)physical > r4300->rdram->dram_size
                    || r4300->rdram->dram_size - (size_t)physical
                        < sizeof(uint32_t)) {
                code_status[slot] = DD_CONTEXT_CODE_RDRAM_OUT_OF_RANGE;
                continue;
            }

            code[slot] = r4300->rdram->dram[physical / sizeof(uint32_t)];
            code_status[slot] = DD_CONTEXT_CODE_OK;
            ++valid_count;
        }
    }

    code_state = valid_count == 32 ? "available"
        : valid_count != 0 ? "partial" : "unavailable";
    DdStartupDiagnosticsTraceContext(
        "DDSTART5 context: candidate=%u pc_sample=%08" PRIx32
        " pc_state=%s pc_is_exact_writer=false gpr_state=%s code_state=%s",
        progress_ordinal, pc_sample, pc_state, regs != NULL ? "available" : "unavailable",
        code_state);

    for (chunk = 0; chunk < 4; ++chunk) {
        unsigned int first = chunk * 8;
        DdStartupDiagnosticsTraceContext(
            "DDSTART5 context: candidate=%u gpr%u"
            " r%02u=%016" PRIx64 " r%02u=%016" PRIx64
            " r%02u=%016" PRIx64 " r%02u=%016" PRIx64
            " r%02u=%016" PRIx64 " r%02u=%016" PRIx64
            " r%02u=%016" PRIx64 " r%02u=%016" PRIx64,
            progress_ordinal, chunk, first, (uint64_t)gpr[first],
            first + 1, (uint64_t)gpr[first + 1],
            first + 2, (uint64_t)gpr[first + 2],
            first + 3, (uint64_t)gpr[first + 3],
            first + 4, (uint64_t)gpr[first + 4],
            first + 5, (uint64_t)gpr[first + 5],
            first + 6, (uint64_t)gpr[first + 6],
            first + 7, (uint64_t)gpr[first + 7]);
    }

    for (chunk = 0; chunk < 4; ++chunk) {
        unsigned int first = chunk * 8;
        DdStartupDiagnosticsTraceContext(
            "DDSTART5 context: candidate=%u code%u"
            " s%02u=%s:%08" PRIx32 " s%02u=%s:%08" PRIx32
            " s%02u=%s:%08" PRIx32 " s%02u=%s:%08" PRIx32
            " s%02u=%s:%08" PRIx32 " s%02u=%s:%08" PRIx32
            " s%02u=%s:%08" PRIx32 " s%02u=%s:%08" PRIx32,
            progress_ordinal, chunk,
            first, dd_context_code_status_name(code_status[first]), code[first],
            first + 1, dd_context_code_status_name(code_status[first + 1]), code[first + 1],
            first + 2, dd_context_code_status_name(code_status[first + 2]), code[first + 2],
            first + 3, dd_context_code_status_name(code_status[first + 3]), code[first + 3],
            first + 4, dd_context_code_status_name(code_status[first + 4]), code[first + 4],
            first + 5, dd_context_code_status_name(code_status[first + 5]), code[first + 5],
            first + 6, dd_context_code_status_name(code_status[first + 6]), code[first + 6],
            first + 7, dd_context_code_status_name(code_status[first + 7]), code[first + 7]);
    }
}

/*
 * DDSTART6 scheduler evidence is deliberately independent from the emulator
 * scheduler.  These are guest addresses and guest-layout byte offsets, not
 * host pointers or executable control data.
 *
 * The address identities and instruction fingerprint below are evidence from
 * DDSTART5 capture a4b042448d0b22dce51427a79de78bdd1999dda745fd45f12bd24054938a161e:
 * at sampled PC 0x800679f8, slot 6 is 0x0c030a98 (jal 0x800c2a60),
 * slot 8 is 0x1000ffff, slot 9 is nop, and previous slot 3 is
 * 0x0c031dc4 (jal 0x800c7710).  The JP/rev0 symbol map maps those calls to
 * osSetThreadPri and osStartThread, and maps __osRunQueue to 0x800d1d88 and
 * __osRunningThread to 0x800d1d90.  They are used only after the exact PC
 * and all four nearby words match; a mismatch makes the mapped roots
 * explicitly unavailable.
 *
 * The scheduler fields and fault-context offsets are matched to the public
 * /tmp/fzerox-reference/include/PR/os_thread.h at revision
 * 4fd50c7ca6b44f996aa0fbb68ec86df75855d5b8.  The selected fault context has
 * a 0x128-byte prefix and 29 saved integer slots starting at 0x20 in the
 * at,v0,v1,a0-a3,t0-t7,s0-s7,t8,t9,gp,sp,s8,ra order. OSThread pointers are
 * four-byte guest words; the saved 64-bit values are stored as two direct
 * host uint32_t guest words.  Reading by these explicit offsets avoids
 * host-ABI casts and extracts the state halfword from the guest word.
 * The source-side identity is Idle_ThreadEntry in src/sys/sys_main.c; that
 * function's osSetThreadPri/while-true tail is evidence, not control.
 *
 * The word at 0x800d1d80 is a two-word queue-tail sentinel, not an OSThread.
 * It is never decoded or followed through tlnext.  The active-list head at
 * 0x800d1d8c is used only after the same exact PC/code signature gate, with
 * the adjacent run-queue/running/faulted globals serving as the corroborating
 * thread.c layout.
 */
enum
{
    DD_SCHEDULER_EXPECTED_PC = 0x800679f8,
    DD_SCHEDULER_QUEUE_SENTINEL = 0x800d1d80,
    DD_SCHEDULER_RUN_QUEUE = 0x800d1d88,
    DD_SCHEDULER_ACTIVE_THREAD_HEAD = 0x800d1d8c,
    DD_SCHEDULER_RUNNING_THREAD = 0x800d1d90,
    DD_SCHEDULER_FAULTED_THREAD = 0x800d1d94,
    DD_SCHEDULER_EXPECTED_PREV_JAL = 0x0c031dc4,
    DD_SCHEDULER_EXPECTED_SET_PRI_JAL = 0x0c030a98,
    DD_SCHEDULER_EXPECTED_LOOP = 0x1000ffff,
    DD_SCHEDULER_EXPECTED_NOP = 0x00000000,
    DD_SCHEDULER_THREAD_BYTES = 288,
    DD_SCHEDULER_FAULT_CONTEXT_PREFIX = DD_FAULT_LAYOUT_CONTEXT_PREFIX,
    DD_SCHEDULER_THREAD_NEXT = 0,
    DD_SCHEDULER_THREAD_PRIORITY = 4,
    DD_SCHEDULER_THREAD_QUEUE = 8,
    DD_SCHEDULER_THREAD_TLNEXT = 12,
    DD_SCHEDULER_THREAD_STATE = 16,
    DD_SCHEDULER_THREAD_ID = 20,
    DD_SCHEDULER_THREAD_CONTEXT = DD_FAULT_LAYOUT_GPR_OFFSET,
    DD_SCHEDULER_CONTEXT_GPR_COUNT = DD_FAULT_LAYOUT_GPR_COUNT,
    DD_SCHEDULER_CONTEXT_SP = DD_FAULT_LAYOUT_SP_OFFSET,
    DD_SCHEDULER_CONTEXT_RA = DD_FAULT_LAYOUT_RA_OFFSET,
    DD_SCHEDULER_CONTEXT_LO = DD_FAULT_LAYOUT_LO_OFFSET,
    DD_SCHEDULER_CONTEXT_HI = DD_FAULT_LAYOUT_HI_OFFSET,
    DD_SCHEDULER_CONTEXT_SR = DD_FAULT_LAYOUT_SR_OFFSET,
    DD_SCHEDULER_CONTEXT_PC = DD_FAULT_LAYOUT_PC_OFFSET,
    DD_SCHEDULER_CONTEXT_CAUSE = DD_FAULT_LAYOUT_CAUSE_OFFSET,
    DD_SCHEDULER_CONTEXT_BADVADDR = DD_FAULT_LAYOUT_BADVADDR_OFFSET,
    DD_SCHEDULER_CHAIN_LIMIT = 8,
    DD_SCHEDULER_SEEN_LIMIT = 16
};

struct dd_scheduler_thread_snapshot
{
    uint32_t address;
    uint32_t next;
    uint32_t queue;
    uint32_t tlnext;
    int32_t priority;
    int32_t id;
    uint16_t state;
    uint64_t saved_sp;
    uint64_t saved_ra;
    uint32_t saved_pc;
    uint32_t wait_raw[4];
    int wait_raw_valid;
    int wait_raw_partial;
};

static int dd_scheduler_read_word(const struct r4300_core* r4300,
        uint32_t address, uint32_t* value)
{
    return r4300 != NULL && r4300->rdram != NULL
        && dd_fault_guest_read_u32(r4300->rdram->dram,
            r4300->rdram->dram_size, address, value);
}

static int dd_scheduler_guest_range(const struct r4300_core* r4300,
        uint32_t address, size_t bytes)
{
    return r4300 != NULL && r4300->rdram != NULL
        && dd_fault_guest_range(r4300->rdram->dram,
            r4300->rdram->dram_size, address, bytes, NULL);
}

static const char* dd_scheduler_pointer_state(
        const struct r4300_core* r4300, uint32_t pointer, size_t bytes)
{
    if (pointer == 0)
        return "null";
    return dd_scheduler_guest_range(r4300, pointer, bytes)
        ? "valid" : "invalid";
}

static int dd_scheduler_read_u64(const struct r4300_core* r4300,
        uint32_t address, uint64_t* value)
{
    return r4300 != NULL && r4300->rdram != NULL
        && dd_fault_guest_read_u64(r4300->rdram->dram,
            r4300->rdram->dram_size, address, value);
}

static int dd_scheduler_read_thread(const struct r4300_core* r4300,
        uint32_t address, struct dd_scheduler_thread_snapshot* snapshot)
{
    uint32_t state_flags;
    uint32_t priority;
    uint32_t id;
    uint32_t queue_word;
    unsigned int i;

    if (snapshot == NULL
            || !dd_scheduler_guest_range(r4300, address,
                DD_SCHEDULER_THREAD_BYTES))
        return 0;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->address = address;
    if (!dd_scheduler_read_word(r4300, address + DD_SCHEDULER_THREAD_NEXT,
                &snapshot->next)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_THREAD_QUEUE, &queue_word)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_THREAD_TLNEXT, &snapshot->tlnext)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_THREAD_PRIORITY, &priority)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_THREAD_STATE, &state_flags)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_THREAD_ID, &id)
            || !dd_scheduler_read_u64(r4300,
                address + DD_SCHEDULER_CONTEXT_SP, &snapshot->saved_sp)
            || !dd_scheduler_read_u64(r4300,
                address + DD_SCHEDULER_CONTEXT_RA, &snapshot->saved_ra)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_CONTEXT_PC, &snapshot->saved_pc))
        return 0;

    snapshot->queue = queue_word;
    snapshot->priority = (int32_t)priority;
    snapshot->state = (uint16_t)(state_flags >> 16);
    snapshot->id = (int32_t)id;

    /*
     * OSThread.queue is a pointer to a queue-head slot.  The words below are
     * intentionally only "raw": this snapshot does not infer OSMesgQueue or
     * any undocumented active-queue layout from that pointer.
     */
    if (snapshot->queue == 0)
        return 1;
    if (!dd_scheduler_guest_range(r4300, snapshot->queue,
                sizeof(snapshot->wait_raw))) {
        snapshot->wait_raw_partial = 1;
        return 1;
    }
    snapshot->wait_raw_valid = 1;
    for (i = 0; i < sizeof(snapshot->wait_raw) / sizeof(snapshot->wait_raw[0]);
            ++i) {
        if (!dd_scheduler_read_word(r4300,
                    snapshot->queue + i * sizeof(uint32_t),
                    &snapshot->wait_raw[i])) {
            snapshot->wait_raw_valid = 0;
            snapshot->wait_raw_partial = 1;
            break;
        }
    }
    return 1;
}

static int dd_scheduler_seen(const uint32_t* seen, unsigned int count,
        uint32_t address)
{
    unsigned int i;

    for (i = 0; i < count; ++i) {
        if (seen[i] == address)
            return 1;
    }
    return 0;
}

static const char* dd_scheduler_wait_state(
        const struct dd_scheduler_thread_snapshot* snapshot)
{
    if (snapshot->queue == 0)
        return "null";
    if (snapshot->wait_raw_valid)
        return "available";
    return snapshot->wait_raw_partial ? "partial" : "unavailable";
}

static void dd_scheduler_emit_thread(unsigned int progress_ordinal,
        const char* chain, unsigned int depth,
        const struct dd_scheduler_thread_snapshot* snapshot)
{
    DdStartupDiagnosticsTraceScheduler(
        "DDSTART6 scheduler: candidate=%u chain=%s depth=%u"
        " thread=%08" PRIx32 " id=%" PRId32 " priority=%" PRId32
        " state=%04x queue=%08" PRIx32 " next=%08" PRIx32
        " tlnext=%08" PRIx32 " saved_pc=%08" PRIx32
        " saved_sp=%016" PRIx64 " saved_ra=%016" PRIx64
        " wait_queue_raw_state=%s wait_queue_head_raw=%08" PRIx32
        " wait_queue_raw1=%08" PRIx32 " wait_queue_raw2=%08" PRIx32
        " wait_queue_raw3=%08" PRIx32,
        progress_ordinal, chain, depth, snapshot->address, snapshot->id,
        snapshot->priority, snapshot->state, snapshot->queue, snapshot->next,
        snapshot->tlnext, snapshot->saved_pc, snapshot->saved_sp,
        snapshot->saved_ra, dd_scheduler_wait_state(snapshot),
        snapshot->wait_raw[0], snapshot->wait_raw[1], snapshot->wait_raw[2],
        snapshot->wait_raw[3]);
}

static void dd_scheduler_emit_sentinel(unsigned int progress_ordinal,
        const struct r4300_core* r4300, const char* chain, unsigned int depth)
{
    uint32_t sentinel_next = 0;
    uint32_t sentinel_priority = 0;
    int next_available = dd_scheduler_read_word(r4300,
        DD_SCHEDULER_QUEUE_SENTINEL, &sentinel_next);
    int priority_available = dd_scheduler_read_word(r4300,
        DD_SCHEDULER_QUEUE_SENTINEL + sizeof(uint32_t),
        &sentinel_priority);

    DdStartupDiagnosticsTraceScheduler(
        "DDSTART6 scheduler: candidate=%u chain=%s depth=%u"
        " sentinel=%08" PRIx32 " state=%s"
        " raw_next=%08" PRIx32 " raw_priority=%08" PRIx32
        " result=sentinel",
        progress_ordinal, chain, depth,
        DD_SCHEDULER_QUEUE_SENTINEL,
        next_available && priority_available ? "available" : "partial",
        sentinel_next, sentinel_priority);
}

static const char* dd_scheduler_walk(unsigned int progress_ordinal,
        const struct r4300_core* r4300, const char* chain, uint32_t start,
        int use_tlnext, uint32_t* seen, unsigned int* seen_count)
{
    uint32_t branch_seen[DD_SCHEDULER_CHAIN_LIMIT];
    unsigned int branch_count = 0;
    uint32_t current = start;
    unsigned int depth;
    const char* result = "empty";

    memset(branch_seen, 0, sizeof(branch_seen));
    for (depth = 0; depth < DD_SCHEDULER_CHAIN_LIMIT && current != 0;
            ++depth) {
        struct dd_scheduler_thread_snapshot snapshot;
        uint32_t next;
        const char* pointer_state;

        /*
         * __osRunQueue's root may point at this two-word fake tail object.
         * It has only next/priority words; treating it as an OSThread would
         * reinterpret unrelated data as state/context and follow tlnext into
         * the adjacent globals.  Read only the two sentinel words and stop.
         */
        if (current == DD_SCHEDULER_QUEUE_SENTINEL) {
            dd_scheduler_emit_sentinel(progress_ordinal, r4300, chain, depth);
            result = "sentinel";
            break;
        }

        pointer_state = dd_scheduler_pointer_state(r4300, current,
                DD_SCHEDULER_THREAD_BYTES);
        if (pointer_state != NULL && strcmp(pointer_state, "valid") != 0) {
            DdStartupDiagnosticsTraceScheduler(
                "DDSTART6 scheduler: candidate=%u chain=%s depth=%u"
                " thread=%08" PRIx32 " state=unavailable"
                " reason=invalid-rdram-pointer",
                progress_ordinal, chain, depth, current);
            result = "invalid-pointer";
            break;
        }
        if (dd_scheduler_seen(seen, *seen_count, current)) {
            result = dd_scheduler_seen(branch_seen, branch_count, current)
                ? "cycle" : "visited";
            break;
        }
        if (*seen_count >= DD_SCHEDULER_SEEN_LIMIT) {
            result = "visited-limit";
            break;
        }
        if (!dd_scheduler_read_thread(r4300, current, &snapshot)) {
            DdStartupDiagnosticsTraceScheduler(
                "DDSTART6 scheduler: candidate=%u chain=%s depth=%u"
                " thread=%08" PRIx32 " state=unavailable"
                " reason=guest-read-failed",
                progress_ordinal, chain, depth, current);
            result = "unavailable";
            break;
        }

        seen[(*seen_count)++] = current;
        branch_seen[branch_count++] = current;
        dd_scheduler_emit_thread(progress_ordinal, chain, depth, &snapshot);
        next = use_tlnext ? snapshot.tlnext : snapshot.next;
        if (next == 0) {
            result = "end";
            break;
        }
        if (next == DD_SCHEDULER_QUEUE_SENTINEL) {
            /*
             * The sentinel is intentionally allowed as the terminal next
             * value even though its allocation is only eight bytes.
             * The next loop iteration performs the two-word sentinel read
             * and stops without applying OSThread offsets.
             */
            current = next;
            continue;
        }
        if (!dd_scheduler_guest_range(r4300, next,
                    DD_SCHEDULER_THREAD_BYTES)) {
            result = "invalid-next";
            break;
        }
        if (dd_scheduler_seen(branch_seen, branch_count, next)) {
            result = "cycle";
            break;
        }
        if (dd_scheduler_seen(seen, *seen_count, next)) {
            result = "visited";
            break;
        }
        current = next;
    }

    /*
     * Eight real nodes are the limit.  If the eighth node points at the
     * sentinel, emit that terminal two-word object without admitting a ninth
     * OSThread decode.
     */
    if (current == DD_SCHEDULER_QUEUE_SENTINEL
            && strcmp(result, "sentinel") != 0) {
        dd_scheduler_emit_sentinel(progress_ordinal, r4300, chain, depth);
        result = "sentinel";
    }
    if (current != 0 && depth >= DD_SCHEDULER_CHAIN_LIMIT
            && strcmp(result, "cycle") != 0
            && strcmp(result, "visited") != 0
            && strcmp(result, "invalid-next") != 0
            && strcmp(result, "sentinel") != 0)
        result = "limit";
    DdStartupDiagnosticsTraceScheduler(
        "DDSTART6 scheduler: candidate=%u chain=%s limit=%u result=%s"
        " visited=%u",
        progress_ordinal, chain, DD_SCHEDULER_CHAIN_LIMIT, result,
        *seen_count);
    return result;
}

static void dd_trace_scheduler_snapshot(struct r4300_core* r4300,
        unsigned int progress_ordinal, uint32_t pc_sample, int pc_available)
{
    static const uint32_t fingerprint_address[] = {
        DD_SCHEDULER_EXPECTED_PC - 20,
        DD_SCHEDULER_EXPECTED_PC - 8,
        DD_SCHEDULER_EXPECTED_PC,
        DD_SCHEDULER_EXPECTED_PC + 4
    };
    static const uint32_t fingerprint_expected[] = {
        DD_SCHEDULER_EXPECTED_PREV_JAL,
        DD_SCHEDULER_EXPECTED_SET_PRI_JAL,
        DD_SCHEDULER_EXPECTED_LOOP,
        DD_SCHEDULER_EXPECTED_NOP
    };
    uint32_t fingerprint[sizeof(fingerprint_expected)
        / sizeof(fingerprint_expected[0])];
    uint32_t roots_raw[7];
    uint32_t run_queue = 0;
    uint32_t active_thread = 0;
    uint32_t running_thread = 0;
    uint32_t seen[DD_SCHEDULER_SEEN_LIMIT];
    uint32_t active_seen[DD_SCHEDULER_SEEN_LIMIT];
    unsigned int seen_count = 0;
    unsigned int active_seen_count = 0;
    unsigned int i;
    unsigned int raw_count = 0;
    int fingerprint_available = 1;
    int fingerprint_matches = 1;
    int roots_available;
    int run_queue_available;
    int active_thread_available;
    int running_thread_available;

    if (!DdStartupDiagnosticsEnabled())
        return;

    memset(fingerprint, 0, sizeof(fingerprint));
    if (!pc_available || pc_sample != DD_SCHEDULER_EXPECTED_PC) {
        DdStartupDiagnosticsTraceScheduler(
            "DDSTART6 scheduler: candidate=%u pc_sample=%08" PRIx32
            " pc_state=%s fingerprint=unavailable mapped_roots=unavailable",
            progress_ordinal, pc_sample,
            pc_available ? "mismatch" : "unavailable");
        return;
    }

    for (i = 0; i < sizeof(fingerprint_expected) / sizeof(fingerprint_expected[0]);
            ++i) {
        if (!dd_scheduler_read_word(r4300, fingerprint_address[i],
                    &fingerprint[i])) {
            fingerprint_available = 0;
            continue;
        }
        if (fingerprint[i] != fingerprint_expected[i])
            fingerprint_matches = 0;
    }
    if (!fingerprint_available || !fingerprint_matches) {
        DdStartupDiagnosticsTraceScheduler(
            "DDSTART6 scheduler: candidate=%u pc_sample=%08" PRIx32
            " pc_state=exact fingerprint=%s mapped_roots=unavailable"
            " raw_prev=%08" PRIx32 " raw_setpri=%08" PRIx32
            " raw_loop=%08" PRIx32 " raw_nop=%08" PRIx32,
            progress_ordinal, pc_sample,
            fingerprint_available ? "mismatch" : "unavailable",
            fingerprint[0], fingerprint[1], fingerprint[2], fingerprint[3]);
        return;
    }

    run_queue_available = dd_scheduler_read_word(r4300,
        DD_SCHEDULER_RUN_QUEUE, &run_queue);
    active_thread_available = dd_scheduler_read_word(r4300,
        DD_SCHEDULER_ACTIVE_THREAD_HEAD, &active_thread);
    running_thread_available = dd_scheduler_read_word(r4300,
        DD_SCHEDULER_RUNNING_THREAD, &running_thread);
    roots_available = run_queue_available && running_thread_available;
    if (!roots_available) {
        DdStartupDiagnosticsTraceScheduler(
            "DDSTART6 scheduler: candidate=%u pc_sample=%08" PRIx32
            " pc_state=exact fingerprint=match mapped_roots=unavailable"
            " runQueue_state=%s runningThread_state=%s"
            " activeThreadHead_state=%s",
            progress_ordinal, pc_sample,
            run_queue_available ? "available" : "unavailable",
            running_thread_available ? "available" : "unavailable",
            active_thread_available ? "available" : "unavailable");
        return;
    }

    /*
     * Read one raw neighborhood around the verified root words.  The
     * addresses are contiguous in this map, but the labels remain raw rather
     * than assigning meaning to a possible active-queue variable.
     */
    for (i = 0; i < sizeof(roots_raw) / sizeof(roots_raw[0]); ++i) {
        if (dd_scheduler_read_word(r4300,
                    DD_SCHEDULER_RUN_QUEUE - 8 + i * sizeof(uint32_t),
                    &roots_raw[i]))
            ++raw_count;
        else
            roots_raw[i] = 0;
    }
    DdStartupDiagnosticsTraceScheduler(
        "DDSTART6 scheduler: candidate=%u pc_sample=%08" PRIx32
        " pc_state=exact fingerprint=match mapped_roots=available"
        " runQueue=%08" PRIx32 " runningThread=%08" PRIx32
        " runQueue_ptr_state=%s runningThread_ptr_state=%s"
        " root_raw_base=%08" PRIx32 " root_raw_state=%s"
        " raw0=%08" PRIx32 " raw1=%08" PRIx32
        " raw2=%08" PRIx32 " raw3=%08" PRIx32 " raw4=%08" PRIx32
        " raw5=%08" PRIx32 " raw6=%08" PRIx32
        " activeThreadHead=%08" PRIx32 " activeThreadHead_ptr_state=%s",
        progress_ordinal, pc_sample, run_queue, running_thread,
        dd_scheduler_pointer_state(r4300, run_queue,
            DD_SCHEDULER_THREAD_BYTES),
        dd_scheduler_pointer_state(r4300, running_thread,
            DD_SCHEDULER_THREAD_BYTES),
        DD_SCHEDULER_RUN_QUEUE - 8,
        raw_count == sizeof(roots_raw) / sizeof(roots_raw[0])
            ? "available" : "partial",
        roots_raw[0], roots_raw[1], roots_raw[2], roots_raw[3], roots_raw[4],
        roots_raw[5], roots_raw[6], active_thread,
        dd_scheduler_pointer_state(r4300, active_thread,
            DD_SCHEDULER_THREAD_BYTES));

    memset(seen, 0, sizeof(seen));
    dd_scheduler_walk(progress_ordinal, r4300, "running-tlnext",
        running_thread, 1, seen, &seen_count);
    dd_scheduler_walk(progress_ordinal, r4300, "runQueue-next",
        run_queue, 0, seen, &seen_count);
    if (active_thread_available) {
        memset(active_seen, 0, sizeof(active_seen));
        dd_scheduler_walk(progress_ordinal, r4300, "active-tlnext",
            active_thread, 1, active_seen, &active_seen_count);
    } else {
        DdStartupDiagnosticsTraceScheduler(
            "DDSTART6 scheduler: candidate=%u chain=active-tlnext"
            " limit=%u result=unavailable reason=active-head-read-failed",
            progress_ordinal, DD_SCHEDULER_CHAIN_LIMIT);
    }
}

static int dd_fault_signature_matches(const struct r4300_core* r4300,
        uint32_t fingerprint[4], int* fingerprint_available)
{
    static const uint32_t fingerprint_address[] = {
        DD_SCHEDULER_EXPECTED_PC - 20,
        DD_SCHEDULER_EXPECTED_PC - 8,
        DD_SCHEDULER_EXPECTED_PC,
        DD_SCHEDULER_EXPECTED_PC + 4
    };
    static const uint32_t fingerprint_expected[] = {
        DD_SCHEDULER_EXPECTED_PREV_JAL,
        DD_SCHEDULER_EXPECTED_SET_PRI_JAL,
        DD_SCHEDULER_EXPECTED_LOOP,
        DD_SCHEDULER_EXPECTED_NOP
    };
    unsigned int i;
    int matches = 1;

    if (fingerprint == NULL || fingerprint_available == NULL)
        return 0;

    *fingerprint_available = 1;
    for (i = 0; i < sizeof(fingerprint_expected)
            / sizeof(fingerprint_expected[0]); ++i) {
        if (!dd_scheduler_read_word(r4300, fingerprint_address[i],
                    &fingerprint[i])) {
            *fingerprint_available = 0;
            matches = 0;
        } else if (fingerprint[i] != fingerprint_expected[i]) {
            matches = 0;
        }
    }
    return matches;
}

/*
 * DDSTART7 is the selected-thread counterpart to DDSTART6.  It reads the
 * fault-selected OSThread directly from __osFaultedThread, independently of
 * either list walk's eight-node limit.  The offsets below are explicit guest
 * offsets from the public N64 os_thread.h, not a host-ABI cast.
 */
struct dd_fault_thread_snapshot
{
    uint32_t address;
    int32_t priority;
    int32_t id;
    uint16_t state;
    uint16_t flags;
    uint64_t gpr[DD_SCHEDULER_CONTEXT_GPR_COUNT];
    uint64_t saved_sp;
    uint64_t saved_ra;
    uint64_t saved_lo;
    uint64_t saved_hi;
    uint32_t saved_sr;
    uint32_t saved_cause;
    uint32_t saved_badvaddr;
    uint32_t saved_pc;
    uint32_t code[32];
    enum dd_context_code_status code_status[32];
    unsigned int code_valid;
};

static int dd_fault_read_thread(const struct r4300_core* r4300,
        uint32_t address, struct dd_fault_thread_snapshot* snapshot)
{
    uint32_t state_flags;
    uint32_t priority;
    uint32_t id;
    unsigned int i;

    if (snapshot == NULL || !dd_scheduler_guest_range(r4300, address,
                DD_SCHEDULER_FAULT_CONTEXT_PREFIX))
        return 0;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->address = address;
    if (!dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_THREAD_PRIORITY, &priority)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_THREAD_STATE, &state_flags)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_THREAD_ID, &id)
            || !dd_scheduler_read_u64(r4300,
                address + DD_SCHEDULER_CONTEXT_SP, &snapshot->saved_sp)
            || !dd_scheduler_read_u64(r4300,
                address + DD_SCHEDULER_CONTEXT_RA, &snapshot->saved_ra)
            || !dd_scheduler_read_u64(r4300,
                address + DD_SCHEDULER_CONTEXT_LO, &snapshot->saved_lo)
            || !dd_scheduler_read_u64(r4300,
                address + DD_SCHEDULER_CONTEXT_HI, &snapshot->saved_hi)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_CONTEXT_SR, &snapshot->saved_sr)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_CONTEXT_CAUSE, &snapshot->saved_cause)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_CONTEXT_BADVADDR,
                &snapshot->saved_badvaddr)
            || !dd_scheduler_read_word(r4300,
                address + DD_SCHEDULER_CONTEXT_PC, &snapshot->saved_pc))
        return 0;

    snapshot->priority = (int32_t)priority;
    snapshot->state = (uint16_t)(state_flags >> 16);
    snapshot->flags = (uint16_t)state_flags;
    snapshot->id = (int32_t)id;

    /*
     * The fault context contains 29 saved integer registers in this order:
     * at,v0,v1,a0-a3,t0-t7,s0-s7,t8,t9,gp,sp,s8,ra. The direct guest u64
     * assembly preserves the N64 high/low word representation.
     */
    for (i = 0; i < DD_SCHEDULER_CONTEXT_GPR_COUNT; ++i) {
        if (!dd_scheduler_read_u64(r4300,
                    address + DD_SCHEDULER_THREAD_CONTEXT + i * 8,
                    &snapshot->gpr[i]))
            return 0;
    }
    return 1;
}

static void dd_fault_read_code_window(const struct r4300_core* r4300,
        uint32_t pc, uint32_t code[32],
        enum dd_context_code_status code_status[32], unsigned int* valid_count)
{
    unsigned int pc_segment = 0;
    unsigned int slot;

    if (code == NULL || code_status == NULL || valid_count == NULL)
        return;
    *valid_count = 0;
    memset(code, 0, sizeof(uint32_t) * 32);
    for (slot = 0; slot < 32; ++slot)
        code_status[slot] = DD_CONTEXT_CODE_PC_UNAVAILABLE;

    if ((pc & UINT32_C(3)) != 0) {
        for (slot = 0; slot < 32; ++slot)
            code_status[slot] = DD_CONTEXT_CODE_PC_MISALIGNED;
        return;
    }
    if (!dd_context_map_kseg(pc, NULL, &pc_segment)) {
        for (slot = 0; slot < 32; ++slot)
            code_status[slot] = DD_CONTEXT_CODE_NOT_KSEG;
        return;
    }

    for (slot = 0; slot < 32; ++slot) {
        uint32_t address;
        uint32_t physical;
        unsigned int segment;
        uint32_t delta;

        if (slot < 8) {
            delta = (8 - slot) * 4;
            if (pc < delta) {
                code_status[slot] = DD_CONTEXT_CODE_ADDRESS_UNDERFLOW;
                continue;
            }
            address = pc - delta;
        } else {
            delta = (slot - 8) * 4;
            if (pc > UINT32_MAX - delta) {
                code_status[slot] = DD_CONTEXT_CODE_ADDRESS_OVERFLOW;
                continue;
            }
            address = pc + delta;
        }

        if (!dd_context_map_kseg(address, &physical, &segment)) {
            code_status[slot] = DD_CONTEXT_CODE_NOT_KSEG;
            continue;
        }
        if (segment != pc_segment) {
            code_status[slot] = DD_CONTEXT_CODE_CROSSED_KSEG;
            continue;
        }
        if (r4300 == NULL || r4300->rdram == NULL
                || r4300->rdram->dram == NULL) {
            code_status[slot] = DD_CONTEXT_CODE_RDRAM_UNAVAILABLE;
            continue;
        }
        if ((size_t)physical > r4300->rdram->dram_size
                || r4300->rdram->dram_size - (size_t)physical
                    < sizeof(uint32_t)) {
            code_status[slot] = DD_CONTEXT_CODE_RDRAM_OUT_OF_RANGE;
            continue;
        }
        code[slot] = r4300->rdram->dram[physical / sizeof(uint32_t)];
        code_status[slot] = DD_CONTEXT_CODE_OK;
        ++*valid_count;
    }
}

static void dd_fault_emit_snapshot(unsigned int progress_ordinal,
        const struct dd_fault_thread_snapshot* snapshot)
{
    unsigned int chunk;

    DdStartupDiagnosticsTraceFault(
        "DDSTART7 fault: candidate=%u structured=available"
        " fault_thread=%08" PRIx32 " id=%" PRId32
        " priority=%" PRId32 " state=%04x flags=%04x"
        " saved_sr=%08" PRIx32 " saved_cause=%08" PRIx32
        " saved_badvaddr=%08" PRIx32 " saved_pc=%08" PRIx32
        " saved_sp=%016" PRIx64 " saved_ra=%016" PRIx64
        " saved_lo=%016" PRIx64 " saved_hi=%016" PRIx64
        " saved_cause_source=thread_context"
        " code_state=%s code_valid=%u",
        progress_ordinal, snapshot->address, snapshot->id,
        snapshot->priority, snapshot->state, snapshot->flags,
        snapshot->saved_sr, snapshot->saved_cause, snapshot->saved_badvaddr,
        snapshot->saved_pc, snapshot->saved_sp, snapshot->saved_ra,
        snapshot->saved_lo, snapshot->saved_hi,
        snapshot->code_valid == 32 ? "available"
            : snapshot->code_valid != 0 ? "partial" : "unavailable",
        snapshot->code_valid);

    for (chunk = 0; chunk < 4; ++chunk) {
        unsigned int first = chunk * 8;
        unsigned int slot;
        size_t used = 0;
        char registers[256] = {0};
        for (slot = first; slot < first + 8
                && slot < DD_SCHEDULER_CONTEXT_GPR_COUNT; ++slot) {
            int written = snprintf(registers + used, sizeof(registers) - used,
                " slot%02u=%016" PRIx64, slot, snapshot->gpr[slot]);
            if (written < 0 || (size_t)written >= sizeof(registers) - used)
                break; /* At most 8 fixed-size slots fit in this buffer. */
            used += (size_t)written;
        }
        DdStartupDiagnosticsTraceFault(
            "DDSTART7 fault: candidate=%u gpr%u%s",
            progress_ordinal, chunk, registers);
    }

    for (chunk = 0; chunk < 4; ++chunk) {
        unsigned int first = chunk * 8;
        DdStartupDiagnosticsTraceFault(
            "DDSTART7 fault: candidate=%u code%u"
            " s%02u=%s:%08" PRIx32 " s%02u=%s:%08" PRIx32
            " s%02u=%s:%08" PRIx32 " s%02u=%s:%08" PRIx32
            " s%02u=%s:%08" PRIx32 " s%02u=%s:%08" PRIx32
            " s%02u=%s:%08" PRIx32 " s%02u=%s:%08" PRIx32,
            progress_ordinal, chunk,
            first, dd_context_code_status_name(snapshot->code_status[first]),
            snapshot->code[first],
            first + 1, dd_context_code_status_name(snapshot->code_status[first + 1]),
            snapshot->code[first + 1],
            first + 2, dd_context_code_status_name(snapshot->code_status[first + 2]),
            snapshot->code[first + 2],
            first + 3, dd_context_code_status_name(snapshot->code_status[first + 3]),
            snapshot->code[first + 3],
            first + 4, dd_context_code_status_name(snapshot->code_status[first + 4]),
            snapshot->code[first + 4],
            first + 5, dd_context_code_status_name(snapshot->code_status[first + 5]),
            snapshot->code[first + 5],
            first + 6, dd_context_code_status_name(snapshot->code_status[first + 6]),
            snapshot->code[first + 6],
            first + 7, dd_context_code_status_name(snapshot->code_status[first + 7]),
            snapshot->code[first + 7]);
    }
}

static void dd_trace_fault_snapshot(struct r4300_core* r4300,
        unsigned int progress_ordinal, uint32_t pc_sample, int pc_available)
{
    uint32_t fingerprint[4] = {0, 0, 0, 0};
    uint32_t fault_thread;
    struct dd_fault_thread_snapshot snapshot;
    int fingerprint_available = 1;
    int signature_matches;
    int root_available;

    if (!DdStartupDiagnosticsEnabled())
        return;

    if (!pc_available || pc_sample != DD_SCHEDULER_EXPECTED_PC) {
        DdStartupDiagnosticsTraceFault(
            "DDSTART7 fault: candidate=%u structured=unavailable"
            " reason=pc-signature-mismatch pc_sample=%08" PRIx32
            " pc_state=%s",
            progress_ordinal, pc_sample,
            pc_available ? "mismatch" : "unavailable");
        return;
    }

    signature_matches = dd_fault_signature_matches(r4300, fingerprint,
        &fingerprint_available);
    if (!signature_matches) {
        DdStartupDiagnosticsTraceFault(
            "DDSTART7 fault: candidate=%u structured=unavailable"
            " reason=code-signature-%s pc_sample=%08" PRIx32
            " raw_prev=%08" PRIx32 " raw_setpri=%08" PRIx32
            " raw_loop=%08" PRIx32 " raw_nop=%08" PRIx32,
            progress_ordinal, fingerprint_available ? "mismatch" : "unavailable",
            pc_sample, fingerprint[0], fingerprint[1], fingerprint[2],
            fingerprint[3]);
        return;
    }

    root_available = dd_scheduler_read_word(r4300,
        DD_SCHEDULER_FAULTED_THREAD, &fault_thread);
    if (!root_available) {
        DdStartupDiagnosticsTraceFault(
            "DDSTART7 fault: candidate=%u structured=unavailable"
            " reason=fault-pointer-read-failed"
            " fault_root=%08" PRIx32 " signature=match",
            progress_ordinal, DD_SCHEDULER_FAULTED_THREAD);
        return;
    }
    if (fault_thread == 0 || fault_thread == DD_SCHEDULER_QUEUE_SENTINEL
            || !dd_scheduler_guest_range(r4300, fault_thread,
                DD_SCHEDULER_FAULT_CONTEXT_PREFIX)) {
        DdStartupDiagnosticsTraceFault(
            "DDSTART7 fault: candidate=%u structured=unavailable"
            " reason=%s fault_thread=%08" PRIx32
            " pointer_state=%s signature=match",
            progress_ordinal, fault_thread == 0 ? "null-fault-pointer"
                : fault_thread == DD_SCHEDULER_QUEUE_SENTINEL
                    ? "sentinel-fault-pointer" : "invalid-fault-pointer",
            fault_thread, fault_thread == 0 ? "null" : "invalid");
        return;
    }
    if (!dd_fault_read_thread(r4300, fault_thread, &snapshot)) {
        DdStartupDiagnosticsTraceFault(
            "DDSTART7 fault: candidate=%u structured=unavailable"
            " reason=guest-thread-read-failed fault_thread=%08" PRIx32
            " pointer_state=valid signature=match",
            progress_ordinal, fault_thread);
        return;
    }

    dd_fault_read_code_window(r4300, snapshot.saved_pc, snapshot.code,
        snapshot.code_status, &snapshot.code_valid);
    dd_fault_emit_snapshot(progress_ordinal, &snapshot);
}

static void dd_trace_interrupt_progress(struct r4300_core* r4300,
                                        unsigned int event_type)
{
    uint32_t pc_sample = 0;
    const uint32_t* cp0_regs;
    unsigned int progress_ordinal;
    int pc_available;

    if (!DdStartupDiagnosticsEnabled())
        return;

    /*
     * gen_interrupt is an existing emulation-thread boundary.  This is
     * intentionally a sample, not an instruction hook or an exact writer
     * attribution; compiled stores may occur between these boundaries.
     */
    pc_available = dd_context_sample_pc(r4300, &pc_sample);
    cp0_regs = r4300_cp0_regs(&r4300->cp0);
    progress_ordinal = DdStartupDiagnosticsNextProgressOrdinal();
    if (progress_ordinal == 65536 || progress_ordinal == 1048576) {
        dd_trace_context_snapshot(r4300, progress_ordinal, pc_sample,
            pc_available);
        dd_trace_scheduler_snapshot(r4300, progress_ordinal, pc_sample,
            pc_available);
        dd_trace_fault_snapshot(r4300, progress_ordinal, pc_sample,
            pc_available);
    }
    DdStartupDiagnosticsTrace(DD_TRACE_PROGRESS, DD_TRACE_PROGRESS_SPARSE,
        "DDSTART3 progress: boundary=interrupt event=%u cp0_count=%08"
        PRIx32 " cause=%08" PRIx32 " pc_sample=%08" PRIx32
        " pc_is_exact_writer=false",
        event_type, cp0_regs[CP0_COUNT_REG], cp0_regs[CP0_CAUSE_REG],
        pc_sample);
}

void gen_interrupt(struct r4300_core* r4300)
{
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0);
    unsigned int* cp0_next_interrupt = r4300_cp0_next_interrupt(&r4300->cp0);
    int* cp0_cycle_count = r4300_cp0_cycle_count(&r4300->cp0);

    if (r4300->cp0.q.first != NULL)
        dd_trace_interrupt_progress(r4300, r4300->cp0.q.first->data.type);

    if (*r4300_stop(r4300) == 1)
    {
        g_gs_vi_counter = 0; // debug
#ifndef NO_ASM
#ifndef NEW_DYNAREC
        dyna_stop(r4300);
#endif
#endif
    }

    if (!r4300->cp0.interrupt_unsafe_state)
    {
        if (savestates_get_job() == savestates_job_load)
        {
            savestates_load();
            return;
        }

        if (r4300->reset_hard_job)
        {
            call_interrupt_handler(&r4300->cp0, 11);
            return;
        }
    }

    if (r4300->skip_jump)
    {
        uint32_t dest = r4300->skip_jump;
        r4300->skip_jump = 0;

        *cp0_next_interrupt = (r4300->cp0.q.first != NULL)
            ? r4300->cp0.q.first->data.count
            : 0;

        *cp0_cycle_count = (r4300->cp0.q.first != NULL)
            ? (cp0_regs[CP0_COUNT_REG] - r4300->cp0.q.first->data.count)
            : 0;

        r4300->cp0.last_addr = dest;
        generic_jump_to(r4300, dest);
        return;
    }

    switch (r4300->cp0.q.first->data.type)
    {
        case VI_INT:
            call_interrupt_handler(&r4300->cp0, 0);
            break;

        case COMPARE_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 1);
            break;

        case CHECK_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 2);
            break;

        case SI_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 3);
            break;

        case PI_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 4);
            break;

        case SPECIAL_INT:
            call_interrupt_handler(&r4300->cp0, 5);
            break;

        case AI_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 6);
            break;

        case SP_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 7);
            break;

        case DP_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 8);
            break;

        case HW2_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 9);
            break;

        case NMI_INT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 10);
            break;

        case RSP_DMA_EVT:
            remove_interrupt_event(&r4300->cp0);
            call_interrupt_handler(&r4300->cp0, 12);
            break;

        default:
            DebugMessage(M64MSG_ERROR, "Unknown interrupt queue event type %.8X.", r4300->cp0.q.first->data.type);
            remove_interrupt_event(&r4300->cp0);
            exception_general(r4300);
            break;
    }

    if (!r4300->cp0.interrupt_unsafe_state)
    {
        if (savestates_get_job() == savestates_job_save)
        {
            savestates_save();
            return;
        }
    }
}

