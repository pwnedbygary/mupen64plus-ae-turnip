#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "api/callbacks.h"
#include "device/dd/boot_policy.h"

static unsigned count;
static unsigned limits;
static unsigned traces;
static unsigned progress_traces;
static unsigned ordinal_256;
static unsigned bm_traces;
static unsigned bm_ordinal;
static unsigned bm_bad_ordinals;
static unsigned pi_dma_traces;
static unsigned pi_boundary_traces;
static unsigned context_traces;
static void capture(void *context, int level, const char *message)
{
    const char *ordinal;

    (void)context;
    (void)level;
    ++count;
    if (strstr(message, "limit reached"))
        ++limits;
    if (strstr(message, "DDSTART3"))
        ++traces;
    if (strstr(message, "DDSTART3 progress")) {
        ++progress_traces;
        if (strstr(message, "ordinal=256"))
            ++ordinal_256;
    }
    if (strstr(message, "DDSTART3 PI DMA start"))
        ++pi_dma_traces;
    if (strstr(message, "DDSTART3 PI boundary"))
        ++pi_boundary_traces;
    if (strstr(message, "DDSTART4 BM")) {
        ++bm_traces;
        ordinal = strstr(message, "ordinal=");
        if (ordinal == NULL || strtoul(ordinal + 8, NULL, 10) != bm_ordinal + 1)
            ++bm_bad_ordinals;
        else
            ++bm_ordinal;
    }
    if (strstr(message, "DDSTART5 context"))
        ++context_traces;
}

int main(void)
{
    assert(!dd_combo_cart_boot(NULL, 16, 4));
    assert(!dd_combo_cart_boot("0", 16, 4));
    assert(!dd_combo_cart_boot("10", 16, 4));
    assert(!dd_combo_cart_boot("", 16, 4));
    assert(!dd_combo_cart_boot("1", 0, 4));
    assert(!dd_combo_cart_boot("1", 16, 0));
    assert(dd_combo_cart_boot("1", 16, 4));
    /* DD off retains all baseline levels, with no cap. */
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(capture, NULL);
    assert(!DdStartupDiagnosticsEnabled());
    for (unsigned i = 0; i < 300; ++i)
        DebugMessage(M64MSG_VERBOSE, "plain %u", i);
    assert(count == 300);

    /* DD on emits native identity, filters before formatting and caps output. */
    count = 0;
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "1", 1);
    SetDebugCallback(capture, NULL);
    assert(DdStartupDiagnosticsEnabled());
    assert(count == 1);
    DebugMessage(M64MSG_VERBOSE, "ignored");
    DebugMessage(M64MSG_STATUS, "ignored");
    assert(count == 1);
    for (unsigned i = 0; i < 1000; ++i)
        DebugMessage(M64MSG_INFO, "dd %u", i);
    assert(count == 256 && limits == 1);

    /*
     * The independent DDSTART3 budget remains available after the DDSTART1
     * DebugMessage cap is exhausted.  Sparse reads emit the first eight and
     * power-of-two milestones (16, 32, 64).
     */
    DdStartupDiagnosticsTrace(DD_TRACE_REGISTER_READ, DD_TRACE_SPARSE, "DDSTART3 read");
    for (unsigned i = 1; i < 64; ++i)
        DdStartupDiagnosticsTrace(DD_TRACE_REGISTER_READ, DD_TRACE_SPARSE, "DDSTART3 read");
    assert(count == 267 && traces == 11);

    /*
     * PI boundaries cannot consume the separate DD DMA-start class.  The
     * DDSTART4 BM class is also independent and bounded at 512 records.
     */
    count = 0;
    traces = 0;
    limits = 0;
    bm_traces = 0;
    bm_ordinal = 0;
    bm_bad_ordinals = 0;
    pi_dma_traces = 0;
    pi_boundary_traces = 0;
    SetDebugCallback(capture, NULL);
    for (unsigned i = 0; i < 100; ++i) {
        DdStartupDiagnosticsTrace(DD_TRACE_REGISTER_COMMAND, DD_TRACE_EARLY, "DDSTART3 command");
        DdStartupDiagnosticsTrace(DD_TRACE_REGISTER_READ, DD_TRACE_EARLY, "DDSTART3 response");
        DdStartupDiagnosticsTrace(DD_TRACE_PI_DMA, DD_TRACE_EARLY,
            "DDSTART3 PI DMA start");
        DdStartupDiagnosticsTrace(DD_TRACE_PI_BOUNDARY, DD_TRACE_EARLY,
            "DDSTART3 PI boundary");
        DdStartupDiagnosticsTrace(DD_TRACE_INTERRUPT, DD_TRACE_EARLY, "DDSTART3 irq");
        DdStartupDiagnosticsTrace(DD_TRACE_PROGRESS, DD_TRACE_EARLY, "DDSTART3 progress");
    }
    for (unsigned i = 0; i < 600; ++i) {
        DdStartupDiagnosticsTrace(DD_TRACE_BM_HANDSHAKE, DD_TRACE_EARLY,
            "DDSTART4 BM observation");
    }
    assert(count == 629 && traces == 116);
    assert(bm_traces == 512 && bm_ordinal == 512 && bm_bad_ordinals == 0);
    assert(pi_dma_traces == 20 && pi_boundary_traces == 20);

    /* Progress samples are 1, then powers of four, not early power-of-two. */
    count = 0;
    traces = 0;
    progress_traces = 0;
    ordinal_256 = 0;
    bm_traces = 0;
    SetDebugCallback(capture, NULL);
    for (unsigned i = 0; i < 128; ++i)
        DdStartupDiagnosticsTrace(DD_TRACE_PROGRESS, DD_TRACE_PROGRESS_SPARSE,
            "DDSTART3 progress");
    assert(count == 5 && progress_traces == 4 && ordinal_256 == 0);
    for (unsigned i = 128; i < 256; ++i)
        DdStartupDiagnosticsTrace(DD_TRACE_PROGRESS, DD_TRACE_PROGRESS_SPARSE,
            "DDSTART3 progress");
    assert(count == 6 && progress_traces == 5 && ordinal_256 == 1);

    /* Disabled sessions cannot emit DDSTART3, including direct trace calls. */
    count = 0;
    traces = 0;
    progress_traces = 0;
    bm_traces = 0;
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(capture, NULL);
    DdStartupDiagnosticsTrace(DD_TRACE_PROGRESS, DD_TRACE_EARLY, "disabled");
    assert(count == 0 && traces == 0);

    /* Re-registration resets the budget; null callback disables all output. */
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "1", 1);
    count = 0;
    SetDebugCallback(capture, NULL);
    DebugMessage(M64MSG_ERROR, "error");
    DebugMessage(M64MSG_WARNING, "warning");
    assert(count == 3); /* identity + error + warning */
    SetDebugCallback(NULL, NULL);
    assert(!DdStartupDiagnosticsEnabled());
    DebugMessage(M64MSG_ERROR, "not delivered");
    assert(count == 3);

    /* A subsequent plain-cart session cannot inherit the DD filter. */
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(capture, NULL);
    DebugMessage(M64MSG_VERBOSE, "plain again");
    assert(count == 4);

    /*
     * DDSTART5 is host-gated, has an independent 24-record budget even
     * after DDSTART4 consumes the aggregate budget, and resets on callback
     * registration.
     */
    count = 0;
    context_traces = 0;
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(capture, NULL);
    DdStartupDiagnosticsTraceContext("DDSTART5 context: disabled");
    assert(count == 0 && context_traces == 0);

    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "1", 1);
    SetDebugCallback(capture, NULL);
    assert(count == 1); /* DDSTART1 identity */
    bm_ordinal = 0;
    bm_bad_ordinals = 0;
    for (unsigned i = 0; i < 600; ++i)
        DdStartupDiagnosticsTrace(DD_TRACE_BM_HANDSHAKE, DD_TRACE_EARLY,
            "DDSTART4 BM observation");
    for (unsigned i = 0; i < 40; ++i)
        DdStartupDiagnosticsTraceContext("DDSTART5 context: budget");
    assert(context_traces == 24 && count == 1 + 512 + 24);
    assert(bm_ordinal == 512 && bm_bad_ordinals == 0);

    count = 0;
    context_traces = 0;
    SetDebugCallback(capture, NULL);
    assert(count == 1); /* DDSTART1 identity after reset */
    DdStartupDiagnosticsTraceContext("DDSTART5 context: reset");
    assert(count == 2 && context_traces == 1);
    return 0;
}