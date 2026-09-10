#include "../state.hpp"

#include <cstdio>

#ifdef PARALLEL_INTEGRATION
#include "../rsp_1.1.h"
#include "m64p_plugin.h"
namespace RSP
{
extern RSP_INFO rsp;
extern short MFC0_count[32];
extern int SP_STATUS_TIMEOUT;
} // namespace RSP
#endif

#ifdef PARALLEL_INTEGRATION
/* DD-route gate (runtime IsDDPresent()); defined in parallel.cpp. */
extern "C" int rsp_ares_budget_enabled(void);

/* ROUND-12 DIAG (DD route only).  The post-load failure writes a WORD-ALIGNED
   copy of the audio ucode's data table -- a long run of 0x00010001 at
   RDRAM 0x8076b268, i.e. ucode+0x2408 -- over live RSP memory (IMEM[0] and the
   DMEM 0xFC0 task header both end up 0x00010001).  The RSP then executes data,
   and its wild DMAs overwrite RDRAM 0..0x400 -- the exception vectors -- which
   is what kills the CPU (192M COP1-unusable faults, c_exc_int frozen at 12680).
   The core's own do_sp_dma log (wd_dma2.txt) shows only four clean transfer
   shapes for the whole run, so the damaging transfer is issued by the ucode
   through THIS path.  Keep a 128-entry RAM ring of every ucode-issued transfer
   and dump it (bounded) the first few times one looks like the culprit, so the
   source address, length and count are on record without per-transfer file I/O
   in the RSP's hot path. */
struct r12_ent { uint32_t dst, src, len, cnt, skip, s0, s1; unsigned dir; };
static struct r12_ent r12_ring[128];
static unsigned r12_idx = 0;
static unsigned r12_dumps = 0;

static void r12_record(RSP::CPUState* rsp, unsigned dir, uint32_t dst, uint32_t src,
                       uint32_t len, unsigned count, uint32_t skip)
{
	if (!rsp_ares_budget_enabled())
		return;

	struct r12_ent* e = &r12_ring[r12_idx & 127];
	e->dir = dir; e->dst = dst; e->src = src; e->len = len; e->cnt = count; e->skip = skip;
	e->s0 = rsp->rdram[(src & 0x7FFFFC) >> 2];
	e->s1 = rsp->rdram[((src + 4) & 0x7FFFFC) >> 2];
	r12_idx++;

	/* Trigger only on the two shapes that damage the machine:
	   a read whose RDRAM source starts with the ucode's 1-table, and a write
	   that lands in RDRAM's first page (the exception vectors). */
	if (dir == 2) { if (e->s0 != 0x00010001u) return; }
	else          { if ((dst & 0x7FFFFC) >= 0x1000) return; }
	if (r12_dumps >= 4)
		return;
	r12_dumps++;

	FILE* f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_pdma.txt",
	                (r12_dumps == 1) ? "w" : "a");
	if (f == NULL)
		return;
	fprintf(f, "R12DMA dump=%u idx=%u trig=%s dst=%08x src=%08x len=%u cnt=%u skip=%u s0=%08x s1=%08x\n",
	        r12_dumps, r12_idx, (dir == 2) ? "RD" : "WR", dst, src, len, count, skip, e->s0, e->s1);
	unsigned k;
	for (k = 0; k < 128; k++) {
		struct r12_ent* q = &r12_ring[(r12_idx + k) & 127];
		if (q->len == 0 && q->dst == 0 && q->src == 0) continue;
		fprintf(f, "  %s dst=%08x src=%08x len=%u cnt=%u skip=%u s0=%08x s1=%08x\n",
		        (q->dir == 2) ? "RD" : "WR", q->dst, q->src, q->len, q->cnt, q->skip, q->s0, q->s1);
	}
	fprintf(f, "  live pc=%04x status=%08x imem0=%08x %08x dmem0=%08x fc0=%08x\n",
	        rsp->pc & 0xfff, *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS],
	        rsp->imem[0], rsp->imem[1], rsp->dmem[0], rsp->dmem[0xfc0 / 4]);
	fclose(f);
}

/* ---------------------------------------------------------------------------
   ROUND-14 DIAG (DD route only): the COMPLETE ucode-issued DMA trace.

   Round 13 left two open questions that need the transfer stream in order:
   (a) which instruction issues the transfer that lands 0x00010001 over IMEM
   (measuring `dst=1000 src=000f80 len=152`, i.e. a 152-byte read out of the
   guest's low-RDRAM fill), and (b) what takes RSP memory from "IMEM full of
   ucode text, DMEM full of tables" to ALL ZERO -- the state the machine is
   frozen in (every ENTER afterwards reports imem0=0 and the RSP runs NOPs).

   A ring of the last 256 transfers is kept in RAM and flushed to the file in
   batches of 64, so the RSP's hot path pays a few stores per transfer and no
   per-transfer file I/O (user rule: never put file I/O in the RSP path).
   Each line carries the RSP pc, the four DMA registers, the first source
   word, SP_STATUS and the task-header word, which is what identifies both
   halves: the issuing instruction and the memory state it acted on. */
struct r14_ent {
	unsigned dir; uint32_t pc, dst, src, len, cnt, skip, s0, st, fc0;
	/* ROUND 15: the two DMEM words the F3DEX2/F3DLX2 entry code keys its
	   fresh/warm/resume decision on, sampled per transfer:
	     DMEM[0x0F0] = the "already initialised" marker (the ucode stores the
	                   RDP end pointer there on a cold start),
	     DMEM[0xFF0] = the task header's data_ptr, which the ucode loads into
	                   k0 for its display-list walk.
	   With both on every line the DMEM[0xFF0] history can be read straight off
	   the transfer stream and compared with the source address the ucode
	   actually fetched from. */
	uint32_t f0, ff0, bf8, fc4;
};
static struct r14_ent r14_ring[256];
static unsigned r14_idx = 0;
static void r14_note(FILE* f);

static void r14_flush(void)
{
	unsigned i, start;
	FILE* f;
	if (!rsp_ares_budget_enabled() || r14_idx == 0)
		return;
	start = r14_idx - (r14_idx < 64 ? r14_idx : 64);
	f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_dmatr.txt",
	          r14_idx > 64 ? "a" : "w");
	if (f == NULL)
		return;
	for (i = start; i < r14_idx; i++) {
		const struct r14_ent* e = &r14_ring[i & 255];
		fprintf(f, "D%u %s pc=%03x dst=%04x src=%06x len=%04x cnt=%u skip=%u s0=%08x st=%08x fc0=%08x f0=%08x ff0=%08x bf8=%08x fc4=%08x\n",
		        i + 1, e->dir == 2 ? "RD" : "WR", e->pc, e->dst, e->src,
		        (unsigned)e->len, (unsigned)e->cnt, (unsigned)e->skip,
		        (unsigned)e->s0, (unsigned)e->st, (unsigned)e->fc0,
		        (unsigned)e->f0, (unsigned)e->ff0, (unsigned)e->bf8, (unsigned)e->fc4);
	}
	r14_note(f);
	fclose(f);
}

static void r14_record(RSP::CPUState* rsp, unsigned dir, uint32_t dst, uint32_t src,
                       uint32_t len, unsigned count, uint32_t skip)
{
	struct r14_ent* e;
	if (!rsp_ares_budget_enabled())
		return;
	e = &r14_ring[r14_idx & 255];
	e->dir = dir; e->pc = rsp->pc & 0xfff; e->dst = dst; e->src = src;
	e->len = len; e->cnt = count; e->skip = skip;
	e->s0 = rsp->rdram[(src & 0x7ffffc) >> 2];
	e->st = *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS];
	e->fc0 = ((uint32_t*)RSP::rsp.DMEM)[0xfc0 / 4];
	e->f0 = ((uint32_t*)RSP::rsp.DMEM)[0x0f0 / 4];
	e->ff0 = ((uint32_t*)RSP::rsp.DMEM)[0xff0 / 4];
	e->bf8 = ((uint32_t*)RSP::rsp.DMEM)[0xbf8 / 4];
	e->fc4 = ((uint32_t*)RSP::rsp.DMEM)[0xfc4 / 4];
	r14_idx++;
	if ((r14_idx & 63) == 0)
		r14_flush();
}

/* ROUND-14 DIAG: RSP-side SP_STATUS writes, RAM ring only (see above). */
static uint32_t r14_spw_ring[16][2];
static unsigned r14_spw_n = 0;
static unsigned r14_fake_n = 0;
static uint32_t r14_fake_last_pc = 0, r14_fake_last_st = 0;

/* Called from r14_flush(): one line per batch naming the RSP-side SP_STATUS
   conversation (the last write's raw value + resulting status) and the count
   of synthetic yields the DD route injected, so the two halves of the task
   protocol can be lined up with the transfer stream. */
static void r14_note(FILE* f)
{
	unsigned i = (r14_spw_n - 1u) & 15u;
	fprintf(f, "X sw_n=%u sw_last=%08x sw_after=%08x fake_n=%u fake_pc=%03x fake_st=%08x\n",
	        r14_spw_n, r14_spw_n ? r14_spw_ring[i][0] : 0,
	        r14_spw_n ? r14_spw_ring[i][1] : 0,
	        r14_fake_n, r14_fake_last_pc, r14_fake_last_st);
}

/* ROUND-15 DIAG (DD route only, ONE-SHOT): the first DMA read whose RDRAM
   source lands in the low 1MB -- i.e. the ucode fetching a "display list" out
   of the guest's zero page / boot framebuffer fill instead of its pool.  The
   whole register file plus the DMEM regions the F3DEX2/F3DLX2 entry code and
   display-list walk keep their pointers in are written once, so the issuing
   instruction and its inputs are both visible.  One-shot: a single fopen on a
   path that fires at most once per run cannot perturb the timing the way
   per-transfer logging does (round 14: per-SP_STATUS-write logging killed the
   run with SIGILL in the RSP JIT). */
static unsigned r15_bad_dumped = 0;
static void r15_bad_dump(RSP::CPUState* rsp, uint32_t src, uint32_t dst, uint32_t len)
{
	FILE* f;
	unsigned i;
	char path[128];
	if (r15_bad_dumped >= 8 || !rsp_ares_budget_enabled())
		return;
	snprintf(path, sizeof(path), "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_bad%u.txt", r15_bad_dumped + 1);
	r15_bad_dumped++;
	f = fopen(path, "w");
	if (f == NULL)
		return;
	fprintf(f, "BADRD pc=%04x src=%06x dst=%04x len=%04x status=%08x\n",
	        rsp->pc & 0xfff, src, dst, len, *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS]);
	fprintf(f, "  spregs mem=%08x dram=%08x rdlen=%08x wrlen=%08x cmd_start=%08x cmd_end=%08x cmd_cur=%08x\n",
	        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_CACHE], *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_DRAM],
	        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_READ_LENGTH], *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_WRITE_LENGTH],
	        *rsp->cp0.cr[RSP::CP0_REGISTER_CMD_START], *rsp->cp0.cr[RSP::CP0_REGISTER_CMD_END],
	        *rsp->cp0.cr[RSP::CP0_REGISTER_CMD_CURRENT]);
	fprintf(f, "  regs ");
	for (i = 0; i < 32; i++)
		fprintf(f, "%s=%08x ", (i % 8 == 0) ? "\n   " : "", rsp->sr[i]);
	fprintf(f, "\n  dmem ");
	for (i = 0; i < 0x1000 / 4; i += 4)
	{
		if ((i & 0x3f) == 0)
			fprintf(f, "\n   %03x:", i * 4);
		fprintf(f, " %08x", ((uint32_t*)rsp->dmem)[i]);
	}
	fprintf(f, "\n  imem ");
	for (i = 0; i < 0x1000 / 4; i += 16)
		fprintf(f, "\n   %03x: %08x %08x %08x %08x", 0x1000 + i * 4,
		        ((uint32_t*)rsp->imem)[i], ((uint32_t*)rsp->imem)[i + 1],
		        ((uint32_t*)rsp->imem)[i + 2], ((uint32_t*)rsp->imem)[i + 3]);
	fprintf(f, "\n");
	fclose(f);
}

#endif

using namespace RSP;

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
			/* RUNTIME DD gate: IsDDPresent() is evaluated at task time (after
			   init_device set dd.idisk), so plain cart games fall through to
			   the stock 0x7fff-HALT path below.  ForceSynchronize itself is
			   wired for every game but no-ops without a disk attached. */
			if (RSP::rsp.IsDDPresent && RSP::rsp.IsDDPresent())
			{
			/* DD-ONLY ares path. */
			/* ares cpu.forceSynchronize() equivalent: on every SP_STATUS
			   read, advance core CP0 time to the next pending peripheral event
			   so a queued DMA/interrupt completes and the guest's wait wakes.
			   On real HW the CPU keeps running while the RSP polls; in the
			   synchronous model time is frozen during DoRspCycles, so the
			   pending event would otherwise never become due. */
			RSP::rsp.ForceSynchronize();

			/* ares-like RSP yield at the ucode's SP_STATUS read (ares
			   force-synchronizes the CPU on this read): in the synchronous
			   model the CPU only runs when the RSP yields, so a single
			   fixed 0x7fff threshold makes the game's yield protocol turns
			   too coarse and the loader stalls at ~6/8.  Give non-audio
			   tasks a frequent turn (every 256 polls); audio (clean type 2)
			   keeps the large threshold (its own DSP wait protocol).

			   ROUND 17 (DD route only): the GFX task (type 1) was given the
			   stock 0x7fff-scale wait in a first attempt to keep the ucode's
			   own SIG0 handshake in charge.  MEASURED AND REVERTED: with the
			   gfx task left unprompted the ucode walks its list and then
			   parks forever -- wd_stall of that build (r17d) reads
			   `RDPKICK n=0`, `FRAME loads t1gfx=1`, `raise_bits VI=271`
			   (vs 6087 before), i.e. the gfx task never reached DPC_END and
			   the whole frame protocol died with it.  The host-side
			   preemption is load-bearing for this game's 64DD path and stays
			   exactly as it was; the round-17 fix instead removes the
			   *consequence* of a forced yield (the stale DMEM[0xBF8] k0) in
			   parallel.cpp, where it does not change how often the task is
			   preempted. */
			unsigned task_type = ((uint32_t*)RSP::rsp.DMEM)[0xfc0 / 4];
			unsigned threshold = (task_type == 2) ? (unsigned)RSP::SP_STATUS_TIMEOUT : 256u;
			if (RSP::MFC0_count[rt] >= threshold)
			{
			// The ucode is polling SP_STATUS (typically waiting for the
			// CPU to signal via INTR_BREAK / task-done).  On real hardware
			// the RSP spins here while the CPU continues; in the
			// synchronous emulation model we must terminate the wait or
			// the emulation thread is stuck inside DoRspCycles forever.
			// Set the bits the libultra ucode polls for (INTR_BREAK) PLUS
			// HALT (so DoRspCycles exits) and raise the RSP interrupt so
			// the CPU-side wait (osSpTaskStart / SP_STATUS poll at the
			// game's side) completes.  This is the CXD4 "CPU host" model:
			// the CPU took over the timeline.
			*RSP::rsp.SP_STATUS_REG |= SP_STATUS_INTR_BREAK | SP_STATUS_HALT;
			/* ROUND 10 (DD-GATED: this whole block only runs when a 64DD
			   disk is attached).  ACKNOWLEDGE A PENDING LIBULTRA YIELD.
			   osSpTaskYield() asks the ucode to yield by setting SIG0
			   (SP_STATUS_YIELD), and libultra's osSpTaskYielded() reports
			   OS_TASK_YIELDED only when the ucode answers with SIG1
			   (SP_STATUS_YIELDED).  Forcing the CPU handover WITHOUT that
			   acknowledgement makes osSpTaskYielded() return 0, so the game
			   never sets its "gfx task yielded" flag, never calls
			   Sched_SpTaskResumeGfx(), and the interrupted GFX task is
			   abandoned forever: the audio task it swung to keeps
			   re-triggering the yield path while the gfx task is never run
			   again, so the RDP is never kicked and the game's DP event
			   never fires (measured on the RP6: 193 audio tasks vs 28 gfx
			   tasks, the gfx thread parked in osRecvMesg on D_800DCAC8, and
			   raise_bits DP=0 for the entire run).  Answer the request the
			   way a real F3DEX2 ucode does at its yield point.

			   ROUND 16: answer it with the SAME bit pattern the ucode uses.
			   The game's boot ucode (this ROM, RDRAM 0x7504F0) acks a yield
			   with `ori t0,r0,0x5200; mtc0 t0,SP_STATUS; break`, i.e.
			   SP_CLR_SIG0 | SP_SET_SIG1 | SP_SET_SIG2: it CLEARS the YIELD
			   request while setting YIELDED + TASKDONE.  The round-10 form
			   (OR in SIG1, leave SIG0 set) is not reachable in the ucode and
			   is actively harmful: libultra's osSpTaskYielded() only does
			   `tp->t.flags |= OS_TASK_YIELDED; tp->t.flags &= ~OS_TASK_DP_WAIT;`
			   while SIG0 is STILL set, and the boot ucode gates its
			   ucode_data -> DMEM DMA on the DP_WAIT bit -- so that
			   combination makes the next resume of the task run on stale
			   DMEM (round 16 also defends against the consequence in
			   parallel.cpp; this removes the cause). */
			if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_SIG0)
				*RSP::rsp.SP_STATUS_REG =
				    (*RSP::rsp.SP_STATUS_REG | SP_STATUS_SIG1 | SP_STATUS_SIG2) & ~SP_STATUS_SIG0;
			*rsp->cp0.irq |= 1;
			/* ROUND-14 DIAG: count the synthetic yields this block
			   fabricates (RAM only -- no file I/O in the RSP path). */
			r14_fake_n++;
			r14_fake_last_pc = rsp->pc & 0xfff;
			r14_fake_last_st = *RSP::rsp.SP_STATUS_REG;
			return MODE_CHECK_FLAGS;
			}
			}
			else if (RSP::MFC0_count[rt] >= RSP::SP_STATUS_TIMEOUT)
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
		/* ROUND-14: no file I/O here.  The ucode writes SP_STATUS from its
		   inner loops, and round 14 measured that per-write file I/O on
		   this path changes the run (the emulation process died with
		   SIGILL in the RSP JIT ~4s in, a state round 13's tree never
		   reached).  Keep a RAM-only ring of the last 16 writes for the
		   dump instead. */
		r14_spw_ring[r14_spw_n & 15u][0] = rt;
		r14_spw_ring[r14_spw_n & 15u][1] = status;
		r14_spw_n++;
		return ((*rsp->cp0.irq & 1) || (status & SP_STATUS_HALT)) ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

#ifdef PARALLEL_INTEGRATION
	static int rsp_dma_read(RSP::CPUState *rsp)
	{
		uint32_t length_reg = *rsp->cp0.cr[CP0_REGISTER_DMA_READ_LENGTH];
		uint32_t length = (length_reg & 0xFFF) + 1;
		uint32_t skip = (length_reg >> 20) & 0xFFF;
		unsigned count = (length_reg >> 12) & 0xFF;

		// Force alignment.
		length = (length + 0x7) & ~0x7;
		*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] &= ~0x3;
		*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] &= ~0x7;

		/* DMA length.  Upstream parallel-RSP CLAMPS a transfer that runs off
		   the end of DMEM/IMEM; real hardware WRAPS (the per-word address is
		   masked with 0x1FFC, which is what the inner loop below does).  The
		   64DD boot ucode depends on the wrap: it issues
		   SP_MEM_ADDR=0x1080 / SP_RD_LEN=0xF7F, i.e. a 4096-byte load of the
		   whole main ucode into IMEM starting at 0x80 and wrapping into
		   0x00..0x7F.  Clamping truncates it to 3968 bytes and leaves the
		   ucode's first 128 bytes (IMEM 0x000..0x07F) holding whatever was
		   there before, so the RSP resumes into garbage and burns its whole
		   host budget instead of running the ucode.  DD route only: plain
		   games keep the stock clamp exactly (user rule 2026-09-05). */
		if (!rsp_ares_budget_enabled() &&
		    ((*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF) + length) > 0x1000)
			length = 0x1000 - (*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF);

		unsigned i = 0;
		uint32_t source = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM];
		uint32_t dest = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE];

		r12_record(rsp, 2, dest, source, length, count, skip);
		r14_record(rsp, 2, dest, source, length, count, skip);

		/* ROUND-15: first fetch out of the low 1MB (guest zero page / the
		   boot framebuffer fill) -- capture the full RSP state once. */
		if (source < 0x100000)
			r15_bad_dump(rsp, source, dest, length);

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

		/* Same hardware-wrap fix as rsp_dma_read (DD route only). */
		if (!rsp_ares_budget_enabled() &&
		    ((*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF) + length) > 0x1000)
			length = 0x1000 - (*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF);

		uint32_t dest = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM];
		uint32_t source = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE];

		r12_record(rsp, 1, dest, source, length, count, skip);
		r14_record(rsp, 1, dest, source, length, count, skip);

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
