#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "api/callbacks.h"

static unsigned count;
static unsigned limits;
static void capture(void *context, int level, const char *message)
{
    (void)context;
    (void)level;
    ++count;
    if (strstr(message, "limit reached"))
        ++limits;
}

int main(void)
{
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

    /* Re-registration resets the budget; null callback disables all output. */
    count = 0;
    SetDebugCallback(capture, NULL);
    DebugMessage(M64MSG_ERROR, "error");
    DebugMessage(M64MSG_WARNING, "warning");
    assert(count == 3);
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