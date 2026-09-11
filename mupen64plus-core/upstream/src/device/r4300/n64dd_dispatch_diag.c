/*
 * Narrow, observational 64DD dispatcher diagnostics.
 *
 * The addresses below are the F-Zero X EK image's libultra scheduler symbols
 * recorded in doc/HANDOFF.md and .fzxwork/freeze_state.py.  This is a probe,
 * not a scheduler repair: it does not alter RDRAM, CP0, interrupt state, queue
 * links, or the selected thread.
 */
#include "n64dd_dispatch_diag.h"

#include <stddef.h>
#include <pthread.h>
#include <string.h>

#include "device/r4300/r4300_core.h"
#include "main/main.h"

/* The existing HANDOFF probe uses this exact resident __osDispatchThread span. */
#define DD_DISPATCH_START UINT32_C(0x80746f64)
#define DD_DISPATCH_END   UINT32_C(0x807470e4)

/* Guest virtual addresses, retained here for readable dump labels. */
#define DD_RUN_QUEUE      UINT32_C(0x80771e18)
#define DD_RUNNING_THREAD UINT32_C(0x80771e20)
#define DD_AUDIO_THREAD   UINT32_C(0x807999d0)
#define DD_AUDIO_BYTES    UINT32_C(0x140)

/* The rings are intentionally small: no hot-path file I/O and bounded dumps. */
#define DD_EVENT_RING 512
#define DD_STORE_RING 256
#define DD_AUDIO_WORDS (DD_AUDIO_BYTES / 4)

enum dd_event_kind
{
    DD_EVENT_STEP_PRE = 1,
    DD_EVENT_STEP_POST,
    DD_EVENT_DELAY_PRE,
    DD_EVENT_DELAY_POST,
    DD_EVENT_BOUNDARY,
    DD_EVENT_ERET
};

enum dd_store_kind
{
    DD_STORE_RUN_QUEUE = 1,
    DD_STORE_RUNNING_THREAD,
    DD_STORE_AUDIO_STATE,
    DD_STORE_AUDIO_CONTEXT,
    DD_STORE_AUDIO_OTHER,
    DD_STORE_SELECTED_STATE,
    DD_STORE_SELECTED_CONTEXT,
    DD_STORE_RUNNING_STATE,
    DD_STORE_RUNNING_CONTEXT
};

enum dd_store_provenance
{
    DD_STORE_DIRECT = 1,
    DD_STORE_SHADOW
};

struct dd_event
{
    uint32_t kind;
    uint32_t mode;
    uint32_t pc;
    uint32_t opcode;
    uint32_t run_queue;
    uint32_t running;
    uint32_t selected_state;
    uint32_t selected_sp;
    uint32_t selected_ra;
    uint32_t selected_pc;
    uint32_t selected_sr;
    uint32_t audio_state;
    uint32_t audio_queue;
    uint32_t audio_sp;
    uint32_t audio_ra;
    uint32_t audio_pc;
    uint32_t audio_sr;
    uint32_t cp0_epc;
    uint32_t epc;
    uint32_t target;
    uint32_t status;
};

struct dd_store
{
    uint32_t kind;
    uint32_t provenance;
    uint32_t mode;
    uint32_t pc;
    uint32_t virtual_address;
    uint32_t physical_address;
    uint32_t width;
    uint32_t value_hi;
    uint32_t value_lo;
    uint32_t mask_hi;
    uint32_t mask_lo;
    uint32_t after_hi;
    uint32_t after_lo;
};

static struct dd_event dd_events[DD_EVENT_RING];
static volatile uint32_t dd_event_count;
static struct dd_store dd_stores[DD_STORE_RING];
static volatile uint32_t dd_store_count;
static volatile uint32_t dd_step_count;
static volatile uint32_t dd_boundary_count;
static volatile uint32_t dd_eret_count;
static volatile uint32_t dd_audio_store_count;
static volatile uint32_t dd_selected_store_count;
static volatile uint32_t dd_running_store_count;
static volatile uint32_t dd_queue_store_count;
static uint32_t dd_current_pc;

static uint32_t dd_audio_shadow[DD_AUDIO_WORDS];
static int dd_audio_shadow_valid;
static const void* dd_shadow_disk;
/*
 * The watchdog dumps from a different host thread while the CPU thread fills
 * these rings.  Every publication and every dump is serialized; volatile
 * counters alone would still leave C data races on the structs themselves.
 */
static pthread_mutex_t dd_lock = PTHREAD_MUTEX_INITIALIZER;

static int dd_enabled(void)
{
    return g_dev.dd.idisk != NULL;
}

static uint32_t dd_phys(uint32_t address)
{
    /*
     * KSEG0/KSEG1 and already-physical RDRAM addresses all reduce to the
     * same low 29-bit RDRAM address.  The read helper below still bounds it.
     */
    return address & UINT32_C(0x1fffffff);
}

static int dd_read_phys(uint32_t address, uint32_t* value)
{
    uint32_t physical = dd_phys(address);
    if (g_dev.rdram.dram == NULL || g_dev.rdram.dram_size < 4 ||
        physical > g_dev.rdram.dram_size - 4)
        return 0;
    *value = g_dev.rdram.dram[physical >> 2];
    return 1;
}

static uint32_t dd_word(uint32_t address)
{
    uint32_t value;
    return dd_read_phys(address, &value) ? value : UINT32_C(0xffffffff);
}

static uint32_t dd_mode(const struct r4300_core* r4300)
{
    if (r4300 == NULL)
        return 0;
    return r4300->emumode;
}

static const char* dd_mode_name(uint32_t mode)
{
    switch (mode)
    {
    case EMUMODE_PURE_INTERPRETER: return "pure";
    case EMUMODE_INTERPRETER: return "cached";
    case EMUMODE_DYNAREC: return "dynarec";
    default: return "unknown";
    }
}

static const char* dd_event_name(uint32_t kind)
{
    switch (kind)
    {
    case DD_EVENT_STEP_PRE: return "pre";
    case DD_EVENT_STEP_POST: return "post";
    case DD_EVENT_DELAY_PRE: return "delay-pre";
    case DD_EVENT_DELAY_POST: return "delay-post";
    case DD_EVENT_BOUNDARY: return "boundary";
    case DD_EVENT_ERET: return "eret";
    default: return "unknown";
    }
}

static const char* dd_store_name(uint32_t kind)
{
    switch (kind)
    {
    case DD_STORE_RUN_QUEUE: return "run-queue";
    case DD_STORE_RUNNING_THREAD: return "running-thread";
    case DD_STORE_AUDIO_STATE: return "audio-state";
    case DD_STORE_AUDIO_CONTEXT: return "audio-context";
    case DD_STORE_AUDIO_OTHER: return "audio-other";
    case DD_STORE_SELECTED_STATE: return "selected-state";
    case DD_STORE_SELECTED_CONTEXT: return "selected-context";
    case DD_STORE_RUNNING_STATE: return "running-state";
    case DD_STORE_RUNNING_CONTEXT: return "running-context";
    default: return "unknown";
    }
}

static const char* dd_provenance_name(uint32_t provenance)
{
    return provenance == DD_STORE_SHADOW ? "shadow" : "direct";
}

static int dd_in_dispatch(uint32_t pc)
{
    return pc >= DD_DISPATCH_START && pc < DD_DISPATCH_END;
}

static int dd_in_audio(uint32_t physical, uint32_t* offset)
{
    const uint32_t base = dd_phys(DD_AUDIO_THREAD);
    physical = dd_phys(physical) & ~UINT32_C(3);
    if (physical < base || physical >= base + DD_AUDIO_BYTES)
        return 0;
    if (offset != NULL)
        *offset = physical - base;
    return 1;
}

static int dd_in_thread(uint32_t thread, uint32_t physical,
                        uint32_t* offset)
{
    uint32_t base;
    physical = dd_phys(physical) & ~UINT32_C(3);
    if (thread == 0 || thread == UINT32_C(0xffffffff))
        return 0;
    base = dd_phys(thread);
    if (base > UINT32_C(0x007ffffc) || physical < base ||
        physical >= base + DD_AUDIO_BYTES)
        return 0;
    if (offset != NULL)
        *offset = physical - base;
    return 1;
}

static int dd_in_selected(uint32_t physical, uint32_t* offset)
{
    return dd_in_thread(dd_word(DD_RUN_QUEUE), physical, offset);
}

static int dd_in_running(uint32_t physical, uint32_t* offset)
{
    return dd_in_thread(dd_word(DD_RUNNING_THREAD), physical, offset);
}

static void dd_selected_fields(uint32_t thread, uint32_t* state,
                               uint32_t* sp, uint32_t* ra, uint32_t* pc,
                               uint32_t* sr)
{
    if (thread == 0 || thread == UINT32_C(0xffffffff))
    {
        *state = *sp = *ra = *pc = *sr = UINT32_C(0xffffffff);
        return;
    }
    *state = dd_word(thread + 0x10);
    *sp = dd_word(thread + 0x20 + 0xd0);
    *ra = dd_word(thread + 0x20 + 0xe0);
    *pc = dd_word(thread + 0x20 + 0xfc);
    *sr = dd_word(thread + 0x20 + 0xf8);
}

static void dd_fill_event(struct r4300_core* r4300, struct dd_event* event,
                          uint32_t kind, uint32_t pc)
{
    uint32_t selected;
    memset(event, 0, sizeof(*event));
    event->kind = kind;
    event->mode = dd_mode(r4300);
    event->pc = pc;
    event->opcode = dd_word(pc);
    event->run_queue = dd_word(DD_RUN_QUEUE);
    event->running = dd_word(DD_RUNNING_THREAD);
    event->audio_state = dd_word(DD_AUDIO_THREAD + 0x10);
    event->audio_queue = dd_word(DD_AUDIO_THREAD + 0x08);
    event->audio_sp = dd_word(DD_AUDIO_THREAD + 0x20 + 0xd0);
    event->audio_ra = dd_word(DD_AUDIO_THREAD + 0x20 + 0xe0);
    event->audio_pc = dd_word(DD_AUDIO_THREAD + 0x20 + 0xfc);
    event->audio_sr = dd_word(DD_AUDIO_THREAD + 0x20 + 0xf8);
    if (r4300 != NULL)
    {
        event->cp0_epc = r4300_cp0_regs(&r4300->cp0)[CP0_EPC_REG];
        event->status = r4300_cp0_regs(&r4300->cp0)[CP0_STATUS_REG];
    }
    selected = event->run_queue;
    dd_selected_fields(selected, &event->selected_state, &event->selected_sp,
                       &event->selected_ra, &event->selected_pc,
                       &event->selected_sr);
}

static void dd_record_event(struct r4300_core* r4300, uint32_t kind,
                            uint32_t pc, uint32_t epc, uint32_t target)
{
    struct dd_event* event;
    uint32_t index;
    if (!dd_enabled())
        return;
    index = dd_event_count++ & (DD_EVENT_RING - 1);
    event = &dd_events[index];
    dd_fill_event(r4300, event, kind, pc);
    event->epc = epc;
    event->target = target;
}

static void dd_reset_shadow_if_needed(void)
{
    unsigned i;
    if (dd_shadow_disk == g_dev.dd.idisk && dd_audio_shadow_valid)
        return;
    dd_shadow_disk = g_dev.dd.idisk;
    dd_audio_shadow_valid = 0;
    if (g_dev.rdram.dram == NULL)
        return;
    for (i = 0; i < DD_AUDIO_WORDS; i++)
        dd_audio_shadow[i] = dd_word(DD_AUDIO_THREAD + i * 4);
    dd_audio_shadow_valid = 1;
}

static void dd_boundary_shadow(struct r4300_core* r4300, uint32_t pc)
{
    unsigned i;
    uint32_t now;
    if (!dd_enabled())
        return;
    dd_reset_shadow_if_needed();
    if (!dd_audio_shadow_valid)
        return;
    /*
     * The dynarec can inline a RDRAM store.  This cheap shadow catches a
     * changed audio-thread word at the next interrupt/sample boundary, while
     * the direct write hook below catches C memory paths exactly.
     */
    for (i = 0; i < DD_AUDIO_WORDS; i++)
    {
        now = dd_word(DD_AUDIO_THREAD + i * 4);
        if (now != dd_audio_shadow[i])
        {
            struct dd_store* store;
            uint32_t index = dd_store_count++ & (DD_STORE_RING - 1);
            store = &dd_stores[index];
            memset(store, 0, sizeof(*store));
            store->kind = (i == 0x10 / 4) ? DD_STORE_AUDIO_STATE
                : (i >= 0x20 / 4) ? DD_STORE_AUDIO_CONTEXT
                : DD_STORE_AUDIO_OTHER;
            store->provenance = DD_STORE_SHADOW;
            store->mode = dd_mode(r4300);
            store->pc = pc;
            store->virtual_address = DD_AUDIO_THREAD + i * 4;
            store->physical_address = dd_phys(store->virtual_address);
            store->width = 4;
            store->after_lo = now;
            dd_audio_store_count++;
            dd_audio_shadow[i] = now;
        }
    }
}

static void dd_reconcile_audio_shadow(uint32_t physical_address,
                                      unsigned width)
{
    uint32_t base = dd_phys(DD_AUDIO_THREAD);
    uint32_t first = dd_phys(physical_address) & ~UINT32_C(3);
    uint32_t last = first + (width > 4 ? 4 : 0);
    uint32_t word;
    if (!dd_audio_shadow_valid || dd_shadow_disk != g_dev.dd.idisk)
        return;
    for (word = first; word <= last; word += 4)
    {
        uint32_t offset;
        if (word < base || word >= base + DD_AUDIO_BYTES)
            continue;
        offset = (word - base) >> 2;
        dd_audio_shadow[offset] = dd_word(word);
    }
}

void n64dd_dispatch_diag_step(struct r4300_core* r4300, uint32_t pc,
                              unsigned phase)
{
    if (!dd_enabled())
        return;
    dd_current_pc = pc;
    pthread_mutex_lock(&dd_lock);
    dd_step_count++;
    if (dd_in_dispatch(pc))
    {
        uint32_t kind = phase == N64DD_DISPATCH_DIAG_PRE
            ? DD_EVENT_STEP_PRE
            : phase == N64DD_DISPATCH_DIAG_POST
            ? DD_EVENT_STEP_POST
            : phase == N64DD_DISPATCH_DIAG_DELAY_PRE
            ? DD_EVENT_DELAY_PRE
            : DD_EVENT_DELAY_POST;
        dd_record_event(r4300,
                        kind,
                        pc, 0, 0);
    }
    pthread_mutex_unlock(&dd_lock);
}

void n64dd_dispatch_diag_boundary(struct r4300_core* r4300, uint32_t pc)
{
    if (!dd_enabled())
        return;
    dd_current_pc = pc;
    pthread_mutex_lock(&dd_lock);
    dd_boundary_count++;
    dd_boundary_shadow(r4300, pc);
    if (dd_in_dispatch(pc))
        dd_record_event(r4300, DD_EVENT_BOUNDARY, pc, 0, 0);
    pthread_mutex_unlock(&dd_lock);
}

uint32_t n64dd_dispatch_diag_current_pc(struct r4300_core* r4300)
{
#ifdef NEW_DYNAREC
    if (r4300 != NULL && r4300->emumode == EMUMODE_DYNAREC)
        return r4300->new_dynarec_hot_state.pcaddr;
#else
    (void)r4300;
#endif
    return dd_current_pc;
}

void n64dd_dispatch_diag_store(struct r4300_core* r4300,
                               uint32_t virtual_address,
                               uint32_t physical_address,
                               unsigned width,
                               uint64_t value,
                               uint64_t mask,
                               uint32_t source_pc)
{
    uint32_t offset = 0;
    uint32_t selected_offset = 0;
    uint32_t kind = 0;
    struct dd_store* store;
    uint32_t index;
    if (!dd_enabled())
        return;
    if (source_pc == 0)
        source_pc = n64dd_dispatch_diag_current_pc(r4300);
    pthread_mutex_lock(&dd_lock);
    if (dd_phys(physical_address) == dd_phys(DD_RUN_QUEUE))
    {
        kind = DD_STORE_RUN_QUEUE;
        dd_queue_store_count++;
    }
    else if (dd_phys(physical_address) == dd_phys(DD_RUNNING_THREAD))
    {
        kind = DD_STORE_RUNNING_THREAD;
        dd_queue_store_count++;
    }
    else if (dd_in_audio(physical_address, &offset))
    {
        kind = offset == 0x10 ? DD_STORE_AUDIO_STATE
            : offset >= 0x20 ? DD_STORE_AUDIO_CONTEXT : DD_STORE_AUDIO_OTHER;
        dd_audio_store_count++;
    }
    else if (dd_in_selected(physical_address, &selected_offset))
    {
        kind = selected_offset == 0x10 ? DD_STORE_SELECTED_STATE
            : selected_offset >= 0x20 ? DD_STORE_SELECTED_CONTEXT : 0;
        if (kind != 0)
            dd_selected_store_count++;
    }
    else if (dd_in_running(physical_address, &selected_offset))
    {
        kind = selected_offset == 0x10 ? DD_STORE_RUNNING_STATE
            : selected_offset >= 0x20 ? DD_STORE_RUNNING_CONTEXT : 0;
        if (kind != 0)
            dd_running_store_count++;
    }
    if (kind == 0)
    {
        pthread_mutex_unlock(&dd_lock);
        return;
    }

    index = dd_store_count++ & (DD_STORE_RING - 1);
    store = &dd_stores[index];
    memset(store, 0, sizeof(*store));
    store->kind = kind;
    store->provenance = DD_STORE_DIRECT;
    store->mode = dd_mode(r4300);
    store->pc = source_pc;
    store->virtual_address = virtual_address;
    store->physical_address = dd_phys(physical_address);
    store->width = width;
    store->value_hi = (uint32_t)(value >> 32);
    store->value_lo = (uint32_t)value;
    store->mask_hi = (uint32_t)(mask >> 32);
    store->mask_lo = (uint32_t)mask;
    /*
     * The memory handler writes the architectural high word at address and
     * the low word at address+4.  Keep the textual 64-bit value high-to-low
     * as well; the previous split assigned these two fields backwards.
     */
    store->after_hi = width > 4 ? dd_word(physical_address) : 0;
    store->after_lo = width > 4 ? dd_word(physical_address + 4)
                                : dd_word(physical_address);
    dd_reconcile_audio_shadow(physical_address, width);
    pthread_mutex_unlock(&dd_lock);
}

void n64dd_dispatch_diag_eret(struct r4300_core* r4300, uint32_t pc,
                              uint32_t epc)
{
    if (!dd_enabled())
        return;
    pthread_mutex_lock(&dd_lock);
    dd_eret_count++;
    dd_record_event(r4300, DD_EVENT_ERET, pc, epc, epc);
    pthread_mutex_unlock(&dd_lock);
}

static void dd_print_event(FILE* file, uint32_t serial,
                           const struct dd_event* e)
{
    fprintf(file,
            "DDDISP n=%u kind=%s mode=%s pc=%08x op=%08x "
            "runq=%08x running=%08x sel_state=%08x sel_sp=%08x "
            "sel_ra=%08x sel_pc=%08x sel_sr=%08x audio_state=%08x "
            "audio_queue=%08x audio_sp=%08x audio_ra=%08x audio_pc=%08x "
            "audio_sr=%08x cp0_epc=%08x epc=%08x target=%08x cp0_sr=%08x\n",
            serial, dd_event_name(e->kind), dd_mode_name(e->mode), e->pc,
            e->opcode, e->run_queue, e->running, e->selected_state,
            e->selected_sp, e->selected_ra, e->selected_pc, e->selected_sr,
            e->audio_state, e->audio_queue, e->audio_sp, e->audio_ra,
            e->audio_pc, e->audio_sr, e->cp0_epc, e->epc, e->target,
            e->status);
}

static void dd_print_store(FILE* file, uint32_t serial,
                           const struct dd_store* s)
{
    fprintf(file,
            "DDSTORE n=%u kind=%s mode=%s pc=%08x va=%08x pa=%08x "
            "source=%s width=%u value=%08x%08x mask=%08x%08x "
            "after=%08x%08x\n",
            serial, dd_store_name(s->kind), dd_mode_name(s->mode), s->pc,
            s->virtual_address, s->physical_address,
            dd_provenance_name(s->provenance), s->width, s->value_hi,
            s->value_lo, s->mask_hi, s->mask_lo, s->after_hi, s->after_lo);
}

void n64dd_dispatch_diag_dump(FILE* file)
{
    uint32_t i;
    uint32_t n;
    uint32_t start;
    if (file == NULL)
        return;

    pthread_mutex_lock(&dd_lock);
    fprintf(file, "DDDIAG v1 gate=dd dispatch=%08x..%08x runq=%08x "
                  "running=%08x audio=%08x bytes=%x\n",
            DD_DISPATCH_START, DD_DISPATCH_END, DD_RUN_QUEUE,
            DD_RUNNING_THREAD, DD_AUDIO_THREAD, DD_AUDIO_BYTES);
    fprintf(file, "DDDIAG_COUNTS steps=%u boundaries=%u erets=%u events=%u "
                  "stores=%u audio_stores=%u selected_stores=%u "
                  "running_stores=%u queue_stores=%u\n",
            (unsigned)dd_step_count, (unsigned)dd_boundary_count,
            (unsigned)dd_eret_count, (unsigned)dd_event_count,
            (unsigned)dd_store_count, (unsigned)dd_audio_store_count,
            (unsigned)dd_selected_store_count,
            (unsigned)dd_running_store_count,
            (unsigned)dd_queue_store_count);

    n = dd_event_count < DD_EVENT_RING ? dd_event_count : DD_EVENT_RING;
    start = dd_event_count - n;
    for (i = 0; i < n; i++)
        dd_print_event(file, start + i,
                       &dd_events[(start + i) & (DD_EVENT_RING - 1)]);

    n = dd_store_count < DD_STORE_RING ? dd_store_count : DD_STORE_RING;
    start = dd_store_count - n;
    for (i = 0; i < n; i++)
        dd_print_store(file, start + i,
                       &dd_stores[(start + i) & (DD_STORE_RING - 1)]);
    fprintf(file, "DDDIAG_END\n");
    pthread_mutex_unlock(&dd_lock);
}