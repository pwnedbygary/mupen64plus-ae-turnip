#ifdef DEBUG_JIT
#include "debug_rsp.hpp"
#else
#include "rsp_jit.hpp"
#endif
#include <stdint.h>
#include <chrono>

#include "m64p_plugin.h"
#include "rsp_1.1.h"

#define RSP_PARALLEL_VERSION 0x0101
#define RSP_PLUGIN_API_VERSION 0x020000

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;


#define ATTR_FMT(fmtpos, attrpos) __attribute__ ((format (printf, fmtpos, attrpos)))
static void DebugMessage(int level, const char *message, ...) ATTR_FMT(2, 3);

void DebugMessage(int level, const char *message, ...)
{
    char msgbuf[1024];
    va_list args;

    if (l_DebugCallback == NULL)
        return;

    va_start(args, message);
    vsprintf(msgbuf, message, args);

    (*l_DebugCallback)(l_DebugCallContext, level, msgbuf);

    va_end(args);
}

extern "C" void rsp_set_budget_deadline_us(long long us);
extern "C" int rsp_budget_expired_now(void);
/* ROUND-18 DIAG: emulated work (budget checks) this slice consumed, and whether
   the wall-clock backstop -- not the deterministic countdown -- ended it. */
extern "C" unsigned long long rsp_slice_units_now(void);
extern "C" int rsp_budget_wall_hit_now(void);

namespace RSP
{
RSP_INFO rsp;
#ifdef DEBUG_JIT
RSP::CPU cpu;
#else
RSP::JIT::CPU cpu;
#endif
short MFC0_count[32];
int SP_STATUS_TIMEOUT;
} // namespace RSP

/* DIAG: hard cap on the DD RSP task trace.  wd_rsp.txt is written per task
   entry AND exit; when a ucode dead-waits on SP_STATUS the core re-schedules
   ~60x/s, and with the DD poll budget lowered each slice got cheap enough that
   the trace reached 4.5 GB in 75s -- the logging then dominated the emulation
   thread and invalidated the measurement.  Cap it and stop writing. */
static unsigned long wd_rsp_log_n = 0;
#define WD_RSP_LOG_MAX 40000UL

/* DD gate for the JIT-side budget/watchdog emission (rsp_jit.cpp): the core
   provides a RUNTIME IsDDPresent() query (evaluated at task time, after
   init_device set dd.idisk), so plain cart games get structurally-identical
   JIT code (no per-loop host calls). */
extern "C" int rsp_ares_budget_enabled(void)
{
	return RSP::rsp.IsDDPresent && RSP::rsp.IsDDPresent();
}

extern "C"
{
	// Hack entry point to use when loading savestates when we're tracing.
	void rsp_clear_registers()
	{
		memset(RSP::cpu.get_state().sr, 0, sizeof(uint32_t) * 32);
		memset(&RSP::cpu.get_state().cp2, 0, sizeof(RSP::cpu.get_state().cp2));
	}

#ifdef INTENSE_DEBUG
	// Need super-fast hash here.
	static uint64_t hash_imem(const uint8_t *data, size_t size)
	{
		uint64_t h = 0xcbf29ce484222325ull;
		size_t i;
		for (i = 0; i < size; i++)
			h = (h * 0x100000001b3ull) ^ data[i];
		return h;
	}

	void log_rsp_mem_parallel(void)
	{
		fprintf(stderr, "IMEM HASH: 0x%016llx\n", hash_imem(RSP::rsp.IMEM, 0x1000));
		fprintf(stderr, "DMEM HASH: 0x%016llx\n", hash_imem(RSP::rsp.DMEM, 0x1000));
	}
#endif

static inline long long wd_now_ms()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* DIAG: freeze heartbeat — log RSP state when one task run exceeds 30ms.
   Called from rsp_enter at every JIT block boundary (rsp_jit.cpp), so even
   self-branching ucode loops produce rate-limited lines with the live pc. */
/* ROUND 27 (DD route only): the RSP's SP_DMA_BUSY / SP_DMA_FULL as the CORE
   last left them, sampled once per DoRspCycles slice.  Used by the R20 summary
   and by the redirect below. */
static uint32_t r27_raw_busy = 0, r27_raw_full = 0;

static long long wd_task_start_ms = 0;
static long long wd_last_hb_ms = 0;
static FILE* whf = NULL;

extern "C" void rsp_watchdog_tick(unsigned pc_lo)
{
	long long now = wd_now_ms();
	if (now - wd_task_start_ms < 30) return;
	if (now - wd_last_hb_ms < 10) return;
	wd_last_hb_ms = now;
	if (!whf) whf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_freeze.txt", "a");
	if (whf) {
		fprintf(whf, "ms=%lld pc=%04x busy=%u full=%u status=%08x irq=%u\n", now,
			pc_lo & 0xfff, *RSP::rsp.SP_DMA_BUSY_REG,
			*RSP::rsp.SP_DMA_FULL_REG, *RSP::rsp.SP_STATUS_REG,
			*RSP::cpu.get_state().cp0.irq & 1);
		fflush(whf);
	}
}

/* ======================================================================
   ROUND 29 DIAG (DD route only): THE PC RING -- HOW DOES THE RSP GET TO pc 0?

   Round 28's wd_rsp.txt (RSPTASK) says, for the F-Zero X EK gfx task:

       3372 of 3530 slices ENTER at pc=0000 with imem0=0x09000419 (rspboot's
       `j 0x1064` resident at IMEM 0x000), and 157 ENTER at pc=0x0fc8.
       imem0 is rspboot for EVERY gfx slice.

   SP_PC_REG is saved at every slice exit (`0x04001000 | (pc & 0xffc)`) and
   restored at every slice entry, and the guest only writes 0x1000 into it
   once per osSpTaskLoad (LOADGUARD set=200 == the 4 gfx + 196 audio loads).
   So a slice cannot ENTER at pc 0 unless the ucode itself REACHED pc 0 --
   which means rspboot runs again, re-DMAs the text, jumps to the F3DEX2
   entry at pc 0x080, and the display-list walk restarts from scratch.  That
   would explain every round-25..28 symptom at once (walk pointer never
   advances past the first chunk, ring never written, ring stays zero).

   WHAT THIS INSTRUMENT ANSWERS: which instruction jumped to pc 0, and with
   what register state.  rsp_jit.cpp calls r29_pc_hook() from rsp_enter(),
   i.e. at every JIT block boundary with the live pc, so the ring holds the
   block-level path INTO the restart.  On the first 6 times the RSP reaches
   pc 0 the tail of the ring plus the live registers, DMEM header words and
   the handler-dispatch table are dumped, and IMEM||DMEM (8 KiB) is written
   once so the code at the jumping pc can be disassembled offline.

   Read it as: R29PC gives the state, R29TRACE is the pc path (oldest first,
   the LAST entry is always 000).  dd-gated by rsp_ares_budget_enabled()
   (runtime IsDDPresent()) at the call site, so plain carts are untouched. */
#define R29_RING 512
static uint32_t r29_ring[R29_RING];
static uint32_t r29_seq = 0;       /* block entries recorded so far       */
static uint32_t r29_zero_n = 0;    /* times pc==0 seen while the hook ran */
static uint32_t r29_unfix_n = 0;   /* descriptor un-fixes applied         */

static FILE* r29f = NULL;

/* ---------------------------------------------------------------------
   ROUND 29 FIX (DD route only): KEEP THE F3DEX2 OVERLAY-DESCRIPTOR FIX-UP
   IDEMPOTENT.  THIS IS THE ROOT CAUSE OF THE WHOLE BLACK SCREEN.

   The F3DEX2 text entry (IMEM pc 0x080..0x15C, disassembled this round from
   the live ucode) does, on every entry that is not a libultra resume:

       pc 0x12C  lw   at, 0xFD0(r0)      ; at = the ucode base (0x7505C0)
       pc 0x130  lw   v0, 0x2E0(r0)      ; overlay descriptor A
       pc 0x134  lw   v1, 0x2E8(r0)      ; overlay descriptor B
       pc 0x138  lw   a0, 0x410(r0)
       pc 0x13C  lw   a1, 0x418(r0)
       pc 0x140  add  v0, v0, at         ; *** ADD THE BASE ***
       ...       sw   back to 0x2E0/0x2E8/0x410/0x418

   i.e. it converts {ucode_data-relative offsets} into {absolute RDRAM
   addresses} exactly once per ucode_data load.  The four words live in DMEM
   0x2E0..0x2EF / 0x410..0x41F and hold whatever the ucode_data DMA left.

   MEASURED HERE (round-28 wd_k0.txt and this round's r28a/ram.bin): DMEM
   0x2E0/0x2E8 end up holding 0x00EA1B00 / 0x00EA1B98, and

       0x751540 + 0x7505C0 == 0xEA1B00      (descriptor A, fixed up twice)
       0x7515D8 + 0x7505C0 == 0xEA1B98      (descriptor B, fixed up twice)

   Neither value occurs anywhere in the 8 MB RDRAM dump, so they can only be
   the fix-up's own output.  With the descriptors double-fixed the ucode's
   overlay loader (pc 0x164 -> 0xFB4 -> the DMA primitive at 0xFD8) DMAs from
   RDRAM 0xEA1B98: past the end of RDRAM, so the 24-bit mask makes it
   0x6A1B98 -- the 64DD data area -- and copies 0x170 bytes of that over
   IMEM 0x000..0x16F.  The FIFO ucode's own boot overlay is destroyed and the
   RSP then executes data.  Every round-19..28 symptom (no write DMA ever
   issued, the 336 KiB RDP ring all zero while DPC_END climbs, k0 =
   0x152C03C0 inherited from the audio task rather than DMEM[0xFF0], the
   `WILD dir=RD dram=00ea1b98` transfer) is downstream of this double-add.

   THE FIX.  At the instant the text entry is about to run -- rspboot's
   trampoline `jr a3` with a3 = 0x1080 lands on IMEM pc 0x080 -- put the four
   descriptors back into the form the entry expects by repeatedly subtracting
   the ucode base until the value is below it.  The true value is
   `offset + k*base` with `offset < base` (the descriptors are ucode_data
   offsets and the ucode is 0x1000 bytes), so the loop recovers the offset
   exactly for any number of accidental adds, and it is a no-op on a genuine
   fresh load (offsets 0xF80/0x1018/0x1188/0x250 are far below 0x7505C0).

   GATED ON THE UCODE'S OWN BRANCH: the entry runs the fix-up only when
   `DMEM[0xF0] == 0` (cold start) or `(DMEM[0xFC4] & 1) == 0` (not an
   OS_TASK_YIELDED resume).  On a real resume the ucode SKIPS it (pc 0x0B8
   `j 0x164`) and the restored yield image's already-absolute descriptors must
   be left alone, so the un-fix is applied only when the fix-up is about to
   run -- the exact complement of the ucode's `beq $11,$0,0x0C0` /
   `beq $12,$0,0x12C` pair.

   DD-only: r29_pc_hook is only reached from rsp_enter under
   rsp_ares_budget_enabled() (the runtime IsDDPresent()), so plain carts and
   the cart-hack route never execute any of this. */
static void r29_unfix_descriptors(void)
{
	uint32_t* dm = (uint32_t*)RSP::rsp.DMEM;
	static const unsigned off[4] = { 0x2e0u, 0x2e8u, 0x410u, 0x418u };
	uint32_t base = dm[0xfd0 / 4];
	uint32_t fifo = dm[0x0f0 / 4];
	uint32_t yld  = dm[0xfc4 / 4] & 1u;
	unsigned i;

	if (!(fifo == 0u || yld == 0u)) return;     /* the entry will skip it   */
	if (base < 0x1000u || base >= 0x800000u) return;
	for (i = 0; i < 4u; i++)
	{
		uint32_t v = dm[off[i] / 4];
		unsigned k = 0;
		while (v >= base && k < 8u) { v -= base; k++; }
		if (k) { dm[off[i] / 4] = v; r29_unfix_n++; }
	}
	/* Proof-of-fire file (the R29PC dump only exists on a pc==0 event, which
	   may never happen once the fix works).  Capped so it cannot grow. */
	if (r29_unfix_n != 0u && r29_unfix_n <= 40u)
	{
		FILE* ff = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r29fix.txt",
		                 r29_unfix_n == 1u ? "w" : "a");
		if (ff)
		{
			fprintf(ff, "R29FIX n=%u base=%08x f0=%08x fc4=%08x dm[2e0]=%08x dm[2e8]=%08x dm[410]=%08x dm[418]=%08x "
			            "im0=%08x seq=%u\n",
			        r29_unfix_n, base, fifo, dm[0xfc4 / 4],
			        dm[0x2e0 / 4], dm[0x2e8 / 4], dm[0x410 / 4], dm[0x418 / 4],
			        ((const uint32_t*)RSP::rsp.IMEM)[0], r29_seq);
			fclose(ff);
		}
	}
}

extern "C" void r29_pc_hook(unsigned pc_lo)
{
	uint32_t p = pc_lo & 0xfffu;
	uint32_t i, n;
	const uint32_t* dm = (const uint32_t*)RSP::rsp.DMEM;
	const uint32_t* im = (const uint32_t*)RSP::rsp.IMEM;
	const uint32_t* sr = RSP::cpu.get_state().sr;

	r29_ring[r29_seq & (R29_RING - 1u)] = p;
	r29_seq++;

	/* The F3DEX2 text entry is about to run: normalise the descriptors. */
	if (p == 0x080u) { r29_unfix_descriptors(); return; }

	if (p != 0u) return;
	/* The AUDIO task's legitimate task-start entries (audio ucode resident at
	   IMEM 0) consumed the round-29a dump budget and hid the gfx-side events
	   that matter, so skip them. */
	if (im[0] == 0x340a0fc0u) return;
	if (r29_zero_n >= 6u) return;
	r29_zero_n++;
	if (r29f == NULL)
		r29f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r29pc.txt", "w");
	if (r29f == NULL) return;
	{
		/* Rewritten on EVERY dump, so the image left on disk is the one from
		   the LAST pc==0 (the most interesting one, after the ucode has been
		   running), not the trivial task-start entry.  Same layout as
		   wd_ucode[123].bin minus the 32-byte header: 0x1000 IMEM then
		   0x1000 DMEM.  The jumping pc is disassembled out of IMEM. */
		FILE* sf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r29sp.bin", "wb");
		if (sf) {
			fwrite(RSP::rsp.IMEM, 1, 0x1000, sf);
			fwrite(RSP::rsp.DMEM, 1, 0x1000, sf);
			fclose(sf);
		}
	}
	fprintf(r29f, "R29PC n=%u seq=%u pc=0 k0=%08x ra=%08x v0=%08x v1=%08x t8=%08x t9=%08x s3=%08x s4=%08x\n",
	        r29_zero_n, r29_seq, (uint32_t)sr[26], (uint32_t)sr[31], (uint32_t)sr[2], (uint32_t)sr[3],
	        (uint32_t)sr[24], (uint32_t)sr[25], (uint32_t)sr[19], (uint32_t)sr[20]);
	fprintf(r29f, "R29DM fc0=%08x fc4=%08x fd0=%08x f0=%08x ff0=%08x ff4=%08x bf8=%08x 2e0=%08x 2e8=%08x 410=%08x 418=%08x st=%08x pc_reg=%08x\n",
	        dm[0xfc0 / 4], dm[0xfc4 / 4], dm[0xfd0 / 4], dm[0x0f0 / 4],
	        dm[0xff0 / 4], dm[0xff4 / 4], dm[0xbf8 / 4],
	        dm[0x2e0 / 4], dm[0x2e8 / 4], dm[0x410 / 4], dm[0x418 / 4],
	        *RSP::rsp.SP_STATUS_REG, *RSP::rsp.SP_PC_REG);
	/* The F3DEX2/F-Zero X command dispatch table the main loop indexes with
	   the command's top byte: pc = lhu DMEM[0x36e + 2*opcode] (the loop at
	   IMEM 0x190..0x1b4 ends in `jr t3`).  All-zero entries there dispatch
	   straight to pc 0, so print the head of it. */
	fprintf(r29f, "R29TBL 36e:");
	for (i = 0x36e / 2; i < (0x36e / 2) + 24; i++)
		fprintf(r29f, " %04x", (uint32_t)(dm[i] & 0xffffu));
	fprintf(r29f, " |370:");
	for (i = 0x370 / 4; i < (0x370 / 4) + 12; i++)
		fprintf(r29f, " %08x", dm[i]);
	fprintf(r29f, "\n");
	fprintf(r29f, "R29IM 000=%08x 004=%08x 080=%08x 160=%08x 170=%08x 178=%08x 1b0=%08x\n",
	        im[0x000 / 4], im[0x004 / 4], im[0x080 / 4], im[0x160 / 4],
	        im[0x170 / 4], im[0x178 / 4], im[0x1b0 / 4]);
	n = r29_seq < R29_RING ? r29_seq : R29_RING;
	fprintf(r29f, "R29TRACE:");
	for (i = 0; i < n; i++)
		fprintf(r29f, " %03x", r29_ring[(r29_seq - n + i) & (R29_RING - 1u)]);
	fprintf(r29f, "\n");
	fflush(r29f);
}

	EXPORT unsigned int CALL DoRspCycles(unsigned int cycles)
	{
		/* DD-gate: the core provides a RUNTIME IsDDPresent() query (task
		   time, after init_device set dd.idisk; plugin start runs before
		   init_device so a static wiring check would always fail).  All
		   ares-derived work below (budget, clean-yield protocol, DIAG
		   traces) is keyed off this so plain cart games keep the stock
		   parallel-RSP behavior exactly. */
		const int dd_mode = RSP::rsp.IsDDPresent && RSP::rsp.IsDDPresent();

	/* ROUND 27 (DD route only) -- SP_DMA_BUSY/SP_DMA_FULL ARE NOT THE BLOCKER:
	   MEASURED, HYPOTHESIS ELIMINATED.

	   The FIFO ucode's own DMA helper polls SP_DMA_BUSY:

	       IMEM 1FC8  mfc0 $11, SP_DMA_BUSY
	       IMEM 1FCC  bne  $11,$0,-1
	       IMEM 1FD0  mfc0 $11, SP_DMA_BUSY   (delay slot)

	   and 162-168 slices of every round-27 run END at exactly `pc=0fc8`, so
	   the obvious reading was "the flag is stuck and the ucode livelocks
	   there".  It is not: the RSP's cr[0x5]/cr[0x6] point at the core's
	   regs[SP_DMA_FULL_REG]/regs[SP_DMA_BUSY_REG] (the CPU-side FIFO engine,
	   set by fifo_push and cleared asynchronously by fifo_pop), so the plugin
	   was asked to make the RSP's view of them report idle -- which is the
	   truthful model here, because this plugin performs every RSP-initiated
	   transfer itself, synchronously, inside the mtc0 SP_RD_LEN/SP_WR_LEN
	   handler.

	   Round 27c redirected cr[0x5]/cr[0x6] to a plugin-local zero for the DD
	   route and ran it: `core_busy=0 core_full=0` -- the core's flags were
	   ALREADY zero -- and the run is unchanged (162 vs 168 slices at pc=0fc8;
	   `R20W wr=97257 outbuf=0 datalist=97257`, `R20P pub=53279 ring=0`,
	   `R26W wild=20479`, all byte-identical to the control arm).  The redirect
	   was therefore reverted; only the sampling below was kept, so every future
	   run states the flags outright.

	   Consequence for the diagnosis: PC 0x0FC8 is NOT a livelock.  It is the
	   shared DMA helper, which the ucode passes through on EVERY transfer, so a
	   slice boundary lands there often.  The real chain stays: the gfx FIFO's
	   write DMAs never target the output buffer and it never publishes. */
	r27_raw_busy = *RSP::rsp.SP_DMA_BUSY_REG;
	r27_raw_full = *RSP::rsp.SP_DMA_FULL_REG;


	/* ROUND 20: the FIFO-protocol counters (see cp0.cpp) rewritten every few
	   seconds so the state at the test's screenshot time is always on disk.
	   The core cannot reference these symbols (the plugin is dlopened), so the
	   summary is produced here. */
	if (dd_mode)
	{
		static long long r20_last_ms = 0;
		long long r20_now = wd_now_ms();
		if (r20_now - r20_last_ms >= 4000)
		{
			extern unsigned r20_dma_total(void), r20_dma_datalist(void), r20_dma_outbuf(void);
			extern unsigned r20_dma_low_mem(void), r20_save_count(void);
			extern unsigned r20_yield_req(void), r20_yield_timeout(void);
			extern unsigned r20_first_dma_n(void);
			extern const uint32_t* r20_first_dma(void);
			extern unsigned r20_wr_total(void), r20_wr_outbuf(void), r20_wr_datalist(void);
			extern unsigned r20_wr_latch_n(void);
			extern const uint32_t* r20_wr_latch(void);
			extern unsigned r20_pub_n(void), r20_pub_ring(void), r20_pub_stale(void);
			extern unsigned r20_pub_latch_n(void);
			extern const uint32_t* r20_pub_latch(void);
			extern uint32_t r20_saved_k0(void), r20_saved_ptr(void);
			extern unsigned r21_emu_save_n(void);
			extern uint32_t r21_emu_save_k0(void), r21_emu_save_f0(void);
			unsigned k, m;
			FILE* f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_r20.txt", "w");
			r20_last_ms = r20_now;
			if (f)
			{
				fprintf(f, "R20 ms=%lld pc=%04x dma=%u datalist=%u outbuf=%u low=%u save=%u yreq=%u ytimeout=%u saved_k0=%08x yptr=%08x core_busy=%u core_full=%u\n",
				        r20_now, *RSP::rsp.SP_PC_REG & 0xfff, r20_dma_total(), r20_dma_datalist(),
				        r20_dma_outbuf(), r20_dma_low_mem(), r20_save_count(),
				        r20_yield_req(), r20_yield_timeout(), r20_saved_k0(), r20_saved_ptr(),
				        r27_raw_busy, r27_raw_full);
				m = r20_first_dma_n();
				for (k = 0; k < m && k < 12; k++)
				{
					const uint32_t* e = r20_first_dma() + k * 6;
					fprintf(f, "R20F %u pc=%03x src=%06x len=%05x dst=%04x data_ptr=%06x bf8=%08x\n",
					        k, e[0], e[1], e[2], e[3], e[4], e[5]);
				}
				fprintf(f, "R21EMU n=%u k0=%08x f0=%08x\n",
				        r21_emu_save_n(), r21_emu_save_k0(), r21_emu_save_f0());
				{
					/* ROUND 28: reads of SP_READ_LENGTH/SP_WRITE_LENGTH and
					   how many of them came back NON-ZERO.  A ucode uses this
					   pair as the "did my DMA finish?" poll (ares:
					   n64/rsp/io.cpp answers it with dma.current.length, i.e.
					   0 between transfers); this integration echoed the last
					   written length instead.  MEASURED: the F-Zero X FIFO
					   ucode reads this pair ZERO times (`reads=0`) -- it polls
					   SP_DMA_FULL/SP_DMA_BUSY instead -- so R28_LEN_READBACK is
					   0 and this line is the record of the A/B.

					   R28D is the fix that IS under test: the gfx ucode's saved
					   display-list pointer (DMEM 0xBF8) was 0x152C03C0, which
					   is not a physical RDRAM address, so its RDL walk ran in
					   the all-zero region 0x2C03C0.. and it never published.
					   `n` counts the task starts at which it was replaced by
					   the header's data_ptr, `saved` the offending value and
					   `new` the replacement. */
					extern unsigned r28_len_reads(void), r28_len_reads_nz(void);
					extern uint32_t r28_len_read_last(void), r28_len_read_pc(void);
					extern unsigned r28_restart_count(void);
					extern uint32_t r28_restart_saved(void), r28_restart_new(void);
					fprintf(f, "R28L reads=%u nz=%u last=%08x pc=%03x\n",
					        r28_len_reads(), r28_len_reads_nz(),
					        r28_len_read_last(), r28_len_read_pc());
					fprintf(f, "R28D n=%u saved=%08x new=%08x\n",
					        r28_restart_count(), r28_restart_saved(), r28_restart_new());
				}
				fprintf(f, "R20W wr=%u outbuf=%u datalist=%u\n",
				        r20_wr_total(), r20_wr_outbuf(), r20_wr_datalist());
				fprintf(f, "R20P pub=%u ring=%u stale=%u\n",
				        r20_pub_n(), r20_pub_ring(), r20_pub_stale());
				{
					/* ROUND 26: out-of-RDRAM ("wild") RSP transfers seen this
					   run.  Rounds 18-25 REFUSED them (and livelocked: the
					   address registers never advanced, so the ucode re-issued
					   the same transfer forever); round 26 lets them through
					   and lets the per-word `& 0x7FFFFC` masking do the
					   hardware's own 24-bit wrap.  Read this together with
					   `R20W wr` and `R20P pub`: if wr/pub are still 0 while
					   this climbs, the run is still publishing nothing. */
					extern unsigned r14_wild_count(void);
					fprintf(f, "R26W wild=%u\n", r14_wild_count());
				}
				m = r20_pub_latch_n();
				for (k = 0; k < m && k < 4; k++)
				{
					const uint32_t* e = r20_pub_latch() + k * 6;
					fprintf(f, "R20P%u pc=%03x dst=%06x len=%05x ring0_f0=%06x ringend=%06x memsrc=%05x\n",
					        k, e[0], e[1], e[2], e[3], e[4], e[5]);
				}
				m = r20_wr_latch_n();
				for (k = 0; k < m && k < 8; k++)
				{
					const uint32_t* e = r20_wr_latch() + k * 4;
					fprintf(f, "R20W%u pc=%03x dst=%06x len=%05x bf8=%08x\n",
					        k, e[0], e[1], e[2], e[3]);
				}
				fclose(f);
			}
		}
	}

		/* DIAG: task entry/exit trace (file) — identifies the freezing task.
		   DD-only (plain games must have no tracing overhead). */
		if (dd_mode)
		{
			static FILE* rf = NULL;
			static unsigned task_seq = 0;
			if (!rf) rf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_rsp.txt", "a");
			unsigned ttype = ((uint32_t*)RSP::rsp.DMEM)[0xfc0 / 4];
			int expired = rsp_budget_expired_now();
			if (rf && wd_rsp_log_n < WD_RSP_LOG_MAX)
			{
				long long now_ms = wd_now_ms();
				wd_rsp_log_n++;
				/* ROUND-14 DIAG: how much of RSP memory is live right now
				   (nonzero words in IMEM and DMEM).  The frozen machine's
				   terminal state is ALL ZERO -- both banks, 2048 words --
				   with the RSP executing NOPs forever and never breaking, so
				   the exact entry where the counts collapse names the step
				   that destroys the task.  2048 word compares per call is
				   noise next to the fprintf it accompanies. */
				unsigned r14_nzi = 0, r14_nzd = 0, r14_i;
				for (r14_i = 0; r14_i < 0x1000 / 4; r14_i++)
				{
					if (((const uint32_t*)RSP::rsp.IMEM)[r14_i]) r14_nzi++;
					if (((const uint32_t*)RSP::rsp.DMEM)[r14_i]) r14_nzd++;
				}
				fprintf(rf, "RSPTASK ms=%lld seq=%u ENTER pc=%04x status=%08x ttype=%u exp=%d imem0=%08x %08x nzi=%u nzd=%u pimem=%p pdmem=%p pram=%p cimem=%p\n",
					now_ms, task_seq, *RSP::rsp.SP_PC_REG & 0xfff, *RSP::rsp.SP_STATUS_REG,
					ttype, expired,
					((uint32_t*)RSP::rsp.IMEM)[0], ((uint32_t*)RSP::rsp.IMEM)[1],
					r14_nzi, r14_nzd,
					(void*)RSP::rsp.IMEM, (void*)RSP::rsp.DMEM, (void*)RSP::rsp.RDRAM,
					(void*)RSP::cpu.get_state().imem);
				/* ROUND-15 DIAG: the F3DEX2/F3DLX2 entry code keys its
				   fresh/warm/resume decision on DMEM[0x0F0] (the RDP end
				   pointer it stores on a cold start) and takes k0 from
				   DMEM[0xFF0] (the header's data_ptr) -- or from DMEM[0xBF8]
				   (the pointer saved by the yield path) on a resume.  Log
				   those words plus the flags the ucode clears itself on every
				   gfx task entry, before the walk starts rewriting them. */
				if (ttype == 1 || ttype == 0)
				{
					static int r15_hdr_first = 1;
					FILE* hf15 = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_hdr15.txt",
					                   r15_hdr_first ? "w" : "a");
					r15_hdr_first = 0;
					if (hf15)
					{
						uint32_t* h15 = (uint32_t*)RSP::rsp.DMEM;
						fprintf(hf15, "H15 ms=%lld seq=%u pc=%04x type=%08x flags=%08x f0=%08x ff0=%08x ff4=%08x bf8=%08x bf4=%08x ucode=%08x yield=%08x ysz=%08x st=%08x\n",
						        now_ms, task_seq, *RSP::rsp.SP_PC_REG & 0xfff,
						        h15[0xfc0 / 4], h15[0xfc4 / 4], h15[0x0f0 / 4],
						        h15[0xff0 / 4], h15[0xff4 / 4], h15[0xbf8 / 4], h15[0xbf4 / 4],
						        h15[0xfd0 / 4], h15[0xff8 / 4], h15[0xffc / 4],
						        *RSP::rsp.SP_STATUS_REG);
						fclose(hf15);
					}
				}
				/* DIAG (round 6): log the DMEM task header the guest submitted.
				   A zeroed header means the guest re-started the RSP without an
				   __osSpTaskLoad (or with an uninitialised OSTask), which makes
				   the ucode DMA from a null pointer and leaves the RSP executing
				   zeroed IMEM until the host budget expires. */
				if (ttype != 1 && ttype != 2 && wd_rsp_log_n < WD_RSP_LOG_MAX)
				{
					uint32_t* h = (uint32_t*)RSP::rsp.DMEM + (0xfc0 / 4);
					wd_rsp_log_n++;
					fprintf(rf, "RSPHDR ms=%lld seq=%u type=%u flags=%u boot=%08x bootsz=%08x ucode=%08x ucosz=%08x udata=%08x udsz=%08x stack=%08x stksz=%08x obuf=%08x obsz=%08x\n",
						now_ms, task_seq, h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11]);
				}
				/* dump: any entry whose task header looks wrong (invalid type) or
				   matches the classic pc 0x18C stall — live IMEM+DMEM so the stuck
				   ucode can be disassembled offline. Up to 3 captures per run. */
				static unsigned n_dump = 0;
				int pc_lo = *RSP::rsp.SP_PC_REG & 0xfff;
				/* DIAG: capture the first tasks unconditionally (early audio/gfx
				   entries), plus any stale-header entry — live IMEM+DMEM. */
				if (n_dump < 3)
				{
					char uf[128];
					snprintf(uf, sizeof(uf), "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_ucode%u.bin", n_dump + 1);
					FILE* df = fopen(uf, "wb");
					if (df)
				{
					n_dump++;
						uint32_t hdr[8] = {0x57444344 /*WDCD*/, task_seq, (uint32_t)pc_lo,
						                   *RSP::rsp.SP_STATUS_REG, ttype, (uint32_t)expired,
						                   RSP::cpu.get_state().pc & 0xfff, 0};
						fwrite(hdr, 4, 8, df);
						fwrite(RSP::rsp.IMEM, 1, 0x1000, df);
						fwrite(RSP::rsp.DMEM, 1, 0x1000, df);
						fclose(df);
					}
				}
				/* DIAG: capture non-audio tasks (gfx + invalid) deduped by pc — grabs the
				   resume gfx ucode and transition points for disassembly. */
				static unsigned n_inv = 0;
				static int seen_pc[16]; static unsigned seen_n = 0;
				if (n_inv < 10 && ttype != 2)
				{
					int dup = 0;
					for (unsigned s=0;s<seen_n;s++) if (seen_pc[s]==pc_lo){dup=1;break;}
					if (!dup) {
						char uf[128];
						snprintf(uf, sizeof(uf), "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_ucode_inv%u.bin", n_inv + 1);
						FILE* df = fopen(uf, "wb");
						if (df) {
							uint32_t hdr[8] = {0x5744494E /*WDIN*/, task_seq, (uint32_t)pc_lo,
							                   *RSP::rsp.SP_STATUS_REG, ttype, (uint32_t)expired,
							                   RSP::cpu.get_state().pc & 0xfff, 0};
							fwrite(hdr, 4, 8, df);
							fwrite(RSP::rsp.IMEM, 1, 0x1000, df);
							fwrite(RSP::rsp.DMEM, 1, 0x1000, df);
							fclose(df);
							seen_pc[seen_n++] = pc_lo;
							n_inv++;
						}
					}
				}
				/* DIAG: one-shot capture of the STUCK task (pc=0xa0, imem0=4b7d5ef2)
				   — live IMEM+DMEM so the spinning ucode can be disassembled offline. */
				static int stuck_captured = 0;
				if (!stuck_captured && pc_lo == 0xa0)
				{
					uint32_t im0 = ((uint32_t*)RSP::rsp.IMEM)[0];
					uint32_t im1 = ((uint32_t*)RSP::rsp.IMEM)[1];
					if (im0 == 0x4b7d5ef2u && im1 == 0x4b7c5eb1u)
					{
						stuck_captured = 1;
						char uf[128];
						snprintf(uf, sizeof(uf), "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_ucode_stuck.bin");
						FILE* df = fopen(uf, "wb");
						if (df) {
							uint32_t hdr[8] = {0x57445354 /*WDSK*/, task_seq, (uint32_t)pc_lo,
							                   *RSP::rsp.SP_STATUS_REG, ttype, (uint32_t)expired,
							                   RSP::cpu.get_state().pc & 0xfff, 0};
							fwrite(hdr, 4, 8, df);
							fwrite(RSP::rsp.IMEM, 1, 0x1000, df);
							fwrite(RSP::rsp.DMEM, 1, 0x1000, df);
							fclose(df);
						}
					}
				}
				/* remember the seq for the exit log */
				RSP::cpu.get_state().sr[31] = task_seq;
				task_seq++;
				/* batch the RSP-trace flush (per-event fflush to flash was a
				   big chunk of the EK-load slowdown) */
				{
					static unsigned rsp_flush_n = 0;
					if ((++rsp_flush_n & 0xff) == 0)
						fflush(rf);
				}
			}
		}
		if (*RSP::rsp.SP_STATUS_REG & (SP_STATUS_HALT | SP_STATUS_BROKE))
			return 0;

		/* ROUND 16, 64DD ROUTE ONLY (g_dev.dd.idisk != NULL, i.e. dd_mode):
		   HONOUR LIBULTRA'S YIELD-RESUME CONTRACT.

		   osSpTaskYielded() (decomp src/libultra/io/sptaskyielded.c) does
		   `tp->t.flags |= OS_TASK_YIELDED; tp->t.flags &= ~OS_TASK_DP_WAIT;`
		   the moment SIG1 (YIELDED) is set while SIG0 (YIELD) is still set,
		   and osSpTaskLoad() then maps `ucode_data = yield_data_ptr` for that
		   task -- i.e. the RSP is meant to be restarted from the DMEM image
		   the ucode saved in its yield buffer.

		   The game's boot ucode (this ROM, RDRAM 0x7504F0; disassembled):
		     1064 lw  v0,4(at)      # header flags
		     1068 andi v0,v0,2      # OS_TASK_DP_WAIT only
		     106c beq v0,r0,0x108c  # not DP_WAIT -> SKIP the DATA DMA
		     ... 108c..10ac: DMA ucode_data -> DMEM 0 ...
		   ROUND 17 CORRECTION (the DMA trace settles it): the ucode_data ->
		   DMEM 0 transfer is NOT gated on the DP_WAIT bit -- IMEM 106C's
		   `beq v0,r0,0x108C` jumps *to* the DMA when DP_WAIT is clear, so a
		   resumed task does get its saved image back (measured: wd_dmatr
		   D7982/D8944 are `RD dst=0000 src=32dcd0 len=0c00`, i.e. the boot
		   ucode loading the whole 0xC00 yield buffer over DMEM 0 immediately
		   after a yielded osSpTaskLoad).  This copy is therefore the same
		   transfer the guest performs, done one slice earlier; what it buys is
		   the word-level fix-up below, which the guest's DMA cannot express.
		   Gates (all required, so nothing else in the tree can match):
		     * DD route only (Runtime IsDDPresent()); plain carts unchanged.
		     * SP_PC == 0x1000 & 0xfff == 0: a FRESH task start (osSpTaskSetPc
		       set 0x1000); slice re-entries carry the running ucode's pc, so a
		       2ms budget slice can never re-run this and clobber live DMEM.
		     * header type <= 2 (the poisoned 0x00010001 headers fail this).
		     * flags & OS_TASK_YIELDED, !(flags & OS_TASK_DP_WAIT)  -- the exact
		       combination libultra produces for a yielded task.
		     * yield ptr/size sane; size is 0xC00 (whole DMEM 0..0xBFF) and the
		       OSTask header itself (0xFC0..0xFFF) is deliberately NOT touched. */
		if (dd_mode && (*RSP::rsp.SP_PC_REG & 0xfff) == 0)
		{
			uint32_t* hdr = (uint32_t*)RSP::rsp.DMEM;
			uint32_t r16_type  = hdr[0xfc0 / 4];
			uint32_t r16_flags = hdr[0xfc4 / 4];
			uint32_t r16_yptr  = hdr[0xff8 / 4];
			uint32_t r16_ysz   = hdr[0xffc / 4];
			/* Note: the buffer copy needs a sane descriptor; the k0 fix-up
			   below does not (it only reads the header), so the two halves are
			   gated separately. */
			if (r16_type <= 2u && (r16_flags & 0x1u) && !(r16_flags & 0x2u))
			{
				uint32_t r16_n = 0, r16_i;
				uint32_t r16_before_f0  = hdr[0x0f0 / 4];
				uint32_t r16_before_bf8 = hdr[0xbf8 / 4];
				uint32_t r16_before_ff0 = hdr[0xff0 / 4];
				if (r16_ysz >= 0x40u && r16_ysz <= 0x1000u &&
				    r16_yptr >= 0x100u && (r16_yptr & 0x3u) == 0 &&
				    r16_yptr + r16_ysz <= 0x800000u)
				{
					r16_n = (r16_ysz + 3u) & ~3u;
					/* NOTE: RSP::rsp.RDRAM is an `unsigned char *` (RSP_INFO is
					   byte-typed); the word view is state.rdram (uint32_t*). */
					for (r16_i = 0; r16_i < (r16_n >> 2); r16_i++)
						hdr[r16_i] = RSP::cpu.get_state().rdram[((r16_yptr & 0x7ffffcu) >> 2) + r16_i];
				}

				/* ROUND 17: GIVE THE RESUMED UCODE A VALID DISPLAY-LIST POINTER.

				   Disassembly of the ucode this ROM actually submits
				   (GFXMODE_F3DFLX -> gspF3DFLX2_Rej_fifo, text RDRAM 0x752AE0
				   -> IMEM 0x080; linker_scripts/jp/ek/symbol_addrs_nlib_vars.txt
				   confirms both, and the live IMEM matches the text byte for
				   byte) shows that the yielded resume is the ONE entry path that
				   does not take its display list from the task header:

				     IMEM 0098  lw  t3,0xF0(r0)     # DMEM[0xF0] = FIFO end ptr
				     IMEM 009C  lw  t4,0xFC4(r0)    # DMEM[0xFC4] = task flags
				     IMEM 00A4  beq t3,r0,0x00C0    # 0 -> COLD init path
				     IMEM 00AC  andi t4,t4,0x1      # OS_TASK_YIELDED
				     IMEM 00B0  beq t4,r0,0x0144    # not yielded -> warm path
				     IMEM 00B4  sw  r0,0xFC4(r0)    # (consume the flag)
				     IMEM 00B8  j   0x0164          # <- YIELDED RESUME
				     IMEM 00BC  lw  k0,0xBF8(r0)    #    k0 = DMEM[0xBF8]  (saved)
				     IMEM 0144  (adds the ucode base to the DMEM 0x280/0x288
				                 overlay descriptors -- SKIPPED on the resume,
				                 which is why those words must stay absolute)
				     IMEM 0160  lw  k0,0xFF0(r0)    # k0 = header data_ptr
				     IMEM 0164  (main display-list loop; the loop polls
				                 SP_STATUS & 0x80 per command and jumps to
				                 IMEM 0FAC, the DMA+overlay loader)

				   So every entry except the yielded resume walks from
				   data_ptr.  Measured on the RP6: the healthy trip reads
				   `RD dst=09b0 src=24e260` (== data_ptr), while the yielded
				   resume whose saved DMEM had [0xBF8]=0x1208 walked RDRAM
				   0x1208 -- framebuffer memory filled with 0x00010001, which
				   the ucode reads as G_NOOP (opcode 0x00) straight into
				   gTaskOutputBuffer, kicking the RDP on an empty list; the
				   game's DP event then never fires and the loader stalls.

				   DMEM[0xBF8] is scratch the ucode itself never stores k0 to
				   (verified: no store to 0xBF8 anywhere in the 0xF80-byte text
				   or the 0x170-byte overlay at RDRAM 0x753AF8), so whatever the
				   save captured there is a stale command word -- 0xE6000000,
				   0x10000003, 0x1208 in the live traces.  Re-pointing it at the
				   header's data_ptr makes the resume walk the real, current
				   display list exactly like a fresh task, without disturbing
				   anything else the saved image restored (the FIFO/RDP state at
				   0xF0/0xF4 and the absolute overlay descriptors at 0x280/0x288
				   that the 0x144 path must not re-base). */
				{
				uint32_t r16_saved_k0 = hdr[0xbf8 / 4];
				uint32_t r16_fixed_bf8 = hdr[0xff0 / 4];
				/* ROUND 20: DO NOT CLOBBER DMEM 0xBF8.  Round 17 set this
				   word to data_ptr because the value restored from the
				   yield buffer looked like a stale command word.  The
				   ucode's own yield handler (overlay A, ucode+0xF80, loaded
				   over IMEM 0x000 by the 0xFAC path and run at PC 0x000)
				   writes it at PC 0x0064:

				       lw  t3,0xFD0(r0)   # ucode base
				       sw  k0,0xBF8(r0)   # the display-list pointer
				       sw  t3,0xBFC(r0)
				       ... DMA write DMEM[0..0xBFF] -> yield_data_ptr
				       mtc0 SIG1|SIG2 ; break

				   and the YIELDED resume entry takes k0 from exactly this
				   word (IMEM 0x00B8 `j 0x0164` with IMEM 0x00BC
				   `lw k0,0xBF8(r0)`).  Overwriting it restarts the walk at
				   the top of the display list on every resume instead of
				   continuing it -- 2538 RDP kicks with a command stream of
				   all-zero words (the output buffer never once written) and
				   MI_INTR_DP never raised is what that looks like from the
				   guest's side.  The restore of the yield buffer above is
				   the contract; leave the saved state alone.  Logged below
				   so the value can be re-checked against RDRAM. */
				(void)r16_fixed_bf8;

				/* DIAG (DD-gated, a handful of events per run): the stale DMEM
				   word, the saved k0 the ucode would have taken, the data_ptr it
				   takes instead, and how much of the image was restored. */
				{
					static FILE* yf = NULL;
					if (!yf) yf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_yld.txt", "a");
					if (yf)
					{
						fprintf(yf, "YLD ms=%lld type=%u flags=%08x yptr=%06x ysz=%04x n=%04x stale_f0=%08x stale_bf8=%08x rest_f0=%08x saved_k0=%08x data_ptr=%08x rest_bfc=%08x ff0=%08x stale_ff0=%08x st=%08x\n",
						        wd_now_ms(), r16_type, r16_flags, r16_yptr, r16_ysz, r16_n,
						        r16_before_f0, r16_before_bf8,
						        hdr[0x0f0 / 4], r16_saved_k0, r16_fixed_bf8,
						        hdr[0xbfc / 4], hdr[0xff0 / 4], r16_before_ff0,
						        *RSP::rsp.SP_STATUS_REG);
						fflush(yf);
					}
				}
				}
			}
		}

		/* ROUND-19: WHERE IS THE SP MEMORY KILLED?
		   Every instrumented writer came back clean (no CPU SP DMA, no CPU
		   direct SP-memory write in the low IMEM page, and only two legitimate
		   RSP-side IMEM loads in the whole run), yet the watchdog keeps finding
		   IMEM[0..3] and DMEM[0xFC0..] full of the game's 0x00010001 fill.
		   Checking IMEM[0..1] at EVERY slice entry bounds the corruption to a
		   single slice, and dumping the whole 8 KiB of SP memory plus this
		   slice's entry state says which side (RSP instruction stream or CPU
		   between slices) did it.  DD-gated, once per run. */
		if (dd_mode)
		{
			static int wd_spkill_n = 0;
			if (wd_spkill_n < 2 &&
			    RSP::cpu.get_state().imem[0] == 0x00010001u &&
			    RSP::cpu.get_state().imem[1] == 0x00010001u)
			{
				FILE* f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_spkill.txt",
				                wd_spkill_n ? "a" : "w");
				wd_spkill_n++;
				if (f)
				{
					unsigned k;
					fprintf(f, "SPKILL n=%d ms=%lld pc=%04x status=%08x sp_pc=%08x fc0=%08x f0=%08x ff0=%08x bf8=%08x\n",
					        wd_spkill_n, wd_now_ms(), RSP::cpu.get_state().pc & 0xfff,
					        *RSP::rsp.SP_STATUS_REG, *RSP::rsp.SP_PC_REG,
					        RSP::cpu.get_state().dmem[0xfc0 / 4], RSP::cpu.get_state().dmem[0x0f0 / 4],
					        RSP::cpu.get_state().dmem[0xff0 / 4], RSP::cpu.get_state().dmem[0xbf8 / 4]);
					fprintf(f, "  dmem ");
					for (k = 0; k < 0x1000 / 4; k++)
					{
						if ((k & 15) == 0) fprintf(f, "\n   %03x:", k * 4);
						fprintf(f, " %08x", RSP::cpu.get_state().dmem[k]);
					}
					fprintf(f, "\n  imem ");
					for (k = 0; k < 0x1000 / 4; k++)
					{
						if ((k & 15) == 0) fprintf(f, "\n   %03x:", 0x1000 + k * 4);
						fprintf(f, " %08x", RSP::cpu.get_state().imem[k]);
					}
					fprintf(f, "\n");
					fclose(f);
				}
			}
		}

		/* DIAG: arm freeze heartbeat (DD-only) */
		if (dd_mode)
		{
			wd_task_start_ms = wd_now_ms();
			wd_last_hb_ms = 0;
		}

		// We don't know if Mupen from the outside invalidated our IMEM.
		RSP::cpu.invalidate_imem();

		// Run CPU until we either break or we need to fire an IRQ.
		RSP::cpu.get_state().pc = *RSP::rsp.SP_PC_REG & 0xfff;

#ifdef INTENSE_DEBUG
		fprintf(stderr, "RUN TASK: %u\n", RSP::cpu.get_state().pc);
		log_rsp_mem_parallel();
#endif

		for (auto &count : RSP::MFC0_count)
			count = 0;

		/* ROUND 20 (DD route only): reset the clean-yield state machine for
		   this slice (see cp0.cpp: the FIFO ucode is preempted by asking for
		   SIG0 and letting it run into its own yield handler). */
		{
			extern void r20_task_begin(void);
			if (dd_mode)
				r20_task_begin();
		}

		/* Host-run budget: yield the emulation thread if a single ucode run
		   exceeds 50ms (libultra infinite-wait ucode).  Clean audio tasks
		   (type 2) do legit DSP vector work in long blocks and preempting
		   them mid-frame garbles the audio output, so they run unbounded.
		   EVERYTHING else (type 1 gfx AND clobbered F3DEX redraw/resume
		   tasks whose DMEM 0xFC0 header the ucode overwrites with
		   display-list data, reading as type 0/garbage/0x10001) gets the
		   50ms deadline — a clobbered resume otherwise runs unbounded and
		   hard-freezes the emulation thread inside the RSP JIT (the DD
		   loader stall).  The intra-loop budget check in rsp_jit.cpp
		   preempts tight ucode loops on this deadline, and the MFC0
		   SP_STATUS poll timeout covers the poll-based waits. */
		unsigned dsp_task_type = ((uint32_t*)RSP::rsp.DMEM)[0xfc0 / 4];
		/* Still read for the DD task-type trace below; the deadline no longer
		   depends on it (see the round-8 note). */
		(void)dsp_task_type;
		/* Round 8: the host-run budget was 100ms (audio) / 50ms (everything
		   else), which was measured to be the machine's real bottleneck.
		   The F-Zero X EK audio ucode parks in a `beq z,z` wait-loop and
		   only leaves it when the CPU feeds it more data -- but for as long
		   as the budget lasts, the emulation thread never returns from
		   DoRspCycles, so the CPU cannot run.  Measured on the RP6 with
		   `wd_freeze.txt`: every DoRspCycles call took a full 50-100ms of
		   WALL time and they ran strictly back-to-back (20/s), i.e. 100% of
		   the emulation thread inside the RSP, with the dynarec sample hook
		   (wd_c_sample) frozen because the CPU never got a slice.  That is
		   what starves the guest: its VI manager thread cannot run, so
		   `while (osViGetCurrentFramebuffer() != gFrameBuffers[i]) {}` at
		   sys_gfx.c:198 never completes and the boot parks on the 64DD logo.
		   On real hardware a spinning RSP does NOT block the CPU, so the
		   budget must be small enough to interleave: 2ms of host time is
		   still ~millions of RSP cycles (far more than any real task slice)
		   while guaranteeing the CPU a turn.  The budget-yield path below
		   still emits the clean HALT+INTR_BREAK+irq task boundary, so the
		   guest acks and re-dispatches exactly as with the long deadline.

		   ROUND 17 (DD route only): relaxing the budget for gfx tasks (so the
		   ucode could finish its display list unprompted) was TRIED AND
		   REVERTED.  That build (r17d) reads `RDPKICK n=0`,
		   `FRAME loads t1gfx=1`, `raise_bits VI=271/AI=192` against
		   6087/6005 for this build: the gfx task never reached DPC_END, so
		   the RDP was never kicked and the frame protocol stopped dead.  The
		   2 ms slice is what makes the DD path progress at all, so it stays
		   in force for every task type. */
		rsp_set_budget_deadline_us(dd_mode ? 2000 : 0);
		/* ROUND-18 DIAG: wall time this slice costs, paired in the exit log with
		   the budget checks it consumed (see rsp_slice_units_now).  This is the
		   only clock read on this path -- one per DoRspCycles, never per
		   instruction. */
		const auto wd_slice_t0 = std::chrono::steady_clock::now();

		int wd_budget_yield = 0;
		{
			while (!(*RSP::rsp.SP_STATUS_REG & SP_STATUS_HALT))
			{
				auto mode = RSP::cpu.run();
				/* Any flags-check / host-wait request returns to the core (CXD4
				   model); the core reschedules and re-enters DoRspCycles.  The
				   ucode's SP_STATUS wait keeps INTR_BREAK (0x40) set the whole
				   time, so the only meaningful "still running" state is HALT:
				   if the ucode asked for a host check while NOT halted, return
				   and let the core decide (rsp_task_locked / SP interrupt).
				   CPU::run() itself enforces the host-run budget and returns
				   MODE_CHECK_FLAGS on expiry, so this branch always yields. */
				if (dd_mode)
				{
				if (mode == RSP::MODE_CHECK_FLAGS && !(*RSP::rsp.SP_STATUS_REG & SP_STATUS_HALT))
				{
					/* Budget/hard-wait expiry on a non-audio task: emit the SAME clean
					   yield state as the cp0.cpp MFC0-threshold path (HALT + INTR_BREAK +
					   irq) so do_SP_Task treats this as a proper task boundary instead of
					   signaling done on an unfinished ucode.  A raw break without HALT/irq
					   desyncs CPU<->RSP: do_SP_Task runs the gfx completion path and the
					   next osTaskStart reads a stale/clobbered [DMEM 0xFC0] header (observed
					   as type=garbage, ucode in PI space) -> cascading corrupt PC.

					   ROUND 9 -- two corrections, both required to stop an instantaneous
					   fake-completion livelock:

					   (a) MODE_CHECK_FLAGS is NOT synonymous with "budget expired".  The
					       ucode itself produces it on every `mtc0 SP_STATUS` while the
					       RSP-side irq flag is set (rsp/cp0.cpp:171 returns CHECK_FLAGS when
					       `(cp0.irq & 1) || (status & HALT)`).  Treating that as a yield
					       fabricates a task completion for a task that never ran.  Only a
					       real host-budget expiry is a yield; otherwise drop the irq flag and
					       let the ucode keep executing (the JIT budget check bounds this).

					   (b) `cp0.irq |= 1` was STICKY.  Nothing in the core clears the
					       plugin's irq flag -- the guest's SP_CLR_INTR write only clears
					       MI_INTR_SP (rsp_core.c update_sp_status) -- and this `break` skips
					       the CheckInterrupts() call that would consume it.  So from the
					       first yield onwards every cpu.run() returned MODE_CHECK_FLAGS
					       immediately, i.e. the RSP never executed another instruction.
					       Measured live (wd_rsp.txt): 13406 "tasks" inside ONE millisecond,
					       every one with ttype=1048576 / a garbage OSTask, and an SP
					       interrupt storm of 524k/s (wd_stall.txt DELTA2 c_exc_int).  The
					       core raises MI_INTR_SP for this yield itself (rsp_core.c
					       DD-gated HALT+INTR_BREAK completion block), so the plugin's irq
					       flag is not needed here and must not survive the yield. */
					if (!rsp_budget_expired_now())
					{
						*RSP::cpu.get_state().cp0.irq = 0;
						continue;
					}
					/* ROUND 9 (c): a budget yield must NOT fabricate a task
					   completion.  Setting HALT|INTR_BREAK made do_SP_Task report
					   the task as finished, and the guest's libultra SP handler
					   then re-entered do_SP_Task per ack -- so the guest's CPU time
					   was 100% SP-handler and the RSP still owned the thread
					   (measured: c_task 500/s, guest idle thread `b .`, CP0 COUNT
					   advancing 134k/s = 0.3%).  Leave SP_STATUS exactly as the
					   ucode left it: the task is simply unfinished and resumable.
					   The core then marks it rsp_task_locked and rsp_dd_background_pump()
					   (rsp_core.c, called from dynarec_gen_interrupt) keeps feeding
					   the RSP in slices while the CPU runs -- which is the
					   concurrency real hardware has and this synchronous plugin
					   model otherwise cannot express. */
					*RSP::cpu.get_state().cp0.irq = 0;
					wd_budget_yield = 1;
					break;
				}
				}
				else if (mode == RSP::MODE_CHECK_FLAGS && (*RSP::cpu.get_state().cp0.irq & 1))
					break;
			}
		}
	/* DIAG: disarm freeze heartbeat; note slow-but-recovered tasks (DD-only) */
	if (dd_mode && wd_task_start_ms)
	{
		long long dur = wd_now_ms() - wd_task_start_ms;
		wd_task_start_ms = 0;
		if (dur > 30 && whf) { fprintf(whf, "ms=%lld EXIT ok pc=%04x dur=%lld\n", wd_now_ms(), RSP::cpu.get_state().pc & 0xfff, dur); fflush(whf); }
	}


		*RSP::rsp.SP_PC_REG = 0x04001000 | (RSP::cpu.get_state().pc & 0xffc);

		/* DIAG: exit log (DD-only) */
		if (dd_mode)
		{
			static FILE* rf = NULL;
			if (!rf) rf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_rsp.txt", "a");
			if (rf && wd_rsp_log_n < WD_RSP_LOG_MAX)
			{
				const long long wd_slice_us = std::chrono::duration_cast<std::chrono::microseconds>(
					std::chrono::steady_clock::now() - wd_slice_t0).count();
				wd_rsp_log_n++;
				fprintf(rf, "RSPTASK ms=%lld seq=%u EXIT pc=%04x status=%08x irq=%u sem=%08x timed=%d units=%llu us=%lld wall=%d\n",
					wd_now_ms(),
					RSP::cpu.get_state().sr[31], RSP::cpu.get_state().pc & 0xfff,
					*RSP::rsp.SP_STATUS_REG, *RSP::cpu.get_state().cp0.irq & 1,
					*RSP::rsp.SP_SEMAPHORE_REG, RSP::SP_STATUS_TIMEOUT,
					rsp_slice_units_now(), wd_slice_us, rsp_budget_wall_hit_now());
				fflush(rf);
			}
		}

		// From CXD4.
		if (*RSP::rsp.SP_STATUS_REG & SP_STATUS_BROKE)
			return cycles;
		else if (*RSP::cpu.get_state().cp0.irq & 1)
			RSP::rsp.CheckInterrupts();
		else if (*RSP::rsp.SP_SEMAPHORE_REG != 0) // Semaphore lock fixes.
		{
		}
		else
			RSP::SP_STATUS_TIMEOUT = dd_mode ? 0x7fff : 16; // DD keeps the long wait (JIT budget handles preemption); plain games keep the stock 16-turn wait

		// CPU restarts with the correct SIGs.
		/* 64DD ROUTE ONLY: do NOT un-halt after a HOST-BUDGET yield.  The
		   stock clear below is CXD4's "task finished, RSP idle" handshake and
		   is only safe when the ucode actually finished (BROKE) or halted
		   itself.  After a budget yield the task is still unfinished, so
		   leaving HALT cleared makes the core read "RSP still running"
		   (do_SP_Task sets rsp_task_locked + raises MI_INTR_SP); the guest's
		   __osRspHandler then acknowledges with SP_STATUS=0x8008
		   (SP_CLR_SIG3|SP_CLR_INTR), which passes update_sp_status' gate
		   because rsp_task_locked is set and HALT is clear -> do_SP_Task ->
		   another full 50ms RSP run.  Measured live before this fix: 1362 RSP
		   runs in 68s, every one 50ms, i.e. 100% of the emulation thread, with
		   only 2 SP DMAs in the whole run -- a livelock that starves the guest
		   CPU so the audio task it is waiting on can never complete.  Keeping
		   HALT set makes the acknowledge write a no-op and leaves the RSP
		   halted until the guest starts a new task, which is the real-hardware
		   behaviour. */
		if (!(dd_mode && wd_budget_yield))
			*RSP::rsp.SP_STATUS_REG &= ~SP_STATUS_HALT;

		return cycles;
	}

	EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion,
	                                                   int *APIVersion, const char **PluginNamePtr, int *Capabilities)
	{
		/* set version info */
		if (PluginType != NULL)
			*PluginType = M64PLUGIN_RSP;

		if (PluginVersion != NULL)
			*PluginVersion = RSP_PARALLEL_VERSION;

		if (APIVersion != NULL)
			*APIVersion = RSP_PLUGIN_API_VERSION;

		if (Capabilities != NULL)
			*Capabilities = 0;

		return M64ERR_SUCCESS;
	}

	EXPORT void CALL RomClosed(void)
	{
		*RSP::rsp.SP_PC_REG = 0x00000000;
	}

	EXPORT void CALL InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
	{
        DebugMessage(M64MSG_ERROR, "InitiateRSP IsDDPresent=%p ForceSynchronize=%p DMEM=%p IMEM=%p",
			Rsp_Info.IsDDPresent, Rsp_Info.ForceSynchronize, Rsp_Info.DMEM, Rsp_Info.IMEM);

		if (CycleCount)
			*CycleCount = 0;

		if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
			return; /* DMA is not executed just because plugin initiates. */

		RSP::rsp = Rsp_Info;
		*RSP::rsp.SP_PC_REG = 0x04001000 & 0x00000FFF; /* task init bug on Mupen64 */

		auto **cr = RSP::cpu.get_state().cp0.cr;
		cr[0x0] = RSP::rsp.SP_MEM_ADDR_REG;
		cr[0x1] = RSP::rsp.SP_DRAM_ADDR_REG;
		cr[0x2] = RSP::rsp.SP_RD_LEN_REG;
		cr[0x3] = RSP::rsp.SP_WR_LEN_REG;
		cr[0x4] = RSP::rsp.SP_STATUS_REG;
		cr[0x5] = RSP::rsp.SP_DMA_FULL_REG;
		cr[0x6] = RSP::rsp.SP_DMA_BUSY_REG;
		cr[0x7] = RSP::rsp.SP_SEMAPHORE_REG;
		cr[0x8] = RSP::rsp.DPC_START_REG;
		cr[0x9] = RSP::rsp.DPC_END_REG;
		cr[0xA] = RSP::rsp.DPC_CURRENT_REG;
		cr[0xB] = RSP::rsp.DPC_STATUS_REG;
		cr[0xC] = RSP::rsp.DPC_CLOCK_REG;
		cr[0xD] = RSP::rsp.DPC_BUFBUSY_REG;
		cr[0xE] = RSP::rsp.DPC_PIPEBUSY_REG;
		cr[0xF] = RSP::rsp.DPC_TMEM_REG;

		*cr[RSP::CP0_REGISTER_SP_STATUS] = SP_STATUS_HALT;
		RSP::cpu.get_state().cp0.irq = RSP::rsp.MI_INTR_REG;

		// From CXD4.
		RSP::SP_STATUS_TIMEOUT = 0x7fff;

		RSP::cpu.set_dmem(reinterpret_cast<uint32_t *>(Rsp_Info.DMEM));
		RSP::cpu.set_imem(reinterpret_cast<uint32_t *>(Rsp_Info.IMEM));
		RSP::cpu.set_rdram(reinterpret_cast<uint32_t *>(Rsp_Info.RDRAM));
	}

	EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
									 void (*DebugCallback)(void *, int, const char *))
	{
        /* first thing is to set the callback function for debug info */
        l_DebugCallback = DebugCallback;
        l_DebugCallContext = Context;


        DebugMessage(M64MSG_ERROR, "PluginStartup");

		return M64ERR_SUCCESS;
	}

	EXPORT m64p_error CALL PluginShutdown(void)
	{
		return M64ERR_SUCCESS;
	}

	EXPORT int CALL RomOpen(void)
	{
        DebugMessage(M64MSG_ERROR, "RomOpen");
		return 1;
	}
}
