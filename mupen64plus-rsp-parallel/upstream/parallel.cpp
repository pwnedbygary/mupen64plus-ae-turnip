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
static long long wd_task_start_ms = 0;
static long long wd_last_hb_ms = 0;
static FILE* whf = NULL;

extern "C" void rsp_watchdog_tick(unsigned pc_lo)
{
	if (wd_task_start_ms == 0) return;
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

	EXPORT unsigned int CALL DoRspCycles(unsigned int cycles)
	{
		/* DD-gate: the core provides a RUNTIME IsDDPresent() query (task
		   time, after init_device set dd.idisk; plugin start runs before
		   init_device so a static wiring check would always fail).  All
		   ares-derived work below (budget, clean-yield protocol, DIAG
		   traces) is keyed off this so plain cart games keep the stock
		   parallel-RSP behavior exactly. */
		const int dd_mode = RSP::rsp.IsDDPresent && RSP::rsp.IsDDPresent();

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
		   guest acks and re-dispatches exactly as with the long deadline. */
		rsp_set_budget_deadline_us(dd_mode ? 2000 : 0);

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
				wd_rsp_log_n++;
				fprintf(rf, "RSPTASK ms=%lld seq=%u EXIT pc=%04x status=%08x irq=%u sem=%08x timed=%d\n",
					wd_now_ms(),
					RSP::cpu.get_state().sr[31], RSP::cpu.get_state().pc & 0xfff,
					*RSP::rsp.SP_STATUS_REG, *RSP::cpu.get_state().cp0.irq & 1,
					*RSP::rsp.SP_SEMAPHORE_REG, RSP::SP_STATUS_TIMEOUT);
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
