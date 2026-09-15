#include "device/dd/dd_watch.h"

#include <assert.h>
#include <string.h>

#include "api/callbacks.h"

/*
 * P08a: synthetic-event tests for the watched-range observation
 * infrastructure (device/dd/dd_watch.c).  No writer is wired yet, so every
 * event here is fabricated; the tests assert the module's own contract —
 * the policy gate, range normalization, generation stamping, the bounded
 * ring with its counters, and session isolation.
 *
 * The range values come from the P07 evidence (docs/P07_COMMAND_EVIDENCE.md
 * section 8/9), not from this module: the bytewise zero run is
 * [0x3DA9EF, 0x6ECA10).  The aligned range armed here is
 * [0x3DA9F0, 0x6ECA10) = 0x312020 bytes, which contains both command
 * buffers (0x411910, 0x4132D0) and ends exactly at gAudioCtx (0x6ECA10,
 * excluded).  The core policy state is the real one (api/callbacks.c), not a
 * stub, so the gate is exercised through the production path.
 */
#define RANGE_BASE 0x3DA9F0u
#define RANGE_LENGTH 0x312020u
#define G_AUDIO_CTX 0x6ECA10u

static struct dd_watch_event make_event(uint32_t phys, uint32_t value)
{
	struct dd_watch_event event;

	memset(&event, 0, sizeof(event));
	event.phys = phys;
	event.value = value;
	event.before = value ^ 0xFFFFFFFFu;
	event.pc = 0x80012345u;
	event.width = 4;
	event.writer = DD_WATCH_WRITER_CPU_FAST;
	event.generation = 0xDEADBEEFu; /* must be replaced by the module */
	return event;
}

int main(void)
{
	struct dd_watch_event event;
	struct dd_watch_event out;
	unsigned calls = 0;
	unsigned i;

	/* A DD-disabled session cannot arm, and every call is accounted as
	 * rejected.  Nothing is stored and no range is armed. */
	assert(SetDdRuntimePolicy(0) == M64ERR_SUCCESS);
	assert(DdRuntimePolicyGet() == 0);
	assert(dd_watch_armed() == 0);
	assert(dd_watch_arm(RANGE_BASE, RANGE_LENGTH) == 0);
	assert(dd_watch_armed() == 0);
	assert(dd_watch_in_range(0x411910u) == 0);
	event = make_event(0x411910u, 0x11111111u);
	assert(dd_watch_record(&event) == DD_WATCH_ERR_OFF);
	assert(dd_watch_rejected() == 1u);
	assert(dd_watch_count() == 0u);
	assert(dd_watch_total() == 0u);

	/* The explicit per-game DD policy is the only way in. */
	assert(SetDdRuntimePolicy(1) == M64ERR_SUCCESS);
	assert(dd_watch_arm(RANGE_BASE, RANGE_LENGTH) == 1);
	assert(dd_watch_armed() == 1);
	/* The earlier rejected call belonged to the previous session. */
	assert(dd_watch_rejected() == 0u);
	assert(dd_watch_range_base() == RANGE_BASE);
	assert(dd_watch_range_length() == RANGE_LENGTH);
	assert(dd_watch_generation() == 0u);

	/* KSEG0/KSEG1 mirrors normalize to physical addresses; physical
	 * addresses are unchanged. */
	assert(dd_watch_normalize(0x806EEAA0u) == 0x6EEAA0u);
	assert(dd_watch_normalize(0xA06EEAA0u) == 0x6EEAA0u);
	assert(dd_watch_normalize(0x411910u) == 0x411910u);
	assert(dd_watch_normalize(0x80411910u) == 0x411910u);

	/* Range membership, including both ends and the KSEG0 mirror. */
	assert(dd_watch_in_range(RANGE_BASE) == 1);
	assert(dd_watch_in_range(RANGE_BASE + RANGE_LENGTH - 4u) == 1);
	assert(dd_watch_in_range(RANGE_BASE - 1u) == 0);
	assert(dd_watch_in_range(G_AUDIO_CTX) == 0);
	assert(dd_watch_in_range(0x411910u) == 1); /* slot 0 command buffer */
	assert(dd_watch_in_range(0x4132D0u) == 1); /* slot 1 command buffer */
	assert(dd_watch_in_range(0x80411910u) == 1); /* KSEG0 mirror */

	/* A second arm with a KSEG0 base normalizes it, and re-arming clears
	 * the previous session. */
	event = make_event(0x411910u, 0x22222222u);
	assert(dd_watch_record(&event) == DD_WATCH_STORED);
	assert(dd_watch_count() == 1u);
	assert(dd_watch_arm(0x803DA9F0u, RANGE_LENGTH) == 1);
	assert(dd_watch_range_base() == RANGE_BASE);
	assert(dd_watch_count() == 0u);
	assert(dd_watch_total() == 0u);
	assert(dd_watch_rejected() == 0u);
	assert(dd_watch_dropped() == 0u);

	/* Generation model: the record's own generation field is replaced by
	 * the module's, and the caller's struct is left untouched. */
	dd_watch_set_generation(7u);
	event = make_event(0x411910u, 0x33333333u);
	assert(event.generation == 0xDEADBEEFu);
	assert(dd_watch_record(&event) == DD_WATCH_STORED);
	assert(event.generation == 0xDEADBEEFu);
	assert(dd_watch_get(0u, &out) == 1);
	assert(out.generation == 7u);
	assert(out.phys == 0x411910u);
	assert(out.value == 0x33333333u);
	assert(out.before == (0x33333333u ^ 0xFFFFFFFFu));
	assert(out.pc == 0x80012345u);
	assert(out.width == 4u);
	assert(out.writer == DD_WATCH_WRITER_CPU_FAST);
	assert(out.sequence == 0u);

	/* Retrieval is bounded, and a rejected index leaves the caller's
	 * struct untouched. */
	assert(dd_watch_get(1u, &out) == 0);
	assert(dd_watch_get(0u, NULL) == 0);
	out.value = 0xAAAAAAAAu;
	assert(dd_watch_get(9999u, &out) == 0);
	assert(out.value == 0xAAAAAAAAu);

	/* The bounded ring retains the OLDEST events.  Re-arming starts a
	 * clean session, so the arithmetic is exact: 300 in-range writes fill
	 * 256 entries, count 44 drops, and the first event survives. */
	assert(dd_watch_arm(RANGE_BASE, RANGE_LENGTH) == 1);
	dd_watch_set_generation(8u);
	calls = 0;
	for (i = 0; i < DD_WATCH_RING_CAPACITY + 44u; i++) {
		event = make_event(RANGE_BASE + (i % 100u) * 4u, i);
		assert(dd_watch_record(&event) ==
		       (i < DD_WATCH_RING_CAPACITY ? DD_WATCH_STORED
						   : DD_WATCH_ERR_FULL));
		calls++;
	}
	assert(dd_watch_count() == DD_WATCH_RING_CAPACITY);
	assert(dd_watch_total() == DD_WATCH_RING_CAPACITY);
	assert(dd_watch_dropped() == 44u);
	assert(dd_watch_rejected() == 0u);
	assert(dd_watch_get(0u, &out) == 1);
	assert(out.value == 0u); /* the first event of the fill loop */
	assert(out.generation == 8u);
	assert(out.sequence == 0u);
	assert(dd_watch_get(DD_WATCH_RING_CAPACITY - 1u, &out) == 1);
	assert(out.value == DD_WATCH_RING_CAPACITY - 1u);
	assert(out.sequence == DD_WATCH_RING_CAPACITY - 1u);
	assert(dd_watch_get(DD_WATCH_RING_CAPACITY, &out) == 0);

	/* Out-of-range and absent records are rejected and counted, never
	 * stored — rejection takes precedence over the full ring. */
	event = make_event(G_AUDIO_CTX, 0x44444444u);
	assert(dd_watch_record(&event) == DD_WATCH_ERR_RANGE);
	calls++;
	event = make_event(RANGE_BASE - 1u, 0x55555555u);
	assert(dd_watch_record(&event) == DD_WATCH_ERR_RANGE);
	calls++;
	assert(dd_watch_record(NULL) == DD_WATCH_ERR_RANGE);
	calls++;
	assert(dd_watch_rejected() == 3u);
	assert(dd_watch_count() == DD_WATCH_RING_CAPACITY);

	/* Every call in the armed session is accounted for exactly once. */
	assert(dd_watch_total() + dd_watch_dropped() + dd_watch_rejected() == calls);

	/* Disarming preserves evidence; the range test reports nothing while
	 * disarmed (the pre-filter a recording site relies on); re-arming starts
	 * a clean session. */
	dd_watch_disarm();
	assert(dd_watch_armed() == 0);
	assert(dd_watch_in_range(0x411910u) == 0);
	assert(dd_watch_count() == DD_WATCH_RING_CAPACITY);
	assert(dd_watch_total() == DD_WATCH_RING_CAPACITY);
	event = make_event(0x411910u, 0x66666666u);
	assert(dd_watch_record(&event) == DD_WATCH_ERR_OFF);
	assert(dd_watch_rejected() == 4u);
	assert(dd_watch_arm(RANGE_BASE, RANGE_LENGTH) == 1);
	assert(dd_watch_count() == 0u);
	assert(dd_watch_total() == 0u);
	assert(dd_watch_dropped() == 0u);
	assert(dd_watch_rejected() == 0u);
	assert(dd_watch_generation() == 0u);

	/* Stored addresses are normalized rather than passed through, so a
	 * recording site may hand over either the mirror or the physical form,
	 * and the caller's own struct is never modified. */
	event = make_event(0x80411910u, 0x88888888u);
	assert(dd_watch_record(&event) == DD_WATCH_STORED);
	assert(dd_watch_get(0u, &out) == 1);
	assert(out.phys == 0x411910u);
	assert(event.phys == 0x80411910u);
	assert(out.value == 0x88888888u);

	/* Degenerate arms are refused and leave the armed session intact.  The
	 * wrap guard applies to the NORMALIZED range: 0x9FFFFFF0 is a KSEG0
	 * mirror of physical 0x1FFFFFF0, so the overflowing length below is
	 * what crosses 32 bits, not the mirror itself. */
	assert(dd_watch_arm(RANGE_BASE, 0u) == 0);
	assert(dd_watch_arm(0x9FFFFFF0u, 0xE0001000u) == 0);
	assert(dd_watch_armed() == 1);
	assert(dd_watch_range_base() == RANGE_BASE);
	assert(dd_watch_range_length() == RANGE_LENGTH);

	/* The policy is a launch-time gate (P03): it is consulted when arming,
	 * and the frontend clears it at ROM close, where the host is expected
	 * to disarm (P08b's wiring).  A later policy change does not rewrite an
	 * already-armed session's behavior. */
	assert(SetDdRuntimePolicy(0) == M64ERR_SUCCESS);
	event = make_event(0x411910u, 0x77777777u);
	assert(dd_watch_record(&event) == DD_WATCH_STORED);
	dd_watch_disarm();
	assert(dd_watch_arm(RANGE_BASE, RANGE_LENGTH) == 0);
	assert(dd_watch_armed() == 0);

	/* Clearing the policy is the documented end of the session. */
	assert(DdRuntimePolicyGet() == 0);

	return 0;
}
