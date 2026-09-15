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
#define DD_CMD_WATCH_PROBE_BUDGET 8u
#define DD_CMD_WATCH_COMPILE_REJECTION_BUDGET 8u

/*
 * P08e targets one compiled ARM64 caller/callee pair.  These constants are
 * evidence filters, not a guest branch selector.  A probe is emitted only
 * for the exact compiled JAL and its delay word, and the recorder checks them
 * again before accepting a record.
 */
#define DD_CMD_WATCH_TARGET_CALL_PC UINT32_C(0x800aea0c)
#define DD_CMD_WATCH_TARGET_CALL_OPCODE UINT32_C(0x0c1d1c90)
#define DD_CMD_WATCH_TARGET_DELAY_OPCODE UINT32_C(0x02002825)
#define DD_CMD_WATCH_TARGET_ENTRY_PC UINT32_C(0x80747240)
#define DD_CMD_WATCH_TARGET_RETURN_PC UINT32_C(0x800aea14)

enum dd_cmd_watch_probe_kind
{
    DD_CMD_WATCH_PROBE_CALL = 1u,
    DD_CMD_WATCH_PROBE_ENTRY = 2u
};

enum dd_cmd_watch_probe_flags
{
    DD_CMD_WATCH_PROBE_AFTER_DELAY = 1u << 0,
    DD_CMD_WATCH_PROBE_VALID_A0 = 1u << 1,
    DD_CMD_WATCH_PROBE_VALID_A1 = 1u << 2,
    DD_CMD_WATCH_PROBE_VALID_T8 = 1u << 3,
    DD_CMD_WATCH_PROBE_VALID_T1 = 1u << 4,
    DD_CMD_WATCH_PROBE_VALID_RA = 1u << 5,
    DD_CMD_WATCH_PROBE_VALID_SP = 1u << 6
};

#define DD_CMD_WATCH_PROBE_CALL_VALID \
    (DD_CMD_WATCH_PROBE_VALID_A0 \
     | DD_CMD_WATCH_PROBE_VALID_A1 \
     | DD_CMD_WATCH_PROBE_VALID_T8 \
     | DD_CMD_WATCH_PROBE_VALID_T1 \
     | DD_CMD_WATCH_PROBE_VALID_RA \
     | DD_CMD_WATCH_PROBE_VALID_SP)

#define DD_CMD_WATCH_PROBE_REGISTER_MASK \
    DD_CMD_WATCH_PROBE_CALL_VALID

#define DD_CMD_WATCH_PROBE_CALL_REQUIRED \
    (DD_CMD_WATCH_PROBE_VALID_A0 \
     | DD_CMD_WATCH_PROBE_VALID_A1 \
     | DD_CMD_WATCH_PROBE_VALID_RA \
     | DD_CMD_WATCH_PROBE_VALID_SP)

#define DD_CMD_WATCH_PROBE_ENTRY_VALID \
    (DD_CMD_WATCH_PROBE_VALID_A0 \
     | DD_CMD_WATCH_PROBE_VALID_A1 \
     | DD_CMD_WATCH_PROBE_VALID_RA \
     | DD_CMD_WATCH_PROBE_VALID_SP)

enum dd_cmd_watch_probe_compile_reason
{
    DD_CMD_WATCH_PROBE_COMPILE_MISSING_REGISTER = 1u,
    DD_CMD_WATCH_PROBE_COMPILE_PAGE_SPAN = 2u,
    DD_CMD_WATCH_PROBE_COMPILE_PROVENANCE = 3u,
    DD_CMD_WATCH_PROBE_COMPILE_OPCODE_GATE = 4u
};

/*
 * This is written only to the diagnostic hot-state slot immediately before
 * the observational C call.  It is deliberately a compiled-code record:
 * the recorder never reconstructs its opcode/target fields from current
 * RDRAM.
 */
struct dd_cmd_watch_probe_snapshot
{
    uint32_t call_pc;
    uint32_t call_opcode;
    uint32_t delay_opcode;
    uint32_t target;
    uint32_t entry_pc;
    uint32_t entry_opcode;
    uint32_t kind;
    uint32_t flags;
    uint32_t generation;
    uint32_t reserved;
    uint64_t a0;
    uint64_t a1;
    uint64_t t8;
    uint64_t t1;
    uint64_t ra;
    uint64_t sp;
};

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
 * RDRAM or changes task execution.  It retains only a bounded raw descriptor
 * hash/prefix/suffix history and emits that history at a zero-audio boundary.
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
 * Consume a materialized compiled-call/entry snapshot.  For a call probe,
 * `source_header_valid` and `source_header_value` are produced by a wrapper
 * that has first checked t8 through dd_fault_guest_read_u32().  Invalid or
 * non-KSEG source addresses remain explicit unavailable evidence; no guest
 * memory handler is used.
 */
void dd_cmd_watch_capture_probe(
    const struct dd_cmd_watch_probe_snapshot *snapshot,
    int source_header_valid,
    uint32_t source_header_value);

/*
 * Extended call evidence supplied by the ARM64 wrapper after each direct
 * RDRAM read has passed dd_fault_guest_read_u32().  `header_sample` has
 * sixteen raw words (64 bytes), and `stack_sample` has four raw words in
 * the fixed 0x38/0x3c/0x4c/0x58 order.  Samples are never decoded as a
 * header, resource, or stack object.
 */
void dd_cmd_watch_capture_probe_samples(
    const struct dd_cmd_watch_probe_snapshot *snapshot,
    int source_header_valid,
    uint32_t source_header_value,
    const uint32_t *header_sample,
    uint32_t header_sample_valid_mask,
    const uint32_t *stack_sample,
    uint32_t stack_sample_valid_mask);

/*
 * A compile-time probe rejection is host-side bounded telemetry.  It does
 * not emit guest code and is intentionally separate from the runtime record
 * budget, so a missing optional mapping cannot hide a later executed call.
 */
void dd_cmd_watch_note_compile_rejection(
    uint32_t kind, uint32_t reason, uint32_t valid_mask);

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