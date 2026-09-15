/*
 * Host-only contract test for the bounded DD command-buffer writer probe.
 *
 * Example:
 *   cc -std=gnu99 -I../../src dd_cmd_watch_test.c \
 *      ../../src/device/dd/dd_cmd_watch.c -o dd_cmd_watch_test
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "device/dd/dd_cmd_watch.h"

static int diagnostics_enabled;
static int runtime_policy;
static unsigned int callback_lines;
static char callback_last[2048];
static int callback_saw_sd;
static int callback_saw_pc;
static int callback_saw_context_first;
static int callback_saw_context_latest;
static int callback_saw_resize_latest;
static int callback_saw_context_provenance;
static int callback_saw_compiled_call;
static int callback_saw_compiled_entry;
static int callback_saw_header_unavailable;
static int callback_saw_partial_call;
static int callback_saw_raw_samples;
static int callback_saw_launch_snapshot;
static unsigned int load_history_reset_calls;
static unsigned int load_history_flush_calls;
static char load_history_last_reason[32];

static void test_callback(void *context, int level, const char *message);

int DdStartupDiagnosticsEnabled(void)
{
    return diagnostics_enabled;
}

int DdRuntimePolicyGet(void)
{
    return runtime_policy;
}

void dd_load_history_reset(void)
{
    ++load_history_reset_calls;
}

void dd_load_history_flush(const char *reason)
{
    ++load_history_flush_calls;
    (void)snprintf(load_history_last_reason,
        sizeof(load_history_last_reason), "%s", reason);
}

ptr_DdStartupDiagnosticsCallback DdStartupDiagnosticsGetCallback(void **context)
{
    if (context != NULL)
        *context = NULL;
    return diagnostics_enabled && runtime_policy ? test_callback : NULL;
}

static void test_callback(void *context, int level, const char *message)
{
    (void)context;
    (void)level;
    ++callback_lines;
    (void)snprintf(callback_last, sizeof(callback_last), "%s", message);
    if (strstr(message, "width=8") != NULL
            && strstr(message, "before=0x1122334455667788") != NULL
            && strstr(message, "after=0x8877665544332211") != NULL)
        callback_saw_sd = 1;
    if (strstr(message, "pc=0x80001ffc") != NULL
            && strstr(message, "delay_slot=1") != NULL)
        callback_saw_pc = 1;
    if (strstr(message, "store_context") != NULL
            && strstr(message, "context=first") != NULL
            && strstr(message, "store_pc=0x80747278") != NULL
            && strstr(message, "delay_slot=1") != NULL
            && strstr(message, "store_a0=0x0000000080747298") != NULL)
        callback_saw_context_first = 1;
    if (strstr(message, "store_context") != NULL
            && strstr(message, "context=latest") != NULL
            && strstr(message, "store_pc=0x80747298") != NULL
            && strstr(message, "store_a0=0x00000000807472b8") != NULL)
        callback_saw_context_latest = 1;
    if (strstr(message, "store_context") != NULL
            && strstr(message, "context=latest") != NULL
            && strstr(message, "store_pc=0x80747278") != NULL
            && strstr(message, "store_a0=0x0000000080747298") != NULL)
        callback_saw_resize_latest = 1;
    if (strstr(message, "context_provenance=generated-arm64-route-live-mapped-or-state")
            != NULL
            && strstr(message, "entry_arguments=not-inferred") != NULL)
        callback_saw_context_provenance = 1;
    if (strstr(message, "DDSTART15 load_clear_call") != NULL
            && strstr(message, "call_pc=0x800aea0c") != NULL
            && strstr(message, "call_opcode=0x0c1d1c90") != NULL
            && strstr(message, "delay_opcode=0x02002825") != NULL
            && strstr(message, "phase=after-delay") != NULL
            && strstr(message, "source_header_value=0x4d494f30") != NULL
            && strstr(message, "a1_provenance=compiled-delay-or-a1-s0") != NULL)
        callback_saw_compiled_call = 1;
    if (strstr(message, "DDSTART15 load_clear_call") != NULL
            && strstr(message, "header_reason=bounds-unavailable") != NULL
            && strstr(message, "rejection_reason=header-unavailable") != NULL)
        callback_saw_header_unavailable = 1;
    if (strstr(message, "DDSTART15 load_clear_call") != NULL
            && strstr(message, "valid_mask=0x0000007a") != NULL
            && strstr(message, "rejection_reason=missing-required-register")
                != NULL)
        callback_saw_partial_call = 1;
    if (strstr(message, "header_sample_valid_mask=0x000000ff") != NULL
            && strstr(message, "header_sample_raw=4d494f30,00000001")
                != NULL
            && strstr(message, "stack_sample_valid_mask=0x0000000f")
                != NULL
            && strstr(message, "stack_sample_raw=00000038,0000003c")
                != NULL)
        callback_saw_raw_samples = 1;
    if (strstr(message, "DDSTART14 RSP launch_snapshot") != NULL
            && strstr(message, "descriptor_hash=0x") != NULL
            && strstr(message, "descriptor_prefix={0x00000002") != NULL
            && strstr(message, "descriptor_suffix={0x00411910,0x000001a0")
                != NULL)
        callback_saw_launch_snapshot = 1;
    if (strstr(message, "DDSTART15 load_clear_entry") != NULL
            && strstr(message, "entry_pc=0x80747240") != NULL
            && strstr(message, "entry_opcode=0x0c00a128") != NULL
            && strstr(message, "ra=0xffffffff800aea14") != NULL
            && strstr(message, "original_arguments=callee-entry") != NULL)
        callback_saw_compiled_entry = 1;
}

static void task_words(uint32_t *words, uint32_t base, uint32_t size)
{
    memset(words, 0, 16 * sizeof(*words));
    words[0] = 2;
    words[12] = base;
    words[13] = size;
}

static void capture_context(uint32_t store_pc, uint32_t flags,
                            uint64_t a0)
{
    dd_cmd_watch_capture_context(UINT32_C(0x80411910), store_pc, flags,
        UINT64_C(0x1111111180000100), UINT64_C(0x2222222280000200),
        a0, UINT64_C(0x4444444480000400),
        UINT64_C(0x5555555580000500));
}

int main(void)
{
    uint32_t words[16];
    struct dd_cmd_watch_probe_snapshot probe;
    uint32_t header_sample[16] = {
        UINT32_C(0x4d494f30), UINT32_C(0x00000001), UINT32_C(0x00000002),
        UINT32_C(0x00000003), UINT32_C(0x00000004), UINT32_C(0x00000005),
        UINT32_C(0x00000006), UINT32_C(0x00000007), UINT32_C(0x00000008),
        UINT32_C(0x00000009), UINT32_C(0x0000000a), UINT32_C(0x0000000b),
        UINT32_C(0x0000000c), UINT32_C(0x0000000d), UINT32_C(0x0000000e),
        UINT32_C(0x0000000f)
    };
    uint32_t stack_sample[4] = {
        UINT32_C(0x00000038), UINT32_C(0x0000003c),
        UINT32_C(0x0000004c), UINT32_C(0x00000058)
    };
    unsigned int i;

    diagnostics_enabled = 1;
    runtime_policy = 1;
    dd_cmd_watch_reset();
    assert(load_history_reset_calls == 1);

    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 10, 1, 104);
    assert(!dd_cmd_watch_route_should_slow(UINT32_C(0x00411910), 4));
    assert(dd_cmd_watch_route_should_slow(UINT32_C(0x80411910), 4));
    assert(dd_cmd_watch_route_consume());
    assert(!dd_cmd_watch_route_consume());
    assert(!dd_cmd_watch_in_range(UINT32_C(0x00411910), 4));
    assert(dd_cmd_watch_in_range(UINT32_C(0x80411910), 4));

    /* Rolling storage keeps the last 32, and counts the eight overwritten. */
    for (i = 0; i < 40; ++i)
        dd_cmd_watch_record_aligned(UINT32_C(0x80411910) + i * 4, 4,
            i, i + 1, UINT32_C(0x80001000) + i * 4);

    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 11, 1, 0);
    assert(strcmp(load_history_last_reason, "zero-audio") == 0);
    assert(callback_saw_launch_snapshot);
    assert(callback_lines >= 33);
    assert(strstr(callback_last, "coverage=0x0000003f") != NULL);

    /* Non-audio entries do not tear down the preceding range. */
    memset(words, 0, sizeof(words));
    dd_cmd_watch_task_entry(words, 12, 1, 104);
    assert(dd_cmd_watch_in_range(UINT32_C(0x80411910), 4));

    /* The second range has an independent recent ring and SD metadata. */
    task_words(words, UINT32_C(0x004132d0), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 20, 1, 104);
    dd_cmd_watch_record_aligned(UINT32_C(0x804132d0), 8,
        UINT64_C(0x1122334455667788), UINT64_C(0x8877665544332211),
        UINT32_C(0x80002001));
    {
        unsigned int lines_before = callback_lines;
        task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
        dd_cmd_watch_task_entry(words, 21, 1, 0);
        assert(callback_lines >= lines_before + 1);
        assert(strstr(callback_last, "recent=0") != NULL);
    }
    assert(!callback_saw_sd);
    task_words(words, UINT32_C(0x004132d0), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 22, 1, 0);
    assert(callback_saw_sd);
    assert(callback_saw_pc);

    /* A third identity reports replacement loss instead of dropping it. */
    dd_cmd_watch_record_aligned(UINT32_C(0x804132d0), 4, 1, 2,
        UINT32_C(0x80003000));
    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 30, 1, 104);
    task_words(words, UINT32_C(0x00415000), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 31, 1, 104);
    task_words(words, UINT32_C(0x00415000), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 32, 1, 0);
    assert(strstr(callback_last, "replaced_events=1 replacements=1") != NULL);

    runtime_policy = 0;
    assert(!dd_cmd_watch_route_should_slow(UINT32_C(0x00411910), 4));
    assert(!dd_cmd_watch_in_range(UINT32_C(0x80411910), 4));
    runtime_policy = 1;
    assert(!dd_cmd_watch_in_range(UINT32_C(0x80411910), 4));

    /*
     * Context is attached only to a qualifying nonzero-to-zero transition.
     * The first and latest records are independent of the recent-event ring,
     * and the output explicitly labels the values as store-time registers.
     */
    dd_cmd_watch_reset();
    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 40, 1, 104);
    capture_context(UINT32_C(0x80747278),
        DD_CMD_WATCH_CONTEXT_DELAY_SLOT | DD_CMD_WATCH_CONTEXT_VALID_ALL,
        UINT64_C(0x0000000080747298));
    dd_cmd_watch_record_aligned(UINT32_C(0x80411910), 4, 1, 0,
        UINT32_C(0x80747279));
    capture_context(UINT32_C(0x80747298), DD_CMD_WATCH_CONTEXT_VALID_ALL,
        UINT64_C(0x00000000807472b8));
    dd_cmd_watch_record_aligned(UINT32_C(0x80411910), 4, 2, 0,
        UINT32_C(0x8074729c));
    capture_context(UINT32_C(0x807472a0), DD_CMD_WATCH_CONTEXT_VALID_ALL,
        UINT64_C(0x00000000807472c0));
    dd_cmd_watch_record_aligned(UINT32_C(0x80411910), 4, 0, 0,
        UINT32_C(0x807472a4));
    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 41, 1, 0);
    assert(callback_saw_context_first);
    assert(callback_saw_context_latest);
    assert(callback_saw_context_provenance);

    /*
     * A same-base size change carries the latest qualifying context only
     * when its address remains inside the new range.
     */
    callback_saw_context_first = 0;
    callback_saw_context_latest = 0;
    callback_saw_resize_latest = 0;
    callback_saw_context_provenance = 0;
    dd_cmd_watch_reset();
    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a0));
    dd_cmd_watch_task_entry(words, 50, 1, 104);
    capture_context(UINT32_C(0x80747278), DD_CMD_WATCH_CONTEXT_VALID_ALL,
        UINT64_C(0x0000000080747298));
    dd_cmd_watch_record_aligned(UINT32_C(0x80411910), 4, 3, 0,
        UINT32_C(0x8074727c));
    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a4));
    dd_cmd_watch_task_entry(words, 51, 1, 104);
    task_words(words, UINT32_C(0x00411910), UINT32_C(0x1a4));
    dd_cmd_watch_task_entry(words, 52, 1, 0);
    assert(callback_saw_resize_latest);

    /*
     * P08e host records require the exact compiled JAL/delay provenance,
     * coherent post-delay values, and a validated KSEG source header.  The
     * recorder must reject a source-word mutation rather than turning a
     * current-memory guess into an executed-call claim.
     */
    callback_saw_compiled_call = 0;
    callback_saw_compiled_entry = 0;
    dd_cmd_watch_reset();
    memset(&probe, 0, sizeof(probe));
    probe.call_pc = DD_CMD_WATCH_TARGET_CALL_PC;
    probe.call_opcode = DD_CMD_WATCH_TARGET_CALL_OPCODE;
    probe.delay_opcode = DD_CMD_WATCH_TARGET_DELAY_OPCODE;
    probe.target = DD_CMD_WATCH_TARGET_ENTRY_PC;
    probe.kind = DD_CMD_WATCH_PROBE_CALL;
    probe.flags = DD_CMD_WATCH_PROBE_AFTER_DELAY
        | DD_CMD_WATCH_PROBE_CALL_VALID;
    probe.generation = 71;
    probe.a0 = UINT64_C(0x0000000080411920);
    probe.a1 = UINT64_C(0x0000000080411ac0);
    probe.t8 = UINT64_C(0xffffffff80002000);
    probe.t1 = UINT64_C(0x000000004d494f30);
    probe.ra = UINT64_C(0xffffffff800aea14);
    probe.sp = UINT64_C(0xffffffff80796c10);
    dd_cmd_watch_capture_probe_samples(&probe, 1,
        UINT32_C(0x4d494f30), header_sample, UINT32_C(0xff),
        stack_sample, UINT32_C(0xf));
    assert(strcmp(load_history_last_reason, "call") == 0);
    assert(callback_saw_compiled_call);
    assert(callback_saw_raw_samples);

    {
        unsigned int lines_before = callback_lines;
        probe.delay_opcode ^= 1;
        dd_cmd_watch_capture_probe(&probe, 1, UINT32_C(0x4d494f30));
        assert(callback_lines == lines_before);
        probe.delay_opcode = DD_CMD_WATCH_TARGET_DELAY_OPCODE;
    }

    {
        unsigned int lines_before = callback_lines;
        unsigned int flushes_before = load_history_flush_calls;
        probe.ra ^= 1;
        dd_cmd_watch_capture_probe(&probe, 1, UINT32_C(0x4d494f30));
        assert(callback_lines == lines_before);
        assert(load_history_flush_calls == flushes_before);
        probe.ra = UINT64_C(0xffffffff800aea14);
    }

    {
        unsigned int lines_before = callback_lines;
        dd_cmd_watch_capture_probe(&probe, 0, 0);
        assert(callback_lines == lines_before + 1);
        assert(callback_saw_header_unavailable);
    }

    {
        unsigned int lines_before = callback_lines;
        probe.t8 = UINT64_C(0x0000000012345000);
        dd_cmd_watch_capture_probe(&probe, 1, UINT32_C(0x4d494f30));
        assert(callback_lines == lines_before + 1);
        assert(strstr(callback_last, "header_reason=invalid-t8") != NULL);
        probe.t8 = UINT64_C(0xffffffff80002000);
    }

    /*
     * Optional t1/t8 comparator values do not gate the trustworthy call
     * fields.  A missing required field is retained as a partial record with
     * its validity mask rather than being silently discarded.
     */
    {
        unsigned int lines_before = callback_lines;
        probe.flags = DD_CMD_WATCH_PROBE_AFTER_DELAY
            | DD_CMD_WATCH_PROBE_CALL_REQUIRED;
        dd_cmd_watch_capture_probe(&probe, 0, 0);
        assert(callback_lines == lines_before + 1);
        assert(strstr(callback_last, "valid_mask=0x00000066") != NULL);
        assert(strstr(callback_last,
            "rejection_reason=header-unavailable") != NULL);
        probe.flags = DD_CMD_WATCH_PROBE_AFTER_DELAY
            | DD_CMD_WATCH_PROBE_CALL_VALID;

        lines_before = callback_lines;
        probe.flags &= ~DD_CMD_WATCH_PROBE_VALID_A1;
        dd_cmd_watch_capture_probe(&probe, 1, UINT32_C(0x4d494f30));
        assert(callback_lines == lines_before + 1);
        assert(callback_saw_partial_call);
        assert(strstr(callback_last,
            "a1=0x0000000000000000 a1_valid=0") != NULL);

        /*
         * Validity bits are an information boundary: deliberately stale
         * values in all invalid fields must not appear in the record.
         */
        probe.flags = DD_CMD_WATCH_PROBE_AFTER_DELAY
            | DD_CMD_WATCH_PROBE_VALID_T8;
        probe.a0 = UINT64_C(0x1111111111111111);
        probe.a1 = UINT64_C(0x2222222222222222);
        probe.ra = UINT64_C(0x3333333333333333);
        probe.sp = UINT64_C(0x4444444444444444);
        dd_cmd_watch_capture_probe(&probe, 1, UINT32_C(0x4d494f30));
        assert(strstr(callback_last,
            "a0=0x0000000000000000 a0_valid=0") != NULL);
        assert(strstr(callback_last,
            "a1=0x0000000000000000 a1_valid=0") != NULL);
        assert(strstr(callback_last,
            "ra=0x0000000000000000 ra_valid=0") != NULL);
        assert(strstr(callback_last,
            "sp=0x0000000000000000 sp_valid=0") != NULL);
        probe.a0 = UINT64_C(0x0000000080411920);
        probe.a1 = UINT64_C(0x0000000080411ac0);
        probe.ra = UINT64_C(0xffffffff800aea14);
        probe.sp = UINT64_C(0xffffffff80796c10);
        probe.flags = DD_CMD_WATCH_PROBE_AFTER_DELAY
            | DD_CMD_WATCH_PROBE_CALL_VALID;
    }

    {
        unsigned int lines_before = callback_lines;
        diagnostics_enabled = 0;
        dd_cmd_watch_capture_probe(&probe, 1, UINT32_C(0x4d494f30));
        assert(callback_lines == lines_before);
        diagnostics_enabled = 1;
    }

    {
        unsigned int lines_before;
        unsigned int flushes_before;
        unsigned int n;

        dd_cmd_watch_reset();
        lines_before = callback_lines;
        flushes_before = load_history_flush_calls;
        for (n = 0; n < DD_CMD_WATCH_PROBE_BUDGET + 1; ++n)
            dd_cmd_watch_capture_probe(&probe, 1, UINT32_C(0x4d494f30));
        assert(callback_lines == lines_before + DD_CMD_WATCH_PROBE_BUDGET);
        assert(load_history_flush_calls == flushes_before
            + DD_CMD_WATCH_PROBE_BUDGET);

        /* Reset starts a fresh bounded session; it does not latch exhaustion. */
        dd_cmd_watch_reset();
        lines_before = callback_lines;
        flushes_before = load_history_flush_calls;
        dd_cmd_watch_capture_probe(&probe, 1, UINT32_C(0x4d494f30));
        assert(callback_lines == lines_before + 1);
        assert(load_history_flush_calls == flushes_before + 1);
    }

    {
        unsigned int lines_before;
        unsigned int n;

        dd_cmd_watch_reset();
        lines_before = callback_lines;
        for (n = 0; n < DD_CMD_WATCH_COMPILE_REJECTION_BUDGET + 1; ++n)
            dd_cmd_watch_note_compile_rejection(DD_CMD_WATCH_PROBE_CALL,
                DD_CMD_WATCH_PROBE_COMPILE_MISSING_REGISTER, 0);
        assert(callback_lines == lines_before
            + DD_CMD_WATCH_COMPILE_REJECTION_BUDGET);
        dd_cmd_watch_reset();
        lines_before = callback_lines;
        dd_cmd_watch_note_compile_rejection(DD_CMD_WATCH_PROBE_ENTRY,
            DD_CMD_WATCH_PROBE_COMPILE_PAGE_SPAN, 0);
        assert(callback_lines == lines_before + 1);
    }

    memset(&probe, 0, sizeof(probe));
    probe.call_pc = DD_CMD_WATCH_TARGET_CALL_PC;
    probe.call_opcode = DD_CMD_WATCH_TARGET_CALL_OPCODE;
    probe.delay_opcode = DD_CMD_WATCH_TARGET_DELAY_OPCODE;
    probe.target = DD_CMD_WATCH_TARGET_ENTRY_PC;
    probe.entry_pc = DD_CMD_WATCH_TARGET_ENTRY_PC;
    /* First word from the corrected P07 production RDRAM window. */
    probe.entry_opcode = UINT32_C(0x0c00a128);
    probe.kind = DD_CMD_WATCH_PROBE_ENTRY;
    probe.flags = DD_CMD_WATCH_PROBE_ENTRY_VALID;
    probe.generation = 72;
    probe.a0 = UINT64_C(0x0000000080411920);
    probe.a1 = UINT64_C(0x0000000080411ac0);
    probe.ra = UINT64_C(0xffffffff800aea14);
    probe.sp = UINT64_C(0xffffffff80796c10);
    probe.flags = DD_CMD_WATCH_PROBE_VALID_A0;
    dd_cmd_watch_capture_probe(&probe, 0, 0);
    assert(strstr(callback_last,
        "a1=0x0000000000000000 a1_valid=0") != NULL);
    assert(strstr(callback_last,
        "ra=0x0000000000000000 ra_valid=0") != NULL);
    assert(strstr(callback_last,
        "sp=0x0000000000000000 sp_valid=0") != NULL);
    probe.flags = DD_CMD_WATCH_PROBE_ENTRY_VALID;
    dd_cmd_watch_capture_probe(&probe, 0, 0);
    assert(callback_saw_compiled_entry);
    assert(strcmp(load_history_last_reason, "entry") == 0);

    return 0;
}