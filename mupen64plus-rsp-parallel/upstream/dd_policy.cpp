#include "dd_policy.hpp"

#include <atomic>

namespace RSP
{

/*
 * The core sets this before the emulation thread runs or while it is
 * stopped (the command is rejected while the emulator is running), so an
 * atomic int is sufficient; DMA code reads it without writes racing.
 */
static std::atomic<int> dd_runtime_policy(0);

void DdRuntimePolicySet(int enabled)
{
	dd_runtime_policy.store(enabled ? 1 : 0, std::memory_order_release);
}

int DdRuntimePolicyEnabled()
{
	return dd_runtime_policy.load(std::memory_order_acquire);
}

} // namespace RSP
