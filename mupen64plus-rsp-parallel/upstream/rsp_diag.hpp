#pragma once

#include <stdint.h>

namespace RSP
{
namespace Diagnostics
{
/*
 * The core supplies this callback only for an explicitly DD-enabled game.
 * The observer is called synchronously by the emulation thread; callback
 * registration and reset happen at plugin/ROM lifecycle boundaries before
 * that thread runs.
 */
using DebugCallback = void (*)(void *, int, const char *);

constexpr unsigned DMA_IMEM_SAMPLE_COUNT = 4;
constexpr unsigned DMA_TASK_WORD_COUNT = 16;
constexpr unsigned DMA_GPR_SNAPSHOT_COUNT = 32;
/*
 * P05 trigger snapshots are bounded independently of the ordinary DMA
 * record budget (repair plan section 7): at most four detailed snapshots of
 * the suspect request shape per session, with duplicates counted and
 * reported rather than silently suppressed.
 */
constexpr unsigned DMA_TRIGGER_SNAPSHOT_BUDGET = 4;
/* Version of the DmaReadObservation record contract emitted below. */
constexpr unsigned DMA_OBSERVATION_SCHEMA_VERSION = 2;

enum DmaTriggerReason : uint32_t
{
	DMA_TRIGGER_NONE = 0,
	/* Raw shape observed in DDSTART11: zero extracted size (length
	 * register 0xffffffff after the microcode decrement) with a zero
	 * 24-bit DRAM address.  Logging-only; never alters behavior. */
	DMA_TRIGGER_SUSPECT_REQUEST = 1,
	/* Legacy arm wrote IMEM from a DMEM-started transfer (the DDSTART11
	 * corruption shape; only possible under legacy policy). */
	DMA_TRIGGER_LEGACY_CROSSING = 2
};

enum DmaProbeSkipReason : uint32_t
{
	DMA_PROBE_SKIP_NONE = 0,
	/* Ordinary eligibility was not met: IMEM hash/range probes were not
	 * taken for this event (trigger snapshots still emit). */
	DMA_PROBE_SKIP_NOT_ELIGIBLE = 1
};

struct DmaReadObservation
{
	uint32_t raw_dma_cache;
	uint32_t raw_dma_dram;
	uint32_t raw_read_length;
	uint32_t requested_length;
	uint32_t aligned_length;
	uint32_t effective_length;
	uint32_t count;
	uint32_t transfer_count;
	uint32_t skip;
	uint64_t payload_hash;
	uint32_t payload_word_count;
	uint32_t payload_first[4];
	unsigned payload_first_count;
	uint32_t payload_last[4];
	unsigned payload_last_count;
	uint32_t source_first[4];
	unsigned source_first_count;
	uint32_t source_last[4];
	unsigned source_last_count;
	bool payload_samples_truncated;
	uint64_t imem_before_hash;
	uint64_t imem_after_hash;
	uint32_t imem_before_samples[DMA_IMEM_SAMPLE_COUNT];
	uint32_t imem_after_samples[DMA_IMEM_SAMPLE_COUNT];
	uint16_t first_imem_start;
	uint16_t first_imem_end;
	uint16_t imem_min_start;
	uint16_t imem_max_end;
	uint16_t last_imem_end;
	uint16_t imem_bank_mask;
	unsigned imem_range_count;
	unsigned imem_write_word_count;
	/*
	 * P05 evidence contract (schema version 2).  A captured event does not
	 * by itself mean IMEM was touched or that every probe succeeded: use
	 * trigger_reason / capture_eligible / probe_skipped /
	 * actual_imem_write_count to classify.  The GPR snapshot is taken at
	 * the MTC0 launch from the live guest registers (origin: exact, not
	 * reconstructed from memory) so the operands that produced the raw
	 * request survive any later DMEM overwrite.
	 */
	uint32_t schema_version;
	uint32_t policy_corrected;
	uint32_t trigger_reason;
	uint32_t capture_eligible;
	uint32_t probe_skipped;
	uint32_t probe_skip_reason;
	uint32_t actual_imem_write_count;
	uint32_t skip_effective;
	uint32_t dirty_blocks_after;
	uint32_t final_dma_cache;
	uint32_t final_dma_dram;
	uint32_t sp_pc;
	uint32_t gpr_snapshot[DMA_GPR_SNAPSHOT_COUNT];
	uint64_t entry_generation;
};

void set_callback(DebugCallback callback, void *context);
bool enabled();
bool imem_dma_eligible(uint32_t dest, uint32_t effective_length,
                       uint32_t transfer_count);

bool trace_jit(uintptr_t host_start, uintptr_t host_end,
               unsigned imem_start_pc, unsigned instruction_count,
               uint64_t existing_region_hash, const char *event);
bool trace_jit_range_unavailable(uintptr_t host_entry);
bool trace_jit_compile_words(uintptr_t host_start, unsigned imem_start_pc,
                             const uint32_t *words, unsigned word_count);

bool trace_rsp_entry(uint64_t task_hash, uint64_t imem_hash,
                     uint32_t task_type, uint32_t sp_pc,
                     const uint32_t *task_words = nullptr);
void trace_rsp_dma_read(const DmaReadObservation &observation);
void rsp_return();
/* Monotonic task-entry generation for stamping DMA observations. */
uint64_t entry_generation();
} // namespace Diagnostics
} // namespace RSP
