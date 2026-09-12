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
static void capture(void *context, int level, const char *message)
{
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

    /* Each trace class has its own strict budget; aggregate output is <= 96. */
    count = 0;
    traces = 0;
    limits = 0;
    SetDebugCallback(capture, NULL);
    for (unsigned i = 0; i < 100; ++i) {
        DdStartupDiagnosticsTrace(DD_TRACE_REGISTER_COMMAND, DD_TRACE_EARLY, "DDSTART3 command");
        DdStartupDiagnosticsTrace(DD_TRACE_REGISTER_READ, DD_TRACE_EARLY, "DDSTART3 response");
        DdStartupDiagnosticsTrace(DD_TRACE_PI_DMA, DD_TRACE_EARLY, "DDSTART3 dma");
        DdStartupDiagnosticsTrace(DD_TRACE_INTERRUPT, DD_TRACE_EARLY, "DDSTART3 irq");
        DdStartupDiagnosticsTrace(DD_TRACE_PROGRESS, DD_TRACE_EARLY, "DDSTART3 progress");
    }
    assert(count == 97 && traces == 96);

    /* Progress samples are 1, then powers of four, not early power-of-two. */
    count = 0;
    traces = 0;
    progress_traces = 0;
    ordinal_256 = 0;
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
    return 0;
}