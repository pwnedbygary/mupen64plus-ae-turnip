#ifndef DD_CMD_WATCH_H
#define DD_CMD_WATCH_H

#include <stdint.h>

/*
 * The command-buffer writer probe is intentionally separate from dd_watch.
 * dd_watch is a general oldest-retaining observation primitive; this probe
 * keeps only the recent writes for each of the two buffers that can alternate
 * between audio tasks.
 */
#define DD_CMD_WATCH_BUFFER_COUNT 2u
#define DD_CMD_WATCH_ALIAS_COUNT 6u /* two KSEG aliases for two buffers */
#define DD_CMD_WATCH_RECENT_CAPACITY 32u
#define DD_CMD_WATCH_TRACE_BUDGET 128u

enum dd_cmd_watch_coverage
{
    DD_CMD_WATCH_COVERAGE_CPU_FAST_ALIGNED = 1u << 0,
    DD_CMD_WATCH_COVERAGE_CPU_SLOW_ALIGNED = 1u << 1,
    DD_CMD_WATCH_GAP_CPU_FAST_UNALIGNED = 1u << 2,
    DD_CMD_WATCH_GAP_CPU_SLOW_UNALIGNED = 1u << 3,
    DD_CMD_WATCH_GAP_DMA = 1u << 4,
    DD_CMD_WATCH_GAP_TLB = 1u << 5
};

/*
 * This state is read by generated ARM64 route checks through a C helper.
 * Keeping it public also makes the dynamic-range contract explicit; generated
 * code never embeds the address or size from one particular task.
 */
struct dd_cmd_watch_route_state
{
    uint32_t enabled;
    uint32_t base[DD_CMD_WATCH_ALIAS_COUNT];
    uint32_t end[DD_CMD_WATCH_ALIAS_COUNT];
};

extern volatile struct dd_cmd_watch_route_state dd_cmd_watch_routes;

struct dd_cmd_watch_event
{
    uint32_t buffer_base;
    uint32_t buffer_size;
    uint32_t generation;
    uint32_t address;
    uint64_t before;
    uint64_t after;
    uint32_t pc;
    uint16_t sequence;
    uint8_t width;
    uint8_t delay_slot;
};

void dd_cmd_watch_reset(void);

/*
 * Called for a valid audio task at the launch boundary.  `nonzero_words` is
 * the already-computed command-buffer summary, so this function never walks
 * RDRAM or changes task execution.
 */
void dd_cmd_watch_task_entry(const uint32_t *task_words,
                             uint32_t generation,
                             int buffer_valid,
                             uint32_t nonzero_words);

/*
 * Used by both the generated route check and the slow writer adapter.  The
 * policy and diagnostics gates are checked here, not only at code generation
 * time, so old compiled blocks fail closed after DD policy is disabled.
 */
int dd_cmd_watch_route_should_slow(uint32_t address, uint32_t width);
int dd_cmd_watch_route_consume(void);
int dd_cmd_watch_in_range(uint32_t address, uint32_t width);

/*
 * Append one successful aligned write.  The caller supplies values read
 * before and after the existing memory helper; this module never changes the
 * write or its exception behavior.
 */
void dd_cmd_watch_record_aligned(uint32_t address,
                                 uint32_t width,
                                 uint64_t before,
                                 uint64_t after,
                                 uint32_t pcaddr);

unsigned int dd_cmd_watch_coverage_mask(void);

#endif /* DD_CMD_WATCH_H */