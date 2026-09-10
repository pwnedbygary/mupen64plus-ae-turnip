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
	fclose(f);
}

/* The first CMD_START/CMD_END the ucode programmes.  CMD_START is the one that
   matters: the plugin sets START=CURRENT=END from it, so a garbage START makes
   every later kick an empty window. */
static unsigned r19_cmd_n = 0;
static int r19_cmd_latch(RSP::CPUState* rsp, const char* what, uint32_t val)
{
	FILE* f;
	if (r19_cmd_n >= 16)
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
   save itself; 0 = the round-10..19 behaviour (fabricate INTR_BREAK|HALT and
   the ack).  Kept as a switch so the two models can be measured back to back
   on one build. */
#define R20_SIG0_YIELD 0

/* ROUND 21 (DD route only): 1 = on a host-forced yield, emulate the ucode's own
   yield save (DMEM[0xBF8] = live k0, DMEM[0xBFC] = ucode base, DMEM[0..0xBFF] ->
   the header's yield_data_ptr) and answer with SIG1|SIG2 while LEAVING SIG0 set
   (libultra's osSpTaskYielded only records OS_TASK_YIELDED while SIG0 is still
   visible).  0 = the round-10..20 behaviour, kept for A/B measurement on one
   build.  See the long note at the yield site in RSP_MFC0. */
#define R21_KEEP_SIG0 0

static unsigned r21_save_n = 0, r21_yield_n = 0, r21_log_n = 0;
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

/* Called by DoRspCycles for every fresh task entry. */
extern "C" void r20_task_begin(void) { r20_ystage = 0; r20_ystage_poll = 0; }

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
				uint32_t yptr = dmem[0xff8 / 4];
				uint32_t k0 = rsp->sr[26];
				int sig0 = (st_before & SP_STATUS_SIG0) != 0;
				r21_yield_n++;
				dmem[0xbf8 / 4] = k0;              /* the resume DL pointer */
				dmem[0xbfc / 4] = dmem[0xfd0 / 4]; /* the resume ucode base */
				r21_save_k0 = k0;
				r21_save_f0 = dmem[0xf0 / 4];
				/* Same word-indexed convention as rsp_dma_write above. */
				if (RSP::rsp.RDRAM != NULL && (yptr & 0x7fffffu) <= 0x800000u - 0xc00u)
				{
					uint32_t* rd = (uint32_t*)RSP::rsp.RDRAM;
					unsigned i;
					for (i = 0; i < 0xc00u / 4u; i++)
						rd[((yptr & 0x7ffffcu) >> 2) + i] = dmem[i];
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
						fprintf(f, "R21Y n=%u pc=%03x sig0=%d st=%08x k0=%08x f0=%08x bf8=%08x yptr=%08x typ=%u flg=%08x saved=%u\n",
						        r21_yield_n, rsp->pc & 0xfff, sig0, st_before, k0,
						        dmem[0xf0 / 4], dmem[0xbf8 / 4], yptr,
						        dmem[0xfc0 / 4], dmem[0xfc4 / 4], r21_save_n);
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
			/* ROUND 21: answer the yield the way the ucode does (SIG1 =
			   SP_STATUS_YIELDED, SIG2 = TASKDONE) but LEAVE SIG0 SET -- see
			   the libultra excerpt above: osSpTaskYielded() records
			   OS_TASK_YIELDED only while SP_STATUS_YIELD (SIG0) is still
			   visible, and osSpTaskLoad() clears SIG0 at the next task load,
			   so leaving it set is the correct, self-limiting handshake.
			   R21_KEEP_SIG0=0 restores the round-16 form (clear SIG0) for
			   A/B measurement. */
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
	   path byte for byte and their DMA behaviour is untouched. */
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
		return 1;
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
		r19_imem_note(rsp, dest, source, length);
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
		   (SP_MEM_ADDR=0x1080, RD_LEN=0xF7F: 4096 bytes into IMEM starting at
		   0x080) ends exactly on the bank boundary, so bank-limited wrapping
		   reproduces it byte for byte while stopping a stray length from
		   crossing banks.  Plain carts keep the stock mask (user rule
		   2026-09-05). */
		{
			unsigned j = 0;
			const uint32_t wd_bank_limited = rsp_ares_budget_enabled() ? 1u : 0u;
			const uint32_t wd_dbank = dest & 0x1000u;
			do
			{
				uint32_t source_addr = (source + j) & 0x7FFFFC;
				uint32_t dest_addr = wd_bank_limited
				                   ? (wd_dbank | ((dest + j) & 0xFFCu))
				                   : ((dest + j) & 0x1FFCu);
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
		r20_dma_save_note(rsp, dest, length, source);

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
#ifdef PARALLEL_INTEGRATION
			r19_cmd_latch(rsp, "START", val & 0xfffffff8u);
#endif
			break;

		case CP0_REGISTER_CMD_END:
#ifdef INTENSE_DEBUG
			fprintf(stderr, "CMD_END 0x%x\n", val & 0xfffffff8u);
#endif
			*rsp->cp0.cr[CP0_REGISTER_CMD_END] = val & 0xfffffff8u;

#ifdef PARALLEL_INTEGRATION
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
