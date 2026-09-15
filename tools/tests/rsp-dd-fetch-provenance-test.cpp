/*
 * P08b: production-path fetch-provenance fixtures.
 *
 * These fixtures drive the ACTUAL production DMA read code through RSP_MTC0
 * (mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp, compiled here with
 * PARALLEL_INTEGRATION) and the real watch/emission code in rsp_diag.cpp,
 * exactly as the RSP JIT launches it.  No capture logic is duplicated into
 * the test.
 *
 * What is asserted: a read whose source intersects the command buffer armed
 * at audio task entry is recorded with the entry generation, the
 * buffer-relative offset of its first byte inside the range and the index of
 * the row that reaches it, and with the payload of the bytes actually read — including for
 * DMEM-destined reads, which the IMEM-only eligibility excludes.  The watch
 * cannot be armed without the per-game DD policy, an unarmed read is never
 * recorded as a fetch, the per-generation budget bounds the records and
 * counts duplicates, and the session summary reports both.  The payload
 * expectation is computed here from the RDRAM pattern the fixture installed,
 * with the FNV constants declared from the algorithm documented in
 * docs/DDSTART11_IMEM_DMA_PROVENANCE.md (not read from production code).
 *
 * Usage: rsp-dd-fetch-provenance-test
 */

#include "state.hpp"
#include "rsp_op.hpp"
#include "rsp_1.1.h"
#include "rsp_diag.hpp"
#include "dd_policy.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Externs referenced by cp0.cpp under PARALLEL_INTEGRATION (cp0.cpp:10-12).
namespace RSP
{
RSP_INFO rsp{};
short MFC0_count[32]{};
int SP_STATUS_TIMEOUT;
} // namespace RSP

namespace
{

constexpr unsigned kRdramWords = 0x800000u / 4; // 8 MiB bus mask backing.
constexpr uint32_t kModeContinue = 0;           // RSP::MODE_CONTINUE
constexpr uint32_t kModeCheckFlags = 4;         // RSP::MODE_CHECK_FLAGS

/* The multiply-then-xor FNV variant the record contract describes
 * (docs/DDSTART11_IMEM_DMA_PROVENANCE.md), declared here so the expectation
 * is computed independently of production code. */
constexpr uint64_t kFnvOffset = 0xCBF29CE484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001B3ull;

/* The P07 evidence command buffer: slot 0's data_ptr, size 0x1a0. */
constexpr uint32_t kBufferBase = 0x00411910u;
constexpr uint32_t kBufferSize = 0x1a0u;

/* Corrected raw read-length encoding: row bytes = ((raw & 0xfff) | 7) + 1,
 * rows = ((raw >> 12) & 0xff) + 1, skip = ((raw >> 20) & 0xfff) & 0xff8. */
uint32_t raw_read_length(uint32_t row_length, uint32_t rows, uint32_t skip)
{
	return ((skip & 0xff8u) << 20) | ((rows - 1u) << 12)
	       | ((row_length - 1u) & 7u);
}

int g_failures = 0;
std::vector<std::string> g_trace_lines;

void check(bool condition, const char *what)
{
	if (!condition)
	{
		fprintf(stderr, "FAIL: %s\n", what);
		++g_failures;
	}
}

void trace_capture(void *context, int level, const char *line)
{
	(void)context;
	(void)level;
	if (line != nullptr && g_trace_lines.size() < 64)
		g_trace_lines.emplace_back(line);
}

void rsp_dma_read(uint32_t raw_cache, uint32_t raw_dram, uint32_t raw_len);

uint32_t mix32(uint32_t x)
{
	x += 0x9e3779b9u;
	x ^= x >> 16;
	x *= 0x21f0aaadu;
	x ^= x >> 15;
	x *= 0x735a2d97u;
	x ^= x >> 15;
	return x;
}

uint32_t pattern_word(unsigned array_id, uint32_t word_index)
{
	return (array_id << 28) ^ mix32(word_index * 0x51ed270bu + array_id) ^
	       0x01234567u;
}

struct Guarded
{
	void *base = nullptr;
	size_t map_size = 0;
	void *payload = nullptr;

	void map(size_t bytes)
	{
		const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
		map_size = bytes + 2 * page;
		base = mmap(nullptr, map_size, PROT_NONE,
		            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (base == MAP_FAILED)
		{
			perror("mmap");
			exit(2);
		}
		void *start = static_cast<char *>(base) + page;
		if (mprotect(start, bytes, PROT_READ | PROT_WRITE) != 0)
		{
			perror("mprotect");
			exit(2);
		}
		payload = start;
	}

	~Guarded()
	{
		if (base != nullptr)
			munmap(base, map_size);
	}
};

struct State
{
	Guarded dmem_g, imem_g, rdram_g;
	RSP::CPUState cpu{};
	uint32_t cr[RSP::CP0_REGISTER_CMD_TMEM_BUSY + 1] = {};
	uint32_t irq = 0;

	void init()
	{
		dmem_g.map(DMEM_SIZE);
		imem_g.map(IMEM_SIZE);
		rdram_g.map(kRdramWords * 4);
		cpu.dmem = reinterpret_cast<uint32_t *>(dmem_g.payload);
		cpu.imem = reinterpret_cast<uint32_t *>(imem_g.payload);
		cpu.rdram = reinterpret_cast<uint32_t *>(rdram_g.payload);
		for (unsigned i = 0; i < sizeof(cr) / sizeof(cr[0]); ++i)
			cpu.cp0.cr[i] = &cr[i];
		cpu.cp0.irq = &irq;
		refill();
	}

	void refill()
	{
		for (uint32_t i = 0; i < kRdramWords; ++i)
			cpu.rdram[i] = pattern_word(3, i);
		for (uint32_t i = 0; i < DMEM_WORDS; ++i)
			cpu.dmem[i] = pattern_word(1, i);
		for (uint32_t i = 0; i < IMEM_WORDS; ++i)
			cpu.imem[i] = pattern_word(2, i);
		cpu.dirty_blocks = 0;
	}

	/* The 16 task words the core copies into DMEM at 0xfc0; words 12-13 are
	 * data_ptr / data_size per the corrected P07-C field map. */
	uint32_t task_words[16] = {};

	void make_audio_task(uint32_t data_ptr, uint32_t data_size)
	{
		memset(task_words, 0, sizeof(task_words));
		task_words[0] = 2; /* OSTask type 2 = audio */
		task_words[12] = data_ptr;
		task_words[13] = data_size;
	}
} g_state;

/* Production entry: the exact MTC0 sequence a guest uses to launch a read. */
void rsp_dma_read(uint32_t raw_cache, uint32_t raw_dram, uint32_t raw_len)
{
	g_state.cpu.sr[8] = raw_cache;
	RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_CACHE, 8);
	g_state.cpu.sr[8] = raw_dram;
	RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_DRAM, 8);
	g_state.cpu.sr[8] = raw_len;
	RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_READ_LENGTH, 8);
}

const std::string *find_line(const char *needle)
{
	for (const std::string &line : g_trace_lines)
	{
		if (line.find(needle) != std::string::npos)
			return &line;
	}
	return nullptr;
}

/* Each generation has its own fetch budget, so cases that must produce a
 * record arm a fresh one (distinct entry hashes so entry dedup never masks
 * the generation advance). */
uint64_t arm_fresh(uint64_t seq)
{
	RSP::Diagnostics::trace_rsp_entry(0x1000000000000000ull + seq,
	                                  0x2000000000000000ull + seq, 2u,
	                                  0x04001000u, g_state.task_words);
	check(RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "a fresh audio generation must arm the watch");
	g_trace_lines.clear();
	return RSP::Diagnostics::entry_generation();
}

unsigned count_lines(const char *needle)
{
	unsigned n = 0;
	for (const std::string &line : g_trace_lines)
	{
		if (line.find(needle) != std::string::npos)
			++n;
	}
	return n;
}

} // namespace

int main()
{
	g_state.init();

	/* 1. Without the per-game DD policy the watch cannot be armed and no
	 *    read is ever recorded as a fetch — the DD-disabled guarantee. */
	g_state.make_audio_task(0x80411910u, kBufferSize);
	g_trace_lines.clear();
	RSP::Diagnostics::set_callback(&trace_capture, nullptr);
	RSP::DdRuntimePolicySet(0);
	check(!RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "watch must not arm with the DD policy off");
	check(!RSP::Diagnostics::watch_armed(), "watch must not be armed");
	RSP::Diagnostics::watch_clear();
	rsp_dma_read(0x0000u, kBufferBase, 0x0000u); /* DMEM dest, 8 bytes */
	check(count_lines("fetch_watched=1") == 0,
	      "no fetch record may be emitted without an armed watch");

	/* 2. With the policy on, an audio task entry arms the buffer watch and
	 *    the entry generation is captured with it. */
	RSP::DdRuntimePolicySet(1);
	RSP::Diagnostics::trace_rsp_entry(0x1111222233334444ull, 0x5555666677778888ull,
	                                  2u, 0x04001000u, g_state.task_words);
	const uint64_t generation = RSP::Diagnostics::entry_generation();
	check(generation != 0, "entry generation must advance at task entry");
	check(RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "watch must arm for an audio task under the DD policy");
	check(RSP::Diagnostics::watch_armed(), "watch must report armed");
	check(RSP::Diagnostics::watch_generation() == generation,
	      "armed watch must carry the entry generation");

	/* 3. A DMEM-destined read inside the buffer is captured with its
	 *    generation, the offset of the first byte it reads inside the buffer,
	 *    the reaching row index and the payload actually read. */
	g_trace_lines.clear();
	g_state.refill();
	const uint32_t source_word0 = g_state.cpu.rdram[(kBufferBase & 0x7FFFFCu) >> 2];
	const uint32_t source_word1 = g_state.cpu.rdram[((kBufferBase + 4) & 0x7FFFFCu) >> 2];
	uint64_t expected_hash = kFnvOffset;
	expected_hash = (expected_hash * kFnvPrime) ^ source_word0;
	expected_hash = (expected_hash * kFnvPrime) ^ source_word1;
	rsp_dma_read(0x0000u, kBufferBase, 0x0000u); /* DMEM dest, row 8 bytes */
	{
		const std::string *line = find_line("fetch_watched=1");
		check(line != nullptr, "a watched DMEM-destined read must be recorded");
		if (line != nullptr)
		{
			char expected[256];
			snprintf(expected, sizeof(expected),
			         "schema=3");
			check(line->find(expected) != std::string::npos,
			      "fetch record must carry schema version 3");
			snprintf(expected, sizeof(expected),
			         "fetch_watched=1 fetch_generation=%" PRIu64
			         " fetch_buffer_offset=0 fetch_first_row=0",
			         generation);
			check(line->find(expected) != std::string::npos,
			      "fetch record must carry generation, offset and first row");
			snprintf(expected, sizeof(expected), "payload_hash=0x%016" PRIx64,
			         expected_hash);
			check(line->find(expected) != std::string::npos,
			      "fetch record must carry the payload hash of the bytes read");
			snprintf(expected, sizeof(expected), "payload_word_count=2");
			check(line->find(expected) != std::string::npos,
			      "fetch record must report the two payload words read");
		}
	}

	/* 4. A read that does not touch the buffer is not a fetch record. */
	g_trace_lines.clear();
	rsp_dma_read(0x0000u, kBufferBase + 0x10000u, 0x0000u);
	check(count_lines("fetch_watched=1") == 0,
	      "an unwatched read must not be recorded as a fetch");

	/* 5. A read that starts before the buffer and overlaps its head is
	 *    captured with offset 0 (the first byte inside the range).  The
	 *    corrected path aligns the source down to 8 bytes, so a 16-byte row
	 *    starting at base-8 reaches into the buffer. */
	g_trace_lines.clear();
	rsp_dma_read(0x0000u, kBufferBase - 8u, 0x0008u); /* 16-byte row, DMEM */
	{
		const std::string *line = find_line("fetch_watched=1");
		check(line != nullptr, "an overlapping read must be recorded");
		if (line != nullptr)
			check(line->find("fetch_buffer_offset=0 fetch_first_row=0") !=
			          std::string::npos,
			      "an overlapping read must report offset 0 in its first row");
	}

	/* 5b. Multi-row with a skip: the row spans, not a linear span, decide.
	 *     rows=3, row bytes=8, skip=16, start=base-24 reads
	 *     [base-24,base-16) [base,base+8) [base+24,base+32), so the second
	 *     row reaches the buffer at offset 0 — the shape a linearized span
	 *     would miss. */
	arm_fresh(0x5bull);
	rsp_dma_read(0x0000u, kBufferBase - 24u,
	             raw_read_length(8u, 3u, 16u));
	{
		const std::string *line = find_line("fetch_watched=1");
		check(line != nullptr,
		      "a row separated by skip must still be recorded");
		if (line != nullptr)
			check(line->find("fetch_buffer_offset=0 fetch_first_row=1") !=
			          std::string::npos,
			      "the reaching row must be reported with its index");
	}

	/* 5c. The mirror case: rows=2, row bytes=8, skip=16, start=base-8 reads
	 *     [base-8,base) and [base+16,base+24), so it never reads offset 0 and
	 *     the reported offset must be 16, not 0. */
	arm_fresh(0x5cull);
	rsp_dma_read(0x0000u, kBufferBase - 8u, raw_read_length(8u, 2u, 16u));
	{
		const std::string *line = find_line("fetch_watched=1");
		check(line != nullptr, "the second row must be recorded");
		if (line != nullptr)
			check(line->find("fetch_buffer_offset=16 fetch_first_row=1") !=
			          std::string::npos,
			      "a skipped-over head must not be claimed as read");
	}

	/* 5d. A read starting inside the buffer reports its real offset. */
	arm_fresh(0x5dull);
	rsp_dma_read(0x0000u, kBufferBase + 16u, raw_read_length(8u, 1u, 0u));
	{
		const std::string *line = find_line("fetch_watched=1");
		check(line != nullptr, "an in-buffer read must be recorded");
		if (line != nullptr)
			check(line->find("fetch_buffer_offset=16 fetch_first_row=0") !=
			          std::string::npos,
			      "the reported offset must be the byte actually read");
	}

	/* 5e. Contiguous multi-row (skip 0) is covered as one run. */
	arm_fresh(0x5eull);
	rsp_dma_read(0x0000u, kBufferBase, raw_read_length(8u, 4u, 0u));
	check(count_lines("fetch_watched=1") == 1,
	      "a contiguous multi-row read must be recorded once");

	/* 5f. A skip gap must not be counted as coverage: rows=2, row bytes=8,
	 *     skip=0x200 from base-8 reads [base-8,base) and [base+0x200,...),
	 *     neither of which is inside the 0x1a0-byte buffer. */
	arm_fresh(0x5full);
	rsp_dma_read(0x0000u, kBufferBase - 8u, raw_read_length(8u, 2u, 0x200u));
	check(count_lines("fetch_watched=1") == 0,
	      "a gap must not be reported as coverage");

	/* 6. The per-generation budget bounds the records and counts the rest;
	 *    the session summary reports records and duplicates.  The session is
	 *    reset first so the arithmetic is exact. */
	RSP::Diagnostics::set_callback(nullptr, nullptr);
	RSP::Diagnostics::set_callback(&trace_capture, nullptr);
	g_trace_lines.clear();
	RSP::Diagnostics::trace_rsp_entry(0x0000111122223333ull, 0x4444555566667777ull,
	                                  2u, 0x04001000u, g_state.task_words);
	check(RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "the watch must arm for the budget case");
	for (unsigned i = 0; i < RSP::Diagnostics::DMA_FETCH_RECORD_BUDGET + 1; ++i)
	{
		/* Distinct payloads so identity dedup could never mask a record. */
		g_state.cpu.rdram[(kBufferBase & 0x7FFFFCu) >> 2] = 0xA0000000u + i;
		rsp_dma_read(0x0000u, kBufferBase, 0x0000u);
	}
	check(count_lines("fetch_watched=1") ==
	          RSP::Diagnostics::DMA_FETCH_RECORD_BUDGET,
	      "watched fetches must be bounded by the per-generation budget");
	check(count_lines("fetch_budget exhaustion: limit=4") == 1,
	      "budget exhaustion must be reported exactly once");
	g_trace_lines.clear();
	RSP::Diagnostics::set_callback(nullptr, nullptr); /* emits the summary */
	{
		const std::string *line = find_line("fetch_summary");
		check(line != nullptr, "the session summary must report the fetch budget");
		if (line != nullptr)
			check(line->find("records=4 duplicates=1") != std::string::npos,
			      "the session summary must carry records and duplicates");
	}

	/* 7. Re-arming on the next task generation resets the per-generation
	 *    budget, so a later generation is observable again. */
	RSP::Diagnostics::set_callback(&trace_capture, nullptr);
	g_trace_lines.clear();
	RSP::Diagnostics::trace_rsp_entry(0x9999aaaabbbbccccull, 0xddddeeeeffff0000ull,
	                                  2u, 0x04001000u, g_state.task_words);
	check(RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "the next audio task must re-arm the watch");
	rsp_dma_read(0x0000u, kBufferBase, 0x0000u);
	check(count_lines("fetch_watched=1") == 1,
	      "a new generation must expose its own fetches");

	/* 8. A non-audio task does not arm the watch. */
	RSP::Diagnostics::watch_clear();
	g_state.task_words[0] = 1; /* graphics */
	check(!RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "a graphics task must not arm the audio command-buffer watch");
	check(!RSP::Diagnostics::watch_armed(), "watch must stay unarmed");
	RSP::Diagnostics::set_callback(nullptr, nullptr);
	RSP::DdRuntimePolicySet(0);

	/* 9. A watched read that also matches the P05 suspect shape keeps its
	 *    trigger snapshot (the richer record) and is counted as a trigger
	 *    record, so the fetch summary still accounts for it.  The observed
	 *    freeze read is exactly this shape: rows=16, row bytes=0x1000,
	 *    skip=0xff8, dram=0. */
	RSP::DdRuntimePolicySet(1); /* case 8 left the policy off */
	/* case 8 left the graphics task words behind: restore the audio task. */
	g_state.make_audio_task(0x80411910u, kBufferSize);
	RSP::Diagnostics::set_callback(&trace_capture, nullptr);
	arm_fresh(0x99ull);
	rsp_dma_read(0x0000u, 0x00000000u, 0xffffffffu);
	/* With the real row geometry (256 rows, stride 0x1ff8) this transfer
	 * tops out near 0x1fe7c8: it does not reach the P07 buffer at 0x411910,
	 * and the record must say so rather than claim an intersection. */
	check(count_lines("fetch_watched=1") == 0,
	      "the trigger-shaped read must not claim to reach the P07 buffer");
	{
		const std::string *line = find_line("trigger=1");
		check(line != nullptr, "the suspect-shape snapshot must still emit");
		if (line != nullptr)
			check(line->find("fetch_watched=0") != std::string::npos,
			      "the record must report that the watch did not hit");
	}
	/* A range those rows DO reach: row 64 starts at 0x7fe00 and covers the
	 * buffer head at 0x80000, so the first byte read inside the range is
	 * buffer offset 0, read by row 64. */
	g_state.make_audio_task(0x80000u, 0x1000u);
	arm_fresh(0x9aull);
	rsp_dma_read(0x0000u, 0x00000000u, 0xffffffffu);
	{
		const std::string *line = find_line("fetch_watched=1");
		check(line != nullptr,
		      "a trigger-shaped read that reaches the buffer must be recorded");
		if (line != nullptr)
		{
			check(line->find("trigger=1") != std::string::npos,
			      "the suspect-shape record must keep its trigger tag");
			check(line->find("fetch_buffer_offset=0 fetch_first_row=64") !=
			          std::string::npos,
			      "the reaching row and its offset must be exact");
		}
	}
	g_trace_lines.clear();
	RSP::Diagnostics::set_callback(nullptr, nullptr);
	{
		const std::string *line = find_line("fetch_summary");
		check(line != nullptr, "the summary must be emitted for trigger records too");
		if (line != nullptr)
			check(line->find("trigger_observations=1") != std::string::npos,
			      "the summary must count watched trigger-shaped observations");
	}

	/* 10. The callback gate and the degenerate task are refused. */
	g_state.make_audio_task(0x80411910u, kBufferSize);
	RSP::DdRuntimePolicySet(1);
	RSP::Diagnostics::watch_clear();
	check(!RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "the watch must not arm without a registered callback");
	g_state.make_audio_task(0x80411910u, 0u);
	RSP::Diagnostics::set_callback(&trace_capture, nullptr);
	check(!RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "a zero-length command buffer must not arm the watch");
	g_state.make_audio_task(0x80411910u, kBufferSize);
	check(RSP::Diagnostics::watch_arm_from_task(g_state.task_words),
	      "the well-formed audio task must arm again");
	RSP::Diagnostics::watch_clear();
	check(!RSP::Diagnostics::watch_probe(kBufferBase, 8u, 1u, 0u, nullptr,
	                                     nullptr),
	      "a cleared watch must not probe as hit");

	if (g_failures != 0)
	{
		fprintf(stderr, "rsp-dd-fetch-provenance-test: %d failure(s)\n",
		        g_failures);
		return 1;
	}
	/* The runner prints the suite result; stay silent on success. */
	return 0;
}
