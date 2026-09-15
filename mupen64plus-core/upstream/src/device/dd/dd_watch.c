#include "device/dd/dd_watch.h"

#include <stddef.h>

#include "api/callbacks.h"

/* KSEG0 (0x80000000) and KSEG1 (0xA0000000) mirror the low 29 bits. */
#define DD_WATCH_KSEG_MASK 0x1FFFFFFFu

static struct dd_watch_state
{
	int armed;
	uint32_t base;
	uint32_t length;
	uint32_t generation;
	unsigned count;
	unsigned total;
	unsigned dropped;
	unsigned rejected;
	uint16_t next_sequence;
	struct dd_watch_event ring[DD_WATCH_RING_CAPACITY];
} watch;

uint32_t dd_watch_normalize(uint32_t addr)
{
	return addr & DD_WATCH_KSEG_MASK;
}

int dd_watch_arm(uint32_t base, uint32_t length)
{
	uint32_t normalized = dd_watch_normalize(base);
	uint64_t end = (uint64_t) normalized + (uint64_t) length;

	if (!DdRuntimePolicyGet())
		return 0;
	if (length == 0 || end > 0x100000000ull)
		return 0;

	watch.armed = 1;
	watch.base = normalized;
	watch.length = length;
	watch.generation = 0;
	watch.count = 0;
	watch.total = 0;
	watch.dropped = 0;
	watch.rejected = 0;
	watch.next_sequence = 0;
	return 1;
}

void dd_watch_disarm(void)
{
	watch.armed = 0;
}

int dd_watch_armed(void)
{
	return watch.armed;
}

void dd_watch_set_generation(uint32_t generation)
{
	watch.generation = generation;
}

uint32_t dd_watch_generation(void)
{
	return watch.generation;
}

int dd_watch_in_range(uint32_t addr)
{
	if (!watch.armed)
		return 0;
	return (uint32_t) (dd_watch_normalize(addr) - watch.base) < watch.length;
}

int dd_watch_record(const struct dd_watch_event *event)
{
	struct dd_watch_event stored;

	if (!watch.armed) {
		watch.rejected++;
		return DD_WATCH_ERR_OFF;
	}
	if (event == NULL || !dd_watch_in_range(event->phys)) {
		watch.rejected++;
		return DD_WATCH_ERR_RANGE;
	}
	if (watch.count >= DD_WATCH_RING_CAPACITY) {
		watch.dropped++;
		return DD_WATCH_ERR_FULL;
	}

	stored = *event;
	stored.generation = watch.generation;
	stored.phys = dd_watch_normalize(event->phys);
	stored.sequence = watch.next_sequence++;
	watch.ring[watch.count] = stored;
	watch.count++;
	watch.total++;
	return DD_WATCH_STORED;
}

unsigned dd_watch_count(void)
{
	return watch.count;
}

unsigned dd_watch_dropped(void)
{
	return watch.dropped;
}

unsigned dd_watch_rejected(void)
{
	return watch.rejected;
}

unsigned dd_watch_total(void)
{
	return watch.total;
}

int dd_watch_get(unsigned index, struct dd_watch_event *out)
{
	if (out == NULL || index >= watch.count)
		return 0;
	*out = watch.ring[index];
	return 1;
}

uint32_t dd_watch_range_base(void)
{
	return watch.base;
}

uint32_t dd_watch_range_length(void)
{
	return watch.length;
}

/*
 * P08c writer watch: production adapters.  See dd_watch.h for the contract
 * and for the fast-path coverage limit.
 */
int dd_watch_arm_from_task_words(const uint32_t *task_words)
{
	if (task_words == NULL || task_words[0] != 2u)
	{
		/* Not an audio task: nothing to watch, and any previously armed
		 * range belongs to a task that is no longer current. */
		dd_watch_disarm();
		return 0;
	}

	/* Corrected P07-C field map: words 12-13 = data_ptr / data_size.
	 * dd_watch_arm() applies the per-game DD policy gate itself and
	 * normalizes the base. */
	if (dd_watch_arm(task_words[12], task_words[13]))
		return 1;

	dd_watch_disarm();
	return 0;
}

int dd_watch_record_store(uint32_t addr, uint32_t width, uint32_t value,
                          uint32_t before, uint32_t pc, uint8_t writer)
{
	struct dd_watch_event event;

	/* Hot-path order: armed first, then the range, so an untargeted store
	 * costs a load and a compare and never builds a record. */
	if (!dd_watch_armed() || !dd_watch_in_range(addr))
		return 0;

	event.generation = 0; /* stamped by dd_watch_record() */
	event.phys = addr;
	event.value = value;
	event.before = before;
	event.pc = pc;
	event.sequence = 0;
	event.width = (uint8_t) width;
	event.writer = writer;
	return dd_watch_record(&event);
}
