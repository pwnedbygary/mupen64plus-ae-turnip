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
} // namespace Diagnostics
} // namespace RSP
