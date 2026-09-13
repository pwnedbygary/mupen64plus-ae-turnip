#include "rsp_diag.hpp"

#include <atomic>
#include <stdio.h>

namespace RSP
{
namespace Diagnostics
{
namespace
{
constexpr unsigned JIT_RECORD_BUDGET = 2048;
constexpr unsigned RSP_ENTRY_RECORD_BUDGET = 128;
constexpr unsigned JIT_COMPILE_WORD_BUDGET = 32768;
constexpr unsigned JIT_COMPILE_WORDS_PER_LINE = 64;
constexpr int DIAGNOSTIC_INFO_LEVEL = 3; // M64MSG_INFO; M64MSG_ERROR is 1.

std::atomic<DebugCallback> g_callback(nullptr);
std::atomic<void *> g_context(nullptr);
std::atomic<bool> g_enabled(false);

std::atomic<unsigned> g_jit_remaining(0);
std::atomic<unsigned> g_jit_seen(0);
std::atomic<bool> g_jit_exhausted(false);

std::atomic<unsigned> g_jit_compile_words_remaining(0);
std::atomic<bool> g_jit_compile_words_exhausted(false);

std::atomic<unsigned> g_entry_remaining(0);
std::atomic<unsigned> g_entry_seen(0);
std::atomic<bool> g_entry_exhausted(false);
std::atomic<uint64_t> g_entry_count(0);
std::atomic<uint64_t> g_return_count(0);

struct RspIdentity
{
	uint64_t task_hash;
	uint64_t imem_hash;
};

RspIdentity g_seen_rsp_identities[RSP_ENTRY_RECORD_BUDGET];
unsigned g_seen_rsp_identity_count = 0;

bool reserve(std::atomic<unsigned> &remaining)
{
	unsigned available = remaining.load(std::memory_order_relaxed);
	do
	{
		if (available == 0)
			return false;
	} while (!remaining.compare_exchange_weak(available, available - 1,
	                                          std::memory_order_relaxed,
	                                          std::memory_order_relaxed));
	return true;
}

unsigned reserve_words(unsigned requested)
{
	unsigned available = g_jit_compile_words_remaining.load(
	    std::memory_order_relaxed);
	do
	{
		if (available == 0)
			return 0;
		if (requested > available)
			requested = available;
	} while (!g_jit_compile_words_remaining.compare_exchange_weak(
	    available, available - requested, std::memory_order_relaxed,
	    std::memory_order_relaxed));
	return requested;
}

bool seen_rsp_identity(uint64_t task_hash, uint64_t imem_hash)
{
	for (unsigned i = 0; i < g_seen_rsp_identity_count; ++i)
	{
		if (g_seen_rsp_identities[i].task_hash == task_hash
		    && g_seen_rsp_identities[i].imem_hash == imem_hash)
			return true;
	}

	if (g_seen_rsp_identity_count < RSP_ENTRY_RECORD_BUDGET)
	{
		g_seen_rsp_identities[g_seen_rsp_identity_count++] = {
			task_hash, imem_hash
		};
	}
	return false;
}

void emit(const char *message)
{
	DebugCallback callback = g_callback.load(std::memory_order_acquire);
	if (callback != nullptr && g_enabled.load(std::memory_order_acquire))
		callback(g_context.load(std::memory_order_acquire),
		         DIAGNOSTIC_INFO_LEVEL, message);
}

void emit_exhaustion(std::atomic<bool> &already_emitted, const char *kind,
                     unsigned limit)
{
	bool expected = false;
	if (already_emitted.compare_exchange_strong(expected, true,
	                                            std::memory_order_relaxed,
	                                            std::memory_order_relaxed))
	{
		char message[192];
		snprintf(message, sizeof(message),
		         "DDSTART10 %s exhaustion: limit=%u", kind, limit);
		emit(message);
	}
}
} // namespace

void set_callback(DebugCallback callback, void *context)
{
	/*
	 * Lifecycle setter contract: this is called before the emulation thread
	 * starts, or while it is stopped.  Atomics make accidental observer reads
	 * during teardown benign without adding a lock to DoRspCycles.
	 */
	g_callback.store(callback, std::memory_order_release);
	g_context.store(context, std::memory_order_release);
	g_enabled.store(callback != nullptr, std::memory_order_release);

	g_jit_remaining.store(JIT_RECORD_BUDGET, std::memory_order_relaxed);
	g_jit_seen.store(0, std::memory_order_relaxed);
	g_jit_exhausted.store(false, std::memory_order_relaxed);
	g_jit_compile_words_remaining.store(JIT_COMPILE_WORD_BUDGET,
	                                    std::memory_order_relaxed);
	g_jit_compile_words_exhausted.store(false, std::memory_order_relaxed);
	g_entry_remaining.store(RSP_ENTRY_RECORD_BUDGET, std::memory_order_relaxed);
	g_entry_seen.store(0, std::memory_order_relaxed);
	g_entry_exhausted.store(false, std::memory_order_relaxed);
	g_entry_count.store(0, std::memory_order_relaxed);
	g_return_count.store(0, std::memory_order_relaxed);
	g_seen_rsp_identity_count = 0;
}

bool enabled()
{
	return g_enabled.load(std::memory_order_acquire)
	    && g_callback.load(std::memory_order_acquire) != nullptr;
}

bool trace_jit(uintptr_t host_start, uintptr_t host_end,
               unsigned imem_start_pc, unsigned instruction_count,
               uint64_t existing_region_hash, const char *event)
{
	char message[512];
	unsigned ordinal;

	if (!enabled() || event == nullptr || host_start >= host_end
	    || imem_start_pc >= 0x1000 || instruction_count == 0
	    || instruction_count > (0x1000 - imem_start_pc) / 4)
		return false;

	if (!reserve(g_jit_remaining))
	{
		emit_exhaustion(g_jit_exhausted, "jit_region", JIT_RECORD_BUDGET);
		return false;
	}

	ordinal = g_jit_seen.fetch_add(1, std::memory_order_relaxed) + 1;
	snprintf(message, sizeof(message),
	         "DDSTART10 RSP jit_region %s record=%u "
	         "host_range=[0x%llx,0x%llx) imem_start_pc=0x%03x "
	         "instruction_count=%u existing_region_hash=0x%016llx",
	         event, ordinal, (unsigned long long)host_start,
	         (unsigned long long)host_end, imem_start_pc, instruction_count,
	         (unsigned long long)existing_region_hash);
	emit(message);
	return true;
}

bool trace_jit_range_unavailable(uintptr_t host_entry)
{
	char message[256];
	unsigned ordinal;

	if (!enabled() || host_entry == 0)
		return false;

	if (!reserve(g_jit_remaining))
	{
		emit_exhaustion(g_jit_exhausted, "jit_region", JIT_RECORD_BUDGET);
		return false;
	}

	ordinal = g_jit_seen.fetch_add(1, std::memory_order_relaxed) + 1;
	snprintf(message, sizeof(message),
	         "DDSTART10 RSP jit_region cache-range-unavailable "
	         "record=%u host_entry=0x%llx",
	         ordinal, (unsigned long long)host_entry);
	emit(message);
	return true;
}

bool trace_jit_compile_words(uintptr_t host_start, unsigned imem_start_pc,
                             const uint32_t *words, unsigned word_count)
{
	unsigned offset = 0;
	bool emitted = false;

	if (!enabled() || words == nullptr || host_start == 0
	    || imem_start_pc >= 0x1000 || word_count == 0
	    || word_count > (0x1000 - imem_start_pc) / 4)
		return false;

	while (offset < word_count)
	{
		unsigned chunk = word_count - offset;
		if (chunk > JIT_COMPILE_WORDS_PER_LINE)
			chunk = JIT_COMPILE_WORDS_PER_LINE;

		unsigned reserved = reserve_words(chunk);
		if (reserved == 0)
		{
			emit_exhaustion(g_jit_compile_words_exhausted,
			                "jit_compile_words", JIT_COMPILE_WORD_BUDGET);
			break;
		}
		chunk = reserved;

		char message[1024];
const unsigned chunk_imem_start_pc = imem_start_pc + offset * 4;
		int length = snprintf(
		    message, sizeof(message),
    "DDSTART10 RSP jit_compile_input region_host_start=0x%llx "
		    "imem_start_pc=0x%03x word_count=%u words=",
    (unsigned long long)host_start, chunk_imem_start_pc, chunk);
		for (unsigned i = 0; i < chunk && length < (int)sizeof(message); ++i)
		{
			length += snprintf(message + length, sizeof(message) - length,
			                   "%s%08x", i == 0 ? "" : " ",
			                   words[offset + i]);
		}
		emit(message);
		emitted = true;
		offset += chunk;
	}
	return emitted;
}

bool trace_rsp_entry(uint64_t task_hash, uint64_t imem_hash,
                     uint32_t task_type, uint32_t sp_pc)
{
	char message[512];
	unsigned ordinal;
	uint64_t entry_count;
	uint64_t return_count;

	if (!enabled())
		return false;

	entry_count = g_entry_count.fetch_add(1, std::memory_order_relaxed) + 1;
	const bool already_seen = seen_rsp_identity(task_hash, imem_hash);

	if (already_seen)
		return true;

	if (!reserve(g_entry_remaining))
	{
		emit_exhaustion(g_entry_exhausted, "rsp_entry",
		                RSP_ENTRY_RECORD_BUDGET);
		return true;
	}

	ordinal = g_entry_seen.fetch_add(1, std::memory_order_relaxed) + 1;
	return_count = g_return_count.load(std::memory_order_relaxed);
	snprintf(message, sizeof(message),
	         "DDSTART10 RSP entry sequence=%u entry_count=%llu "
	         "return_count=%llu task_type=0x%08x task_hash=0x%016llx "
	         "imem_hash=0x%016llx sp_pc=0x%08x",
	         ordinal, (unsigned long long)entry_count,
	         (unsigned long long)return_count, task_type,
	         (unsigned long long)task_hash, (unsigned long long)imem_hash,
	         sp_pc);
	emit(message);
	return true;
}

void rsp_return()
{
	if (enabled())
		g_return_count.fetch_add(1, std::memory_order_relaxed);
}
} // namespace Diagnostics
} // namespace RSP
