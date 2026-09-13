/*
 * P02: production-path SP DMA read/write fixtures for the N64DD repair.
 *
 * These fixtures invoke the ACTUAL production transfer code through
 * RSP_MTC0 (mupen64plus-rsp-parallel/upstream/rsp/cp0.cpp, compiled here
 * with PARALLEL_INTEGRATION), the same entry the RSP JIT uses for MTC0
 * writes; a write to SP_READ_LENGTH executes rsp_dma_read, a write to
 * SP_WRITE_LENGTH executes rsp_dma_write. No copy algorithm is duplicated
 * into production or replaced by test code.
 *
 * Expectations come from the independent address/byte oracle below, which
 * implements docs/P01_DMA_POLICY_LEDGER.md directly (section 2 legacy,
 * section 4 corrected decision rows). It deliberately does not call or
 * include production helpers.
 *
 * Usage: rsp-dd-dma-transfer-test [legacy|corrected]
 * - legacy: drives the real P03 policy seam to off (DD-off) and asserts the
 *   documented legacy semantics.  Must always pass (validation D22).
 * - corrected: drives the real seam to on via RSP::DdRuntimePolicySet — the
 *   same receiver state the core's optional capability pushes — and asserts
 *   the P01 corrected policy through the production rsp_dma_read.  Wired at
 *   P04; both modes are hard gates in tools/test-dd-dma-transfer.sh.
 *
 * Guards (validation D24): the DMEM, IMEM and RDRAM arrays are carved out
 * of mappings with PROT_NONE guard pages on both sides, so a production
 * out-of-bounds access faults the process instead of passing silently.
 * Every case starts from deterministic nonuniform patterns whose words
 * encode their own word index, so misplaced reads/writes are detectable.
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
constexpr uint32_t kModeCheckFlags = 4;         // RSP::MODE_CHECK_FLAGS
constexpr uint32_t kModeContinue = 1;           // RSP::MODE_CONTINUE

int g_failures = 0;

[[noreturn]] void die(const char *message)
{
	fprintf(stderr, "FATAL: %s\n", message);
	exit(2);
}

/* Guarded allocation: payload pages flanked by PROT_NONE guard pages. */
struct Guarded
{
	char *base = nullptr;   // mmap base (guard page)
	char *payload = nullptr; // usable region
	size_t payload_size = 0;
	size_t map_size = 0;

	void map(size_t bytes)
	{
		const long page = sysconf(_SC_PAGESIZE);
		if (page <= 0)
			die("sysconf(_SC_PAGESIZE) failed");
		payload_size = (bytes + page - 1) / page * page;
		map_size = payload_size + 2 * static_cast<size_t>(page);
		base = static_cast<char *>(
		    mmap(nullptr, map_size, PROT_NONE,
		         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
		if (base == MAP_FAILED)
			die("mmap failed");
		if (mprotect(base + page, payload_size,
		             PROT_READ | PROT_WRITE) != 0)
			die("mprotect failed");
		payload = base + page;
	}
	~Guarded()
	{
		if (base != nullptr)
			munmap(base, map_size);
	}
};

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
	// Deterministic, nonuniform, self-identifying: a misplaced word almost
	// certainly differs from the expected one.
	return (array_id << 28) ^ mix32(word_index * 0x51ed270bu + array_id) ^
	       0x01234567u;
}

	struct State
{
	Guarded dmem_g, imem_g, rdram_g;
	std::vector<uint32_t> dmem_pattern, imem_pattern, rdram_pattern;
	RSP::CPUState cpu{};
	uint32_t cr[RSP::CP0_REGISTER_CMD_TMEM_BUSY + 1] = {};
	uint32_t irq = 0;
	uint32_t last_read_mode = 0;

	void init()
	{
		dmem_g.map(DMEM_SIZE);
		imem_g.map(IMEM_SIZE);
		rdram_g.map(kRdramWords * 4);
		dmem_pattern.resize(DMEM_WORDS);
		imem_pattern.resize(IMEM_WORDS);
		rdram_pattern.resize(kRdramWords);
		refill();
		cpu.dmem = reinterpret_cast<uint32_t *>(dmem_g.payload);
		cpu.imem = reinterpret_cast<uint32_t *>(imem_g.payload);
		cpu.rdram = reinterpret_cast<uint32_t *>(rdram_g.payload);
		for (unsigned i = 0; i < sizeof(cr) / sizeof(cr[0]); ++i)
			cpu.cp0.cr[i] = &cr[i];
		cpu.cp0.irq = &irq;
	}

	void refill()
	{
		for (uint32_t i = 0; i < DMEM_WORDS; ++i)
			dmem_pattern[i] = pattern_word(1, i);
		for (uint32_t i = 0; i < IMEM_WORDS; ++i)
			imem_pattern[i] = pattern_word(2, i);
		for (uint32_t i = 0; i < kRdramWords; ++i)
			rdram_pattern[i] = pattern_word(3, i);
		memcpy(dmem_g.payload, dmem_pattern.data(), DMEM_SIZE);
		memcpy(imem_g.payload, imem_pattern.data(), IMEM_SIZE);
		memcpy(rdram_g.payload, rdram_pattern.data(), kRdramWords * 4);
		cpu.dirty_blocks = 0;
	}
} g_state;

/* Production entry: the exact MTC0 sequence a guest uses to launch a DMA. */
uint32_t production_dma_read(uint32_t raw_cache, uint32_t raw_dram,
                             uint32_t raw_len)
{
	g_state.cpu.sr[8] = raw_cache;
	RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_CACHE, 8);
	g_state.cpu.sr[8] = raw_dram;
	RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_DRAM, 8);
	g_state.cpu.sr[8] = raw_len;
	return static_cast<uint32_t>(
	    RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_READ_LENGTH, 8));
}

void production_dma_write(uint32_t raw_cache, uint32_t raw_dram,
                          uint32_t raw_len)
{
	g_state.cpu.sr[8] = raw_cache;
	RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_CACHE, 8);
	g_state.cpu.sr[8] = raw_dram;
	RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_DRAM, 8);
	g_state.cpu.sr[8] = raw_len;
	RSP_MTC0(&g_state.cpu, RSP::CP0_REGISTER_DMA_WRITE_LENGTH, 8);
}

/* ---------------------------------------------------------------------------
 * Independent oracle. Implements docs/P01_DMA_POLICY_LEDGER.md, not the
 * production code. Both models start from the pristine patterns; the
 * production fixture must reproduce the oracle's post image exactly.
 * ------------------------------------------------------------------------- */

struct Result
{
	std::vector<uint32_t> dmem, imem;
	uint32_t dirty = 0;
	uint32_t final_cache = 0, final_dram = 0;
	uint32_t rd_len_reg = 0, wr_len_reg = 0; // ledger row 11: never modified
	uint64_t copied_words = 0, imem_word_writes = 0;
};

void mark_dirty(Result &r, uint32_t dest_addr13)
{
	// Ledger §4 row 12: production marks the 256-byte block containing an
	// actual IMEM word write with dirty_blocks |= (0x3 << block) >> 1
	// (cp0.cpp:291-292; state.hpp:10-13 gives 16 blocks of 256 bytes),
	// i.e. the containing block and the previous one (block 0: bit 0 only).
	const unsigned block = (dest_addr13 & 0xfff) >> 8;
	r.dirty |= 1u << block;
	if (block != 0)
		r.dirty |= 1u << (block - 1);
}

uint32_t rdram_word(const std::vector<uint32_t> &rdram, uint32_t addr)
{
	return rdram[(addr & 0x7FFFFCu) >> 2]; // Ledger §4 row 8 (unchanged mask).
}

/* Ledger §2: the legacy (DD-off) model = current production semantics. */
Result oracle_legacy(uint32_t raw_cache, uint32_t raw_dram, uint32_t raw_len)
{
	Result r;
	r.dmem = g_state.dmem_pattern;
	r.imem = g_state.imem_pattern;

	uint32_t length = (raw_len & 0xFFF) + 1;
	const uint32_t skip = (raw_len >> 20) & 0xFFF;
	const uint32_t rows = ((raw_len >> 12) & 0xFF) + 1;
	length = (length + 7) & ~7;                       // 8-byte row align
	uint32_t cache = (raw_cache & 0x1fff) & ~0x3u;    // MTC0 + 4-byte SP align
	uint32_t dram = (raw_dram & 0xffffff) & ~0x7u;    // MTC0 + 8-byte align

	// Ledger §2: the bank-boundary clamp is applied ONCE, before the row
	// loop, from the masked DMA_CACHE register; the clamped length then
	// stays fixed for every row (cp0.cpp:248-249).
	if ((cache & 0xFFF) + length > 0x1000)
		length = 0x1000 - (cache & 0xFFF);

	uint32_t src = dram, dst = cache;
	for (uint32_t i = 0; i < rows; ++i)
	{
		for (uint32_t j = 0; j < length; j += 4)
		{
			const uint32_t sa = (src + j) & 0x7FFFFC;
			const uint32_t da = (dst + j) & 0x1FFC; // 13-bit walk
			const uint32_t word = rdram_word(g_state.rdram_pattern, sa);
			if (da & 0x1000)
			{
				r.imem[(da & 0xfff) >> 2] = word;
				mark_dirty(r, da);
				r.imem_word_writes++;
			}
			else
			{
				r.dmem[da >> 2] = word;
			}
			r.copied_words++;
		}
		// Ledger §2: per-row advance INCLUDING the last row (visible
		// trailing skip on the DRAM side); raw accumulated writeback.
		src += length + skip;
		dst += length;
	}
	r.final_cache = dst;
	r.final_dram = src;
	r.rd_len_reg = raw_len; // MTC0 stores the length register unmasked
	r.wr_len_reg = 0;       // a read transfer never writes SP_WRITE_LENGTH
	return r;
}

/* Ledger §4: the corrected model (pinned Ares semantics, rows 1-3, 5-10, 12). */
Result oracle_corrected(uint32_t raw_cache, uint32_t raw_dram, uint32_t raw_len)
{
	Result r;
	r.dmem = g_state.dmem_pattern;
	r.imem = g_state.imem_pattern;

	const uint32_t rowbytes = ((raw_len & 0xfff) | 7) + 1; // row 3 (no clamp)
	const uint32_t rows = ((raw_len >> 12) & 0xFF) + 1;    // row 4
	const uint32_t skip = ((raw_len >> 20) & 0xFFF) & 0xFF8u; // row 7
	const uint32_t cache13 = raw_cache & 0x1fff;
	const uint32_t region = (cache13 >> 12) & 1;           // row 1: latched
	uint32_t off = cache13 & 0xFF8u;                       // rows 2+5
	uint32_t src = (raw_dram & 0xffffff) & ~0x7u;          // row 6

	for (uint32_t i = 0; i < rows; ++i)
	{
		for (uint32_t b = 0; b < rowbytes; b += 8)
		{
			const uint32_t w0 = rdram_word(g_state.rdram_pattern, src);
			const uint32_t w1 = rdram_word(g_state.rdram_pattern, src + 4);
			if (region)
			{
				// 8-byte beat into IMEM; off stays inside the bank.
				r.imem[off >> 2] = w0;
				r.imem[(off + 4) >> 2] = w1;
				mark_dirty(r, 0x1000u | off);
				mark_dirty(r, 0x1000u | (off + 4));
				r.imem_word_writes += 2;
			}
			else
			{
				r.dmem[off >> 2] = w0;
				r.dmem[(off + 4) >> 2] = w1;
			}
			r.copied_words += 2;
			off = (off + 8) & 0xfff; // row 2: wrap within latched bank
			src += 8;
		}
		// Ledger row 10: skip only BETWEEN rows (no trailing skip).
		if (i + 1 < rows)
			src += skip;
	}
	r.final_cache = (region << 12) | off;             // row 9
	r.final_dram = src & 0xffffff;                    // row 10
	r.rd_len_reg = raw_len; // MTC0 stores the length register unmasked
	r.wr_len_reg = 0;       // a read transfer never writes SP_WRITE_LENGTH
	return r;
}

/* ---------------------------------------------------------------------------
 * Case table and comparison.
 * ------------------------------------------------------------------------- */

enum class Kind
{
	Shared,    // both models agree; asserted in both modes
	Legacy,    // pins current/legacy behavior (D01, D22, D23, D18)
	Corrected  // P01 corrected expectations (D02, D06, D07, D08, D11, D13,
	           //  D14, D16, D19); pinned by the production corrected arm (P04)
};

struct Case
{
	const char *id;
	const char *why;
	uint32_t cache, dram, len;
	Kind kind;
	bool diagnostics;      // capture production observer counts (D01)
	uint64_t oracle_words; // oracle self-check, 0 = skip
	uint64_t oracle_imem;  // oracle self-check, 0 = skip
};

const Case kCases[] = {
	// ---- Shared semantics (both policies must agree; oracle cross-check) ----
	{"D03", "short ordinary DMEM row", 0x0000, 0x00000100, 0x0000003b,
	 Kind::Shared, false, 16, 0},
	{"D09a", "raw length 0 -> 8 bytes, never zero-byte", 0x0100, 0x00000300,
	 0x00000000, Kind::Shared, false, 2, 0},
	{"D09b", "raw length 7 -> 8 bytes", 0x0108, 0x00000380, 0x00000007,
	 Kind::Shared, false, 2, 0},
	{"D10", "count 0xff -> exactly 256 rows", 0x0000, 0x00001000,
	 0x000ff007, Kind::Shared, false, 512, 0},
	{"D12", "DRAM low bits masked ~7", 0x0200, 0x00001234, 0x00000007,
	 Kind::Shared, false, 2, 0},
	// ---- Legacy pins (must keep passing post-fix in legacy mode) ----
	{"D01", "captured request under legacy policy", 0x0fb0, 0x00000000,
	 0xffffffff, Kind::Legacy, true, 5120, 3052},
	{"D22a", "clamp once, crossing across rows", 0x0f80, 0x00004000,
	 0x000010ff, Kind::Legacy, false, 64, 32},
	{"D22b", "raw unaligned skip, word-floor source", 0x0100, 0x00001000,
	 0x1230203f, Kind::Legacy, false, 48, 0},
	{"D14L", "legacy 4-byte SP alignment at 4-mod-8", 0x1ab4, 0x00006000,
	 0x0000001f, Kind::Legacy, false, 8, 8},
	// ---- Corrected expectations (production corrected arm, P04) ----
	// D05 (exact fit ending at 0x1000) is here, not Shared: bytes agree but
	// the final register poststate diverges (legacy writes back the raw
	// accumulated 0x1000; corrected writes back 0x0000 per ledger row 9).
	{"D05", "DMEM exact fit; register poststate per row 9", 0x0fc0,
	 0x00000200, 0x00000038, Kind::Corrected, false, 16, 0},
	{"D02", "captured request under corrected policy", 0x0fb0, 0x00000000,
	 0xffffffff, Kind::Corrected, false, 262144, 0},
	{"D02diag", "captured request, diagnostics on: still observed, zero "
	 "IMEM writes", 0x0fb0, 0x00000000, 0xffffffff, Kind::Corrected, true,
	 262144, 0},
	{"D06", "DMEM row wraps inside bank, never IMEM", 0x0fb0, 0x00002000,
	 0x00000fff, Kind::Corrected, false, 1024, 0},
	{"D07", "IMEM row wraps inside bank, never DMEM", 0x1fb0, 0x00003000,
	 0x00000fff, Kind::Corrected, false, 1024, 1024},
	{"D08", "cumulative crossing, bank stays latched", 0x0f00, 0x00004000,
	 0x000010ff, Kind::Corrected, false, 128, 0},
	{"D11", "8-byte SP alignment at 4-mod-8 start", 0x0ff4, 0x00005000,
	 0x00000007, Kind::Corrected, false, 2, 0},
	{"D13", "skip low bits aligned to 8", 0x0000, 0x00001000, 0xfff01007,
	 Kind::Corrected, false, 4, 0},
	{"D14", "bit-12 bank + 8-byte offset decode", 0x1abc, 0x00006000,
	 0x0000001f, Kind::Corrected, false, 8, 8},
	{"D16", "two full-circle rows; last writer wins", 0x0800, 0x00007000,
	 0x00001fff, Kind::Corrected, false, 2048, 0},
	{"D19", "no trailing skip in final DRAM register", 0x0100, 0x00001000,
	 0x0100203f, Kind::Corrected, false, 48, 0},
};

std::vector<std::string> g_trace_lines;

void trace_capture(void *context, int level, const char *line)
{
	(void)context;
	(void)level;
	if (line != nullptr && g_trace_lines.size() < 64)
		g_trace_lines.emplace_back(line);
}

bool parse_u32_field(const std::string &lines, const char *field,
                     uint64_t *out)
{
	const size_t pos = lines.rfind(field);
	if (pos == std::string::npos)
		return false;
	*out = strtoull(lines.c_str() + pos + strlen(field), nullptr, 10);
	return true;
}

struct Diff
{
	const char *field;
	std::string detail;
};

std::string hex32(uint32_t v)
{
	char buf[16];
	snprintf(buf, sizeof(buf), "0x%x", v);
	return buf;
}

std::vector<Diff> compare(const Result &expected)
{
	std::vector<Diff> diffs;
	const auto &cpu = g_state.cpu;
	for (uint32_t i = 0; i < DMEM_WORDS; ++i)
	{
		if (cpu.dmem[i] != expected.dmem[i])
		{
			diffs.push_back({"dmem", "word " + hex32(i) + ": want " +
			                             hex32(expected.dmem[i]) + " got " +
			                             hex32(cpu.dmem[i])});
			break;
		}
	}
	for (uint32_t i = 0; i < IMEM_WORDS; ++i)
	{
		if (cpu.imem[i] != expected.imem[i])
		{
			diffs.push_back({"imem", "word " + hex32(i) + ": want " +
			                             hex32(expected.imem[i]) + " got " +
			                             hex32(cpu.imem[i])});
			break;
		}
	}
	if (cpu.dirty_blocks != expected.dirty)
		diffs.push_back({"dirty", "want " + hex32(expected.dirty) +
		                              " got " + hex32(cpu.dirty_blocks)});
	if (g_state.cr[RSP::CP0_REGISTER_DMA_CACHE] != expected.final_cache)
		diffs.push_back({"cache_reg",
		                 "want " + hex32(expected.final_cache) + " got " +
		                     hex32(g_state.cr[RSP::CP0_REGISTER_DMA_CACHE])});
	if (g_state.cr[RSP::CP0_REGISTER_DMA_DRAM] != expected.final_dram)
		diffs.push_back({"dram_reg",
		                 "want " + hex32(expected.final_dram) + " got " +
		                     hex32(g_state.cr[RSP::CP0_REGISTER_DMA_DRAM])});
	if (g_state.cr[RSP::CP0_REGISTER_DMA_READ_LENGTH] != expected.rd_len_reg)
		diffs.push_back({"rd_len_reg",
		                 "want " + hex32(expected.rd_len_reg) + " got " +
		                     hex32(g_state.cr[RSP::CP0_REGISTER_DMA_READ_LENGTH])});
	if (g_state.cr[RSP::CP0_REGISTER_DMA_WRITE_LENGTH] != expected.wr_len_reg)
		diffs.push_back({"wr_len_reg",
		                 "want " + hex32(expected.wr_len_reg) + " got " +
		                     hex32(g_state.cr[RSP::CP0_REGISTER_DMA_WRITE_LENGTH])});
	const uint32_t want_mode =
	    expected.dirty != 0 ? kModeCheckFlags : kModeContinue;
	if (g_state.last_read_mode != want_mode)
		diffs.push_back({"mode", "want " + std::to_string(want_mode) +
		                             " got " +
		                             std::to_string(g_state.last_read_mode)});
	return diffs;
}

void run_case(const Case &c, Result (*oracle)(uint32_t, uint32_t, uint32_t),
              const char *oracle_name)
{
	g_state.refill();

	if (c.diagnostics)
	{
		g_trace_lines.clear();
		RSP::Diagnostics::set_callback(&trace_capture, nullptr);
	}

	const uint32_t mode =
	    production_dma_read(c.cache, c.dram, c.len);
	g_state.last_read_mode = mode;

	if (c.diagnostics)
		RSP::Diagnostics::set_callback(nullptr, nullptr);

	Result expected = oracle(c.cache, c.dram, c.len);

	if (c.oracle_words != 0 && expected.copied_words != c.oracle_words)
	{
		fprintf(stderr,
		        "[ORACLE-BUG] %s: copied words %" PRIu64 " != pinned %" PRIu64
		        "\n",
		        c.id, expected.copied_words, c.oracle_words);
		g_failures++;
		return;
	}
	if (c.oracle_imem != 0 && expected.imem_word_writes != c.oracle_imem)
	{
		fprintf(stderr,
		        "[ORACLE-BUG] %s: IMEM word writes %" PRIu64 " != pinned %" PRIu64
		        "\n",
		        c.id, expected.imem_word_writes, c.oracle_imem);
		g_failures++;
		return;
	}

	const std::vector<Diff> diffs = compare(expected);

	if (c.diagnostics)
	{
		std::string all;
		for (const auto &l : g_trace_lines)
			all += l + "\n";
		uint64_t payload_words = 0, imem_writes = 0;
		const bool have_payload =
		    parse_u32_field(all, "payload_word_count=", &payload_words);
		const bool have_imem =
		    parse_u32_field(all, "imem_write_word_count=", &imem_writes);
		if (!have_payload || !have_imem)
		{
			fprintf(stderr,
			        "[FAIL] %s: production DMA observer did not report "
			        "word counts (diagnostics enabled)\n",
			        c.id);
			g_failures++;
		}
		else if (payload_words != c.oracle_words ||
		         imem_writes != c.oracle_imem)
		{
			fprintf(stderr,
			        "[FAIL] %s: production observer counts (payload=%" PRIu64
			        " imem=%" PRIu64 ") disagree with oracle (%" PRIu64
			        "/%" PRIu64 ")\n",
			        c.id, payload_words, imem_writes, c.oracle_words,
			        c.oracle_imem);
			g_failures++;
		}
		else
		{
			printf("  %s production observer: payload=%" PRIu64
			       " imem_writes=%" PRIu64 " (matches DDSTART11 record)\n",
			       c.id, payload_words, imem_writes);
		}
	}

	if (diffs.empty())
	{
		printf("[PASS] %s (%s)\n", c.id, c.why);
		return;
	}

	fprintf(stderr, "[FAIL] %s (%s, %s oracle):\n", c.id, c.why, oracle_name);
	for (const auto &d : diffs)
		fprintf(stderr, "       %s: %s\n", d.field, d.detail.c_str());
	g_failures++;
}

/* D23: pin the rsp_dma_write path (deliberately unchanged by the repair). */
void run_write_pins()
{
	g_state.refill();
	// SP 0x300..0x31f -> RDRAM 0x5000, 32 bytes, count 1, skip 0.
	production_dma_write(0x0300, 0x00005000, 0x0000001f);
	bool ok = true;
	for (uint32_t j = 0; j < 8; ++j)
	{
		const uint32_t sa = (0x0300 + 4 * j) & 0x1FFC;
		const uint32_t want =
		    (sa & 0x1000) ? g_state.imem_pattern[(sa & 0xfff) >> 2]
		                  : g_state.dmem_pattern[sa >> 2];
		if (g_state.cpu.rdram[(0x5000 >> 2) + j] != want)
			ok = false;
	}
	if (g_state.cr[RSP::CP0_REGISTER_DMA_CACHE] != 0x320 ||
	    g_state.cr[RSP::CP0_REGISTER_DMA_DRAM] != 0x5020)
		ok = false;
	if (!ok)
	{
		fprintf(stderr, "[FAIL] D23a: rsp_dma_write basic pin changed\n");
		g_failures++;
	}
	else
	{
		printf("[PASS] D23a (rsp_dma_write basic pin)\n");
	}

	g_state.refill();
	// Same geometry with raw skip 0x11: trailing skip lands in the DRAM
	// register writeback (0x5000 + 32 + 17 = 0x5031, unmasked raw value).
	production_dma_write(0x0300, 0x00005000, 0x0110001f);
	if (g_state.cr[RSP::CP0_REGISTER_DMA_CACHE] != 0x320 ||
	    g_state.cr[RSP::CP0_REGISTER_DMA_DRAM] != 0x5031)
	{
		fprintf(stderr,
		        "[FAIL] D23b: rsp_dma_write trailing-skip pin changed "
		        "(cache=0x%x dram=0x%x)\n",
		        g_state.cr[RSP::CP0_REGISTER_DMA_CACHE],
		        g_state.cr[RSP::CP0_REGISTER_DMA_DRAM]);
		g_failures++;
	}
	else
	{
		printf("[PASS] D23b (rsp_dma_write trailing-skip pin)\n");
	}
}

/* D18: an IMEM transfer followed by a DMEM transfer must not attribute
 * stale dirty state to the new transfer. */
void run_d18_compound()
{
	g_state.refill();
	production_dma_read(0x1ab4, 0x00006000, 0x0000001f);
	const uint32_t dirty_after_imem = g_state.cpu.dirty_blocks;
	std::vector<uint32_t> imem_after_imem(
	    g_state.cpu.imem, g_state.cpu.imem + IMEM_WORDS);
	if (dirty_after_imem == 0)
	{
		fprintf(stderr, "[FAIL] D18: IMEM transfer set no dirty blocks\n");
		g_failures++;
		return;
	}

	production_dma_read(0x0000, 0x00000100, 0x0000003b);
	if (g_state.cpu.dirty_blocks != dirty_after_imem)
	{
		fprintf(stderr,
		        "[FAIL] D18: DMEM transfer changed dirty state "
		        "(0x%x -> 0x%x)\n",
		        dirty_after_imem, g_state.cpu.dirty_blocks);
		g_failures++;
		return;
	}
	if (memcmp(g_state.cpu.imem, imem_after_imem.data(),
	           IMEM_SIZE) != 0)
	{
		fprintf(stderr, "[FAIL] D18: DMEM transfer modified IMEM\n");
		g_failures++;
		return;
	}
	printf("[PASS] D18 (DMEM transfer after IMEM transfer)\n");
}

/* D21: randomized legacy differential with a fixed seed. */
void run_random_legacy()
{
	uint32_t seed = 0xD0DAB1Eu;
	const auto next = [&seed]() {
		seed = seed * 1664525u + 1013904223u;
		return seed;
	};
	int bad = 0;
	for (int iter = 0; iter < 128; ++iter)
	{
		g_state.refill();
		const uint32_t cache = next() & 0x1fff;
		const uint32_t dram = next() & 0x00ffffff;
		const uint32_t len = next();
		const uint32_t mode = production_dma_read(cache, dram, len);
		g_state.last_read_mode = mode;
		const Result expected = oracle_legacy(cache, dram, len);
		const std::vector<Diff> diffs = compare(expected);
		if (!diffs.empty())
		{
			if (bad == 0)
				fprintf(stderr,
				        "[FAIL] D21 random legacy case %d "
				        "(cache=0x%x dram=0x%x len=0x%x): %s: %s\n",
				        iter, cache, dram, len, diffs[0].field,
				        diffs[0].detail.c_str());
			bad++;
		}
	}
	if (bad != 0)
	{
		fprintf(stderr, "[FAIL] D21: %d/128 randomized legacy cases diverged\n",
		        bad);
		g_failures++;
	}
	else
	{
		printf("[PASS] D21 (128 randomized legacy differential cases)\n");
	}
}

void usage(const char *argv0)
{
	fprintf(stderr, "usage: %s [legacy|corrected]\n", argv0);
}

} // namespace

int main(int argc, char **argv)
{
	const char *mode = "legacy";
	for (int i = 1; i < argc; ++i)
	{
		if (strcmp(argv[i], "legacy") == 0)
			mode = "legacy";
		else if (strcmp(argv[i], "corrected") == 0)
			mode = "corrected";
		else
		{
			usage(argv[0]);
			return 2;
		}
	}

	g_state.init();
	printf("rsp-dd-dma-transfer-test: mode=%s\n", mode);

	// Oracle cross-check on shared cases: the two models must agree exactly
	// when nothing diverges (no crossing, no unaligned skip, no trailing
	// skip effect). This validates the corrected oracle against the
	// production-verified legacy oracle.
	for (const Case &c : kCases)
	{
		if (c.kind != Kind::Shared)
			continue;
		const Result legacy =
		    oracle_legacy(c.cache, c.dram, c.len);
		const Result corrected =
		    oracle_corrected(c.cache, c.dram, c.len);
		if (legacy.final_cache != corrected.final_cache ||
		    legacy.final_dram != corrected.final_dram ||
		    legacy.dirty != corrected.dirty ||
		    legacy.dmem != corrected.dmem || legacy.imem != corrected.imem)
		{
			fprintf(stderr,
			        "[FAIL] oracle cross-check %s: models disagree on a "
			        "shared case\n",
			        c.id);
			g_failures++;
		}
	}
	if (g_failures == 0)
		printf("[PASS] oracle cross-check (shared cases agree)\n");

	if (strcmp(mode, "legacy") == 0)
	{
		/* DD-off: drive the real receiver state the core pushes. */
		RSP::DdRuntimePolicySet(0);
		for (const Case &c : kCases)
		{
			if (c.kind == Kind::Shared || c.kind == Kind::Legacy)
				run_case(c, oracle_legacy, "legacy");
		}
		run_d18_compound();
		run_random_legacy();
	}
	else
	{
		/* Explicitly DD-authorized session: enable the real seam (P03
		 * receiver) so production rsp_dma_read takes the corrected arm. */
		RSP::DdRuntimePolicySet(1);
		for (const Case &c : kCases)
		{
			if (c.kind == Kind::Shared || c.kind == Kind::Corrected)
				run_case(c, oracle_corrected, "corrected");
		}
		RSP::DdRuntimePolicySet(0);
	}

	run_write_pins();

	if (g_failures != 0)
	{
		fprintf(stderr, "rsp-dd-dma-transfer-test: %d failure(s)\n",
		        g_failures);
		return 1;
	}
	printf("rsp-dd-dma-transfer-test: all checks passed\n");
	return 0;
}
