#include "device/dd/dd_cmd_watch.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"

#define DD_CMD_WATCH_KSEG_MASK UINT32_C(0x1fffffff)
#define DD_CMD_WATCH_KSEG0 UINT32_C(0x80000000)
#define DD_CMD_WATCH_KSEG1 UINT32_C(0xa0000000)
#define DD_CMD_WATCH_ROUTE_SLACK 7u

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
};

static struct dd_cmd_watch_slot slots[DD_CMD_WATCH_BUFFER_COUNT];
static unsigned int trace_remaining = DD_CMD_WATCH_TRACE_BUDGET;
static int route_pending;
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
    route_pending = 0;
    dd_cmd_watch_clear_routes();
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
}

void dd_cmd_watch_reset(void)
{
    unsigned int i;

    for (i = 0; i < DD_CMD_WATCH_BUFFER_COUNT; ++i)
        dd_cmd_watch_clear_slot(&slots[i]);
    trace_remaining = DD_CMD_WATCH_TRACE_BUDGET;
    dd_cmd_watch_clear_routes();
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

    if (!dd_cmd_watch_diagnostics_open())
    {
        dd_cmd_watch_close();
        return;
    }
    if (task_words == NULL || task_words[0] != 2u || !buffer_valid)
        return;

    base = dd_cmd_watch_task_physical(task_words[12]);
    length = task_words[13];
    if (length == 0 || (uint64_t)base + (uint64_t)length > UINT32_MAX + 1ull)
        return;

    slot_index = dd_cmd_watch_find_slot(base, length);
    if (slot_index < 0)
        slot_index = dd_cmd_watch_choose_slot();
    slot = &slots[slot_index];

    if (slot->armed && (slot->base != base || slot->length != length))
    {
        /* A replacement is itself a bounded-coverage boundary. */
        slot->replaced += slot->count + slot->dropped;
        ++slot->replacements;
        slot->head = 0;
        slot->count = 0;
        slot->dropped = 0;
    }
    if (nonzero_words == 0)
        dd_cmd_watch_flush_slot(slot);

    slot->armed = 1;
    slot->base = base;
    slot->length = length;
    slot->generation = generation;
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