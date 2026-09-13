/*
 * P03a: explicit DD runtime-policy core-state tests.
 *
 * Covers the core-side truth table of validation section 3 that is
 * host-testable: default-off, strict value validation, independence from
 * the debug callback and from the DD startup-diagnostics gate (G03/G05),
 * and the set/clear lifecycle used by the launch and ROM-close paths
 * (G06/G07).  The policy state must never enable diagnostics by itself.
 */
#include <assert.h>
#include <stdlib.h>
#include "api/callbacks.h"

static void capture(void *context, int level, const char *message)
{
    (void)context;
    (void)level;
    (void)message;
}

int main(void)
{
    /* Default is off: a plain session never gains corrected semantics. */
    assert(DdRuntimePolicyGet() == 0);

    /* Invalid values are rejected without changing the state. */
    assert(SetDdRuntimePolicy(-1) == M64ERR_INPUT_INVALID);
    assert(DdRuntimePolicyGet() == 0);
    assert(SetDdRuntimePolicy(2) == M64ERR_INPUT_INVALID);
    assert(DdRuntimePolicyGet() == 0);

    /* Explicit set/get cycle (G07: DD off -> DD on). */
    assert(SetDdRuntimePolicy(1) == M64ERR_SUCCESS);
    assert(DdRuntimePolicyGet() == 1);

    /* The policy is independent of the debug callback: a null callback
     * must not clear it (G05) and callback presence must not flip it
     * (G03).  DebugCallback re-registration also resets every diagnostics
     * budget; it must leave the policy untouched. */
    SetDebugCallback(NULL, NULL);
    assert(DdRuntimePolicyGet() == 1);
    SetDebugCallback(capture, NULL);
    assert(DdRuntimePolicyGet() == 1);

    /* Enabling the policy never enables diagnostics by itself. */
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(NULL, NULL);
    assert(DdRuntimePolicyGet() == 1);
    assert(!DdStartupDiagnosticsEnabled());
    SetDebugCallback(capture, NULL);
    assert(DdRuntimePolicyGet() == 1);
    assert(!DdStartupDiagnosticsEnabled());

    /* Conversely, diagnostics gate changes never move the policy. */
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "1", 1);
    SetDebugCallback(capture, NULL);
    assert(DdStartupDiagnosticsEnabled());
    assert(DdRuntimePolicyGet() == 1);
    setenv("M64P_DD_STARTUP_DIAGNOSTICS", "0", 1);
    SetDebugCallback(capture, NULL);
    assert(!DdStartupDiagnosticsEnabled());
    assert(DdRuntimePolicyGet() == 1);

    /* Clearing restores legacy semantics (ROM-close lifecycle). */
    assert(SetDdRuntimePolicy(0) == M64ERR_SUCCESS);
    assert(DdRuntimePolicyGet() == 0);

    /* A fresh off state can be enabled again for the next session. */
    assert(SetDdRuntimePolicy(1) == M64ERR_SUCCESS);
    assert(DdRuntimePolicyGet() == 1);
    assert(SetDdRuntimePolicy(0) == M64ERR_SUCCESS);
    assert(DdRuntimePolicyGet() == 0);

    return 0;
}
