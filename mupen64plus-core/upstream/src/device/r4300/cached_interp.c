/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - cached_interp.c                                         *
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

#include "cached_interp.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <fcntl.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <string.h>

#include "api/callbacks.h"
#include "api/debugger.h"
#include "api/m64p_types.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/cp0.h"
#include "device/r4300/n64dd_dispatch_diag.h"
#include "device/r4300/idec.h"
#include "device/rcp/vi/vi_controller.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/pi/pi_controller.h"
#include "device/rcp/rsp/rsp_core.h"
#include "device/dd/dd_controller.h"
#include "main/main.h"
#include "osal/preproc.h"

#ifdef DBG
#include "debugger/dbg_debugger.h"
#endif

// -----------------------------------------------------------
// Cached interpreter functions (and fallback for dynarec).
// -----------------------------------------------------------
#ifdef DBG
#define UPDATE_DEBUGGER() if (g_DebuggerActive) update_debugger(*r4300_pc(r4300))
#else
#define UPDATE_DEBUGGER() do { } while(0)
#endif

#define DECLARE_R4300 struct r4300_core* r4300 = &g_dev.r4300;
#define PCADDR *r4300_pc(r4300)
#ifdef NEW_DYNAREC
#define ADD_TO_PC(x) \
    if (r4300->emumode != EMUMODE_DYNAREC) \
      (*r4300_pc_struct(r4300)) += x; \
    else \
    { \
      assert(*r4300_pc_struct(r4300) == &r4300->new_dynarec_hot_state.fake_pc); \
      r4300->new_dynarec_hot_state.pcaddr += x*4; \
    }
#else
#define ADD_TO_PC(x) (*r4300_pc_struct(r4300)) += x;
#endif
#define DECLARE_INSTRUCTION(name) void cached_interp_##name(void)

#define DECLARE_JUMP(name, destination, condition, link, likely, cop1) \
void cached_interp_##name(void) \
{ \
    DECLARE_R4300 \
    const int take_jump = (condition); \
    const uint32_t jump_target = (destination); \
    int64_t *link_register = (link); \
    if (cop1 && check_cop1_unusable(r4300)) return; \
    if (link_register != &r4300_regs(r4300)[0]) \
    { \
        *link_register = SE32(*r4300_pc(r4300) + 8); \
    } \
    if (!likely || take_jump) \
    { \
        (*r4300_pc_struct(r4300))++; \
        r4300->delay_slot=1; \
        const uint32_t n64dd_delay_pc = (*r4300_pc_struct(r4300))->addr; \
        if (g_dev.dd.idisk != NULL) \
            n64dd_dispatch_diag_step(r4300, n64dd_delay_pc, \
                                     N64DD_DISPATCH_DIAG_DELAY_PRE); \
        UPDATE_DEBUGGER(); \
        (*r4300_pc_struct(r4300))->ops(); \
        if (g_dev.dd.idisk != NULL) \
            n64dd_dispatch_diag_step(r4300, n64dd_delay_pc, \
                                     N64DD_DISPATCH_DIAG_DELAY_POST); \
        cp0_update_count(r4300); \
        r4300->delay_slot=0; \
        if (take_jump && !r4300->skip_jump) \
        { \
            (*r4300_pc_struct(r4300))=r4300->cached_interp.actual->block+((jump_target-r4300->cached_interp.actual->start)>>2); \
        } \
    } \
    else \
    { \
        (*r4300_pc_struct(r4300)) += 2; \
        cp0_update_count(r4300); \
    } \
    r4300->cp0.last_addr = *r4300_pc(r4300); \
    if (*r4300_cp0_cycle_count(&r4300->cp0) >= 0) gen_interrupt(r4300); \
} \
 \
void cached_interp_##name##_OUT(void) \
{ \
    DECLARE_R4300 \
    const int take_jump = (condition); \
    const uint32_t jump_target = (destination); \
    int64_t *link_register = (link); \
    if (cop1 && check_cop1_unusable(r4300)) return; \
    if (link_register != &r4300_regs(r4300)[0]) \
    { \
        *link_register = SE32(*r4300_pc(r4300) + 8); \
    } \
    if (!likely || take_jump) \
    { \
        (*r4300_pc_struct(r4300))++; \
        r4300->delay_slot=1; \
        const uint32_t n64dd_delay_pc = (*r4300_pc_struct(r4300))->addr; \
        if (g_dev.dd.idisk != NULL) \
            n64dd_dispatch_diag_step(r4300, n64dd_delay_pc, \
                                     N64DD_DISPATCH_DIAG_DELAY_PRE); \
        UPDATE_DEBUGGER(); \
        (*r4300_pc_struct(r4300))->ops(); \
        if (g_dev.dd.idisk != NULL) \
            n64dd_dispatch_diag_step(r4300, n64dd_delay_pc, \
                                     N64DD_DISPATCH_DIAG_DELAY_POST); \
        cp0_update_count(r4300); \
        r4300->delay_slot=0; \
        if (take_jump && !r4300->skip_jump) \
        { \
            generic_jump_to(r4300, jump_target); \
        } \
    } \
    else \
    { \
        (*r4300_pc_struct(r4300)) += 2; \
        cp0_update_count(r4300); \
    } \
    r4300->cp0.last_addr = *r4300_pc(r4300); \
    if (*r4300_cp0_cycle_count(&r4300->cp0) >= 0) gen_interrupt(r4300); \
} \
  \
void cached_interp_##name##_IDLE(void) \
{ \
    DECLARE_R4300 \
    uint32_t* cp0_regs = r4300_cp0_regs(&r4300->cp0); \
    int* cp0_cycle_count = r4300_cp0_cycle_count(&r4300->cp0); \
    const int take_jump = (condition); \
    if (cop1 && check_cop1_unusable(r4300)) return; \
    if (take_jump) \
    { \
        cp0_update_count(r4300); \
        if(*cp0_cycle_count < 0) \
        { \
            cp0_regs[CP0_COUNT_REG] -= *cp0_cycle_count; \
            *cp0_cycle_count = 0; \
        } \
    } \
    cached_interp_##name(); \
}

/* These macros allow direct access to parsed opcode fields. */
#define rrt *(*r4300_pc_struct(r4300))->f.r.rt
#define rrd *(*r4300_pc_struct(r4300))->f.r.rd
#define rfs (*r4300_pc_struct(r4300))->f.r.nrd
#define rrs *(*r4300_pc_struct(r4300))->f.r.rs
#define rsa (*r4300_pc_struct(r4300))->f.r.sa
#define irt *(*r4300_pc_struct(r4300))->f.i.rt
#define ioffset (*r4300_pc_struct(r4300))->f.i.immediate
#define iimmediate (*r4300_pc_struct(r4300))->f.i.immediate
#define irs *(*r4300_pc_struct(r4300))->f.i.rs
#define ibase *(*r4300_pc_struct(r4300))->f.i.rs
#define jinst_index (*r4300_pc_struct(r4300))->f.j.inst_index
#define lfbase (*r4300_pc_struct(r4300))->f.lf.base
#define lfft (*r4300_pc_struct(r4300))->f.lf.ft
#define lfoffset (*r4300_pc_struct(r4300))->f.lf.offset
#define cfft (*r4300_pc_struct(r4300))->f.cf.ft
#define cffs (*r4300_pc_struct(r4300))->f.cf.fs
#define cffd (*r4300_pc_struct(r4300))->f.cf.fd

/* 32 bits macros */
#ifndef M64P_BIG_ENDIAN
#define rrt32 *((int32_t*) (*r4300_pc_struct(r4300))->f.r.rt)
#define rrd32 *((int32_t*) (*r4300_pc_struct(r4300))->f.r.rd)
#define rrs32 *((int32_t*) (*r4300_pc_struct(r4300))->f.r.rs)
#define irs32 *((int32_t*) (*r4300_pc_struct(r4300))->f.i.rs)
#define irt32 *((int32_t*) (*r4300_pc_struct(r4300))->f.i.rt)
#else
#define rrt32 *((int32_t*) (*r4300_pc_struct(r4300))->f.r.rt + 1)
#define rrd32 *((int32_t*) (*r4300_pc_struct(r4300))->f.r.rd + 1)
#define rrs32 *((int32_t*) (*r4300_pc_struct(r4300))->f.r.rs + 1)
#define irs32 *((int32_t*) (*r4300_pc_struct(r4300))->f.i.rs + 1)
#define irt32 *((int32_t*) (*r4300_pc_struct(r4300))->f.i.rt + 1)
#endif

#include "mips_instructions.def"

// -----------------------------------------------------------
// Flow control 'fake' instructions
// -----------------------------------------------------------
void cached_interp_FIN_BLOCK(void)
{
    DECLARE_R4300
    if (!r4300->delay_slot)
    {
        generic_jump_to(r4300, ((*r4300_pc_struct(r4300))-1)->addr+4);
/*
#ifdef DBG
      if (g_DebuggerActive) update_debugger(*r4300_pc(r4300));
#endif
Used by dynarec only, check should be unnecessary
*/
        (*r4300_pc_struct(r4300))->ops();
    }
    else
    {
        struct precomp_block *blk = r4300->cached_interp.actual;
        struct precomp_instr *inst = (*r4300_pc_struct(r4300));
        generic_jump_to(r4300, ((*r4300_pc_struct(r4300))-1)->addr+4);

/*
#ifdef DBG
          if (g_DebuggerActive) update_debugger(*r4300_pc(r4300));
#endif
Used by dynarec only, check should be unnecessary
*/
        if (!r4300->skip_jump)
        {
            (*r4300_pc_struct(r4300))->ops();
            r4300->cached_interp.actual = blk;
            (*r4300_pc_struct(r4300)) = inst+1;
        }
        else
            (*r4300_pc_struct(r4300))->ops();
    }
}

void cached_interp_NOTCOMPILED(void)
{
    DECLARE_R4300
    uint32_t *mem = fast_mem_access(r4300, r4300->cached_interp.blocks[*r4300_pc(r4300)>>12]->start);
#ifdef DBG
    DebugMessage(M64MSG_INFO, "NOTCOMPILED: addr = %x ops = %lx", *r4300_pc(r4300), (long) (*r4300_pc_struct(r4300))->ops);
#endif

    if (mem == NULL) {
        DebugMessage(M64MSG_ERROR, "not compiled exception");
    }
    else {
        r4300->cached_interp.recompile_block(r4300, mem, r4300->cached_interp.blocks[*r4300_pc(r4300) >> 12], *r4300_pc(r4300));
    }

/*
#ifdef DBG
      if (g_DebuggerActive) update_debugger(*r4300_pc(r4300));
#endif
The preceeding update_debugger SHOULD be unnecessary since it should have been
called before NOTCOMPILED would have been executed
*/
    (*r4300_pc_struct(r4300))->ops();
}

void cached_interp_NOTCOMPILED2(void)
{
    cached_interp_NOTCOMPILED();
}

/* TODO: implement them properly */
#define cached_interp_BC0F        cached_interp_NI
#define cached_interp_BC0F_IDLE   cached_interp_NI
#define cached_interp_BC0F_OUT    cached_interp_NI
#define cached_interp_BC0FL       cached_interp_NI
#define cached_interp_BC0FL_IDLE  cached_interp_NI
#define cached_interp_BC0FL_OUT   cached_interp_NI
#define cached_interp_BC0T        cached_interp_NI
#define cached_interp_BC0T_IDLE   cached_interp_NI
#define cached_interp_BC0T_OUT    cached_interp_NI
#define cached_interp_BC0TL       cached_interp_NI
#define cached_interp_BC0TL_IDLE  cached_interp_NI
#define cached_interp_BC0TL_OUT   cached_interp_NI
#define cached_interp_BC2F        cached_interp_NI
#define cached_interp_BC2F_IDLE   cached_interp_NI
#define cached_interp_BC2F_OUT    cached_interp_NI
#define cached_interp_BC2FL       cached_interp_NI
#define cached_interp_BC2FL_IDLE  cached_interp_NI
#define cached_interp_BC2FL_OUT   cached_interp_NI
#define cached_interp_BC2T        cached_interp_NI
#define cached_interp_BC2T_IDLE   cached_interp_NI
#define cached_interp_BC2T_OUT    cached_interp_NI
#define cached_interp_BC2TL       cached_interp_NI
#define cached_interp_BC2TL_IDLE  cached_interp_NI
#define cached_interp_BC2TL_OUT   cached_interp_NI
#define cached_interp_BREAK       cached_interp_NI
#define cached_interp_CFC0        cached_interp_NI
#define cached_interp_CFC2        cached_interp_NI
#define cached_interp_CTC0        cached_interp_NI
#define cached_interp_CTC2        cached_interp_NI
#define cached_interp_DMFC0       cached_interp_NI
#define cached_interp_DMFC2       cached_interp_NI
#define cached_interp_DMTC0       cached_interp_NI
#define cached_interp_DMTC2       cached_interp_NI
#define cached_interp_LDC2        cached_interp_NI
#define cached_interp_LWC2        cached_interp_NI
#define cached_interp_LLD         cached_interp_NI
#define cached_interp_MFC2        cached_interp_NI
#define cached_interp_MTC2        cached_interp_NI
#define cached_interp_SCD         cached_interp_NI
#define cached_interp_SDC2        cached_interp_NI
#define cached_interp_SWC2        cached_interp_NI
#define cached_interp_JR_IDLE     cached_interp_NI
#define cached_interp_JALR_IDLE   cached_interp_NI
#define cached_interp_CP1_ABS     cached_interp_RESERVED
#define cached_interp_CP1_ADD     cached_interp_RESERVED
#define cached_interp_CP1_CEIL_L  cached_interp_RESERVED
#define cached_interp_CP1_CEIL_W  cached_interp_RESERVED
#define cached_interp_CP1_C_EQ    cached_interp_RESERVED
#define cached_interp_CP1_C_F     cached_interp_RESERVED
#define cached_interp_CP1_C_LE    cached_interp_RESERVED
#define cached_interp_CP1_C_LT    cached_interp_RESERVED
#define cached_interp_CP1_C_NGE   cached_interp_RESERVED
#define cached_interp_CP1_C_NGL   cached_interp_RESERVED
#define cached_interp_CP1_C_NGLE  cached_interp_RESERVED
#define cached_interp_CP1_C_NGT   cached_interp_RESERVED
#define cached_interp_CP1_C_OLE   cached_interp_RESERVED
#define cached_interp_CP1_C_OLT   cached_interp_RESERVED
#define cached_interp_CP1_C_SEQ   cached_interp_RESERVED
#define cached_interp_CP1_C_SF    cached_interp_RESERVED
#define cached_interp_CP1_C_UEQ   cached_interp_RESERVED
#define cached_interp_CP1_C_ULE   cached_interp_RESERVED
#define cached_interp_CP1_C_ULT   cached_interp_RESERVED
#define cached_interp_CP1_C_UN    cached_interp_RESERVED
#define cached_interp_CP1_CVT_D   cached_interp_RESERVED
#define cached_interp_CP1_CVT_L   cached_interp_RESERVED
#define cached_interp_CP1_CVT_S   cached_interp_RESERVED
#define cached_interp_CP1_CVT_W   cached_interp_RESERVED
#define cached_interp_CP1_DIV     cached_interp_RESERVED
#define cached_interp_CP1_FLOOR_L cached_interp_RESERVED
#define cached_interp_CP1_FLOOR_W cached_interp_RESERVED
#define cached_interp_CP1_MOV     cached_interp_RESERVED
#define cached_interp_CP1_MUL     cached_interp_RESERVED
#define cached_interp_CP1_NEG     cached_interp_RESERVED
#define cached_interp_CP1_ROUND_L cached_interp_RESERVED
#define cached_interp_CP1_ROUND_W cached_interp_RESERVED
#define cached_interp_CP1_SQRT    cached_interp_RESERVED
#define cached_interp_CP1_SUB     cached_interp_RESERVED
#define cached_interp_CP1_TRUNC_L cached_interp_RESERVED
#define cached_interp_CP1_TRUNC_W cached_interp_RESERVED

#define X(op) cached_interp_##op
static void (*const ci_table[R4300_OPCODES_COUNT])(void) =
{
    #include "opcodes.md"
};
#undef X

/* return 0:normal, 1:idle, 2:out */
static int infer_jump_sub_type(uint32_t target, uint32_t pc, uint32_t next_iw, const struct precomp_block* block)
{
    /* test if jumping to same location with empty delay slot */
    if (target == pc) {
        if (next_iw == 0) {
            return 1;
        }
    }
    else {
        /* test if target is outside of block, or if we're at the end of block */
        if (target < block->start || target >= block->end || (pc == (block->end - 4))) {
            return 2;
        }
    }

    /* regular jump */
    return 0;
}

enum r4300_opcode r4300_decode(struct precomp_instr* inst, struct r4300_core* r4300, const struct r4300_idec* idec, uint32_t iw, uint32_t next_iw, const struct precomp_block* block)
{
    /* assume instr->addr is already setup */
    uint8_t dummy;
    enum r4300_opcode opcode = idec->opcode;

    switch(idec->opcode)
    {
    case R4300_OP_JALR:
        /* use the OUT version since we don't know until runtime
         * if we're going to jump inside or outside of block */
        opcode += 2;
        inst->f.r.rs = IDEC_U53(r4300, iw, idec->u53[2], &dummy);
        inst->f.r.rt = IDEC_U53(r4300, iw, idec->u53[1], &dummy);
        inst->f.r.rd = IDEC_U53(r4300, iw, idec->u53[0], &inst->f.r.nrd);
        idec_u53(iw, idec->u53[3], &inst->f.r.sa);
        break;

    case R4300_OP_JR:
        /* use the OUT version since we don't know until runtime
         * if we're going to jump inside or outside of block */
        opcode += 2;

        /* XXX: mips_instruction.def expects i-type */
        inst->f.i.rs = IDEC_U53(r4300, iw, idec->u53[2], &dummy);
        inst->f.i.rt = IDEC_U53(r4300, iw, idec->u53[1], &dummy);
        inst->f.i.immediate  = (int16_t)iw;
        break;

    case R4300_OP_J:
    case R4300_OP_JAL:
        inst->f.j.inst_index  = (iw & UINT32_C(0x3ffffff));
        /* select normal, idle or out jump type */
        opcode += infer_jump_sub_type((inst->addr & ~0xfffffff) | (idec_imm(iw, idec) & 0xfffffff), inst->addr, next_iw, block);
        break;

    case R4300_OP_BC0F:
    case R4300_OP_BC0FL:
    case R4300_OP_BC0T:
    case R4300_OP_BC0TL:
    case R4300_OP_BC1F:
    case R4300_OP_BC1FL:
    case R4300_OP_BC1T:
    case R4300_OP_BC1TL:
    case R4300_OP_BC2F:
    case R4300_OP_BC2FL:
    case R4300_OP_BC2T:
    case R4300_OP_BC2TL:
    case R4300_OP_BEQ:
    case R4300_OP_BEQL:
    case R4300_OP_BGEZ:
    case R4300_OP_BGEZAL:
    case R4300_OP_BGEZALL:
    case R4300_OP_BGEZL:
    case R4300_OP_BGTZ:
    case R4300_OP_BGTZL:
    case R4300_OP_BLEZ:
    case R4300_OP_BLEZL:
    case R4300_OP_BLTZ:
    case R4300_OP_BLTZAL:
    case R4300_OP_BLTZALL:
    case R4300_OP_BLTZL:
    case R4300_OP_BNE:
    case R4300_OP_BNEL:
        inst->f.i.rs = IDEC_U53(r4300, iw, idec->u53[2], &dummy);
        inst->f.i.rt = IDEC_U53(r4300, iw, idec->u53[1], &dummy);
        inst->f.i.immediate  = (int16_t)iw;

        /* select normal, idle or out branch type */
        opcode += infer_jump_sub_type(inst->addr + inst->f.i.immediate*4 + 4, inst->addr, next_iw, block);
        break;

    case R4300_OP_ADD:
    case R4300_OP_ADDU:
    case R4300_OP_AND:
    case R4300_OP_DADD:
    case R4300_OP_DADDU:
    case R4300_OP_DSLL:
    case R4300_OP_DSLL32:
    case R4300_OP_DSLLV:
    case R4300_OP_DSRA:
    case R4300_OP_DSRA32:
    case R4300_OP_DSRAV:
    case R4300_OP_DSRL:
    case R4300_OP_DSRL32:
    case R4300_OP_DSRLV:
    case R4300_OP_DSUB:
    case R4300_OP_DSUBU:
    case R4300_OP_MFHI:
    case R4300_OP_MFLO:
    case R4300_OP_NOR:
    case R4300_OP_OR:
    case R4300_OP_SLL:
    case R4300_OP_SLLV:
    case R4300_OP_SLT:
    case R4300_OP_SLTU:
    case R4300_OP_SRA:
    case R4300_OP_SRAV:
    case R4300_OP_SRL:
    case R4300_OP_SRLV:
    case R4300_OP_SUB:
    case R4300_OP_SUBU:
    case R4300_OP_XOR:
        inst->f.r.rs = IDEC_U53(r4300, iw, idec->u53[2], &dummy);
        inst->f.r.rt = IDEC_U53(r4300, iw, idec->u53[1], &dummy);
        inst->f.r.rd = IDEC_U53(r4300, iw, idec->u53[0], &inst->f.r.nrd);
        idec_u53(iw, idec->u53[3], &inst->f.r.sa);

        /* optimization: nopify instruction when r0 is the destination register (rd) */
        if (inst->f.r.nrd == 0) { opcode = R4300_OP_NOP; }
        break;

    case R4300_OP_ADDI:
    case R4300_OP_ADDIU:
    case R4300_OP_ANDI:
    case R4300_OP_DADDI:
    case R4300_OP_DADDIU:
    case R4300_OP_LB:
    case R4300_OP_LBU:
    case R4300_OP_LD:
    case R4300_OP_LDL:
    case R4300_OP_LDR:
    case R4300_OP_LH:
    case R4300_OP_LHU:
    case R4300_OP_LL:
    case R4300_OP_LLD:
    case R4300_OP_LUI:
    case R4300_OP_LW:
    case R4300_OP_LWL:
    case R4300_OP_LWR:
    case R4300_OP_LWU:
    case R4300_OP_ORI:
    case R4300_OP_SC:
    case R4300_OP_SLTI:
    case R4300_OP_SLTIU:
    case R4300_OP_XORI:
        inst->f.i.rs = IDEC_U53(r4300, iw, idec->u53[2], &dummy);
        inst->f.i.rt = IDEC_U53(r4300, iw, idec->u53[1], &dummy);
        inst->f.i.immediate  = (int16_t)iw;

        /* optimization: nopify instruction when r0 is the destination register (rt) */
        if (dummy == 0) { opcode = R4300_OP_NOP; }
        break;

    case R4300_OP_LDC1:
    case R4300_OP_LWC1:
    case R4300_OP_SDC1:
    case R4300_OP_SWC1:
        idec_u53(iw, idec->u53[2], &inst->f.lf.base);
        idec_u53(iw, idec->u53[1], &inst->f.lf.ft);
        inst->f.lf.offset  = (uint16_t)iw;
        break;

    case R4300_OP_CFC0:
    case R4300_OP_CFC1:
    case R4300_OP_CFC2:
    case R4300_OP_DMFC0:
    case R4300_OP_DMFC1:
    case R4300_OP_DMFC2:
    case R4300_OP_MFC0:
    case R4300_OP_MFC1:
    case R4300_OP_MFC2:
        inst->f.r.rs = IDEC_U53(r4300, iw, idec->u53[2], &dummy);
        inst->f.r.rt = IDEC_U53(r4300, iw, idec->u53[1], &dummy);
        inst->f.r.rd = IDEC_U53(r4300, iw, idec->u53[0], &inst->f.r.nrd);
        idec_u53(iw, idec->u53[3], &inst->f.r.sa);

        /* optimization: nopify instruction when r0 is the destination register (rt) */
        if (dummy == 0) { opcode = R4300_OP_NOP; }
        break;

#define CP1_S_D(op) \
    case R4300_OP_CP1_##op: \
        idec_u53(iw, idec->u53[3], &dummy); \
        idec_u53(iw, idec->u53[2], &inst->f.cf.fs); \
        idec_u53(iw, idec->u53[1], &inst->f.cf.ft); \
        idec_u53(iw, idec->u53[0], &inst->f.cf.fd); \
        switch(dummy) \
        { \
        case 0x10: inst->ops = cached_interp_##op##_S; return idec->opcode; \
        case 0x11: inst->ops = cached_interp_##op##_D; return idec->opcode; \
        default: opcode = R4300_OP_RESERVED; \
        } \
        break;

    CP1_S_D(ABS)
    CP1_S_D(ADD)
    CP1_S_D(CEIL_L)
    CP1_S_D(CEIL_W)
    CP1_S_D(C_EQ)
    CP1_S_D(C_F)
    CP1_S_D(C_LE)
    CP1_S_D(C_LT)
    CP1_S_D(C_NGE)
    CP1_S_D(C_NGL)
    CP1_S_D(C_NGLE)
    CP1_S_D(C_NGT)
    CP1_S_D(C_OLE)
    CP1_S_D(C_OLT)
    CP1_S_D(C_SEQ)
    CP1_S_D(C_SF)
    CP1_S_D(C_UEQ)
    CP1_S_D(C_ULE)
    CP1_S_D(C_ULT)
    CP1_S_D(C_UN)
    CP1_S_D(CVT_L)
    CP1_S_D(CVT_W)
    CP1_S_D(DIV)
    CP1_S_D(FLOOR_L)
    CP1_S_D(FLOOR_W)
    CP1_S_D(MOV)
    CP1_S_D(MUL)
    CP1_S_D(NEG)
    CP1_S_D(ROUND_L)
    CP1_S_D(ROUND_W)
    CP1_S_D(SQRT)
    CP1_S_D(SUB)
    CP1_S_D(TRUNC_L)
    CP1_S_D(TRUNC_W)
#undef CP1_S_D

    case R4300_OP_CP1_CVT_D:
        idec_u53(iw, idec->u53[3], &dummy);
        idec_u53(iw, idec->u53[2], &inst->f.cf.fs);
        idec_u53(iw, idec->u53[1], &inst->f.cf.ft);
        idec_u53(iw, idec->u53[0], &inst->f.cf.fd);
        switch(dummy)
        {
        case 0x10: inst->ops = cached_interp_CVT_D_S; return idec->opcode;
        case 0x14: inst->ops = cached_interp_CVT_D_W; return idec->opcode;
        case 0x15: inst->ops = cached_interp_CVT_D_L; return idec->opcode;
        default: opcode = R4300_OP_RESERVED;
        }
        break;

    case R4300_OP_CP1_CVT_S:
        idec_u53(iw, idec->u53[3], &dummy);
        idec_u53(iw, idec->u53[2], &inst->f.cf.fs);
        idec_u53(iw, idec->u53[1], &inst->f.cf.ft);
        idec_u53(iw, idec->u53[0], &inst->f.cf.fd);
        switch(dummy)
        {
        case 0x11: inst->ops = cached_interp_CVT_S_D; return idec->opcode;
        case 0x14: inst->ops = cached_interp_CVT_S_W; return idec->opcode;
        case 0x15: inst->ops = cached_interp_CVT_S_L; return idec->opcode;
        default: opcode = R4300_OP_RESERVED;
        }
        break;

    case R4300_OP_CTC0:
    case R4300_OP_CTC1:
    case R4300_OP_CTC2:
    case R4300_OP_DDIV:
    case R4300_OP_DDIVU:
    case R4300_OP_DIV:
    case R4300_OP_DIVU:
    case R4300_OP_DMTC0:
    case R4300_OP_DMTC1:
    case R4300_OP_DMTC2:
    case R4300_OP_DMULT:
    case R4300_OP_DMULTU:
    case R4300_OP_MTC0:
    case R4300_OP_MTC1:
    case R4300_OP_MTC2:
    case R4300_OP_MTHI:
    case R4300_OP_MTLO:
    case R4300_OP_MULT:
    case R4300_OP_MULTU:
    case R4300_OP_NOP:
    case R4300_OP_TEQ:
    case R4300_OP_TGE:
    case R4300_OP_TGEU:
    case R4300_OP_TLT:
    case R4300_OP_TLTU:
    case R4300_OP_TNE:
        inst->f.r.rs = IDEC_U53(r4300, iw, idec->u53[2], &dummy);
        inst->f.r.rt = IDEC_U53(r4300, iw, idec->u53[1], &dummy);
        inst->f.r.rd = IDEC_U53(r4300, iw, idec->u53[0], &inst->f.r.nrd);
        idec_u53(iw, idec->u53[3], &inst->f.r.sa);
        break;

    case R4300_OP_LDC2:
    case R4300_OP_LWC2:
    case R4300_OP_SB:
    case R4300_OP_SCD:
    case R4300_OP_SD:
    case R4300_OP_SDC2:
    case R4300_OP_SDL:
    case R4300_OP_SDR:
    case R4300_OP_SH:
    case R4300_OP_SW:
    case R4300_OP_SWC2:
    case R4300_OP_SWL:
    case R4300_OP_SWR:
    case R4300_OP_TEQI:
    case R4300_OP_TGEI:
    case R4300_OP_TGEIU:
    case R4300_OP_TLTI:
    case R4300_OP_TLTIU:
    case R4300_OP_TNEI:
        inst->f.i.rs = IDEC_U53(r4300, iw, idec->u53[2], &dummy);
        inst->f.i.rt = IDEC_U53(r4300, iw, idec->u53[1], &dummy);
        inst->f.i.immediate  = (int16_t)iw;
        break;

    case R4300_OP_BREAK:
    case R4300_OP_CACHE:
    case R4300_OP_ERET:
    case R4300_OP_SYNC:
    case R4300_OP_SYSCALL:
    case R4300_OP_TLBP:
    case R4300_OP_TLBR:
    case R4300_OP_TLBWI:
    case R4300_OP_TLBWR:
    case R4300_OP_RESERVED:
        /* no need for additonal instruction parsing */
        break;

    default:
        DebugMessage(M64MSG_ERROR, "invalid instruction: %08x", iw);
        assert(0);
        break;
    }

    /* set appropriate handler */
    inst->ops = ci_table[opcode];

    /* propagate opcode info to allow further processing */
    return opcode;
}


static uint32_t update_invalid_addr(struct r4300_core* r4300, uint32_t addr)
{
    char* const invalid_code = r4300->cached_interp.invalid_code;

    if ((addr & UINT32_C(0xc0000000)) == UINT32_C(0x80000000))
    {
        if (invalid_code[addr>>12]) {
            invalid_code[(addr^0x20000000)>>12] = 1;
        }
        if (invalid_code[(addr^0x20000000)>>12]) {
            invalid_code[addr>>12] = 1;
        }
        return addr;
    }
    else
    {
        uint32_t paddr = virtual_to_physical_address(r4300, addr, 2);
        if (paddr)
        {
            uint32_t beg_paddr = paddr - (addr - (addr & ~0xfff));

            update_invalid_addr(r4300, paddr);

            if (invalid_code[(beg_paddr+0x000)>>12]) {
                invalid_code[addr>>12] = 1;
            }
            if (invalid_code[(beg_paddr+0xffc)>>12]) {
                invalid_code[addr>>12] = 1;
            }
            if (invalid_code[addr>>12]) {
                invalid_code[(beg_paddr+0x000)>>12] = 1;
            }
            if (invalid_code[addr>>12]) {
                invalid_code[(beg_paddr+0xffc)>>12] = 1;
            }
        }
        return paddr;
    }
}

int get_block_length(const struct precomp_block *block)
{
    return (block->end-block->start)/4;
}

size_t get_block_memsize(const struct precomp_block *block)
{
    int length = get_block_length(block);
    return ((length+1)+(length>>2)) * sizeof(struct precomp_instr);
}

void cached_interp_init_block(struct r4300_core* r4300, uint32_t address)
{
    int i, length;

    struct precomp_block** block = &r4300->cached_interp.blocks[address >> 12];

    /* allocate block */
    if (*block == NULL) {
        *block = malloc(sizeof(struct precomp_block));
        (*block)->block = NULL;
        (*block)->start = address & ~UINT32_C(0xfff);
        (*block)->end = (address & ~UINT32_C(0xfff)) + 0x1000;
    }

    struct precomp_block* b = *block;

    length = get_block_length(b);

#ifdef DBG
    DebugMessage(M64MSG_INFO, "init block %" PRIX32 " - %" PRIX32, b->start, b->end);
#endif

    /* allocate block instructions */
    if (!b->block)
    {
        size_t memsize = get_block_memsize(b);
        b->block = (struct precomp_instr*)malloc(memsize);
        if (!b->block) {
            DebugMessage(M64MSG_ERROR, "Memory error: couldn't allocate memory for cached interpreter.");
            return;
        }

        memset(b->block, 0, memsize);
    }

    /* reset block instructions (addr + ops) */
    for (i = 0; i < length; ++i)
    {
        b->block[i].addr = b->start + 4*i;
        b->block[i].ops = cached_interp_NOTCOMPILED;
    }

    /* here we're marking the block as a valid code even if it's not compiled
     * yet as the game should have already set up the code correctly.
     */
    r4300->cached_interp.invalid_code[b->start>>12] = 0;


    if (b->end < UINT32_C(0x80000000) || b->start >= UINT32_C(0xc0000000))
    {
        uint32_t paddr = virtual_to_physical_address(r4300, b->start, 2);

        r4300->cached_interp.invalid_code[paddr>>12] = 0;
        cached_interp_init_block(r4300, paddr);

        paddr += b->end - b->start - 4;

        r4300->cached_interp.invalid_code[paddr>>12] = 0;
        cached_interp_init_block(r4300, paddr);
    }
    else
    {
        uint32_t alt_addr = b->start ^ UINT32_C(0x20000000);

        if (r4300->cached_interp.invalid_code[alt_addr>>12])
        {
            cached_interp_init_block(r4300, alt_addr);
        }
    }
}

void cached_interp_free_block(struct precomp_block* block)
{
    if (block->block) {
        free(block->block);
        block->block = NULL;
    }
}

void cached_interp_recompile_block(struct r4300_core* r4300, const uint32_t* iw, struct precomp_block* block, uint32_t func)
{
    int i, length, length2, finished;
    struct precomp_instr* inst;
    enum r4300_opcode opcode;

    /* ??? not sure why we need these 2 different tests */
    int block_start_in_tlb = ((block->start & UINT32_C(0xc0000000)) != UINT32_C(0x80000000));
    int block_not_in_tlb = (block->start >= UINT32_C(0xc0000000) || block->end < UINT32_C(0x80000000));

    length = get_block_length(block);
    length2 = length - 2 + (length >> 2);

    /* reset xxhash */
    block->xxhash = 0;


    for (i = (func & 0xFFF) / 4, finished = 0; finished != 2; ++i)
    {
        inst = block->block + i;

        /* set decoded instruction address */
        inst->addr = block->start + i * 4;

        if (block_start_in_tlb)
        {
            uint32_t address2 = virtual_to_physical_address(r4300, inst->addr, 0);
            if (r4300->cached_interp.blocks[address2>>12]->block[(address2&UINT32_C(0xFFF))/4].ops == cached_interp_NOTCOMPILED) {
                r4300->cached_interp.blocks[address2>>12]->block[(address2&UINT32_C(0xFFF))/4].ops = cached_interp_NOTCOMPILED2;
            }
        }

        /* decode instruction */
        opcode = r4300_decode(inst, r4300, r4300_get_idec(iw[i]), iw[i], iw[i+1], block);

        /* decode ending conditions */
        if (i >= length2) { finished = 2; }
        if (i >= (length-1)
        && (block->start == UINT32_C(0xa4000000) || block_not_in_tlb)) { finished = 2; }
        if (opcode == R4300_OP_ERET || finished == 1) { finished = 2; }
        if (/*i >= length && */
                (opcode == R4300_OP_J ||
                 opcode == R4300_OP_J_OUT ||
                 opcode == R4300_OP_JR ||
                 opcode == R4300_OP_JR_OUT) &&
                !(i >= (length-1) && block_not_in_tlb)) {
            finished = 1;
        }
    }

    if (i >= length)
    {
        inst = block->block + i;
        inst->addr = block->start + i*4;
        inst->ops = cached_interp_FIN_BLOCK;
        ++i;
        if (i <= length2) // useful when last opcode is a jump
        {
            inst = block->block + i;
            inst->addr = block->start + i*4;
            inst->ops = cached_interp_FIN_BLOCK;
            i++;
        }
    }

#ifdef DBG
    DebugMessage(M64MSG_INFO, "block recompiled (%" PRIX32 "-%" PRIX32 ")", func, block->start+i*4);
#endif
}

void cached_interpreter_jump_to(struct r4300_core* r4300, uint32_t address)
{
    struct cached_interp* const cinterp = &r4300->cached_interp;

    if (r4300->skip_jump) {
        return;
    }

    if (!update_invalid_addr(r4300, address)) {
        return;
    }

    /* setup new block if invalid */
    if (cinterp->invalid_code[address >> 12]) {
        r4300->cached_interp.init_block(r4300, address);
    }

    /* set new PC */
    cinterp->actual = cinterp->blocks[address >> 12];
    (*r4300_pc_struct(r4300)) = cinterp->actual->block + ((address - cinterp->actual->start) >> 2);
}


void init_blocks(struct cached_interp* cinterp)
{
    size_t i;
    for (i = 0; i < 0x100000; ++i)
    {
        cinterp->invalid_code[i] = 1;
        cinterp->blocks[i] = NULL;
    }
}

void free_blocks(struct cached_interp* cinterp)
{
    size_t i;
    for (i = 0; i < 0x100000; ++i)
    {
        if (cinterp->blocks[i])
        {
            cinterp->free_block(cinterp->blocks[i]);
            free(cinterp->blocks[i]);
            cinterp->blocks[i] = NULL;
        }
    }
}

void invalidate_cached_code_hacktarux(struct r4300_core* r4300, uint32_t address, size_t size)
{
    size_t i;
    uint32_t addr;
    uint32_t addr_max;

    if (size == 0)
    {
        /* invalidate everthing */
        memset(r4300->cached_interp.invalid_code, 1, 0x100000);
    }
    else
    {
        /* invalidate blocks (if necessary) */
        addr_max = address+size;

        for(addr = address; addr < addr_max; addr += 4)
        {
            i = (addr >> 12);

            if (r4300->cached_interp.invalid_code[i] == 0)
            {
                if (r4300->cached_interp.blocks[i] == NULL
                 || r4300->cached_interp.blocks[i]->block[(addr & 0xfff) / 4].ops != r4300->cached_interp.not_compiled)
                {
                    r4300->cached_interp.invalid_code[i] = 1;
                    /* go directly to next i */
                    addr &= ~0xfff;
                    addr |= 0xffc;
                }
            }
            else
            {
                /* go directly to next i */
                addr &= ~0xfff;
                addr |= 0xffc;
            }
        }
    }
}

// -----------------------------------------------------------
// DSTALL watchdog: stall / on-demand force dump of the full guest RDRAM
// plus device state.  Ported from the pre-reset branch (lost in the
// dc955483a "fresh start" reset).  The recompiler (emumode=2) is the active
// CPU path, so the on-demand force dump + spin detector live in
// dynarec_sample_hook (called from do_interrupt in linkage_arm64.S); a
// realtime stall thread watches the heartbeat for a hard stall.
//
// The dump writes the full 8MB RDRAM (identity guest->file offset) followed
// by a "WD pc=" header + CPU/CP0/SP/VI/MI state + DD trace; that layout is
// exactly what .fzxwork/freeze_state.py decodes for guest OS thread/queue
// state.
//
#define WD_STALL_SECS 4
#define WD_FILES_DIR "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/"
#define WD_FORCE_FLAG WD_FILES_DIR "wd_force.flag"

static volatile uint64_t wd_hb = 0;
static volatile int wd_dumped = 0;
static struct r4300_core* wd_r4300 = NULL;

/* Progress counters (2026-09-10 round 6).  The round-5 stall analysis could not
   tell "the emulation thread is wedged inside the RSP" apart from "the CPU runs
   but nothing is delivered", because every existing signal was a file trace that
   either saturated its cap or was written from only one side of the machine.
   These counters are cheap (monotonic increments on the emulation thread) and
   are sampled twice 300ms apart by the stall dump, so the dump itself answers
   "what is still moving".  Written only on the 64DD route so plain carts pay at
   most one predictable branch. */
volatile uint32_t wd_c_do_sp_task = 0;   /* core: do_SP_Task entries            */
volatile uint32_t wd_c_sp_int_evt = 0;   /* core: rsp_interrupt_event fires     */
volatile uint32_t wd_c_gen_int    = 0;   /* core: gen_interrupt entries         */
volatile uint32_t wd_c_sample     = 0;   /* dynarec: do_interrupt (sample hook) */
volatile uint32_t wd_c_dd_asic    = 0;   /* DD: ASIC commands issued by the guest */
volatile uint32_t wd_c_pi_dma     = 0;   /* PI: completed cart DMAs                */

/* Round 8: interrupt-source attribution.  The stall ring shows the guest
   cycling through 0x80000180 (exception vector) + libultra/os/exceptasm +
   __osDispatchThread roughly once every 18 dynarec blocks, i.e. the emulated
   CPU is spending a large slice of its time inside exceptions -- yet
   gen_interrupt only advances ~40/s and the dynarec's do_interrupt sample
   hook (wd_c_sample) is FROZEN.  Both facts together mean the exceptions are
   NOT coming from the emulator's event queue: they come from
   raise_maskable_interrupt(), which calls exception_general() synchronously
   from whatever context raised the RCP interrupt.  These counters name the
   source: how many exceptions are taken in total, how many of them are
   interrupt-class, and how many RCP raises each MI_INTR bit contributed.
   DD-gated like the rest (one predictable branch per event on plain carts). */
volatile uint32_t wd_c_exc_total  = 0;   /* exception_general() entries             */
volatile uint32_t wd_c_exc_int    = 0;   /* ... of which ExcCode == 0 (interrupt)   */
volatile uint32_t wd_c_exc_nested = 0;   /* ... taken while EXL/ERL already set     */
volatile uint32_t wd_c_raise      = 0;   /* raise_rcp_interrupt() calls             */
volatile uint32_t wd_c_signal     = 0;   /* signal_rcp_interrupt() calls            */
volatile uint32_t wd_c_raise_bits[8] = {0}; /* per MI_INTR bit: raise+signal count  */
volatile uint32_t wd_c_cmp_int    = 0;   /* compare_int_handler() calls (CP0 IP7)   */
volatile uint32_t wd_c_vi_evt     = 0;   /* vi_vertical_interrupt_event() calls     */
volatile uint32_t wd_c_vi_ack     = 0;   /* guest writes to VI_CURRENT (VI ack)     */
volatile uint32_t wd_c_vi_last    = 0;   /* MI_INTR bits at the last VI raise       */
volatile uint32_t wd_c_ai_evt     = 0;   /* ai_controller interrupt events          */
volatile uint32_t wd_c_rsp_run    = 0;   /* RSP DoRspCycles() budget runs           */
volatile uint32_t wd_c_rsp_full   = 0;   /* ... of which exhausted the budget       */

/* ===========================================================================
   ROUND 57 (DD route only): WHO IS EATING THE CORE THREAD, AND IS THE CART
   STREAM MAKING PROGRESS?

   Round 56 left two open questions that no existing counter could answer:

   (a) The DD-trace ring tail is a pure stream of pi_end_of_dma_event entries
       whose PI_CART_ADDR walks a ~462 KB window of the *cartridge*
       (0x109880b0..0x109f90b0) with a ~960-byte stride, then restarts at the
       bottom.  The trace records only PI_CART_ADDR and PI_STATUS, so it cannot
       distinguish "streaming one big region into RDRAM" (progress) from
       "re-reading the same region into the same buffer forever" (a loop).
       The ledger below records the RDRAM destination, the direction, the
       length and the guest PC of every PI DMA, plus how long the handler
       itself took -- which is where round 56 suspected the ~71 ms between
       cart reads was going.

   (b) The core thread's host CPU time is unmeasured.  apply_speed_limiter()
       runs ON the emulation thread (new_vi -> gen_interrupt case VI_INT) and
       sleeps to hold 60 VI/s of wall clock, so "the guest is slow" and "the
       thread is asleep" are the same statement unless the split is measured.
       wd_core_tid is latched in wd_attach() (which runs on the core thread)
       and the watchdog thread polls /proc/self/task/<tid>/stat for it, so the
       periodic probe reports the core thread's utime/stime delta and kernel
       state across the same 300 ms window as every other DELTA. */
#define WD_PIR_RING 1024

volatile uint32_t wd_pir_ring[WD_PIR_RING][6]; /* dir,cart,dram,len,pc,us */
volatile uint32_t wd_pir_n      = 0;
volatile uint32_t wd_pir_rd_n   = 0;     /* cart/DD -> RDRAM DMAs issued     */
volatile uint32_t wd_pir_wr_n   = 0;     /* RDRAM -> cart/DD DMAs issued     */
volatile uint32_t wd_pir_rd_us  = 0;     /* total us inside handler->dma_read  */
volatile uint32_t wd_pir_wr_us  = 0;
volatile uint32_t wd_pir_rd_max = 0;     /* worst single transfer, us        */
volatile uint32_t wd_pir_wr_max = 0;
volatile uint32_t wd_c_lim_calls = 0;    /* apply_speed_limiter() calls       */
volatile uint32_t wd_c_lim_us    = 0;    /* total us slept inside it          */
volatile uint32_t wd_c_lim_max   = 0;    /* worst single sleep, us            */
volatile uint32_t wd_c_vi_delay  = 0;    /* g_dev.vi.device as the core sees it */

/* Round 57: the DD-route background RSP pump.  rsp_dd_background_pump() is
   called from dynarec_gen_interrupt(), i.e. from the CPU thread's per-block
   interrupt check, and it runs do_SP_Task() from there every ~3 ms of wall
   time when a task is locked.  If the RSP ucode is spinning (waiting for the
   RDP) each of those slices burns host time on the CPU thread, which would
   explain the only really anomalous measurement of this round: the core thread
   at ~80% of a core while the guest clock advances at 4.6 MHz and every other
   counter (PI DMAs, gen_interrupt, block dispatches) is tiny. */
volatile uint32_t wd_c_pump_n    = 0;    /* pump INVOCATIONS (= block boundaries) */
volatile uint32_t wd_c_pump_call = 0;    /* ... of which ran do_SP_Task           */
volatile uint32_t wd_c_pump_us   = 0;    /* total us inside those do_SP_Task calls*/
volatile uint32_t wd_c_pump_max  = 0;

const char* wd_r57_marker = "R57PILEDGER";

void wd_pir_add(uint32_t kind, uint32_t cart, uint32_t dram, uint32_t len,
                uint32_t pc, uint32_t us)
{
    uint32_t i;
    if (g_dev.dd.idisk == NULL) return;
    i = wd_pir_n++ & (WD_PIR_RING - 1);
    wd_pir_ring[i][0] = kind;
    wd_pir_ring[i][1] = cart;
    wd_pir_ring[i][2] = dram;
    wd_pir_ring[i][3] = len;
    wd_pir_ring[i][4] = pc;
    wd_pir_ring[i][5] = us;
}

static void wd_pir_dump(FILE* f)
{
    uint32_t i, n, start;
    if (f == NULL) return;
    n = (wd_pir_n < WD_PIR_RING) ? wd_pir_n : WD_PIR_RING;
    start = (wd_pir_n < WD_PIR_RING) ? 0 : (wd_pir_n - WD_PIR_RING);
    fprintf(f, "=== PI LEDGER (%u of %u, ring=%u) ===\n",
            n, (unsigned)wd_pir_n, (unsigned)WD_PIR_RING);
    fprintf(f, "PISUM rd_n=%u rd_us=%u rd_max=%u wr_n=%u wr_us=%u wr_max=%u "
               "lim_calls=%u lim_us=%u lim_max=%u vi_delay=%u\n",
            (unsigned)wd_pir_rd_n, (unsigned)wd_pir_rd_us, (unsigned)wd_pir_rd_max,
            (unsigned)wd_pir_wr_n, (unsigned)wd_pir_wr_us, (unsigned)wd_pir_wr_max,
            (unsigned)wd_c_lim_calls, (unsigned)wd_c_lim_us, (unsigned)wd_c_lim_max,
            (unsigned)wd_c_vi_delay);
    for (i = 0; i < n; i++) {
        const volatile uint32_t* e = wd_pir_ring[(start + i) & (WD_PIR_RING - 1)];
        fprintf(f, "  %u %08x %08x %08x %08x %u\n",
                (unsigned)e[0], (unsigned)e[1], (unsigned)e[2],
                (unsigned)e[3], (unsigned)e[4], (unsigned)e[5]);
    }
}

/* Round 57: the core thread's own tid + kernel-visible CPU time. */
static pid_t wd_core_tid = 0;

static int wd_read_core_stat(uint64_t* utime, uint64_t* stime, char* state)
{
    char path[64], buf[1024];
    FILE* g;
    char* p;
    int i;
    *utime = *stime = 0;
    *state = '?';
    if (wd_core_tid <= 0) return 0;
    snprintf(path, sizeof(path), "/proc/self/task/%d/stat", (int)wd_core_tid);
    g = fopen(path, "r");
    if (g == NULL) return 0;
    if (fgets(buf, sizeof(buf), g) == NULL) { fclose(g); return 0; }
    fclose(g);
    p = strrchr(buf, ')');
    if (p == NULL) return 0;
    p++;
    /* ROUND 49 FIX: this used to advance three characters and then read p[0]
       as the state, which lands on the FIRST DIGIT OF PPID -- every sample
       printed `state=1` (a digit, never R/S/D), and the extra skip left the
       field walk one token out of step, so utime/stime were read from the
       wrong columns.  Field 3 is a state CHARACTER: skip the space, take it,
       step over the following space. */
    while (*p == ' ') p++;
    *state = *p;
    p++;
    /* skip ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt */
    for (i = 0; i < 10; i++) {
        while (*p == ' ') p++;
        while (*p && *p != ' ') p++;
    }
    *utime = strtoull(p, &p, 10);
    *stime = strtoull(p, &p, 10);
    return 1;
}

/* ===========================================================================
   ROUND 35 (DD route only): WHO DESTROYS THE GUEST'S EXCEPTION VECTOR?

   MEASURED this round, offline, on the archived full-RAM dumps: RDRAM
   0x00000180 -- the R4300 general exception vector -- holds the standard
   libultra prologue (3C1A800C 275AC4C0 = `lui k0,0x800C / addiu k0,k0,-0x3b40`;
   the 0x8074 variant 3C1A8074 275A6800 also occurs) in EVERY full-RAM dump
   from round 3 through round 32, and holds RSP *microcode* (four COP2 vector
   ops: 4B8641B3 E9DA0F06 4B914473 E9C40F05 ...) in the round-33 dump -- the
   only dump in the series that differs.

   The round-33 stall snapshot is exactly what that predicts: CP0 STATUS =
   0x0000FF03 (IE|EXL both set), EPC frozen at 0x8074651C, the dynarec PC ring
   2048/2048 entries of 0x80000180, the do_interrupt sample hook (wd_c_sample)
   FROZEN while gen_interrupt and raise_rcp_interrupt each advance +49 per
   300 ms, and wd_c_ht advancing 15M blocks/s -- i.e. the CPU takes an
   interrupt, jumps to 0x80000180, executes microcode that is not MIPS, takes
   a second exception with EXL already set, and re-enters the vector forever.
   No interrupt is ever delivered again, so the guest -- whose screen had
   already been drawn -- stops submitting gfx tasks.  That is the frozen
   "64DD" screen round 34 proved is the guest's own output.

   So this window is a canary.  (a) A shadow of RDRAM 0x0000..0x07FF is taken
   while the vector is still valid; (b) the watchdog thread -- which always
   gets scheduled, unlike the dynarec sample hook -- polls it and dumps the
   whole machine the first time it diverges; (c) every SP and PI DMA is
   recorded in a small ring with source/destination/length and guest PC, so
   the dump names the writer even though the RDRAM store itself may be emitted
   inline by the dynarec and cannot be hooked. */
#define WD_LOW_WINDOW 0x800u
#define WD_LX_RING    64

/* Packaged-binary marker (checked with `strings` on the apk's libmupen64plus-core.so). */
const char* wd_r35_marker = "R35LOWCANARY";

volatile uint32_t wd_lx_ring[WD_LX_RING][6];
volatile uint32_t wd_lx_n = 0;
volatile uint32_t wd_low_n = 0;
uint32_t wd_low_off = 0, wd_low_was = 0, wd_low_now = 0;
static uint8_t wd_low_shadow[WD_LOW_WINDOW];
static int wd_low_armed = 0;
static int wd_low_seen = 0;
static int wd_low_dumped = 0;

void wd_lx_add(uint32_t kind, uint32_t a, uint32_t b, uint32_t len, uint32_t pc)
{
    uint32_t i;
    if (g_dev.dd.idisk == NULL) return;
    i = wd_lx_n++ & (WD_LX_RING - 1);
    wd_lx_ring[i][0] = kind;
    wd_lx_ring[i][1] = a;
    wd_lx_ring[i][2] = b;
    wd_lx_ring[i][3] = len;
    wd_lx_ring[i][4] = pc;
    wd_lx_ring[i][5] = wd_lx_n;
}

/* Does the RDRAM byte range [addr, addr+len) intersect the low window?
   Tested on the RAW register value as well as the masked one: a wrapped or
   KSEG0-tagged address (0x80000000 masks to 0) is exactly the kind of value
   that lands at physical 0. */
int wd_low_range(uint32_t addr, uint32_t len)
{
    uint32_t a;
    if (len == 0) return 0;
    if (len > 0x800000u) len = 0x800000u;
    if (addr < WD_LOW_WINDOW) return 1;              /* starts inside        */
    a = addr & 0x7fffffu;
    if (a < WD_LOW_WINDOW) return 1;                 /* masks into the window*/
    if (a + len > 0x800000u) return 1;               /* wraps past the end   */
    if (addr < 0x800000u && addr + len > 0x800000u) return 1;
    return 0;
}

void wd_low_note(const char* who, uint32_t a, uint32_t b, uint32_t len, uint32_t pc)
{
    static unsigned n = 0;
    FILE* f;
    if (g_dev.dd.idisk == NULL) return;
    wd_lx_add(0x100u, a, b, len, pc);
    if (n >= 64) { wd_low_n++; return; }
    n++;
    wd_low_n++;
    f = fopen(WD_FILES_DIR "wd_low.txt", (n == 1) ? "w" : "a");
    if (f) {
        fprintf(f, "WDLOW n=%u who=%s a=%08x b=%08x len=%08x pc=%08x\n",
                n, who, a, b, len, pc);
        fclose(f);
    }
}

/* Round 9: dynarec dispatch attribution.  wd_pc_ring is written by BOTH
   get_addr_ht() and dynarec_sample_hook(), so 2048 identical entries cannot
   distinguish "the guest is executing the exception vector at 0x80000180"
   from "the dispatcher keeps re-entering the vector block and it never runs".
   wd_ht_ring is written by get_addr_ht() ONLY, so it is a true trace of every
   block the dynarec resolves; wd_c_cop1 counts the dynarec's COP1-unusable
   faults.  If the COP1 fault rate is tens of millions/s while the dispatch
   ring never leaves 0x80000180, then the code the hash table hands back for
   the vector is NOT the vector's translation -- which is exactly the state
   found at the end of round 8 (epc frozen at 0x80000408, cause=EXCCODE_CPU|CE1,
   the whole machine spinning through exception_general()).  DD-gated. */
#define WD_HT_RING 64
volatile uint32_t wd_ht_ring[WD_HT_RING];
volatile uint32_t wd_ht_idx = 0;
volatile uint32_t wd_c_ht = 0;       /* get_addr_ht() calls                     */
volatile uint32_t wd_c_cop1 = 0;     /* dynarec cop1_unusable() entries         */
volatile uint32_t wd_cop1_idx = 0;
volatile uint32_t wd_cop1_ring[16];  /* last 16 faulting pcs                    */
#define WD_COP1_SNAP 4
volatile uint32_t wd_cop1_snap_n = 0;
volatile uint32_t wd_cop1_regs[WD_COP1_SNAP][32];
/* pcaddr, SR, CAUSE, EPC, BadVaddr, delay_slot, ht_ret_lo, ht_ret_hi */
volatile uint32_t wd_cop1_meta[WD_COP1_SNAP][8];
/* Hash-table contents for the exception vector (row 0) and for the 0x80000400
   fill block (row 1): {vaddr, start, length, addr, clean_addr/...}. */
volatile uint32_t wd_vec_probe[2][10];

/* Host-PC sampler (2026-09-10 round 6).  Every other signal says the emulation
   thread is burning 100% of a core with ZERO progress in gen_interrupt,
   do_SP_Task, rsp_interrupt_event and the dynarec's do_interrupt -- so it is
   spinning inside generated code, and only a host backtrace can say which
   generator produced it.  ITIMER_PROF delivers SIGPROF to whichever thread is
   consuming CPU, so the handler samples the spinning thread directly; the raw
   host pc/lr are classified offline against /proc/<pid>/maps.  Armed by the
   stall thread only after dd.idisk is set (DD route only). */
#define WD_PCS_N 128
static volatile uint64_t wd_pcs[WD_PCS_N][4]; /* host pc, host lr, host fp, RSP SP_PC */
static volatile uint32_t wd_pcs_idx = 0;
static volatile int wd_pcs_armed = 0;
static struct sigaction wd_sigprof_old;
static int wd_sigprof_saved = 0;

static void wd_sigprof(int sig, siginfo_t* si, void* uc_)
{
    ucontext_t* uc = (ucontext_t*)uc_;
    uint32_t i = wd_pcs_idx++ & (WD_PCS_N - 1);
    uintptr_t pc = 0, lr = 0, fp = 0;
    (void)sig; (void)si;
#if defined(__aarch64__)
    pc = (uintptr_t)uc->uc_mcontext.pc;
    lr = (uintptr_t)uc->uc_mcontext.regs[30];
    fp = (uintptr_t)uc->uc_mcontext.regs[29];
#elif defined(__arm__)
    pc = (uintptr_t)uc->uc_mcontext.arm_pc;
    lr = (uintptr_t)uc->uc_mcontext.arm_lr;
    fp = (uintptr_t)uc->uc_mcontext.arm_fp;
#endif
    wd_pcs[i][0] = (uint64_t)pc;
    wd_pcs[i][1] = (uint64_t)lr;
    wd_pcs[i][2] = (uint64_t)fp;
    wd_pcs[i][3] = (uint64_t)g_dev.sp.regs2[SP_PC_REG];
    if (wd_sigprof_saved && (wd_sigprof_old.sa_flags & SA_SIGINFO)
        && wd_sigprof_old.sa_sigaction != NULL)
        wd_sigprof_old.sa_sigaction(sig, si, uc_);
}

static struct sigaction wd_crash_old[3];
static int wd_crash_saved = 0;
static void wd_crash_dump(int sig, siginfo_t* si, void* uc_);

static void wd_pcs_arm(void)
{
    struct sigaction sa;
    struct itimerval it;
    if (wd_pcs_armed) return;
    wd_pcs_armed = 1;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = wd_sigprof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPROF, &sa, &wd_sigprof_old) == 0) wd_sigprof_saved = 1;
    /* Round 9: the recompiler can now crash with a raw SIGILL inside the JIT
       arena, which produces no usable C backtrace. */
    {
        int sigs[3];
        int k;
        sigs[0] = SIGILL; sigs[1] = SIGSEGV; sigs[2] = SIGBUS;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = wd_crash_dump;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&sa.sa_mask);
        for (k = 0; k < 3; k++)
            if (sigaction(sigs[k], &sa, &wd_crash_old[k]) == 0) wd_crash_saved |= (1 << k);
    }
    it.it_interval.tv_sec = 0; it.it_interval.tv_usec = 10000; /* 10ms of CPU */
    it.it_value = it.it_interval;
    setitimer(ITIMER_PROF, &it, NULL);
}

/* Recent-PC ring buffer: sampled in dynarec_sample_hook (do_interrupt), dumped
   on the FIRST guest fault (TLB miss / address error).  The 64DD route only --
   the guest fault is what parks F-Zero X's main thread. */
#define WD_PC_RING 2048
static volatile uint32_t wd_pc_ring[WD_PC_RING];
static volatile uint32_t wd_reg_ring[WD_PC_RING][2];
static volatile uint32_t wd_pc_idx = 0;

static void wd_crash_dump(int sig, siginfo_t* si, void* uc_)
{
    ucontext_t* uc = (ucontext_t*)uc_;
    FILE* f;
    uintptr_t hpc = 0, hlr = 0, hsp = 0, hfp = 0;
    int k, idx;
    (void)si;
#if defined(__aarch64__)
    hpc = (uintptr_t)uc->uc_mcontext.pc;
    hlr = (uintptr_t)uc->uc_mcontext.regs[30];
    hsp = (uintptr_t)uc->uc_mcontext.regs[31];
    hfp = (uintptr_t)uc->uc_mcontext.regs[29];
#endif
    f = fopen(WD_FILES_DIR "wd_crash.txt", "w");
    if (f != NULL) {
        fprintf(f, "WDCRASH sig=%d code=%d fault=%p hpc=%p hlr=%p hsp=%p hfp=%p\n",
                sig, si ? si->si_code : -1, si ? si->si_addr : NULL,
                (void*)hpc, (void*)hlr, (void*)hsp, (void*)hfp);
        for (k = 0; k < 30; k++)
#if defined(__aarch64__)
            fprintf(f, "HOSTREG x%d=%016llx\n", k, (unsigned long long)uc->uc_mcontext.regs[k]);
#else
            fprintf(f, "HOSTREG x%d=?\n", k);
#endif
        if (wd_r4300 != NULL) {
            uint32_t* cp0 = r4300_cp0_regs(&wd_r4300->cp0);
            int64_t* g = r4300_regs(wd_r4300);
            fprintf(f, "GUEST pcaddr=%08x sr=%08x cause=%08x epc=%08x badvaddr=%08x count=%08x\n",
                    *r4300_pc(wd_r4300), cp0[CP0_STATUS_REG], cp0[CP0_CAUSE_REG],
                    cp0[CP0_EPC_REG], cp0[CP0_BADVADDR_REG], cp0[CP0_COUNT_REG]);
            for (k = 0; k < 32; k++) fprintf(f, "G%02d=%08x\n", k, (uint32_t)g[k]);
            fprintf(f, "SP sp_status=%08x sp_pc=%08x busy=%08x full=%08x mi_intr=%08x mi_mask=%08x\n",
                    g_dev.sp.regs[SP_STATUS_REG], g_dev.sp.regs2[SP_PC_REG],
                    g_dev.sp.regs[SP_DMA_BUSY_REG], g_dev.sp.regs[SP_DMA_FULL_REG],
                    g_dev.mi.regs[MI_INTR_REG], g_dev.mi.regs[MI_INTR_MASK_REG]);
            fprintf(f, "CNT c_ht=%u c_cop1=%u c_exc=%u c_exc_nested=%u c_vi_evt=%u c_task=%u c_sample=%u c_genint=%u\n",
                    wd_c_ht, wd_c_cop1, wd_c_exc_total, wd_c_exc_nested, wd_c_vi_evt,
                    wd_c_do_sp_task, wd_c_sample, wd_c_gen_int);
        }
        /* The last 64 blocks the dynarec resolved (oldest first) -- this is the
           trace that says whether the guest was executing real code or was
           already lost in the exception vector. */
        idx = wd_ht_idx;
        fprintf(f, "HTRING\n");
        for (k = 0; k < WD_HT_RING; k++)
            fprintf(f, "%08x\n", (uint32_t)wd_ht_ring[(idx + k) & (WD_HT_RING - 1)]);
        idx = wd_pc_idx;
        fprintf(f, "PCRING\n");
        for (k = 0; k < 256; k++) {
            uint32_t i2 = (idx + k) & (WD_PC_RING - 1);
            fprintf(f, "%08x %08x %08x\n", (uint32_t)wd_pc_ring[i2],
                    (uint32_t)wd_reg_ring[i2][0], (uint32_t)wd_reg_ring[i2][1]);
        }
        fprintf(f, "COP1RING\n");
        idx = wd_cop1_idx;
        for (k = 0; k < 16; k++) fprintf(f, "%08x\n", (uint32_t)wd_cop1_ring[(idx + k) & 15]);
        fprintf(f, "WDCRASH_END\n");
        fclose(f);
    }
    /* Put the original handler back and take the signal again so the tombstone
       and the framework crash report still happen. */
    for (k = 0; k < 3; k++) {
        int sigs[3];
        sigs[0] = SIGILL; sigs[1] = SIGSEGV; sigs[2] = SIGBUS;
        if (wd_crash_saved & (1 << k)) sigaction(sigs[k], &wd_crash_old[k], NULL);
    }
    raise(sig);
}

static volatile int wd_fault_dumped = 0;
static volatile int wd_spodd_dumped = 0;

/* DD-route only (2026-09-10 round 3): instruction-level ring of
   (pc, opcode, sp) sampled after every interpreted instruction.  `$sp` going
   odd (0x800d4200 ^ 3 -- mupen's S8 byte-lane constant) is the FIRST defect in
   the F-Zero X EK boot failure, and it reproduces IDENTICALLY under the cached
   interpreter and the recompiler, so it is shared code, not the dynarec.
   This records the exact instruction that introduces it. */
#define WD_IRING 96
static uint32_t wd_i_pc[WD_IRING];
static uint32_t wd_i_op[WD_IRING];
static int64_t  wd_i_sp[WD_IRING];
static uint32_t wd_i_idx = 0;
static volatile int wd_spodd2_dumped = 0;

static void wd_spodd2_dump(struct r4300_core* r4300, uint32_t pc, uint32_t op)
{
    FILE* f = fopen(WD_FILES_DIR "wd_spodd2.txt", "w");
    uint32_t i, n;
    int64_t* regs;
    if (f == NULL) return;
    regs = r4300_regs(r4300);
    fprintf(f, "WDSPODD2 pc=%08x op=%08x sp=%08x ra=%08x a0=%08x a1=%08x a2=%08x a3=%08x\n",
            pc, op, (uint32_t)regs[29], (uint32_t)regs[31], (uint32_t)regs[4],
            (uint32_t)regs[5], (uint32_t)regs[6], (uint32_t)regs[7]);
    n = wd_i_idx;
    fprintf(f, "IRING n=%u\n", n < WD_IRING ? n : WD_IRING);
    for (i = 0; i < WD_IRING; i++) {
        uint32_t idx = (n - WD_IRING + i) & (WD_IRING - 1);
        if (n < WD_IRING && i >= n) continue;
        fprintf(f, "%08x %08x %08x\n", wd_i_pc[idx], wd_i_op[idx], (uint32_t)wd_i_sp[idx]);
    }
    fclose(f);
    DebugMessage(M64MSG_WARNING, "WDSPODD2 pc=%08x op=%08x sp=%08x", pc, op, (uint32_t)regs[29]);
}

/* guest word read from the (word-swapped) RDRAM image */
static uint32_t wd_rdram32(uint32_t addr)
{
    return ((const volatile uint32_t*)g_mem_base)[(addr & 0x7FFFFF) >> 2];
}

/* Shared state dumper: `path` selects the trigger label (iplram_fault.bin for
   the first guest fault, iplram_spodd.bin for the first misaligned $sp).
   Tag string is written into the header so the two dumps stay distinguishable. */
static void wd_state_dump(const char* path, const char* tag, struct r4300_core* r4300,
                          uint32_t vaddr, int w)
{
    FILE* f;
    uint32_t* cp0_regs;
    int64_t* regs;
    uint32_t sp, pc, i, n;

    /* DD route only: plain carts must not pay for (or see) any of this. */
    if (g_dev.dd.idisk == NULL) return;
    if (r4300 == NULL || g_mem_base == NULL) return;

    f = fopen(path, "wb");
    if (f == NULL) return;

    regs = r4300_regs(r4300);
    cp0_regs = r4300_cp0_regs(&r4300->cp0);
    sp = (uint32_t)regs[29];
    pc = (uint32_t)*r4300_pc(r4300);

    fwrite(g_mem_base, 1, 0x800000, f);
    fprintf(f, "WD%s vaddr=%08x w=%d pc=%08x ra=%08x sp=%08x a0=%08x a1=%08x a2=%08x a3=%08x "
               "v0=%08x v1=%08x t0=%08x t1=%08x t2=%08x t3=%08x t9=%08x s0=%08x s1=%08x s2=%08x\n",
        tag, vaddr, w, pc, (uint32_t)regs[31], sp, (uint32_t)regs[4], (uint32_t)regs[5],
        (uint32_t)regs[6], (uint32_t)regs[7], (uint32_t)regs[2], (uint32_t)regs[3],
        (uint32_t)regs[8], (uint32_t)regs[9], (uint32_t)regs[10], (uint32_t)regs[11],
        (uint32_t)regs[25], (uint32_t)regs[16], (uint32_t)regs[17], (uint32_t)regs[18]);
    fprintf(f, "WDCP0 cause=%08x status=%08x epc=%08x badvaddr=%08x errorpc=%08x\n",
        cp0_regs[CP0_CAUSE_REG], cp0_regs[CP0_STATUS_REG], cp0_regs[CP0_EPC_REG],
        cp0_regs[CP0_BADVADDR_REG], cp0_regs[CP0_ERROREPC_REG]);
    fprintf(f, "WDRSP status=%08x pc=%08x mi_intr=%08x mi_mask=%08x\n",
        g_dev.sp.regs[SP_STATUS_REG], g_dev.sp.regs2[SP_PC_REG],
        g_dev.mi.regs[MI_INTR_REG], g_dev.mi.regs[MI_INTR_MASK_REG]);

    /* stack window around sp (walking down = older frames) */
    if (sp >= 0x80000000u && sp < 0x80800000u) {
        fprintf(f, "WDSTACK sp=%08x\n", sp);
        for (i = 0; i < 0x300; i += 4) {
            fprintf(f, "%08x%c", (uint32_t)wd_rdram32(sp + i - 0x100),
                    ((i % 16) == 12) ? '\n' : ' ');
        }
    }
    /* recent PCs, oldest first */
    n = wd_pc_idx;
    fprintf(f, "WDPCS n=%u\n", n < WD_PC_RING ? n : WD_PC_RING);
    for (i = 0; i < WD_PC_RING; i++) {
        uint32_t idx = (n - WD_PC_RING + i) & (WD_PC_RING - 1);
        if (n < WD_PC_RING && i >= n) continue;
        fprintf(f, "%08x %08x %08x\n", (uint32_t)wd_pc_ring[idx],
                (uint32_t)wd_reg_ring[idx][0], (uint32_t)wd_reg_ring[idx][1]);
    }
    fprintf(f, "\nWDEND\n");
    fclose(f);
    DebugMessage(M64MSG_WARNING, "WD%s vaddr=%08x w=%d pc=%08x sp=%08x -> %s",
        tag, vaddr, w, pc, sp, path);
}

/* ---------------------------------------------------------------------------
   Round-6 stall probe.  Samples the whole machine twice, 300ms apart, and
   writes one small text file: device state, the four progress counters, the
   dynarec's recent-block ring, the live RSP IMEM+DMEM, and the kernel state
   (S/R/D + utime/stime) of every thread in the process.  Together those answer
   the question the round-5 traces could not: is the emulation thread wedged
   inside the RSP, spinning in the dynarec, or idle waiting for a delivery that
   never comes?  DD route only (caller is DD-gated); fopen here runs on the
   already-stalled watchdog thread, never in a hot path. */
struct wd_snap {
    uint32_t cause, status, epc, badvaddr, count;
    uint32_t mi_intr, mi_mask;
    uint32_t sp_status, sp_pc, sp_busy, sp_full, sp_sem;
    uint32_t c_task, c_spint, c_genint, c_sample, c_asic, c_pi;
    uint32_t vi_current, vi_field, vi_delay;
    /* Round 8: exception/int-source attribution + the guest's own libultra VI
       state.  See the counter block at the top of this file. */
    uint32_t c_exc, c_exc_int, c_exc_nested, c_raise, c_signal, c_cmp_int;
    uint32_t c_vi_evt, c_vi_ack;
    uint32_t raise_bits[8];
    uint32_t g_vi_curr_framep, g_vi_next_framep, g_vi_curr_state, g_vi_retrace;
    uint32_t g_vievtq_valid, g_vievtq_count;
    uint32_t c_ht, c_cop1;
    /* Round 57: core-thread host CPU accounting + PI ledger totals. */
    uint64_t core_utime, core_stime;
    char     core_state;
    uint32_t pir_rd_n, pir_wr_n, pir_rd_us, pir_wr_us, pir_rd_max, pir_wr_max;
    uint32_t lim_calls, lim_us, lim_max;
    uint32_t pump_n, pump_call, pump_us, pump_max;
    /* ROUND-69/70: the DD ASIC interrupt state + the cart line (IP3).  The
       DD's MECHA/BM level interrupts were invisible in every prior stall
       dump (raise_bits has no DD column). */
    uint32_t dd_asic_status, dd_cause_ip3, cause_ip_bits;
};

/* Read a guest u32 out of RDRAM (the guest sees KSEG0 0x80xxxxxx = phys). */
static uint32_t wd_guest32(uint32_t vaddr)
{
    const uint8_t* mb = (const uint8_t*)g_mem_base;
    uint32_t phys = (vaddr >= 0x80000000u) ? (vaddr - 0x80000000u) : vaddr;
    uint32_t w;
    if (mb == NULL || phys + 4 > 0x800000u) return 0xdeadbeefu;
    memcpy(&w, mb + phys, 4);
    return w;
}

static void wd_take_snap(struct wd_snap* s)
{
    uint32_t* cp0_regs = r4300_cp0_regs(&wd_r4300->cp0);
    int i;
    s->cause = cp0_regs[CP0_CAUSE_REG];
    s->status = cp0_regs[CP0_STATUS_REG];
    s->epc = cp0_regs[CP0_EPC_REG];
    s->badvaddr = cp0_regs[CP0_BADVADDR_REG];
    s->count = cp0_regs[CP0_COUNT_REG];
    s->mi_intr = g_dev.mi.regs[MI_INTR_REG];
    s->mi_mask = g_dev.mi.regs[MI_INTR_MASK_REG];
    /* ROUND-69/70: the DD ASIC's MECHA/BM interrupt bits and the live CAUSE
       IP bits (IP3 = the cart line the DD raises on).  CAUSE IP bits are
       0xff00; IP3 = 0x0800. */
    s->dd_asic_status = g_dev.dd.idisk != NULL ? g_dev.dd.regs[DD_ASIC_CMD_STATUS] : 0;
    s->cause_ip_bits = cp0_regs[CP0_CAUSE_REG] & UINT32_C(0xff00);
    s->dd_cause_ip3 = (s->cause_ip_bits & UINT32_C(0x0800)) != 0;
    s->sp_status = g_dev.sp.regs[SP_STATUS_REG];
    s->sp_pc = g_dev.sp.regs2[SP_PC_REG];
    s->sp_busy = g_dev.sp.regs[SP_DMA_BUSY_REG];
    s->sp_full = g_dev.sp.regs[SP_DMA_FULL_REG];
    s->sp_sem = g_dev.sp.regs[SP_SEMAPHORE_REG];
    s->c_task = wd_c_do_sp_task;
    s->c_spint = wd_c_sp_int_evt;
    s->c_genint = wd_c_gen_int;
    s->c_sample = wd_c_sample;
    s->c_asic = wd_c_dd_asic;
    s->c_pi = wd_c_pi_dma;
    s->vi_current = g_dev.vi.regs[VI_CURRENT_REG];
    s->vi_field = g_dev.vi.field;
    s->vi_delay = g_dev.vi.delay;
    s->c_exc = wd_c_exc_total;
    s->c_exc_int = wd_c_exc_int;
    s->c_exc_nested = wd_c_exc_nested;
    s->c_raise = wd_c_raise;
    s->c_signal = wd_c_signal;
    s->c_cmp_int = wd_c_cmp_int;
    s->c_vi_evt = wd_c_vi_evt;
    s->c_vi_ack = wd_c_vi_ack;
    for (i = 0; i < 8; i++) s->raise_bits[i] = wd_c_raise_bits[i];
    /* Guest libultra VI state.  The addresses are this build's (Base FZX-J):
       __osViCurr=0x80773110, __osViNext=0x80773114, contexts at 0x807730B0/0x807730E0
       (layout from libultra's PR/viint.h: state@0, retraceCount@2, framep@4).
       The game's own frame index lives at 0x8079A360 and gFrameBuffers at
       0x8079A330 (decomp: framebuffers at 0x801D9800 / 0x80200000). */
    s->g_vi_curr_framep = wd_guest32(wd_guest32(0x80773110u) + 4u);
    s->g_vi_next_framep = wd_guest32(wd_guest32(0x80773114u) + 4u);
    s->g_vi_curr_state = wd_guest32(wd_guest32(0x80773110u)) & 0xffffu;
    s->g_vi_retrace = (wd_guest32(wd_guest32(0x80773110u)) >> 16) & 0xffffu;
    /* libultra viEventQueue (created by osCreateViManager, capacity 5):
       OSMesgQueue { mtqueue@0, fullqueue@4, validCount@8, first@0xC,
       msgCount@0x10 }.  An empty queue with the vi manager thread blocked on
       it means no VI retrace message has been delivered to the guest. */
    s->g_vievtq_valid = wd_guest32(0x807C46C8u);
    s->g_vievtq_count = wd_guest32(0x807C46D0u);
    s->c_ht = wd_c_ht;
    s->c_cop1 = wd_c_cop1;
    /* Round 57: the core thread's host CPU time, straight from the kernel, so
       "the guest advances slowly" can be split into "the thread is burning CPU"
       versus "the thread is asleep in the speed limiter". */
    wd_read_core_stat(&s->core_utime, &s->core_stime, &s->core_state);
    s->pir_rd_n = wd_pir_rd_n;
    s->pir_wr_n = wd_pir_wr_n;
    s->pir_rd_us = wd_pir_rd_us;
    s->pir_wr_us = wd_pir_wr_us;
    s->pir_rd_max = wd_pir_rd_max;
    s->pir_wr_max = wd_pir_wr_max;
    s->lim_calls = wd_c_lim_calls;
    s->lim_us = wd_c_lim_us;
    s->lim_max = wd_c_lim_max;
    s->pump_n = wd_c_pump_n;
    s->pump_call = wd_c_pump_call;
    s->pump_us = wd_c_pump_us;
    s->pump_max = wd_c_pump_max;
}

static void wd_print_snap(FILE* f, const char* tag, const struct wd_snap* s)
{
    fprintf(f, "%s cause=%08x status=%08x epc=%08x badvaddr=%08x count=%08x\n",
        tag, s->cause, s->status, s->epc, s->badvaddr, s->count);
    fprintf(f, "%s dd_asic_status=%08x cause_ip_bits=%08x dd_ip3=%u (MECHA_INT=0x02000000 BM_INT=0x04000000)\n",
        tag, s->dd_asic_status, s->cause_ip_bits, s->dd_cause_ip3);
    fprintf(f, "%s mi_intr=%08x mi_mask=%08x sp_status=%08x sp_pc=%08x sp_busy=%08x sp_full=%08x sp_sem=%08x\n",
        tag, s->mi_intr, s->mi_mask, s->sp_status, s->sp_pc, s->sp_busy, s->sp_full, s->sp_sem);
    fprintf(f, "%s c_task=%u c_spint=%u c_genint=%u c_sample=%u c_asic=%u c_pi=%u vi_cur=%08x field=%u delay=%u\n",
        tag, s->c_task, s->c_spint, s->c_genint, s->c_sample, s->c_asic, s->c_pi,
        s->vi_current, s->vi_field, s->vi_delay);
    /* Round 8: what is generating guest exceptions, and is the guest's VI
       manager being fed?  raise_bits order = MI_INTR bits
       0=SP 1=SI 2=AI 3=VI 4=PI 5=DP. */
    fprintf(f, "%s c_exc=%u c_exc_int=%u c_exc_nested=%u c_raise=%u c_signal=%u c_cmp_int=%u c_vi_evt=%u c_vi_ack=%u\n",
        tag, s->c_exc, s->c_exc_int, s->c_exc_nested, s->c_raise, s->c_signal, s->c_cmp_int,
        s->c_vi_evt, s->c_vi_ack);
    fprintf(f, "%s raise_bits SP=%u SI=%u AI=%u VI=%u PI=%u DP=%u\n",
        tag, s->raise_bits[0], s->raise_bits[1], s->raise_bits[2],
        s->raise_bits[3], s->raise_bits[4], s->raise_bits[5]);
    fprintf(f, "%s guest viCurr.framep=%08x viNext.framep=%08x viCurr.state=%04x retrace=%u vievtq=%u/%u\n",
        tag, s->g_vi_curr_framep, s->g_vi_next_framep, s->g_vi_curr_state,
        s->g_vi_retrace, s->g_vievtq_valid, s->g_vievtq_count);
    fprintf(f, "%s c_ht=%u c_cop1=%u\n", tag, s->c_ht, s->c_cop1);
    /* Round 11: SP-memory integrity.  If the RSP's IMEM was destroyed by a
       stray write, these latches date it and name the writer. */
    {
        extern volatile uint32_t wd_cpuw_n, wd_cpuw_imem_n, wd_cpuw_fill_n;
        extern uint32_t wd_cpuw_latch_addr, wd_cpuw_latch_val, wd_cpuw_latch_mask;
        extern uint32_t wd_cpuw_last_addr, wd_cpuw_last_val, wd_cpuw_last_mask;
        extern uint32_t wd_cpuw_imem_latch_addr, wd_cpuw_imem_latch_val;
        extern volatile uint32_t wd_imem_bad;
        extern uint32_t wd_imem_bad_word[4], wd_imem_bad_fc0, wd_imem_bad_pc;
        extern uint32_t wd_imem_bad_status, wd_imem_bad_count, wd_imem_bad_spdma;

        fprintf(f, "%s cpuw n=%u imem_n=%u fill_n=%u last=%08x/%08x/%08x latch1=%08x/%08x/%08x latchIMEM=%08x/%08x\n",
            tag, wd_cpuw_n, wd_cpuw_imem_n, wd_cpuw_fill_n,
            wd_cpuw_last_addr, wd_cpuw_last_val, wd_cpuw_last_mask,
            wd_cpuw_latch_addr, wd_cpuw_latch_val, wd_cpuw_latch_mask,
            wd_cpuw_imem_latch_addr, wd_cpuw_imem_latch_val);
        fprintf(f, "%s imem_bad=%u word=%08x %08x %08x %08x fc0=%08x pc=%08x status=%08x count=%08x dmas=%u\n",
            tag, wd_imem_bad, wd_imem_bad_word[0], wd_imem_bad_word[1],            wd_imem_bad_word[2], wd_imem_bad_word[3], wd_imem_bad_fc0,
            wd_imem_bad_pc, wd_imem_bad_status, wd_imem_bad_count, wd_imem_bad_spdma);
        /* ROUND 19/49: which path wrote the fill pattern into IMEM (1 = CPU
           direct SP-memory write, 2 = CPU-side SP DMA, 5 = only ever seen by
           the pump; path 3 is a phantom -- the plugin never calls the probe and
           an RSP ucode cannot store IMEM), with which parameters.  `fill` is
           the sampled fill density at the latch (32 samples, threshold 20) and
           `gpc` the GUEST pc of the write, which for path 1 names the libultra
           routine doing the CPU store. */
        {
            extern volatile uint32_t wd_imem_kill_path;
            extern uint32_t wd_imem_kill_a, wd_imem_kill_b, wd_imem_kill_c, wd_imem_kill_d;
            extern uint32_t wd_imem_kill_pc, wd_imem_kill_count, wd_imem_kill_spdma;
            extern uint32_t wd_imem_kill_fill, wd_imem_kill_gpc;
            fprintf(f, "%s IMEMKILL path=%u a=%08x b=%08x c=%08x d=%08x pc=%08x count=%08x dmas=%u fill=%u gpc=%08x\n",
                tag, wd_imem_kill_path, wd_imem_kill_a, wd_imem_kill_b, wd_imem_kill_c,
                wd_imem_kill_d, wd_imem_kill_pc, wd_imem_kill_count, wd_imem_kill_spdma,
                wd_imem_kill_fill, wd_imem_kill_gpc);
        }
    }
    /* ROUND 13: the frame protocol.  A task-load is classified by the type word
       in the OSTask the guest DMAd into DMEM 0xFC0 (1=gfx, 2=audio); the RDP
       kick is the ares RSP's `mtc0 DPC_END` reaching this core's
       ProcessRdpList wrapper.  Guest thread state (round 12): sGameThread
       blocked in osRecvMesg(&D_800DCAC8) for message 0x2A == waiting for
       EVENT_MESG_DP == waiting for MI_INTR_DP.  If gfx loads == 0 the guest
       never submitted a frame; if gfx loads > 0 but kicks == 0 the RSP never
       reached DPC_END; if kicks > 0 the RDP ran and the interrupt is what is
       being lost. */
    {
        extern volatile uint32_t wd_hdr_type_n[4], wd_c_gfx_load, wd_c_audio_load;
        extern volatile uint32_t wd_c_task_etype[4];
        extern volatile uint32_t wd_c_rdp_kick, wd_c_sp_status_wr, wd_c_sp_sig_wr;
        extern volatile uint32_t wd_hdr_ring_n, wd_spw_ring_n;
        extern uint32_t wd_rdp_last_start, wd_rdp_last_end, wd_rdp_last_mi, wd_rdp_last_sp;
        fprintf(f, "%s FRAME loads t0=%u t1gfx=%u t2aud=%u t3=%u entrytype t0=%u t1=%u t2=%u t3=%u\n",
            tag, wd_hdr_type_n[0], wd_hdr_type_n[1], wd_hdr_type_n[2], wd_hdr_type_n[3],
            wd_c_task_etype[0], wd_c_task_etype[1], wd_c_task_etype[2], wd_c_task_etype[3]);
        /* ROUND 25: did the task-load guard engage?  `set` counts the guest's
           __osSpSetPc(SP_IMEM_START) (i.e. osSpTaskLoad entries) and `skip` the
           background-pump slices refused because the guest still owns the RSP.
           Both must be 0 on a plain cart run. */
        {
            extern volatile uint32_t wd_c_loadguard_set, wd_c_loadguard_skip;
            fprintf(f, "%s LOADGUARD set=%u skip=%u\n",
                tag, wd_c_loadguard_set, wd_c_loadguard_skip);
        }
        fprintf(f, "%s RDPKICK n=%u last start=%08x end=%08x mi=%08x sp_status=%08x spwr=%u sigwr=%u\n",
            tag, wd_c_rdp_kick, wd_rdp_last_start, wd_rdp_last_end, wd_rdp_last_mi,
            wd_rdp_last_sp, wd_c_sp_status_wr, wd_c_sp_sig_wr);
        {
            /* ROUND-17: did the video plugin actually see a command list, and
               did it raise MI_INTR_DP for it?  empty = the CURRENT..END window
               handed to parallel-RDP was non-positive, i.e. the plugin returned
               without touching a single command. */
            extern volatile uint32_t wd_c_rdp_dp_seen, wd_c_rdp_dp_hot, wd_c_rdp_empty;
            extern volatile uint32_t wd_rdp_ring_n;
            extern uint32_t wd_rdp_ring[16][6];
            uint32_t k, n = wd_rdp_ring_n < 16 ? wd_rdp_ring_n : 16;
            extern volatile uint32_t wd_c_rdp_noadv, wd_c_rdp_bad;
            fprintf(f, "%s RDPDP dp_seen=%u dp_hot=%u empty=%u ring_n=%u noadv=%u bad=%u\n",
                tag, wd_c_rdp_dp_seen, wd_c_rdp_dp_hot, wd_c_rdp_empty, wd_rdp_ring_n,
                wd_c_rdp_noadv, wd_c_rdp_bad);
            for (k = 0; k < n; k++)
            {
                uint32_t* e = wd_rdp_ring[(wd_rdp_ring_n - n + k) & 15];
                fprintf(f, "%s RDPR %u start=%08x cur=%08x end=%08x st=%08x mib=%08x mia=%08x\n",
                    tag, k, e[0], e[1], e[2], e[3], e[4], e[5]);
            }
            /* ROUND 19: the first 16 kicks, in order. */
            {
                extern volatile uint32_t wd_rdp_first_n;
                extern uint32_t wd_rdp_first[16][6];
                uint32_t m = wd_rdp_first_n < 16 ? wd_rdp_first_n : 16;
                for (k = 0; k < m; k++)
                {
                    uint32_t* e = wd_rdp_first[k];
                    fprintf(f, "%s RDPF %u start=%08x cur=%08x end=%08x st=%08x mib=%08x mia=%08x\n",
                        tag, k, e[0], e[1], e[2], e[3], e[4], e[5]);
                }
            }
        }
        {
            extern volatile uint32_t wd_c_mi_rd_dp, wd_c_dp_ack, wd_c_signal, wd_c_raise;
            extern volatile uint32_t wd_c_dp_consumed;
            fprintf(f, "%s DPCHAIN mi_rd_dp=%u dp_ack=%u dp_consumed=%u core_signal=%u core_raise=%u\n",
                tag, wd_c_mi_rd_dp, wd_c_dp_ack, wd_c_dp_consumed, wd_c_signal, wd_c_raise);
        }
        /* ROUND 20 of the FIFO protocol lives in the RSP plugin (wd_r20.txt,
           rewritten every few seconds): the core cannot reference the plugin's
           symbols, so those counters are dumped from there. */
    }
}

/* One line per /proc/self/task/<tid>: tid comm <full stat line>.  The stat line
   carries the kernel state and utime/stime, which is what distinguishes a
   spinning thread from a sleeping one. */
static void wd_dump_threads(FILE* f)
{
    DIR* d = opendir("/proc/self/task");
    if (d == NULL) { fprintf(f, "THR opendir failed\n"); return; }
    struct dirent* e;
    while ((e = readdir(d)) != NULL)
    {
        char path[128], buf[1024];
        FILE* g;
        int n;
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
        buf[0] = 0;
        g = fopen(path, "r");
        if (g) { if (fgets(buf, sizeof(buf), g) == NULL) buf[0] = 0; fclose(g); }
        for (n = 0; buf[n]; n++) if (buf[n] == '\n') buf[n] = 0;
        fprintf(f, "THR tid=%s comm=%s ", e->d_name, buf);
        snprintf(path, sizeof(path), "/proc/self/task/%s/stat", e->d_name);
        g = fopen(path, "r");
        if (g) {
            if (fgets(buf, sizeof(buf), g) != NULL) fprintf(f, "stat=%s", buf);
            fclose(g);
        } else fprintf(f, "stat=?\n");
    }
    closedir(d);
}

static void wd_stall_probe(const char* path)
{
    FILE* f;
    struct wd_snap a, b;
    if (g_dev.dd.idisk == NULL || wd_r4300 == NULL) return;
    wd_take_snap(&a);
    usleep(300 * 1000);
    wd_take_snap(&b);
    f = fopen(path, "wb");
    if (f == NULL) return;
    fprintf(f, "WDSTALL v1\n");
    wd_print_snap(f, "A", &a);
    wd_print_snap(f, "B", &b);
    /* Narrow DD-only dispatcher/context rings; no guest state changes. */
    n64dd_dispatch_diag_dump(f);
    fprintf(f, "DELTA c_task=%d c_spint=%d c_genint=%d c_sample=%d c_asic=%d c_pi=%d sp_pc=%08x\n",
        (int)(b.c_task - a.c_task), (int)(b.c_spint - a.c_spint),
        (int)(b.c_genint - a.c_genint), (int)(b.c_sample - a.c_sample),
        (int)(b.c_asic - a.c_asic), (int)(b.c_pi - a.c_pi), b.sp_pc ^ a.sp_pc);
    /* Round 8: the high-rate side of the machine.  genint/sample are the
       interrupt path; exc/raise are the SYNCHRONOUS exception path taken
       directly by raise_maskable_interrupt(); vi_evt/vi_ack say whether the
       guest is dispatching (and acknowledging) VI at all. */
    fprintf(f, "DELTA2 c_exc=%d c_exc_int=%d c_exc_nested=%d c_raise=%d c_signal=%d c_cmp_int=%d c_vi_evt=%d c_vi_ack=%d\n",
        (int)(b.c_exc - a.c_exc), (int)(b.c_exc_int - a.c_exc_int),
        (int)(b.c_exc_nested - a.c_exc_nested), (int)(b.c_raise - a.c_raise),
        (int)(b.c_signal - a.c_signal), (int)(b.c_cmp_int - a.c_cmp_int),
        (int)(b.c_vi_evt - a.c_vi_evt), (int)(b.c_vi_ack - a.c_vi_ack));
    fprintf(f, "DELTA2 raise_bits dSP=%d dSI=%d dAI=%d dVI=%d dPI=%d dDP=%d\n",
        (int)(b.raise_bits[0] - a.raise_bits[0]), (int)(b.raise_bits[1] - a.raise_bits[1]),
        (int)(b.raise_bits[2] - a.raise_bits[2]), (int)(b.raise_bits[3] - a.raise_bits[3]),
        (int)(b.raise_bits[4] - a.raise_bits[4]), (int)(b.raise_bits[5] - a.raise_bits[5]));
    fprintf(f, "DELTA2 guest viCurr.framep %08x -> %08x (goal fb[0]=801d9800 fb[1]=80200000), vievtq %u/%u -> %u/%u\n",
        a.g_vi_curr_framep, b.g_vi_curr_framep, a.g_vievtq_valid, a.g_vievtq_count,
        b.g_vievtq_valid, b.g_vievtq_count);
    /* Round 9: how fast is the dynarec resolving blocks, and how fast is it
       taking COP1-unusable faults?  c_ht == c_cop1 means no guest instruction
       is executing between faults (pure emulator-side loop). */
    fprintf(f, "DELTA3 c_ht=%d c_cop1=%d\n",
        (int)(b.c_ht - a.c_ht), (int)(b.c_cop1 - a.c_cop1));
    /* ROUND 57: THE CORE THREAD ITSELF.  utime/stime are kernel jiffies
       (USER_HZ=100 on Android) consumed by the thread that runs run_r4300(),
       so a 300 ms window can hold at most 30 ticks.  30/30 means the thread is
       CPU-bound and the guest's slowness is emulation cost; 1-2/30 means the
       thread is asleep and the guest is being throttled.  `state` is the
       kernel state at the B sample (R=run, S=sleep, D=uninterruptible). */
    fprintf(f, "DELTA5 CORE tid=%d state=%c d_utime=%d d_stime=%d (of 30 ticks/300ms) "
               "abs_utime=%llu abs_stime=%llu lim_calls=%d lim_us=%d lim_max=%d\n",
        (int)wd_core_tid, b.core_state,
        (int)(b.core_utime - a.core_utime), (int)(b.core_stime - a.core_stime),
        (unsigned long long)b.core_utime, (unsigned long long)b.core_stime,
        (int)(b.lim_calls - a.lim_calls), (int)(b.lim_us - a.lim_us), (int)b.lim_max);
    /* ROUND 57: PI ledger totals over the same window + worst-case handler. */
    fprintf(f, "DELTA5 PI rd=%d rd_us=%d rd_max=%d wr=%d wr_us=%d wr_max=%d vi_delay=%u\n",
        (int)(b.pir_rd_n - a.pir_rd_n), (int)(b.pir_rd_us - a.pir_rd_us), (int)b.pir_rd_max,
        (int)(b.pir_wr_n - a.pir_wr_n), (int)(b.pir_wr_us - a.pir_wr_us), (int)b.pir_wr_max,
        (unsigned)b.vi_delay);
    /* ROUND 57: the background RSP pump.  pump_n counts every call of
       rsp_dd_background_pump() (i.e. every CPU block boundary on the DD route);
       pump_call counts the ones that actually ran do_SP_Task(); pump_us is the
       host time those consumed.  Compare d_us (in DELTA5 CORE) against the sum
       of lim_us and pump_us: whatever is left is the guest's own execution. */
    fprintf(f, "DELTA5 PUMP n=%d call=%d us=%d max=%d (core d_utime+d_stime=%d us)\n",
        (int)(b.pump_n - a.pump_n), (int)(b.pump_call - a.pump_call),
        (int)(b.pump_us - a.pump_us), (int)b.pump_max,
        (int)((b.core_utime - a.core_utime) + (b.core_stime - a.core_stime)) * 10000);
    /* ROUND-68: guest-PC distribution over the stall window (the pump fires
       at every recompiler block boundary; the loaded DD-boot code at
       0x80600000+ has no decomp symbols, so the raw addresses are the
       deliverable -- group them offline). */
    {
        extern uint32_t wd68_pcp_ring[64];
        extern volatile uint32_t wd68_pcp_n;
        uint32_t n68 = wd68_pcp_n < 64 ? wd68_pcp_n : 64;
        fprintf(f, "WD_PCREG n=%u:", wd68_pcp_n);
        for (uint32_t i = 0; i < n68; i++)
            fprintf(f, " %08x", wd68_pcp_ring[(wd68_pcp_n - n68 + i) & 63u]);
        fprintf(f, "\n");
    }
    /* ROUND-69/70: the dispatch trace -- samples taken while the guest PC
       was inside __osDispatchThread: pc, __osRunQueue, __osRunningThread,
       sAudioThread.state|flags, sAudioThread saved pc, sIdleThread.state.
       Reconstructs the lost context switch from the inside (HANDOFF 4c). */
    {
        extern uint32_t wd69_disp_ring[64][6];
        extern volatile uint32_t wd69_disp_n;
        uint32_t n69 = wd69_disp_n < 64 ? wd69_disp_n : 64;
        fprintf(f, "WD_DISPDSP n=%u\n", wd69_disp_n);
        for (uint32_t i = 0; i < n69; i++)
        {
            const uint32_t* e = wd69_disp_ring[(wd69_disp_n - n69 + i) & 63u];
            fprintf(f, "  D pc=%08x runq=%08x running=%08x aud=%08x/%08x idle_st=%08x\n",
                    e[0], e[1], e[2], e[3], e[4], e[5]);
        }
    }
    /* ROUND-70: the ERET ledger -- the guest pc of the last 64 erets. */
    {
        extern uint32_t wd70_eret_ring[64];
        extern volatile uint32_t wd70_eret_n;
        uint32_t n70 = wd70_eret_n < 64 ? wd70_eret_n : 64;
        fprintf(f, "WD_ERET n=%u:", wd70_eret_n);
        for (uint32_t i = 0; i < n70; i++)
            fprintf(f, " %08x", wd70_eret_ring[(wd70_eret_n - n70 + i) & 63u]);
        fprintf(f, "\n");
    }
    /* ROUND 49: SP MEMORY IN ONE LINE.
       Until now the only way to see this was the raw 8 KiB image in the big
       dump (SPMEM, a bare fwrite), which needs a python pass to read -- so the
       single most decisive fact about the RSP, "is there a program in IMEM at
       all?", was invisible in every wd_stall.txt of the campaign.

       Bank map, pinned down this round from the tree's own logs rather than
       from memory: sp->mem byte offset 0x0000 is DMEM and 0x1000 is IMEM.  The
       evidence is the trace this tree already writes (files/wd_dma2.txt): the
       64-byte OSTask header DMA lands at dst=0fc0 and afterwards
       `sp->mem[0xfc0/4]` reads that header's type, while the 4096-byte ucode
       load lands at dst=1000 and afterwards `sp->mem[0x1000/4]` reads the
       ucode's first word.  libultra's `__osSpSetPc(SP_IMEM_START)` is
       0x04001000 (see write_rsp_regs2), i.e. bit 12 SET = IMEM, so IMEM is the
       bank at 0x1000 and holds ucode CODE; DMEM at 0x0000 holds the header and
       ucode DATA.

       `imem_runs` is the number of maximal runs of equal words in IMEM: a real
       ucode scores ~1024, a single fill pattern scores 1.  O(n), no alloc. */
    {
        const uint32_t* m = (const uint32_t*)g_dev.sp.mem;
        uint32_t i, imem_runs = 1, imem_nz = 0, imem_fill = 0, dmem_runs = 1, dmem_nz = 0;
        for (i = 0; i < 1024; i++) {
            uint32_t wi = m[(0x1000 >> 2) + i], wd = m[i];
            if (wi) imem_nz++;
            if (wi == 0x00010001u) imem_fill++;
            if (i && wi != m[(0x1000 >> 2) + i - 1]) imem_runs++;
            if (wd) dmem_nz++;
            if (i && wd != m[i - 1]) dmem_runs++;
        }
        fprintf(f, "SPMEM1 dmem_nz=%u dmem_runs=%u imem_nz=%u imem_runs=%u imem_fill=%u "
                   "imem0=%08x dmem0=%08x dmem_fc0=%08x sp_pc=%08x sp_status=%08x\n",
                dmem_nz, dmem_runs, imem_nz, imem_runs, imem_fill,
                m[0x1000 >> 2], m[0], m[0xfc0 >> 2],
                g_dev.sp.regs2[SP_PC_REG], g_dev.sp.regs[SP_STATUS_REG]);
    }
    wd_pir_dump(f);
    /* Round 8: THE CP0 EVENT QUEUE.  gen_interrupt() dispatches on
       cp0.q.first->data.type, and the VI_INT handler (case 0) is what re-arms
       the next vertical interrupt -- so if the VI event is missing from this
       list, or buried behind events that keep being re-added, c_vi_evt stops
       advancing and the guest's libultra vi manager thread never receives a
       retrace.  That is exactly the observed state (c_vi_evt froze at 517 and
       guest viCurr.framep never leaves 0x80200000), which parks the boot on
       `while (osViGetCurrentFramebuffer() != gFrameBuffers[i]) {}`.
       type: 0=VI 1=COMPARE 2=CHECK 3=SI 4=PI 5=SPECIAL 6=AI 7=SP 8=DP
             9=HW2 10=NMI 12=RSP_DMA 13/14/15=DD MC/BM/DV.
       delta = event count - CP0 COUNT (negative/0 means it is already due). */
    {
        const struct node* e = wd_r4300->cp0.q.first;
        uint32_t cnt = r4300_cp0_regs(&wd_r4300->cp0)[CP0_COUNT_REG];
        int n;
        fprintf(f, "CP0Q count=%08x head=%p\n", cnt, (const void*)e);
        for (n = 0; e != NULL && n < 16; e = e->next, n++) {
            fprintf(f, "  EV%d type=%d count=%08x delta=%d\n",
                n, e->data.type, e->data.count, (int32_t)(e->data.count - cnt));
        }
        if (e != NULL) fprintf(f, "  ... (list longer than 16)\n");
    }
    /* dynarec recent-block ring, oldest first */
    {
        uint32_t i, start = wd_pc_idx;
        fprintf(f, "RING n=%u\n", (unsigned)WD_PC_RING);
        for (i = 0; i < WD_PC_RING; i++) {
            uint32_t idx = (start + i) & (WD_PC_RING - 1);
            fprintf(f, "%08x %08x %08x\n", (uint32_t)wd_pc_ring[idx],
                (uint32_t)wd_reg_ring[idx][0], (uint32_t)wd_reg_ring[idx][1]);
        }
    }
    /* live RSP memory: DMEM then IMEM (sp.mem layout: DMEM 0, IMEM 0x1000) */
    /* Round 9: the true dynarec dispatch trace (get_addr_ht only) and the
       COP1-unusable fault capture.  Read HTRING* bottom-up for
       oldest->newest order. */
    {
        uint32_t i, start = wd_ht_idx;
        fprintf(f, "HTRING n=%u total=%u\n", (unsigned)WD_HT_RING, (unsigned)wd_c_ht);
        for (i = 0; i < WD_HT_RING; i++)
            fprintf(f, "  %08x\n", (uint32_t)wd_ht_ring[(start + i) & (WD_HT_RING - 1)]);
    }
    {
        uint32_t i, start = wd_cop1_idx;
        fprintf(f, "COP1 total=%u\n", (unsigned)wd_c_cop1);
        for (i = 0; i < 16; i++)
            fprintf(f, "  %08x\n", (uint32_t)wd_cop1_ring[(start + i) & 15]);
    }
    /* VEC0 = hash entry hit for 0x80000180; VEC1 = hash entry for 0x80000400.
       vaddr/start/length say whether the block handed back for the vector is
       really the vector's translation. */
    fprintf(f, "HTVEC v0.vaddr=%08x v0.start=%08x v0.len=%08x v0.addr=%08x v0.clean=%08x v0.reg32=%08x\n",
        (unsigned)wd_vec_probe[0][0], (unsigned)wd_vec_probe[0][1],
        (unsigned)wd_vec_probe[0][2], (unsigned)wd_vec_probe[0][3],
        (unsigned)wd_vec_probe[0][4], (unsigned)wd_vec_probe[0][5]);
    fprintf(f, "HTVEC v1.vaddr=%08x v1.start=%08x v1.len=%08x v1.addr=%08x\n",
        (unsigned)wd_vec_probe[0][6], (unsigned)wd_vec_probe[0][7],
        (unsigned)wd_vec_probe[0][8], (unsigned)wd_vec_probe[0][9]);
    fprintf(f, "HTVEC f0.vaddr=%08x f0.start=%08x f0.len=%08x f0.addr=%08x\n",
        (unsigned)wd_vec_probe[1][0], (unsigned)wd_vec_probe[1][1],
        (unsigned)wd_vec_probe[1][2], (unsigned)wd_vec_probe[1][3]);
    fprintf(f, "HTVEC f1.vaddr=%08x f1.start=%08x f1.len=%08x f1.addr=%08x\n",
        (unsigned)wd_vec_probe[1][4], (unsigned)wd_vec_probe[1][5],
        (unsigned)wd_vec_probe[1][6], (unsigned)wd_vec_probe[1][7]);
    {
        uint32_t s, i, n = wd_cop1_snap_n;
        if (n > WD_COP1_SNAP) n = WD_COP1_SNAP;
        for (s = 0; s < n; s++) {
            const volatile uint32_t* m = wd_cop1_meta[s];
            fprintf(f, "COP1SNAP%u pc=%08x sr=%08x cause=%08x epc=%08x badvaddr=%08x ds=%u htret=%08x%08x\n",
                s, m[0], m[1], m[2], m[3], m[4], m[5], m[7], m[6]);
            fprintf(f, "  regs");
            for (i = 0; i < 32; i++) fprintf(f, " %08x", (uint32_t)wd_cop1_regs[s][i]);
            fprintf(f, "\n");
        }
    }
    /* ROUND 13: the last 32 task loads (DMEM 0xFC0 header copies) and the last
       16 guest SP_STATUS writes, oldest first.  ring idx/serial name the task
       (ucode == 0x807505C0 / ucode_data == 0x80779860 is gspF3DEX2_fifo, i.e. a
       gfx task; ucode == 0x80768E60 is aspMain, i.e. audio). */
    {
        extern volatile uint32_t wd_hdr_type_n[4], wd_c_gfx_load, wd_c_audio_load;
        extern volatile uint32_t wd_hdr_ring_n, wd_spw_ring_n;
        extern uint32_t wd_hdr_ring[32][8], wd_spw_ring[16][2];
        uint32_t i, start = wd_hdr_ring_n;
        fprintf(f, "TASKRING n=%u gfxn=%u audn=%u\n", (unsigned)wd_hdr_ring_n,
                (unsigned)wd_c_gfx_load, (unsigned)wd_c_audio_load);
        /* ROUND 22: the header the plugin acts on for a forced yield -- latched
           from the task-load DMA source, not read from the clobbered DMEM copy.
           [0] type [1] flags [4] ucode [14] yield_data_ptr [15] yield_data_size */
        {
            extern uint32_t wd_cur_hdr[16];
            extern volatile uint32_t wd_cur_hdr_seq;
            fprintf(f, "CURHDR seq=%u type=%08x flags=%08x ucode=%08x ucd=%08x data=%08x yptr=%08x ysz=%08x\n",
                    (unsigned)wd_cur_hdr_seq, wd_cur_hdr[0], wd_cur_hdr[1], wd_cur_hdr[4],
                    wd_cur_hdr[6], wd_cur_hdr[12], wd_cur_hdr[14], wd_cur_hdr[15]);
        }
        for (i = 0; i < 32; i++)
        {
            const uint32_t* e = wd_hdr_ring[(start + i) & 31u];
            if (e[0] == 0 && e[1] == 0 && e[2] == 0) continue;
            fprintf(f, "  T seq=%u type=%u ucode=%08x ucdata=%08x data=%08x status=%08x pc=%08x raw=%08x\n",
                e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7]);
        }
        start = wd_spw_ring_n;
        fprintf(f, "SPWRING n=%u\n", (unsigned)wd_spw_ring_n);
        for (i = 0; i < 16; i++)
        {
            const uint32_t* e = wd_spw_ring[(start + i) & 15u];
            if (e[0] == 0 && e[1] == 0) continue;
            fprintf(f, "  W val=%08x pc=%08x\n", e[0], e[1]);
        }
    }
    /* live RSP memory: DMEM then IMEM (sp.mem layout: DMEM 0, IMEM 0x1000) */
    fprintf(f, "SPMEM\n");
    fwrite(g_dev.sp.mem, 1, SP_MEM_SIZE, f);
    fprintf(f, "THREADS\n");
    wd_dump_threads(f);
    fprintf(f, "HOSTPC n=%u armed=%d\n", (unsigned)wd_pcs_idx, wd_pcs_armed);
    {
        uint32_t i, start = wd_pcs_idx;
        for (i = 0; i < WD_PCS_N; i++) {
            uint32_t k = (start + i) & (WD_PCS_N - 1);
            fprintf(f, "HPC %016llx %016llx %016llx %08x\n",
                (unsigned long long)wd_pcs[k][0], (unsigned long long)wd_pcs[k][1],
                (unsigned long long)wd_pcs[k][2], (unsigned)wd_pcs[k][3]);
        }
    }
    fprintf(f, "WDSTALL_END\n");
    fclose(f);
    DebugMessage(M64MSG_WARNING, "WDSTALL -> %s", path);
}

/* Dump the full 8MB RDRAM + "WD pc=" header + CPU/CP0/SP/VI/MI state + DD
   trace to `path`. */
static void wd_full_dump(const char* path, uint32_t pc){
    FILE* f;
    /* DD route only: the watchdog exists for the 64DD/EK boot bringsup and
       must stay invisible to plain carts (user rule 2026-09-05). Checked at
       dump time because dd.idisk is only valid after init_device. */
    if (g_dev.dd.idisk == NULL) return;
    f = fopen(path, "wb");
    if (f == NULL) return;
    const uint8_t* mb = (const uint8_t*)g_mem_base;
    int64_t* regs = r4300_regs(wd_r4300);
    fwrite(mb, 1, 0x800000, f);
    fprintf(f, "WD pc=%08x ra=%08x sp=%08x a0=%08x a1=%08x a2=%08x a3=%08x v0=%08x s0=%08x s3=%08x s4=%08x s5=%08x\n",
        pc, (uint32_t)regs[31], (uint32_t)regs[29], (uint32_t)regs[4], (uint32_t)regs[5],
        (uint32_t)regs[6], (uint32_t)regs[7], (uint32_t)regs[2], (uint32_t)regs[16],
        (uint32_t)regs[19], (uint32_t)regs[20], (uint32_t)regs[21]);
    uint32_t* cp0_regs = r4300_cp0_regs(&wd_r4300->cp0);
    fprintf(f, "WDCP0 cause=%08x status=%08x epc=%08x badvaddr=%08x count=%08x\n",
        cp0_regs[CP0_CAUSE_REG], cp0_regs[CP0_STATUS_REG],
        cp0_regs[CP0_EPC_REG], cp0_regs[CP0_BADVADDR_REG], cp0_regs[CP0_COUNT_REG]);
    fprintf(f, "MISTATE intr=%08x mask=%08x\n",
        g_dev.mi.regs[MI_INTR_REG], g_dev.mi.regs[MI_INTR_MASK_REG]);
    fprintf(f, "SPSTATE status=%08x dma_busy=%08x dma_full=%08x pc=%08x\n",
        g_dev.sp.regs[SP_STATUS_REG], g_dev.sp.regs[SP_DMA_BUSY_REG],
        g_dev.sp.regs[SP_DMA_FULL_REG], g_dev.sp.regs2[SP_PC_REG]);
    fprintf(f, "VISTATE field=%u delay=%u cpsl=%u current=%08x origin=%08x\n",
        g_dev.vi.field, g_dev.vi.delay, g_dev.vi.count_per_scanline,
        g_dev.vi.regs[VI_CURRENT_REG], g_dev.vi.regs[VI_ORIGIN_REG]);
    /* Round 8: the guest's own libultra VI state, so the dump is
       self-describing.  framep is what osViGetCurrentFramebuffer() returns and
       what sys_gfx.c:198 spins on; vievtq is the libultra viEventQueue that
       the vi manager thread (prio 254) blocks on waiting for a retrace. */
    fprintf(f, "GUESTVI viCurr.framep=%08x viNext.framep=%08x viCurr.state=%04x retrace=%u vievtq=%u/%u idx=%u fb0=%08x fb1=%08x\n",
        wd_guest32(wd_guest32(0x80773110u) + 4u),
        wd_guest32(wd_guest32(0x80773114u) + 4u),
        wd_guest32(wd_guest32(0x80773110u)) & 0xffffu,
        (wd_guest32(wd_guest32(0x80773110u)) >> 16) & 0xffffu,
        wd_guest32(0x807C46C8u), wd_guest32(0x807C46D0u),
        wd_guest32(0x8079A360u), wd_guest32(0x8079A330u), wd_guest32(0x8079A334u));
    fprintf(f, "EXCSTATE total=%u int=%u nested=%u raise=%u signal=%u cmp=%u vi_evt=%u vi_ack=%u rb=%u/%u/%u/%u/%u/%u\n",
        wd_c_exc_total, wd_c_exc_int, wd_c_exc_nested, wd_c_raise, wd_c_signal,
        wd_c_cmp_int, wd_c_vi_evt, wd_c_vi_ack,
        wd_c_raise_bits[0], wd_c_raise_bits[1], wd_c_raise_bits[2],
        wd_c_raise_bits[3], wd_c_raise_bits[4], wd_c_raise_bits[5]);
    /* ROUND 13: the frame protocol, one line, so the 8MB image is
       self-describing (see wd_print_snap for what each number means). */
    {
        extern volatile uint32_t wd_hdr_type_n[4], wd_c_gfx_load, wd_c_audio_load;
        extern volatile uint32_t wd_c_task_etype[4];
        extern volatile uint32_t wd_c_rdp_kick, wd_c_sp_status_wr, wd_c_sp_sig_wr;
        extern uint32_t wd_rdp_last_start, wd_rdp_last_end, wd_rdp_last_mi, wd_rdp_last_sp;
        extern volatile uint32_t wd_c_mi_rd_dp, wd_c_dp_ack;
        fprintf(f, "FRAMEPROTO loads t0=%u gfx=%u aud=%u t3=%u entry t1=%u t2=%u rdpkick=%u kick_last=%08x..%08x mi=%08x spwr=%u sigwr=%u dp_rd=%u dp_ack=%u\n",
            wd_hdr_type_n[0], wd_hdr_type_n[1], wd_hdr_type_n[2], wd_hdr_type_n[3],
            wd_c_task_etype[1], wd_c_task_etype[2], wd_c_rdp_kick,
            wd_rdp_last_start, wd_rdp_last_end, wd_rdp_last_mi,
            wd_c_sp_status_wr, wd_c_sp_sig_wr, wd_c_mi_rd_dp, wd_c_dp_ack);
    }
    dd_trace_dump(f);
    wd_pir_dump(f);
    fclose(f);
    /* Round 6: also write the small "what is still moving" probe.  The 8MB
       image above proves the guest state; this proves which side of the
       machine is alive. */
    wd_stall_probe(WD_FILES_DIR "wd_stall.txt");
    DebugMessage(M64MSG_WARNING, "WDDUMP pc=%08x -> %s", pc, path);
}

/* Round 35: poll the low-RDRAM canary.  Runs on the watchdog thread.  Returns
   silently while the guest's own exception vector is intact. */
static void wd_low_check(void)
{
    const uint8_t* dram;
    uint32_t w180, op;
    if (g_mem_base == NULL || g_dev.dd.idisk == NULL) return;
    dram = (const uint8_t*)mem_base_u32(g_mem_base, MM_RDRAM_DRAM);
    if (dram == NULL) return;
    memcpy(&w180, dram + 0x180, 4);

    if (!wd_low_armed) {
        /* libultra installs `lui k0,...` (op 0x0F) or a plain `j` (op 0x02)
           at the vector.  Anything else means the region is still boot
           scratch, which is expected to change -- so require the prologue to
           be present and the whole window stable across two polls before
           arming, and never trip on the boot-time churn. */
        op = (w180 >> 26) & 0x3fu;
        if (op == 0x0fu || op == 0x02u) {
            if (wd_low_seen &&
                memcmp(wd_low_shadow, dram, WD_LOW_WINDOW) == 0) {
                wd_low_armed = 1;
            } else {
                memcpy(wd_low_shadow, dram, WD_LOW_WINDOW);
                wd_low_seen = 1;
            }
        } else {
            wd_low_seen = 0;
        }
        return;
    }

    if (memcmp(wd_low_shadow, dram, WD_LOW_WINDOW) != 0) {
        uint32_t i, n, pc = 0;
        struct precomp_instr** pp;
        FILE* f;
        if (wd_low_dumped) { wd_low_n++; return; }
        wd_low_dumped = 1;
        for (i = 0; i < WD_LOW_WINDOW; i += 4) {
            if (memcmp(wd_low_shadow + i, dram + i, 4) != 0) {
                memcpy(&wd_low_was, wd_low_shadow + i, 4);
                memcpy(&wd_low_now, dram + i, 4);
                wd_low_off = i;
                break;
            }
        }
        pp = (wd_r4300 != NULL) ? r4300_pc_struct(wd_r4300) : NULL;
        if (pp != NULL && *pp != NULL) pc = (*pp)->addr;
        f = fopen(WD_FILES_DIR "wd_lowdump.txt", "w");
        if (f) {
            fprintf(f, "WDLOWDUMP off=%03x was=%08x now=%08x w0_was=%08x w0_now=%08x "
                       "w180_was=%08x w180_now=%08x guestpc=%08x lx_n=%u\n",
                    wd_low_off, wd_low_was, wd_low_now,
                    ((const uint32_t*)wd_low_shadow)[0], ((const uint32_t*)dram)[0],
                    ((const uint32_t*)wd_low_shadow)[0x180 / 4],
                    ((const uint32_t*)dram)[0x180 / 4], pc, (unsigned)wd_lx_n);
            fprintf(f, "SP mem=%08x dram=%08x rd=%08x wr=%08x st=%08x full=%u busy=%u\n",
                    (unsigned)g_dev.sp.regs[SP_MEM_ADDR_REG],
                    (unsigned)g_dev.sp.regs[SP_DRAM_ADDR_REG],
                    (unsigned)g_dev.sp.regs[SP_RD_LEN_REG],
                    (unsigned)g_dev.sp.regs[SP_WR_LEN_REG],
                    (unsigned)g_dev.sp.regs[SP_STATUS_REG],
                    (unsigned)g_dev.sp.regs[SP_DMA_FULL_REG],
                    (unsigned)g_dev.sp.regs[SP_DMA_BUSY_REG]);
            fprintf(f, "PI cart=%08x dram=%08x rd=%08x wr=%08x st=%08x intr=%08x\n",
                    (unsigned)g_dev.pi.regs[PI_CART_ADDR_REG],
                    (unsigned)g_dev.pi.regs[PI_DRAM_ADDR_REG],
                    (unsigned)g_dev.pi.regs[PI_RD_LEN_REG],
                    (unsigned)g_dev.pi.regs[PI_WR_LEN_REG],
                    (unsigned)g_dev.pi.regs[PI_STATUS_REG],
                    (unsigned)g_dev.mi.regs[MI_INTR_REG]);
            n = (wd_lx_n < WD_LX_RING) ? wd_lx_n : WD_LX_RING;
            for (i = 0; i < n; i++) {
                uint32_t k = (wd_lx_n - n + i) & (WD_LX_RING - 1);
                fprintf(f, "  LX k=%08x a=%08x b=%08x len=%08x pc=%08x seq=%u\n",
                        wd_lx_ring[k][0], wd_lx_ring[k][1], wd_lx_ring[k][2],
                        wd_lx_ring[k][3], wd_lx_ring[k][4], wd_lx_ring[k][5]);
            }
            fclose(f);
        }
        wd_full_dump(WD_FILES_DIR "iplram_lowclobber.bin", pc);
        wd_low_n++;
    }
}

static void* wd_thread(void* arg)
{
    (void)arg;
    uint64_t last = wd_hb;
    struct timespec t0, t;
    int force_dumps = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;)
    {
        clock_gettime(CLOCK_MONOTONIC, &t);
        /* Arm the host-PC sampler once the 64DD route is actually live
           (dd.idisk is only valid after init_device). */
        if (!wd_pcs_armed && g_dev.dd.idisk != NULL) wd_pcs_arm();

        /* Round 8: on-demand dumps from THIS thread.  The original force-flag
           check lived in dynarec_sample_hook, which stops running exactly when
           the CPU is starved by the RSP -- i.e. precisely when a dump is
           wanted (measured: wd_c_sample frozen while the RSP owned 100% of the
           emulation thread).  The watchdog thread always gets scheduled, so it
           is the reliable place to poll the flag.  Capped so a stale flag
           cannot fill the flash. */
        if (force_dumps < 8 && wd_r4300 != NULL && g_mem_base != NULL
            && access(WD_FORCE_FLAG, F_OK) == 0) {
            uint32_t pc = 0;
            struct precomp_instr** pp = r4300_pc_struct(wd_r4300);
            force_dumps++;
            unlink(WD_FORCE_FLAG);
            if (pp != NULL && *pp != NULL) pc = (*pp)->addr;
            wd_full_dump(WD_FILES_DIR "iplram_force.bin", pc);
        }

        /* Round 35: the low-RDRAM canary.  Cheap (a 2 KiB memcmp) and it runs
           on the one thread that is always scheduled. */
        wd_low_check();

        uint64_t cur = wd_hb;
        if (cur != last) { last = cur; t0 = t; }
        else if ((t.tv_sec - t0.tv_sec) > WD_STALL_SECS) {
            if (!wd_dumped && wd_r4300 != NULL && g_mem_base != NULL) {
                wd_dumped = 1;
                uint32_t pc = 0;
                struct precomp_instr** pp = r4300_pc_struct(wd_r4300);
                if (pp != NULL && *pp != NULL) pc = (*pp)->addr;
                wd_full_dump(WD_FILES_DIR "iplram_wd.bin", pc);
            }
            /* Keep looping (round 8): the heartbeat can resume after a long
               RSP/CPU starvation and stall again, and the run is more useful
               with a live 1 Hz guest-VI/heartbeat series than with none. */
        }
        usleep(10 * 1000);
    }
    return NULL;
}

/* Executed-block trace (called from get_addr_ht in the dynarec).  DD route
   only: plain games pay a single pointer compare per block transition.
   Each entry records target vaddr + sp + ra so the corruption of the stack
   pointer / return address can be localized to a single block. */
void wd_pc_record(uint32_t vaddr)
{
    uint32_t idx, sp;
    int64_t* regs;
    if (g_dev.dd.idisk == NULL) return;
    idx = wd_pc_idx & (WD_PC_RING - 1);
    wd_pc_ring[idx] = vaddr;
    if (wd_r4300 != NULL) {
        regs = r4300_regs(wd_r4300);
        sp = (uint32_t)regs[29];
        wd_reg_ring[idx][0] = sp;                   /* sp */
        wd_reg_ring[idx][1] = (uint32_t)regs[31];   /* ra */
        /* Root-cause probe (2026-09-10): an odd $sp is never legal MIPS, and it
           is the *first* corruption in the F-Zero X EK boot failure -- the very
           next `lw ra,20(sp)` reads a byte-rotated word (rotl8 of the correct
           pointer) because mupen does not raise AdEL for the misaligned access,
           and `jr ra` then jumps into unmapped space.  Dump the ring the first
           time $sp goes odd so the offending block is identifiable. */
        if (!wd_spodd_dumped && (sp & 3) != 0) {
            wd_spodd_dumped = 1;
            wd_state_dump(WD_FILES_DIR "iplram_spodd.bin", "SPODD", wd_r4300, vaddr, -1);
        }
    }
    wd_pc_idx++;
}

/* Called from TLB_refill_exception (cp0.c) before the guest handler runs:
   dumps the first guest fault on the DD route. */
void wd_fault_hook(struct r4300_core* r4300, uint32_t vaddr, int w)
{
    if (wd_fault_dumped) return;
    wd_fault_dumped = 1;
    wd_state_dump(WD_FILES_DIR "iplram_fault.bin", "FAULT", r4300, vaddr, w);
}

void wd_attach(struct r4300_core* r4300)
{
    if (wd_r4300 == NULL) {
        wd_r4300 = r4300;
        /* Round 57: latch the CORE thread's tid here -- wd_attach() is called
           from run_r4300() on the emulation thread, so this is the thread whose
           CPU time the periodic probe has to attribute. */
        wd_core_tid = (pid_t)syscall(SYS_gettid);
        pthread_t t;
        if (pthread_create(&t, NULL, wd_thread, NULL) == 0) {
            pthread_detach(t);
        }
    }
}

/* Per-interrupt-check sample hook for the dynarec (called from do_interrupt
   in linkage_arm64.S): advances the watchdog heartbeat, detects a CPU spin
   (same pc region repeated), and performs the on-demand FULL-RDRAM force dump
   when files/wd_force.flag is present.  The force dump is the reliable
   trigger for a livelocked/cycling run that never hard-stalls. */
void dynarec_sample_hook(uint32_t pc)
{
    static uint64_t d_sample = 0;
    static uint32_t d_stall_pc = 0xffffffff;
    static int d_stall_count = 0;
    static int d_stall_dumped = 0;
    static int wd_force_dumped = 0;
    wd_c_sample++;
    /* DD-only boundary sample.  The diagnostic source performs its own gate. */
    if (g_dev.dd.idisk != NULL)
        n64dd_dispatch_diag_boundary(&g_dev.r4300, pc);
    wd_pc_ring[wd_pc_idx++ & (WD_PC_RING - 1)] = pc;
    /* Heartbeat: on the 64DD route advance it on EVERY sample.  The old
       `(d_sample & 0xFFFFF)==0` fired once per million calls, i.e. never
       at the observed 10-300 calls/s, so the stall thread always fired 4s
       after emulation start and every "stall" dump was really a boot
       dump.  Plain carts keep the old behavior (they never dump). */
    if (g_dev.dd.idisk != NULL) wd_hb++;
    else if ((d_sample & 0xFFFFF) == 0) wd_hb++;
    d_sample++;
    if (!wd_force_dumped && (d_sample & 0x1FFF) == 0 &&
        access(WD_FORCE_FLAG, F_OK) == 0 && g_mem_base != NULL && wd_r4300 != NULL) {
        wd_force_dumped = 1;
        unlink(WD_FORCE_FLAG);
        wd_full_dump(WD_FILES_DIR "iplram_force.bin", pc);
    }
    if ((d_sample & 0xFFFFFULL) == 0 && wd_r4300 != NULL) {
        uint32_t w = pc & 0xFFFFF000u;
        if (w == d_stall_pc) {
            d_stall_count++;
        } else {
            d_stall_pc = w;
            d_stall_count = 0;
        }
        if (d_stall_count > 40 && !d_stall_dumped) {
            d_stall_dumped = 1;
            wd_full_dump(WD_FILES_DIR "iplram_dump.bin", pc);
        }
    }
}

void run_cached_interpreter(struct r4300_core* r4300)
{
    wd_attach(r4300);
    while (!*r4300_stop(r4300))
    {
        /* DD-route heartbeat: dynarec_sample_hook only runs on the recompiler,
           so without this the CI (emumode=1) + 64DD run would false-dump. */
        if (g_dev.dd.idisk != NULL) wd_hb++;
#ifdef COMPARE_CORE
        if ((*r4300_pc_struct(r4300))->ops == cached_interp_FIN_BLOCK && ((*r4300_pc_struct(r4300))->addr < 0x80000000 || (*r4300_pc_struct(r4300))->addr >= 0xc0000000))
            virtual_to_physical_address(r4300, (*r4300_pc_struct(r4300))->addr, 2);
        CoreCompareCallback();
#endif
#ifdef DBG
        if (g_DebuggerActive) update_debugger((*r4300_pc_struct(r4300))->addr);
#endif
        {
            struct precomp_instr* pin = *r4300_pc_struct(r4300);
            uint32_t pc_here = pin->addr;
            if (g_dev.dd.idisk != NULL)
                n64dd_dispatch_diag_step(r4300, pc_here,
                                         N64DD_DISPATCH_DIAG_PRE);
            if (g_dev.dd.idisk != NULL && !wd_spodd2_dumped) {
                uint32_t idx = wd_i_idx & (WD_IRING - 1);
                wd_i_pc[idx] = pc_here;
                wd_i_op[idx] = wd_rdram32(pc_here);
                wd_i_sp[idx] = r4300_regs(r4300)[29];
                wd_i_idx = wd_i_idx + 1;
            }
            pin->ops();
            if (g_dev.dd.idisk != NULL)
                n64dd_dispatch_diag_step(r4300, pc_here,
                                         N64DD_DISPATCH_DIAG_POST);
            if (g_dev.dd.idisk != NULL && !wd_spodd2_dumped) {
                if (r4300_regs(r4300)[29] & 3) {
                    wd_spodd2_dumped = 1;
                    wd_spodd2_dump(r4300, pc_here, wd_rdram32(pc_here));
                }
            }
        }
    }
}
