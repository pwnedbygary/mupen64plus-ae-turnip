#define NEW_DYNAREC 4
#define ARCH_MIN_SSE 0

#include "device/device.h"

#include <assert.h>
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
 * --gc-sections, retaining only the four callbacks and their direct reader
 * dependencies; it does not reproduce any observer or decoder logic.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "device/r4300/new_dynarec/new_dynarec.c"
#pragma GCC diagnostic pop

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

    test_disabled_gate();
    test_fault_observer();
    test_unavailable_fault_instruction();
    test_coherence_observers();

    free(dram);
    return 0;
}