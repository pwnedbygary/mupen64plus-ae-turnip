#include "rsp_jit.hpp"
#include "rsp_disasm.hpp"
#include <utility>
#include <assert.h>

using namespace std;

//#define TRACE
//#define TRACE_ENTER
//#define TRACE_DISASM

// We're only guaranteed 3 V registers (x86).
#define JIT_REGISTER_STATE JIT_V0
#define JIT_REGISTER_DMEM JIT_V1
#define JIT_REGISTER_INDIRECT_PC JIT_V2

#define JIT_REGISTER_MODE JIT_R1
#define JIT_REGISTER_NEXT_PC JIT_R0

#define JIT_FRAME_SIZE 256

#if __WORDSIZE == 32
#undef jit_ldxr_ui
#define jit_ldxr_ui jit_ldxr_i
#undef jit_ldxi_ui
#define jit_ldxi_ui jit_ldxi_i
#endif

namespace RSP
{
namespace JIT
{
CPU::CPU()
{
	init_jit("RSP");
	init_jit_thunks();
}

CPU::~CPU()
{
	finish_jit();
}

void CPU::invalidate_imem()
{
	for (unsigned i = 0; i < CODE_BLOCKS; i++)
		if (memcmp(cached_imem + i * CODE_BLOCK_WORDS, state.imem + i * CODE_BLOCK_WORDS, CODE_BLOCK_SIZE))
			state.dirty_blocks |= (0x3 << i) >> 1;
}

/* ==========================================================================
   ROUND 31 FIX -- INVALIDATE THE CODE CACHE *INSIDE* THE RUN, NOT AT ITS END.

   `blocks[pc >> 2]` is a plain lookup table: get_jit_block() consults the
   (pc, IMEM-hash) cache in `cached_blocks` ONLY when `blocks[pc >> 2]` is
   NULL.  So a block that is already resident for a pc keeps being used even
   after the IMEM bytes under it were replaced -- until something zeroes the
   table.

   The only thing that zeroes it is invalidate_code(), which run() calls ONCE,
   at the top of a slice, after parallel.cpp's invalidate_imem() has content-
   compared the IMEM.  That ordering is fine when the IMEM is rewritten from
   OUTSIDE the RSP (the CPU-side SP DMA between slices), and it is fine when
   the rewritten bytes are identical (a game re-loading the same ucode).

   It is NOT fine when the RSP rewrites its own IMEM *and then executes the
   new bytes inside the same slice* -- which is exactly the ucode-swap flow:

       rspboot  pc 0x08c  DMA ucode_data -> DMEM
                pc 0x008  DMA ucode text -> IMEM 0x080..0xFFF
                pc 0x034  jr a3  -->  pc 0x080 = the NEW ucode's entry

   MEASURED (F-Zero X EK, DD route, run 30a/30e/30g): the gfx task's entry at
   pc 0x080 runs the code of the PREVIOUS task (the audio ucode, whose 4 KiB
   image occupies the whole IMEM) because those pcs already had resident
   blocks compiled from the audio ucode.  Consequence: the entry's
   `lw k0,0xFF0(r0)` at pc 0x160 never executes, $k0 keeps the audio task's
   leftover 0x152C03C0 (present in RDRAM only inside the audio command list at
   0x411998), and the display-list walk starts outside RDRAM.

   Round 30 marked dirty_blocks on every IMEM DMA word and saw NO change: the
   mark is honoured only by the NEXT slice's invalidate_code(), by which time
   the entry has already run.  The mark must be honoured at the instant the
   next block is looked up.

   The fix below is a generation counter bumped by the RSP's own IMEM-writing
   DMA (rsp_imem_dma_bump, called from cp0.cpp's rsp_dma_read).  The next block
   lookup content-compares the IMEM against cached_imem and only then clears
   the affected chunks.

   UNCONDITIONAL AND REGRESSION-FREE BY CONSTRUCTION: the comparison means a
   re-load of byte-identical code (every plain-cart task load) marks nothing,
   and invalidate_code() returns without touching a single block.  Only a real
   program change invalidates anything. */
static unsigned s_rsp31_imem_gen = 0;
static unsigned s_rsp31_imem_gen_seen = 0;

#define R31_DIAG_PATH "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/"

/* ROUND 31 DIAGNOSTIC: log the (pc, live IMEM word) of every block lookup for
   the pcs that matter to the F3DEX2 entry, and whether the block came from the
   table (HIT) or was compiled (NEW).  `im_pc` is the live IMEM word at the
   block's own pc -- comparing it with the ucode text on the host says which
   program the JIT compiled the block from. */
static int s_r31_blk_armed = 0;

static void r31_blk_log(const char *what, uint32_t pc, uint32_t im0, uint32_t im_pc)
{
	if (!s_r31_blk_armed)
		return;
	switch (pc)
	{
	case 0x000: case 0x008: case 0x018: case 0x064: case 0x080: case 0x094:
	case 0x0a4: case 0x0c0: case 0x12c: case 0x160: case 0x170: case 0x180:
	case 0xfb4: case 0xfd8:
		break;
	default:
		return;
	}
	static int n = 0;
	if (n >= 400)
		return;
	n++;
	FILE *f = fopen(R31_DIAG_PATH "wd_r31blk.txt", "a");
	if (f)
	{
		fprintf(f, "R31BLK %s pc=%03x im_pc=%08x im0=%08x\n", what, pc, im_pc, im0);
		fclose(f);
	}
}

extern "C" void r31_arm_blocks(void)
{
	s_r31_blk_armed = 1;
	FILE *f = fopen(R31_DIAG_PATH "wd_r31blk.txt", "w");
	if (f)
		fclose(f);
}

/* Build marker (verified in the packaged .so with `strings`): R31IMEMGEN.
   Non-static so the linker cannot drop it. */
extern "C" const char rsp31_build_marker[] = "R31IMEMGEN-rsp-imem-dma-invalidation";

extern "C" void rsp_imem_dma_bump(void)
{
	s_rsp31_imem_gen++;
}

void CPU::invalidate_code()
{
	if (!state.dirty_blocks)
		return;

	for (unsigned i = 0; i < CODE_BLOCKS; i++)
	{
		if (state.dirty_blocks & (1 << i))
		{
			memset(blocks + i * CODE_BLOCK_WORDS, 0, CODE_BLOCK_WORDS * sizeof(blocks[0]));
			memcpy(cached_imem + i * CODE_BLOCK_WORDS, state.imem + i * CODE_BLOCK_WORDS, CODE_BLOCK_SIZE);
		}
	}

	state.dirty_blocks = 0;
}

// Need super-fast hash here.
uint64_t CPU::hash_imem(unsigned pc, unsigned count) const
{
	size_t size = count;

	// FNV-1.
	const auto *data = state.imem + pc;
	uint64_t h = 0xcbf29ce484222325ull;
	h = (h * 0x100000001b3ull) ^ pc;
	h = (h * 0x100000001b3ull) ^ count;
	for (size_t i = 0; i < size; i++)
		h = (h * 0x100000001b3ull) ^ data[i];
	return h;
}

#ifdef TRACE
static uint64_t hash_registers(const CPUState *rsp)
{
	const auto *data = rsp->sr;
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 1; i < 32; i++)
		h = (h * 0x100000001b3ull) ^ data[i];

	data = reinterpret_cast<const uint32_t *>(&rsp->cp2);
	unsigned words = sizeof(rsp->cp2) >> 2;
	for (size_t i = 0; i < words; i++)
		h = (h * 0x100000001b3ull) ^ data[i];

	return h;
}

static uint64_t hash_dmem(const CPUState *rsp)
{
	const auto *data = rsp->dmem;
	uint64_t h = 0xcbf29ce484222325ull;
	for (size_t i = 0; i < 1024; i++)
		h = (h * 0x100000001b3ull) ^ data[i];
	return h;
}
#endif

unsigned CPU::analyze_static_end(unsigned pc, unsigned end)
{
	// Scans through IMEM and finds the logical "end" of the instruction stream.
	// A logical end of the instruction stream is where execution must terminate.
	// If we have forward branches into this block, i.e. gotos, they extend the execution stream.
	// However, we cannot execute beyond end.
	unsigned max_static_pc = pc;
	unsigned count = end - pc;

	for (unsigned i = 0; i < count; i++)
	{
		uint32_t instr = state.imem[pc + i];
		uint32_t type = instr >> 26;
		uint32_t target;

		bool forward_goto;
		if (pc + i + 1 >= max_static_pc)
		{
			forward_goto = false;
			max_static_pc = pc + i + 1;
		}
		else
			forward_goto = true;

		// VU
		if ((instr >> 25) == 0x25)
			continue;

		switch (type)
		{
		case 000:
			switch (instr & 63)
			{
			case 010:
			case 011:
				// JR and JALR always terminate execution of the block.
				// We execute the next instruction via delay slot and exit.
				// Unless we can branch past the JR
				// (max_static_pc will be higher than expected),
				// this will be the static end.
				if (!forward_goto)
				{
					max_static_pc = max(pc + i + 2, max_static_pc);
					goto end;
				}
				break;

			case 015:
				// BREAK always terminates.
				if (!forward_goto)
					goto end;
				break;

			default:
				break;
			}
			break;

		case 001: // REGIMM
			switch ((instr >> 16) & 31)
			{
			case 000: // BLTZ
			case 001: // BGEZ
			case 021: // BGEZAL
			case 020: // BLTZAL
				// TODO/Optimization: Handle static branch case where $0 is used.
				target = (pc + i + 1 + instr) & 0x3ff;
				if (target >= pc && target < end) // goto
					max_static_pc = max(max_static_pc, target + 1);
				break;

			default:
				break;
			}
			break;

		case 002: // J
		case 003: // JAL
			// Where we choose to end the block here is critical for performance, since otherwise
			// we end up hashing a lot of garbage as it turns out ...

			// J is resolved by goto. Same with JAL if call target happens to be inside the block.
			target = instr & 0x3ff;
			if (target >= pc && target < end) // goto
			{
				// J is a static jump, so if we aren't branching
				// past this instruction and we're branching backwards,
				// we can end the block here.
				if (!forward_goto)
				{
					max_static_pc = max(pc + i + 2, max_static_pc);
					goto end;
				}
				else
					max_static_pc = max(max_static_pc, target + 1);
			}
			else if (!forward_goto)
			{
				// If we have static branch outside our block,
				// we terminate the block.
				max_static_pc = max(pc + i + 2, max_static_pc);
				goto end;
			}
			break;

		case 004: // BEQ
		case 005: // BNE
		case 006: // BLEZ
		case 007: // BGTZ
			// TODO/Optimization: Handle static branch case where $0 is used.
			target = (pc + i + 1 + instr) & 0x3ff;
			if (target >= pc && target < end) // goto
				max_static_pc = max(max_static_pc, target + 1);
			break;

		default:
			break;
		}
	}

end:
	unsigned ret = min(max_static_pc, end);
	return ret;
}

extern "C"
{
#define BYTE_ENDIAN_FIXUP(x, off) ((((x) + (off)) ^ 3) & 0xfffu)

	/* Host-run budget (shared with DoRspCycles): the RSP ucode runs
	   synchronously on the emulation thread and some libultra ucode loops
	   (SP_STATUS / RDP-busy waits that never get their signal in the
	   emulated environment) run indefinitely.  rsp_enter is invoked at
	   EVERY JIT block boundary; when the budget expires it bails to the
	   run() return path, which yields to the core ("task still running" ->
	   rsp_task_locked + SP interrupt; RSP state preserved).

	   ROUND 17: the budget is now an *executed-instruction* countdown, not a
	   wall-clock deadline.  The 64DD route's whole behaviour depends on WHERE
	   inside the ucode a forced yield lands -- each one can fabricate the
	   libultra yield acknowledgement for a task the ucode never saved, and the
	   next start then resumes from scratch DMEM.  With a `steady_clock`
	   deadline those points moved run to run: five runs of near-identical
	   builds gave 245 / 3 / 2 / 140 RDP kicks and screens ranging from a drawn
	   frame to black, which makes every experiment unfalsifiable.  Counting
	   instructions instead pins the slice boundaries to the emulated program,
	   so the same ROM replays the same slice sequence.

	   The countdown is decremented ONLY by rsp_budget_check(), which the JIT
	   emits at every 32-instruction boundary inside a block and at every
	   intra-block label (loop top), so it is a pure function of the executed
	   control flow.  A wall-clock backstop still exists so a pathological
	   path that never reaches a check cannot hold the emulation thread, but
	   it sits far above any real slice so it cannot perturb the sequence.

	   ROUND 18: the countdown unit is the CHECK, not a fabricated instruction
	   count.  A check is one deterministic event in the emulated program, so
	   "N checks per slice" is exactly reproducible run to run, and it is
	   directly measurable against wall time -- the plugin logs checks and
	   microseconds per slice, which is how the size gets calibrated back to
	   the 2 ms of RSP work the 64DD route was tuned on.  `rsp_budget_wall_hit`
	   records whether a slice ended on the wall backstop instead, so a run
	   whose sequence was perturbed by host timing can never be mistaken for a
	   deterministic one. */
	static long long s_rsp_budget_units = -1;      /* < 0: unlimited          */
	static unsigned long long s_rsp_slice_units = 0; /* checks run this slice */
	static volatile int s_rsp_budget_wall_hit = 0;
	static std::chrono::steady_clock::time_point s_rsp_budget_hard_deadline = std::chrono::steady_clock::time_point::max();

	/* Checks per host slice.  The round-17 build ran 1024 checks (262144
	   countdown / 256) and the route died early with it: measured on the RP6
	   (r18a, .fzxwork/r18a/slice_stats.py) a full 1024-check slice costs
	   100 us of wall time -- twenty times shorter than the 2 ms of RSP work
	   the 64DD route was tuned on -- so the RSP got ~0.1 ms of work per pump
	   and never finished a task before the guest overwrote the header again.
	   20480 checks is that same 2 ms, but counted in emulated work instead of
	   host time, so the slice boundaries stay identical run to run.

	   ROUND 33: the unit count is now a PARAMETER.  The 2 ms slice is what
	   the AUDIO task needs (it parks in a wait loop and only leaves it when
	   the CPU feeds it more data), but it is exactly what stops the GFX task
	   from ever finishing its display list -- and the guest's frame protocol
	   waits for the DP event that only a FINISHED gfx task raises.  A budget
	   is a CAP, not a fixed quantum: a task that finishes early still returns
	   to the guest immediately, so the gfx cap can be generous. */
	#define RSP_BUDGET_SLICE_UNITS 20480LL
	#define RSP_BUDGET_HARD_CAP_US 250000LL

	extern "C" void rsp_set_budget_deadline_us(long long us, long long units)
	{
		s_rsp_slice_units = 0;
		if (us > 0)
		{
			s_rsp_budget_units = (units > 0) ? units : RSP_BUDGET_SLICE_UNITS;
			s_rsp_budget_wall_hit = 0;
			s_rsp_budget_hard_deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(RSP_BUDGET_HARD_CAP_US);
		}
		else
		{
			s_rsp_budget_units = -1;
			s_rsp_budget_hard_deadline = std::chrono::steady_clock::time_point::max();
		}
	}

	/* DIAG (read from DoRspCycles' exit log): how many budget checks this
	   slice actually consumed, i.e. the emulated work the slice bought. */
	extern "C" unsigned long long rsp_slice_units_now(void)
	{
		return s_rsp_slice_units;
	}

	/* DIAG: 1 when the wall backstop, not the check countdown, ended a slice.
	   Any run that reports this is NOT a deterministic run. */
	extern "C" int rsp_budget_wall_hit_now(void)
	{
		return s_rsp_budget_wall_hit;
	}

	static bool rsp_budget_expired()
	{
		if (s_rsp_budget_units < 0)
			return false;
		if (s_rsp_budget_units == 0)
			return true;
		if (std::chrono::steady_clock::now() > s_rsp_budget_hard_deadline)
		{
			s_rsp_budget_wall_hit = 1;
			return true;
		}
		return false;
	}

	extern "C" int rsp_budget_expired_now(void)
	{
		return rsp_budget_expired();
	}

	/* Host-callable budget check used inside the JIT'd blocks (per-32
	   instructions and per loop iteration): returns true when the run budget
	   expired.  This is the only place the countdown moves, so it is exactly
	   proportional to the RSP work the slice did. */
	static jit_uword_t rsp_budget_check()
	{
		if (s_rsp_budget_units < 0)
			return 0;
		s_rsp_slice_units++;
		if (s_rsp_budget_units > 0)
		{
			s_rsp_budget_units--;
			if (s_rsp_budget_units > 0)
				return 0;
			s_rsp_budget_units = 0;
		}
		return 1;
	}

	/* DIAG: freeze heartbeat hook (defined in parallel.cpp). */
	extern "C" void rsp_watchdog_tick(unsigned pc_lo);

	/* ROUND 29 DIAG (defined in parallel.cpp): pc ring at every JIT block
	   boundary.  Round 28 measured that 3372 of the gfx task's 3530 DoRspCycles
	   slices ENTER at pc 0x000 with rspboot resident at IMEM 0, i.e. the whole
	   task restarts from osSpTaskLoad's state every 2 ms slice and never
	   progresses past the first display-list fetch.  The ring records the block
	   pcs leading INTO that restart, which names the instruction that jumped to
	   pc 0.  DD-gated at the call site. */
	extern "C" void r29_pc_hook(unsigned pc_lo);

	/* ROUND 30 DIAG (defined in rsp/cp0.cpp): block-level execution trace of the
	   first gfx task.  Round 29 left a hard contradiction -- every input the
	   ucode's entry reads is correct (header type=1/flags=4, data_ptr 0x284990
	   is a real display list, descriptors already re-based) and yet the first
	   fetch runs with k0 = 0x152C03C0, a value that lives inside the AUDIO
	   task's command list.  This trace records the pc path and the walk
	   registers from the instant the gfx header appears, so the path that
	   reaches IMEM 0x170 without loading k0 from DMEM is visible. */
	extern "C" void r30_pc_hook(unsigned pc_lo);

	/* DD gate (defined in parallel.cpp): 1 when the core wired
	   ForceSynchronize (64DD disk present).  Plain cart games keep
	   stock behavior: budget never fires, JIT emits no budget checks. */
	extern "C" int rsp_ares_budget_enabled(void);

	/* ROUND 36: per-block-entry traces are opt-in (files/wd_trace.flag);
	   on a normal DD run nothing below fires.
	   ROUND 37: the per-preemption IMEM dump (wd_rsp.txt, 43 MB/run) is
	   unbounded I/O in the hot path, so it needs the deeper flag. */
	extern "C" int rsp_diag_trace(void);
	extern "C" int rsp_diag_deep(void);

	/* ------------------------------------------------------------------
	   ROUND 25 DIAG (DD route only -- the call site is gated on
	   rsp_ares_budget_enabled(), i.e. the core's runtime IsDDPresent()).

	   WHO WRITES THE TASK HEADER AND THE OVERLAY DESCRIPTORS?

	   Round 25 fixed the layout offline, from ONE run's RDRAM dump
	   (.fzxwork/r22a/ram.bin) plus that run's traces, so the addresses below
	   are all in that run's coordinates:

	     rspboot   RDRAM 0x7504F0, 0xD0 B, loaded to IMEM 0x000.
	       000  j 0x064                 (+ delay 004 addi $1,$0,0xFC0 = header base)
	       008  lw  $2,16($1)           $2 = DMEM[0xFD0] = t.ucode
	       00C  addi $3,$0,0xF7F        len-1 = 0xF80 = the text size
	       010  addi $7,$0,0x1080       SP_MEM_ADDR = IMEM 0x080
	       014..01C  DMA  t.ucode -> IMEM 0x080, 0xF80 bytes
	       034  jr $7                   -> PC = 0x080 = the F3DEX2 text entry
	       064  ... (reads t.flags bit1 = OS_TASK_DP_WAIT, then DMAs
	                 t.ucode_data[0x18]/size[0x1C] -> DMEM 0x000)
	       0C4  j 0x002                 -> falls into 008 (the text load)
	     So the ONE reader of DMEM 0xFD0 that matters is rspboot, and it is
	     also the code that loads the main text: n=1/n=3 (and n=4/n=5) of
	     wd_imem.txt are rspboot's own transfer.  Nothing else in the ucode
	     DMAs from DMEM 0xFD0.

	     text      RDRAM 0x7505C0, 0xF80 B, loaded to IMEM 0x080.
	       098  lw  $11,0xF0($0)   / 09C lw $12,0xFC4($0)  = FIFO ptr, t.flags
	       0A4  beq $11,$0,0xC0    (cold) / 0B0 beq $12&1,$0,0x12C (warm init)
	       0B4  sw  $0,0xFC4($0)   CONSUMES the OS_TASK_YIELDED bit
	       0B8  j 0x164 -> 160 lw $26,0xFF0($0) = k0 = t.data_ptr
	       12C  lw  $1,0xFD0($0)   = ucode base
	       130..15C  $2 += $1; $3 += $1; $4 += $1; $5 += $1
	                 and store back to DMEM 0x2E0 / 0x2E8 / 0x410 / 0x418
	       15C  (this is an ADD, so running this path TWICE double-offsets
	             every descriptor -- see wd_watch.txt below)
	       FAC  addi $12,$0,0x1000 / FB0 addi $11,$0,0x2E0
	       FB4  lw  $24,0($11)  / FB8 lhu $19,4($11) / FC0 lhu $20,6($11)
	            = the descriptor {u32 src; u16 len-1; u16 dest} at DMEM 0x2E0
	       FBC  jal 0xFD8 (DMA it), FC4 ori $31,$12,0 = 0x1000
	       FD4  jr $31           -> IMEM 0x000: run the freshly loaded overlay
	     overlay A RDRAM 0x751540, 0x98 B  (= data-segment descriptor 0x2E0)
	     overlay B RDRAM 0x7515D8, 0x170 B (= data-segment descriptor 0x2E8)
	       The descriptors come from the ucode DATA segment (r22a: RDRAM
	       0x779860 DMA'd to DMEM 0x000) at +0x2E0 = {00000F80, 00971000} and
	       +0x2E8 = {00001018, 016F1000} -- i.e. OFFSETS, made absolute by the
	       0x12C path above.  Overlay A at IMEM 0x030 then does
	       `sw $24,0xFD0($0)` and at 0x02C `sw $26,0xFF0($0)`: the ucode
	       REWRITES the header's ucode/data_ptr fields when it swaps itself.

	   Consequence: DMEM 0xFC0..0xFFF is not a read-only copy of the OSTask.
	   F3DEX2 owns it (it clears 0xFC4 and rewrites 0xFD0/0xFF0), so "the
	   header is garbage at a preemption point" is not by itself a fault --
	   but a n=5-style rspboot reload from src=0x6F0000 means DMEM 0xFD0 had
	   been left at a non-ucode value when the next task started, and the
	   descriptor at 0x2E0 being a DOUBLE-offset would send the 0xFAC loader
	   to a wild source.  Both are one instruction away from identification if
	   the change is caught as it happens.

	   This hook runs at EVERY JIT block boundary, so a change is attributed
	   to the block that just ran (prev_pc) and the block being entered (pc):
	   disassembling that block names the `sw`.  DD-gated, bounded (160
	   lines), and it only ever reads host memory -- no emulation effect on
	   plain carts. */
	static void r25_dmem_watch(void *cpu, unsigned pc)
	{
		/* Tier A: the task header (0xFC0..0xFFF), the two overlay descriptor
		   pairs the data segment supplies (0x2E0.., 0x410..) and the ucode's
		   saved display-list pointer pair (0xBF8/0xBFC).  These change only a
		   handful of times per task, so every change is logged -- the first
		   run of this instrument capped each word at 12 and lost exactly the
		   F3DEX2 task load it was built to see. */
		static const unsigned offa[30] = {
			0xFC0, 0xFC4, 0xFC8, 0xFCC, 0xFD0, 0xFD4, 0xFD8, 0xFDC,
			0xFE0, 0xFE4, 0xFE8, 0xFEC, 0xFF0, 0xFF4, 0xFF8, 0xFFC,
			/* ROUND 51: 0xF90..0xFBF is the 48 bytes BELOW the OSTask copy
			   and is where the 28-word 0x00010001 run actually starts
			   (measured: every run's freeze dump has 0xF80 = 0/x0 and
			   0xF90..0xFFF = 0x00010001).  It was never watched. */
			0xF90, 0xF94, 0xF98, 0xF9C, 0xFA0, 0xFA4, 0xFA8, 0xFAC,
			0xFB0, 0xFB4, 0xFB8, 0xFBC, 0xBF8, 0xBFC,
		};
		static uint32_t last[30];
		static unsigned last_im0 = 0, last_im1 = 0;
		static unsigned prev_pc = 0;
		static int init = 0;
		static int n = 0, nsw = 0;
		auto &st = static_cast<CPU *>(cpu)->get_state();
		unsigned i;

		if (n >= 4000 && nsw >= 200)
			return;

		/* ROUND 51: the old cap (n >= 400) was exhausted by the 0x2E0/0x410
		   descriptor churn within the first seconds, so the ONE transition
		   this instrument exists to catch -- the header at 0xFC0..0xFFF
		   turning into 0x00010001 -- was never logged.  Measured this round:
		   the window log dies at flush 137 and the header is still a valid
		   gfx task at that point; the all-0x00010001 header appears only
		   afterwards.  The cap is raised 10x and the noisy descriptor tier is
		   dropped (it is covered by the rdsc fields of wd_watch/wd_r25). */
		/* Event B: the live ucode changed (a task load or an in-ucode swap).
		   Logged as one block so the whole visible state travels together. */
		if (init && (st.imem[0] != last_im0 || st.imem[1] != last_im1) && nsw < 60) {
			static FILE *f = NULL;
			if (!f)
				f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_watch.txt", "a");
			if (f) {
				fprintf(f, "R25SW n=%d prev_pc=%03x pc=%03x im0=%08x -> %08x im1=%08x "
				           "st=%08x sr1=%08x sr2=%08x sr26=%08x sr27=%08x\n  hdr",
				        nsw, prev_pc, pc & 0xfff, last_im0, st.imem[0], st.imem[1],
				        *st.cp0.cr[CP0_REGISTER_SP_STATUS],
				        st.sr[1], st.sr[2], st.sr[26], st.sr[27]);
				for (i = 0; i < 16; i++)
					fprintf(f, " %08x", st.dmem[0xfc0 / 4 + i]);
				fprintf(f, "\n  dsc");
				for (i = 0; i < 8; i++)
					fprintf(f, " %08x", st.dmem[0x2e0 / 4 + i]);
				fprintf(f, "\n  dsc2");
				for (i = 0; i < 4; i++)
					fprintf(f, " %08x", st.dmem[0x410 / 4 + i]);
				fprintf(f, "\n");
				fflush(f);
			}
			nsw++;
		}
		last_im0 = st.imem[0];
		last_im1 = st.imem[1];

		/* Event C: the display-list pointer k0 ($26).  This is the one that
		   decides whether the walk reads the guest's real list.
		   .fzxwork/r25c/dmatr.txt (RSP DMA trace) shows the gfx task's walk
		   starting at RDRAM 0x2C03C0 while DMEM 0xFF0 -- the header's
		   data_ptr, which IMEM 0x160 `lw $26,0xFF0($0)` is supposed to load
		   it from -- reads 0x00284990 in the very same transfers.  So $26 was
		   either not loaded from there or overwritten afterwards; logging
		   every change of $26 with the block that made it names the
		   instruction.  Bounded, DD-gated. */
		{
			static unsigned last26 = 0;
			static int n26 = 0;
			/* Skip the audio ucode (IMEM[0] = 0x340a0fc0, the 4 KiB blob at
			   RDRAM 0x768E60): it churns $26 thousands of times per second
			   and filled the whole budget in the first run of this
			   instrument, so the gfx task -- the one that matters -- never
			   got logged. */
			if (rsp_diag_trace() && init && st.imem[0] != 0x340a0fc0u &&
			    st.sr[26] != last26 && n26 < 400) {
				static FILE *f = NULL;
				if (!f)
					f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_k0.txt", "a");
				if (f) {
					fprintf(f,
					        "R25K0 n=%d prev_pc=%03x pc=%03x sr26=%08x -> %08x ff0=%08x bf8=%08x "
					        "sr1=%08x sr2=%08x sr11=%08x sr12=%08x sr19=%08x sr20=%08x sr24=%08x sr27=%08x "
					        "sr31=%08x im0=%08x fc4=%08x f0=%08x 2e0=%08x 2e8=%08x st=%08x\n",
					        n26, prev_pc, pc & 0xfff, last26, st.sr[26],
					        st.dmem[0xff0 / 4], st.dmem[0xbf8 / 4],
					        st.sr[1], st.sr[2], st.sr[11], st.sr[12],
					        st.sr[19], st.sr[20], st.sr[24], st.sr[27], st.sr[31],
					        st.imem[0], st.dmem[0xfc4 / 4], st.dmem[0x0f0 / 4],
					        st.dmem[0x2e0 / 4], st.dmem[0x2e8 / 4],
					        *st.cp0.cr[CP0_REGISTER_SP_STATUS]);
					fflush(f);
				}
				n26++;
			}
			last26 = st.sr[26];
		}

		for (i = 0; i < 30; i++) {
			uint32_t v = st.dmem[offa[i] >> 2];
			if (!init) {
				last[i] = v;
				continue;
			}
			if (v == last[i])
				continue;
			if (n < 400) {
				static FILE *f = NULL;
				if (!f)
					f = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_watch.txt", "a");
				if (f) {
					fprintf(f,
					        "R25W n=%d prev_pc=%03x pc=%03x dmem[%03x] %08x -> %08x st=%08x "
					        "sr1=%08x sr2=%08x sr11=%08x sr12=%08x sr24=%08x sr26=%08x sr27=%08x "
					        "im0=%08x im1=%08x fd0=%08x ff0=%08x 2e0=%08x 2e8=%08x bf8=%08x\n",
					        n, prev_pc, pc & 0xfff, offa[i], last[i], v,
					        *st.cp0.cr[CP0_REGISTER_SP_STATUS],
					        st.sr[1], st.sr[2], st.sr[11], st.sr[12],
					        st.sr[24], st.sr[26], st.sr[27],
					        st.imem[0], st.imem[1],
					        st.dmem[0xfd0 / 4], st.dmem[0xff0 / 4],
					        st.dmem[0x2e0 / 4], st.dmem[0x2e8 / 4],
					        st.dmem[0xbf8 / 4]);
					fflush(f);
				}
				n++;
			}
			last[i] = v;
		}
		init = 1;
		prev_pc = pc & 0xfff;
	}

	static Func rsp_enter(void *cpu, unsigned pc)
	{
		if (rsp_ares_budget_enabled()) {
			r29_pc_hook(pc);
			r30_pc_hook(pc);
			r25_dmem_watch(cpu, pc);
		}
		if (rsp_ares_budget_enabled() && rsp_budget_expired()) {
			rsp_watchdog_tick(pc);
			return static_cast<CPU *>(cpu)->get_return_thunk();
		}
		return static_cast<CPU *>(cpu)->get_jit_block(pc);
	}

	static jit_word_t rsp_unaligned_lh(const uint8_t *dram, jit_word_t addr)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		return jit_word_t(int16_t((dram[off0] << 8) |
		                          (dram[off1] << 0)));
	}

	static jit_word_t rsp_unaligned_lw(const uint8_t *dram, jit_word_t addr)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		auto off2 = BYTE_ENDIAN_FIXUP(addr, 2);
		auto off3 = BYTE_ENDIAN_FIXUP(addr, 3);

		// To sign extend, or not to sign extend, hm ...
		return jit_word_t((int32_t(dram[off0]) << 24) |
		                  (int32_t(dram[off1]) << 16) |
		                  (int32_t(dram[off2]) << 8) |
		                  (int32_t(dram[off3]) << 0));
	}

	static jit_uword_t rsp_unaligned_lhu(const uint8_t *dram, jit_word_t addr)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		return jit_word_t(uint16_t((dram[off0] << 8) |
		                          (dram[off1] << 0)));
	}

	static void rsp_unaligned_sh(uint8_t *dram, jit_word_t addr, jit_word_t data)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		dram[off0] = (data >> 8) & 0xff;
		dram[off1] = (data >> 0) & 0xff;
	}

	static void rsp_unaligned_sw(uint8_t *dram, jit_word_t addr, jit_word_t data)
	{
		auto off0 = BYTE_ENDIAN_FIXUP(addr, 0);
		auto off1 = BYTE_ENDIAN_FIXUP(addr, 1);
		auto off2 = BYTE_ENDIAN_FIXUP(addr, 2);
		auto off3 = BYTE_ENDIAN_FIXUP(addr, 3);

		dram[off0] = (data >> 24) & 0xff;
		dram[off1] = (data >> 16) & 0xff;
		dram[off2] = (data >> 8) & 0xff;
		dram[off3] = (data >> 0) & 0xff;
	}

#ifdef TRACE
	static void rsp_report_pc(const CPUState *state, jit_uword_t pc, jit_uword_t instr)
	{
		auto disasm = disassemble(pc, instr);
		disasm += " (" + std::to_string(hash_registers(state)) + ") (" + std::to_string(hash_dmem(state)) + ")";
		puts(disasm.c_str());
	}
#endif

#ifdef TRACE_ENTER
	static void rsp_report_enter(jit_uword_t pc)
	{
		printf("  ... Enter 0x%03x ...  ", unsigned(pc & 0xffcu));
	}
#endif
}

void CPU::jit_save_indirect_register(jit_state_t *_jit, unsigned mips_register)
{
	unsigned jit_reg = regs.load_mips_register_noext(_jit, mips_register);
	jit_movr(JIT_REGISTER_INDIRECT_PC, jit_reg);
	regs.unlock_mips_register(mips_register);
}

void CPU::jit_save_illegal_indirect_register(jit_state_t *_jit)
{
	jit_stxi(-JIT_FRAME_SIZE + 3 * sizeof(jit_word_t), JIT_FP, JIT_REGISTER_INDIRECT_PC);
}

void CPU::jit_load_indirect_register(jit_state_t *_jit, unsigned jit_reg)
{
	jit_movr(jit_reg, JIT_REGISTER_INDIRECT_PC);
}

void CPU::jit_load_illegal_indirect_register(jit_state_t *_jit, unsigned jit_reg)
{
	jit_ldxi(jit_reg, JIT_FP, -JIT_FRAME_SIZE + 3 * sizeof(jit_word_t));
}

void CPU::jit_begin_call(jit_state_t *_jit)
{
	// Workarounds weird Lightning behavior around register usage.
	// It has been observed that EBX (V0) is clobbered on x86 Linux when
	// calling out to C code.
	jit_live(JIT_REGISTER_STATE);
	jit_live(JIT_REGISTER_DMEM);
	jit_live(JIT_REGISTER_INDIRECT_PC);

	jit_prepare();
}

void CPU::jit_end_call(jit_state_t *_jit, jit_pointer_t ptr)
{
	jit_finishi(ptr);

	// Workarounds weird Lightning behavior around register usage.
	// It has been observed that EBX (V0) is clobbered on x86 Linux when
	// calling out to C code.
	jit_live(JIT_REGISTER_STATE);
	jit_live(JIT_REGISTER_DMEM);
	jit_live(JIT_REGISTER_INDIRECT_PC);
}

void CPU::jit_save_illegal_cond_branch_taken(jit_state_t *_jit)
{
	unsigned cond_reg = regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
	jit_stxi(-JIT_FRAME_SIZE + sizeof(jit_word_t), JIT_FP, cond_reg);
	regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
}

void CPU::jit_restore_illegal_cond_branch_taken(jit_state_t *_jit, unsigned reg)
{
	jit_ldxi(reg, JIT_FP, -JIT_FRAME_SIZE + sizeof(jit_word_t));
}

void CPU::jit_clear_illegal_cond_branch_taken(jit_state_t *_jit, unsigned tmp_reg)
{
	jit_movi(tmp_reg, 0);
	jit_stxi(-JIT_FRAME_SIZE + sizeof(jit_word_t), JIT_FP, tmp_reg);
}

void CPU::init_jit_thunks()
{
	jit_state_t *_jit = jit_new_state();

	jit_prolog();

	// Saves registers from C++ code.
	jit_frame(JIT_FRAME_SIZE);
	auto *state = jit_arg();

	// These registers remain fixed and all called thunks will poke into these registers as necessary.
	jit_getarg(JIT_REGISTER_STATE, state);
	jit_ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, pc));
	jit_ldxi(JIT_REGISTER_DMEM, JIT_REGISTER_STATE, offsetof(CPUState, dmem));

	// When thunks need non-local goto, they jump here.
	auto *entry_label = jit_indirect();

#ifdef TRACE_ENTER
	{
		// Save PC.
		jit_stxi_i(offsetof(CPUState, pc), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);
		jit_prepare();
		jit_pushargr(JIT_REGISTER_NEXT_PC);
		jit_finishi(reinterpret_cast<jit_pointer_t>(rsp_report_enter));
		jit_ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, pc));
	}
#endif

	jit_prepare();
	jit_pushargr(JIT_REGISTER_STATE);
	jit_pushargr(JIT_REGISTER_NEXT_PC);
	jit_finishi(reinterpret_cast<jit_pointer_t>(rsp_enter));
	jit_retval(JIT_REGISTER_NEXT_PC);

	// Jump to thunk.

	// Clear out branch delay slots.
	jit_clear_illegal_cond_branch_taken(_jit, JIT_REGISTER_MODE);
	jit_stxi_i(offsetof(CPUState, sr) + RegisterCache::COND_BRANCH_TAKEN * 4, JIT_REGISTER_STATE, JIT_REGISTER_MODE);

	jit_jmpr(JIT_REGISTER_NEXT_PC);

	// When we want to return, JIT thunks will jump here.
	auto *return_label = jit_indirect();

	// Save PC.
	jit_stxi_i(offsetof(CPUState, pc), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);

	// Return status. This register is considered common for all thunks.
	jit_retr(JIT_REGISTER_MODE);

	jit_realize();
	jit_word_t code_size;
	jit_get_code(&code_size);
	void *thunk_code = allocator.allocate_code(code_size);
	if (!thunk_code)
		abort();
	jit_set_code(thunk_code, code_size);

	thunks.enter_frame = reinterpret_cast<int (*)(void *)>(jit_emit());
	thunks.enter_thunk = jit_address(entry_label);
	thunks.return_thunk = jit_address(return_label);

	//printf(" === DISASM ===\n");
	//jit_disassemble();
	jit_clear_state();
	//printf(" === END DISASM ===\n");
	jit_destroy_state();

	if (!Allocator::commit_code(thunk_code, code_size))
		abort();
}

Func CPU::get_jit_block(uint32_t pc)
{
	pc &= IMEM_SIZE - 1;

	/* ROUND 31: honour an IMEM rewrite by the RSP's own DMA immediately.  See
	   the long note above invalidate_code().  Content-compared, so a task load
	   of unchanged code is free. */
	if (s_rsp31_imem_gen != s_rsp31_imem_gen_seen)
	{
		uint32_t before;
		s_rsp31_imem_gen_seen = s_rsp31_imem_gen;
		before = state.dirty_blocks;
		if (rsp_ares_budget_enabled())
		{
			/* DD ROUTE: unconditional.  MEASURED (run 31b): the content
			   comparison below finds NOTHING to invalidate here -- by the
			   time this lookup runs, cached_imem already matches the new
			   IMEM -- so the entry at pc 0x080 kept executing the AUDIO
			   ucode's resident block.  Proof: with IMEM 0x160 (the entry's
			   own `lw k0,0xFF0(r0)`) patched to `lui k0,0x5A5A` at the text
			   load, the first fetch STILL reported k0=152c03c0 -- the
			   patched instruction never ran.  A ucode load is a handful of
			   events per task, so clearing everything is cheap and it is the
			   only thing that is unconditionally correct. */
			state.dirty_blocks = ~0u;
		}
		else
		{
			/* Plain carts: byte-identical re-loads of the same ucode mark
			   nothing and cost one comparison per chunk. */
			invalidate_imem();
		}
		{
			static int gn = 0;
			/* ROUND 39: opt-in.  This sat in the PLAIN-CART branch of the
			   ucode-load path, so every plain game wrote up to 200 lines of
			   wd_r31gen.txt (measured: 14 KB per Mario Tennis launch).  The
			   DD route already has files/wd_trace.flag for exactly this. */
			if (gn < 200 && rsp_diag_trace())
			{
				FILE *f = fopen(R31_DIAG_PATH "wd_r31gen.txt", "a");
				gn++;
				if (f)
				{
					fprintf(f, "R31GEN gen=%u dirty_before=%08x dirty_after=%08x im0=%08x\n",
					        s_rsp31_imem_gen, before, state.dirty_blocks, state.imem[0]);
					fclose(f);
				}
			}
		}
		invalidate_code();
	}

	uint32_t word_pc = pc >> 2;
	auto &block = blocks[word_pc];

	if (!block)
	{
		unsigned end = (pc + (CODE_BLOCK_SIZE * 2)) >> CODE_BLOCK_SIZE_LOG2;
		end <<= CODE_BLOCK_SIZE_LOG2 - 2;
		end = min(end, unsigned(IMEM_SIZE >> 2));
		end = analyze_static_end(word_pc, end);

		uint64_t hash = hash_imem(word_pc, end - word_pc);
		auto &ptr = cached_blocks[word_pc][hash];
		if (ptr)
			block = ptr;
		else
			block = ptr = jit_region(hash, word_pc, end - word_pc);
		r31_blk_log("NEW", pc, state.imem[0], state.imem[word_pc]);
	}
	else
		r31_blk_log("HIT", pc, state.imem[0], state.imem[word_pc]);
	return block;
}

int CPU::enter(uint32_t pc)
{
	// Top level enter.
	state.pc = pc;
	static_assert(offsetof(CPU, state) == 0, "CPU state must lie on first byte.");
	int ret = thunks.enter_frame(this);
	return ret;
}

void CPU::jit_end_of_block(jit_state_t *_jit, uint32_t pc, const CPU::InstructionInfo &last_info)
{
	// If we run off the end of a block with a pending delay slot, we need to move it to CPUState.
	// We always branch to the next PC, and the delay slot will be handled after the first instruction in next block.

	unsigned cond_branch_reg = 0;
	if (last_info.branch && last_info.conditional)
	{
		cond_branch_reg = regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
	}
	unsigned scratch_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
	regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
	regs.flush_register_window(_jit);

	jit_node_t *forward = nullptr;
	if (last_info.branch)
	{
		if (last_info.conditional)
			forward = jit_beqi(cond_branch_reg, 0);

		if (last_info.indirect)
			jit_load_indirect_register(_jit, scratch_reg);
		else
			jit_movi(scratch_reg, last_info.branch_target);
		jit_stxi_i(offsetof(CPUState, branch_target), JIT_REGISTER_STATE, scratch_reg);
		jit_movi(scratch_reg, 1);
		jit_stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, scratch_reg);
	}

	if (forward)
		jit_patch(forward);
	jit_movi(JIT_REGISTER_NEXT_PC, pc);
	jit_patch_abs(jit_jmpi(), thunks.enter_thunk);
}

void CPU::jit_handle_impossible_delay_slot(jit_state_t *_jit, const InstructionInfo &info,
                                           const InstructionInfo &last_info, uint32_t base_pc,
                                           uint32_t end_pc)
{
	unsigned cond_branch_reg = regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
	unsigned scratch_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
	unsigned illegal_cond_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER1);

	regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
	regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
	regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER1);
	regs.flush_register_window(_jit);
	// We can still use the registers after flushing,
	// but we cannot call on the register cache any more until we resolve the branch.

	// A case here would be:
	// beq r0, r1, somewhere
	// beq r1, r2, somewhere
	// <-- we are here ...
	// add r0, r1, r2

	// This case should normally never happen, but you never know what happens on a fixed platform ...
	// Cond branch information for the first branch is found in JIT_FP[-JIT_FRAME_SIZE].
	// Cond branch information for the second branch is found in COND_BRANCH_TAKEN.

	// If the first branch was taken, we will transfer control, but we will never use a local goto here
	// since we potentially need to set the has_delay_slot argument.
	// If the first branch is not taken, we will defer any control transfer until the next instruction, nothing happens,
	// except that FP[0] is cleared.

	jit_node_t *nobranch = nullptr;
	if (last_info.conditional)
	{
		jit_restore_illegal_cond_branch_taken(_jit, illegal_cond_reg);
		jit_clear_illegal_cond_branch_taken(_jit, scratch_reg);
		nobranch = jit_beqi(illegal_cond_reg, 0);
	}
	else
		jit_clear_illegal_cond_branch_taken(_jit, cond_branch_reg);

	// ... But do we have a delay slot to take care of?
	if (!info.conditional)
		jit_movi(cond_branch_reg, 1);
	jit_stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, cond_branch_reg);

	if (info.indirect)
		jit_load_indirect_register(_jit, cond_branch_reg);
	else
		jit_movi(cond_branch_reg, info.branch_target);
	jit_stxi_i(offsetof(CPUState, branch_target), JIT_REGISTER_STATE, cond_branch_reg);

	// We are done with register use.

	// Here we *will* take the branch.
	if (last_info.indirect)
		jit_load_illegal_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
	else
		jit_movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);

	jit_patch_abs(jit_jmpi(), thunks.enter_thunk);

	if (nobranch)
		jit_patch(nobranch);
}

void CPU::jit_handle_delay_slot(jit_state_t *_jit, const InstructionInfo &last_info,
                                uint32_t base_pc, uint32_t end_pc)
{
	unsigned scratch_cond_reg = 0;
	if (last_info.conditional)
	{
		regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
		unsigned cond_branch_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);

		scratch_cond_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);

		// Clear out branch state.
		jit_movr(scratch_cond_reg, cond_branch_reg);
		jit_movi(cond_branch_reg, 0);

		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
	}
	else
	{
		unsigned cond_branch_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
		jit_movi(cond_branch_reg, 0);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
	}
	regs.flush_register_window(_jit);

	if (last_info.conditional)
	{
		if (!last_info.indirect && last_info.branch_target >= base_pc && last_info.branch_target < end_pc)
		{
			// Patch this up later.
			unsigned local_index = (last_info.branch_target - base_pc) >> 2;
			local_branches.push_back({ jit_bnei(scratch_cond_reg, 0), local_index });
		}
		else
		{
			auto *no_branch = jit_beqi(scratch_cond_reg, 0);
			if (last_info.indirect)
				jit_load_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
			else
				jit_movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);
			jit_patch_abs(jit_jmpi(), thunks.enter_thunk);
			jit_patch(no_branch);
		}
	}
	else
	{
		if (!last_info.indirect && last_info.branch_target >= base_pc && last_info.branch_target < end_pc)
		{
			// Patch this up later.
			unsigned local_index = (last_info.branch_target - base_pc) >> 2;
			local_branches.push_back({ jit_jmpi(), local_index });
		}
		else
		{
			if (last_info.indirect)
				jit_load_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
			else
				jit_movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);
			jit_patch_abs(jit_jmpi(), thunks.enter_thunk);
		}
	}
}

void CPU::jit_exit(jit_state_t *_jit, uint32_t pc, const InstructionInfo &last_info,
                   ReturnMode mode, bool first_instruction)
{
	regs.flush_register_window(_jit);
	jit_movi(JIT_REGISTER_MODE, mode);
	jit_exit_dynamic(_jit, pc, last_info, first_instruction);
}

void CPU::jit_exit_dynamic(jit_state_t *_jit, uint32_t pc, const InstructionInfo &last_info, bool first_instruction)
{
	// We must not touch REGISTER_MODE / TMP1 here, fortunately we don't need to.
	if (first_instruction)
	{
		// Need to consider that we need to move delay slot to PC.
		jit_ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, has_delay_slot));

		auto *latent_delay_slot = jit_bnei(JIT_REGISTER_NEXT_PC, 0);

		// Common case.
		// Immediately exit.
		jit_movi(JIT_REGISTER_NEXT_PC, (pc + 4) & 0xffcu);
		jit_patch_abs(jit_jmpi(), thunks.return_thunk);

		// If we had a latent delay slot, we handle it here.
		jit_patch(latent_delay_slot);

		// jit_exit is never called from a branch instruction, so we do not have to handle double branch delay slots here.
		jit_movi(JIT_REGISTER_NEXT_PC, 0);
		jit_stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);
		jit_ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, branch_target));
	}
	else if (!last_info.branch)
	{
		// Immediately exit.
		jit_movi(JIT_REGISTER_NEXT_PC, (pc + 4) & 0xffcu);
	}
	else if (!last_info.indirect && !last_info.conditional)
	{
		// Redirect PC to whatever value we were supposed to branch to.
		jit_movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);
	}
	else if (!last_info.conditional)
	{
		// We have an indirect branch, load that register into PC.
		jit_load_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
	}
	else if (last_info.indirect)
	{
		// Indirect conditional branch.
		jit_ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE,
		           offsetof(CPUState, sr) + RegisterCache::COND_BRANCH_TAKEN * 4);
		auto *node = jit_beqi(JIT_REGISTER_NEXT_PC, 0);
		jit_load_indirect_register(_jit, JIT_REGISTER_NEXT_PC);
		auto *to_end = jit_jmpi();
		jit_patch(node);
		jit_movi(JIT_REGISTER_NEXT_PC, (pc + 4) & 0xffcu);
		jit_patch(to_end);
	}
	else
	{
		// Direct conditional branch.
		jit_ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE,
		           offsetof(CPUState, sr) + RegisterCache::COND_BRANCH_TAKEN * 4);
		auto *node = jit_beqi(JIT_REGISTER_NEXT_PC, 0);
		jit_movi(JIT_REGISTER_NEXT_PC, last_info.branch_target);
		auto *to_end = jit_jmpi();
		jit_patch(node);
		jit_movi(JIT_REGISTER_NEXT_PC, (pc + 4) & 0xffcu);
		jit_patch(to_end);
	}

	jit_patch_abs(jit_jmpi(), thunks.return_thunk);
}

void CPU::jit_emit_store_operation(jit_state_t *_jit,
                                   uint32_t pc, uint32_t instr,
                                   void (*jit_emitter)(jit_state_t *jit, unsigned, unsigned, unsigned), const char *asmop,
                                   jit_pointer_t rsp_unaligned_op,
                                   uint32_t endian_flip,
                                   const InstructionInfo &last_info)
{
	uint32_t align_mask = 3 - endian_flip;
	unsigned rt = (instr >> 16) & 31;
	int16_t simm = int16_t(instr);
	unsigned rs = (instr >> 21) & 31;
	unsigned rt_reg = regs.load_mips_register_noext(_jit, rt);
	unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
	unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
	jit_addi(rs_tmp_reg, rs_reg, simm);
	jit_andi(rs_tmp_reg, rs_tmp_reg, 0xfffu);

	// If we are unaligned, it gets very messy to JIT, so just thunk it out to C code.
	jit_node_t *unaligned = nullptr;
	if (align_mask)
	{
		regs.unlock_mips_register(rt);
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
		// We're going to call, so need to save caller-save register we care about.
		regs.flush_caller_save_registers(_jit);

		unaligned = jit_bmsi(rs_tmp_reg, align_mask);
	}

	// The MIPS is big endian, but the words are swapped per word in integration, so it's kinda little-endian,
	// except we need to XOR the address for byte and half-word accesses.
	if (endian_flip != 0)
		jit_xori(rs_tmp_reg, rs_tmp_reg, endian_flip);

	jit_emitter(_jit, rs_tmp_reg, JIT_REGISTER_DMEM, rt_reg);

	jit_node_t *aligned = nullptr;
	if (align_mask)
	{
		aligned = jit_jmpi();
		jit_patch(unaligned);
		jit_begin_call(_jit);
		jit_pushargr(JIT_REGISTER_DMEM);
		jit_pushargr(rs_tmp_reg);
		jit_pushargr(rt_reg);
		jit_end_call(_jit, rsp_unaligned_op);
		jit_patch(aligned);
	}
	else
	{
		regs.unlock_mips_register(rt);
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
	}
}

// The RSP may or may not have a load-delay slot, but it doesn't seem to matter in practice, so just emulate without
// a load-delay slot.

void CPU::jit_emit_load_operation(jit_state_t *_jit,
                                  uint32_t pc, uint32_t instr,
                                  void (*jit_emitter)(jit_state_t *jit, unsigned, unsigned, unsigned), const char *asmop,
                                  jit_pointer_t rsp_unaligned_op,
                                  uint32_t endian_flip,
                                  const InstructionInfo &last_info)
{
	uint32_t align_mask = endian_flip ^ 3;
	unsigned rt = (instr >> 16) & 31;
	if (rt == 0)
		return;

	int16_t simm = int16_t(instr);
	unsigned rs = (instr >> 21) & 31;
	unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
	unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
	jit_addi(rs_tmp_reg, rs_reg, simm);
	jit_andi(rs_tmp_reg, rs_tmp_reg, 0xfffu);

	unsigned ret_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER1);

	// If we are unaligned, it gets very messy to JIT, so just thunk it out to C code.
	jit_node_t *unaligned = nullptr;
	if (align_mask)
	{
		// Flush the register cache here since we might call.
		// We will still use rs_reg/rt_reg, but they only live for this short burst only.
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER1);

		regs.flush_caller_save_registers(_jit);
		unaligned = jit_bmsi(rs_tmp_reg, align_mask);
	}

	// The MIPS is big endian, but the words are swapped per word in integration, so it's kinda little-endian,
	// except we need to XOR the address for byte and half-word accesses.
	if (endian_flip != 0)
		jit_xori(rs_tmp_reg, rs_tmp_reg, endian_flip);

	jit_emitter(_jit, ret_reg, JIT_REGISTER_DMEM, rs_tmp_reg);

	jit_node_t *aligned = nullptr;
	if (align_mask)
	{
		aligned = jit_jmpi();
		jit_patch(unaligned);
	}

	if (align_mask)
	{
		// We're going to call, so need to save caller-save register we care about.
		jit_begin_call(_jit);
		jit_pushargr(JIT_REGISTER_DMEM);
		jit_pushargr(rs_tmp_reg);
		jit_end_call(_jit, rsp_unaligned_op);
		jit_retval(ret_reg);
		jit_patch(aligned);
	}
	else
	{
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
		regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER1);
	}

	unsigned rt_reg = regs.modify_mips_register(_jit, rt);
	jit_movr(rt_reg, ret_reg);
	regs.unlock_mips_register(rt);
}

void CPU::jit_instruction(jit_state_t *_jit, uint32_t pc, uint32_t instr,
                          InstructionInfo &info, const InstructionInfo &last_info,
                          bool first_instruction, bool next_instruction_is_branch_target)
{
#ifdef TRACE
	regs.flush_register_window(_jit);
	jit_begin_call(_jit);
	jit_pushargr(JIT_REGISTER_STATE);
	jit_pushargi(pc);
	jit_pushargi(instr);
	jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(rsp_report_pc));
#endif

	// VU
	if ((instr >> 25) == 0x25)
	{
		// VU instruction. COP2, and high bit of opcode is set.
		uint32_t op = instr & 63;
		uint32_t vd = (instr >> 6) & 31;
		uint32_t vs = (instr >> 11) & 31;
		uint32_t vt = (instr >> 16) & 31;
		uint32_t e = (instr >> 21) & 15;

		using VUOp = void (*)(RSP::CPUState *, unsigned vd, unsigned vs, unsigned vt, unsigned e);

		static const VUOp ops[64] = {
			RSP_VMULF, RSP_VMULU, nullptr, nullptr, RSP_VMUDL, RSP_VMUDM, RSP_VMUDN, RSP_VMUDH, RSP_VMACF, RSP_VMACU, nullptr,
			nullptr, RSP_VMADL, RSP_VMADM, RSP_VMADN, RSP_VMADH, RSP_VADD, RSP_VSUB, nullptr, RSP_VABS, RSP_VADDC, RSP_VSUBC,
			nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, RSP_VSAR, nullptr, nullptr, RSP_VLT,
			RSP_VEQ, RSP_VNE, RSP_VGE, RSP_VCL, RSP_VCH, RSP_VCR, RSP_VMRG, RSP_VAND, RSP_VNAND, RSP_VOR, RSP_VNOR,
			RSP_VXOR, RSP_VNXOR, nullptr, nullptr, RSP_VRCP, RSP_VRCPL, RSP_VRCPH, RSP_VMOV, RSP_VRSQ, RSP_VRSQL, RSP_VRSQH,
			RSP_VNOP,
		};

		auto *vuop = ops[op];
		if (!vuop)
			vuop = RSP_RESERVED;

		regs.flush_caller_save_registers(_jit);
		jit_begin_call(_jit);
		jit_pushargr(JIT_REGISTER_STATE);
		jit_pushargi(vd);
		jit_pushargi(vs);
		jit_pushargi(vt);
		jit_pushargi(e);
		jit_end_call(_jit ,reinterpret_cast<jit_pointer_t>(vuop));
		return;
	}

	// TODO: Meaningful register allocation.
	// For now, always flush register state to memory after an instruction for simplicity.
	// Should be red-hot in L1 cache, so probably won't be that bad.
	// On x86 and x64, we unfortunately have an anemic register bank to work with in Lightning.

	uint32_t type = instr >> 26;

#define NOP_IF_RD_ZERO() if (rd == 0) { break; }
#define NOP_IF_RT_ZERO() if (rt == 0) { break; }

	switch (type)
	{
	case 000:
	{
		auto rd = (instr >> 11) & 31;
		auto rt = (instr >> 16) & 31;
		auto shift = (instr >> 6) & 31;
		auto rs = (instr >> 21) & 31;

		switch (instr & 63)
		{
		case 000: // SLL
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_noext(_jit, rt);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			jit_lshi(rd_reg, rt_reg, shift);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			break;
		}

		case 002: // SRL
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_zext(_jit, rt);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			jit_rshi_u(rd_reg, rt_reg, shift);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			break;
		}

		case 003: // SRA
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_sext(_jit, rt);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			jit_rshi(rd_reg, rt_reg, shift);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			break;
		}

		case 004: // SLLV
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_noext(_jit, rt);
			unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
			unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			jit_andi(rs_tmp_reg, rs_reg, 31);
			regs.unlock_mips_register(rs);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			jit_lshr(rd_reg, rt_reg, rs_tmp_reg);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
			break;
		}

		case 006: // SRLV
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_zext(_jit, rt);
			unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
			unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			jit_andi(rs_tmp_reg, rs_reg, 31);
			regs.unlock_mips_register(rs);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			jit_rshr_u(rd_reg, rt_reg, rs_tmp_reg);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
			break;
		}

		case 007: // SRAV
		{
			unsigned rt_reg = regs.load_mips_register_sext(_jit, rt);
			unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
			unsigned rs_tmp_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			jit_andi(rs_tmp_reg, rs_reg, 31);
			regs.unlock_mips_register(rs);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			jit_rshr(rd_reg, rt_reg, rs_tmp_reg);
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rd);
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
			break;
		}

		// If the last instruction is also a branch instruction, we will need to do some funky handling
		// so make sure we save the old branch taken register.
#define FLUSH_IMPOSSIBLE_DELAY_SLOT() do { \
	if (last_info.branch && last_info.conditional) \
		jit_save_illegal_cond_branch_taken(_jit); \
	if (last_info.branch && last_info.indirect) \
		jit_save_illegal_indirect_register(_jit); \
	} while(0)

		case 010: // JR
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			info.branch = true;
			info.indirect = true;
			jit_save_indirect_register(_jit, rs);

			// If someone can branch to the delay slot, we have to turn this into a conditional branch.
			if (next_instruction_is_branch_target)
			{
				info.conditional = true;
				regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 1);
			}
			else
				regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 0);

			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
			break;
		}

		case 011: // JALR
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			jit_save_indirect_register(_jit, rs);
			if (rd != 0)
			{
				regs.immediate_mips_register(_jit, rd, (pc + 8) & 0xffcu);
				regs.unlock_mips_register(rd);
			}

			info.branch = true;
			info.indirect = true;
			// If someone can branch to the delay slot, we have to turn this into a conditional branch.
			if (next_instruction_is_branch_target)
			{
				info.conditional = true;
				regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 1);
			}
			else
				regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 0);

			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
			break;
		}

		case 015: // BREAK
		{
			jit_exit(_jit, pc, last_info, MODE_BREAK, first_instruction);
			info.handles_delay_slot = true;
			break;
		}

#define THREE_REG_OP(op, ext) \
	NOP_IF_RD_ZERO(); \
	unsigned rs_reg = regs.load_mips_register_##ext(_jit, rs); \
	unsigned rt_reg = regs.load_mips_register_##ext(_jit, rt); \
	unsigned rd_reg = regs.modify_mips_register(_jit, rd); \
	jit_##op(rd_reg, rs_reg, rt_reg); \
	regs.unlock_mips_register(rs); \
	regs.unlock_mips_register(rt); \
	regs.unlock_mips_register(rd)

		case 040: // ADD
		case 041: // ADDU
		{
			THREE_REG_OP(addr, noext);
			break;
		}

		case 042: // SUB
		case 043: // SUBU
		{
			THREE_REG_OP(subr, noext);
			break;
		}

		case 044: // AND
		{
			THREE_REG_OP(andr, noext);
			break;
		}

		case 045: // OR
		{
			THREE_REG_OP(orr, noext);
			break;
		}

		case 046: // XOR
		{
			THREE_REG_OP(xorr, noext);
			break;
		}

		case 047: // NOR
		{
			NOP_IF_RD_ZERO();
			unsigned rt_reg = regs.load_mips_register_noext(_jit, rt);
			unsigned rs_reg = regs.load_mips_register_noext(_jit, rs);
			unsigned rd_reg = regs.modify_mips_register(_jit, rd);
			jit_orr(rd_reg, rt_reg, rs_reg);
			jit_xori(rd_reg, rd_reg, jit_word_t(-1));
			regs.unlock_mips_register(rt);
			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(rd);
			break;
		}

		case 052: // SLT
		{
			THREE_REG_OP(ltr, sext);
			break;
		}

		case 053: // SLTU
		{
			THREE_REG_OP(ltr_u, zext);
			break;
		}

		default:
			break;
		}
		break;
	}

	case 001: // REGIMM
	{
		unsigned rt = (instr >> 16) & 31;

		switch (rt)
		{
		case 020: // BLTZAL
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			unsigned rs = (instr >> 21) & 31;
			uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			jit_lti(cond_reg, rs_reg, 0);

			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

			// Link register is written after condition.
			regs.immediate_mips_register(_jit, 31, (pc + 8) & 0xffcu);
			regs.unlock_mips_register(31);

			info.branch = true;
			info.conditional = true;
			info.branch_target = target_pc;
			break;
		}

		case 000: // BLTZ
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			unsigned rs = (instr >> 21) & 31;
			uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;

			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			jit_lti(cond_reg, rs_reg, 0);

			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

			info.branch = true;
			info.conditional = true;
			info.branch_target = target_pc;
			break;
		}

		case 021: // BGEZAL
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			unsigned rs = (instr >> 21) & 31;
			uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			jit_gei(cond_reg, rs_reg, 0);

			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

			// Link register is written after condition.
			regs.immediate_mips_register(_jit, 31, (pc + 8) & 0xffcu);
			regs.unlock_mips_register(31);

			info.branch = true;
			info.conditional = true;
			info.branch_target = target_pc;
			break;
		}

		case 001: // BGEZ
		{
			FLUSH_IMPOSSIBLE_DELAY_SLOT();
			unsigned rs = (instr >> 21) & 31;
			uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			jit_gei(cond_reg, rs_reg, 0);

			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

			info.branch = true;
			info.conditional = true;
			info.branch_target = target_pc;
			break;
		}

		default:
			break;
		}
		break;
	}

	case 003: // JAL
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		uint32_t target_pc = (instr & 0x3ffu) << 2;
		regs.immediate_mips_register(_jit, 31, (pc + 8) & 0xffcu);

		info.branch = true;
		info.branch_target = target_pc;
		if (next_instruction_is_branch_target)
		{
			info.conditional = true;
			regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 1);
		}
		else
			regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 0);

		regs.unlock_mips_register(31);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		break;
	}

	case 002: // J
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		uint32_t target_pc = (instr & 0x3ffu) << 2;

		info.branch = true;
		info.branch_target = target_pc;
		if (next_instruction_is_branch_target)
		{
			info.conditional = true;
			regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 1);
		}
		else
			regs.immediate_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN, 0);

		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		break;
	}

	case 004: // BEQ
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		unsigned rs = (instr >> 21) & 31;
		unsigned rt = (instr >> 16) & 31;
		uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
		unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
		unsigned rt_reg = regs.load_mips_register_sext(_jit, rt);
		unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
		jit_eqr(cond_reg, rs_reg, rt_reg);
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(rt);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		info.branch = true;
		info.conditional = true;
		info.branch_target = target_pc;
		break;
	}

	case 005: // BNE
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		unsigned rs = (instr >> 21) & 31;
		unsigned rt = (instr >> 16) & 31;
		uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
		unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
		unsigned rt_reg = regs.load_mips_register_sext(_jit, rt);
		unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
		jit_ner(cond_reg, rs_reg, rt_reg);
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(rt);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
		info.branch = true;
		info.conditional = true;
		info.branch_target = target_pc;
		break;
	}

	case 006: // BLEZ
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		unsigned rs = (instr >> 21) & 31;
		uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;

		// If using $0, it's an unconditional branch.
		if (rs != 0)
		{
			unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
			unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
			jit_lei(cond_reg, rs_reg, 0);
			regs.unlock_mips_register(rs);
			regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
			info.conditional = true;
		}

		info.branch = true;
		info.branch_target = target_pc;
		break;
	}

	case 007: // BGTZ
	{
		FLUSH_IMPOSSIBLE_DELAY_SLOT();
		unsigned rs = (instr >> 21) & 31;

		// Meaningless
		if (rs == 0)
			break;

		uint32_t target_pc = (pc + 4 + (instr << 2)) & 0xffc;
		unsigned rs_reg = regs.load_mips_register_sext(_jit, rs);
		unsigned cond_reg = regs.modify_mips_register(_jit, RegisterCache::COND_BRANCH_TAKEN);
		jit_gti(cond_reg, rs_reg, 0);
		regs.unlock_mips_register(rs);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);

		info.branch = true;
		info.conditional = true;
		info.branch_target = target_pc;
		break;
	}

#define TWO_REG_RS_IS_ZERO() (((instr >> 21) & 31) == 0)

#define TWO_REG_IMM_OP(op, immtype, ext) \
	unsigned rt = (instr >> 16) & 31; \
	NOP_IF_RT_ZERO(); \
	unsigned rs = (instr >> 21) & 31; \
	unsigned rs_reg = regs.load_mips_register_##ext(_jit, rs); \
	unsigned rt_reg = regs.modify_mips_register(_jit, rt); \
	jit_##op(rt_reg, rs_reg, immtype(instr)); \
	regs.unlock_mips_register(rs); \
	regs.unlock_mips_register(rt)

	case 010: // ADDI
	case 011:
	{
		if (TWO_REG_RS_IS_ZERO())
		{
			unsigned rt = (instr >> 16) & 31;
			NOP_IF_RT_ZERO();
			regs.immediate_mips_register(_jit, rt, int16_t(instr));
			regs.unlock_mips_register(rt);
		}
		else
		{
			TWO_REG_IMM_OP(addi, int16_t, noext);
		}
		break;
	}

	case 012: // SLTI
	{
		TWO_REG_IMM_OP(lti, int16_t, sext);
		break;
	}

	case 013: // SLTIU
	{
		TWO_REG_IMM_OP(lti_u, uint16_t, zext);
		break;
	}

	case 014: // ANDI
	{
		TWO_REG_IMM_OP(andi, uint16_t, noext);
		break;
	}

	case 015: // ORI
	{
		if (TWO_REG_RS_IS_ZERO())
		{
			unsigned rt = (instr >> 16) & 31;
			NOP_IF_RT_ZERO();
			regs.immediate_mips_register(_jit, rt, uint16_t(instr));
			regs.unlock_mips_register(rt);
		}
		else
		{
			TWO_REG_IMM_OP(ori, uint16_t, noext);
		}
		break;
	}

	case 016: // XORI
	{
		TWO_REG_IMM_OP(xori, uint16_t, noext);
		break;
	}

	case 017: // LUI
	{
		unsigned rt = (instr >> 16) & 31;
		NOP_IF_RT_ZERO();
		int16_t imm = int16_t(instr);
		regs.immediate_mips_register(_jit, rt, imm << 16);
		regs.unlock_mips_register(rt);
		break;
	}

	case 020: // COP0
	{
		unsigned rd = (instr >> 11) & 31;
		unsigned rs = (instr >> 21) & 31;
		unsigned rt = (instr >> 16) & 31;

		switch (rs)
		{
		case 000: // MFC0
		{
			regs.flush_register_window(_jit);

			jit_begin_call(_jit);
			jit_pushargr(JIT_REGISTER_STATE);
			jit_pushargi(rt);
			jit_pushargi(rd);
			jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(RSP_MFC0));
			jit_retval(JIT_REGISTER_MODE);

			jit_node_t *noexit = jit_beqi(JIT_REGISTER_MODE, MODE_CONTINUE);
			jit_exit_dynamic(_jit, pc, last_info, first_instruction);
			jit_patch(noexit);

			break;
		}

		case 004: // MTC0
		{
			regs.flush_register_window(_jit);

			jit_begin_call(_jit);
			jit_pushargr(JIT_REGISTER_STATE);
			jit_pushargi(rd);
			jit_pushargi(rt);
			jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(RSP_MTC0));
			jit_retval(JIT_REGISTER_MODE);

			jit_node_t *noexit = jit_beqi(JIT_REGISTER_MODE, MODE_CONTINUE);
			jit_exit_dynamic(_jit, pc, last_info, first_instruction);
			jit_patch(noexit);

			break;
		}

		default:
			break;
		}
		break;
	}

	case 022: // COP2
	{
		unsigned rd = (instr >> 11) & 31;
		unsigned rs = (instr >> 21) & 31;
		unsigned rt = (instr >> 16) & 31;
		unsigned imm = (instr >> 7) & 15;

		switch (rs)
		{
		case 000: // MFC2
		{
			regs.flush_caller_save_registers(_jit);
			regs.flush_mips_register(_jit, rt);
			jit_begin_call(_jit);
			jit_pushargr(JIT_REGISTER_STATE);
			jit_pushargi(rt);
			jit_pushargi(rd);
			jit_pushargi(imm);
			jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(RSP_MFC2));
			break;
		}

		case 002: // CFC2
		{
			regs.flush_caller_save_registers(_jit);
			regs.flush_mips_register(_jit, rt);
			jit_begin_call(_jit);
			jit_pushargr(JIT_REGISTER_STATE);
			jit_pushargi(rt);
			jit_pushargi(rd);
			jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(RSP_CFC2));
			break;
		}

		case 004: // MTC2
		{
			regs.flush_caller_save_registers(_jit);
			regs.flush_mips_register(_jit, rt);
			jit_begin_call(_jit);
			jit_pushargr(JIT_REGISTER_STATE);
			jit_pushargi(rt);
			jit_pushargi(rd);
			jit_pushargi(imm);
			jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(RSP_MTC2));
			break;
		}

		case 006: // CTC2
		{
			regs.flush_caller_save_registers(_jit);
			regs.flush_mips_register(_jit, rt);

			jit_begin_call(_jit);
			jit_pushargr(JIT_REGISTER_STATE);
			jit_pushargi(rt);
			jit_pushargi(rd);
			jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(RSP_CTC2));
			break;
		}

		default:
			break;
		}
		break;
	}

	case 040: // LB
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](jit_state_t *_jit, unsigned a, unsigned b, unsigned c) { jit_ldxr_c(a, b, c); },
		                        "lb",
		                        nullptr,
		                        3, last_info);
		break;
	}

	case 041: // LH
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](jit_state_t *_jit, unsigned a, unsigned b, unsigned c) { jit_ldxr_s(a, b, c); },
		                        "lh",
		                        reinterpret_cast<jit_pointer_t>(rsp_unaligned_lh),
		                        2, last_info);
		break;
	}

	case 043: // LW
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](jit_state_t *_jit, unsigned a, unsigned b, unsigned c) { jit_ldxr_i(a, b, c); },
		                        "lw",
		                        reinterpret_cast<jit_pointer_t>(rsp_unaligned_lw),
		                        0, last_info);
		break;
	}

	case 044: // LBU
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](jit_state_t *_jit, unsigned a, unsigned b, unsigned c) { jit_ldxr_uc(a, b, c); },
		                        "lbu",
		                        nullptr,
		                        3, last_info);
		break;
	}

	case 045: // LHU
	{
		jit_emit_load_operation(_jit, pc, instr,
		                        [](jit_state_t *_jit, unsigned a, unsigned b, unsigned c) { jit_ldxr_us(a, b, c); },
		                        "lhu",
		                        reinterpret_cast<jit_pointer_t>(rsp_unaligned_lhu),
		                        2, last_info);
		break;
	}

	case 050: // SB
	{
		jit_emit_store_operation(_jit, pc, instr,
		                         [](jit_state_t *_jit, unsigned a, unsigned b, unsigned c) { jit_stxr_c(a, b, c); },
		                         "sb",
		                         nullptr,
		                         3, last_info);
		break;
	}

	case 051: // SH
	{
		jit_emit_store_operation(_jit, pc, instr,
		                         [](jit_state_t *_jit, unsigned a, unsigned b, unsigned c) { jit_stxr_s(a, b, c); },
		                         "sh",
		                         reinterpret_cast<jit_pointer_t>(rsp_unaligned_sh),
		                         2, last_info);
		break;
	}

	case 053: // SW
	{
		jit_emit_store_operation(_jit, pc, instr,
		                         [](jit_state_t *_jit, unsigned a, unsigned b, unsigned c) { jit_stxr_i(a, b, c); },
		                         "sh",
		                         reinterpret_cast<jit_pointer_t>(rsp_unaligned_sw),
		                         0, last_info);
		break;
	}

	case 062: // LWC2
	{
		unsigned rt = (instr >> 16) & 31;
		int16_t simm = instr;
		// Sign-extend.
		simm <<= 9;
		simm >>= 9;
		unsigned rs = (instr >> 21) & 31;
		unsigned rd = (instr >> 11) & 31;
		unsigned imm = (instr >> 7) & 15;

		using LWC2Op = void (*)(RSP::CPUState *, unsigned rt, unsigned imm, int simm, unsigned rs);
		static const LWC2Op ops[32] = {
			RSP_LBV, RSP_LSV, RSP_LLV, RSP_LDV, RSP_LQV, RSP_LRV, RSP_LPV, RSP_LUV, RSP_LHV, nullptr, nullptr, RSP_LTV,
		};

		auto *op = ops[rd];
		if (op)
		{
			regs.flush_caller_save_registers(_jit);
			regs.flush_mips_register(_jit, rs);
			jit_begin_call(_jit);
			jit_pushargr(JIT_REGISTER_STATE);
			jit_pushargi(rt);
			jit_pushargi(imm);
			jit_pushargi(simm);
			jit_pushargi(rs);
			jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(op));
		}
		break;
	}

	case 072: // SWC2
	{
		unsigned rt = (instr >> 16) & 31;
		int16_t simm = instr;
		// Sign-extend.
		simm <<= 9;
		simm >>= 9;
		unsigned rs = (instr >> 21) & 31;
		unsigned rd = (instr >> 11) & 31;
		unsigned imm = (instr >> 7) & 15;

		using SWC2Op = void (*)(RSP::CPUState *, unsigned rt, unsigned imm, int simm, unsigned rs);
		static const SWC2Op ops[32] = {
			RSP_SBV, RSP_SSV, RSP_SLV, RSP_SDV, RSP_SQV, RSP_SRV, RSP_SPV, RSP_SUV, RSP_SHV, RSP_SFV, nullptr, RSP_STV,
		};

		auto *op = ops[rd];
		if (op)
		{
			regs.flush_caller_save_registers(_jit);
			regs.flush_mips_register(_jit, rs);
			jit_begin_call(_jit);
			jit_pushargr(JIT_REGISTER_STATE);
			jit_pushargi(rt);
			jit_pushargi(imm);
			jit_pushargi(simm);
			jit_pushargi(rs);
			jit_end_call(_jit, reinterpret_cast<jit_pointer_t>(op));
		}
		break;
	}

	default:
		break;
	}
}

void CPU::jit_mark_block_entries(uint32_t pc, uint32_t end, bool *block_entries)
{
	unsigned count = end - pc;

	// Find all places where we need to insert a label.
	// This also affects codegen for static branches.
	// If the delay slot for a static branch is a block entry,
	// it is not actually a static branch, but a conditional one because
	// some other instruction might have branches into the delay slot.
	for (unsigned i = 0; i < count; i++)
	{
		uint32_t instr = state.imem[pc + i];
		uint32_t type = instr >> 26;
		uint32_t target;

		// VU
		if ((instr >> 25) == 0x25)
			continue;

		switch (type)
		{
		case 001: // REGIMM
			switch ((instr >> 16) & 31)
			{
			case 000: // BLTZ
			case 001: // BGEZ
			case 021: // BGEZAL
			case 020: // BLTZAL
				target = (pc + i + 1 + instr) & 0x3ff;
				if (target >= pc && target < end) // goto
					block_entries[target - pc] = true;
				break;

			default:
				break;
			}
			break;

		case 002:
		case 003:
			// J is resolved by goto. Same with JAL.
			target = instr & 0x3ff;
			if (target >= pc && target < end) // goto
				block_entries[target - pc] = true;
			break;

		case 004: // BEQ
		case 005: // BNE
		case 006: // BLEZ
		case 007: // BGTZ
			target = (pc + i + 1 + instr) & 0x3ff;
			if (target >= pc && target < end) // goto
				block_entries[target - pc] = true;
			break;

		default:
			break;
		}
	}
}

void CPU::jit_handle_latent_delay_slot(jit_state_t *_jit, const InstructionInfo &last_info)
{
	unsigned cond_branch_reg = JIT_REGISTER_NEXT_PC;
	if (last_info.branch && last_info.conditional)
	{
		cond_branch_reg = regs.load_mips_register_noext(_jit, RegisterCache::COND_BRANCH_TAKEN);
		regs.unlock_mips_register(RegisterCache::COND_BRANCH_TAKEN);
	}
	regs.flush_register_window(_jit);

	if (last_info.branch)
	{
		// Well then ... two branches in a row just happened. Try to do something sensible.
		if (!last_info.conditional)
			jit_movi(cond_branch_reg, 1);
		jit_stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, cond_branch_reg);

		jit_ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, branch_target));

		if (last_info.indirect)
			jit_load_indirect_register(_jit, JIT_REGISTER_MODE);
		else
			jit_movi(JIT_REGISTER_MODE, last_info.branch_target);

		jit_stxi_i(offsetof(CPUState, branch_target), JIT_REGISTER_STATE, JIT_REGISTER_MODE);
		jit_patch_abs(jit_jmpi(), thunks.enter_thunk);
	}
	else
	{
		jit_movi(JIT_REGISTER_NEXT_PC, 0);
		jit_stxi_i(offsetof(CPUState, has_delay_slot), JIT_REGISTER_STATE, JIT_REGISTER_NEXT_PC);
		jit_ldxi_i(JIT_REGISTER_NEXT_PC, JIT_REGISTER_STATE, offsetof(CPUState, branch_target));
		jit_patch_abs(jit_jmpi(), thunks.enter_thunk);
	}
}

Func CPU::jit_region(uint64_t hash, unsigned pc_word, unsigned instruction_count)
{
	regs.reset();

	mips_disasm.clear();
	jit_state_t *_jit = jit_new_state();

	jit_prolog();
	jit_tramp(JIT_FRAME_SIZE);

	jit_node_t *branch_targets[CODE_BLOCK_WORDS * 2];
	jit_node_t *latent_delay_slot = nullptr;
	local_branches.clear();

	assert(instruction_count <= (CODE_BLOCK_WORDS * 2));

	// Mark which instructions can be branched to via local goto.
	bool block_entry[CODE_BLOCK_WORDS * 2];
	memset(block_entry, 0, instruction_count * sizeof(bool));
	jit_mark_block_entries(pc_word, pc_word + instruction_count, block_entry);

	InstructionInfo last_info = {};
	InstructionInfo first_info = {};

	for (unsigned i = 0; i < instruction_count; i++)
	{
		if (block_entry[i])
		{
			// Before we enter into a new block, we have to flush register window since someone can branch here.
			regs.flush_register_window(_jit);
			regs.reset();
			branch_targets[i] = jit_label();

			/* DD-ONLY emission: plain games get structurally-identical JIT
			   code (no per-loop host calls), matching the baseline build. */
			if (rsp_ares_budget_enabled())
			{
			/* DIAG: freeze heartbeat with the exact loop-top pc (rate-limited
			   in host; no-op unless a task run exceeds 30ms). */
			jit_prepare();
			jit_pushargi((pc_word + i) << 2);
			jit_finishi(reinterpret_cast<jit_pointer_t>(rsp_watchdog_tick));

			/* Host-run budget at every intra-block label (loop top): a
			   tight ucode loop branches only within its block, so it never
			   crosses a block boundary and never executes the per-32-instr
			   checks if the block is shorter than 32 instructions; the
			   loader's vertex loop then runs forever and starves the CPU
			   (watchdog freeze mid-RSP).  Check the budget on every loop
			   iteration so the emulation thread always yields. */
			jit_prepare();
			jit_finishi(reinterpret_cast<jit_pointer_t>(rsp_budget_check));
			jit_retval(JIT_REGISTER_MODE);
			auto *loop_budget_ok = jit_beqi(JIT_REGISTER_MODE, 0);
			regs.flush_register_window(_jit);
			jit_movi(JIT_REGISTER_MODE, RSP::MODE_CHECK_FLAGS);
			jit_patch_abs(jit_jmpi(), thunks.return_thunk);
			jit_patch(loop_budget_ok);
			}
		}

		uint32_t instr = state.imem[pc_word + i];

#ifdef TRACE_DISASM
		mips_disasm += disassemble((pc_word + i) << 2, instr);
		if (last_info.branch)
		{
			mips_disasm += "  [branch]";
			if (last_info.conditional)
				mips_disasm += "  [cond]";
			if (last_info.indirect)
				mips_disasm += "  [indirect]";
			if (last_info.handles_delay_slot)
				mips_disasm += "  [handles delay slot]";
		}
		if (block_entry[i])
			mips_disasm += "  [block entry]";
		mips_disasm += "\n";
#endif

		InstructionInfo inst_info = {};
		jit_instruction(_jit, (pc_word + i) << 2, instr, inst_info, last_info, i == 0,
		                (i + 1 < instruction_count) && block_entry[i + 1]);

		/* Host-run budget (per 32 instructions): leave the JIT to the
		   return thunk when the budget expired so CPU::run() yields to the
		   core (the core's "task still running" path keeps the guest live).
		   DD-ONLY: plain games emit no checks. */
		if (rsp_ares_budget_enabled() && (i & 0x1F) == 0x1F)
		{
			jit_prepare();
			jit_finishi(reinterpret_cast<jit_pointer_t>(rsp_budget_check));
			jit_retval(JIT_REGISTER_MODE);
			auto *budget_ok = jit_beqi(JIT_REGISTER_MODE, 0);
			regs.flush_register_window(_jit);
			jit_movi(JIT_REGISTER_MODE, RSP::MODE_CHECK_FLAGS);
			jit_patch_abs(jit_jmpi(), thunks.return_thunk);
			jit_patch(budget_ok);
		}

		// Handle all the fun cases with branch delay slots.
		// Not sure if we really need to handle them, but IIRC CXD4 does it and the LLVM RSP as well.

		if (i == 0 && !inst_info.handles_delay_slot)
		{
			unsigned scratch_reg = regs.modify_mips_register(_jit, RegisterCache::SCRATCH_REGISTER0);
			jit_ldxi_i(scratch_reg, JIT_REGISTER_STATE, offsetof(CPUState, has_delay_slot));
			regs.unlock_mips_register(RegisterCache::SCRATCH_REGISTER0);
			regs.flush_register_window(_jit);

			// After the first instruction, we might need to resolve a latent delay slot.
			latent_delay_slot = jit_bnei(scratch_reg, 0);
			first_info = inst_info;
		}
		else if (inst_info.branch && last_info.branch)
		{
			// "Impossible" handling of the delay slot.
			// Happens if we have two branch instructions in a row.
			// Weird magic happens here!
			jit_handle_impossible_delay_slot(_jit, inst_info, last_info, pc_word << 2, (pc_word + instruction_count) << 2);
		}
		else if (!inst_info.handles_delay_slot && last_info.branch)
		{
			// Normal handling of the delay slot.
			jit_handle_delay_slot(_jit, last_info, pc_word << 2, (pc_word + instruction_count) << 2);
		}
		last_info = inst_info;
	}

	regs.flush_register_window(_jit);

	// Jump to another block.
	jit_end_of_block(_jit, (pc_word + instruction_count) << 2, last_info);

	// If we had a latent delay slot, we handle it here.
	if (latent_delay_slot)
	{
		jit_patch(latent_delay_slot);
		jit_handle_latent_delay_slot(_jit, first_info);
	}

	for (auto &b : local_branches)
		jit_patch_at(b.node, branch_targets[b.local_index]);

	jit_realize();
	jit_word_t code_size;
	jit_get_code(&code_size);
	auto *block_code = allocator.allocate_code(code_size);
	if (!block_code)
		abort();
	jit_set_code(block_code, code_size);

	auto ret = reinterpret_cast<Func>(jit_emit());

#ifdef TRACE_DISASM
	printf(" === DISASM ===\n");
	printf("%s\n", mips_disasm.c_str());
	jit_disassemble();
	printf(" === DISASM END ===\n\n");
#endif
	jit_clear_state();
	jit_destroy_state();

	if (!Allocator::commit_code(block_code, code_size))
		abort();
	return ret;
}

ReturnMode CPU::run()
{
	invalidate_code();
	/* Host-run budget: the RSP JIT executes the ucode synchronously on the
	   emulation thread, and some ucode loops (libultra SP_STATUS / RDP-busy
	   waits that never get their signal in the emulated environment) run
	   indefinitely.  Never hold the VM thread: after the budget expires,
	   return MODE_CHECK_FLAGS so DoRspCycles yields to the core (the core's
	   "task still running" path -> rsp_task_locked + SP interrupt keeps the
	   guest moving; the RSP state is preserved for the next DoRspCycles). */

	/* DIAG: one-shot live-RSP capture.  The F-Zero X EK audio (type 2) task
	   hard-wedges: it enters a bare `beq z,z` wait-loop (no SP_STATUS MFC0
	   poll, and type-2 has NO host budget) so DoRspCycles never returns and
	   the CI-loop force-dump never fires.  Capture the live RSP state (all 32
	   GPRs + DMEM window around sp + IMEM) at that wait-spin, plus at any
	   ucode `break` (MODE_BREAK).  One dump per run to bound file size. */
	static int wd_sp_dumped = 0;
	auto wd_sp_capture = [&]() {
		if (wd_sp_dumped) return;
		wd_sp_dumped = 1;
		const char* bp = "/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_sp_break.bin";
		FILE* bf = fopen(bp, "wb");
		if (bf) {
			uint32_t sp = state.sr[29];
			fprintf(bf, "RSPCAP pc=%04x sp=%08x status=%08x ttype=%u imem0=%08x %08x\n",
				state.pc & 0xfff, sp, *state.cp0.cr[CP0_REGISTER_SP_STATUS],
				((uint32_t*)state.dmem)[0xfc0 / 4], state.imem[0], state.imem[1]);
			for (unsigned i = 0; i < 32; i++)
				fprintf(bf, "r%02u %08x\n", i, state.sr[i]);
			/* DMEM window around the RSP stack pointer (the audio ucode reads
			   the command word via `lw k0,0(sp)`).  Word-indexed DMEM. */
			fprintf(bf, "sp_word=%08x\n", sp >> 2);
			unsigned d0 = (sp >> 2) & 1023;
			for (int k = -32; k <= 32; k++) {
				unsigned idx = (d0 + k) & 1023;
				fprintf(bf, "dmem[0x%03x] %08x\n", idx, state.dmem[idx]);
			}
			fputc('\n', bf);
			fwrite(state.imem, 1, 0x1000, bf);
			fwrite(state.dmem, 1, 0x1000, bf);
			fclose(bf);
		}
	};

	for (;;)
	{
		/* DIAG: wait-spin detector — if the ucode PC does not advance across
		   many iterations it is in a tight wait-loop (the audio wedge).  The
		   budget never fires for type-2 audio, so catch it here.  DD-only. */
		if (rsp_ares_budget_enabled())
		{
			static uint32_t wd_last_pc = 0xffffffff;
			static unsigned wd_same = 0;
			if ((state.pc & 0xfff) == wd_last_pc) {
				if (++wd_same > 200000) {
					wd_sp_capture();
					wd_same = 0;
				}
			} else {
				wd_last_pc = state.pc & 0xfff;
				wd_same = 0;
			}
		}
		int ret = enter(state.pc);
		switch (ret)
		{
		case MODE_BREAK:
			*state.cp0.cr[CP0_REGISTER_SP_STATUS] |= SP_STATUS_BROKE | SP_STATUS_HALT;
			if (*state.cp0.cr[CP0_REGISTER_SP_STATUS] & SP_STATUS_INTR_BREAK)
				*state.cp0.irq |= 1;
			if (rsp_ares_budget_enabled())
				wd_sp_capture();
#ifndef PARALLEL_INTEGRATION
			print_registers();
#endif
			return MODE_BREAK;

		case MODE_CHECK_FLAGS:
		case MODE_DMA_READ:
			return static_cast<ReturnMode>(ret);

		default:
			break;
		}
		if (rsp_ares_budget_enabled() && rsp_budget_expired())
		{
#ifdef PARALLEL_INTEGRATION
			static FILE* rf = NULL;
			if (rsp_diag_deep() && !rf)
				rf = fopen("/data/data/org.mupen64plusae.turnip.pwnedbygary.debug/files/wd_rsp.txt", "a");
			if (rf)
			{
				/* dump IMEM words around the preempt pc so the ucode's wait
				   loop can be identified offline (state.pc is byte-addressed,
				   state.imem is word-indexed) */
				unsigned w = (state.pc >> 2) & 0x3ff;
				fprintf(rf, "RSPBUDGET pc=%04x status=%08x sr1=%08x imem=%08x %08x %08x %08x %08x %08x\n",
					state.pc & 0xfff, *state.cp0.cr[CP0_REGISTER_SP_STATUS], state.sr[1],
					state.imem[w], state.imem[w + 1], state.imem[w + 2],
					state.imem[w + 3], state.imem[w + 4], state.imem[w + 5]);
				fflush(rf);
			}
#endif
			return MODE_CHECK_FLAGS;
		}
	}
}

void CPU::print_registers()
{
#define DUMP_FILE stdout
	fprintf(DUMP_FILE, "RSP state:\n");
	fprintf(DUMP_FILE, "  PC: 0x%03x\n", state.pc);
	for (unsigned i = 1; i < 32; i++)
		fprintf(DUMP_FILE, "  SR[%s] = 0x%08x\n", register_name(i), state.sr[i]);
	fprintf(DUMP_FILE, "\n");
	for (unsigned i = 0; i < 32; i++)
	{
		fprintf(DUMP_FILE, "  VR[%02u] = { 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x }\n", i,
		        state.cp2.regs[i].e[0], state.cp2.regs[i].e[1], state.cp2.regs[i].e[2], state.cp2.regs[i].e[3],
		        state.cp2.regs[i].e[4], state.cp2.regs[i].e[5], state.cp2.regs[i].e[6], state.cp2.regs[i].e[7]);
	}

	fprintf(DUMP_FILE, "\n");

	for (unsigned i = 0; i < 3; i++)
	{
		static const char *strings[] = { "ACC_HI", "ACC_MD", "ACC_LO" };
		fprintf(DUMP_FILE, "  %s = { 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x }\n", strings[i],
		        state.cp2.acc.e[8 * i + 0], state.cp2.acc.e[8 * i + 1], state.cp2.acc.e[8 * i + 2],
		        state.cp2.acc.e[8 * i + 3], state.cp2.acc.e[8 * i + 4], state.cp2.acc.e[8 * i + 5],
		        state.cp2.acc.e[8 * i + 6], state.cp2.acc.e[8 * i + 7]);
	}

	fprintf(DUMP_FILE, "\n");

	for (unsigned i = 0; i < 3; i++)
	{
		static const char *strings[] = { "VCO", "VCC", "VCE" };
		uint16_t flags = rsp_get_flags(state.cp2.flags[i].e);
		fprintf(DUMP_FILE, "  %s = 0x%04x\n", strings[i], flags);
	}

	fprintf(DUMP_FILE, "\n");
	fprintf(DUMP_FILE, "  Div Out = 0x%04x\n", state.cp2.div_out);
	fprintf(DUMP_FILE, "  Div In  = 0x%04x\n", state.cp2.div_in);
	fprintf(DUMP_FILE, "  DP flag = 0x%04x\n", state.cp2.dp_flag);
}

RegisterCache::CacheEntry *RegisterCache::find_live_mips_register(unsigned mips_reg)
{
	for (auto &entry : entries)
		if (entry.is_live && entry.mips_register == mips_reg)
			return &entry;
	return nullptr;
}

RegisterCache::CacheEntry *RegisterCache::find_free_register()
{
	for (auto &entry : entries)
		if (!entry.is_live)
			return &entry;
	return nullptr;
}

RegisterCache::CacheEntry *RegisterCache::find_oldest_unlocked_register()
{
	CacheEntry *best = nullptr;
	for (auto &entry : entries)
	{
		if (entry.is_live && !entry.num_locks)
		{
			if (!best || entry.timestamp < best->timestamp)
				best = &entry;
		}
	}
	return best;
}

RegisterCache::CacheEntry &RegisterCache::find_register(unsigned mips_reg)
{
	auto *reg = find_live_mips_register(mips_reg);
	if (!reg)
		reg = find_free_register();
	if (!reg)
		reg = find_oldest_unlocked_register();
	assert(reg);
	return *reg;
}

void RegisterCache::writeback_register(jit_state_t *_jit, CacheEntry &entry)
{
	// The scratch registers are never flushed out to memory.
	assert(entry.mips_register != 0);
	if (entry.mips_register <= COND_BRANCH_TAKEN)
		jit_stxi_i(offsetof(CPUState, sr) + 4 * entry.mips_register, JIT_REGISTER_STATE, entry_to_jit_register(entry));
	entry.modified = false;
}

unsigned RegisterCache::immediate_mips_register(jit_state_t *_jit, unsigned mips_reg, jit_word_t value)
{
	unsigned jit_reg = modify_mips_register(_jit, mips_reg);
	jit_movi(jit_reg, value);
	entries[jit_register_to_index(jit_reg)].sign = SExt;
	return jit_reg;
}

unsigned RegisterCache::load_mips_register_noext(jit_state_t *_jit, unsigned mips_reg)
{
	auto &reg = find_register(mips_reg);
	unsigned jit_reg = entry_to_jit_register(reg);
	assert(mips_reg <= COND_BRANCH_TAKEN);

	if (reg.is_live && reg.mips_register != mips_reg)
	{
		if (reg.modified)
			writeback_register(_jit, reg);
		reg.mips_register = mips_reg;

		if (mips_reg)
			jit_ldxi_i(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			jit_movi(jit_reg, 0);
		reg.modified = false;

		// We know that the input is sign-extended so future opcodes which rely on
		// sign-ness will be able to assume so.
		reg.sign = SExt;
	}
	else if (!reg.is_live)
	{
		reg.mips_register = mips_reg;

		if (mips_reg)
			jit_ldxi_i(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			jit_movi(jit_reg, 0);

		reg.sign = SExt;
		reg.is_live = true;
		reg.modified = false;
	}

	// If the register is already live and well, we just need to update the timestamp.

	reg.timestamp = ++timestamp;
	reg.num_locks++;
	return jit_reg;
}

unsigned RegisterCache::modify_mips_register(jit_state_t *_jit, unsigned mips_reg)
{
	auto &reg = find_register(mips_reg);
	unsigned jit_reg = entry_to_jit_register(reg);

	if (reg.is_live && reg.mips_register != mips_reg)
	{
		if (reg.modified)
			writeback_register(_jit, reg);
		reg.mips_register = mips_reg;
	}
	else if (!reg.is_live)
	{
		reg.mips_register = mips_reg;
		reg.is_live = true;
	}

	// If the register is already live and well, we just need to update the timestamp.

	reg.sign = Unknown;
	reg.timestamp = ++timestamp;
	reg.num_locks++;
	reg.modified = true;
	return jit_reg;
}

unsigned RegisterCache::load_mips_register_sext(jit_state_t *_jit, unsigned mips_reg)
{
	auto &reg = find_register(mips_reg);
	unsigned jit_reg = entry_to_jit_register(reg);
	assert(mips_reg <= COND_BRANCH_TAKEN);

	if (reg.is_live && reg.mips_register != mips_reg)
	{
		if (reg.modified)
			writeback_register(_jit, reg);
		reg.mips_register = mips_reg;

		if (mips_reg)
			jit_ldxi_i(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			jit_movi(jit_reg, 0);

		reg.modified = false;
		reg.sign = SExt;
	}
	else if (!reg.is_live)
	{
		reg.mips_register = mips_reg;

		if (mips_reg)
			jit_ldxi_i(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			jit_movi(jit_reg, 0);

		reg.sign = SExt;
		reg.is_live = true;
		reg.modified = false;
	}
	else if (reg.sign != SExt)
	{
#if __WORDSIZE > 32
		if (mips_reg)
		{
			// Have to sign-extend if we're not sure.
			jit_extr_i(jit_reg, jit_reg);
		}
#endif
		reg.sign = SExt;
	}

	reg.num_locks++;
	reg.timestamp = ++timestamp;
	return jit_reg;
}

unsigned RegisterCache::load_mips_register_zext(jit_state_t *_jit, unsigned mips_reg)
{
	auto &reg = find_register(mips_reg);
	unsigned jit_reg = entry_to_jit_register(reg);
	assert(mips_reg <= COND_BRANCH_TAKEN);

	if (reg.is_live && reg.mips_register != mips_reg)
	{
		if (reg.modified)
			writeback_register(_jit, reg);
		reg.mips_register = mips_reg;

		if (mips_reg)
			jit_ldxi_ui(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			jit_movi(jit_reg, 0);

		reg.modified = false;
		reg.sign = ZExt;
	}
	else if (!reg.is_live)
	{
		reg.mips_register = mips_reg;

		if (mips_reg)
			jit_ldxi_ui(jit_reg, JIT_REGISTER_STATE, offsetof(CPUState, sr) + 4 * mips_reg);
		else
			jit_movi(jit_reg, 0);

		reg.sign = ZExt;
		reg.is_live = true;
		reg.modified = false;
	}
	else if (reg.sign != ZExt)
	{
#if __WORDSIZE > 32
		if (mips_reg)
		{
			// Have to zero-extend if we're not sure.
			jit_extr_ui(jit_reg, jit_reg);
		}
#endif
		reg.sign = ZExt;
	}

	reg.num_locks++;
	reg.timestamp = ++timestamp;
	return jit_reg;
}

void RegisterCache::unlock_mips_register(unsigned mips_reg)
{
	auto *live_reg = find_live_mips_register(mips_reg);
	assert(live_reg);
	assert(live_reg->num_locks > 0);
	live_reg->num_locks--;
}

void RegisterCache::flush_register_window(jit_state_t *_jit)
{
	for (auto &entry : entries)
	{
		if (entry.is_live)
		{
			if (entry.modified)
				writeback_register(_jit, entry);
			assert(!entry.num_locks);
			entry = {};
		}
	}
	timestamp = 0;
}

void RegisterCache::flush_caller_save_registers(jit_state_t *_jit)
{
	for (unsigned i = 0; i < JIT_R_NUM; i++)
	{
		auto &entry = entries[jit_register_to_index(JIT_R(i))];
		if (entry.is_live)
		{
			if (entry.modified)
				writeback_register(_jit, entry);
			assert(!entry.num_locks);
			entry = {};
		}
	}
}

void RegisterCache::reset()
{
	for (auto &entry : entries)
		entry = {};
}

void RegisterCache::flush_mips_register(jit_state_t *_jit, unsigned mips_reg)
{
	auto *live_reg = find_live_mips_register(mips_reg);
	if (live_reg)
	{
		if (live_reg->modified)
			writeback_register(_jit, *live_reg);
		assert(!live_reg->num_locks);
		live_reg->is_live = false;
		*live_reg = {};
	}
}

unsigned RegisterCache::jit_register_to_index(unsigned jit_reg)
{
	if (jit_reg >= JIT_R0 && jit_reg < JIT_R(JIT_R_NUM))
		return jit_reg - JIT_R0;
	else
		return JIT_R_NUM + (jit_reg - JIT_V(3));
}

unsigned RegisterCache::entry_to_jit_register(const CacheEntry &entry)
{
	auto index = unsigned(&entry - entries);
	if (index < JIT_R_NUM)
		return JIT_R(index);
	else
		return JIT_V(3 + (index - JIT_R_NUM));
}

} // namespace JIT
} // namespace RSP
