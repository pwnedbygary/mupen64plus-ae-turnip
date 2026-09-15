#ifndef DD_WATCH_H
#define DD_WATCH_H

#include <stdint.h>

/*
 * P08a — watched-range observation infrastructure for the N64DD producer
 * investigation (work packages P08, subdivision P08a: a dynamic watched
 * range, a generation model and a bounded ring, exercised with synthetic
 * events and no generated-code changes).
 *
 * This file OBSERVES.  It adds no writer hook, no generated-code change and
 * no production call site; P08b adds exactly one demonstrated writer family
 * and the call sites that fill these records.  Landing the infrastructure
 * first keeps the writer-specific register/ABI work separately reviewable.
 *
 * Hardware behavior: none is changed.  No emulator-visible value, timing or
 * ordering is altered — records carry the writer's own values unchanged and
 * every counter is bookkeeping.  The five-point hardware-evidence checklist
 * (pinned wiki revision, P01 DMA ledger, source/class/disagreement/
 * validation statement) therefore does not apply to this subdivision; P08b
 * is the first package that touches an emulation path and must carry it.
 *
 * Invariants a future call site cannot weaken:
 *  - Arming requires the explicit per-game DD runtime policy
 *    (DdRuntimePolicyGet(), P03): a DD-disabled session can never arm.  The
 *    policy is consulted at ARM TIME ONLY — a session armed while the policy
 *    was enabled keeps recording until it is disarmed, so the host must
 *    disarm at or before ROM close (P08b's wiring), and DD_WATCH_ERR_OFF
 *    means "not armed", never "the policy is off".
 *  - Storage is static and bounded — no allocation, no I/O, no locking.
 *    arm/record/get are emulation-thread only; registration-at-lifecycle
 *    and cross-thread reads would need their own review.
 *  - The ring retains the OLDEST stored events (fill-then-drop): for "find
 *    the first wrong write", the first transition after arming is the
 *    evidence, so later events are counted as dropped rather than
 *    overwriting it.  Drop and rejection counters are the coverage report;
 *    they are never reset by anything except a new arm.  This is a deliberate
 *    divergence from the work-packages wording "fixed recent-event ring"
 *    (P08 step 7): oldest-retention preserves the first transition, at the
 *    cost that after 256 stored in-range events every later one is dropped
 *    and only the counter survives — so arm narrowly, and plan to re-arm per
 *    generation when more coverage is needed.
 *  - Recording stamps the module's own generation, so a caller cannot label
 *    an event with a generation the model has not reached.
 *  - The armed base is stored normalized, so a caller may pass either the
 *    KSEG0/KSEG1 mirror or the physical address.
 *
 * Address semantics and their limits (reference class, not measured here):
 * the KSEG0/KSEG1 segment bits are stripped with the 0x1FFFFFFF mask, which
 * follows the core's own address handling (r4300_core.c, api/debugger.c) and
 * differs from the sibling device/r4300/dd_fault_layout.h helper, which
 * REJECTS a segment that is not exactly KSEG0/KSEG1.  A KUSEG/TLB-mapped
 * virtual address is NOT translated: 0x003DA9F0 and the physical 0x3DA9F0
 * are indistinguishable, so a recording site must pass the address its writer
 * path already resolved, and analysis must not read a stored `phys` as proof
 * that the writer used the physical address.
 */

#define DD_WATCH_RING_CAPACITY 256u

/* Writer family a recording site tags itself with (P08b fills this in). */
enum dd_watch_writer
{
	DD_WATCH_WRITER_UNKNOWN = 0,
	DD_WATCH_WRITER_CPU_FAST = 1,
	DD_WATCH_WRITER_CPU_SLOW = 2,
	DD_WATCH_WRITER_CORE_DMA = 3,
	DD_WATCH_WRITER_RSP_DMA = 4,
	DD_WATCH_WRITER_CORE_COPY = 5
};

/* dd_watch_record() results. */
enum
{
	DD_WATCH_STORED = 1,
	/* Not armed (see the arm-time policy gate above). */
	DD_WATCH_ERR_OFF = -1,
	/* Address outside the armed range (rejected, then counted); also the
	 * classification for a NULL record. */
	DD_WATCH_ERR_RANGE = -2,
	/* Bounded ring full: counted as dropped, not stored. */
	DD_WATCH_ERR_FULL = -3
};

struct dd_watch_event
{
	uint32_t generation; /* generation the module held when stored */
	uint32_t phys;       /* normalized address the writer used */
	uint32_t value;      /* stored value; low `width` bytes significant */
	uint32_t before;     /* bytes at phys before the write, same low bits */
	uint32_t pc;         /* exact emitting PC when available, else 0 */
	uint16_t sequence;   /* accepted-event index within the armed session */
	uint8_t width;       /* 1, 2 or 4 bytes; 0 = unknown or whole-block */
	uint8_t writer;      /* enum dd_watch_writer */
};

/*
 * KSEG0/KSEG1 mirror normalization: 0x806EEAA0 and 0xA06EEAA0 both
 * normalize to the physical 0x6EEAA0.  Addresses already in physical space
 * are returned unchanged, and no RDRAM mask is applied here, because the
 * mask belongs to the writer path that reported the address (the P01 ledger
 * records that the RSP and core paths differ there).
 */
uint32_t dd_watch_normalize(uint32_t addr);

/*
 * Arm one observation session over the physical range [base, base + length).
 * Returns 1 when armed, 0 when the per-game DD policy is off, the length is
 * zero, or the range is empty or wraps the 32-bit space.  Arming clears the
 * ring, the counters and the generation, so one session's records are never
 * mixed with a previous one.  The range is half-open and exact; note that the
 * documented zero run starts one byte before the word-aligned base P07 used
 * (0x3DA9EF), and that byte is therefore unwatched when arming at 0x3DA9F0.
 */
int dd_watch_arm(uint32_t base, uint32_t length);

/*
 * Stop accepting records.  Retained evidence and counters are preserved —
 * analysis needs the records after the interesting transition — and arming
 * again is what starts a new session.
 */
void dd_watch_disarm(void);

int dd_watch_armed(void);

/*
 * Generation model: the producer sets the generation when it publishes a new
 * task or buffer (0 = unset).  Stored events are stamped with this value.
 */
void dd_watch_set_generation(uint32_t generation);
uint32_t dd_watch_generation(void);

/* Range test a recording site uses before building a record. */
int dd_watch_in_range(uint32_t addr);

/*
 * Store one record.  Returns DD_WATCH_STORED or a DD_WATCH_ERR_* value.
 * Every call is accounted for exactly once (stored, dropped or rejected),
 * which is the identity a coverage report can rely on.
 */
int dd_watch_record(const struct dd_watch_event *event);

/* Stored events currently retained (<= DD_WATCH_RING_CAPACITY). */
unsigned dd_watch_count(void);
/* Events dropped because the bounded ring was full. */
unsigned dd_watch_dropped(void);
/* Calls rejected because the watch was off or the address was outside. */
unsigned dd_watch_rejected(void);
/* Events STORED since arming: always equal to dd_watch_count(), because the
 * full-ring path returns before this counter is touched and drops are counted
 * separately.  It is not "calls seen" — that is total + dropped + rejected. */
unsigned dd_watch_total(void);
/* Bounded retrieval: index < dd_watch_count(); returns 1 on success and
 * writes *out only then (a rejected index leaves *out untouched). */
int dd_watch_get(unsigned index, struct dd_watch_event *out);
uint32_t dd_watch_range_base(void);
uint32_t dd_watch_range_length(void);

#endif /* DD_WATCH_H */
