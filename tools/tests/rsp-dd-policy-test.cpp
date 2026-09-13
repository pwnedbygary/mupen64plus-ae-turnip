/*
 * P03b: DD runtime-policy receiver tests (Parallel-RSP plugin side).
 *
 * The core pushes the explicit per-game policy through the optional
 * DdRspRuntimePolicySet capability into RSP::DdRuntimePolicySet.  These
 * tests verify the receiver's readback contract and its independence from
 * the diagnostics observer (validation G03/G05/G10 receiver behavior).
 * The DMA path consuming this state arrives with the P04 correction; the
 * P02 transfer suite re-run proves DMA behavior is unchanged by P03.
 */
#include "dd_policy.hpp"
#include "rsp_diag.hpp"

static void noop(void *, int, const char *)
{
}

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

	/* Policy never enables diagnostics by itself (G05): the observer
	 * stays off without a callback even while the policy is enabled. */
	RSP::DdRuntimePolicySet(1);
	if (RSP::Diagnostics::enabled())
		return 1;
	RSP::Diagnostics::set_callback(nullptr, nullptr);
	if (RSP::Diagnostics::enabled())
		return 1;
	if (RSP::DdRuntimePolicyEnabled() != 1)
		return 1;

	/* Detach/ROM-close contract: the core pushes 0 and the receiver
	 * returns to legacy semantics. */
	RSP::DdRuntimePolicySet(0);
	if (RSP::DdRuntimePolicyEnabled() != 0)
		return 1;

	/* Diagnostics on with policy off (G03 receiver side): callback
	 * presence must not change the policy state. */
	RSP::Diagnostics::set_callback(&noop, nullptr);
	if (!RSP::Diagnostics::enabled())
		return 1;
	if (RSP::DdRuntimePolicyEnabled() != 0)
		return 1;
	RSP::Diagnostics::set_callback(nullptr, nullptr);
	if (RSP::Diagnostics::enabled())
		return 1;

	return 0;
}
