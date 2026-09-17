/*
 * Explicit DD runtime-policy state for the Parallel-RSP plugin (P03).
 *
 * The core pushes the per-game launch policy here through the optional
 * DdRspRuntimePolicySet capability; see plugin.c.  This channel is the only
 * authority for corrected DMA semantics. It defaults to off (legacy
 * semantics), so DD-disabled sessions and old cores can never enable it by
 * accident.
 *
 * P03 establishes the channel and lifecycle only; the DMA path consults
 * this state starting with the P04 correction.
 */
#ifndef RSP_DD_POLICY_HPP__
#define RSP_DD_POLICY_HPP__

namespace RSP
{

/* Set the explicit DD runtime policy.  Any nonzero value means enabled. */
void DdRuntimePolicySet(int enabled);

/* Read the explicit DD runtime policy: 1 = corrected semantics authorized
 * for this explicitly DD-enabled session, 0 = legacy semantics. */
int DdRuntimePolicyEnabled();

} // namespace RSP

#endif /* RSP_DD_POLICY_HPP__ */
