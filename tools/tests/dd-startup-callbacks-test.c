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
static unsigned scheduler_traces;
static unsigned fault_traces;
static unsigned dynarec_fault_traces;
static unsigned dynarec_coherence_traces;
static unsigned dynarec_fault_ordinal;
static unsigned dynarec_coherence_ordinal;
static unsigned dynarec_bad_ordinals;
static unsigned dynarec_bad_prefixes;
static unsigned dynarec_bad_buffers;
static unsigned dynarec_max_message_length;
static void capture(void *context, int level, const char *message)
{
    const char *ordinal;
    size_t message_length;

    (void)context;
    (void)level;
    ++count;
    message_length = strlen(message);
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
    if (strstr(message, "DDSTART6 scheduler"))
        ++scheduler_traces;
    if (strstr(message, "DDSTART7 fault"))
        ++fault_traces;
    if (strstr(message, "DDSTART8 dynarec fault")) {
        ++dynarec_fault_traces;
        if (strncmp(message, "record=", 7) != 0)
            ++dynarec_bad_prefixes;
        ordinal = strstr(message, "record=");
        if (ordinal == NULL
                || strtoul(ordinal + 7, NULL, 10) != dynarec_fault_ordinal + 1)
            ++dynarec_bad_ordinals;
        else
            ++dynarec_fault_ordinal;
        if (message_length > dynarec_max_message_length)
            dynarec_max_message_length = (unsigned)message_length;
        if (message_length >= 512)
            ++dynarec_bad_buffers;
    }
    if (strstr(message, "DDSTART8 dynarec coherence")) {
        ++dynarec_coherence_traces;
        if (strncmp(message, "record=", 7) != 0)
            ++dynarec_bad_prefixes;
        ordinal = strstr(message, "record=");
        if (ordinal == NULL
                || strtoul(ordinal + 7, NULL, 10)
                    != dynarec_coherence_ordinal + 1)
            ++dynarec_bad_ordinals;
        else
            ++dynarec_coherence_ordinal;
        if (message_length > dynarec_max_message_length)
            dynarec_max_message_length = (unsigned)message_length;
        if (message_length >= 512)
            ++dynarec_bad_buffers;
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

    /* DDSTART6 is separately gated, bounded at 64, and reset per session. */
    count = 0;
    scheduler_traces = 0;
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(capture, NULL);
    DdStartupDiagnosticsTraceScheduler("DDSTART6 scheduler: disabled");
    assert(count == 0 && scheduler_traces == 0);

    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "1", 1);
    SetDebugCallback(capture, NULL);
    assert(count == 1); /* DDSTART1 identity */
    for (unsigned i = 0; i < 100; ++i)
        DdStartupDiagnosticsTrace(DD_TRACE_REGISTER_COMMAND, DD_TRACE_EARLY,
            "DDSTART3 command before scheduler");
    for (unsigned i = 0; i < 80; ++i)
        DdStartupDiagnosticsTraceScheduler("DDSTART6 scheduler: budget");
    assert(scheduler_traces == 64 && count == 1 + 24 + 64);

    count = 0;
    scheduler_traces = 0;
    SetDebugCallback(capture, NULL);
    assert(count == 1); /* identity after reset */
    DdStartupDiagnosticsTraceScheduler("DDSTART6 scheduler: reset");
    assert(count == 2 && scheduler_traces == 1);

    /* DDSTART7 is separately gated, bounded at 18, and reset per session. */
    count = 0;
    fault_traces = 0;
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(capture, NULL);
    DdStartupDiagnosticsTraceFault("DDSTART7 fault: disabled");
    assert(count == 0 && fault_traces == 0);

    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "1", 1);
    SetDebugCallback(capture, NULL);
    assert(count == 1); /* DDSTART1 identity */
    for (unsigned i = 0; i < 40; ++i)
        DdStartupDiagnosticsTraceFault("DDSTART7 fault: budget");
    assert(fault_traces == 18 && count == 1 + 18);

    count = 0;
    fault_traces = 0;
    SetDebugCallback(capture, NULL);
    assert(count == 1); /* identity after reset */
    DdStartupDiagnosticsTraceFault("DDSTART7 fault: reset");
    assert(count == 2 && fault_traces == 1);

    /*
     * DDSTART8 is independently gated and has separate fault (32) and
     * coherence (64) record classes.  Invalid calls do not consume either
     * class, and each emitted record has its own ordinal prefix.
     */
    count = 0;
    dynarec_fault_traces = 0;
    dynarec_coherence_traces = 0;
    dynarec_fault_ordinal = 0;
    dynarec_coherence_ordinal = 0;
    dynarec_bad_ordinals = 0;
    dynarec_bad_prefixes = 0;
    dynarec_bad_buffers = 0;
    dynarec_max_message_length = 0;
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(capture, NULL);
    assert(!DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_FAULT,
        "DDSTART8 dynarec fault: disabled"));
    assert(!DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_COHERENCE,
        "DDSTART8 dynarec coherence: disabled"));
    assert(count == 0);

    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "1", 1);
    SetDebugCallback(capture, NULL);
    assert(count == 1); /* identity */
    assert(!DdStartupDiagnosticsTraceDynarec(
        (enum dd_dynarec_trace_kind)DD_DYNAREC_TRACE_KIND_COUNT,
        "DDSTART8 dynarec fault: invalid kind"));
    assert(!DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_FAULT, NULL));
    assert(count == 1);
    for (unsigned i = 0; i < 32; ++i)
        assert(DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_FAULT,
            "DDSTART8 dynarec fault: exact cap"));
    assert(!DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_FAULT,
        "DDSTART8 dynarec fault: exhausted"));
    for (unsigned i = 0; i < 64; ++i)
        assert(DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_COHERENCE,
            "DDSTART8 dynarec coherence: exact cap"));
    assert(!DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_COHERENCE,
        "DDSTART8 dynarec coherence: exhausted"));
    assert(count == 1 + 32 + 64);
    assert(dynarec_fault_traces == 32 && dynarec_coherence_traces == 64);
    assert(dynarec_fault_ordinal == 32 && dynarec_coherence_ordinal == 64);
    assert(dynarec_bad_ordinals == 0 && dynarec_bad_prefixes == 0);

    /*
     * Exhausting the earlier aggregate trace budget does not affect either
     * DDSTART8 class.  They also remain independent from one another.
     */
    count = 0;
    traces = 0;
    bm_traces = 0;
    bm_ordinal = 0;
    bm_bad_ordinals = 0;
    dynarec_fault_traces = 0;
    dynarec_coherence_traces = 0;
    dynarec_fault_ordinal = 0;
    dynarec_coherence_ordinal = 0;
    dynarec_bad_ordinals = 0;
    dynarec_bad_prefixes = 0;
    dynarec_bad_buffers = 0;
    dynarec_max_message_length = 0;
    SetDebugCallback(capture, NULL);
    for (unsigned i = 0; i < 700; ++i)
        DdStartupDiagnosticsTrace(DD_TRACE_BM_HANDSHAKE, DD_TRACE_EARLY,
            "DDSTART4 BM observation before DDSTART8");
    assert(bm_traces == 512 && count == 1 + 512);
    assert(DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_FAULT,
        "DDSTART8 dynarec fault: after early cap"));
    assert(DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_COHERENCE,
        "DDSTART8 dynarec coherence: after early cap"));
    assert(count == 1 + 512 + 2);
    assert(dynarec_fault_traces == 1 && dynarec_coherence_traces == 1);
    assert(dynarec_fault_ordinal == 1 && dynarec_coherence_ordinal == 1);
    assert(dynarec_bad_ordinals == 0 && dynarec_bad_prefixes == 0);

    /*
     * A new callback registration resets both DDSTART8 remaining budgets
     * and both seen ordinals.  The callback buffer remains bounded at 512
     * bytes, including the record prefix.
     */
    {
        char long_message[1024];

        memset(long_message, 'x', sizeof(long_message) - 1);
        long_message[sizeof(long_message) - 1] = '\0';
        count = 0;
        dynarec_fault_traces = 0;
        dynarec_coherence_traces = 0;
        dynarec_fault_ordinal = 0;
        dynarec_coherence_ordinal = 0;
        dynarec_bad_ordinals = 0;
        dynarec_bad_prefixes = 0;
        dynarec_bad_buffers = 0;
        dynarec_max_message_length = 0;
        SetDebugCallback(capture, NULL);
        assert(count == 1); /* identity after reset */
        assert(DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_FAULT,
            "DDSTART8 dynarec fault: reset"));
        assert(DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_COHERENCE,
            "DDSTART8 dynarec coherence: reset"));
        assert(dynarec_fault_ordinal == 1 && dynarec_coherence_ordinal == 1);
        assert(DdStartupDiagnosticsTraceDynarec(DD_DYNAREC_FAULT, "%s",
            long_message));
        assert(dynarec_max_message_length < 512 && dynarec_bad_buffers == 0);
        assert(dynarec_bad_ordinals == 0 && dynarec_bad_prefixes == 0);
    }
    return 0;
}