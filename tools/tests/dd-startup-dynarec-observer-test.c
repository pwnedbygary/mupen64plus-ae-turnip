#define NEW_DYNAREC 4
#define ARCH_MIN_SSE 0

#include "device/device.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * The ARM64 source selects mprotect() for its cache, even though this HOST
 * fixture never calls new_dynarec_init().  WIN32 is defined only after the
 * core headers have been included so that the ARM64 cache-flush assembler is
 * omitted without changing the production source's selected architecture.
 */
struct device g_dev;
#define WIN32 1
typedef unsigned long DWORD;
typedef int BOOL;
#define MEM_RELEASE 0
static inline BOOL VirtualFree(void *address, size_t size, int type)
{
    (void)address;
    (void)size;
    (void)type;
    return 1;
}

/*
 * Include the production observer and its helpers.  The test links with
 * --gc-sections, retaining only the observer callbacks and their direct
 * reader dependencies; it does not reproduce any observer or decoder logic.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#define DD_CMD_WATCH_CODEGEN_TEST 1
#include "device/r4300/new_dynarec/new_dynarec.c"
#pragma GCC diagnostic pop

/*
 * The codegen fixture does not link the bounded writer module.  Its
 * DD_CMD_WATCH_CODEGEN_TEST build mode suppresses only the final call word;
 * this ABI sink documents the production target, while dd_cmd_watch_test.c
 * exercises the production recorder itself.
 */
void dd_cmd_watch_capture_context(uint32_t address, uint32_t store_pc,
                                  uint32_t flags, uint64_t ra, uint64_t sp,
                                  uint64_t a0, uint64_t a1, uint64_t a3)
{
    (void)address;
    (void)store_pc;
    (void)flags;
    (void)ra;
    (void)sp;
    (void)a0;
    (void)a1;
    (void)a3;
}

void dd_cmd_watch_capture_probe(
    const struct dd_cmd_watch_probe_snapshot *snapshot,
    int source_header_valid,
    uint32_t source_header_value)
{
    (void)snapshot;
    (void)source_header_valid;
    (void)source_header_value;
}

#define DRAM_BYTES UINT32_C(0x01000000)
#define MAX_MESSAGES 128

static uint32_t *dram;
static unsigned int message_count;
static char messages[MAX_MESSAGES][512];

static void capture(void *context, int level, const char *message)
{
    (void)context;
    (void)level;
    assert(message != NULL);
    if (message_count < MAX_MESSAGES)
        snprintf(messages[message_count], sizeof(messages[0]), "%s", message);
    ++message_count;
}

static unsigned int count_messages(const char *needle)
{
    unsigned int count = 0;
    unsigned int i;

    for (i = 0; i < message_count && i < MAX_MESSAGES; ++i)
        if (strstr(messages[i], needle) != NULL)
            ++count;
    return count;
}

static int has_message(const char *needle)
{
    return count_messages(needle) != 0;
}

static void clear_capture(void)
{
    message_count = 0;
    memset(messages, 0, sizeof(messages));
}

static void begin_session(int enabled)
{
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", enabled ? "1" : "0", 1);
    clear_capture();
    assert(SetDebugCallback(capture, NULL) == M64ERR_SUCCESS);
}

static void test_nonlink_continuation_gate(void)
{
    const int decision_index = 3;
    unsigned int offset;

    start = UINT32_C(0x800bb540);
    memset(ba, 0xff, sizeof(ba));
    /*
     * The decision index is the post-terminator scan point.  The existing
     * assembler contract consumes the architectural delay slot at that
     * boundary; this helper only decides whether to reopen the block.
     */
    begin_session(0);
    assert(message_count == 0);
    for (offset = 0; offset < 3; ++offset) {
        ba[1] = start + (decision_index + offset) * 4;
        assert(dd_dynarec_nonlink_block_continues(decision_index));
    }

    begin_session(1);
    for (offset = 0; offset < 3; ++offset) {
        ba[1] = start + (decision_index + offset) * 4;
        assert(!dd_dynarec_nonlink_block_continues(decision_index));
    }
    assert(message_count == 1); /* DDSTART1 identity only */

    begin_session(0);
    for (offset = 0; offset < 3; ++offset) {
        ba[1] = start + (decision_index + offset) * 4;
        assert(dd_dynarec_nonlink_block_continues(decision_index));
    }
    memset(ba, 0xff, sizeof(ba));
    assert(!dd_dynarec_nonlink_block_continues(decision_index));

    /* The callee at the excluded final word remains an external target. */
    slen = 5;
    memset(requires_32bit, 0, sizeof(requires_32bit));
    assert(internal_branch(0, start + 4));
    assert(!internal_branch(0, start + slen * 4 - 4));
    assert(!internal_branch(0, start + slen * 4));
}

static void test_external_store_regs_writeback(void)
{
    signed char regmap[HOST_REGS];
    uint32_t code[4] = { 0, 0, 0, 0 };
    uint32_t emitted;
    uint32_t expected;

    memset(regmap, -1, sizeof(regmap));
    regmap[0] = 1;
    start = UINT32_C(0x800bb540);
    slen = 5;
    out = (u_char *)code;
    store_regs_bt(regmap, 0, UINT64_C(1),
        start + slen * 4 - 4); /* internal_branch excludes this callee */
    assert(out == (u_char *)code + 4);
    memcpy(&emitted, code, sizeof(emitted));
    expected = UINT32_C(0xb9000000)
        | (u_int)(((fp_regs + 8) >> 2) << 10)
        | (u_int)(FP << 5);
    assert(emitted == expected);
}

static void test_context_codegen_uses_live_mappings(void)
{
    struct regstat context_regs;

    memset(&context_regs, 0, sizeof(context_regs));
    memset(context_regs.regmap, -1, sizeof(context_regs.regmap));
    context_regs.regmap[8] = 31; /* live ra low */
    context_regs.regmap[13] = 31 | 64; /* live ra high */
    context_regs.regmap[9] = 29; /* live sp low */
    context_regs.regmap[14] = 29 | 64; /* live sp high */
    context_regs.regmap[10] = 4; /* live a0 */
    context_regs.regmap[11] = 5; /* live a1 */
    context_regs.regmap[12] = 7; /* live a3 */
    context_regs.is32 = (UINT64_C(1) << 4)
        | (UINT64_C(1) << 5) | (UINT64_C(1) << 7);
    assert(emit_dd_cmd_watch_context_reg_valid(&context_regs, 31));
    assert(emit_dd_cmd_watch_context_reg_valid(&context_regs, 29));
    assert(emit_dd_cmd_watch_context_reg_valid(&context_regs, 4));
    assert(emit_dd_cmd_watch_context_reg_valid(&context_regs, 5));
    assert(emit_dd_cmd_watch_context_reg_valid(&context_regs, 7));
    assert(emit_dd_cmd_watch_context_direct_safe(&context_regs));
    context_regs.regmap[8] = -1;
    context_regs.regmap[3] = 31;
    assert(!emit_dd_cmd_watch_context_direct_safe(&context_regs));
    context_regs.regmap[3] = -1;
    context_regs.regmap[8] = 31;

    /* An unmaterialized, explicitly unneeded value must not be fabricated. */
    context_regs.regmap[11] = -1;
    context_regs.u |= UINT64_C(1) << 5;
    assert(!emit_dd_cmd_watch_context_reg_valid(&context_regs, 5));
    context_regs.regmap[11] = 5;
    context_regs.u &= ~(UINT64_C(1) << 5);
}

static int code_has_word(const uint32_t *code, unsigned int count,
                         uint32_t value, uint32_t mask)
{
    unsigned int i;

    for (i = 0; i < count; ++i)
        if ((code[i] & mask) == (value & mask))
            return 1;
    return 0;
}

static void test_context_codegen_machine_words(void)
{
    struct regstat context_regs;
    uint32_t code[256];
    unsigned int count;
    uint32_t mov_ra;
    uint32_t orr_ra;
    uint32_t mov_a0;
    uint32_t asr_a0;
    uint32_t orr_a0;
    uint32_t mov_const_a1;
    uint32_t flags;
    u_int protected_regs;

    memset(&context_regs, 0, sizeof(context_regs));
    memset(context_regs.regmap, -1, sizeof(context_regs.regmap));
    context_regs.regmap[8] = 31;
    context_regs.regmap[13] = 31 | 64;
    context_regs.regmap[9] = 29;
    context_regs.regmap[14] = 29 | 64;
    context_regs.regmap[10] = 4;
    context_regs.regmap[15] = 5;
    context_regs.regmap[16] = 5 | 64;
    context_regs.regmap[12] = 7;
    context_regs.is32 = UINT64_C(1) << 4;
    context_regs.isconst = UINT64_C(1) << 15;
    context_regs.constmap[15] = UINT64_C(0x12345678);
    context_regs.uu = UINT64_C(1) << 7;

    /*
     * This calls the production context emitter with its call instruction
     * disabled by DD_CMD_WATCH_CODEGEN_TEST.  The words are still the exact
     * argument/materialization sequence emitted before the production call.
     */
    memset(code, 0, sizeof(code));
    start = UINT32_C(0x80747278);
    out = (u_char *)code;
    emit_dd_cmd_watch_context(3, &context_regs, 14, 0, 1, 0);
    count = (unsigned int)(((u_char *)out - (u_char *)code) / sizeof(code[0]));

    mov_ra = UINT32_C(0x2a000000) | (8u << 16) | (31u << 5) | 3u;
    orr_ra = UINT32_C(0xaa000000) | (13u << 16) | (32u << 10)
        | (3u << 5) | 3u;
    mov_a0 = UINT32_C(0x2a000000) | (10u << 16) | (31u << 5) | 5u;
    asr_a0 = UINT32_C(0x13000000) | (31u << 16) | (31u << 10)
        | (5u << 5) | 30u;
    orr_a0 = UINT32_C(0xaa000000) | (30u << 16) | (32u << 10)
        | (5u << 5) | 5u;
    mov_const_a1 = UINT32_C(0x2a000000) | (15u << 16)
        | (31u << 5) | 6u;
    flags = DD_CMD_WATCH_CONTEXT_DELAY_SLOT
        | DD_CMD_WATCH_CONTEXT_VALID_RA
        | DD_CMD_WATCH_CONTEXT_VALID_SP
        | DD_CMD_WATCH_CONTEXT_VALID_A0
        | DD_CMD_WATCH_CONTEXT_VALID_A1;

    assert(code_has_word(code, count, mov_ra, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, orr_ra, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, mov_a0, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, asr_a0, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, orr_a0, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, mov_const_a1, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count,
        UINT32_C(0x52800000) | (flags << 5) | 2u,
        UINT32_C(0xffffffff)));
    assert(emit_dd_cmd_watch_context_reg_valid(&context_regs, 7) == 0);

    /* A link delay slot never reports ra as valid, even when mapped. */
    memset(code, 0, sizeof(code));
    out = (u_char *)code;
    emit_dd_cmd_watch_context(3, &context_regs, 14, 0, 1, 1);
    count = (unsigned int)(((u_char *)out - (u_char *)code) / sizeof(code[0]));
    flags &= ~DD_CMD_WATCH_CONTEXT_VALID_RA;
    assert(code_has_word(code, count,
        UINT32_C(0x52800000) | (flags << 5) | 2u,
        UINT32_C(0xffffffff)));

    /*
     * An overlapping allocator mapping must use the production spill path.
     * Save/restore words bracket the call setup, and the selected value is
     * loaded from the hot state only after the spill.
     */
    context_regs.regmap[8] = -1;
    context_regs.regmap[3] = 31;
    protected_regs = (1u << 3) | (1u << 8) | (1u << 10)
        | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15)
        | (1u << 16);
    memset(code, 0, sizeof(code));
    out = (u_char *)code;
    save_regs(protected_regs);
    emit_dd_cmd_watch_context(3, &context_regs, 14, protected_regs, 1, 0);
    restore_regs(protected_regs);
    count = (unsigned int)(((u_char *)out - (u_char *)code) / sizeof(code[0]));
    assert(count > 4);
    assert((code[0] & UINT32_C(0xffc00000)) == UINT32_C(0xa9000000));
    assert(code_has_word(code, count,
        UINT32_C(0xb9000000)
            | (u_int)(((fp_regs + (31u << 3)) >> 2) << 10)
            | (u_int)(FP << 5) | 3u,
        UINT32_C(0xffffffff)));
    assert((code[count - 1] & UINT32_C(0xffc00000))
        == UINT32_C(0xa9400000));
}

static void test_compiled_call_probe_machine_words(void)
{
    struct regstat probe_regs;
    uint32_t code[512];
    unsigned int count;
    unsigned int code_words_before_literal;
    size_t probe_base;
    uintptr_t snapshot_pointer;
    uint32_t call_pc_store;
    uint32_t delay_store;
    uint32_t flags_store;
    uint32_t a0_store;
    uint32_t t8_store;
    uint32_t t1_store;
    uint32_t ra_store;
    uint32_t sp_store;

    memset(&probe_regs, 0, sizeof(probe_regs));
    memset(probe_regs.regmap, -1, sizeof(probe_regs.regmap));
    /*
     * These are real allocator mappings, not ABI argument placeholders:
     * a0/a1/t8/t1/ra/sp each have live low/high halves in distinct hosts.
     */
    probe_regs.regmap[8] = 4;
    probe_regs.regmap[14] = 4 | 64;
    probe_regs.regmap[9] = 5;
    probe_regs.regmap[15] = 5 | 64;
    probe_regs.regmap[10] = 24;
    probe_regs.regmap[16] = 24 | 64;
    probe_regs.regmap[11] = 9;
    probe_regs.regmap[17] = 9 | 64;
    probe_regs.regmap[12] = 31;
    probe_regs.regmap[18] = 31 | 64;
    probe_regs.regmap[13] = 29;
    probe_regs.regmap[19] = 29 | 64;

    memset(code, 0, sizeof(code));
    out = (u_char *)code;
    emit_dd_cmd_watch_call_probe(&probe_regs,
        DD_CMD_WATCH_TARGET_CALL_PC,
        DD_CMD_WATCH_TARGET_CALL_OPCODE,
        DD_CMD_WATCH_TARGET_DELAY_OPCODE,
        DD_CMD_WATCH_TARGET_ENTRY_PC,
        UINT32_C(0x1234));
    count = (unsigned int)(((u_char *)out - (u_char *)code)
        / sizeof(code[0]));
    assert(count > 24);
    probe_base = offsetof(struct new_dynarec_hot_state, dd_cmd_watch_probe);
    call_pc_store = UINT32_C(0xb9000000)
        | (u_int)((((probe_base
            + offsetof(struct dd_cmd_watch_probe_snapshot, call_pc))
            >> 2) << 10))
        | (u_int)(FP << 5) | ARG1_REG;
    delay_store = UINT32_C(0xb9000000)
        | (u_int)((((probe_base
            + offsetof(struct dd_cmd_watch_probe_snapshot, delay_opcode))
            >> 2) << 10))
        | (u_int)(FP << 5) | ARG1_REG;
    flags_store = UINT32_C(0xb9000000)
        | (u_int)((((probe_base
            + offsetof(struct dd_cmd_watch_probe_snapshot, flags))
            >> 2) << 10))
        | (u_int)(FP << 5) | ARG1_REG;
    a0_store = UINT32_C(0xf9000000)
        | (u_int)((((probe_base
            + offsetof(struct dd_cmd_watch_probe_snapshot, a0))
            >> 3) << 10))
        | (u_int)(FP << 5) | ARG1_REG;
    t8_store = UINT32_C(0xf9000000)
        | (u_int)((((probe_base
            + offsetof(struct dd_cmd_watch_probe_snapshot, t8))
            >> 3) << 10))
        | (u_int)(FP << 5) | ARG1_REG;
    t1_store = UINT32_C(0xf9000000)
        | (u_int)((((probe_base
            + offsetof(struct dd_cmd_watch_probe_snapshot, t1))
            >> 3) << 10))
        | (u_int)(FP << 5) | ARG1_REG;
    ra_store = UINT32_C(0xf9000000)
        | (u_int)((((probe_base
            + offsetof(struct dd_cmd_watch_probe_snapshot, ra))
            >> 3) << 10))
        | (u_int)(FP << 5) | ARG1_REG;
    sp_store = UINT32_C(0xf9000000)
        | (u_int)((((probe_base
            + offsetof(struct dd_cmd_watch_probe_snapshot, sp))
            >> 3) << 10))
        | (u_int)(FP << 5) | ARG1_REG;
    assert((code[0] & UINT32_C(0xffc00000)) == UINT32_C(0xa9000000));
    assert(code_has_word(code, count, call_pc_store, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, delay_store, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, flags_store, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, a0_store, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, t8_store, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, t1_store, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, ra_store, UINT32_C(0xffffffff)));
    assert(code_has_word(code, count, sp_store, UINT32_C(0xffffffff)));
    /*
     * The snapshot pointer is a host address, not a guest word.  It must be
     * materialized with an LDR-literal and retained in the literal pool; a
     * 32-bit MOV-immediate would reproduce the Android crash on high ASLR
     * addresses when dd_dynarec_capture_probe() reads snapshot->kind.
     */
    assert(code_has_word(code, count, UINT32_C(0x58000000),
        UINT32_C(0xff00001f)));
    code_words_before_literal = count;
    /* x18 is the final odd caller-save mapping, so restore ends in ldr. */
    assert(code_has_word(code, code_words_before_literal,
        UINT32_C(0xa9400000), UINT32_C(0xffc00000)));
    assert(code_has_word(code, code_words_before_literal,
        UINT32_C(0xf9400000), UINT32_C(0xffc00000)));
    literal_pool(0);
    count = (unsigned int)(((u_char *)out - (u_char *)code)
        / sizeof(code[0]));
    assert(count >= 2);
    memcpy(&snapshot_pointer, &code[count - 2], sizeof(snapshot_pointer));
    assert(snapshot_pointer == (uintptr_t)
        &g_dev.r4300.new_dynarec_hot_state.dd_cmd_watch_probe);

    /* A source-word or target mismatch suppresses the probe entirely. */
    out = (u_char *)code;
    emit_dd_cmd_watch_call_probe(&probe_regs,
        DD_CMD_WATCH_TARGET_CALL_PC,
        DD_CMD_WATCH_TARGET_CALL_OPCODE ^ 1,
        DD_CMD_WATCH_TARGET_DELAY_OPCODE,
        DD_CMD_WATCH_TARGET_ENTRY_PC,
        UINT32_C(0x1234));
    assert(out == (u_char *)code);

    memset(code, 0, sizeof(code));
    out = (u_char *)code;
    emit_dd_cmd_watch_entry_probe(&probe_regs,
        DD_CMD_WATCH_TARGET_ENTRY_PC, UINT32_C(0x0c00a128),
        UINT32_C(0x1235));
    count = (unsigned int)(((u_char *)out - (u_char *)code)
        / sizeof(code[0]));
    assert(count > 18);
    assert(code_has_word(code, count, UINT32_C(0x58000000),
        UINT32_C(0xff00001f)));
    literal_pool(0);
    count = (unsigned int)(((u_char *)out - (u_char *)code)
        / sizeof(code[0]));
    assert(count >= 2);
    memcpy(&snapshot_pointer, &code[count - 2], sizeof(snapshot_pointer));
    assert(snapshot_pointer == (uintptr_t)
        &g_dev.r4300.new_dynarec_hot_state.dd_cmd_watch_probe);
    assert(code_has_word(code, count,
        UINT32_C(0x52800000) | DD_CMD_WATCH_PROBE_ENTRY << 5 | ARG1_REG,
        UINT32_C(0xffffffff)));
}

static void test_link_delay_slot_ra_fail_closed(void)
{
    memset(itype, 0, sizeof(itype));
    memset(rt1, 0, sizeof(rt1));

    itype[1] = UJUMP; /* JAL */
    rt1[1] = 31;
    assert(dd_dynarec_watch_link_delay_slot(2, 1));
    itype[1] = RJUMP; /* JALR r31 */
    assert(dd_dynarec_watch_link_delay_slot(2, 1));
    itype[1] = SJUMP; /* branch-and-link */
    assert(dd_dynarec_watch_link_delay_slot(2, 1));

    /* A known-zero loop BNE delay slot is not a link delay slot. */
    itype[1] = CJUMP;
    rt1[1] = 0;
    assert(!dd_dynarec_watch_link_delay_slot(2, 1));
    assert(!dd_dynarec_watch_link_delay_slot(2, 0));
}

static void test_pagespan_context_route_gate(void)
{
    /*
     * The production gate sees the architectural page-span marker before
     * start normalizes it away.  No route stub (and therefore no fabricated
     * delay-slot context) may be emitted for this compile.
     */
    dd_dynarec_pagespan_compile = 1;
    assert(!dd_dynarec_watch_route_allowed());
    dd_dynarec_pagespan_compile = 0;
}

static void test_entry_probe_internal_gap(void)
{
    assert(dd_dynarec_watch_entry_source_eligible(0,
        DD_CMD_WATCH_TARGET_ENTRY_PC));
    /*
     * A matching callee address inside an existing block is deliberately
     * not treated as an entry.  The post-delay callsite probe is the primary
     * executed-path observation; widening block generation is out of scope.
     */
    assert(!dd_dynarec_watch_entry_source_eligible(1,
        DD_CMD_WATCH_TARGET_ENTRY_PC));
    assert(!dd_dynarec_watch_entry_source_eligible(0,
        DD_CMD_WATCH_TARGET_ENTRY_PC + 4));
}

static void reset_observer_budgets(void)
{
    dd_dynarec_compile_generation = UINT32_C(0x100);
    dd_dynarec_fault_snapshots_remaining = 2;
    dd_dynarec_fault_filter_remaining = 2;
    dd_dynarec_coherence_compile_remaining = 16;
    dd_dynarec_coherence_verify_remaining = 24;
    dd_dynarec_coherence_invalidate_remaining = 12;
    dd_dynarec_coherence_writer_remaining = 12;
}

static void guest_store(uint32_t address, uint32_t value)
{
    uint32_t physical;

    assert(dd_fault_guest_range(dram, DRAM_BYTES, address, 4, &physical));
    dram[physical / 4] = value;
}

static uint32_t guest_load(uint32_t address)
{
    uint32_t value;

    assert(dd_fault_guest_read_u32(dram, DRAM_BYTES, address, &value));
    return value;
}

static void seed_window(uint32_t center, uint32_t seed)
{
    unsigned int i;

    for (i = 0; i < 5; ++i)
        guest_store(center - 8 + i * 4, seed + i);
}

static void seed_fault_state(void)
{
    struct new_dynarec_hot_state *hot = &g_dev.r4300.new_dynarec_hot_state;
    unsigned int i;

    memset(hot, 0, sizeof(*hot));
    hot->cp0_regs[CP0_CAUSE_REG] = CP0_CAUSE_EXCCODE_TLBL | CP0_CAUSE_BD;
    hot->cp0_regs[CP0_EPC_REG] = UINT32_C(0x800ad4a8);
    hot->cp0_regs[CP0_BADVADDR_REG] = UINT32_C(0x079bb080);
    hot->cp0_regs[CP0_STATUS_REG] = UINT32_C(0xdead0041);
    hot->cp0_regs[CP0_ENTRYHI_REG] = UINT32_C(0x800bb000);
    hot->cp0_regs[CP0_CONTEXT_REG] = UINT32_C(0x12345000);
    hot->address = UINT32_C(0x079bb080);
    hot->lo = UINT64_C(0x1122334455667788);
    hot->hi = UINT64_C(0x8877665544332211);
    for (i = 0; i < 32; ++i)
        hot->regs[i] = UINT64_C(0x1000000000000000) + i;
    hot->regs[31] = UINT64_C(0x12345678800bb670);

    memset(dram, 0, DRAM_BYTES);
    seed_window(UINT32_C(0x800ad4ac), UINT32_C(0xa0000000));
    seed_window(UINT32_C(0x800bb648), UINT32_C(0xb0000000));
    seed_window(UINT32_C(0x800bb670), UINT32_C(0xc0000000));
    seed_window(UINT32_C(0x800bb67c), UINT32_C(0xd0000000));
    guest_store(UINT32_C(0x800ad4ac), UINT32_C(0x81234567));
}

static void run_fault_snapshot(uint32_t instruction, uint64_t ra)
{
    struct new_dynarec_hot_state *hot = &g_dev.r4300.new_dynarec_hot_state;

    hot->regs[31] = ra;
    dd_dynarec_fault_observer(UINT32_C(0x800ad4ac), instruction,
        UINT32_C(0x800bb540), dd_dynarec_compile_generation);
}

static void test_disabled_gate(void)
{
    struct ll_entry head;
    uint32_t copied[4] = { 1, 2, 3, 4 };
    uint32_t beforeword = 0;

    begin_session(0);
    reset_observer_budgets();
    /*
     * An invalid pointer makes a disabled observer's lack of direct-RDRAM
     * access observable: any read before the explicit gate would fault.
     */
    g_dev.rdram.dram = (uint32_t *)(uintptr_t)1;
    g_dev.rdram.dram_size = SIZE_MAX;
    dd_dynarec_fault_observer(UINT32_C(0x800ad4ac), 0, UINT32_C(0x800bb540), 1);
    dd_dynarec_trace_compile(UINT32_C(0x800bb540), 4, 1, copied);
    memset(&head, 0, sizeof(head));
    head.start = UINT32_C(0x800bb540);
    head.length = sizeof(copied);
    head.copy = copied;
    dd_dynarec_trace_verify(&head, 0);
    dd_dynarec_trace_invalidate(UINT32_C(0x800bb), UINT32_C(0x0bb));
    assert(!dd_dynarec_prepare_byte_store(UINT32_C(0x800bb540), &beforeword));
    dd_dynarec_trace_byte_store(0, UINT32_C(0x800bb540), 0, 0, beforeword);
    assert(message_count == 0);
}

static void test_fault_observer(void)
{
    unsigned int fault_messages;

    begin_session(1);
    reset_observer_budgets();
    g_dev.rdram.dram = dram;
    g_dev.rdram.dram_size = DRAM_BYTES;
    seed_fault_state();

    /* Filter records do not consume either complete-snapshot reservation. */
    g_dev.r4300.new_dynarec_hot_state.cp0_regs[CP0_EPC_REG] =
        UINT32_C(0x80001000);
    g_dev.r4300.new_dynarec_hot_state.cp0_regs[CP0_BADVADDR_REG] =
        UINT32_C(0x00001234);
    dd_dynarec_fault_observer(UINT32_C(0x80001000), 0,
        UINT32_C(0x800bb540), 1);
    dd_dynarec_fault_observer(UINT32_C(0x80001000), 0,
        UINT32_C(0x800bb540), 1);

    seed_fault_state();
    run_fault_snapshot(UINT32_C(0x81234567),
        UINT64_C(0x12345678800bb670));
    run_fault_snapshot(UINT32_C(0xdeadbeef),
        UINT64_C(0x1234567881000000));

    fault_messages = count_messages("DDSTART8 fault");
    assert(message_count == 1 + 32); /* identity plus 2 filters and 2*15 */
    assert(fault_messages == 32);
    assert(count_messages("stage=filter") == 2);
    assert(count_messages("stage=pending_exception") == 2);
    assert(count_messages("stage=instruction") == 2);
    assert(count_messages("compare=match") == 1);
    assert(count_messages("compare=reject") == 1);
    assert(has_message("derived_fault_pc=800ad4ac"));
    assert(has_message("r31=12345678800bb670"));
    assert(has_message("lo=1122334455667788"));
    assert(has_message("hi=8877665544332211"));
    assert(has_message("stage=ra_window"));
    assert(has_message("stage=candidate_window"));
    assert(has_message("stage=ra_window center=81000000"));
    assert(has_message("stage=ra_window center=81000000"
                       " evidence=direct-rdram-unavailable"));
    assert(dd_dynarec_fault_snapshots_remaining == 0);
    assert(dd_dynarec_fault_filter_remaining == 0);
}

static void test_unavailable_fault_instruction(void)
{
    begin_session(1);
    reset_observer_budgets();
    seed_fault_state();
    g_dev.rdram.dram = dram;
    g_dev.rdram.dram_size = 4;
    run_fault_snapshot(UINT32_C(0x81234567),
        UINT64_C(0x12345678800bb670));
    assert(message_count == 1 + 15);
    assert(count_messages("stage=instruction") == 1);
    assert(has_message("current_source=unavailable"));
    assert(count_messages("evidence=direct-rdram-unavailable") == 4);
    assert(dd_dynarec_fault_snapshots_remaining == 1);
}

static void test_coherence_observers(void)
{
    struct ll_entry head;
    uint32_t copied[4];
    uint32_t beforeword;
    unsigned int i;

    begin_session(1);
    reset_observer_budgets();
    g_dev.rdram.dram = dram;
    g_dev.rdram.dram_size = DRAM_BYTES;
    seed_fault_state();
    for (i = 0; i < 4; ++i)
        copied[i] = guest_load(UINT32_C(0x800bb540) + i * 4);

    /* Compile exercises match, mismatch, unavailable, and its cap. */
    dd_dynarec_trace_compile(UINT32_C(0x800bb540), 4, 1, copied);
    guest_store(UINT32_C(0x800bb540), copied[0] ^ UINT32_C(0x00010000));
    dd_dynarec_trace_compile(UINT32_C(0x800bb540), 4, 2, copied);
    g_dev.rdram.dram_size = 4;
    dd_dynarec_trace_compile(UINT32_C(0x800bb540), 4, 3, copied);
    g_dev.rdram.dram_size = DRAM_BYTES;
    guest_store(UINT32_C(0x800bb540), copied[0]);
    for (i = 3; i < 17; ++i)
        dd_dynarec_trace_compile(UINT32_C(0x800bb540), 4, i, copied);
    assert(dd_dynarec_coherence_compile_remaining == 0);

    memset(&head, 0, sizeof(head));
    head.vaddr = UINT32_C(0x800bb540);
    head.start = UINT32_C(0x800bb540);
    head.length = sizeof(copied);
    head.copy = copied;
    dd_dynarec_trace_verify(&head, 0);
    guest_store(UINT32_C(0x800bb540), copied[0] ^ UINT32_C(0x00020000));
    dd_dynarec_trace_verify(&head, 1);
    g_dev.rdram.dram_size = 4;
    dd_dynarec_trace_verify(&head, 0);
    g_dev.rdram.dram_size = DRAM_BYTES;
    guest_store(UINT32_C(0x800bb540), copied[0]);
    for (i = 3; i < 26; ++i)
        dd_dynarec_trace_verify(&head, i & 1);
    assert(dd_dynarec_coherence_verify_remaining == 0);

    /* Nonmatching invalidations are filtered before the per-stage cap. */
    dd_dynarec_trace_invalidate(UINT32_C(0x800aa), UINT32_C(0x0aa));
    for (i = 0; i < 13; ++i)
        dd_dynarec_trace_invalidate(UINT32_C(0x800bb), UINT32_C(0x0bb));
    assert(dd_dynarec_coherence_invalidate_remaining == 0);

    for (i = 0; i < 12; ++i) {
        assert(dd_dynarec_prepare_byte_store(UINT32_C(0x800bb540), &beforeword));
        dram[(UINT32_C(0x800bb540) & UINT32_C(0x1fffffff)) / 4] =
            beforeword ^ (UINT32_C(1) << i);
        dd_dynarec_trace_byte_store((int)(UINT32_C(0x800bb600) + i * 4
                + (i & 1)), UINT32_C(0x800bb540), UINT32_C(0xab00), 8,
            beforeword);
    }
    assert(dd_dynarec_coherence_writer_remaining == 0);
    assert(!dd_dynarec_prepare_byte_store(UINT32_C(0x800bb540), &beforeword));

    assert(message_count == 1 + 64); /* identity plus four exact caps */
    assert(count_messages("stage=compile") == 16);
    assert(count_messages("stage=verify") == 24);
    assert(count_messages("stage=invalidate") == 12);
    assert(count_messages("stage=byte-store") == 12);
    assert(has_message("stage=compile generation=1"));
    assert(has_message("stage=compile generation=2"));
    assert(has_message("current=unavailable compare=reject"));
    assert(has_message("stage=verify result=match"));
    assert(has_message("stage=verify result=reject"));
    assert(has_message("stage=byte-store address=800bb540"));
    assert(has_message("writer_provenance=generated-write_byte_new-pcarg"));
    assert(has_message("delay_slot=1"));
}

int main(void)
{
    dram = calloc(1, DRAM_BYTES);
    assert(dram != NULL);

    test_nonlink_continuation_gate();
    test_external_store_regs_writeback();
    test_context_codegen_uses_live_mappings();
    test_context_codegen_machine_words();
    test_compiled_call_probe_machine_words();
    test_link_delay_slot_ra_fail_closed();
    test_pagespan_context_route_gate();
    test_entry_probe_internal_gap();
    test_disabled_gate();
    test_fault_observer();
    test_unavailable_fault_instruction();
    test_coherence_observers();

    free(dram);
    return 0;
}