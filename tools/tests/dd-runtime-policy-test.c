/*
 * P03a: explicit DD runtime-policy core-state tests.
 *
 * Covers the core-side truth table of validation section 3 that is
 * host-testable: default-off, strict value validation, independence from
 * the debug callback (G03/G05), and the set/clear lifecycle used by the
 * launch and ROM-close paths (G06/G07).
 */
#include <assert.h>
#include <stddef.h>
#include "api/callbacks.h"

_Static_assert(M64CMD_ROM_SET_SETTINGS == 27,
    "existing m64p_command values are ABI");
_Static_assert(M64CMD_DD_RUNTIME_POLICY_SET == 28,
    "DD runtime-policy command must remain appended");

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
     * (G03).  DebugCallback re-registration must leave the policy
     * untouched. */
    SetDebugCallback(NULL, NULL);
    assert(DdRuntimePolicyGet() == 1);
    SetDebugCallback(capture, NULL);
    assert(DdRuntimePolicyGet() == 1);

    /* Re-registering the callback cannot change the explicit policy. */
    SetDebugCallback(NULL, NULL);
    assert(DdRuntimePolicyGet() == 1);
    SetDebugCallback(capture, NULL);
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
