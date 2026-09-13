#include "rsp_diag.hpp"

#include <atomic>
#include <stdio.h>
#include <string.h>

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
constexpr unsigned DMA_RECORD_BUDGET = 512;
constexpr int DIAGNOSTIC_INFO_LEVEL = 3; // M64MSG_INFO; M64MSG_ERROR is 1.

std::atomic<DebugCallback> g_callback(nullptr);
std::atomic<void *> g_context(nullptr);
std::atomic<bool> g_enabled(false);

std::atomic<unsigned> g_jit_remaining(0);
std::atomic<unsigned> g_jit_seen(0);
std::atomic<bool> g_jit_exhausted(false);

std::atomic<unsigned> g_jit_compile_words_remaining(0);
std::atomic<bool> g_jit_compile_words_exhausted(false);

std::atomic<unsigned> g_dma_remaining(0);
std::atomic<unsigned> g_dma_records(0);
std::atomic<unsigned long long> g_imem_dma_sequence(0);
std::atomic<bool> g_dma_exhausted(false);

/*
 * P05 trigger-snapshot budget: independent of the ordinary DMA record
 * budget so a suspect request stays observable even when the DDSTART11
 * budget is exhausted, and exempt from identity dedup so recurrences of
 * the same raw request are not silently swallowed.  Duplicates beyond the
 * budget are counted and reported (once at exhaustion, and in a session
 * summary when the diagnostics are re-registered or detached).
 */
std::atomic<unsigned> g_trigger_remaining(0);
std::atomic<unsigned> g_trigger_duplicates(0);
std::atomic<bool> g_trigger_exhausted(false);

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
uint32_t g_task_snapshot[DMA_TASK_WORD_COUNT] = {};
bool g_task_snapshot_valid = false;

struct DmaIdentity
{
	uint32_t raw_dma_cache;
	uint32_t raw_dma_dram;
	uint32_t raw_read_length;
	uint32_t count;
	uint32_t skip;
	uint64_t payload_hash;
	uint64_t imem_before_hash;
	uint64_t imem_after_hash;
};

DmaIdentity g_seen_dma_identities[DMA_RECORD_BUDGET];
unsigned g_seen_dma_identity_count = 0;

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

void emit_dd11_exhaustion(std::atomic<bool> &already_emitted,
                          const char *kind, unsigned limit)
{
	bool expected = false;
	if (already_emitted.compare_exchange_strong(expected, true,
	                                            std::memory_order_relaxed,
	                                            std::memory_order_relaxed))
	{
		char message[192];
		snprintf(message, sizeof(message),
		         "DDSTART11 RSP %s exhaustion: limit=%u", kind, limit);
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
	{
		/* Session summary for the P05 trigger budget, emitted while the
		 * previous callback (if any) is still registered. */
		const unsigned snapshots =
		    DMA_TRIGGER_SNAPSHOT_BUDGET - g_trigger_remaining.load(
		                                     std::memory_order_relaxed);
		const unsigned duplicates =
		    g_trigger_duplicates.load(std::memory_order_relaxed);
		if (snapshots != 0 || duplicates != 0)
		{
			char summary[160];
			snprintf(summary, sizeof(summary),
			         "DDSTART11 RSP trigger_summary snapshots=%u "
			         "duplicates=%u limit=%u",
			         snapshots, duplicates, DMA_TRIGGER_SNAPSHOT_BUDGET);
			emit(summary);
		}
	}
	g_callback.store(callback, std::memory_order_release);
	g_context.store(context, std::memory_order_release);
	g_enabled.store(callback != nullptr, std::memory_order_release);

	g_jit_remaining.store(JIT_RECORD_BUDGET, std::memory_order_relaxed);
	g_jit_seen.store(0, std::memory_order_relaxed);
	g_jit_exhausted.store(false, std::memory_order_relaxed);
	g_jit_compile_words_remaining.store(JIT_COMPILE_WORD_BUDGET,
	                                    std::memory_order_relaxed);
	g_jit_compile_words_exhausted.store(false, std::memory_order_relaxed);
	g_dma_remaining.store(DMA_RECORD_BUDGET, std::memory_order_relaxed);
	g_dma_records.store(0, std::memory_order_relaxed);
	g_imem_dma_sequence.store(0, std::memory_order_relaxed);
	g_dma_exhausted.store(false, std::memory_order_relaxed);
	g_trigger_remaining.store(DMA_TRIGGER_SNAPSHOT_BUDGET,
	                          std::memory_order_relaxed);
	g_trigger_duplicates.store(0, std::memory_order_relaxed);
	g_trigger_exhausted.store(false, std::memory_order_relaxed);
	g_entry_remaining.store(RSP_ENTRY_RECORD_BUDGET, std::memory_order_relaxed);
	g_entry_seen.store(0, std::memory_order_relaxed);
	g_entry_exhausted.store(false, std::memory_order_relaxed);
	g_entry_count.store(0, std::memory_order_relaxed);
	g_return_count.store(0, std::memory_order_relaxed);
	g_seen_rsp_identity_count = 0;
	g_task_snapshot_valid = false;
	memset(g_task_snapshot, 0, sizeof(g_task_snapshot));
	g_seen_dma_identity_count = 0;
}

bool enabled()
{
	return g_enabled.load(std::memory_order_acquire)
	    && g_callback.load(std::memory_order_acquire) != nullptr;
}

bool imem_dma_eligible(uint32_t dest, uint32_t effective_length,
                       uint32_t transfer_count)
{
	if (dest & 0x1000)
		return true;
	return static_cast<uint64_t>(effective_length) * transfer_count
	    > (0x1000 - (dest & 0xfff));
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
         "allocation_range=[0x%llx,0x%llx) imem_start_pc=0x%03x "
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
                     uint32_t task_type, uint32_t sp_pc,
                     const uint32_t *task_words)
{
	char message[512];
	unsigned ordinal;
	uint64_t entry_count;
	uint64_t return_count;

	if (!enabled())
		return false;

	entry_count = g_entry_count.fetch_add(1, std::memory_order_relaxed) + 1;
	if (task_words != nullptr)
	{
		memcpy(g_task_snapshot, task_words, sizeof(g_task_snapshot));
		g_task_snapshot_valid = true;
	}
	else
		g_task_snapshot_valid = false;
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

uint64_t entry_generation()
{
	return g_entry_count.load(std::memory_order_relaxed);
}

void trace_rsp_dma_read(const DmaReadObservation &observation)
{
	char message[4096];
	unsigned long long sequence;
	bool already_seen = false;

	if (!enabled())
		return;

	sequence = g_imem_dma_sequence.fetch_add(1, std::memory_order_relaxed) + 1;

	/*
	 * Trigger snapshots bypass the identity dedup (a recurring suspect
	 * request must stay observable) and use their own budget.  Duplicates
	 * beyond the budget are counted and reported once; they never emit a
	 * per-occurrence line, so logging stays bounded.
	 */
	if (observation.trigger_reason != DMA_TRIGGER_NONE)
	{
		if (!reserve(g_trigger_remaining))
		{
			g_trigger_duplicates.fetch_add(1, std::memory_order_relaxed);
			emit_dd11_exhaustion(g_trigger_exhausted, "trigger_budget",
			                     DMA_TRIGGER_SNAPSHOT_BUDGET);
			return;
		}
	}
	else
	{
	for (unsigned i = 0; i < g_seen_dma_identity_count; ++i)
	{
		const DmaIdentity &identity = g_seen_dma_identities[i];
		if (identity.raw_dma_cache == observation.raw_dma_cache
		    && identity.raw_dma_dram == observation.raw_dma_dram
		    && identity.raw_read_length == observation.raw_read_length
		    && identity.count == observation.count
		    && identity.skip == observation.skip
		    && identity.payload_hash == observation.payload_hash
		    && identity.imem_before_hash == observation.imem_before_hash
		    && identity.imem_after_hash == observation.imem_after_hash)
		{
			already_seen = true;
			break;
		}
	}
	if (already_seen)
		return;

	if (g_seen_dma_identity_count < DMA_RECORD_BUDGET)
	{
		g_seen_dma_identities[g_seen_dma_identity_count++] = {
			observation.raw_dma_cache,
			observation.raw_dma_dram,
			observation.raw_read_length,
			observation.count,
			observation.skip,
			observation.payload_hash,
			observation.imem_before_hash,
			observation.imem_after_hash
		};
	}

	if (!reserve(g_dma_remaining))
	{
		emit_dd11_exhaustion(g_dma_exhausted, "dma_read", DMA_RECORD_BUDGET);
		return;
	}
	}
	const unsigned record = g_dma_records.fetch_add(1, std::memory_order_relaxed) + 1;
	int length = snprintf(
	    message, sizeof(message),
	    "DDSTART11 RSP dma_read record=%u imem_dma_sequence=%llu entry_count=%llu "
	    "site_pc=unknown raw_dma_cache=0x%08x raw_dma_dram=0x%08x "
	    "raw_read_length=0x%08x requested_length=%u aligned_length=%u "
	    "effective_length=%u count=%u transfer_count=%u skip=%u "
	    "payload_hash=0x%016llx payload_word_count=%u "
	    "imem_before_hash=0x%016llx imem_after_hash=0x%016llx "
	    "imem_before_samples=[%08x,%08x,%08x,%08x] "
	    "imem_after_samples=[%08x,%08x,%08x,%08x] "
	    "first_imem_range=0x%03x-0x%03x imem_bank_mask=0x%x "
	    "imem_min=0x%03x imem_max=0x%03x imem_range_count=%u "
	    "imem_write_word_count=%u payload_samples_truncated=%u "
	    "schema=%u policy=%u trigger=%u capture_eligible=%u "
	    "probe_skipped=%u probe_skip_reason=%u actual_imem_writes=%u "
	    "skip_effective=%u dirty_after=0x%08x final_cache=0x%08x "
	    "final_dram=0x%08x entry_generation=%llu sp_pc=0x%04x "
	    "t9=0x%08x k0=0x%08x gprs=[",
	    record, sequence,
	    (unsigned long long)g_entry_count.load(std::memory_order_relaxed),
	    observation.raw_dma_cache, observation.raw_dma_dram,
	    observation.raw_read_length, observation.requested_length,
	    observation.aligned_length, observation.effective_length,
	    observation.count, observation.transfer_count, observation.skip,
	    (unsigned long long)observation.payload_hash,
	    observation.payload_word_count,
	    (unsigned long long)observation.imem_before_hash,
	    (unsigned long long)observation.imem_after_hash,
	    observation.imem_before_samples[0], observation.imem_before_samples[1],
	    observation.imem_before_samples[2], observation.imem_before_samples[3],
	    observation.imem_after_samples[0], observation.imem_after_samples[1],
	    observation.imem_after_samples[2], observation.imem_after_samples[3],
	    observation.first_imem_start, observation.first_imem_end,
	    observation.imem_bank_mask, observation.imem_min_start,
	    observation.imem_max_end, observation.imem_range_count,
	    observation.imem_write_word_count,
	    observation.payload_samples_truncated ? 1 : 0,
	    observation.schema_version,
	    observation.policy_corrected,
	    observation.trigger_reason,
	    observation.capture_eligible,
	    observation.probe_skipped,
	    observation.probe_skip_reason,
	    observation.actual_imem_write_count,
	    observation.skip_effective,
	    observation.dirty_blocks_after,
	    observation.final_dma_cache,
	    observation.final_dma_dram,
	    (unsigned long long)observation.entry_generation,
	    observation.sp_pc,
	    observation.gpr_snapshot[25],
	    observation.gpr_snapshot[26]);
	for (unsigned i = 0;
	     i < observation.payload_first_count && length < (int)sizeof(message);
	     ++i)
	{
		length += snprintf(message + length, sizeof(message) - length,
		                   "%s%08x", i == 0 ? "" : ",",
		                   observation.payload_first[i]);
	}
	length += snprintf(message + length, sizeof(message) - length,
	                   "] source_first=[");
	for (unsigned i = 0;
	     i < observation.source_first_count && length < (int)sizeof(message);
	     ++i)
	{
		length += snprintf(message + length, sizeof(message) - length,
		                   "%s0x%06x", i == 0 ? "" : ",",
		                   observation.source_first[i]);
	}
	length += snprintf(message + length, sizeof(message) - length,
	                   "] payload_last=[");
	for (unsigned i = 0;
	     i < observation.payload_last_count && length < (int)sizeof(message);
	     ++i)
	{
		length += snprintf(message + length, sizeof(message) - length,
		                   "%s%08x", i == 0 ? "" : ",",
		                   observation.payload_last[i]);
	}
	length += snprintf(message + length, sizeof(message) - length,
	                   "] source_last=[");
	for (unsigned i = 0;
	     i < observation.source_last_count && length < (int)sizeof(message);
	     ++i)
	{
		length += snprintf(message + length, sizeof(message) - length,
		                   "%s0x%06x", i == 0 ? "" : ",",
		                   observation.source_last[i]);
	}
	length += snprintf(message + length, sizeof(message) - length,
	                   "] task_snapshot_valid=%u task_words=[",
	                   g_task_snapshot_valid ? 1 : 0);
	for (unsigned i = 0;
	     i < DMA_TASK_WORD_COUNT && length < (int)sizeof(message);
	     ++i)
	{
		length += snprintf(message + length, sizeof(message) - length,
		                   "%s%08x", i == 0 ? "" : ",", g_task_snapshot[i]);
	}
	length += snprintf(message + length, sizeof(message) - length,
	                   "] gprs_full=[");
	for (unsigned i = 0;
	     i < DMA_GPR_SNAPSHOT_COUNT && length < (int)sizeof(message);
	     ++i)
	{
		length += snprintf(message + length, sizeof(message) - length,
		                   "%s%08x", i == 0 ? "" : ",",
		                   observation.gpr_snapshot[i]);
	}
	snprintf(message + (length < (int)sizeof(message) ? length : sizeof(message) - 1),
	         length < (int)sizeof(message) ? sizeof(message) - length : 1, "]");
	emit(message);
}

void rsp_return()
{
	if (enabled())
		g_return_count.fetch_add(1, std::memory_order_relaxed);
}
} // namespace Diagnostics
} // namespace RSP
