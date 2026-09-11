#include "../state.hpp"

#include <cstdio>

#ifdef PARALLEL_INTEGRATION
#include "../rsp_1.1.h"
#include "m64p_plugin.h"
namespace RSP
{

	/* ROUND 30 TRACE (defined in parallel.cpp): the ordered log of RSP-initiated
	   transfers.  Declared here because the DMA helpers live in this file. */
	extern "C" void r30_dma_line(unsigned dir, uint32_t dst, uint32_t src, uint32_t len,
	                             unsigned pc);

extern RSP_INFO rsp;
extern short MFC0_count[32];
extern int SP_STATUS_TIMEOUT;
} // namespace RSP
#endif

#ifdef PARALLEL_INTEGRATION
/* DD-route gate (runtime IsDDPresent()); defined in parallel.cpp. */
extern "C" int rsp_ares_budget_enabled(void);

/* ROUND 36: opt-in gate for the per-transfer traces (also in parallel.cpp).
   Absent `files/wd_trace.flag` -> every heavy trace returns immediately. */
extern "C" int rsp_diag_trace(void);

/* ROUND 37: the two UNBOUNDED per-transfer/per-preemption traces have their
   own flag (`wd_deep.flag`), so the bounded watches can run alone. */
extern "C" int rsp_diag_deep(void);

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
	     DMEM[0x0F0] = the "already initialized" marker (the ucode stores the
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
	/* ROUND 36: opt-in only -- this single trace wrote 2.9 GB per DD run.
	   ROUND 37: it moved to the DEEPER flag (`wd_deep.flag`, see
	   rsp_diag_deep() in ../parallel.cpp) so the bounded watches can be
	   enabled without paying 19 MB/s of fprintf in the RSP's hot path. */
	if (!rsp_diag_deep())
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

/* ---------------------------------------------------------------------------
   ROUND 19: the two "firsts" that decide the 64DD route's frame protocol.

   Round 18 named the run-killer (a runaway display-list walk) but not what
   starts it.  Two measurements pin the origin down, both cheap and both DD
   gated:

   (1) WHO WRITES IMEM.  The watchdog reports IMEM[0..3] as the game's
       cleared-buffer fill 0x00010001 (the ucode's program is gone) while the
       DMA trace shows NO core-side (CPU) SP DMA into IMEM late in the run, so
       the writer is the RSP side.  Every transfer with bit 12 of SP_MEM_ADDR
       set writes IMEM; they are rare (whole-ucode loads only), so a small RAM
       ring is affordable and the first eight are flushed once.

   (2) WHETHER THE RDP EVER GOT A REAL LIST.  parallel-RDP raises MI_INTR_DP
       only for RDP::Op::SyncFull inside DPC_CURRENT..DPC_END, and the DD
       route's DPC_START reads 0xFFFFFFF8 -- with CURRENT=0xFFFFFFF8 >
       END=0x13C0 every kick is discarded before a single command is looked at,
       so the guest's DP_WAIT gfx task can never complete.  Latch the first
       CMD_START/CMD_END writes (what the ucode actually programmed) together
       with the DMEM the ucode computed them from. */
/* Would the stock `& 0x1FFC` mask have carried this transfer across the
   4 KiB bank boundary (i.e. into the other bank, destroying it)?  Counts once
   for DMEM->IMEM and once for IMEM->DMEM, with the first occurrence on
   record; DD-gated and RAM-only apart from that single latch. */
static unsigned r19_wrap_d2i_n = 0, r19_wrap_i2d_n = 0;
static uint32_t r19_wrap_first[8] = {0,0,0,0,0,0,0,0};
extern "C" void r19_bankwrap_note(uint32_t dst, uint32_t length, unsigned count, uint32_t skip, uint32_t pc)
{
	uint32_t end = (dst & 0xFFFu) + (uint32_t)(count + 1u) * length;
	if (end <= 0x1000u)
		return;
	if (dst & 0x1000u)
	{
		r19_wrap_i2d_n++;
		if (r19_wrap_i2d_n != 1) return;
	}
	else
	{
		r19_wrap_d2i_n++;
		if (r19_wrap_d2i_n != 1) return;
	}
	r19_wrap_first[0] = dst; r19_wrap_first[1] = length; r19_wrap_first[2] = count;
	r19_wrap_first[3] = skip; r19_wrap_first[4] = pc; r19_wrap_first[5] = end;
	{
		FILE* f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_wrap.txt", "a");
		if (f)
		{
			fprintf(f, "R19WRAP %s dst=%04x len=%04x cnt=%u skip=%u end=%05x pc=%03x\n",
			        (dst & 0x1000u) ? "IMEM->DMEM" : "DMEM->IMEM",
			        dst, length, count, skip, end, pc);
			fclose(f);
		}
	}
}
extern "C" unsigned r19_bankwrap_d2i(void) { return r19_wrap_d2i_n; }
extern "C" unsigned r19_bankwrap_i2d(void) { return r19_wrap_i2d_n; }
extern "C" const uint32_t* r19_bankwrap_first(void) { return r19_wrap_first; }

static unsigned r19_imem_n = 0;
static void r19_imem_note(RSP::CPUState* rsp, uint32_t dst, uint32_t src, uint32_t len)
{
	FILE* f;
	if (!(dst & 0x1000))
		return;
	if (r19_imem_n >= 8 || !rsp_ares_budget_enabled())
		return;
	f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_imem.txt",
	          r19_imem_n ? "a" : "w");
	r19_imem_n++;
	if (f == NULL)
		return;
	fprintf(f, "R19IMEM n=%u pc=%03x dst=%04x src=%06x len=%04x status=%08x "
	           "imem0=%08x %08x fc0=%08x f0=%08x ff0=%08x bf8=%08x "
	           "dmemfe0=%08x %08x %08x %08x %08x\n",
	        r19_imem_n, rsp->pc & 0xfff, dst, src, len,
	        *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS],
	        rsp->imem[0], rsp->imem[1],
	        rsp->dmem[0xfc0 / 4], rsp->dmem[0x0f0 / 4], rsp->dmem[0xff0 / 4], rsp->dmem[0xbf8 / 4],
	        rsp->dmem[0x2e0 / 4], rsp->dmem[0x2e4 / 4], rsp->dmem[0x2e8 / 4],
	        rsp->dmem[0xfdc / 4], rsp->dmem[0xfe0 / 4]);
	/* ROUND 25: the whole OSTask header as the ucode sees it, plus the low
	   DMEM page (the boot ucode's opcode-dispatch table lives in the first
	   0x100 bytes of the ucode DATA segment, which is DMA'd to DMEM 0x000 --
	   see doc/HANDOFF.md round 25), plus the descriptor area.  This fires at
	   most 8 times per run, at the exact instant an IMEM-writing DMA is
	   issued, i.e. the one moment at which "what the loader read" and "what
	   the ucode had left in DMEM" can both be seen. */
	{
		unsigned k;
		fprintf(f, "  hdr  fc0:");
		for (k = 0; k < 16; k++)
			fprintf(f, " %08x", rsp->dmem[0xfc0 / 4 + k]);
		fprintf(f, "\n");
		fprintf(f, "  desc 2e0:");
		for (k = 0; k < 8; k++)
			fprintf(f, " %08x", rsp->dmem[0x2e0 / 4 + k]);
		fprintf(f, "\n  desc 410:");
		for (k = 0; k < 4; k++)
			fprintf(f, " %08x", rsp->dmem[0x410 / 4 + k]);
		fprintf(f, "\n  low");
		for (k = 0; k < 0x100 / 4; k++)
		{
			if ((k & 7) == 0)
				fprintf(f, "\n   %03x:", k * 4);
			fprintf(f, " %08x", rsp->dmem[k]);
		}
		fprintf(f, "\n");
		/* ROUND 41: WHO PUT THE OVERLAY ADDRESS IN THE REGISTER.

		   Round 40 named the wrong value: the gfx overlay is DMA'd from
		   0x753AF8 on the first pass and from 0xEA65D8 = 0x753AF8 + 0x752AE0
		   (the ucode base) on the second -- an address that had the base
		   added twice.  Round 40 also proved it is NOT the plugin doing the
		   add on delivery (the R40_NORM normalisation fires and changes
		   nothing), and the two candidate sources -- the DMEM 0x2E0/0x2E8
		   descriptor and the OSTask header -- both hold *other* values at
		   the instant of the load.  So the add happens in the RSP's own
		   code, in a register, and the only way to see it is the register
		   file plus the whole of DMEM at the moment the transfer is issued.

		   DMEM is dumped in full (1024 words) so the word that holds the
		   *offset* (0x1018 the first time, 0x753AF8 the second) can be
		   found by diffing one latch against the next, and all 32 GPRs are
		   dumped so the add itself -- base in one register, offset in
		   another, sum being transferred -- is readable directly.  Still
		   DD-gated and still at most eight times per run. */
		fprintf(f, "  gpr");
		for (k = 0; k < 32; k++)
		{
			if ((k & 7) == 0)
				fprintf(f, "\n   r%02d:", k);
			fprintf(f, " %08x", rsp->sr[k]);
		}
		fprintf(f, "\n  rdram750000:");
		for (k = 0; k < 0x4000 / 4; k++)
		{
			if ((k & 7) == 0)
				fprintf(f, "\n   %06x:", 0x750000 + k * 4);
			fprintf(f, " %08x", rsp->rdram ? rsp->rdram[(0x750000 + k * 4) / 4] : 0u);
		}
		fprintf(f, "\n  dmemall");
		for (k = 0; k < 0x1000 / 4; k++)
		{
			if ((k & 7) == 0)
				fprintf(f, "\n   %03x:", k * 4);
			fprintf(f, " %08x", rsp->dmem[k]);
		}
		fprintf(f, "\n");
	}
	fclose(f);
}

/* ===========================================================================
   ROUND 36: NAME THE STORE THAT HANDS THE RDP A WINDOW OUTSIDE RDRAM.

   Rounds 19..35 measured the *consequence*: the last DPC_END the FIFO ucode
   programmes is 0xFC0012D8 (and 0xFC000640 / 0xFC000C88 before it), i.e. the
   top byte is 0xFC and the low 24 bits are a small offset -- while the twelve
   good kicks immediately before it walk 0x2F7B48 -> 0x2FC9B0 in ~0x5E0-byte
   steps inside the RDP output region [output_buff 0x2D9CD0, output_buff_end
   0x32DCD0).  parallel-RDP drops any window with END > 0x7FFFFFF (one of its
   three silent early returns), so DPC_CURRENT never catches up, the ucode's
   flush loop -- which spins on DPC_CURRENT before publishing the next 344-byte
   DMEM block -- hangs forever, no further DPC_END is ever written, the DP
   interrupt stops, and the guest's game thread stays blocked in
   osRecvMesg(&D_DD_CAC8) waiting for the 0x2A that EVENT_MESG_DP produces.

   The value itself is the only thing left to explain, so this records the RSP
   at the write instead of guessing: a 16-entry ring of the last CMD writes,
   and, on the first few out-of-RDRAM ones, the ENTIRE machine state -- all 32
   GPRs, all of DMEM and IMEM -- so the offending store and the DMEM word it
   loaded from can both be read off offline.  DD-gated: rsp_ares_budget_enabled()
   is the core's runtime IsDDPresent(), so plain carts only pay the ring push
   (16 words) and never take a dump.
   ======================================================================== */
#define R36_FILE "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r36"
static uint32_t r36_ring[16][3];        /* val, pc, what (0=START 1=END) */
static unsigned r36_ring_n = 0;
static unsigned r36_bad_n = 0;

static void r36_cmd_note(RSP::CPUState* rsp, unsigned what, uint32_t val)
{
	r36_ring[r36_ring_n & 15u][0] = val;
	r36_ring[r36_ring_n & 15u][1] = (uint32_t)(rsp->pc & 0xfffu);
	r36_ring[r36_ring_n & 15u][2] = what;
	r36_ring_n++;

	if (!rsp_ares_budget_enabled())      /* DD route only */
		return;
	if (val < 0x00800000u)               /* inside RDRAM: fine */
		return;
	if (r36_bad_n >= 4u)
		return;
	r36_bad_n++;

	{
		FILE* f = fopen(R36_FILE ".txt", (r36_bad_n == 1u) ? "w" : "a");
		if (f)
		{
			fprintf(f, "R36CMD n=%u what=%s val=%08x pc=%03x ring_n=%u "
			           "st=%08x dm=%08x cache=%08x d0=%08x dstart=%08x dend=%08x "
			           "f0=%08x fc0=%08x bf8=%08x 2e0=%08x 2e4=%08x ff0=%08x\n",
			        r36_bad_n, what ? "END" : "START", val, rsp->pc & 0xfffu, r36_ring_n,
			        *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS],
			        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_DRAM],
			        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_CACHE],
			        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_DRAM],
			        *rsp->cp0.cr[RSP::CP0_REGISTER_CMD_START],
			        *rsp->cp0.cr[RSP::CP0_REGISTER_CMD_END],
			        rsp->dmem[0x0f0 / 4], rsp->dmem[0x0fc0 / 4],
			        rsp->dmem[0x0bf8 / 4], rsp->dmem[0x02e0 / 4],
			        rsp->dmem[0x02e4 / 4], rsp->dmem[0x0ff0 / 4]);
			fclose(f);
		}
	}

	/* The full machine, binary: header + 32 GPRs + DMEM + IMEM + the ring. */
	if (r36_bad_n <= 4u)
	{
		FILE* f = fopen(R36_FILE ".bin", (r36_bad_n == 1u) ? "wb" : "ab");
		if (f)
		{
			uint32_t hdr[8], i;
			hdr[0] = 0x42363352u;               /* 'R36B' */
			hdr[1] = r36_bad_n;
			hdr[2] = what;
			hdr[3] = val;
			hdr[4] = (uint32_t)(rsp->pc & 0xfffu);
			hdr[5] = *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS];
			hdr[6] = *rsp->cp0.cr[RSP::CP0_REGISTER_CMD_START];
			hdr[7] = *rsp->cp0.cr[RSP::CP0_REGISTER_CMD_END];
			fwrite(hdr, 4, 8, f);
			fwrite(rsp->sr, 4, 32, f);
			fwrite(rsp->dmem, 4, 1024, f);
			fwrite(rsp->imem, 4, 1024, f);
			for (i = 0; i < 16u; i++)
				fwrite(r36_ring[i], 4, 3, f);
			fclose(f);
		}
	}
}

/* The first CMD_START/CMD_END the ucode programmes.  CMD_START is the one that
   matters: the plugin sets START=CURRENT=END from it, so a garbage START makes
   every later kick an empty window. */
static unsigned r19_cmd_n = 0;
static int r19_cmd_latch(RSP::CPUState* rsp, const char* what, uint32_t val)
{
	FILE* f;
	/* ROUND 39: DD route only.  This is reached on every DPC_START/DPC_END
	   write of every game.  ROUND 51: the cap was 16, which stopped long
	   before the failure; the plugin's own window log dies at flush ~151,
	   so the record must cover the whole run.  Raised to 400. */
	if (!rsp_ares_budget_enabled())
		return 0;
	if (r19_cmd_n >= 400)
		return 0;
	f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_cmd.txt",
	          r19_cmd_n ? "a" : "w");
	r19_cmd_n++;
	if (f == NULL)
		return 0;
	fprintf(f, "R19CMD n=%u %s val=%08x pc=%03x status=%08x imem0=%08x "
	           "fc0=%08x f0=%08x ff0=%08x bf8=%08x "
	           "dstk=%08x outbuf=%08x outend=%08x data=%08x datasz=%08x desc=%08x %08x\n",
	        r19_cmd_n, what, val, rsp->pc & 0xfff,
	        *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS],
	        rsp->imem[0],
	        rsp->dmem[0xfc0 / 4], rsp->dmem[0x0f0 / 4], rsp->dmem[0xff0 / 4], rsp->dmem[0xbf8 / 4],
	        rsp->dmem[0xfe4 / 4], rsp->dmem[0xfe8 / 4], rsp->dmem[0xfec / 4],
	        rsp->dmem[0xff0 / 4], rsp->dmem[0xff4 / 4],
	        rsp->dmem[0x2e0 / 4], rsp->dmem[0x2e4 / 4]);
	fclose(f);
	return 1;
}

/* ---------------------------------------------------------------------------
   ROUND 20: THE FIFO UCODE'S OWN YIELD PROTOCOL (and why the host must not
   fabricate one).

   Ground truth this round: the ucode this ROM actually submits is at RDRAM
   0x7505C0 (the guest's OSTask.ucode; its text is DMA'd to IMEM 0x080, length
   0xF80, so PC = 0x080 + (rdram - 0x7505C0)), and its two overlays follow the
   text at ucode+0xF80 (0x98 bytes) and ucode+0x1018 (0x170 bytes).  DMEM
   0x2E0/0x2E8 hold their descriptors, which the ucode's entry re-bases by
   adding DMEM[0xFD0] (the header's ucode pointer) on the cold/warm paths
   (IMEM 0x12C..0x15C) and which the YIELDED resume deliberately skips.

   Disassembled, the entry is:

     IMEM 0098  lw  t3,0xF0(r0)      # FIFO/RDP end pointer
     IMEM 009C  lw  t4,0xFC4(r0)     # task flags
     IMEM 00A0  addi at,r0,0x2800    # CLR_SIG1|CLR_SIG2
     IMEM 00A4  beq t3,r0,0x00C0     # -> COLD
     IMEM 00A8  mtc0 at,SP_STATUS    # (delay slot) clear YIELDED/TASKDONE
     IMEM 00AC  andi t4,t4,0x1
     IMEM 00B0  beq t4,r0,0x012C     # -> WARM (not yielded)
     IMEM 00B4  sw  r0,0xFC4(r0)
     IMEM 00B8  j   0x0164           # -> YIELDED RESUME
     IMEM 00BC  lw  k0,0xBF8(r0)     #      k0 = the ucode's OWN saved pointer

   and the main loop is

     IMEM 0160  lw  k0,0xFF0(r0)     # cold/warm: k0 = header data_ptr
     IMEM 0170  (DMA 744 bytes RDRAM[k0] -> DMEM 0x920; k0 += 0xA8)
     IMEM 018C  mfc0 at,SP_STATUS
     IMEM 0198  andi at,at,0x80      # SIG0 == "the CPU asked for a yield"
     IMEM 01A8  bne at,r0,0x0FAC     # SIG0 set -> the YIELD HANDLER
     IMEM 01B0  jr  t3               # else dispatch the command

   The yield handler is overlay A (RDRAM 0x751540 = ucode+0xF80, loaded over
   IMEM 0x000 by 0x0FAC and entered at PC 0):

     PC 0060  lw  t3,0xFD0(r0)       # ucode base
     PC 0064  sw  k0,0xBF8(r0)       # >>> SAVE THE DISPLAY-LIST POINTER
     PC 0068  sw  t3,0xBFC(r0)
     PC 0070  lw  t8,0xFF8(r0)       # yield_data_ptr
     PC 0078  addi s3,r0,0x0BFF
     PC 007C  j   0x0FD8             # DMA write DMEM[0..0xBFF] -> yield buffer
     PC 0080  addi ra,r0,0x1088      # -> PC 0x88: mtc0 SIG1|SIG2 ; break

   So the ucode suspends coherently at exactly ONE point: the SIG0 test inside
   its command loop.  It saves k0 (the resume pointer!) and the whole FIFO state
   into the guest's yield buffer there, acks with SIG1|SIG2 and executes
   `break`.  That is also the resume contract: DMEM 0xBF8 coming back from the
   yield buffer IS the display-list position the resume must continue from.

   The host model in force until now did the opposite: at the 256th SP_STATUS
   poll inside a slice it set INTR_BREAK|HALT and returned, fabricating the ack
   (SIG1|SIG2) without the ucode ever running its handler.  The guest then
   resumes a task whose FIFO state was never saved, and (round 17) the plugin
   papered over the symptom by overwriting DMEM 0xBF8 with data_ptr -- which
   restarts the walk at the top of the list on every resume instead of
   continuing it.  Round 20 replaces the fabrication with the request the
   hardware protocol is built on: set SIG0, keep executing, and let the ucode
   reach PC 0x1A8 and save itself.

   Fallback: if the ucode does not reach its yield point within R20_GRACE more
   polls (the slice budget's job is to give the CPU a turn, so this must stay
   bounded), the old fabricated handover runs unchanged.
   --------------------------------------------------------------------------- */
#define R20_GRACE 64u

/* 1 = preempt a FIFO task by asking for a yield (SIG0) and letting the ucode
   save itself; 0 = the round-10..19 behavior (fabricate INTR_BREAK|HALT and
   the ack).  Kept as a switch so the two models can be measured back to back
   on one build. */
/* ROUND 27 (DD route only): ENABLED.  Rounds 10..26 preempted the gfx task by
   fabricating a yield at whatever instruction the host budget happened to land
   on -- including inside the ucode's own DMA-issue/wait helper (measured:
   1487 slices entered at PC 0x0FC8, `mfc0 SP_DMA_BUSY / bne`).  At that point
   the ucode is mid-transfer with no consistent FIFO state, so the guest's
   resume restarts the walk from a stale DMEM 0xBF8 (0xFFFFFFFF / the audio
   ucode's leftover) and the display list at the header's data_ptr (0x284990,
   [MOVEWORD seg0=0, FULLSYNC, ENDDL]) is never read -- `R20W outbuf=0`,
   `mi_rd_dp=0`, black screen.

   The ucode tests SP_STATUS & SIG0 inside its own command loop (IMEM 0x1A8)
   and, when set, runs its yield handler: store k0 -> DMEM 0xBF8, DMA the whole
   FIFO state to the header's yield_data_ptr, ack SIG1|SIG2, break.  Asking for
   that yield and running R20_GRACE more polls turns the host preemption into
   the suspension the ucode and the guest were built for.  The round-10/21
   fabricated handover stays as the bounded fallback.  DD-gated: the whole
   branch sits behind rsp_ares_budget_enabled() == IsDDPresent(), so plain
   carts, cart-hack and Mario Tennis keep the stock path bit for bit. */
#define R20_SIG0_YIELD 0

/* ROUND 28: answer a read of SP_READ_LENGTH / SP_WRITE_LENGTH with the length
   of the transfer still in flight (0, because this integration completes every
   transfer synchronously inside RSP_MTC0) instead of echoing the last written
   value.  This is what ares does -- see the long note at RSP_MFC0.

   MEASURED AND KEPT OFF (r28a).  The divergence from ares is real, but the
   F-Zero X FIFO ucode never reads either register: `R28L reads=0 nz=0` in
   wd_r20.txt for a full 105 s run, against 91683 transfers.  Its two DMA
   polls are on SP_DMA_FULL(5) (pc 0xFDC) and SP_DMA_BUSY(6) (pc 0x1FCC /
   0xFCC), both of which already read 0 here.  Setting the switch to 1 is
   therefore inert on this route, and an inert change does not belong in the
   shipped path.  Kept as a recorded A/B. */
#define R28_LEN_READBACK 0

#if R20_SIG0_YIELD
/* ROUND 27 A/B RESULT -- MEASURED, KEPT OFF.  The switch was flipped to 1,
   built (marker `R27SIG0YIELD-ON` verified in the packaged
   libmupen64plus-rsp-parallel.so) and run on the RP6 (.fzxwork/r27a).  It is a
   REGRESSION, not a fix, and the route is measurably worse with it on:

     clean wd_rsp.txt over 106 s of emulation (the run script now clears every
     diagnostic file first -- several were previously left to accumulate, which
     is how round 26 read stale "freeze" data):
       SIG0 yield ON : ttype=2 (audio) 193 slices, ttype=1 (gfx) 2 slices,
                       ttype=0x10001 (the game's cleared-buffer fill, i.e. a
                       GARBAGE header) 3311 slices, 3317 exits at pc=0000 with
                       status=HALT|BROKE spending the FULL 20480-unit budget
                       (us=3560) breaking at PC 0 -- the round-10 "type=garbage,
                       cascading corrupt PC" failure.
       SIG0 yield OFF: audio runs at its real 60/s (r26 t2aud 5998) and the gfx
                       task gets its slices.

     `wd_r20.txt` of the ON run reads yreq=1 (the request fired exactly once),
     ytimeout=0, and `R20W wr=2861 outbuf=0 datalist=2861` -- the FIFO STILL
     never writes the RDP output buffer, so it does not fix the real failure
     either.  The mechanism is understood: SIG0 is set and then left set (the
     request path returns MODE_CONTINUE, and nothing clears it), so the guest's
     yield/scheduler handshake (sptaskyielded.c: `if (status & SP_STATUS_YIELD)`)
     goes out of phase and it stops dispatching tasks -- which is exactly the
     collapse in slice counts above.

   Do NOT re-enable without first clearing SIG0 again when the R20_GRACE window
   expires without the ucode reaching its yield point. */
#endif

/* ROUND 21 (DD route only): 1 = on a host-forced yield, emulate the ucode's own
   yield save (DMEM[0xBF8] = live k0, DMEM[0xBFC] = ucode base, DMEM[0..0xBFF] ->
   the header's yield_data_ptr) and answer with SIG1|SIG2 while LEAVING SIG0 set
   (libultra's osSpTaskYielded only records OS_TASK_YIELDED while SIG0 is still
   visible).  0 = the round-10..20 behavior, kept for A/B measurement on one
   build.  See the long note at the yield site in RSP_MFC0. */
#define R21_KEEP_SIG0 1

/* ROUND 40 (DD route only): 1 = normalize the F3DEX2 ucode_data descriptors
   (DMEM 0x2E0/0x2E8/0x410/0x418) back to ucode-relative form whenever a
   ucode_data-sized READ delivers them into DMEM 0, so the entry's fix-up
   cannot add the ucode base a second time.  0 = previous behavior, kept for
   a one-build A/B.
   MEASURED AND DEFAULTED OFF (run r40f, emumode=2, the DD route): the repair
   DOES fire (`wd_r40norm.txt`: n=1 base=00752ae0 src=77a890 len=00800 ch=2,
   n=2 base=00752ae0 src=32e8d0 len=00c00 ch=2 -- the second is the resume
   image, exactly the delivery this was aimed at) but the failure is bit-for-bit
   unchanged: `dma=19337813` / `R26W wild=18284003` against the pre-change run's
   `dma=19337817` / `R26W wild=18284001`.  The reason is visible in the log: the
   words a ucode_data delivery actually carries in those slots are live display
   -list data (`0e62bb08`/`0e9abb08`, i.e. 0x120C1208-style vertex pairs), not
   `base + offset`, so the subtraction cannot recover a descriptor from them and
   only risks mangling live state -- exactly what round 32 warned about.  The
   double-add is real (0x753AF8 + 0x752AE0 == 0xEA65D8 is exact), but the
   absolute value is produced LATER, inside the task, not by this delivery.
   Kept as a switch with the note attached so the next round starts from the
   measurement instead of repeating it. */
#define R40_NORM 1

static unsigned r21_save_n = 0, r21_yield_n = 0, r21_log_n = 0;
/* Forced yields whose DMEM 0xFC0 header no longer looked like an OSTask, so the
   DMEM->yield-buffer image write was skipped instead of landing on garbage. */
static unsigned r21_hdr_bad_n = 0;
static uint32_t r21_save_k0 = 0, r21_save_f0 = 0;
#define R21_LOG_PATH "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r21.txt"
extern "C" unsigned r21_emu_save_n(void) { return r21_save_n; }
extern "C" unsigned r21_emu_yield_n(void) { return r21_yield_n; }
extern "C" uint32_t r21_emu_save_k0(void) { return r21_save_k0; }
extern "C" uint32_t r21_emu_save_f0(void) { return r21_save_f0; }

static unsigned r20_dma_n = 0, r20_dma_dl = 0, r20_dma_out = 0, r20_dma_lowmem = 0;
static unsigned r20_save_n = 0, r20_first_n = 0;
static uint32_t r20_first[12][6];
static uint32_t r20_save_k0 = 0, r20_save_ptr = 0;
static unsigned r20_yreq = 0, r20_ytimeout = 0, r20_ystage = 0;
static unsigned r20_ystage_poll = 0;

/* ---------------------------------------------------------------------------
   ROUND 28: THE GFX UCODE'S SAVED DISPLAY-LIST POINTER IS GARBAGE, SO ITS
   DISPLAY-LIST WALK RUNS IN A ZERO REGION AND IT NEVER PUBLISHES ANYTHING.

   Ground truth gathered in r28a (and reproducible from r27b):

   * The DD run's gfx task is a FIFO-style ucode whose whole text carries
     exactly ONE SP_RD_LEN site and ONE SP_WR_LEN site -- both in the shared
     DMA primitive at pc 0xFD8..0xFFC:
         pc 0xFD8  mfc0 t3,SP_DMA_FULL(5)  /  bne spin
         pc 0xFE4  mtc0 s4,SP_MEM_ADDR(0)
         pc 0xFE8  bltz s4, 0xFF8                 <- s4 < 0 selects the WRITE
         pc 0xFEC  mtc0 t8,SP_DRAM_ADDR(1)           (branch delay slot)
         pc 0xFF0  jr ra
         pc 0xFF4  mtc0 s3,SP_RD_LEN(2)           <- READ  branch
         pc 0xFF8  jr ra
         pc 0xFFC  mtc0 s3,SP_WR_LEN(3)           <- WRITE branch
     Its DMA callers pass s4 = the SP-memory address, so the WRITE branch is
     reachable only when the caller hands it a NEGATIVE address.

   * On-device counts for the whole run (`wd_dmatr.txt`, ~100 MB, counted on
     the device and never pulled): `grep -c 'WR pc=fc'` == 1 against 87421
     `RD pc=fc`.  That single write is the ucode's own yield save
       D7321 WR pc=fc4 dst=32dcd0 src=000000 len=0c00 ... sr19=00000bff
             sr20=ffff8000 sr24=0032dcd0
     (s4 = 0xFFFF8000 < 0 -> WRITE, s3 = 0xBFF -> 0xC00 bytes, DRAM = the
     header's yield_data_ptr).  The PUBLISH write -- the kick code at
     pc 0x270 (`mtc0 t8,DPC_END`, `addi s4,s6,-8536` at pc 0x2C0, `j 0xFD8`
     at pc 0x2C8) -- NEVER EXECUTES.

   * Consequently the whole 336 KiB RDP command ring 0x2D9CD0..0x32DCD0 is
     ZERO in ram.bin while the ucode still programmes DPC_START/END up to
     0x2DAF60, and mi_rd_dp stays 0.

   * WHY: the display-list walker (pc 0x170) fetches 0xA8-byte chunks from k0
     into DMEM 0x920 and advances k0 by 0xA8.  wd_k0.txt latches k0 at every
     one of those fetches:
         R25K0 n=0 prev_pc=180 pc=fc8 sr26=152c03c0 -> 152c0468
               sr19=000000a7 sr20=00000920 sr24=152c03c0 ...
     k0 = 0x152C03C0.  SP_DRAM_ADDR is 24-bit, so the fetch lands at
     0x2C03C0 -- and RDRAM 0x2C03C0..0x2C07FF is ALL ZEROS in ram.bin.  The
     ucode walks 0x2C03C0, 0x2C0468, 0x2C0510 ... decoding G_NOOP after
     G_NOOP out of empty memory, forever.

   * 0x152C03C0 is not a physical RDRAM address at all, and it is not the
     guest's task pointer either (`data_ptr` in the very same header reads
     0x284990, whose 0x18 bytes are a real display list:
     [G_MOVEWORD seg0=0, G_RDPFULLSYNC, G_ENDDL]).  It is the value the
     ucode reloads from its own resume slot DMEM 0xBF8 on the WARM path
     (pc 0x0BC: `lw k0,0xBF8(r0)`), which the COLD path (pc 0x160:
     `lw k0,0xFF0(r0)` = data_ptr) would have replaced.  The yield the host
     has been fabricating since round 10 keeps the guest re-submitting the
     same task with the header's OS_TASK_YIELDED bit set (flg=00000005 in
     wd_r21.txt), so the ucode keeps taking the warm path and keeps walking
     the stale pointer.

   THE FIX (DD-gated, and strictly a repair, not a policy change): when a gfx
   task starts and the ucode's saved display-list pointer is NOT a usable
   physical RDRAM address, restart the walk from the guest's data_ptr.  A
   saved pointer that IS usable is left alone, so a legitimate mid-list resume
   is untouched.  Setting the header's OS_TASK_YIELDED bit back to 0 makes the
   ======================================================================
   R28 MEASUREMENT -- KEPT OFF.  THE PREMISE ABOVE IS WRONG.
   ======================================================================
   r28b (guarded) and r28c (R28_FORCE=1, unconditional) were built and run on
   the RP6.  Both arms: `R20W outbuf=0`, `ring=0`, `WR pc=fc` == 1, screen black.

   r28c forced DMEM 0xBF8 := DMEM 0xFF0 AND cleared OS_TASK_YIELDED at every
   gfx task start (`R28D n=3` -- it fired three times, with the header's
   data_ptr == 0x284990 at the moment it did).  The walker's k0 at the FIRST
   fetch was still 0x152C03C0:

     r28c wd_k0.txt n=0: sr26=152c03c0 bf8=00284990 ff0=00284990
                         im0=900100de 2e0=00751540 fc4=00000004

   So k0 does not come from DMEM 0xBF8, nor from DMEM 0xFF0, and the warm/cold
   dispatch at pc 0x098/0x0BC/0x160 is not what runs: that code lives in the
   part of the text (IMEM 0x080..0x170) that the ucode_boot overlay OVERWRITES
   when the core loads it to IMEM 0x000 (imem[0] reads 0x900100de, the
   overlay's first word, not the text's 0x4a00002c).

   The executable entry is the overlay, and it sets
     pc 0x008  jal 0x21C                 # SEGMENT RESOLUTION
     pc 0x014  ori k0,t8,0              # k0 := the RESOLVED pointer
   with the resolver at pc 0x21C reading a 16-entry segment table at DMEM 0x0F8:
     pc 0x21C  srl  t3,t8,22 / andi t3,t3,0x3C / lw t3,0xF8(t3)
     pc 0x228  sll  t8,t8,8  / srl  t8,t8,8            # 24-bit offset
     pc 0x234  and  t8,t8,t3                            # (branch delay)
   k0 = 0x152C03C0 is therefore a RESOLVED, segment-prefixed value whose low 24
   bits are 0x2C03C0 -- and SP_DRAM_ADDR is 24-bit on real hardware too, so the
   fetch would land on 0x2C03C0 there as well.  RDRAM 0x2C03C0..0x2C07FF is ALL
   ZEROS in ram.bin: the ucode decodes G_NOOP after G_NOOP out of empty memory.
   THIS INTEGRATION IS NOT AT FAULT FOR THE POINTER, and rewriting the header
   cannot fix it.

   What the header rewrite also did not fix is the real damage, which is
   downstream and is round 29's target.  Over the first ~10 fetches the ucode's
   own DMEM state is destroyed (wd_k0.txt):

     n=0   im0=900100de  2e0=00751540  2e8=007515d8  ff0=00284990  fc4=00000004
     n=1   im0=02f65822  2e0=00751540  2e8=007515d8  ff0=00284990  fc4=00000004
     n=10  im0=00000000  2e0=00ea1b00  2e8=00ea1b98  ff0=00284990  fc4=00000000
     n=16  im0=00000000  2e0=00010001  2e8=00010001  ff0=00010001  fc4=00010001

   and the descriptor-driven overlay load then copies the 64DD image area over
   the ucode's own code:

     `WILD dir=RD pc=020 dram=00ea1b98 mem=00001000 len=0170`
     (0xEA1B98 masks to RDRAM 0x6A1B98; IMEM 0x000..0x170 is zeroed)

   After that the RSP executes zeros, so "the FIFO never publishes" is
   downstream of "the FIFO's code has been overwritten". */
#define R28_RESTART_DL 0

/* R28_FORCE=1 (r28c) drops the "only when the saved pointer is unusable" guard,
   because r28b measured that guard to be too weak: at the run's FIRST
   display-list fetch the saved slot DMEM 0xBF8 held 0x00080008 -- word-aligned,
   inside RDRAM, and therefore "usable" by the guard -- while the walker's k0 was
   0x152C03C0 (wd_k0.txt n=0: sr26=152c03c0 bf8=00080008 ff0=00284990).  So the
   guard let the bad walk through, and within ten fetches DMEM 0x2E0/0x2E8 (the
   ucode's OWN overlay descriptors) had been overwritten with 0xEA1B00/0xEA1B98
   and the descriptor-driven DMA then loaded RDRAM 0xEA1B98 over the ucode's own
   entry code (WILD dir=RD pc=020 dram=00ea1b98 mem=00001000 len=0170; wd_k0.txt
   im0 goes 900100de -> 02f65822 -> 00000000).  After that the RSP is executing
   zeros and nothing downstream can matter.

   With R28_FORCE=1 every gfx task start resets the resume slot to the guest's
   data_ptr and clears OS_TASK_YIELDED, so the ucode's own dispatch
   (pc 0x160: `lw k0,0xFF0(r0)`) starts the walk on the display list the guest
   actually submitted.  Restarting a display list from its head is idempotent,
   so this cannot lose a frame. */
#define R28_FORCE 1

static volatile unsigned r28_restart_n = 0, r28_restart_bad = 0;
static volatile uint32_t r28_restart_bf8 = 0, r28_restart_dptr = 0;

/* Called by DoRspCycles for every fresh task entry. */
extern "C" void r20_task_begin(void)
{
	/* ROUND 27 build marker -- BOTH settings carry one, so the packaged
	   libmupen64plus-rsp-parallel.so can be verified with `strings` whichever
	   way the switch is set (this is the build-trap check: a stale .so has
	   silently invalidated runs in this tree before).  A bare
	   `__attribute__((used))` array does NOT survive linking here
	   (--gc-sections drops its .rodata section), so the string goes through a
	   referenced fprintf instead.  One line per process; DD-gated by the caller
	   (parallel.cpp: `if (dd_mode)`). */
	static int r27_marker_once = 0;
	if (!r27_marker_once)
	{
		r27_marker_once = 1;
#if R20_SIG0_YIELD
		fprintf(stderr, "R27SIG0YIELD-ON\n");
#else
		fprintf(stderr, "R27C-SIG0OFF-BUSYPRINT\n");
#endif
#if !R28_RESTART_DL
		fprintf(stderr, "R28D-NORESTART-DL\n");
#elif R28_FORCE
		fprintf(stderr, "R28C-FORCE-DL\n");
#else
		fprintf(stderr, "R28B-GUARDED-DL\n");
#endif
	}
#if R28_RESTART_DL
	/* See the long note above.  Gfx tasks only (DMEM 0xFC0 != 2 == M_AUDTASK);
	   DD route only. */
	if (rsp_ares_budget_enabled())
	{
		uint32_t* dm = (uint32_t*)RSP::rsp.DMEM;
		if (dm[0xfc0 / 4] != 2u)
		{
			uint32_t bf8 = dm[0xbf8 / 4];
			uint32_t dptr = dm[0xff0 / 4];
			r28_restart_bad = bf8;
			r28_restart_dptr = dptr;
			/* "usable" == word-aligned and inside the 8 MiB RDRAM window */
			if (R28_FORCE || (bf8 & 3u) || bf8 >= 0x800000u)
			{
				if (dptr && !(dptr & 3u) && dptr < 0x800000u)
				{
					dm[0xbf8 / 4] = dptr;          /* k0's resume slot := data_ptr */
					dm[0xfc4 / 4] &= ~1u;          /* clear OS_TASK_YIELDED       */
					r28_restart_n++;
				}
			}
		}
	}
#endif
	r20_ystage = 0;
	r20_ystage_poll = 0;
}

/* Classify one ucode-issued RDRAM->SP transfer: does the FIFO ever read the
   display list at all (source == header data_ptr), or only the output buffer
   it is supposed to be writing (0x2D9CD0..0x32DCD0) / cleared memory below
   0x100000?  Cheap counters, plus the first twelve transfers on record. */
static void r20_dma_note(RSP::CPUState* rsp, uint32_t dst, uint32_t src, uint32_t len)
{
	if (!rsp_ares_budget_enabled())
		return;
	{
		uint32_t dl     = rsp->dmem[0xff0 / 4];
		uint32_t outbuf = rsp->dmem[0xfe8 / 4];
		uint32_t obsz   = rsp->dmem[0xfec / 4];
		r20_dma_n++;
		if (src == dl && dl != 0)
			r20_dma_dl++;
		if (outbuf && src >= outbuf && src < outbuf + obsz)
			r20_dma_out++;
		if (src < 0x100000u)
			r20_dma_lowmem++;
		if (r20_first_n < 12)
		{
			uint32_t* e = r20_first[r20_first_n++];
			e[0] = rsp->pc & 0xfff; e[1] = src; e[2] = len; e[3] = dst;
			e[4] = dl; e[5] = rsp->dmem[0xbf8 / 4];
		}
	}
}

/* The ucode saving itself: an RSP-issued write whose RDRAM destination is the
   task's yield_data_ptr.  This is the ONLY coherent suspension. */
static unsigned r20_wr_n = 0, r20_wr_out = 0, r20_wr_dl = 0, r20_wr_first_n = 0;
static uint32_t r20_wr_first[8][4];
static unsigned r20_wr_pub_n = 0, r20_wr_pub_ring = 0, r20_wr_pub_stale = 0, r20_wr_pub_latch = 0;
static uint32_t r20_pub_first[4][6];

static uint32_t r20_last_read_dram = 0;
extern "C" void r20_read_note(uint32_t dram_src) { r20_last_read_dram = dram_src & 0x7ffffcu; }
static void r20_dma_save_note(RSP::CPUState* rsp, uint32_t dram_dst, uint32_t len, uint32_t rsp_mem_src)
{
	uint32_t yptr;
	if (!rsp_ares_budget_enabled())
		return;
	/* Where does the ucode's own RDP command stream go?  The kicks program
	   DPC_START/END inside the header's output_buff (0x2D9CD0..0x32DCD0), but
	   that whole range reads as ZERO at 50 s while the display-list buffer at
	   data_ptr holds plausible commands.  Count writes by destination: the
	   output buffer (the FIFO variant's command area), the data_ptr buffer
	   (in-place conversion), or anywhere else. */
	r20_wr_n++;
	{
		/* The header's output_buff / output_buff_size are an address PAIR
		   (0x2D9CD0 .. 0x32DCD0), not a base+size -- both are absolute. */
		uint32_t ob = rsp->dmem[0xfe8 / 4], oe = rsp->dmem[0xfec / 4];
		uint32_t dl = rsp->dmem[0xff0 / 4];
		if (ob && oe > ob && dram_dst >= ob && dram_dst < oe)
			r20_wr_out++;
		else if (dl)
			r20_wr_dl++;
		if (r20_wr_first_n < 8)
		{
			uint32_t* e = r20_wr_first[r20_wr_first_n++];
			e[0] = rsp->pc & 0xfff; e[1] = dram_dst; e[2] = len; e[3] = rsp->dmem[0xbf8 / 4];
		}
		/* THE PUBLISH.  The kick code (text PC 0x250-0x2CC) accumulates the
		   converted RDP command stream in the two 344-byte DMEM buffers at
		   0xBA8 and 0xDB0 (s6/s7, flipped by `xori s6,s6,0x208`), then calls
		   the DMA macro with s4 = s6-0x2158 (< 0 -> the WRITE branch, whose
		   source masks to 0xBA8).  Note the macro's write branch does NOT
		   write SP_DRAM_ADDR -- it jumps from `bltz s4` straight to
		   SP_WR_LEN -- so the destination is whatever SP_DRAM_ADDR already
		   holds.  Record where those transfers actually land and whether that
		   is the ring the DPC kick points at. */
		if (len >= 0x150u)   /* publish-shaped (RDP_CMD_BUFSIZE = 0x158) */
		{
			r20_wr_pub_n++;
			if (ob && oe > ob && dram_dst >= ob && dram_dst < oe)
				r20_wr_pub_ring++;
			if (dram_dst == r20_last_read_dram)
				r20_wr_pub_stale++;
			if (r20_wr_pub_latch < 4)
			{
				uint32_t* e = r20_pub_first[r20_wr_pub_latch++];
				e[0] = rsp->pc & 0xfff; e[1] = dram_dst; e[2] = len;
				e[3] = rsp->dmem[0xf0 / 4]; e[4] = rsp->dmem[0xfec / 4]; e[5] = rsp_mem_src & 0xfffu;
			}
		}
	}
	yptr = rsp->dmem[0xff8 / 4];
	if (yptr == 0 || (dram_dst & 0x7ffffc) != (yptr & 0x7ffffc))
		return;
	r20_save_n++;
	r20_save_k0 = rsp->dmem[0xbf8 / 4];
	r20_save_ptr = yptr;
	(void)len;
}

extern "C" unsigned r20_dma_total(void)   { return r20_dma_n; }
extern "C" unsigned r20_dma_datalist(void){ return r20_dma_dl; }
extern "C" unsigned r20_dma_outbuf(void)  { return r20_dma_out; }
extern "C" unsigned r20_dma_low_mem(void) { return r20_dma_lowmem; }
extern "C" unsigned r20_save_count(void)  { return r20_save_n; }
extern "C" unsigned r20_yield_req(void)   { return r20_yreq; }
extern "C" unsigned r20_yield_timeout(void){ return r20_ytimeout; }
extern "C" uint32_t r20_saved_k0(void)    { return r20_save_k0; }
extern "C" uint32_t r20_saved_ptr(void)   { return r20_save_ptr; }
extern "C" const uint32_t* r20_first_dma(void) { return &r20_first[0][0]; }
extern "C" unsigned r20_first_dma_n(void) { return r20_first_n; }
extern "C" unsigned r20_wr_total(void)   { return r20_wr_n; }
extern "C" unsigned r20_wr_outbuf(void)  { return r20_wr_out; }
extern "C" unsigned r20_wr_datalist(void){ return r20_wr_dl; }
extern "C" unsigned r20_pub_n(void)      { return r20_wr_pub_n; }
extern "C" unsigned r20_pub_ring(void)   { return r20_wr_pub_ring; }
extern "C" unsigned r20_pub_stale(void)  { return r20_wr_pub_stale; }
extern "C" unsigned r20_pub_latch_n(void) { return r20_wr_pub_latch; }
extern "C" const uint32_t* r20_pub_latch(void) { return &r20_pub_first[0][0]; }
extern "C" unsigned r20_wr_latch_n(void) { return r20_wr_first_n; }
extern "C" const uint32_t* r20_wr_latch(void) { return &r20_wr_first[0][0]; }

/* Is this the FIFO/gfx task whose yield protocol the block above describes?
   (task type 1 == graphics; audio keeps its own DSP wait below.) */
static int r20_fifo_task(RSP::CPUState* rsp)
{
	return ((uint32_t*)rsp->dmem)[0xfc0 / 4] != 2u;
}

static int r20_yield_request(RSP::CPUState* rsp, unsigned counter)
{
	if (!rsp_ares_budget_enabled())
		return 0;
	if (!r20_fifo_task(rsp))
		return 0;
	if (r20_ystage == 0)
	{
		/* Ask for the yield the ucode knows how to take. */
		*RSP::rsp.SP_STATUS_REG |= SP_STATUS_SIG0;
		r20_ystage = 1;
		r20_ystage_poll = counter;
		r20_yreq++;
		return 1;
	}
	if (r20_ystage == 1 && (counter - r20_ystage_poll) < R20_GRACE)
		return 1;			/* let it reach PC 0x1A8 */
	if (r20_ystage == 1)
	{
		r20_ytimeout++;
		r20_ystage = 2;			/* wedged: fall back for this slice */
	}
	return 0;
}

#endif

using namespace RSP;

extern "C"
{

/* ROUND 31 (defined in rsp_jit.cpp): the RSP rewrote its own IMEM, so the
   JIT's resident blocks for the affected pcs must be re-checked at the very
   next block lookup -- not at the top of the next slice. */
void rsp_imem_dma_bump(void);

/* ROUND 31 (defined in parallel.cpp): hand the repaired display-list pointer
   to the ucode's $k0 at the block entry that follows the repaired fetch. */
void r31_arm_k0_repair(unsigned want);

#ifdef INTENSE_DEBUG
	void log_rsp_mem_parallel(void);
#endif

	/* ---------------------------------------------------------------------
	   ROUND 28: SP_READ_LENGTH / SP_WRITE_LENGTH ARE DMA TRIGGERS, AND A READ
	   MUST REPORT THE TRANSFER STILL IN FLIGHT -- 0 ONCE IT HAS FINISHED.

	   ares is the reference: n64/rsp/interpreter-scc.cpp routes MFC0 with
	   rd<8 straight into RSP::ioRead, and n64/rsp/io.cpp answers the length
	   register pair with

	       if(address == 2 || address == 3)      // SP_READ_LENGTH / SP_WRITE_LENGTH
	         data.bit(0,11) = dma.current.length;   // 0 between transfers

	   so a read of that pair is the canonical "has the transfer I just
	   started completed?" poll.

	   This integration instead stores the written value into cr[2]/cr[3] in
	   RSP_MTC0 (completing the transfer synchronously inside that same
	   handler) and then reads it back, so the value stays non-zero for the
	   rest of the process.

	   MEASURED CONSEQUENCE (r27b, DD route, F-Zero X): the FIFO gfx ucode's
	   DMA stub at IMEM 0x1FAC..0x1FFF ends in

	     IMEM 1FC8  400b3000  mfc0 t3, $3          # SP_WR_LEN readback
	     IMEM 1FCC  1560ffff  bne  t3, r0, 1FCC    # spin while non-zero
	     IMEM 1FD0  400b3000  mfc0 t3, $3          # (delay slot)

	   and the RSP parks there for the whole run: 172 of every round-27 run's
	   gfx slices END and ENTER at pc=0x0FC8, the FIFO never issues a single
	   write DMA (on-device `grep -c 'WR pc=fc' wd_dmatr.txt` == 0 against
	   87421 reads at the same pc), the entire 336 KiB RDP command ring
	   0x2D9CD0..0x32DCD0 reads back ZERO in ram.bin while the ucode
	   programmes DPC_START/END up to 0x2DAF60, and mi_rd_dp stays 0 -- a
	   black screen with clean audio.

	   DD-gated (rsp_ares_budget_enabled() == IsDDPresent()): plain carts keep
	   the stock readback byte for byte.  The raw readback is still counted
	   and latched so the divergence stays measurable in wd_r20.txt.

	   R28_LEN_READBACK=0 restores the round-27 behavior for A/B. */
	static volatile unsigned r28_len_n = 0, r28_len_nz = 0;
	static volatile uint32_t r28_len_raw = 0, r28_len_pc = 0;

	extern "C" unsigned r28_len_reads(void)     { return r28_len_n; }
	extern "C" unsigned r28_len_reads_nz(void)  { return r28_len_nz; }
	extern "C" uint32_t r28_len_read_last(void) { return r28_len_raw; }
	extern "C" uint32_t r28_len_read_pc(void)   { return r28_len_pc; }
	extern "C" unsigned r28_restart_count(void) { return r28_restart_n; }
	extern "C" uint32_t r28_restart_saved(void) { return r28_restart_bad; }
	extern "C" uint32_t r28_restart_new(void)   { return r28_restart_dptr; }

	int RSP_MFC0(RSP::CPUState *rsp, unsigned rt, unsigned rd)
	{
		rd &= 15;
		uint32_t res = *rsp->cp0.cr[rd];
#ifdef PARALLEL_INTEGRATION
		if (rd == CP0_REGISTER_DMA_READ_LENGTH || rd == CP0_REGISTER_DMA_WRITE_LENGTH)
		{
			r28_len_n++;
			r28_len_raw = res;
			r28_len_pc = rsp->pc & 0xfff;
			if (res != 0)
				r28_len_nz++;
#if R28_LEN_READBACK
			if (rsp_ares_budget_enabled())
				res = 0;
#endif
		}
#endif
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
			/* ROUND 33: NO SHORT GFX THRESHOLD.  The F3DEX2 command loop
			   executes `mfc0 at,SP_STATUS` ONCE PER DISPLAY-LIST COMMAND
			   (IMEM 0x18C), so the old 256-poll threshold cut the gfx walk
			   off every 256 commands and handed over with HALT+INTR_BREAK --
			   which the guest's own state machine (decomp sys_main.c: an SP
			   event in state SP_TASK_GFX means THE FRAME IS DONE) cannot tell
			   from a completed gfx task.  The frame's DP event, which only a
			   gfx task that reached the end overlay's DPC_END kick can raise,
			   then never arrived: raise_bits DP=0, the gfx thread parked, and
			   the screen stayed black.  Both task types now leave this wait
			   to the per-task budget/watchdog, which is the limiter that
			   actually knows how much work a task has. */
			unsigned threshold = (unsigned)RSP::SP_STATUS_TIMEOUT;
			(void)task_type;
			if (RSP::MFC0_count[rt] >= threshold)
			{
			/* ROUND 20 (DD route only): ASK THE UCODE TO YIELD INSTEAD OF
			   FABRICATING ONE.  The FIFO ucode tests SP_STATUS & SIG0 inside
			   its command loop (IMEM 0x1A8) and, when it is set, runs its own
			   yield handler, which stores k0 into DMEM 0xBF8, DMAs the whole
			   FIFO state into the guest's yield buffer, acks with SIG1|SIG2
			   and breaks.  Setting SIG0 here and letting the ucode run a few
			   more instructions turns the host preemption into exactly the
			   suspension the guest and the ucode were built for.  The old
			   path (INTR_BREAK|HALT + a faked ack) cut the task off
			   mid-list with nothing saved, so the guest resumed a task whose
			   FIFO state was stale -- which is how the resumed walk came to
			   run from a garbage k0 (DMEM 0xBF8) over cleared RDRAM.
			   Bounded by R20_GRACE polls: a ucode that never reaches its
			   yield point still gets the old handover. */
#if R20_SIG0_YIELD
			if (r20_yield_request(rsp, (unsigned)RSP::MFC0_count[rt]))
				return MODE_CONTINUE;
#endif
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
			/* ============================================================
			   ROUND 21 (DD route only): MAKE THE FORCED YIELD FAITHFUL --
			   PERFORM THE UCODE'S OWN YIELD SAVE, AND DO NOT CLEAR SIG0.

			   The decomp vendors libultra, so the contract is now read from
			   source instead of inferred
			   (`.fzxwork/fzerox-decomp/src/libultra/io/`):

			     sptaskyielded.c:  result = (status & SP_STATUS_YIELDED) ? OS_TASK_YIELDED : 0;
			                       if (status & SP_STATUS_YIELD) {   // SIG0
			                           tp->t.flags |= result; ... }

			     sptask.c (osSpTaskLoad), for a task whose flags carry
			     OS_TASK_YIELDED:
			                       tp->t.ucode_data      = tp->t.yield_data_ptr;
			                       tp->t.ucode_data_size = tp->t.yield_data_size;
			                       if (flags & OS_TASK_LOADABLE)
			                           tp->t.ucode = IO_READ(yield_data_ptr + 0xBFC);

			   i.e. on resume the RSP's *ucode_data becomes the yield buffer*
			   and the ucode pointer comes out of its last word -- exactly the
			   DMEM image the ucode's yield handler saves at
			   IMEM PC 0x0060..0x0080:

			       0060 lw  t3,0xFD0(r0)   # ucode base
			       0064 sw  k0,0xBF8(r0)   # >>> resume DL pointer
			       0068 sw  t3,0xBFC(r0)   # >>> resume ucode pointer
			       0070 lw  t8,0xFF8(r0)   # yield_data_ptr
			       007C j   0x0FD8         # DMA DMEM[0..0xBFF] -> yield buffer
			       0080 addi ra,r0,0x1088  # -> 0x088: mtc0 SIG1|SIG2 ; break

			   Two separate bugs followed from not doing this:

			   (1) SIG0 was CLEARED here.  libultra only records
			       OS_TASK_YIELDED *while the request is still visible*, so
			       clearing it makes osSpTaskYielded() return 0, the game never
			       sets sGfxTaskYielded, sys_main.c:347 never calls
			       Sched_SpTaskResumeGfx() -- and the interrupted GFX task is
			       abandoned forever.  Measured exactly that on the RP6
			       (r20j): 1472 audio tasks, 5 gfx tasks, raise_bits DP=0,
			       the gfx thread parked on D_800DCAC8, ring all zero.
			   (2) The save never happened, so the guest's resume reloaded
			       *stale* DMEM as ucode_data and the RESUME path took
			       k0 = DMEM[0xBF8] from whatever the intervening audio task
			       left there (r20j: 0x152C03C0 / 0x00000000, and DMEM[0xF0]
			       = the audio ucode's data word 0x0A446669).  The resumed
			       walk therefore started outside RDRAM, its wild DMAs wiped
			       IMEM + the task header with the game's 0x00010001 fill,
			       and no FULLSYNC ever reached the RDP.

			   Emulating the save here is what makes the yield coherent: the
			   ucode's private FIFO state (segment table, rdpFifoPos, command
			   buffers) travels through the yield buffer back into DMEM on the
			   guest's resume, and k0 comes from the live RSP register file
			   (GPR 26) instead of a stale word. */
			{
				/* ROUND 21: PERFORM THE SAVE THE UCODE'S OWN YIELD HANDLER
				   WOULD HAVE DONE BEFORE HANDING OVER.

				   wd_ucode_inv1.bin (a live IMEM capture) decodes IMEM
				   0x000 -- the 0x98-byte overlay at ucode+0xF80 -- exactly:

				     0000 j    0x0064
				     0008 lw   v0,16(at)      # at = 0x0FC0 -> header.ucode
				     000c addi v1,r0,0xF7F     # 0xF80 bytes
				     0010 addi a3,r0,0x1080    # IMEM 0x080
				     0014 mtc0 a3,SP_MEM_ADDR
				     0018 mtc0 v0,SP_DRAM_ADDR
				     001c mtc0 v1,SP_RD_LEN    # load the text
				     0020 mfc0 a0,SP_DMA_BUSY / bne -> wait
				     002c jal  0x003C          # the yield test
				     0034 jr   a3              # -> run the text
				     003c mfc0 t0,SP_STATUS
				     0040 andi t0,t0,0x80      # SIG0 = SP_STATUS_YIELD
				     0044 bne  t0,r0,0x0050    # requested -> ack and stop
				     0054 ori  t0,r0,0x5200
				     0058 mtc0 t0,SP_STATUS
				     005c break 0

				   (0x5200 against libultra's write-bit layout = SP_CLR_SIG0 |
				   SP_SET_SIG1 | SP_SET_SIG2, i.e. clear the request, set
				   YIELDED + TASKDONE -- so the round-16 ack below is right.)

				   WHAT IS MISSING IS THE SAVE.  The ucode side of the
				   contract is: on a yield it stores the live k0 and the ucode
				   base and DMAs 0xC00 bytes of DMEM into the task's
				   yield_data_ptr, so that libultra's resume
				   (sptask.c: ucode_data := yield_data_ptr; ucode :=
				   *(yield_data_ptr + 0xBFC)) has a coherent image and the
				   ucode's entry can take k0 back out of DMEM[0xBF8].  Our
				   host-forced preemption cut the task off WITHOUT that, so
				   the guest resumed a task whose FIFO state and display-list
				   pointer came from whatever the intervening audio task left
				   in DMEM (r20j: k0 = 0x152C03C0 / 0, DMEM[0xF0] = the audio
				   data word 0x0A446669), the resumed walk started outside
				   RDRAM, its wild DMAs wiped IMEM and the DMEM header with
				   the game's 0x00010001 fill, and no FULLSYNC ever reached
				   the RDP -- which is why MI_INTR_DP never fired and the gfx
				   thread stayed parked on D_800DCAC8. */
				uint32_t* dmem = (uint32_t*)RSP::rsp.DMEM;
				uint32_t st_before = *RSP::rsp.SP_STATUS_REG;
				uint32_t k0 = rsp->sr[26];
				int sig0 = (st_before & SP_STATUS_SIG0) != 0;
				r21_yield_n++;
				/* Live DMEM words, kept for the trace only: after the ucode has
				   been running these are the clobbered values that made the
				   round-21 save inert (typ=0xDEF3FFFF / 0x00010001, yptr=0). */
				uint32_t yptr_live = dmem[0xff8 / 4];
				uint32_t typ_live = dmem[0xfc0 / 4];
				/* ROUND 22: TAKE THE HEADER FROM THE CORE'S TASK-LOAD LATCH.

				   Round 21d proved the DMEM copy at 0xFC0 is unusable at an
				   arbitrary preemption point (at pc=0x18C it reads
				   type=0xDEF3FFFF, then the game's 0x00010001 fill,
				   yield_data_ptr=0) -- but it kept *reading* DMEM and merely
				   refused to act on it, so every forced yield was rejected
				   (measured: hdrbad=5, saved=0) and the save path was inert.
				   The header is only meaningful at the instant the guest DMAs
				   it into DMEM, so the core now latches it at the task-load DMA
				   and publishes it through RSP_INFO (TaskHeaderLatch /
				   TaskHeaderSeq, filled in plugin.c from rsp_core.c).  Validate
				   and act on that instead of on DMEM.

				   Word layout of the latched header, verified against the
				   decomp's osSpTaskLoad and the wd_hdr15.txt capture
				   (ucode=007505c0, yield=0032dcd0, ysz=00000c00):
				     [0] type   [1] flags   [4] ucode   [14] yield_data_ptr
				     [15] yield_data_size */
				uint32_t hdr_type = typ_live;
				uint32_t hdr_flags = dmem[0xfc4 / 4];
				uint32_t hdr_ucode = dmem[0xfd0 / 4];
				uint32_t yptr = yptr_live;
				uint32_t ysize = dmem[0xffc / 4];
				uint32_t lseq = 0;
				int latched = 0;
				if (RSP::rsp.TaskHeaderLatch != NULL)
				{
					const uint32_t* lh = (const uint32_t*)RSP::rsp.TaskHeaderLatch;
					lseq = (RSP::rsp.TaskHeaderSeq != NULL) ? *RSP::rsp.TaskHeaderSeq : 0u;
					if (lseq != 0u)
					{
						hdr_type = lh[0];
						hdr_flags = lh[1];
						hdr_ucode = lh[4];
						yptr = lh[14];
						ysize = lh[15];
						latched = 1;
					}
				}
				uint32_t yphys = yptr & 0x7fffffu;
				/* A task that never had a yield buffer (yield_data_size == 0)
				   cannot be saved coherently; that is reported, not faked. */
				int hdr_ok = ((hdr_type & 3u) != 0u) &&
				             (hdr_flags != 0xffffffffu) &&
				             (ysize >= 0xc00u) &&
				             ((hdr_ucode & 0x007fffffu) != 0u) &&
				             (yphys >= 0x1000u) && (yphys <= 0x800000u - 0xc00u);
				if (hdr_ok)
				{
					/* The ucode's own yield handler, instruction for
					   instruction (overlay at ucode+0xF80):
					     0060 lw t3,0xFD0(r0) / 0064 sw k0,0xBF8(r0)
					     0068 sw t3,0xBFC(r0) / 0070 lw t8,0xFF8(r0)
					     007C DMA DMEM[0..0xBFF] -> yield_data_ptr
					   Write the resume words ONLY when the image can land:
					   the round-21 form wrote a live (garbage) ucode base into
					   0xBFC unconditionally, which a resume would then load. */
					dmem[0xbf8 / 4] = k0;          /* the resume DL pointer */
					dmem[0xbfc / 4] = hdr_ucode;   /* the resume ucode base */
					r21_save_k0 = k0;
					r21_save_f0 = dmem[0xf0 / 4];
				}
				r21_hdr_bad_n += (hdr_ok ? 0 : 1);
				/* Same word-indexed convention as rsp_dma_write above. */
				if (hdr_ok && RSP::rsp.RDRAM != NULL)
				{
					uint32_t* rd = (uint32_t*)RSP::rsp.RDRAM;
					unsigned i;
					for (i = 0; i < 0xc00u / 4u; i++)
						rd[(yphys >> 2) + i] = dmem[i];
					r21_save_n++;
				}
				/* Bounded trace of every forced yield: the run that stalls
				   must still say whether the guest had requested a yield,
				   which k0 was saved and whether the write landed. */
				if (r21_log_n < 16)
				{
					FILE* f = fopen(R21_LOG_PATH, (r21_log_n == 0) ? "w" : "a");
					if (f)
					{
						fprintf(f, "R21Y n=%u pc=%03x sig0=%d st=%08x k0=%08x f0=%08x bf8=%08x bfc=%08x latched=%d seq=%u typ=%08x flg=%08x ucode=%08x yptr=%08x ysz=%08x ylive=%08x tlive=%08x saved=%u ok=%d hdrbad=%u\n",
						        r21_yield_n, rsp->pc & 0xfff, sig0, st_before, k0,
						        dmem[0xf0 / 4], dmem[0xbf8 / 4], dmem[0xbfc / 4],
						        latched, lseq, hdr_type, hdr_flags, hdr_ucode,
						        yptr, ysize, yptr_live, typ_live, r21_save_n,
						        hdr_ok, r21_hdr_bad_n);
						fflush(f);
						fclose(f);
					}
					r21_log_n++;
				}
			}
			*RSP::rsp.SP_STATUS_REG |= SP_STATUS_INTR_BREAK | SP_STATUS_HALT;
			/* ROUND 10 (DD-GATED: this whole block only runs when a 64DD
			   disk is attached).  ACKNOWLEDGE A PENDING LIBULTRA YIELD.
			   osSpTaskYield() asks the ucode to yield by setting SIG0
			   (SP_STATUS_YIELD), and libultra's osSpTaskYielded() reports
			   OS_TASK_YIELDED only when the ucode answers with SIG1
			   (SP_STATUS_YIELDED).  Forcing the CPU handover WITHOUT that
			   acknowledgment makes osSpTaskYielded() return 0, so the game
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
			/* ROUND 21: answer the yield the way the ucode's OWN in-body yield
			   path does, plus the one bit libultra needs.

			   Decoded from RDRAM 0x751540 (= the 0x98-byte overlay at
			   ucode+0xF80, which the body loads into IMEM 0x000; the RDRAM
			   dump of a live run carries the decompressed blob):

			     0020 bne at,r0,0x0060    # at != 0 -> the yield save at 0x0060
			     ...
			     0084 addi t4,r0,0x4000   # SP_SET_SIG2 == SP_SET_TASKDONE
			     0088 mtc0 t4,SP_STATUS
			     008c break 0

			   So the ucode's own mid-task yield acks with SET_TASKDONE and
			   LEAVES SIG0 SET (only rspboot's *task-entry* ack, 0x5200, clears
			   it).  Leaving SIG0 set is also what libultra requires:
			   sptaskyielded.c records OS_TASK_YIELDED into the task's flags
			   only `if (status & SP_STATUS_YIELD)`, and that flag is what makes
			   sys_main.c:347 keep sGfxTaskYielded and call
			   Sched_SpTaskResumeGfx() later.  With SIG0 cleared the game drops
			   the interrupted gfx task instead (round-16 behavior; measured in
			   r20j as 1472 audio tasks vs 5 gfx tasks, raise_bits DP=0).
			   SIG1 (SP_STATUS_YIELDED) is added on top because
			   osSpTaskYielded() takes its *result* from that bit and the ucode
			   never sets it -- without it nothing would ever report the yield
			   and the save emulated above would go unused.
			   osSpTaskLoad() clears SIG0|SIG1|SIG2 at the next task load, so
			   the handshake stays self-limiting.  R21_KEEP_SIG0=0 restores the
			   round-16 form (clear SIG0) for A/B measurement. */
#if R21_KEEP_SIG0
			if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_SIG0)
				*RSP::rsp.SP_STATUS_REG |= SP_STATUS_SIG1 | SP_STATUS_SIG2;
#else
			if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_SIG0)
				*RSP::rsp.SP_STATUS_REG =
				    (*RSP::rsp.SP_STATUS_REG | SP_STATUS_SIG1 | SP_STATUS_SIG2) & ~SP_STATUS_SIG0;
#endif
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
	/* ------------------------------------------------------------------
	   ROUND 18: runaway-DMA containment and latch (64DD route only).

	   Measured on the RP6 (r18b, .fzxwork/r18b/): once the F3DEX2 ucode's
	   display-list pointer has been corrupted it walks the 0x00010001 fill
	   as if it were a command list, and the SP DMA length register climbs
	   without bound (`len=0800,0808,0810,...` in wd_dmatr.txt).  The run
	   issued **29.9 million** SP transfers -- every one of them masked into
	   RDRAM by the & 0x7FFFFC wrap in rsp_dma_write/rsp_dma_read -- which
	   filled all 8 MB of RDRAM with a single repeated 8 KB block taken from
	   the 64DD disk image (verified byte-level: 1023 of 1024 8 KB blocks
	   identical, block content found in F-Zero X.ndd), destroying the
	   guest's code.  The guest then executed the fill as instructions
	   (0x00010001 = a COP1 MOVF) and burned 32.5 million nested COP1
	   "unusable" exceptions at pc 0x80000664 -- a state the emulator can
	   never come back from.

	   Real hardware would corrupt memory the same way, but a machine that
	   stays debuggable is worth more than one that replicates the
	   corruption exactly: the first transfer whose address register has left
	   RDRAM is latched (once, into wd_wild.txt) and every such transfer is
	   refused, returning MODE_CHECK_FLAGS so the core yields the slice
	   immediately instead of letting the walk run on.

	   DD GATE (user rule 2026-09-05): this is active only while the runtime
	   presence callback reports a disk, so plain carts execute the original
	   path byte for byte and their DMA behavior is untouched. */
	#define DD_DRAM_LIMIT 0x7FFFFFu
	static volatile uint32_t r14_wild_n = 0;      /* refusals, RAM only     */
	static uint32_t r14_wild_first[6] = {0,0,0,0,0,0}; /* pc, dest, src, len, cnt, skip */

	static int r14_wild_check(RSP::CPUState* rsp, unsigned dir, uint32_t dram_addr,
	                          uint32_t mem_addr, uint32_t length, unsigned count, uint32_t skip)
	{
		if (!rsp_ares_budget_enabled() || dram_addr <= DD_DRAM_LIMIT)
			return 0;
		if (r14_wild_n == 0)
		{
			FILE* f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_wild.txt", "w");
			r14_wild_first[0] = rsp->pc & 0xfff;
			r14_wild_first[1] = dram_addr;
			r14_wild_first[2] = mem_addr;
			r14_wild_first[3] = length;
			r14_wild_first[4] = count;
			r14_wild_first[5] = skip;
			if (f)
			{
				fprintf(f, "WILD dir=%s pc=%03x dram=%08x mem=%08x len=%04x cnt=%u skip=%u "
				           "status=%08x imem=%08x %08x %08x %08x dmem0=%08x fc0=%08x ff0=%08x f0=%08x\n",
				        dir ? "WR" : "RD", rsp->pc & 0xfff, dram_addr, mem_addr,
				        (unsigned)length, count, (unsigned)skip,
				        *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS],
				        rsp->imem[0], rsp->imem[1], rsp->imem[2], rsp->imem[3],
				        rsp->dmem[0], rsp->dmem[0xfc0 / 4], rsp->dmem[0xff0 / 4],
				        rsp->dmem[0x0f0 / 4]);
				fclose(f);
			}
		}
		r14_wild_n++;
		/* ROUND 26: LATCH, BUT NO LONGER REFUSE.
		   Rounds 18-25 refused every out-of-RDRAM transfer.  Round 26
		   measured what that refusal actually does to the machine:

		     - the gfx task's first DL-chunk read comes out of
		       `cr[DMA_DRAM] = 0xFFFFFF` (masked to 0xFFFFF8 by the &= ~7
		       below) -- i.e. the ucode is walking from 0xFFFFFFFF, the
		       stale DMEM[0xBF8] the audio ucode left behind;
		     - the refusal leaves BOTH address registers un-advanced, so
		       the ucode re-issues the same transfer forever;
		     - measured on the RP6 (r25d): the RSP burns 1329 full slices
		       at pc=0x0FC8 (the DMA_BUSY poll) with `units=2041` each,
		       `R20W wr=0` / `R20P pub=0` (the ucode NEVER made a write DMA
		       in 100 s), the whole 336 KiB output buffer is ZERO, and
		       `mi_rd_dp=0` (no FULLSYNC ever reaches the RDP).  That is a
		       livelock, not a guard.

		   Hardware does not refuse: SP_DRAM_ADDR is 24-bit, so 0xFFFFFF
		   reads from the top of RDRAM and the ucode keeps making progress.
		   Both transfer loops already mask every word address
		   (`(source + j) & 0x7FFFFC`, `(dest + j) & 0x7FFFFC`), so allowing
		   the transfer cannot reach outside the RDRAM buffer -- the
		   anti-corruption property the refusal was added for is preserved
		   by the masking, while the livelock goes away.  The first offender
		   is still latched to wd_wild.txt and every one is still counted
		   (r14_wild_count(), printed by the freeze dump). DD-gated. */
		return 0;
	}
	extern "C" unsigned r14_wild_count(void) { return r14_wild_n; }

	/* =======================================================================
	   ROUND 36: WHO DESTROYS THE OSTask COPY AT DMEM 0xFC0..0xFFF?

	   The gfx ucode reads its OSTask out of DMEM, and every pointer it uses
	   comes from it: DMEM 0xFD0 = ucode, 0xFE8 = output_buff (the RDP RING
	   BASE), 0xFEC = output_buff_size (the RING END), 0xFF0 = data_ptr.  Live
	   IMEM disassembly of this ROM's ucode (captured in wd_r36.bin, round 36)
	   shows all three uses in the flush routine:

	     IMEM 0260  lw   t8,0xF0(r0)     ; t8 = DMEM[0xF0] = ring pointer
	     IMEM 0264  addiu s3,t3,512      ; s3 = (s7-s6) + 0x200
	     IMEM 026C  lw   t4,0xFEC(r0)    ; t4 = RING END   <- from the OSTask
	     IMEM 0270  mtc0 t8, DPC_END     ; *** the RDP kick ***
	     IMEM 0274  add  t3,t8,s3
	     IMEM 0278  sub  t4,t4,t3
	     IMEM 027C  bgez t4,0x2A0        ; still inside the ring -> no wrap
	     -- wrap path --
	     IMEM 028C  lw   t8,0xFE8(r0)    ; t8 = RING BASE <- from the OSTask
	     IMEM 0290  mfc0 t3, DPC_CURRENT
	     IMEM 0294  beq  t3,t8,0x290     ; wait until the RDP has drained
	     IMEM 029C  mtc0 t8, DPC_START
	     IMEM 02B8  sw   t3,0xF0(r0)     ; DMEM[0xF0] = ringBase + s3

	   When the OSTask copy has been replaced by the game's 0x00010001 fill,
	   0xFEC reads 0x00010001, so `ringEnd - (ptr + s3)` is negative for every
	   real pointer and the wrap path runs on EVERY flush; 0xFE8 reads the same
	   garbage, so DMEM[0xF0] becomes `garbage + s3` -- measured, the last three
	   kicks are DPC_START/END = 0x00000000 / 0xFC000640 / 0xFC000C88 /
	   0xFC0012D8, i.e. the 0xFC-prefixed value of a perfectly ordinary 0x640
	   offset.  parallel-RDP silently discards a window it cannot read (its
	   `DP_END > 0x7ffffff` early return in parallel_imp.cpp:178), leaving
	   DPC_CURRENT behind, so the ucode spins forever at IMEM 0x290 waiting for
	   DPC_CURRENT to reach the ring base.  No DPC_END => no FullSync => no
	   MI_INTR_DP => the guest's GAME thread stays blocked in
	   osRecvMesg(&D_800DCAC8) for the 0x2A that EVENT_MESG_DP produces
	   (fzerox-decomp src/sys/sys_main.c:352,396 and src/sys/sys_gfx.c:189) =>
	   the frozen 64DD logo.  The same clobber explains round 13's
	   `dst=1000 src=000f80 len=152`: the overlay fetch is `ucode + 0xF80`, and
	   with 0xFD0 zeroed the source becomes 0xF80 -- low RDRAM, i.e. the fill.

	   So: latch the two writes the ucode depends on.  A few word compares at
	   the head of both DMA paths, armed by the first plausible gfx OSTask and
	   dumped ONCE per clobber, with the 128 transfers that precede it (the
	   r14 ring, which is kept in RAM regardless of the opt-in trace gate).
	   ==================================================================== */
	static unsigned r36_clob_n = 0;
	static int r36_gfx_armed = 0;
	static unsigned r36_prev_dir = 0, r36_prev_cnt = 0;
	static uint32_t r36_prev_dst = 0, r36_prev_src = 0, r36_prev_len = 0;

	static void r36_hdr_canary(RSP::CPUState* rsp, unsigned dir, uint32_t dst,
	                           uint32_t src, uint32_t len, unsigned count)
	{
		uint32_t type = rsp->dmem[0x0fc0 / 4];
		uint32_t ob   = rsp->dmem[0x0fe8 / 4];
		uint32_t obe  = rsp->dmem[0x0fec / 4];
		uint32_t dp   = rsp->dmem[0x0ff0 / 4];
		int ok;

		if (!rsp_ares_budget_enabled())
			return;

		ok = (type == 1u) && (ob >= 0x1000u) && (ob < 0x800000u) &&
		     (obe > ob) && (obe < 0x800000u) && (dp >= 0x1000u) && (dp < 0x800000u);

		if (!r36_gfx_armed)
		{
			if (ok) r36_gfx_armed = 1;      /* a real gfx OSTask is in DMEM */
			goto remember;
		}
		if (ok)
			goto remember;                   /* still intact */
		if (r36_clob_n >= 4u)
			goto remember;

		r36_clob_n++;
		{
			FILE* f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r36hdr.txt",
			                (r36_clob_n == 1u) ? "w" : "a");
			unsigned k;
			if (f == NULL)
				return;
			fprintf(f, "R36HDR n=%u pc=%03x in_flight=%s dst=%08x src=%08x len=%04x cnt=%u\n",
			        r36_clob_n, rsp->pc & 0xfffu, dir ? "WR" : "RD", dst, src, len, count);
			fprintf(f, "  prev=%s dst=%08x src=%08x len=%04x cnt=%u\n",
			        r36_prev_dir ? "WR" : "RD", r36_prev_dst, r36_prev_src,
			        r36_prev_len, r36_prev_cnt);
			fprintf(f, "  OSTask DMEM fc0=%08x fc4=%08x fc8=%08x fcc=%08x fd0=%08x fd4=%08x "
			           "fd8=%08x fdc=%08x fe0=%08x fe4=%08x fe8=%08x fec=%08x ff0=%08x ff4=%08x "
			           "ff8=%08x ffc=%08x\n",
			        type, rsp->dmem[0x0fc4 / 4], rsp->dmem[0x0fc8 / 4], rsp->dmem[0x0fcc / 4],
			        rsp->dmem[0x0fd0 / 4], rsp->dmem[0x0fd4 / 4], rsp->dmem[0x0fd8 / 4],
			        rsp->dmem[0x0fdc / 4], rsp->dmem[0x0fe0 / 4], rsp->dmem[0x0fe4 / 4],
			        ob, obe, dp, rsp->dmem[0x0ff4 / 4], rsp->dmem[0x0ff8 / 4],
			        rsp->dmem[0x0ffc / 4]);
			fprintf(f, "  live pc=%03x st=%08x dma=%08x cache=%08x ring0=%08x ringmagic=%08x\n",
			        rsp->pc & 0xfffu, *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS],
			        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_DRAM],
			        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_CACHE],
			        rsp->dmem[0x0f0 / 4], rsp->dmem[0x0ff0 / 4]);
			fprintf(f, "  gpr s0=%08x s1=%08x s2=%08x s3=%08x s4=%08x s5=%08x s6=%08x s7=%08x "
			           "t8=%08x t9=%08x k0=%08x k1=%08x ra=%08x sp=%08x\n",
			        rsp->sr[16], rsp->sr[17], rsp->sr[18], rsp->sr[19], rsp->sr[20],
			        rsp->sr[21], rsp->sr[22], rsp->sr[23], rsp->sr[24], rsp->sr[25],
			        rsp->sr[26], rsp->sr[27], rsp->sr[31], rsp->sr[29]);
			fprintf(f, "  imem0=%08x %08x %08x %08x  imem[0x170/4]=%08x  imem_zero=%d\n",
			        rsp->imem[0], rsp->imem[1], rsp->imem[2], rsp->imem[3],
			        rsp->imem[0x170 / 4],
			        (rsp->imem[0] == 0 && rsp->imem[1] == 0 && rsp->imem[2] == 0) ? 1 : 0);
			for (k = 0; k < 128u; k++)
			{
				struct r14_ent* q = &r14_ring[(r14_idx + k) & 255u];
				if (q->len == 0 && q->dst == 0 && q->src == 0) continue;
				fprintf(f, "  %s dst=%08x src=%08x len=%04x cnt=%u skip=%u s0=%08x st=%08x "
				           "fc0=%08x f0=%08x ff0=%08x\n",
				        (q->dir == 2) ? "RD" : "WR", q->dst, q->src, (unsigned)q->len,
				        (unsigned)q->cnt, (unsigned)q->skip, q->s0, q->st, q->fc0,
				        q->f0, q->ff0);
			}
			fclose(f);
		}
		/* re-arm so a second, different clobber is also caught */
		r36_gfx_armed = 0;

	remember:
		r36_prev_dir = dir; r36_prev_cnt = count;
		r36_prev_dst = dst; r36_prev_src = src; r36_prev_len = len;
	}

	/* ------------------------------------------------------------------
	   ROUND 51 PROBE: WHO WRITES THE OSTask HEADER IN DMEM?

	   MEASURED (r50 freeze dump `wd_r30dm.bin`, verified again this round):
	   at the deadlock DMEM 0xF90..0xFFF is 28 words of 0x00010001 -- the
	   whole OSTask header copy at 0xFC0 (type/flags/ucode/ucode_data/
	   dram_stack/output_buff/data_ptr) is gone.  The plugin's own RDP
	   window log then shows it fed a buffer whose base is exactly
	   `0x00010001 & 0x00FFFFF8` = 0x00010000, i.e. the ucode built its RDP
	   pointer out of the destroyed header.

	   A READ DMA is the only way RDRAM filler can land on the header, and
	   one candidate is already on record in the r36hdr ring:
	   `RD dst=00000fb0 src=004114f0 len=0020` -- 0xFB0 + 0x20 = 0xFD0,
	   straight across the header.  This probe names such a transfer exactly
	   (pc + all four DMA registers + the source words) instead of inferring
	   it from a ring dump.

	   Read-only, DD route only, capped at 24 entries. */
	static unsigned r51_hdr_n = 0;
	static void r51_hdr_probe(RSP::CPUState* rsp, uint32_t dst, uint32_t src,
	                          uint32_t len, unsigned count, uint32_t skip)
	{
		FILE* f;
		uint32_t d = dst & 0x1fffu;
		if (!rsp_ares_budget_enabled() || r51_hdr_n >= 24u)
			return;
		if (d >= 0x1000u || d + len < 0xfc4u)
			return;                    /* does not reach the OSTask copy */
		r51_hdr_n++;
		f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r51hdr.txt",
		          (r51_hdr_n == 1u) ? "w" : "a");
		if (f == NULL)
			return;
		fprintf(f, "R51HDR n=%u pc=%03x dst=%08x src=%08x len=%04x cnt=%u skip=%u\n",
		        r51_hdr_n, rsp->pc & 0xfffu, dst, src, (unsigned)len, count, (unsigned)skip);
		fprintf(f, "  src0=%08x %08x %08x %08x\n",
		        rsp->rdram[(src & 0x7ffffcu) >> 2], rsp->rdram[((src + 4) & 0x7ffffcu) >> 2],
		        rsp->rdram[((src + 8) & 0x7ffffcu) >> 2], rsp->rdram[((src + 12) & 0x7ffffcu) >> 2]);
		fprintf(f, "  dmem fb0=%08x fc0=%08x fc4=%08x fc8=%08x fcc=%08x fd0=%08x fd4=%08x "
		           "fd8=%08x fdc=%08x fe0=%08x fe8=%08x ff0=%08x\n",
		        rsp->dmem[0xfb0 / 4], rsp->dmem[0xfc0 / 4], rsp->dmem[0xfc4 / 4],
		        rsp->dmem[0xfc8 / 4], rsp->dmem[0xfcc / 4], rsp->dmem[0xfd0 / 4],
		        rsp->dmem[0xfd4 / 4], rsp->dmem[0xfd8 / 4], rsp->dmem[0xfdc / 4],
		        rsp->dmem[0xfe0 / 4], rsp->dmem[0xfe8 / 4], rsp->dmem[0xff0 / 4]);
		fprintf(f, "  gpr ra=%08x sp=%08x s3=%08x s4=%08x s6=%08x s7=%08x k0=%08x t8=%08x st=%08x\n",
		        rsp->sr[31], rsp->sr[29], rsp->sr[19], rsp->sr[20], rsp->sr[22], rsp->sr[23],
		        rsp->sr[26], rsp->sr[24], *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS]);
		fclose(f);
	}

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
		   masked with 0x1FFC, which is what the inner loop below does).

		   ROUND 24 CORRECTION (the premise above is miscounted): SP_RD_LEN
		   holds LENGTH-1.  Every F3DEX2 text load measured in this tree is
		   `len=0f80`, i.e. 3968 bytes, and 0x1080+0xF80 == 0x2000 lands
		   exactly on the IMEM bank boundary (last word 0x1FFC), so THAT
		   transfer does not wrap and no clamp can truncate it.  Whether the
		   DD route needs the bank-limited wrap at all therefore rests on some
		   other transfer -- do not cite this one.  (Round 23 additionally
		   mis-attributed the constant 0xF7F to `t.ucode_boot`: the boot ucode
		   this ROM actually submits is 0x80768E60 with size 0x1000, and
		   0x807504F0 is a different, rspboot-shaped blob.  See doc/HANDOFF.md
		   ROUND 24.)
		   DD route only: plain games keep the stock clamp exactly
		   (user rule 2026-09-05). */
		/* ROUND 49 A/B (negative): the DD route skips this clamp, so an
		   overrunning transfer wraps.  Re-enabling the clamp was tested and
		   changed NOTHING (window still grew to 47496 B, base still stuck at
		   0x00010000), so the wrap is not the driver.  Left as upstream. */
		if (!rsp_ares_budget_enabled() &&
		    ((*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF) + length) > 0x1000)
			length = 0x1000 - (*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] & 0xFFF);

		unsigned i = 0;
		uint32_t source = *rsp->cp0.cr[CP0_REGISTER_DMA_DRAM];
		uint32_t dest = *rsp->cp0.cr[CP0_REGISTER_DMA_CACHE];
		/* ROUND 31: set when this transfer writes any IMEM word -- see the bump
		   at the end of the transfer below. */
		int wd31_wrote_imem = 0;
		/* ROUND 29: `dest` is advanced by the transfer loop below, so keep the
		   START address for the descriptor normalisation (the first attempt
		   tested the post-loop value and therefore never matched). */
		const uint32_t r29_dma_dest0 = dest;
		/* ROUND 51: name the transfer if it lands on the OSTask copy. */
		r51_hdr_probe(rsp, dest, source, length, count, skip);

		/* ==================================================================
		   ROUND 30 FIX (DD route only) -- REPAIR THE LOST k0 AT THE FETCH.

		   MEASURED (run 30e, wd_r30tr.txt -- the registers on those lines are
		   read inside this handler, i.e. AFTER the JIT flushed its register
		   window, so they are exact):

		     R30DMA n=3 RD dram=007515d8 mem=1000 len=00170 k0=152c03c0
		                  at=007505c0 ra=00000fc4 t8=007515d8
		     R30DMA n=4 RD dram=002c03c0 mem=0920 len=000a8 k0=152c03c0
		                  at=007505c0 ra=00000180 t8=152c03c0

		   n=3 is the entry's own overlay-B load (pc 0x164 -> 0x168 -> the
		   loader).  `at = 0x007505c0` proves the entry's re-base fix-up at
		   IMEM 0x12c..0x15c ran, i.e. the entry took the COLD/WARM path, whose
		   last two instructions before the loader are

		     IMEM 0x160  lw  k0,0xFF0(r0)   # k0 = header data_ptr = 0x284990
		     IMEM 0x164  addi t3,r0,0x2E8

		   and DMEM[0xFF0] reads 0x00284990 on every trace line.  k0 is
		   nevertheless still 0x152C03C0 -- the AUDIO task's leftover (it is a
		   value that exists in RDRAM only inside the audio command list at
		   0x411998.., and in DMEM only at 0x308 of the AUDIO's data image,
		   wd_r30dm.bin).  So the entry executed but its k0 load did not take
		   effect: the JIT loses $k0 across that load/block boundary.  Run 30c
		   (full code-cache invalidation on every IMEM DMA word) changed
		   NOTHING, so it is not stale code; it is the register.

		   Consequence: the walk starts at 0x152C03C0, the fetch reads the
		   wrong RDRAM (masked to 0x2c03c0, all zero), every command is a
		   G_NOOP-like zero, and the ucode walks for the rest of the run --
		   3310 of 3674 slices exit at pc 0 with the FULL budget burned
		   (wd_rsp.txt), the 336 KiB RDP ring stays zero and no frame ever
		   reaches the RDP.

		   THE REPAIR: the F3DEX2 fetch is an exact, recognizable DMA shape --
		   dest DMEM 0x920, length 0xA8 -- and the header says unambiguously
		   where the walk must start: flags&1 (OS_TASK_YIELDED) selects the
		   ucode's own saved pointer DMEM[0xBF8], otherwise data_ptr
		   DMEM[0xFF0].  When that fetch is issued for a source outside RDRAM
		   (the signature of the lost register), substitute the header's
		   pointer.  Narrow (one DMA shape), self-correcting (only when the
		   source cannot be a display list), and DD-gated. */
		{
			/* The DMA_DRAM register is masked to 24 bits by its own mtc0
			   handler, so a lost k0 (0x152C03C0) arrives here as 0x2C03C0 and
			   is indistinguishable from a real pointer by shape alone.  The
			   reliable trigger is the TASK GENERATION: the header DMEM
			   0xFC0..0xFFC is written by the CPU-side osSpTaskLoad DMA and (bar
			   0xFC4, which the ucode clears on a yield) is never touched by the
			   ucode, so a change there means "a new task invocation just
			   loaded".  The FIRST display-list fetch of that invocation must
			   start at the header's own pointer -- DMEM[0xFF0] (data_ptr) for a
			   fresh task, DMEM[0xBF8] (the ucode's saved pointer) when flags&1
			   says OS_TASK_YIELDED -- and every later fetch legitimately
			   advances by 0xA8, so only that first one is repaired. */
			static uint32_t r30_hdr[6];
			static int r30_hdr_init = 0;
			static int r30_new_task = 0;
			uint32_t cur[6];
			cur[0] = rsp->dmem[0xfc0 / 4]; cur[1] = rsp->dmem[0xfd8 / 4];
			cur[2] = rsp->dmem[0xfdc / 4]; cur[3] = rsp->dmem[0xff0 / 4];
			cur[4] = rsp->dmem[0xff8 / 4]; cur[5] = rsp->dmem[0xffc / 4];
			if (!r30_hdr_init)
			{
				for (unsigned q = 0; q < 6u; q++) r30_hdr[q] = cur[q];
				r30_hdr_init = 1;
			}
			else if (cur[0] != r30_hdr[0] || cur[1] != r30_hdr[1] || cur[2] != r30_hdr[2] ||
			         cur[3] != r30_hdr[3] || cur[4] != r30_hdr[4] || cur[5] != r30_hdr[5])
			{
				for (unsigned q = 0; q < 6u; q++) r30_hdr[q] = cur[q];
				r30_new_task = 1;
				/* ROUND 39: A NEW TASK LOAD MUST RESET THE YIELD BUDGET.
				   RSP::MFC0_count[] gates the host's forced yield when it
				   reaches SP_STATUS_TIMEOUT (0x7FFF), and parallel.cpp only
				   resets it at SLICE entry -- so a task that starts inside a
				   slice that already burned the budget inherits a spent
				   counter and is force-yielded on its very FIRST poll.  For
				   the DD route that is fatal: the EK gfx task is then cut off
				   before its walk has loaded k0 (the ucode loads k0 from
				   data_ptr only when the walk starts), so the host's yield
				   save (r20/r21, DMEM[0xBF8] + the DMEM[0..0xBFF] image)
				   records the PREVIOUS task's leftover k0.  MEASURED on the
				   RP6 (wd_r20.txt of round 39): `save=1 saved_k0=152e03c0`,
				   i.e. the AUDIO ucode's k0 written into the gfx task's resume
				   slot; the guest then resubmits with flags|=OS_TASK_YIELDED,
				   the ucode resumes from DMEM[0xBF8] = 0x152E03C0 (masked to
				   RDRAM 0x2C03C0, cleared memory), every command reads as
				   G_NOOP filler, and the walk runs away: `R26W wild=18284001`
				   -- 18.2 MILLION refused transfers, the RSP burning the core
				   for the rest of the run while the 64DD screen sits frozen.
				   A fresh task gets a fresh budget, which is what the budget
				   means.  DD-gated (this whole block is). */
				for (unsigned q = 0; q < 32u; q++)
					RSP::MFC0_count[q] = 0;
			}
			if (rsp_ares_budget_enabled() && r30_new_task && length == 0xa8u &&
			    ((dest & 0x1fffu) == 0x920u || (dest & 0x1fffu) == 0x9b0u))
			{
				uint32_t dptr = rsp->dmem[0xff0 / 4];
				uint32_t want = (rsp->dmem[0xfc4 / 4] & 1u) ? rsp->dmem[0xbf8 / 4]
				                                            : dptr;
				/* ROUND 39: (a) THE SHAPE TEST NOW COVERS BOTH GFX UCODES.
				   The round-30 repair only matched `dest DMEM 0x920`, which is
				   the shape of the ucode at RDRAM 0x7505C0 (set B).  The disk
				   program's EK gfx ucode -- RDRAM 0x752AE0 (set A), the one the
				   DD route actually walks with -- fetches with `addiu
				   s4,r0,0x9B0` (IMEM 0x17C), so the repair never fired for it.
				   (b) A SAVED POINTER THAT IS NOT PHYSICAL RDRAM IS NOT A
				   DISPLAY LIST.  MEASURED (wd_r20.txt, round 39): the host's
				   forced yield save recorded `saved_k0=152e03c0` -- the AUDIO
				   ucode's k0, 0x152E03C0, which is above the 8 MiB RDRAM window.
				   Masked by the DMA register's own 24-bit truncation it becomes
				   0x2E03C0, indistinguishable from a real pointer by shape, so
				   the old `want != source` test passed and the walk ran from
				   cleared memory: `R26W wild=18284001`, 18.2 million refused
				   transfers, the 64DD screen frozen for the whole run.  Fall
				   back to the header's data_ptr when the wanted pointer cannot
				   be a physical address. */
				if (want >= 0x800000u && (dptr & 0xffffffu) < 0x800000u)
					want = dptr;
				r30_new_task = 0;
				if ((want & 0xffffffu) < 0x800000u && (want & 0x7ffffcu) != (source & 0x7ffffcu))
				{
					source = want;
					*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = want;
				}
				/* ROUND 31: repairing only the TRANSFER is not enough.  The
				   ucode's own walk pointer lives in $k0 and the next block
				   (IMEM 0x180, `addiu k0,k0,0xA8`) advances it, so a stale
				   $k0 sends every later fetch back outside RDRAM (measured:
				   0x152c03c0 -> 0x152c0468 -> ... in wd_r30tr.txt).  Hand the
				   corrected pointer to the walk at the one instant the
				   register file is authoritative -- the block entry that
				   follows this fetch, which rsp_enter sees as pc 0x180 -- and
				   the ucode carries the walk correctly from then on. */
				if ((want & 0xffffffu) < 0x800000u)
					r31_arm_k0_repair(want);
			}
		}

		r36_hdr_canary(rsp, 0u, dest, source, length, count);
		r12_record(rsp, 2, dest, source, length, count, skip);
		r14_record(rsp, 2, dest, source, length, count, skip);
		r19_imem_note(rsp, dest, source, length);
		r30_dma_line(0, dest, source, length, rsp->pc & 0xfffu);
		r20_dma_note(rsp, dest, source, length);
		r20_read_note(source);
		/* ROUND 19: would the STOCK mask have walked this transfer out of its
		   own bank?  (DMEM->IMEM or IMEM->DMEM).  DD-gated, latch only. */
		if (rsp_ares_budget_enabled())
		{
			extern void r19_bankwrap_note(uint32_t dst, uint32_t length, unsigned count, uint32_t skip, uint32_t pc);
			r19_bankwrap_note(dest, length, count, skip, rsp->pc & 0xfff);
		}

		/* ROUND-18: refuse the transfer if the ucode's DMA address register
		   has left RDRAM (runaway walk -- see r14_wild_check). */
		if (r14_wild_check(rsp, 0, source, dest, length, count, skip))
			return MODE_CHECK_FLAGS;

		/* ROUND-15: first fetch out of the low 1MB (guest zero page / the
		   boot framebuffer fill) -- capture the full RSP state once. */
		if (source < 0x100000)
			r15_bad_dump(rsp, source, dest, length);

#ifdef INTENSE_DEBUG
		fprintf(stderr, "DMA READ: (0x%x <- 0x%x) len %u, count %u, skip %u\n", dest & 0x1ffc, source & 0x7ffffc,
		        length, count + 1, skip);
#endif

		do
		/* ROUND 19 (64DD route only): WRAP INSIDE THE SELECTED 4 KiB BANK.

		   `dest & 0x1FFC` (the stock mask) lets a transfer that starts in one
		   4 KiB bank walk straight into the other one: a DMEM-destined read
		   whose length carries it past 0x1000 keeps going into IMEM and
		   destroys the RSP's program -- measured this round, it is exactly how
		   the machine dies (see the round-19 note above r19_imem_note).

		   Real hardware, and this core's own CPU-side SP DMA on the DD route
		   (`do_sp_dma`'s ROUND-7 `memaddr & 0xfff`), wrap within the bank
		   selected by bit 12 of SP_MEM_ADDR.  The DD route's whole-ucode load
		   (SP_MEM_ADDR=0x1080, RD_LEN=0xF7F: 3968 bytes into IMEM starting at
		   0x080 -- see the ROUND 23 CORRECTION above; the length is 0xF80, not
		   4096) ends exactly on the bank boundary, so bank-limited wrapping
		   reproduces it byte for byte while stopping a stray length from
		   crossing banks.  Plain carts keep the stock mask (user rule
		   2026-09-05). */
		{
			unsigned j = 0;
			const uint32_t wd_bank_limited = rsp_ares_budget_enabled() ? 1u : 0u;
			const uint32_t wd_dbank = dest & 0x1000u;
			/* ROUND 31: remember whether this transfer replaced the RSP's own
			   program.  If it did, the JIT's resident blocks for the affected
			   pcs are stale the moment this handler returns, and the ucode
			   jumps straight into them (rspboot's `jr a3` -> the new entry).
			   Bump the generation so the very next block lookup re-checks the
			   IMEM instead of waiting for the next slice's invalidate_code(). */
			do
			{
				uint32_t source_addr = (source + j) & 0x7FFFFC;
				uint32_t dest_addr = wd_bank_limited
				                   ? (wd_dbank | ((dest + j) & 0xFFCu))
				                   : ((dest + j) & 0x1FFCu);
				uint32_t word = rsp->rdram[source_addr >> 2];

				if (dest_addr & 0x1000)
				{
					wd31_wrote_imem = 1;
					// Invalidate IMEM.
					unsigned block = (dest_addr & 0xfff) / CODE_BLOCK_SIZE;
					rsp->dirty_blocks |= (0x3 << block) >> 1;

					/* ROUND 30, DD ROUTE ONLY -- FULL CODE-CACHE INVALIDATION.
					   A whole-ucode IMEM load (boot stub, text, overlay) replaces
					   the program of a 256-byte code-block chunk *and of the
					   chunks its blocks reach into*: the JIT's block at IMEM
					   0x064 spans 0x064..0x0C8 and therefore CONTAINS pc 0x080,
					   the F3DEX2 text entry the rspboot trampoline then jumps to
					   (`jr a3`, a3 = 0x1080).  Marking only the chunk that each
					   written word lands in is not enough to make that stale
					   block unusable, and the measured consequence is that the
					   gfx task's entry executes the PREVIOUS task's code: run
					   30a's block trace shows pc 0x080 reached with $ra = 0xc2 (a
					   value that only the audio ucode produces) and the first
					   display-list fetch issued for k0 = 0x152C03C0 -- a value
					   that exists in RDRAM only at 0x411998.. (the AUDIO task's
					   command list) and in DMEM only at 0x308 of the AUDIO's own
					   data image (wd_r30dm.bin), never at 0xF0/0xBF8/0xFF0 where
					   the F3DEX2 entry reads k0.  A ucode load is a handful of
					   events per task, so recompiling everything is cheap and it
					   is the only thing that is unconditionally correct. */
					if (wd_bank_limited)
						rsp->dirty_blocks = ~0u;

					rsp->imem[(dest_addr & 0xfff) >> 2] = word;
				}
				else
					rsp->dmem[dest_addr >> 2] = word;

				j += 4;
			} while (j < length);

			source += length + skip;
			dest += length;
		} while (++i <= count);

		/* ROUND 31: the RSP just rewrote (part of) its own program.  Tell the
		   JIT now -- the ucode may jump into the new bytes before this slice
		   ends (see the note at the write above and the one in rsp_jit.cpp
		   above invalidate_code()). */
		if (wd31_wrote_imem)
		{
			rsp_imem_dma_bump();

			/* ==========================================================
			   ROUND 31 DIAGNOSTIC (temporary, removed once the round is
			   answered): the F3DEX2 text entry loads the display-list
			   pointer with `lw k0,0xFF0(r0)` at IMEM 0x160.  That load
			   demonstrably does not take effect -- the first fetch is
			   issued with $k0 = the AUDIO task's leftover 0x152C03C0 --
			   while the surrounding entry code (DMEM 0xF0, the overlay
			   descriptor re-base, the loader call) demonstrably does.
			   Two explanations remain and they are told apart by one
			   number: patch that instruction into `lui k0,0x5A5A`.
			     * fetch DMA line reports k0=5a5a0000  -> pc 0x160 IS
			       executed, and the JIT mis-executes the load itself.
			     * fetch DMA line still reports k0=152c03c0 -> pc 0x160 is
			       NOT executed at all, i.e. the block the JIT runs for the
			       entry is not the one the text bytes describe.
			   Patched once per whole-ucode text load (dest IMEM 0x080,
			   0xF80 bytes), which is exactly the rspboot transfer. */
			/* ROUND 31: no IMEM patching -- the diagnostic markers were
			   removed once run 31e answered the question. */
		}

		*rsp->cp0.cr[CP0_REGISTER_DMA_DRAM] = source;
		*rsp->cp0.cr[CP0_REGISTER_DMA_CACHE] = dest;

		/* ==================================================================
		   ROUND 29 FIX (DD route only) -- KEEP THE F3DEX2 OVERLAY-DESCRIPTOR
		   FIX-UP IDEMPOTENT.  THIS IS THE BLACK SCREEN.

		   The F3DEX2 text entry (IMEM pc 0x12C..0x15C) converts four
		   ucode_data-relative overlay descriptors into absolute RDRAM
		   addresses by ADDING the ucode base:

		       pc 0x12C  lw  at, 0xFD0(r0)     ; at = DMEM[0xFD0] = t.ucode
		       pc 0x130  lw  v0, 0x2E0(r0)     ; overlay descriptor A
		       pc 0x134  lw  v1, 0x2E8(r0)     ; overlay descriptor B
		       pc 0x138  lw  a0, 0x410(r0)
		       pc 0x13C  lw  a1, 0x418(r0)
		       pc 0x140  add v0, v0, at        ; *** ADD THE BASE ***
		       pc 0x148..0x15C  sw back to 0x2E0/0x2E8/0x410/0x418

		   It is correct exactly once per ucode_data load.  Those four words
		   live in DMEM 0x2E0..0x2EF / 0x410..0x41F and hold whatever the
		   ucode_data DMA left there.

		   WHY IT RUNS MORE THAN ONCE (decomp src/libultra/io/sptask.c):
		   osSpTaskLoad does `tp = osVirtualToPhysical(intp)` and then, for a
		   yielded task, BOTH `tp->t.ucode_data = tp->t.yield_data_ptr` AND
		   `intp->t.flags &= ~OS_TASK_YIELDED`.  Because tp == intp that clear
		   lands in the very OSTask that is DMA'd to DMEM 0xFC0 -- so the
		   header the ucode sees NEVER has OS_TASK_YIELDED set, and therefore
		   the entry NEVER takes its resume path (pc 0x0B8 `j 0x164`, which is
		   the branch that would skip the fix-up).  On a resume the DMA source
		   is the yield buffer, which holds the SAVED DMEM -- and the saved
		   descriptors are ALREADY ABSOLUTE -- so the fix-up is applied to
		   absolute values.

		   MEASURED (round-28 wd_k0.txt and the r28a/ram.bin dump): DMEM
		   0x2E0/0x2E8 end up holding 0x00EA1B00 / 0x00EA1B98, and

		       0x751540 + 0x7505C0 == 0xEA1B00    (descriptor A, base added twice)
		       0x7515D8 + 0x7505C0 == 0xEA1B98    (descriptor B, base added twice)

		   Neither value occurs anywhere in the 8 MB RDRAM dump, so they can
		   only be the fix-up's own output.  The overlay loader (pc 0x164 ->
		   0xFB4) then DMAs from RDRAM 0xEA1B98 -- past the end of RDRAM, so
		   the 24-bit mask makes it 0x6A1B98, the 64DD data area -- and copies
		   0x170 bytes of that over IMEM 0x000..0x16F, destroying the FIFO
		   ucode's own overlay.  The RSP then executes data, which is round
		   28's `WILD dir=RD dram=00ea1b98`, round 27's zero write-DMAs, round
		   22's "the RDP is fed zeros", and k0 = 0x152C03C0 being INHERITED
		   from the audio ucode instead of read from DMEM[0xFF0].

		   THE FIX.  Normalise the four descriptors in the DESTINATION of every
		   ucode_data-size READ into DMEM 0, i.e. at the exact moment they
		   arrive and immediately before the entry reads them.  Repeatedly
		   subtract the ucode base until each value is below it: the true value
		   is `offset + k*base` with `offset < base` (they are ucode_data
		   offsets and the ucode is 0x1000 bytes), so the loop recovers the
		   offset exactly for ANY number of accidental adds, and it is a no-op
		   on a genuine fresh load (0xF80/0x1018/0x1188/0x250 are far below
		   0x7505C0).

		   DD-gated by rsp_ares_budget_enabled() (the core's runtime
		   IsDDPresent()), so plain carts and the cart-hack route keep the
		   stock DMA handler byte for byte (user rule 2026-09-05). */
		if (rsp_ares_budget_enabled() && (r29_dma_dest0 & 0x1000u) == 0u &&
		    (r29_dma_dest0 & 0xFFFu) == 0u && (length + skip) >= 0x420u)
		{
			/* ROUND 41: added 0x280/0x288 for ucode 0x752AE0 which uses
			   DMEM 0x280/0x288 (not 0x2E0/0x2E8) as overlay descriptors.
			   Both pairs normalize identically. */
			static const unsigned r29_off[6] = { 0x280u, 0x288u, 0x2e0u, 0x2e8u, 0x410u, 0x418u };
			uint32_t r29_base = rsp->dmem[0xfd0 / 4];
			if (r29_base >= 0x1000u && r29_base < 0x800000u)
			{
#if R40_NORM
				/* ============================================================
				   ROUND 40 -- THE DOUBLE-ADD REPAIR, AND THE MEASUREMENT THAT
				   NAMED IT.

				   MEASURED THIS ROUND (run r40e, emumode=2, the DD route, the
				   RSP plugin's IMEM-load latch `wd_imem.txt`):

				     R19IMEM n=5 pc=000 dst=1000 src=753af8 len=0170
				     R19IMEM n=7 pc=000 dst=1080 src=752ae0 len=0f80   <- text
				     R19IMEM n=8 pc=020 dst=1000 src=ea65d8 len=0170   <- WILD

				   n=5 and n=8 are the SAME 0x170-byte overlay load (IMEM 0x000),
				   from 0x753AF8 and from 0xEA65D8 -- and

				       0x753AF8 + 0x752AE0 (the header's `ucode`) == 0xEA65D8

				   exactly, i.e. **the ucode base was added to a descriptor that
				   was already absolute**.  The RSP then copies 0x170 bytes of
				   whatever sits at the masked 0x6A65D8 over its own overlay at
				   IMEM 0x000, executes it as code, and never returns -- the
				   `R26W wild` storm (18.6M in this run) and the frozen 64DD
				   screen are that loop.  `files/wd_wild.txt` latches the same
				   address: `dir=RD pc=020 dram=00ea65d8 mem=00001000 len=0170`.

				   WHY THE DESCRIPTOR IS ALREADY ABSOLUTE: the F3DEX2 entry
				   fix-up converts the four ucode_data descriptors at DMEM
				   0x2E0/0x2E8/0x410/0x418 from ucode-relative to absolute by
				   adding the base.  Those words are live ucode state (round 32
				   measured them walking 0x751540 -> 0x08E60580 -> 0x059803C0),
				   so a task that is suspended AFTER the conversion -- which is
				   exactly what the DD route's libultra yield/resume dance does
				   between the gfx and audio tasks -- carries ABSOLUTE values
				   into the saved DMEM image.  The resume loads that image back
				   into DMEM 0, the entry adds the base a second time, and the
				   overlay load goes wild.

				   THE REPAIR: normalize the descriptors back to ucode-relative
				   form at the moment a ucode_data-sized READ delivers them into
				   DMEM 0 -- i.e. before the entry can read them.
				   `while (v >= base) v -= base` recovers the relative value for
				   any number of accidental adds, and is a NO-OP on a genuine
				   fresh load, whose descriptors are the ucode_data constants
				   0xF80/0x1018 and are far below any ucode base.

				   ROUND 41: the original r40 test (R40_NORM=1) normalized only
				   DMEM 0x2E0/0x2E8/0x410/0x418 -- the descriptors of ucode
				   0x7505C0 (task A).  But ucode 0x752AE0 (task B, the DD
				   game's actual gfx ucode) uses DMEM 0x280/0x288 for its own
				   overlay descriptors, which the round-40 normalisation never
				   touched.  The wild DMA (0xEA65D8) was traced to the
				   double-add at 0x288: 0x753AF8 + 0x752AE0 = 0xEA65D8.  Adding
				   0x280/0x288 to r29_off closes this gap.

				   DD-gated by rsp_ares_budget_enabled() (the core's runtime
				   IsDDPresent()) like every other change in this file, so plain
				   carts keep the stock handler byte for byte.  R40_NORM=0
				   restores the previous behavior for one-build A/B. */
				{
					/* ROUND 41: the plain `while (v >= base)` loop from round 40
					   also fires on LIVE ucode state at DMEM 0x2E0/0x2E8 that
					   legitimately exceeds the base -- e.g. 0x07100D08 from the
					   yield buffer's DMEM snapshot.  Eight subtractions still
					   leave it above the threshold and corrupt it, which feeds
					   stale addresses into the walk.  FIX: after subtracting base
					   enough times, ACCEPT only if the result is below 0x2000 --
					   the maximum overlay-text/data offset for any F3DZEX2 ucode.
					   This catches any number of accidental base-adds (the loop
					   runs up to 8 times) while rejecting all live data, whose
					   residual after subtraction is still well above 0x2000. */
					static unsigned r40_norm_ev = 0, r40_norm_w = 0;
					unsigned r40_i, r40_ch = 0;
					for (r40_i = 0; r40_i < 6u; r40_i++)
					{
						unsigned r40_idx = r29_off[r40_i] / 4u;
						uint32_t r40_v = rsp->dmem[r40_idx];
						uint32_t r40_try = r40_v;
						unsigned r40_g = 0;
						while (r40_try >= r29_base && r40_g < 8u)
						{
							r40_try -= r29_base;
							r40_g++;
						}
						if (r40_g && r40_try < 0x2000u)
						{
							rsp->dmem[r40_idx] = r40_try;
							r40_ch++;
						}
					}
					if (r40_ch)
					{
						r40_norm_ev++;
						r40_norm_w += r40_ch;
						if (r40_norm_ev <= 24u)
						{
							FILE* nf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r40norm.txt",
							                 r40_norm_ev == 1u ? "w" : "a");
							if (nf)
							{
								fprintf(nf, "R40NORM n=%u base=%08x src=%06x len=%05x ch=%u now %08x %08x %08x %08x %08x %08x\n",
								        r40_norm_ev, r29_base, source & 0xFFFFFFu, length, r40_ch,
								        rsp->dmem[0x280 / 4], rsp->dmem[0x288 / 4],
								        rsp->dmem[0x2e0 / 4], rsp->dmem[0x2e8 / 4],
								        rsp->dmem[0x410 / 4], rsp->dmem[0x418 / 4]);
								fclose(nf);
							}
						}
					}
				}
#endif
				/* ROUND 29 FIX + DIAG (see the block comment above).
				   MEASURED (run 29e, the only two ucode_data-size READs into
				   DMEM 0 in a whole 105 s run):

				     n=1 src=779860 len=00800  2e0=00000f80 2e8=00001018
				                              410=00001188 418=00000250  <- FRESH
				     n=2 src=32dcd0 len=00c00  2e0=00010001 2e8=00010001
				                              410=00010001 418=00010001  <- RESUME

				   The resume source is the yield buffer, and the overlay slots
				   in it hold the game's own 0x00010001 fill: by the time the gfx
				   ucode saved DMEM, the AUDIO ucode had already overwritten them
				   (the round-25 R25W watch shows im0=340a0fc0 rewriting DMEM
				   0x2E0/0x410/0x418 continuously).  The entry's fix-up then adds
				   the ucode base to 0x00010001, producing an out-of-range
				   descriptor, and the overlay load copies garbage over IMEM.

				   So the canonical values must come from the FRESH load: latch
				   them there and put them back whenever a later load of the same
				   task delivers anything else.  This is exact (they are the
				   ucode_data constants 0x00000F80/0x00001018/0x00001188/0x00000250)
				   and it is keyed on the ucode base, so a different task's load
				   re-latches rather than being corrupted. */
				static uint32_t r29_good[4];
				static uint32_t r29_good_base = 0;
				static int r29_have_good = 0;
				unsigned r29_i, r29_fired = 0;

				/* NOTE (run 29f got this wrong): you CANNOT tell a fresh load
				   from a resume by comparing the DMA source with the header's
				   `ucode_data` field -- libultra's osSpTaskLoad sets
				   `tp->t.ucode_data = tp->t.yield_data_ptr` for a yielded task,
				   so on a resume the header points AT the yield buffer and both
				   loads look "fresh", which re-latched the corrupted values.
				   The only sound rule is: the FIRST load seen for a given ucode
				   base is the fresh one (it is the one that restored the real
				   constants 0xF80/0x1018/0x1188/0x250, measured as run 29e's
				   n=1), and every later load of the same base is a resume whose
				   overlay slots must be put back. */
				if (!r29_have_good || r29_good_base != r29_base)
				{
					for (r29_i = 0; r29_i < 4u; r29_i++)
						r29_good[r29_i] = rsp->dmem[r29_off[r29_i] / 4];
					r29_good_base = r29_base;
					r29_have_good = 1;
				}
				else
				{
					/* A resume (or any later load of the same task): restore.
					   ROUND 32 DISABLES THE WRITE-BACK.  Run 32a measured the
					   descriptors the ucode itself leaves behind, and they are
					   NOT corruption: DMEM[0x2E0] legitimately walks
					   0x751540 -> 0x08E60580 -> 0x059803C0 as the task changes
					   which segment it is loading, and this restore reverted
					   every one of those updates every time it ran (fired=4/4
					   on every task after the first).  Reverting them forces
					   the ucode to reload the WRONG segment, which is a direct
					   candidate for the wrong overlay code resident at IMEM
					   0x000 (measured: the F3DEX2 output-buffer registers s6/s7
					   are clobbered at the flush and the RDP flush's DMA then
					   runs backwards).  Kept as a counter so the log still
					   reports what WOULD have been reverted. */
					for (r29_i = 0; r29_i < 4u; r29_i++)
					{
						if (rsp->dmem[r29_off[r29_i] / 4] != r29_good[r29_i])
							r29_fired++;
					}
				}
				if (r29_fired || r29_base > 0x1000u)
				{
					/* ROUND 29 DIAG: log EVERY ucode_data-size READ into DMEM 0
					   together with the descriptor words it left behind -- not
					   only the ones that needed normalising.  Runs 29c/29d never
					   fired, so the open question is exactly WHAT such a transfer
					   delivers; this line answers it (`fired=0` with relative
					   descriptors means no resume image ever carries absolute
					   ones; `2e0=00751540 fired=1` proves the fix).  Capped. */
					static unsigned r29_n = 0;
					if (++r29_n <= 40u)
					{
						FILE* ff = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r29fix.txt",
						                 r29_n == 1u ? "w" : "a");
						if (ff)
						{
							fprintf(ff, "R29FIX n=%u base=%08x dest=%04x src=%06x len=%05x skip=%05x count=%u fired=%u "
							            "2e0=%08x 2e8=%08x 410=%08x 418=%08x im0=%08x\n",
							        r29_n, r29_base, r29_dma_dest0, source & 0xFFFFFFu, length, skip, count, r29_fired,
							        rsp->dmem[0x2e0 / 4], rsp->dmem[0x2e8 / 4],
							        rsp->dmem[0x410 / 4], rsp->dmem[0x418 / 4],
							        rsp->imem[0]);
							fclose(ff);
						}
					}
				}
			}
		}

#ifdef INTENSE_DEBUG
		log_rsp_mem_parallel();
#endif
		return rsp->dirty_blocks ? MODE_CHECK_FLAGS : MODE_CONTINUE;
	}

	/* =======================================================================
	   ROUND 32: catch the RDP output-buffer flush wherever it goes.

	   The F3DEX2 flush (pc 0x250..0x2CC) DMAs DMEM [s6-0x2158] (0xBA8 or 0xDB0
	   masked) to RDRAM [DMEM[0x0F0]] and then advances DMEM[0x0F0].  The whole
	   run's write-DMA census shows only TWO writes from the gfx ucode (the
	   ring flush of 8 bytes at task end and the 0xC00-byte yield save), so log
	   every write whose DMEM source is that buffer, plus every write that
	   lands in the ring, with the first 8 source words.
	   ======================================================================= */
	static unsigned r32_wrn = 0;
	/* build marker: verify the packaged .so really carries round 32 */
	static const char r32_marker[] __attribute__((used)) = "R32FLUSH";
	/* ROUND 33: the last block-entry pc, published by parallel.cpp's
	   r30_pc_hook.  rsp->pc is NOT maintained by this JIT (it reads 0 for every
	   DMA in the r32 census), so this is the only usable issuer pc. */
	extern "C" unsigned r33_last_pc;
	static void r32_wr_note(RSP::CPUState* rsp, uint32_t dest, uint32_t source,
	                        uint32_t length, unsigned count, uint32_t skip)
	{
		unsigned so = source & 0x1fffu;
		FILE* f;
		/* ROUND 39: PLAIN-ROUTE REGRESSION FIX (user-visible: lag spikes and
		   audio crackle in Mario Tennis).  This instrument was the one
		   per-DMA file trace still running on EVERY game: it fopen/append/
		   fclose'd on each of the first 3000 write DMAs of the session, i.e.
		   thousands of file operations in the RSP's hot path of a plain cart,
		   which this project's own rule forbids.  It is now DD-gated and
		   behind the deep flag like every other per-transfer trace. */
		if (!rsp_ares_budget_enabled() || !rsp_diag_deep())
			return;
		if (r32_wrn >= 3000u)
			return;
		/* ROUND 33: NO FILTER.  Round 32's filter let an unrelated 0x170-byte
		   overlay copy (DMEM 0xC80/0xE20 -> RDRAM 0x415xxx) fill the 300-line
		   cap, so the flush's own DMA was never recorded and "the ring is
		   never written" was an artifact of the census.  Log them all, with
		   the issuer pc and the ring pointer. */
		r32_wrn++;
		f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r32wr.txt",
		          r32_wrn == 1u ? "w" : "a");
		if (f == NULL)
			return;
		fprintf(f, "R33WR n=%u ipc=%03x dst=%08x raw=%08x src=%04x len=%05x cnt=%u "
		           "skip=%u f0=%08x fe8=%08x fec=%08x\n",
		        r32_wrn, r33_last_pc & 0xfffu, dest, source, so, length,
		        count, skip, ((const uint32_t*)RSP::rsp.DMEM)[0x0f0 / 4],
		        ((const uint32_t*)RSP::rsp.DMEM)[0xfe8 / 4],
		        ((const uint32_t*)RSP::rsp.DMEM)[0xfec / 4]);
		fclose(f);
	}

	/* ======================================================================
	   ROUND 35 (DD route only): THE WRAP THAT DESTROYS THE EXCEPTION VECTOR.

	   MEASURED offline on the archived full-RAM dumps: RDRAM 0x00000180 (the
	   R4300 general exception vector) holds libultra's prologue
	   3C1A800C 275AC4C0 in EVERY full-RAM dump from round 3 through round 32,
	   and RSP microcode (4B8641B3 E9DA0F06 4B914473 E9C40F05 ...) in the
	   round-33 dump.  In that dump RDRAM 0x0..0x3F8 is byte-identical to cart
	   ROM 0x63758, i.e. the F3DEX2 ucode's data section, while RDRAM 0x400
	   upward still holds the game's 0x00010001 fill.  A 0x400-byte body
	   ending at exactly 0x3F8 is the signature of a WRAP, not of a pointer
	   that was simply set to 0.

	   The transfer loop below masks every word address with 0x7FFFFC
	   (hardware-accurate: RDRAM is 8 MiB, so the top of the 24-bit SP address
	   space mirrors it), therefore a write whose dest register sits near the
	   TOP of RDRAM carries its tail around into physical 0x0.  With
	   dest = 0xFFFFF8 -- the stale 0xFFFFFF the previous task leaves in
	   SP_DRAM_ADDR, see the round-26 note in r14_wild_check -- and len = 0x400
	   that is exactly 254 words landing on RDRAM 0x0..0x3F8, the observed
	   damage.

	   Round 12 already had a trigger for "a write that lands in RDRAM's first
	   page", but it tested the REGISTER value
	   (`(dst & 0x7FFFFC) >= 0x1000 -> return`), which 0xFFFFF8 fails, so it
	   never fired once in twenty rounds.  Every guard in this file has
	   reasoned "the mask keeps the transfer inside RDRAM" -- true, and
	   irrelevant: inside RDRAM is where the vectors are.

	   So: every wrapped write is logged to wd_lowsp.txt, and the words that
	   would land below R35_GUARD_LO are SKIPPED rather than deposited, so the
	   guest's boot/exception page survives.  Plain carts are untouched
	   (rsp_ares_budget_enabled() is the core's runtime IsDDPresent()). */
#define R35_GUARD_LO 0x1000u

	static void r35_lowsp_log(RSP::CPUState* rsp, uint32_t dst, uint32_t src,
	                          uint32_t len, unsigned count, uint32_t skip,
	                          uint32_t d0, uint32_t span, uint32_t skipped)
	{
		static unsigned n = 0;
		FILE* f;
		if (n >= 32) return;
		n++;
		f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_lowsp.txt",
		          (n == 1) ? "w" : "a");
		if (f == NULL) return;
		fprintf(f, "WDLOWSP n=%u pc=%03x dst=%08x src=%08x len=%04x cnt=%u skip=%03x "
		           "d0=%08x span=%08x skipped=%u | dm=%08x cache=%08x st=%08x "
		           "imem0=%08x dmem0=%08x fc0=%08x ff0=%08x bf8=%08x\n",
		        n, rsp->pc & 0xfffu, dst, src, (unsigned)len, count, (unsigned)skip,
		        d0, span, skipped,
		        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_DRAM],
		        *rsp->cp0.cr[RSP::CP0_REGISTER_DMA_CACHE],
		        *rsp->cp0.cr[RSP::CP0_REGISTER_SP_STATUS],
		        rsp->imem[0], rsp->dmem[0], rsp->dmem[0xfc0 / 4],
		        rsp->dmem[0xff0 / 4], rsp->dmem[0xbf8 / 4]);
		fclose(f);
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
		r36_hdr_canary(rsp, 1u, dest, source, length, count);
		r14_record(rsp, 1, dest, source, length, count, skip);
		r20_dma_save_note(rsp, dest, length, source);
		r30_dma_line(1, dest, source, length, rsp->pc & 0xfffu);
		r32_wr_note(rsp, dest, source, length, count, skip);

		/* ROUND-18: refuse the transfer if the ucode's DMA address register
		   has left RDRAM (runaway walk -- see r14_wild_check).  This is the
		   path the 29.9-million-transfer wipe used.  The caller ignores this
		   function's result, so refuse by simply not transferring; the
		   deterministic slice budget still ends the runaway's slice. */
		if (r14_wild_check(rsp, 1, dest, source, length, count, skip))
			return;

#ifdef INTENSE_DEBUG
		fprintf(stderr, "DMA WRITE: (0x%x <- 0x%x) len %u, count %u, skip %u\n", dest & 0x7ffffc, source & 0x1ffc,
		        length, count + 1, skip);
#endif

		unsigned i = 0;
		/* ROUND 35: how far the per-word wrap can carry this transfer, and the
		   guard that keeps it out of the guest's boot/exception page. */
		const uint32_t r35_dst0 = dest;
		const uint32_t r35_src0 = source;
		const uint32_t r35_d0 = dest & 0x7FFFFCu;
		const uint32_t r35_span = length + ((uint32_t)count * (length + skip));
		const int r35_wraps = rsp_ares_budget_enabled() &&
		                      ((r35_d0 + r35_span) > 0x800000u);
		uint32_t r35_skipped = 0;
		do
		{
			unsigned j = 0;

			do
			{
				uint32_t source_addr = (source + j) & 0x1FFC;
				uint32_t dest_addr = (dest + j) & 0x7FFFFC;

				if (r35_wraps && dest_addr < R35_GUARD_LO) {
					r35_skipped++;
				} else {
					rsp->rdram[dest_addr >> 2] =
					    (source_addr & 0x1000) ? rsp->imem[(source_addr & 0xfff) >> 2] : rsp->dmem[source_addr >> 2];
				}

				j += 4;
			} while (j < length);

			source += length;
			dest += length + skip;
		} while (++i <= count);

		if (r35_wraps || r35_skipped)
			r35_lowsp_log(rsp, r35_dst0, r35_src0, length, count, skip,
			              r35_d0, r35_span, r35_skipped);

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
#ifdef PARALLEL_INTEGRATION
			r36_cmd_note(rsp, 0u, val & 0xfffffff8u);
			r19_cmd_latch(rsp, "START", val & 0xfffffff8u);
#endif
			break;

		case CP0_REGISTER_CMD_END:
#ifdef INTENSE_DEBUG
			fprintf(stderr, "CMD_END 0x%x\n", val & 0xfffffff8u);
#endif
			*rsp->cp0.cr[CP0_REGISTER_CMD_END] = val & 0xfffffff8u;

#ifdef PARALLEL_INTEGRATION
			r36_cmd_note(rsp, 1u, val & 0xfffffff8u);
			r19_cmd_latch(rsp, "END", val & 0xfffffff8u);
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
