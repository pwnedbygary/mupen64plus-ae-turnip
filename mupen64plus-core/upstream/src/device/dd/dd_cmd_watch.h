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
#define DD_CMD_WATCH_CONTEXT_BUDGET 16u

/*
 * The route stub passes the delay-slot bit and a validity mask in one
 * flags word.  A value is retained only when all five selected architectural
 * registers were materialized coherently by the ARM64 allocator.
 */
#define DD_CMD_WATCH_CONTEXT_DELAY_SLOT UINT32_C(1)
#define DD_CMD_WATCH_CONTEXT_VALID_RA (UINT32_C(1) << 1)
#define DD_CMD_WATCH_CONTEXT_VALID_SP (UINT32_C(1) << 2)
#define DD_CMD_WATCH_CONTEXT_VALID_A0 (UINT32_C(1) << 3)
#define DD_CMD_WATCH_CONTEXT_VALID_A1 (UINT32_C(1) << 4)
#define DD_CMD_WATCH_CONTEXT_VALID_A3 (UINT32_C(1) << 5)
#define DD_CMD_WATCH_CONTEXT_VALID_ALL \
    (DD_CMD_WATCH_CONTEXT_VALID_RA \
     | DD_CMD_WATCH_CONTEXT_VALID_SP \
     | DD_CMD_WATCH_CONTEXT_VALID_A0 \
     | DD_CMD_WATCH_CONTEXT_VALID_A1 \
     | DD_CMD_WATCH_CONTEXT_VALID_A3)

/*
 * This bit is ORed only into the dedicated generated watch write-stub type.
 * It is deliberately outside the normal stub type range and is consumed
 * before the ordinary STORE{B,H,W,D} switch, so every routed store keeps its
 * original width without tagging an ordinary fallback stub.
 */
#define DD_CMD_WATCH_CONTEXT_STUB UINT32_C(0x80)

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
 * Capture the live values at the generated store site.  The helper only
 * arms a same-thread pending record; dd_cmd_watch_record_aligned() consumes
 * it after the existing writer has completed successfully.  The five
 * register arguments are store-time values, not reconstructed entry
 * arguments, and are never read from g_dev.r4300.regs.
 */
void dd_cmd_watch_capture_context(uint32_t address,
                                  uint32_t store_pc,
                                  uint32_t flags,
                                  uint64_t ra,
                                  uint64_t sp,
                                  uint64_t a0,
                                  uint64_t a1,
                                  uint64_t a3);

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