#include "device/dd/dd_cmd_watch.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "device/dd/dd_load_history.h"

#define DD_CMD_WATCH_KSEG_MASK UINT32_C(0x1fffffff)
#define DD_CMD_WATCH_KSEG0 UINT32_C(0x80000000)
#define DD_CMD_WATCH_KSEG1 UINT32_C(0xa0000000)
#define DD_CMD_WATCH_ROUTE_SLACK 7u
#define DD_CMD_WATCH_LAUNCH_HISTORY_CAPACITY 4u
#define DD_CMD_WATCH_LAUNCH_HISTORY_BUDGET 16u
#define DD_CMD_WATCH_LAUNCH_SAMPLE_WORDS 4u

struct dd_cmd_watch_launch_snapshot
{
    uint32_t generation;
    uint64_t descriptor_hash;
    uint32_t data_ptr;
    uint32_t data_size;
    uint32_t prefix[DD_CMD_WATCH_LAUNCH_SAMPLE_WORDS];
    uint32_t suffix[DD_CMD_WATCH_LAUNCH_SAMPLE_WORDS];
    uint32_t buffer_valid;
    uint32_t nonzero_words;
};

struct dd_cmd_watch_slot
{
    int armed;
    uint32_t base;
    uint32_t length;
    uint32_t generation;
    uint16_t next_sequence;
    unsigned int head;
    unsigned int count;
    unsigned int dropped;
    unsigned int replaced;
    unsigned int replacements;
    struct dd_cmd_watch_event recent[DD_CMD_WATCH_RECENT_CAPACITY];
    struct dd_cmd_watch_context_record
    {
        int valid;
        uint32_t buffer_base;
        uint32_t buffer_size;
        uint32_t generation;
        uint32_t address;
        uint32_t store_pc;
        uint32_t flags;
        uint8_t width;
        uint64_t before;
        uint64_t after;
        uint64_t ra;
        uint64_t sp;
        uint64_t a0;
        uint64_t a1;
        uint64_t a3;
    } first_context, latest_context;
};

static struct dd_cmd_watch_launch_snapshot launch_history[
    DD_CMD_WATCH_LAUNCH_HISTORY_CAPACITY];
static unsigned int launch_history_head;
static unsigned int launch_history_count;
static unsigned int launch_history_remaining = DD_CMD_WATCH_LAUNCH_HISTORY_BUDGET;

static int dd_cmd_watch_diagnostics_open(void);
static void dd_cmd_watch_emit(const char *message);

static uint64_t dd_cmd_watch_launch_hash(const uint32_t *task_words)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    unsigned int i;

    for (i = 0; i < 16; ++i)
        hash = (hash * UINT64_C(1099511628211)) ^ task_words[i];
    return hash;
}

static void dd_cmd_watch_clear_launch_history(void)
{
    memset(launch_history, 0, sizeof(launch_history));
    launch_history_head = 0;
    launch_history_count = 0;
    launch_history_remaining = DD_CMD_WATCH_LAUNCH_HISTORY_BUDGET;
}

static void dd_cmd_watch_record_launch(
    const uint32_t *task_words, uint32_t generation,
    int buffer_valid, uint32_t nonzero_words)
{
    struct dd_cmd_watch_launch_snapshot *snapshot;
    unsigned int index;
    unsigned int i;

    if (launch_history_count < DD_CMD_WATCH_LAUNCH_HISTORY_CAPACITY)
    {
        index = (launch_history_head + launch_history_count)
            % DD_CMD_WATCH_LAUNCH_HISTORY_CAPACITY;
        ++launch_history_count;
    }
    else
    {
        index = launch_history_head;
        launch_history_head = (launch_history_head + 1)
            % DD_CMD_WATCH_LAUNCH_HISTORY_CAPACITY;
    }
    snapshot = &launch_history[index];
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->generation = generation;
    snapshot->descriptor_hash = dd_cmd_watch_launch_hash(task_words);
    snapshot->data_ptr = task_words[12];
    snapshot->data_size = task_words[13];
    snapshot->buffer_valid = buffer_valid != 0;
    snapshot->nonzero_words = nonzero_words;
    for (i = 0; i < DD_CMD_WATCH_LAUNCH_SAMPLE_WORDS; ++i)
    {
        snapshot->prefix[i] = task_words[i];
        snapshot->suffix[i] = task_words[16
            - DD_CMD_WATCH_LAUNCH_SAMPLE_WORDS + i];
    }
}

static void dd_cmd_watch_emit_launch_history(void)
{
    unsigned int i;
    char message[512];

    if (!dd_cmd_watch_diagnostics_open() || launch_history_remaining == 0)
        return;
    for (i = 0; i < launch_history_count
            && launch_history_remaining != 0; ++i)
    {
        const struct dd_cmd_watch_launch_snapshot *snapshot =
            &launch_history[(launch_history_head + i)
                % DD_CMD_WATCH_LAUNCH_HISTORY_CAPACITY];

        (void)snprintf(message, sizeof(message),
            "DDSTART14 RSP launch_snapshot source=TASK_DESCRIPTOR"
            " generation=%" PRIu32
            " descriptor_hash=0x%016" PRIx64
            " data_ptr_raw=0x%08" PRIx32
            " data_size_raw=0x%08" PRIx32
            " buffer_valid=%" PRIu32
            " nonzero_words=%" PRIu32
            " descriptor_prefix={0x%08" PRIx32 ",0x%08" PRIx32
            ",0x%08" PRIx32 ",0x%08" PRIx32 "}"
            " descriptor_suffix={0x%08" PRIx32 ",0x%08" PRIx32
            ",0x%08" PRIx32 ",0x%08" PRIx32 "}"
            " sample_provenance=raw-descriptor-retained",
            snapshot->generation, snapshot->descriptor_hash,
            snapshot->data_ptr, snapshot->data_size, snapshot->buffer_valid,
            snapshot->nonzero_words,
            snapshot->prefix[0], snapshot->prefix[1],
            snapshot->prefix[2], snapshot->prefix[3],
            snapshot->suffix[0], snapshot->suffix[1],
            snapshot->suffix[2], snapshot->suffix[3]);
        dd_cmd_watch_emit(message);
        --launch_history_remaining;
    }
}

static struct dd_cmd_watch_slot slots[DD_CMD_WATCH_BUFFER_COUNT];
static unsigned int trace_remaining = DD_CMD_WATCH_TRACE_BUDGET;
static unsigned int context_remaining = DD_CMD_WATCH_CONTEXT_BUDGET;
static unsigned int probe_remaining = DD_CMD_WATCH_PROBE_BUDGET;
static unsigned int compile_rejection_remaining =
    DD_CMD_WATCH_COMPILE_REJECTION_BUDGET;
static unsigned int probe_header_unavailable_count;
static unsigned int probe_header_invalid_t8_count;
static unsigned int probe_missing_required_count;
static int route_pending;
static struct dd_cmd_watch_context_record pending_context;
static unsigned int coverage =
    DD_CMD_WATCH_COVERAGE_CPU_FAST_ALIGNED
    | DD_CMD_WATCH_COVERAGE_CPU_SLOW_ALIGNED
    | DD_CMD_WATCH_GAP_CPU_FAST_UNALIGNED
    | DD_CMD_WATCH_GAP_CPU_SLOW_UNALIGNED
    | DD_CMD_WATCH_GAP_DMA
    | DD_CMD_WATCH_GAP_TLB;

volatile struct dd_cmd_watch_route_state dd_cmd_watch_routes;

static int dd_cmd_watch_diagnostics_open(void)
{
    void *context = NULL;

    if (!DdStartupDiagnosticsEnabled() || !DdRuntimePolicyGet())
        return 0;
    return DdStartupDiagnosticsGetCallback(&context) != NULL;
}

static uint32_t dd_cmd_watch_task_physical(uint32_t address)
{
    return address & DD_CMD_WATCH_KSEG_MASK;
}

static int dd_cmd_watch_guest_physical(uint32_t address, uint32_t *physical)
{
    uint32_t segment = address & UINT32_C(0xe0000000);

    /*
     * Writer attribution is deliberately limited to unmapped KSEG aliases.
     * Do not turn an arbitrary TLB virtual label into a physical RDRAM
     * address merely because its low 29 bits happen to match a buffer.
     */
    if (segment != DD_CMD_WATCH_KSEG0 && segment != DD_CMD_WATCH_KSEG1)
        return 0;
    if (physical != NULL)
        *physical = address & DD_CMD_WATCH_KSEG_MASK;
    return 1;
}

static int dd_cmd_watch_overlap(uint32_t physical, uint32_t width,
                                uint32_t base, uint32_t length)
{
    uint64_t start = physical;
    uint64_t end = start + (uint64_t)width;
    uint64_t range_end = (uint64_t)base + (uint64_t)length;

    return width != 0 && start < range_end && end > base;
}

static int dd_cmd_watch_aligned(uint32_t address, uint32_t width)
{
    if (width == 2)
        return (address & 1u) == 0;
    if (width == 4)
        return (address & 3u) == 0;
    if (width == 8)
        return (address & 7u) == 0;
    return width == 1;
}

static void dd_cmd_watch_clear_slot(struct dd_cmd_watch_slot *slot)
{
    slot->armed = 0;
    slot->base = 0;
    slot->length = 0;
    slot->generation = 0;
    slot->next_sequence = 0;
    slot->head = 0;
    slot->count = 0;
    slot->dropped = 0;
    slot->replaced = 0;
    slot->replacements = 0;
    memset(slot->recent, 0, sizeof(slot->recent));
    memset(&slot->first_context, 0, sizeof(slot->first_context));
    memset(&slot->latest_context, 0, sizeof(slot->latest_context));
}

static void dd_cmd_watch_clear_routes(void)
{
    memset((void *)&dd_cmd_watch_routes, 0, sizeof(dd_cmd_watch_routes));
}

static void dd_cmd_watch_close(void)
{
    unsigned int i;

    for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
        dd_cmd_watch_clear_slot(&slots[i]);
    trace_remaining = DD_CMD_WATCH_TRACE_BUDGET;
    context_remaining = DD_CMD_WATCH_CONTEXT_BUDGET;
    probe_remaining = DD_CMD_WATCH_PROBE_BUDGET;
    compile_rejection_remaining = DD_CMD_WATCH_COMPILE_REJECTION_BUDGET;
    probe_header_unavailable_count = 0;
    probe_header_invalid_t8_count = 0;
    probe_missing_required_count = 0;
    route_pending = 0;
    memset(&pending_context, 0, sizeof(pending_context));
    dd_cmd_watch_clear_routes();
    dd_cmd_watch_clear_launch_history();
    dd_load_history_reset();
}

static void dd_cmd_watch_build_routes(void)
{
    unsigned int slot_index;
    unsigned int alias_index = 0;

    dd_cmd_watch_clear_routes();
    dd_cmd_watch_routes.enabled = dd_cmd_watch_diagnostics_open() ? 1u : 0u;
    if (!dd_cmd_watch_routes.enabled)
        return;

    for (slot_index = 0; slot_index < DD_CMD_WATCH_BUFFER_COUNT; ++slot_index)
    {
        const struct dd_cmd_watch_slot *slot = &slots[slot_index];
        uint32_t aliases[2];
        unsigned int i;

        if (!slot->armed || slot->length == 0)
            continue;

        aliases[0] = slot->base | DD_CMD_WATCH_KSEG0;
        aliases[1] = slot->base | DD_CMD_WATCH_KSEG1;
        for (i = 0; i < 2 && alias_index < DD_CMD_WATCH_ALIAS_COUNT; ++i)
        {
            uint32_t route_base = aliases[i] > DD_CMD_WATCH_ROUTE_SLACK
                ? aliases[i] - DD_CMD_WATCH_ROUTE_SLACK : 0;
            uint64_t route_end = (uint64_t)aliases[i] + slot->length;

            /*
             * The route check is deliberately a conservative superset for
             * SD's eight-byte width.  The slow observer performs the exact
             * half-open overlap test before retaining an event.
             */
            dd_cmd_watch_routes.base[alias_index] = route_base;
            dd_cmd_watch_routes.end[alias_index] =
                route_end > UINT32_MAX ? UINT32_MAX : (uint32_t)route_end;
            ++alias_index;
        }
    }
}

static int dd_cmd_watch_find_slot(uint32_t base, uint32_t length)
{
    unsigned int i;

    for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
    {
        if (slots[i].armed && slots[i].base == base
                && slots[i].length == length)
            return (int)i;
    }
    return -1;
}

static int dd_cmd_watch_choose_slot(void)
{
    unsigned int i;

    for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
    {
        if (!slots[i].armed)
            return (int)i;
    }

    /*
     * The two-slot model is sized for the alternating command buffers.  If a
     * third identity appears, replace the less recently published slot; the
     * summary on the next zero launch remains explicit about the bounded
     * coverage rather than silently growing storage.
     */
    return slots[0].generation <= slots[1].generation ? 0 : 1;
}

static void dd_cmd_watch_emit(const char *message)
{
    void *context = NULL;
    ptr_DdStartupDiagnosticsCallback callback;

    if (trace_remaining == 0 || message == NULL)
        return;
    callback = DdStartupDiagnosticsGetCallback(&context);
    if (callback == NULL || !DdRuntimePolicyGet())
        return;
    --trace_remaining;
    (*callback)(context, M64MSG_INFO, message);
}

static int dd_cmd_watch_emit_probe(const char *message)
{
    void *context = NULL;
    ptr_DdStartupDiagnosticsCallback callback;

    if (probe_remaining == 0 || message == NULL)
        return 0;
    callback = DdStartupDiagnosticsGetCallback(&context);
    if (callback == NULL || !DdRuntimePolicyGet())
        return 0;
    --probe_remaining;
    (*callback)(context, M64MSG_INFO, message);
    return 1;
}

static const char *dd_cmd_watch_probe_kind_name(uint32_t kind)
{
    return kind == DD_CMD_WATCH_PROBE_CALL ? "call"
        : kind == DD_CMD_WATCH_PROBE_ENTRY ? "entry" : "unknown";
}

static const char *dd_cmd_watch_compile_reason_name(uint32_t reason)
{
    switch (reason)
    {
    case DD_CMD_WATCH_PROBE_COMPILE_MISSING_REGISTER:
        return "missing-register";
    case DD_CMD_WATCH_PROBE_COMPILE_PAGE_SPAN:
        return "page-span";
    case DD_CMD_WATCH_PROBE_COMPILE_PROVENANCE:
        return "provenance";
    case DD_CMD_WATCH_PROBE_COMPILE_OPCODE_GATE:
        return "opcode-gate";
    default:
        return "unknown";
    }
}

void dd_cmd_watch_note_compile_rejection(
    uint32_t kind, uint32_t reason, uint32_t valid_mask)
{
    void *context = NULL;
    ptr_DdStartupDiagnosticsCallback callback;
    char message[256];

    if (compile_rejection_remaining == 0
            || !dd_cmd_watch_diagnostics_open())
        return;
    callback = DdStartupDiagnosticsGetCallback(&context);
    if (callback == NULL || !DdRuntimePolicyGet())
        return;
    (void)snprintf(message, sizeof(message),
        "DDSTART15 probe_compile_reject kind=%s reason=%s"
        " valid_mask=0x%08" PRIx32 " budget_remaining=%u",
        dd_cmd_watch_probe_kind_name(kind),
        dd_cmd_watch_compile_reason_name(reason), valid_mask,
        compile_rejection_remaining - 1);
    --compile_rejection_remaining;
    (*callback)(context, M64MSG_INFO, message);
}

static int dd_cmd_watch_probe_kseg_address(uint64_t address)
{
    uint32_t guest = (uint32_t)address;
    uint32_t segment = guest & UINT32_C(0xe0000000);

    /*
     * The dynarec wrapper performs the actual bounded RDRAM read.  Keep this
     * duplicate address check in the recorder so a host fixture cannot turn
     * an arbitrary value into a claimed source header.
     */
    return (guest & UINT32_C(3)) == 0
        && (segment == DD_CMD_WATCH_KSEG0
            || segment == DD_CMD_WATCH_KSEG1);
}

static int dd_cmd_watch_context_safe_for_range(
    const struct dd_cmd_watch_context_record *context,
    uint32_t base, uint32_t length)
{
    uint32_t physical;

    if (!context->valid || length == 0
            || !dd_cmd_watch_guest_physical(context->address, &physical))
        return 0;
    return dd_cmd_watch_overlap(physical, context->width, base, length);
}

static int dd_cmd_watch_context_equal(
    const struct dd_cmd_watch_context_record *left,
    const struct dd_cmd_watch_context_record *right)
{
    return left->valid == right->valid
        && left->buffer_base == right->buffer_base
        && left->buffer_size == right->buffer_size
        && left->generation == right->generation
        && left->address == right->address
        && left->store_pc == right->store_pc
        && left->flags == right->flags
        && left->width == right->width
        && left->before == right->before
        && left->after == right->after
        && left->ra == right->ra
        && left->sp == right->sp
        && left->a0 == right->a0
        && left->a1 == right->a1
        && left->a3 == right->a3;
}

static void dd_cmd_watch_emit_context(
    const struct dd_cmd_watch_context_record *context,
    const char *kind)
{
    char message[1024];

    if (!context->valid || context_remaining == 0
            || (context->flags & DD_CMD_WATCH_CONTEXT_VALID_ALL)
                != DD_CMD_WATCH_CONTEXT_VALID_ALL)
        return;
    (void)snprintf(message, sizeof(message),
        "DDSTART14 CPU store_context source=ROUTED_ALIGNED"
        " context=%s buffer=0x%08" PRIx32 " size=0x%08" PRIx32
        " generation=%" PRIu32 " address=0x%08" PRIx32
        " width=%u before=0x%016" PRIx64 " after=0x%016" PRIx64
        " store_pc=0x%08" PRIx32 " delay_slot=%u"
        " ra=0x%016" PRIx64 " sp=0x%016" PRIx64
        " store_a0=0x%016" PRIx64 " store_a1=0x%016" PRIx64
        " store_a3=0x%016" PRIx64
        " context_provenance=generated-arm64-route-live-mapped-or-state"
        " entry_arguments=not-inferred",
        kind, context->buffer_base, context->buffer_size,
        context->generation, context->address, context->width,
        context->before, context->after, context->store_pc,
        (context->flags & DD_CMD_WATCH_CONTEXT_DELAY_SLOT) != 0,
        context->ra, context->sp, context->a0, context->a1, context->a3);
    dd_cmd_watch_emit(message);
    --context_remaining;
}

static void dd_cmd_watch_flush_slot(struct dd_cmd_watch_slot *slot)
{
    unsigned int i;
    unsigned int emitted = 0;
    unsigned int suppressed;
    char message[512];

    if (!slot->armed)
        return;
    if (!dd_cmd_watch_diagnostics_open())
        return;

    dd_cmd_watch_emit_context(&slot->first_context, "first");
    if (slot->latest_context.valid
            && (!slot->first_context.valid
                || !dd_cmd_watch_context_equal(&slot->latest_context,
                    &slot->first_context)))
        dd_cmd_watch_emit_context(&slot->latest_context, "latest");

    for (i = 0; i < slot->count; ++i)
    {
        const struct dd_cmd_watch_event *event =
            &slot->recent[(slot->head + i) % DD_CMD_WATCH_RECENT_CAPACITY];

        /* Reserve one line for the summary and its exhaustion indicator. */
        if (trace_remaining <= 1)
            break;
        (void)snprintf(message, sizeof(message),
            "DDSTART14 CPU store source=ROUTED_ALIGNED"
            " buffer=0x%08" PRIx32 " size=0x%08" PRIx32
            " generation=%" PRIu32 " address=0x%08" PRIx32
            " width=%u before=0x%016" PRIx64 " after=0x%016" PRIx64
            " pc=0x%08" PRIx32 " delay_slot=%u sequence=%u",
            event->buffer_base, event->buffer_size, event->generation,
            event->address, event->width, event->before, event->after,
            event->pc, event->delay_slot, event->sequence);
        dd_cmd_watch_emit(message);
        ++emitted;
    }

    suppressed = slot->count - emitted;
    (void)snprintf(message, sizeof(message),
        "DDSTART14 CPU store_summary source=ROUTED_ALIGNED"
        " buffer=0x%08" PRIx32 " size=0x%08" PRIx32
        " generation=%" PRIu32 " recent=%u dropped=%u"
        " replaced_events=%u replacements=%u suppressed=%u"
        " budget_exhausted=%u coverage=0x%08x",
        slot->base, slot->length, slot->generation, slot->count,
        slot->dropped, slot->replaced, slot->replacements, suppressed,
        trace_remaining <= 1 ? 1u : 0u, coverage);
    dd_cmd_watch_emit(message);

    slot->head = 0;
    slot->count = 0;
    slot->dropped = 0;
    slot->replaced = 0;
    slot->replacements = 0;
    memset(&slot->first_context, 0, sizeof(slot->first_context));
    memset(&slot->latest_context, 0, sizeof(slot->latest_context));
}

void dd_cmd_watch_reset(void)
{
    unsigned int i;

    for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
        dd_cmd_watch_clear_slot(&slots[i]);
    trace_remaining = DD_CMD_WATCH_TRACE_BUDGET;
    context_remaining = DD_CMD_WATCH_CONTEXT_BUDGET;
    probe_remaining = DD_CMD_WATCH_PROBE_BUDGET;
    compile_rejection_remaining = DD_CMD_WATCH_COMPILE_REJECTION_BUDGET;
    probe_header_unavailable_count = 0;
    probe_header_invalid_t8_count = 0;
    probe_missing_required_count = 0;
    route_pending = 0;
    memset(&pending_context, 0, sizeof(pending_context));
    dd_cmd_watch_clear_routes();
    dd_cmd_watch_clear_launch_history();
    dd_load_history_reset();
}

void dd_cmd_watch_task_entry(const uint32_t *task_words,
                             uint32_t generation,
                             int buffer_valid,
                             uint32_t nonzero_words)
{
    uint32_t base;
    uint32_t length;
    int slot_index;
    struct dd_cmd_watch_slot *slot;
    struct dd_cmd_watch_context_record carried_context;
    int carry_context = 0;

    if (!dd_cmd_watch_diagnostics_open())
    {
        dd_cmd_watch_close();
        return;
    }
    if (task_words == NULL || task_words[0] != 2u)
        return;
    dd_cmd_watch_record_launch(task_words, generation, buffer_valid,
        nonzero_words);
    if (nonzero_words == 0)
    {
        dd_cmd_watch_emit_launch_history();
        dd_load_history_flush("zero-audio");
    }
    if (!buffer_valid)
        return;

    base = dd_cmd_watch_task_physical(task_words[12]);
    length = task_words[13];
    if (length == 0 || (uint64_t)base + (uint64_t)length > UINT32_MAX + 1ull)
        return;

    slot_index = dd_cmd_watch_find_slot(base, length);
    if (slot_index < 0)
    {
        unsigned int i;

        for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
        {
            if (slots[i].armed && slots[i].base == base)
            {
                slot_index = (int)i;
                break;
            }
        }
    }
    if (slot_index < 0)
        slot_index = dd_cmd_watch_choose_slot();
    slot = &slots[slot_index];

    if (slot->armed && (slot->base != base || slot->length != length))
    {
        /*
         * A resized instance with the same physical base may safely carry
         * the last qualifying context into the new bounded slot.  Keep its
         * original generation and range metadata in the carried record; it
         * is evidence from the old size, not an inferred new entry context.
         */
        if (slot->base == base
                && dd_cmd_watch_context_safe_for_range(
                    &slot->latest_context, base, length))
        {
            carried_context = slot->latest_context;
            carry_context = 1;
        }
        /* A replacement is itself a bounded-coverage boundary. */
        slot->replaced += slot->count + slot->dropped;
        ++slot->replacements;
        slot->head = 0;
        slot->count = 0;
        slot->dropped = 0;
        memset(&slot->first_context, 0, sizeof(slot->first_context));
        memset(&slot->latest_context, 0, sizeof(slot->latest_context));
    }
    if (nonzero_words == 0)
        dd_cmd_watch_flush_slot(slot);

    slot->armed = 1;
    slot->base = base;
    slot->length = length;
    slot->generation = generation;
    if (carry_context)
        slot->latest_context = carried_context;
    dd_cmd_watch_build_routes();
}

int dd_cmd_watch_route_should_slow(uint32_t address, uint32_t width)
{
    unsigned int i;

    route_pending = 0;
    if (!dd_cmd_watch_diagnostics_open())
    {
        dd_cmd_watch_close();
        return 0;
    }
    if (!dd_cmd_watch_routes.enabled || !dd_cmd_watch_aligned(address, width))
        return 0;

    for (i = 0; i < DD_CMD_WATCH_ALIAS_COUNT; ++i)
    {
        uint64_t end = (uint64_t)address + width;
        if (dd_cmd_watch_routes.base[i] != 0
                && address < dd_cmd_watch_routes.end[i]
                && end > dd_cmd_watch_routes.base[i])
        {
            route_pending = 1;
            return 1;
        }
    }
    return 0;
}

int dd_cmd_watch_route_consume(void)
{
    int pending = route_pending;

    route_pending = 0;
    return pending;
}

void dd_cmd_watch_capture_context(uint32_t address,
                                  uint32_t store_pc,
                                  uint32_t flags,
                                  uint64_t ra,
                                  uint64_t sp,
                                  uint64_t a0,
                                  uint64_t a1,
                                  uint64_t a3)
{
    unsigned int i;
    uint32_t normalized;

    memset(&pending_context, 0, sizeof(pending_context));
    if (!dd_cmd_watch_diagnostics_open())
    {
        dd_cmd_watch_close();
        return;
    }
    if (!dd_cmd_watch_guest_physical(address, &normalized))
        return;
    for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
    {
        if (slots[i].armed
                /*
                 * The generated route is an eight-byte conservative
                 * superset.  The exact width/range check remains in the
                 * successful record path below.
                 */
                && dd_cmd_watch_overlap(normalized, 8,
                    slots[i].base, slots[i].length))
            break;
    }
    if (i == DD_CMD_WATCH_BUFFER_COUNT)
        return;

    pending_context.valid = 1;
    pending_context.address = address;
    pending_context.store_pc = store_pc;
    pending_context.flags = flags;
    pending_context.ra = ra;
    pending_context.sp = sp;
    pending_context.a0 = a0;
    pending_context.a1 = a1;
    pending_context.a3 = a3;
}

static void dd_cmd_watch_format_sample(
    char *destination, size_t capacity, const uint32_t *sample,
    uint32_t valid_mask, unsigned int count)
{
    size_t used = 0;
    unsigned int i;

    if (destination == NULL || capacity == 0)
        return;
    destination[0] = '\0';
    for (i = 0; i < count; ++i)
    {
        int written = snprintf(destination + used, capacity - used,
            "%s%08" PRIx32, i == 0 ? "" : ",",
            sample != NULL && (valid_mask & (UINT32_C(1) << i))
                ? sample[i] : 0);
        if (written < 0 || (size_t)written >= capacity - used)
            break;
        used += (size_t)written;
    }
}

void dd_cmd_watch_capture_probe(
    const struct dd_cmd_watch_probe_snapshot *snapshot,
    int source_header_valid,
    uint32_t source_header_value)
{
    dd_cmd_watch_capture_probe_samples(snapshot, source_header_valid,
        source_header_value, NULL, 0, NULL, 0);
}

void dd_cmd_watch_capture_probe_samples(
    const struct dd_cmd_watch_probe_snapshot *snapshot,
    int source_header_valid,
    uint32_t source_header_value,
    const uint32_t *header_sample,
    uint32_t header_sample_valid_mask,
    const uint32_t *stack_sample,
    uint32_t stack_sample_valid_mask)
{
    char message[2048];
    char header_sample_text[512];
    char stack_sample_text[128];
    uint32_t flags;
    uint32_t valid_mask;

    if (snapshot == NULL || !dd_cmd_watch_diagnostics_open()
            || probe_remaining == 0)
        return;

    flags = snapshot->flags;
    valid_mask = flags & DD_CMD_WATCH_PROBE_REGISTER_MASK;
    dd_cmd_watch_format_sample(header_sample_text,
        sizeof(header_sample_text), header_sample, header_sample_valid_mask, 16);
    dd_cmd_watch_format_sample(stack_sample_text,
        sizeof(stack_sample_text), stack_sample, stack_sample_valid_mask, 4);
    if (snapshot->kind == DD_CMD_WATCH_PROBE_CALL)
    {
        /*
         * This is the post-delay observation.  In particular, a1 is not
         * labeled as the pre-delay value: the compiled delay word is checked
         * as `or a1,s0,zero` and the emitter places this call after the link
         * value has also been materialized.
         *
         * The decoded call/delay/target gates remain exact.  The live
         * register fields are deliberately independent, however: t1 is an
         * optional comparator and t8 is an optional source-header probe.
         * Missing either must not hide trustworthy a0/a1/ra/sp evidence.
         */
        if (snapshot->call_pc != DD_CMD_WATCH_TARGET_CALL_PC
                || snapshot->call_opcode != DD_CMD_WATCH_TARGET_CALL_OPCODE
                || snapshot->delay_opcode != DD_CMD_WATCH_TARGET_DELAY_OPCODE
                || snapshot->target != DD_CMD_WATCH_TARGET_ENTRY_PC
                || !(flags & DD_CMD_WATCH_PROBE_AFTER_DELAY))
            return;
        if ((flags & DD_CMD_WATCH_PROBE_VALID_RA)
                && (uint32_t)snapshot->ra != DD_CMD_WATCH_TARGET_RETURN_PC)
            return;

        if ((flags & DD_CMD_WATCH_PROBE_CALL_REQUIRED)
                != DD_CMD_WATCH_PROBE_CALL_REQUIRED)
            ++probe_missing_required_count;

        if (!(flags & DD_CMD_WATCH_PROBE_VALID_T8))
        {
            ++probe_header_unavailable_count;
        }
        else if (!dd_cmd_watch_probe_kseg_address(snapshot->t8))
        {
            ++probe_header_unavailable_count;
            ++probe_header_invalid_t8_count;
        }
        else if (!source_header_valid)
        {
            ++probe_header_unavailable_count;
        }

        (void)snprintf(message, sizeof(message),
            "DDSTART15 load_clear_call source=ARM64_COMPILED_JAL"
            " call_pc=0x%08" PRIx32
            " call_opcode=0x%08" PRIx32
            " delay_opcode=0x%08" PRIx32
            " target=0x%08" PRIx32
            " phase=after-delay"
            " generation=%" PRIu32
            " valid_mask=0x%08" PRIx32
            " a0=0x%016" PRIx64 " a0_valid=%u"
            " a1=0x%016" PRIx64 " a1_valid=%u"
            " source_header_address=0x%08" PRIx32
            " source_header_value=0x%08" PRIx32
            " source_header_valid=%u"
            " comparator_t1=0x%016" PRIx64 " comparator_t1_valid=%u"
            " ra=0x%016" PRIx64 " ra_valid=%u"
            " sp=0x%016" PRIx64 " sp_valid=%u"
            " header_reason=%s"
            " header_unavailable_count=%u"
            " header_invalid_t8_count=%u"
            " missing_required_count=%u"
            " rejection_reason=%s"
            " a1_provenance=compiled-delay-or-a1-s0"
            " header_provenance=%s"
            " compiled_provenance=source-word-target-not-rdram",
            snapshot->call_pc, snapshot->call_opcode,
            snapshot->delay_opcode, snapshot->target, snapshot->generation,
            valid_mask,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_A0)
                ? snapshot->a0 : 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_A0) != 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_A1)
                ? snapshot->a1 : 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_A1) != 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_T8)
                ? (uint32_t)snapshot->t8 : 0,
            source_header_valid
                && (flags & DD_CMD_WATCH_PROBE_VALID_T8) != 0
                && dd_cmd_watch_probe_kseg_address(snapshot->t8)
                ? source_header_value : 0,
            source_header_valid
                && (flags & DD_CMD_WATCH_PROBE_VALID_T8) != 0
                && dd_cmd_watch_probe_kseg_address(snapshot->t8),
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_T1) != 0
                ? snapshot->t1 : 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_T1) != 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_RA)
                ? snapshot->ra : 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_RA) != 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_SP)
                ? snapshot->sp : 0,
            (valid_mask & DD_CMD_WATCH_PROBE_VALID_SP) != 0,
            !(flags & DD_CMD_WATCH_PROBE_VALID_T8)
                ? "t8-unavailable"
                : !dd_cmd_watch_probe_kseg_address(snapshot->t8)
                    ? "invalid-t8"
                    : !source_header_valid
                        ? "bounds-unavailable" : "validated",
            probe_header_unavailable_count, probe_header_invalid_t8_count,
            probe_missing_required_count,
            (flags & DD_CMD_WATCH_PROBE_CALL_REQUIRED)
                != DD_CMD_WATCH_PROBE_CALL_REQUIRED
                ? "missing-required-register"
                : !(flags & DD_CMD_WATCH_PROBE_VALID_T8)
                    || !source_header_valid
                    || !dd_cmd_watch_probe_kseg_address(snapshot->t8)
                    ? "header-unavailable" : "none",
            source_header_valid
                && (flags & DD_CMD_WATCH_PROBE_VALID_T8) != 0
                && dd_cmd_watch_probe_kseg_address(snapshot->t8)
                ? "validated-kseg-t8" : "unavailable");
        {
            size_t used = strlen(message);
            (void)snprintf(message + used, sizeof(message) - used,
                " header_sample_valid_mask=0x%08" PRIx32
                " header_sample_bytes=64"
                " header_sample_raw=%s"
                " header_sample_provenance=validated-direct-rdram-raw"
                " stack_sample_valid_mask=0x%08" PRIx32
                " stack_sample_raw=%s"
                " stack_sample_offsets=0x38,0x3c,0x4c,0x58"
                " stack_sample_provenance=validated-direct-rdram-raw",
                header_sample_valid_mask, header_sample_text,
                stack_sample_valid_mask, stack_sample_text);
        }
        if (dd_cmd_watch_emit_probe(message))
            dd_load_history_flush("call");
        return;
    }

    if (snapshot->kind != DD_CMD_WATCH_PROBE_ENTRY
            || snapshot->entry_pc != DD_CMD_WATCH_TARGET_ENTRY_PC
            || snapshot->call_pc != DD_CMD_WATCH_TARGET_CALL_PC
            || snapshot->call_opcode != DD_CMD_WATCH_TARGET_CALL_OPCODE
            || snapshot->delay_opcode != DD_CMD_WATCH_TARGET_DELAY_OPCODE
            || snapshot->target != DD_CMD_WATCH_TARGET_ENTRY_PC
            )
        return;
    if ((flags & DD_CMD_WATCH_PROBE_VALID_RA)
            && (uint32_t)snapshot->ra != DD_CMD_WATCH_TARGET_RETURN_PC)
        return;

    if ((flags & DD_CMD_WATCH_PROBE_ENTRY_VALID)
            != DD_CMD_WATCH_PROBE_ENTRY_VALID)
        ++probe_missing_required_count;

    (void)snprintf(message, sizeof(message),
        "DDSTART15 load_clear_entry source=ARM64_COMPILED_ENTRY"
        " entry_pc=0x%08" PRIx32
        " entry_opcode=0x%08" PRIx32
        " expected_return_ra=0x%08" PRIx32
        " generation=%" PRIu32
        " valid_mask=0x%08" PRIx32
        " a0=0x%016" PRIx64 " a0_valid=%u"
        " a1=0x%016" PRIx64 " a1_valid=%u"
        " ra=0x%016" PRIx64 " ra_valid=%u"
        " sp=0x%016" PRIx64 " sp_valid=%u"
        " missing_required_count=%u"
        " rejection_reason=%s"
        " original_arguments=%s"
        " original_arguments_proven=%u"
        " compiled_provenance=entry-word-call-site-target-not-rdram",
        snapshot->entry_pc, snapshot->entry_opcode,
        DD_CMD_WATCH_TARGET_RETURN_PC, snapshot->generation, valid_mask,
        (valid_mask & DD_CMD_WATCH_PROBE_VALID_A0)
            ? snapshot->a0 : 0,
        (valid_mask & DD_CMD_WATCH_PROBE_VALID_A0) != 0,
        (valid_mask & DD_CMD_WATCH_PROBE_VALID_A1)
            ? snapshot->a1 : 0,
        (valid_mask & DD_CMD_WATCH_PROBE_VALID_A1) != 0,
        (valid_mask & DD_CMD_WATCH_PROBE_VALID_RA)
            ? snapshot->ra : 0,
        (valid_mask & DD_CMD_WATCH_PROBE_VALID_RA) != 0,
        (valid_mask & DD_CMD_WATCH_PROBE_VALID_SP)
            ? snapshot->sp : 0,
        (valid_mask & DD_CMD_WATCH_PROBE_VALID_SP) != 0,
        probe_missing_required_count,
        (flags & DD_CMD_WATCH_PROBE_ENTRY_VALID)
            != DD_CMD_WATCH_PROBE_ENTRY_VALID
            ? "missing-required-register"
            : "none",
        (flags & DD_CMD_WATCH_PROBE_ENTRY_VALID)
            == DD_CMD_WATCH_PROBE_ENTRY_VALID
            && (uint32_t)snapshot->ra == DD_CMD_WATCH_TARGET_RETURN_PC
            ? "callee-entry" : "unproven",
        (flags & DD_CMD_WATCH_PROBE_ENTRY_VALID)
            == DD_CMD_WATCH_PROBE_ENTRY_VALID
            && (uint32_t)snapshot->ra == DD_CMD_WATCH_TARGET_RETURN_PC);
    if (dd_cmd_watch_emit_probe(message))
        dd_load_history_flush("entry");
}

int dd_cmd_watch_in_range(uint32_t address, uint32_t width)
{
    unsigned int i;
    uint32_t normalized;

    if (!dd_cmd_watch_diagnostics_open())
    {
        dd_cmd_watch_close();
        return 0;
    }
    if (!dd_cmd_watch_aligned(address, width)
            || !dd_cmd_watch_guest_physical(address, &normalized))
        return 0;
    for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
    {
        if (slots[i].armed
                && dd_cmd_watch_overlap(normalized, width,
                    slots[i].base, slots[i].length))
            return 1;
    }
    return 0;
}

void dd_cmd_watch_record_aligned(uint32_t address,
                                 uint32_t width,
                                 uint64_t before,
                                 uint64_t after,
                                 uint32_t pcaddr)
{
    unsigned int i;
    uint32_t normalized;
    struct dd_cmd_watch_slot *slot = NULL;
    struct dd_cmd_watch_event event;
    struct dd_cmd_watch_context_record context = pending_context;

    /*
     * A generated route is single-threaded with its writer call.  Consume
     * the pending context before any early return so an unsuccessful or
     * out-of-range write cannot annotate a later store.
     */
    memset(&pending_context, 0, sizeof(pending_context));

    if (!dd_cmd_watch_diagnostics_open())
    {
        dd_cmd_watch_close();
        return;
    }
    if (!dd_cmd_watch_in_range(address, width))
        return;

    if (!dd_cmd_watch_guest_physical(address, &normalized))
        return;
    for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
    {
        if (slots[i].armed
                && dd_cmd_watch_overlap(normalized, width,
                    slots[i].base, slots[i].length))
        {
            slot = &slots[i];
            break;
        }
    }
    if (slot == NULL)
        return;

    event.buffer_base = slot->base;
    event.buffer_size = slot->length;
    event.generation = slot->generation;
    /* Preserve the effective CPU address; matching above normalized aliases. */
    event.address = address;
    event.before = before;
    event.after = after;
    event.pc = ((pcaddr & ~UINT32_C(1)) - UINT32_C(4));
    event.sequence = slot->next_sequence++;
    event.width = (uint8_t)width;
    event.delay_slot = (uint8_t)(pcaddr & 1);

    if (context.valid && context.address == address
            && before != 0 && after == 0
            && (context.flags & DD_CMD_WATCH_CONTEXT_VALID_ALL)
                == DD_CMD_WATCH_CONTEXT_VALID_ALL)
    {
        context.buffer_base = slot->base;
        context.buffer_size = slot->length;
        context.generation = slot->generation;
        context.width = (uint8_t)width;
        context.before = before;
        context.after = after;
        if (!slot->first_context.valid)
            slot->first_context = context;
        slot->latest_context = context;
    }

    if (slot->count < DD_CMD_WATCH_RECENT_CAPACITY)
    {
        unsigned int index =
            (slot->head + slot->count) % DD_CMD_WATCH_RECENT_CAPACITY;
        slot->recent[index] = event;
        ++slot->count;
    }
    else
    {
        slot->recent[slot->head] = event;
        slot->head = (slot->head + 1) % DD_CMD_WATCH_RECENT_CAPACITY;
        ++slot->dropped;
    }
}

unsigned int dd_cmd_watch_coverage_mask(void)
{
    return coverage;
}