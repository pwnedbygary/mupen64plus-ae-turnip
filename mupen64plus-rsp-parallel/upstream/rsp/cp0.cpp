#include "../state.hpp"
#include <string.h>

#ifdef PARALLEL_INTEGRATION
#include "../rsp_1.1.h"
#include "../rsp_diag.hpp"
#include "../dd_policy.hpp"
#include "m64p_plugin.h"
namespace RSP
{
extern RSP_INFO rsp;
extern short MFC0_count[32];
extern int SP_STATUS_TIMEOUT;
} // namespace RSP
#endif

using namespace RSP;

#ifdef PARALLEL_INTEGRATION
namespace
{
constexpr uint64_t DMA_FNV_PRIME = UINT64_C(0x100000001b3);
constexpr uint64_t DMA_FNV_OFFSET = UINT64_C(0xcbf29ce484222325);
constexpr unsigned IMEM_SAMPLE_OFFSETS[RSP::Diagnostics::DMA_IMEM_SAMPLE_COUNT] = {
	0x000, 0x3fc, 0xf60, 0xffc
};

uint64_t dma_hash_imem(const uint32_t *imem)
{
	uint64_t hash = DMA_FNV_OFFSET;
	for (unsigned i = 0; i < IMEM_WORDS; ++i)
		hash = (hash * DMA_FNV_PRIME) ^ imem[i];
	return hash;
}

void dma_imem_samples(const uint32_t *imem, uint32_t *samples)
{
	for (unsigned i = 0; i < RSP::Diagnostics::DMA_IMEM_SAMPLE_COUNT; ++i)
		samples[i] = imem[IMEM_SAMPLE_OFFSETS[i] >> 2];
}

void dma_payload_word(RSP::Diagnostics::DmaReadObservation &observation,
                      uint32_t source_addr, uint32_t word)
{
	observation.payload_hash =
	    (observation.payload_hash * DMA_FNV_PRIME) ^ word;
	if (observation.payload_first_count < 4)
		observation.payload_first[observation.payload_first_count++] = word;
	if (observation.payload_last_count < 4)
		observation.payload_last[observation.payload_last_count++] = word;
	else
	{
		observation.payload_last[0] = observation.payload_last[1];
		observation.payload_last[1] = observation.payload_last[2];
		observation.payload_last[2] = observation.payload_last[3];
		observation.payload_last[3] = word;
	}
	if (observation.source_first_count < 4)
		observation.source_first[observation.source_first_count++] =
		    source_addr;
	if (observation.source_last_count < 4)
		observation.source_last[observation.source_last_count++] = source_addr;
	else
	{
		observation.source_last[0] = observation.source_last[1];
		observation.source_last[1] = observation.source_last[2];
		observation.source_last[2] = observation.source_last[3];
		observation.source_last[3] = source_addr;
	}
	if (observation.payload_word_count >= 8)
		observation.payload_samples_truncated = true;
	++observation.payload_word_count;
}

void dma_imem_range(RSP::Diagnostics::DmaReadObservation &observation,
                    uint32_t dest_addr)
{
	const uint16_t end = static_cast<uint16_t>(dest_addr + 4);
	if (observation.imem_write_word_count == 0)
	{
		observation.first_imem_start = static_cast<uint16_t>(dest_addr);
		observation.first_imem_end = end;
		observation.imem_min_start = static_cast<uint16_t>(dest_addr);
		observation.imem_max_end = end;
		observation.last_imem_end = end;
		observation.imem_range_count = 1;
	}
	else
	{
		if (observation.imem_min_start > dest_addr)
			observation.imem_min_start = static_cast<uint16_t>(dest_addr);
		if (observation.imem_max_end < end)
			observation.imem_max_end = end;
		if (observation.last_imem_end != dest_addr)
			++observation.imem_range_count;
		observation.last_imem_end = end;
		if (observation.imem_range_count == 1)
			observation.first_imem_end = end;
	}
	observation.imem_bank_mask |=
	    static_cast<uint16_t>(1u << ((dest_addr >> 12) & 0xf));
	++observation.imem_write_word_count;
}
} // namespace
#endif

extern "C"
{

#ifdef INTENSE_DEBUG
	void log_rsp_mem_parallel(void);
#endif

	int RSP_MFC0(RSP::CPUState *rsp, unsigned rt, unsigned rd)
	{
		rd &= 15;
		uint32_t res = *rsp->cp0.cr[rd];
		if (rt)
			rsp->sr[rt] = res;

			// CFG_MEND_SEMAPHORE_LOCK == 0 by default,
			// so don't bother implementing semaphores.
			// It makes Mario Golf run terribly for some reason.

#ifdef PARALLEL_INTEGRATION
		// WAIT_FOR_CPU_HOST. From CXD4.
		if (rd == CP0_REGISTER_SP_STATUS)
		{
			RSP::MFC0_count[rt] += 1;
			if (RSP::MFC0_count[rt] >= RSP::SP_STATUS_TIMEOUT)
			{
				*RSP::rsp.SP_STATUS_REG |= SP_STATUS_HALT;
				return MODE_CHECK_FLAGS;
			}
		}
#endif

		//if (rd == 4) // SP_STATUS_REG
		//   fprintf(stderr, "READING STATUS REG!\n");

		return MODE_CONTINUE;
	}

	static inline int rsp_status_write(RSP::CPUState *rsp, uint32_t rt)
	{
		//fprintf(stderr, "Writing 0x%x to status reg!\n", rt);

		uint32_t status = *rsp->cp0.cr[CP0_REGISTER_SP_STATUS];

		if (rt & SP_CLR_HALT)
			status &= ~SP_STATUS_HALT;
		else if (rt & SP_SET_HALT)
			status |= SP_STATUS_HALT;

		if (rt & SP_CLR_BROKE)
			status &= ~SP_STATUS_BROKE;

		if (rt & SP_CLR_INTR)
			*rsp->cp0.irq &= ~1;
		else if (rt & SP_SET_INTR)
			*rsp->cp0.irq |= 1;

		if (rt & SP_CLR_SSTEP)
			status &= ~SP_STATUS_SSTEP;
		else if (rt & SP_SET_SSTEP)
			status |= SP_STATUS_SSTEP;

		if (rt & SP_CLR_INTR_BREAK)
			status &= ~SP_STATUS_INTR_BREAK;
		else if (rt & SP_SET_INTR_BREAK)
			status |= SP_STATUS_INTR_BREAK;

		if (rt & SP_CLR_SIG0)
			status &= ~SP_STATUS_SIG0;
		else if (rt & SP_SET_SIG0)
			status |= SP_STATUS_SIG0;

		if (rt & SP_CLR_SIG1)
			status &= ~SP_STATUS_SIG1;
		else if (rt & SP_SET_SIG1)
			status |= SP_STATUS_SIG1;

		if (rt & SP_CLR_SIG2)
			status &= ~SP_STATUS_SIG2;
		else if (rt & SP_SET_SIG2)
			status |= SP_STATUS_SIG2;

		if (rt & SP_CLR_SIG3)
			status &= ~SP_STATUS_SIG3;
		else if (rt & SP_SET_SIG3)
			status |= SP_STATUS_SIG3;

		if (rt & SP_CLR_SIG4)
			status &= ~SP_STATUS_SIG4;
		else if (rt & SP_SET_SIG4)
			status |= SP_STATUS_SIG4;

		if (rt & SP_CLR_SIG5)
			status &= ~SP_STATUS_SIG5;
		else if (rt & SP_SET_SIG5)
			status |= SP_STATUS_SIG5;

		if (rt & SP_CLR_SIG6)
			status &= ~SP_STATUS_SIG6;
		else if (rt & SP_SET_SIG6)
			status |= SP_STATUS_SIG6;

		if (rt & SP_CLR_SIG7)
			status &= ~SP_STATUS_SIG7;
		else if (rt & SP_SET_SIG7)
			status |= SP_STATUS_SIG7;

		*rsp->cp0.cr[CP0_REGISTER_SP_STATUS] = status;
		return ((*rsp->cp0.irq & 1) || (status & SP_STATUS_HALT)) ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

#ifdef PARALLEL_INTEGRATION
	/*
	 * Corrected internal-RSP-DMA-read arm (P04), implementing the P01
	 * policy ledger (docs/P01_DMA_POLICY_LEDGER.md §4, pinned Ares model):
	 * the bank is latched from bit 12 and never changes (row 1), the
	 * destination wraps inside that bank (row 2), the full decoded row
	 * length is used with no bank-boundary clamp (row 3), the skip is
	 * aligned to 8 bytes and added only between rows (rows 7 and 10), and
	 * the final registers follow rows 9 and 10.  Dirty blocks are marked
	 * with the production expression for actual IMEM word writes only
	 * (row 12), covering wrapped regions.  Raw operands are snapshotted in
	 * rsp_dma_read before any alignment or register mutation; this arm
	 * re-derives geometry from the raw length register and never touches
	 * the legacy path above it.
	 */
	static int rsp_dma_read_corrected(RSP::CPUState *rsp, uint32_t length_reg,
	                                  RSP::Diagnostics::DmaReadObservation &observation,
	                                  bool diagnostics_enabled)
	{
		const uint32_t row_length = ((length_reg & 0xFFF) | 7) + 1;
		const unsigned rows = ((length_reg >> 12) & 0xFF) + 1;
		const uint32_t skip = ((length_reg >> 20) & 0xFFF) & 0xFF8;
		const uint32_t cache13 =
		    *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0x1fff;
		const uint32_t region = (cache13 >> 12) & 1;
		uint32_t dest_offset = cache13 & 0xFF8;
		uint32_t source = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] & ~0x7u;

		if (diagnostics_enabled)
		{
			observation.aligned_length = row_length;
			/* No bank-boundary clamp exists on this path. */
			observation.effective_length = row_length;
		}

		/* Observer eligibility uses the corrected 8-byte-aligned start so
		 * the boundary matches the transfer this arm actually performs. */
		const bool diagnostics_eligible =
		    diagnostics_enabled
		    && RSP::Diagnostics::imem_dma_eligible(
		           cache13 & 0x1FF8, row_length, rows);

		if (diagnostics_eligible)
		{
			observation.payload_hash = DMA_FNV_OFFSET;
			observation.imem_before_hash = dma_hash_imem(rsp->imem);
			dma_imem_samples(rsp->imem, observation.imem_before_samples);
		}

		for (unsigned i = 0; i < rows; ++i)
		{
			for (uint32_t j = 0; j < row_length; j += 8)
			{
				const uint32_t source_addr = source & 0x7FFFFC;
				const uint32_t word_lo = rsp->rdram[source_addr >> 2];
				const uint32_t source_addr_hi = (source + 4) & 0x7FFFFC;
				const uint32_t word_hi = rsp->rdram[source_addr_hi >> 2];
				if (diagnostics_eligible)
				{
					dma_payload_word(observation, source_addr, word_lo);
					dma_payload_word(observation, source_addr_hi, word_hi);
				}

				if (region)
				{
					const uint32_t dest_a = 0x1000u | dest_offset;
					const uint32_t dest_b = 0x1000u | (dest_offset + 4);
					if (diagnostics_eligible)
					{
						dma_imem_range(observation, dest_a);
						dma_imem_range(observation, dest_b);
					}
					const unsigned block_a = (dest_a & 0xfff) / CODE_BLOCK_SIZE;
					const unsigned block_b = (dest_b & 0xfff) / CODE_BLOCK_SIZE;
					rsp->dirty_blocks |= (0x3 << block_a) >> 1;
					rsp->dirty_blocks |= (0x3 << block_b) >> 1;
					rsp->imem[dest_offset >> 2] = word_lo;
					rsp->imem[(dest_offset + 4) >> 2] = word_hi;
				}
				else
				{
					rsp->dmem[dest_offset >> 2] = word_lo;
					rsp->dmem[(dest_offset + 4) >> 2] = word_hi;
				}

				dest_offset = (dest_offset + 8) & 0xFFF;
				source += 8;
			}
			/* Row 10: skip advances the DRAM address only between rows. */
			if (i + 1 < rows)
				source += skip;
		}

		/* Rows 9 and 10: exposed final register poststates. */
		*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = source & 0x00ffffff;
		*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = (region << 12) | dest_offset;

		if (diagnostics_eligible)
		{
			observation.imem_after_hash = dma_hash_imem(rsp->imem);
			dma_imem_samples(rsp->imem, observation.imem_after_samples);
			RSP::Diagnostics::trace_rsp_dma_read(observation);
		}

		return rsp->dirty_blocks ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

	static int rsp_dma_read(RSP::CPUState *rsp)
	{
		uint32_t length_reg = *rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH];
		uint32_t length = (length_reg & 0xFFF) + 1;
		const uint32_t requested_length = length;
		uint32_t skip = (length_reg >> 20) & 0xFFF;
		unsigned count = (length_reg >> 12) & 0xFF;
		const bool diagnostics_enabled = RSP::Diagnostics::enabled();
		RSP::Diagnostics::DmaReadObservation observation;

		if (diagnostics_enabled)
		{
			memset(&observation, 0, sizeof(observation));
			observation.raw_dma_cache =
			    *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE];
			observation.raw_dma_dram = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM];
			observation.raw_read_length = length_reg;
			observation.requested_length = requested_length;
			observation.count = count;
			observation.transfer_count = count + 1;
			observation.skip = skip;
		}

		/*
		 * Explicit DD runtime policy (P03 seam).  The corrected arm runs
		 * only for an explicitly DD-authorized session; the legacy path
		 * below is the DD-off behavior and is kept byte-for-byte identical.
		 * Raw operands were snapshotted above before any alignment or
		 * register mutation.
		 */
		if (RSP::DdRuntimePolicyEnabled() != 0)
			return rsp_dma_read_corrected(
			    rsp, length_reg, observation, diagnostics_enabled);

		// Force alignment.
		length = (length + 0x7) & ~0x7;
		if (diagnostics_enabled)
			observation.aligned_length = length;
		*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] &= ~0x3;
		*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] &= ~0x7;

		// Check length.
		if (((*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF) + length) > 0x1000)
			length = 0x1000 - (*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF);
		if (diagnostics_enabled)
			observation.effective_length = length;

		unsigned i = 0;
		uint32_t source = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM];
		uint32_t dest = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE];
		const bool diagnostics_eligible =
		    diagnostics_enabled
		    && RSP::Diagnostics::imem_dma_eligible(
		           dest, length, count + 1);

		if (diagnostics_eligible)
		{
			observation.payload_hash = DMA_FNV_OFFSET;
			observation.imem_before_hash = dma_hash_imem(rsp->imem);
			dma_imem_samples(rsp->imem, observation.imem_before_samples);
		}

#ifdef INTENSE_DEBUG
		fprintf(stderr, "DMA READ: (0x%x <- 0x%x) len %u, count %u, skip %u\n", dest & 0x1ffc, source & 0x7ffffc,
		        length, count + 1, skip);
#endif

		do
		{
			unsigned j = 0;
			do
			{
				uint32_t source_addr = (source + j) & 0x7FFFFC;
				uint32_t dest_addr = (dest + j) & 0x1FFC;
				uint32_t word = rsp->rdram[source_addr >> 2];
				if (diagnostics_eligible)
				{
					dma_payload_word(observation, source_addr, word);
					if (dest_addr & 0x1000)
						dma_imem_range(observation, dest_addr);
				}

				if (dest_addr & 0x1000)
				{
					// Invalidate IMEM.
					unsigned block = (dest_addr & 0xfff) / CODE_BLOCK_SIZE;
					rsp->dirty_blocks |= (0x3 << block) >> 1;
					//rsp->dirty_blocks = ~0u;
					rsp->imem[(dest_addr & 0xfff) >> 2] = word;
				}
				else
					rsp->dmem[dest_addr >> 2] = word;

				j += 4;
			} while (j < length);

			source += length + skip;
			dest += length;
		} while (++i <= count);

		*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = source;
		*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = dest;

		if (diagnostics_eligible)
		{
			observation.imem_after_hash = dma_hash_imem(rsp->imem);
			dma_imem_samples(rsp->imem, observation.imem_after_samples);
			RSP::Diagnostics::trace_rsp_dma_read(observation);
		}

#ifdef INTENSE_DEBUG
		log_rsp_mem_parallel();
#endif
		return rsp->dirty_blocks ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

	static void rsp_dma_write(RSP::CPUState *rsp)
	{
		uint32_t length_reg = *rsp->cp0.cr[CP0_REGISTER_DMA_WRITE_LENGTH];
		uint32_t length = (length_reg & 0xFFF) + 1;
		uint32_t skip = (length_reg >> 20) & 0xFFF;
		unsigned count = (length_reg >> 12) & 0xFF;

		// Force alignment.
		length = (length + 0x7) & ~0x7;
		*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] &= ~0x3;
		*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] &= ~0x7;

		// Check length.
		if (((*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF) + length) > 0x1000)
			length = 0x1000 - (*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF);

		uint32_t dest = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM];
		uint32_t source = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE];

#ifdef INTENSE_DEBUG
		fprintf(stderr, "DMA WRITE: (0x%x <- 0x%x) len %u, count %u, skip %u\n", dest & 0x7ffffc, source & 0x1ffc,
		        length, count + 1, skip);
#endif

		unsigned i = 0;
		do
		{
			unsigned j = 0;

			do
			{
				uint32_t source_addr = (source + j) & 0x1FFC;
				uint32_t dest_addr = (dest + j) & 0x7FFFFC;

				rsp->rdram[dest_addr >> 2] =
				    (source_addr & 0x1000) ? rsp->imem[(source_addr & 0xfff) >> 2] : rsp->dmem[source_addr >> 2];

				j += 4;
			} while (j < length);

			source += length;
			dest += length + skip;
		} while (++i <= count);

		*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = source;
		*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = dest;
#ifdef INTENSE_DEBUG
		log_rsp_mem_parallel();
#endif
	}
#endif

	int RSP_MTC0(RSP::CPUState *rsp, unsigned rd, unsigned rt)
	{
		uint32_t val = rsp->sr[rt];

		switch (static_cast<CP0Registers>(rd & 15))
		{
		case CP0_REGISTER_DMA_CACHE:
			*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = val & 0x1fff;
			break;

		case CP0_REGISTER_DMA_DRAM:
			*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = val & 0xffffff;
			break;

		case CP0_REGISTER_DMA_READ_LENGTH:
			*rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH] = val;
#ifdef PARALLEL_INTEGRATION
			return rsp_dma_read(rsp);
#else
			return MODE_DMA_READ;
#endif

		case CP0_REGISTER_DMA_WRITE_LENGTH:
			*rsp->cp0.cr[CP0_REGISTER_DMA_WRITE_LENGTH] = val;
#ifdef PARALLEL_INTEGRATION
			rsp_dma_write(rsp);
#endif
			break;

		case CP0_REGISTER_SP_STATUS:
			return rsp_status_write(rsp, val);

		case CP0_REGISTER_SP_RESERVED:
			// CXD4 forces this to 0.
			*rsp->cp0.cr[CP0_REGISTER_SP_RESERVED] = 0;
			break;

		case CP0_REGISTER_CMD_START:
#ifdef INTENSE_DEBUG
			fprintf(stderr, "CMD_START 0x%x\n", val & 0xfffffff8u);
#endif
			*rsp->cp0.cr[CP0_REGISTER_CMD_START] = *rsp->cp0.cr[CP0_REGISTER_CMD_CURRENT] =
			    *rsp->cp0.cr[CP0_REGISTER_CMD_END] = val & 0xfffffff8u;
			break;

		case CP0_REGISTER_CMD_END:
#ifdef INTENSE_DEBUG
			fprintf(stderr, "CMD_END 0x%x\n", val & 0xfffffff8u);
#endif
			*rsp->cp0.cr[CP0_REGISTER_CMD_END] = val & 0xfffffff8u;

#ifdef PARALLEL_INTEGRATION
			RSP::rsp.ProcessRdpList();
#endif
			break;

		case CP0_REGISTER_CMD_CLOCK:
			*rsp->cp0.cr[CP0_REGISTER_CMD_CLOCK] = val;
			break;

		case CP0_REGISTER_CMD_STATUS:
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x1) << 0);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x2) << 0);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x4) << 1);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x8) << 1);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] &= ~(!!(val & 0x10) << 2);
			*rsp->cp0.cr[CP0_REGISTER_CMD_STATUS] |= (!!(val & 0x20) << 2);
			*rsp->cp0.cr[CP0_REGISTER_CMD_TMEM_BUSY] &= !(val & 0x40) * -1;
			*rsp->cp0.cr[CP0_REGISTER_CMD_CLOCK] &= !(val & 0x200) * -1;
			break;

		case CP0_REGISTER_CMD_CURRENT:
		case CP0_REGISTER_CMD_BUSY:
		case CP0_REGISTER_CMD_PIPE_BUSY:
		case CP0_REGISTER_CMD_TMEM_BUSY:
			break;

		default:
			*rsp->cp0.cr[rd & 15] = val;
			break;
		}

		return MODE_CONTINUE;
	}
}
