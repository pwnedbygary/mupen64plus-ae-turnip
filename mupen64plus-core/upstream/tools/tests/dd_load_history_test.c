/*
 * Host-only contract test for the bounded PI cart-to-RDRAM history.
 *
 * Example:
 *   cc -std=gnu11 -Wall -Wextra -Werror -I../../src \
 *      dd_load_history_test.c ../../src/device/dd/dd_load_history.c \
 *      -o dd_load_history_test && ./dd_load_history_test
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "device/dd/dd_load_history.h"
#include "osal/preproc.h"

static int diagnostics_enabled;
static int runtime_policy;
static unsigned int callback_lines;
static char callback_text[1024][512];

int DdStartupDiagnosticsEnabled(void)
{
    return diagnostics_enabled;
}

int DdRuntimePolicyGet(void)
{
    return runtime_policy;
}

static void test_callback(void *context, int level, const char *message)
{
    (void)context;
    (void)level;
    if (callback_lines < sizeof(callback_text) / sizeof(callback_text[0]))
        (void)snprintf(callback_text[callback_lines], sizeof(callback_text[0]),
            "%s", message);
    ++callback_lines;
}

ptr_DdStartupDiagnosticsCallback DdStartupDiagnosticsGetCallback(
    void **context)
{
    if (context != NULL)
        *context = NULL;
    return diagnostics_enabled && runtime_policy ? test_callback : NULL;
}

static void clear_callback(void)
{
    callback_lines = 0;
    memset(callback_text, 0, sizeof(callback_text));
}

static void write_guest_bytes(uint8_t *dram, size_t size, uint32_t address,
                              const uint8_t *bytes, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i)
    {
        uint64_t guest = (uint64_t)address + i;
        uint64_t host = guest ^ S8;
        assert(guest < size && host < size);
        dram[host] = bytes[i];
    }
}

static void test_endian_and_bounds(void)
{
    uint8_t dram[128];
    uint8_t before[32];
    uint8_t after[32];
    struct dd_load_history_dma transfer;
    unsigned int i;

    for (i = 0; i < sizeof(before); ++i)
        before[i] = (uint8_t)(0x80u + i);
    memset(dram, 0, sizeof(dram));
    write_guest_bytes(dram, sizeof(dram), 0x20, before, sizeof(before));

    diagnostics_enabled = 1;
    runtime_policy = 1;
    clear_callback();
    dd_load_history_reset();
    dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram), 0x06000000,
        0x20, 40);
    write_guest_bytes(dram, sizeof(dram), 0x20, before, sizeof(before));
    for (i = 0; i < sizeof(after); ++i)
        after[i] = (uint8_t)(0x20u + i);
    write_guest_bytes(dram, sizeof(dram), 0x20, after, sizeof(after));
    dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
    dd_load_history_flush("bounds-endian");

    assert(callback_lines == 2);
    assert(strstr(callback_text[0],
        "cart_addr=0x06000000 dram_addr=0x00000020") != NULL);
    assert(strstr(callback_text[0],
        "requested_length=0x00000028") != NULL);
    assert(strstr(callback_text[0],
        "before_guest_order=808182838485868788898a8b8c8d8e8f") != NULL);
    assert(strstr(callback_text[0],
        "after_guest_order=202122232425262728292a2b2c2d2e2f") != NULL);
    assert(strstr(callback_text[0], "sample_clipped=1") != NULL);
    assert(strstr(callback_text[1], "ring_wrapped=0") != NULL);

    clear_callback();
    dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram),
        0x06000000, 128, 16);
    dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
    dd_load_history_flush("out-of-bounds");
    assert(callback_lines == 2);
    assert(strstr(callback_text[0], "before_sample_length=0") != NULL);
    assert(strstr(callback_text[0], "after_sample_length=0") != NULL);
    assert(strstr(callback_text[0], "before_out_of_bounds=1") != NULL);
    assert(strstr(callback_text[0], "after_out_of_bounds=1") != NULL);
}

static void test_ring_wrap_and_gates(void)
{
    uint8_t dram[256];
    struct dd_load_history_dma transfer;
    unsigned int i;

    memset(dram, 0, sizeof(dram));
    clear_callback();
    dd_load_history_reset();
    for (i = 0; i < DD_LOAD_HISTORY_CAPACITY + 3; ++i)
    {
        dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram),
            0x06000000 + i, 0x20, 1);
        dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
    }
    dd_load_history_flush("wrap");
    assert(callback_lines == DD_LOAD_HISTORY_CAPACITY + 1);
    assert(strstr(callback_text[DD_LOAD_HISTORY_CAPACITY],
        "ring_wrapped=1") != NULL);
    assert(strstr(callback_text[DD_LOAD_HISTORY_CAPACITY],
        "overwritten=3") != NULL);

    clear_callback();
    runtime_policy = 0;
    dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram),
        0x06000000, 0x20, 1);
    dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
    dd_load_history_flush("policy-off");
    assert(callback_lines == 0);
    runtime_policy = 1;
    diagnostics_enabled = 0;
    dd_load_history_flush("diagnostics-off");
    assert(callback_lines == 0);
    diagnostics_enabled = 1;
}

static void test_reserved_zero_audio_flush(void)
{
    uint8_t dram[128];
    struct dd_load_history_dma transfer;
    unsigned int i;

    memset(dram, 0, sizeof(dram));
    diagnostics_enabled = 1;
    runtime_policy = 1;
    clear_callback();
    dd_load_history_reset();
    for (i = 0; i < DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT; ++i)
    {
        dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram),
            0x06000000, 0x20, 1);
        dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
        dd_load_history_flush(i & 1 ? "entry" : "call");
    }
    assert(callback_lines == DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT * 2);

    /*
     * An eighth early boundary is rejected without clearing the ring.  The
     * reserved zero-audio flush must still receive the completed history.
     */
    dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram),
        0x06000000, 0x20, 1);
    dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
    dd_load_history_flush("entry");
    assert(callback_lines == DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT * 2);

    /*
     * Overflow the ring before the mandatory zero-audio boundary.  All
     * retained records must be emitted; only ring-overwritten records count
     * as lost, never a shared line-budget suppression.
     */
    for (i = 0; i < DD_LOAD_HISTORY_CAPACITY + 2; ++i)
    {
        dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram),
            0x06000000 + i, 0x20, 1);
        dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
    }
    dd_load_history_flush("zero-audio");
    assert(callback_lines
        == DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT * 2
            + DD_LOAD_HISTORY_CAPACITY + 1);
    assert(strstr(callback_text[callback_lines - 1],
        "reason=zero-audio") != NULL);
    assert(strstr(callback_text[callback_lines - 1],
        "records_emitted=16 records_suppressed=0") != NULL);
    assert(strstr(callback_text[callback_lines - 1],
        "ring_wrapped=1 overwritten=3") != NULL);

    /*
     * Later zero-audio boundaries get one explicit bounded suppression
     * summary, then stay silent.  The first later call has one record.
     */
    dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram),
        0x06000000, 0x20, 1);
    dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
    dd_load_history_flush("zero-audio");
    assert(callback_lines
        == DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT * 2
            + DD_LOAD_HISTORY_CAPACITY + 2);
    assert(strstr(callback_text[callback_lines - 1],
        "history_flush_suppressed reason=zero-audio") != NULL);
    dd_load_history_flush("zero-audio");
    assert(callback_lines
        == DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT * 2
            + DD_LOAD_HISTORY_CAPACITY + 2);

    /*
     * Empty early flushes still consume only their seven accepted slots;
     * the separately reserved zero-audio flush must emit its empty summary.
     */
    clear_callback();
    dd_load_history_reset();
    for (i = 0; i < DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT; ++i)
        dd_load_history_flush("entry");
    assert(callback_lines == DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT);
    dd_load_history_flush("zero-audio");
    assert(callback_lines == DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT + 1);
    assert(strstr(callback_text[callback_lines - 1], "records=0") != NULL);
    assert(strstr(callback_text[callback_lines - 1],
        "records_emitted=0") != NULL);

    clear_callback();
    dd_load_history_reset();
    dd_load_history_pi_dma_begin(&transfer, dram, sizeof(dram),
        0x06000000, 0x20, 1);
    dd_load_history_pi_dma_complete(&transfer, dram, sizeof(dram));
    dd_load_history_flush("after-reset");
    assert(callback_lines == 2);
    assert(strstr(callback_text[0], "sequence=1") != NULL);
}

int main(void)
{
    test_endian_and_bounds();
    test_ring_wrap_and_gates();
    test_reserved_zero_audio_flush();
    puts("dd_load_history_test: ok");
    return 0;
}