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
/*
 * P08b fetch-provenance budget: the audio command buffer is watched once a
 * task entry arms it, and a stuck microcode may re-read it indefinitely.
 * The first four watched fetches of each armed generation emit a detailed
 * record; later ones are counted as duplicates and reported once, so the
 * evidence stays bounded while the recurrence stays visible.
 */
constexpr unsigned DMA_FETCH_RECORD_BUDGET = 4;
/* Version of the DmaReadObservation record contract emitted below. */
constexpr unsigned DMA_OBSERVATION_SCHEMA_VERSION = 3;

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
	/*
	 * P08b fetch provenance (schema version 3).  fetch_watched marks a read
	 * that actually reads bytes inside the command buffer armed at audio
	 * task entry; fetch_generation is the entry generation that armed it.
	 * fetch_buffer_offset is the buffer-relative offset of the FIRST byte
	 * the transfer really reads inside the range, and fetch_first_row is the
	 * index of the first row that reaches into it — the transfer's row
	 * geometry (aligned_length, transfer_count, skip) is already in this
	 * record, so coverage is reconstructed from these fields rather than
	 * from a linearized length that would misreport rows separated by skip.
	 * The bytes actually read are carried by payload_hash / payload_first /
	 * source_first (captured for watched reads even when the destination is
	 * DMEM, which the IMEM-only eligibility would otherwise exclude).
	 */
	uint32_t fetch_watched;
	uint64_t fetch_generation;
	uint32_t fetch_buffer_offset;
	uint32_t fetch_first_row;
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

/*
 * P08b command-buffer watch (fetch provenance).
 *
 * arm_from_task() is called at RSP entry with the 16 task words captured
 * from DMEM.  It arms the watch ONLY when the per-game DD runtime policy is
 * enabled (P03) and the task is an audio task (type 2), taking the range
 * from words 12 (data_ptr, KSEG0-masked) and 13 (data_size) of the corrected
 * P07-C field map; it returns 1 when a range of nonzero length is armed and
 * 0 otherwise.  Arming is what makes fetch capture possible at all, so a
 * DD-disabled session never watches and never records.
 *
 * probe() classifies one read EXACTLY: a DMA read transfers `rows` spans of
 * `row_length` bytes starting at `start`, advanced by `row_length + skip`
 * between rows (there is no trailing skip).  It returns true when any of
 * those spans really reads a byte inside the armed range, writing the
 * buffer-relative offset of the first such byte and the index of the row
 * that reaches it.  This matters here because the observed freeze shape is a
 * multi-row read with a large skip: a single linear span would both miss
 * true intersections and claim offsets the transfer never read (review
 * counterexamples A and B, 2026-09-14).  Rows are bounded by the transfer
 * count (at most 256), so the probe is a bounded loop.
 *
 * watch_generation() is the entry generation that armed the current watch
 * (0 when unarmed); the fetch record carries it so a later reuse of the
 * buffer is not attributed to an earlier writer.  It is the ARM generation:
 * a watch armed at audio entry N stays armed through later non-audio entries
 * (they cannot re-arm), so a record stamped N may have been read while a
 * different task type was running.
 */
bool watch_arm_from_task(const uint32_t *task_words);
void watch_clear();
bool watch_armed();
bool watch_probe(uint32_t start, uint32_t row_length, uint32_t rows,
                 uint32_t skip, uint32_t *buffer_offset,
                 uint32_t *first_row);
uint64_t watch_generation();
} // namespace Diagnostics
} // namespace RSP
