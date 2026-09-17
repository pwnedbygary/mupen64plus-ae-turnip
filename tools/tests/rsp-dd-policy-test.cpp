/*
 * P03b: DD runtime-policy receiver tests (Parallel-RSP plugin side).
 *
 * The core pushes the explicit per-game policy through the optional
 * DdRspRuntimePolicySet capability into RSP::DdRuntimePolicySet.  These
 * tests verify the receiver's readback contract. The DMA transfer fixture
 * independently verifies the legacy and corrected paths that consume it.
 */
#include "dd_policy.hpp"

int main()
{
	/* Default is off before any core push. */
	if (RSP::DdRuntimePolicyEnabled() != 0)
		return 1;

	/* Receiver readback: the core's push is observable. */
	RSP::DdRuntimePolicySet(1);
	if (RSP::DdRuntimePolicyEnabled() != 1)
		return 1;

	/* Any nonzero value enables; zero clears (documented contract). */
	RSP::DdRuntimePolicySet(-1);
	if (RSP::DdRuntimePolicyEnabled() != 1)
		return 1;
	RSP::DdRuntimePolicySet(0);
	if (RSP::DdRuntimePolicyEnabled() != 0)
		return 1;

	/* Policy state is retained until the explicit clear from the core. */
	RSP::DdRuntimePolicySet(1);
	if (RSP::DdRuntimePolicyEnabled() != 1)
		return 1;

	/* Detach/ROM-close contract: the core pushes 0 and the receiver
	 * returns to legacy semantics. */
	RSP::DdRuntimePolicySet(0);
	if (RSP::DdRuntimePolicyEnabled() != 0)
		return 1;

	return 0;
}
