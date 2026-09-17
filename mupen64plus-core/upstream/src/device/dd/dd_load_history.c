#include "device/dd/dd_load_history.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "api/callbacks.h"
#include "osal/preproc.h"

/* Window constants for the source-region classification. */
#include "device/device.h"

enum dd_dma_source_region dd_pi_dma_source_region(uint32_t cart_addr)
{
    return (cart_addr >= MM_DOM2_ADDR1 && cart_addr < MM_DOM2_ADDR2)
        ? DD_DMA_SOURCE_DD_ROM : DD_DMA_SOURCE_CART_ROM;
}

struct dd_load_history_record
{
    uint32_t sequence;
    uint32_t cart_addr;
    uint32_t dram_addr;
    uint32_t requested_length;
    uint8_t before[DD_LOAD_HISTORY_SAMPLE_BYTES];
    uint8_t after[DD_LOAD_HISTORY_SAMPLE_BYTES];
    uint8_t before_length;
    uint8_t after_length;
    uint8_t sample_clipped;
    uint8_t before_out_of_bounds;
    enum dd_dma_source_region source_region; /* set at begin from cart_addr */
    uint8_t after_out_of_bounds;
};

static struct dd_load_history_record recent[DD_LOAD_HISTORY_CAPACITY];
static unsigned int recent_head;
static unsigned int recent_count;
static uint32_t next_sequence = 1;
static uint64_t overwritten_total;
static unsigned int ring_wrapped;
static unsigned int suppressed_records;
static unsigned int suppressed_flushes;
static unsigned int early_flushes;
static unsigned int zero_audio_flushes;
static unsigned int zero_audio_suppression_reported;

static ptr_DdStartupDiagnosticsCallback dd_load_history_callback(
    void **context)
{
    if (context != NULL)
        *context = NULL;

    /*
     * Keep the two gates explicit.  In particular, a debug callback or an
     * enabled DD policy alone must never make this observer active.
     */
    if (!DdStartupDiagnosticsEnabled() || !DdRuntimePolicyGet())
        return NULL;
    return DdStartupDiagnosticsGetCallback(context);
}

static int dd_load_history_guest_byte(const uint8_t *dram,
                                      size_t dram_size,
                                      uint32_t dram_addr,
                                      unsigned int offset,
                                      uint8_t *value)
{
    uint64_t guest_index = (uint64_t)dram_addr + offset;
    uint64_t host_index;

    if (dram == NULL || value == NULL || guest_index > UINT32_MAX
            || guest_index >= dram_size)
        return 0;

    /*
     * RDRAM is stored in host byte order while PI addresses are guest byte
     * addresses.  Match the same ^S8 mapping used by the production DMA
     * loops, but validate both sides before dereferencing.
     */
    host_index = guest_index ^ (uint64_t)S8;
    if (host_index >= dram_size)
        return 0;
    *value = dram[(size_t)host_index];
    return 1;
}

static unsigned int dd_load_history_sample(const uint8_t *dram,
                                           size_t dram_size,
                                           uint32_t dram_addr,
                                           uint32_t requested_length,
                                           uint8_t *sample)
{
    unsigned int wanted = requested_length > DD_LOAD_HISTORY_SAMPLE_BYTES
        ? DD_LOAD_HISTORY_SAMPLE_BYTES : requested_length;
    unsigned int i;

    if (sample == NULL)
        return 0;
    for (i = 0; i < wanted; ++i)
    {
        if (!dd_load_history_guest_byte(dram, dram_size, dram_addr, i,
                    &sample[i]))
            break;
    }
    return i;
}

static void dd_load_history_format_sample(char *destination,
                                          size_t capacity,
                                          const uint8_t *sample,
                                          unsigned int length)
{
    size_t used = 0;
    unsigned int i;

    if (destination == NULL || capacity == 0)
        return;
    destination[0] = '\0';
    for (i = 0; i < length && i < DD_LOAD_HISTORY_SAMPLE_BYTES; ++i)
    {
        int written = snprintf(destination + used, capacity - used,
            "%02" PRIx8, sample[i]);
        if (written < 0 || (size_t)written >= capacity - used)
            break;
        used += (size_t)written;
    }
}

static void dd_load_history_push(const struct dd_load_history_dma *transfer)
{
    struct dd_load_history_record *record;
    unsigned int index;

    if (recent_count < DD_LOAD_HISTORY_CAPACITY)
    {
        index = (recent_head + recent_count) % DD_LOAD_HISTORY_CAPACITY;
        ++recent_count;
    }
    else
    {
        index = recent_head;
        recent_head = (recent_head + 1) % DD_LOAD_HISTORY_CAPACITY;
        ++overwritten_total;
        ring_wrapped = 1;
    }

    record = &recent[index];
    record->sequence = transfer->sequence;
    record->cart_addr = transfer->cart_addr;
    record->dram_addr = transfer->dram_addr;
    record->requested_length = transfer->requested_length;
    memcpy(record->before, transfer->before, sizeof(record->before));
    memcpy(record->after, transfer->after, sizeof(record->after));
    record->before_length = transfer->before_length;
    record->after_length = transfer->after_length;
    record->sample_clipped = transfer->sample_clipped;
    record->before_out_of_bounds = transfer->before_out_of_bounds;
    record->after_out_of_bounds = transfer->after_out_of_bounds;
    record->source_region = transfer->source_region;
}

static void dd_load_history_clear_ring(void)
{
    recent_head = 0;
    recent_count = 0;
    memset(recent, 0, sizeof(recent));
    ring_wrapped = 0;
}

void dd_load_history_pi_dma_begin(struct dd_load_history_dma *transfer,
                                  const uint8_t *dram,
                                  size_t dram_size,
                                  uint32_t cart_addr,
                                  uint32_t dram_addr,
                                  uint32_t requested_length)
{
    if (transfer == NULL)
        return;
    memset(transfer, 0, sizeof(*transfer));
    if (dd_load_history_callback(NULL) == NULL)
        return;

    transfer->sequence = next_sequence++;
    if (transfer->sequence == 0)
        transfer->sequence = next_sequence++;
    transfer->cart_addr = cart_addr;
    transfer->dram_addr = dram_addr;
    transfer->requested_length = requested_length;
    /* Classify the source ROM window at capture time (pure, testable). */
    transfer->source_region = dd_pi_dma_source_region(cart_addr);
    transfer->sample_clipped =
        requested_length > DD_LOAD_HISTORY_SAMPLE_BYTES;
    transfer->before_length = (uint8_t)dd_load_history_sample(
        dram, dram_size, dram_addr, requested_length, transfer->before);
    transfer->before_out_of_bounds =
        transfer->before_length < (requested_length
            > DD_LOAD_HISTORY_SAMPLE_BYTES
                ? DD_LOAD_HISTORY_SAMPLE_BYTES : requested_length);
    transfer->active = 1;
}

void dd_load_history_pi_dma_complete(struct dd_load_history_dma *transfer,
                                     const uint8_t *dram,
                                     size_t dram_size)
{
    if (transfer == NULL || !transfer->active)
        return;
    if (dd_load_history_callback(NULL) == NULL)
    {
        transfer->active = 0;
        return;
    }

    transfer->after_length = (uint8_t)dd_load_history_sample(
        dram, dram_size, transfer->dram_addr, transfer->requested_length,
        transfer->after);
    transfer->after_out_of_bounds =
        transfer->after_length < (transfer->requested_length
            > DD_LOAD_HISTORY_SAMPLE_BYTES
                ? DD_LOAD_HISTORY_SAMPLE_BYTES
                : transfer->requested_length);
    dd_load_history_push(transfer);
    transfer->active = 0;
}

static void dd_load_history_emit(
    ptr_DdStartupDiagnosticsCallback callback, void *context,
    const struct dd_load_history_record *record)
{
    char before_text[DD_LOAD_HISTORY_SAMPLE_BYTES * 2 + 1];
    char after_text[DD_LOAD_HISTORY_SAMPLE_BYTES * 2 + 1];
    char message[1024];

    dd_load_history_format_sample(before_text, sizeof(before_text),
        record->before, record->before_length);
    dd_load_history_format_sample(after_text, sizeof(after_text),
        record->after, record->after_length);
    (void)snprintf(message, sizeof(message),
        "DDSTART16 PI DMA completion source=DD_PI_DMA_COMPLETION"
        " source_region=%s phase=completion"
        " sequence=%" PRIu32
        " cart_addr=0x%08" PRIx32
        " dram_src=0x%08" PRIu32
        " dram_dst=0x%016" PRIu64
        " requested_length=0x%08" PRIx32
        " length_semantics=requested-not-exact-completed"
        " before_sample_length=%u after_sample_length=%u"
        " sample_clipped=%u"
        " before_out_of_bounds=%u after_out_of_bounds=%u"
        " before_guest_order=%s after_guest_order=%s"
        " sample_provenance=validated-direct-rdram-guest-order",
        record->source_region == DD_DMA_SOURCE_DD_ROM ? "dd_rom" : "cart_rom",
        record->sequence, record->cart_addr, record->dram_addr, (uint64_t)record->dram_addr + record->requested_length,
        record->requested_length, record->before_length,
        record->after_length, record->sample_clipped,
        record->before_out_of_bounds, record->after_out_of_bounds,
        before_text, after_text);
    (*callback)(context, M64MSG_INFO, message);
}

void dd_load_history_flush(const char *reason)
{
    void *context = NULL;
    ptr_DdStartupDiagnosticsCallback callback;
    unsigned int emit_count;
    unsigned int is_zero_audio;
    unsigned int i;
    char message[512];

    callback = dd_load_history_callback(&context);
    if (callback == NULL)
        return;

    is_zero_audio = reason != NULL && strcmp(reason, "zero-audio") == 0;
    if (is_zero_audio)
    {
        if (zero_audio_flushes >= DD_LOAD_HISTORY_ZERO_AUDIO_FLUSH_LIMIT)
        {
            ++suppressed_flushes;
            suppressed_records += recent_count;
            if (!zero_audio_suppression_reported)
            {
                (void)snprintf(message, sizeof(message),
                    "DDSTART16 PI DMA history_flush_suppressed"
                    " reason=zero-audio history_scope=recent-ring-only"
                    " records=%u records_emitted=0"
                    " records_suppressed=%u"
                    " ring_capacity=%u ring_wrapped=%u"
                    " overwritten=%" PRIu64
                    " suppressed_total=%u flushes_suppressed=%u"
                    " suppression=reserved-zero-audio-flush-consumed",
                    recent_count, recent_count, DD_LOAD_HISTORY_CAPACITY,
                    ring_wrapped, overwritten_total, suppressed_records,
                    suppressed_flushes);
                (*callback)(context, M64MSG_INFO, message);
                zero_audio_suppression_reported = 1;
            }
            dd_load_history_clear_ring();
            return;
        }
        ++zero_audio_flushes;
    }
    else if (early_flushes >= DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT)
    {
        /*
         * Keep the ring intact: the reserved zero-audio flush must still be
         * able to report the latest completed transfers after early evidence
         * has reached its own bounded count.
         */
        ++suppressed_flushes;
        return;
    }
    else
        ++early_flushes;

    emit_count = recent_count;
    for (i = 0; i < emit_count; ++i)
    {
        const struct dd_load_history_record *record = &recent[
            (recent_head + i) % DD_LOAD_HISTORY_CAPACITY];
        dd_load_history_emit(callback, context, record);
    }

    (void)snprintf(message, sizeof(message),
        "DDSTART16 PI DMA history_flush reason=%s"
        " history_scope=recent-ring-only records=%u"
        " ring_capacity=%u ring_wrapped=%u overwritten=%" PRIu64
        " records_emitted=%u records_suppressed=0"
        " suppressed_total=%u flushes_suppressed=%u"
        " early_flushes=%u early_flushes_remaining=%u"
        " zero_audio_flushes=%u zero_audio_flushes_remaining=%u",
        reason != NULL ? reason : "unspecified", recent_count,
        DD_LOAD_HISTORY_CAPACITY, ring_wrapped, overwritten_total, emit_count,
        suppressed_records, suppressed_flushes, early_flushes,
        DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT - early_flushes,
        zero_audio_flushes,
        DD_LOAD_HISTORY_ZERO_AUDIO_FLUSH_LIMIT - zero_audio_flushes);
    (*callback)(context, M64MSG_INFO, message);
    dd_load_history_clear_ring();
}

void dd_load_history_reset(void)
{
    memset(recent, 0, sizeof(recent));
    recent_head = 0;
    recent_count = 0;
    next_sequence = 1;
    overwritten_total = 0;
    ring_wrapped = 0;
    suppressed_records = 0;
    suppressed_flushes = 0;
    early_flushes = 0;
    zero_audio_flushes = 0;
    zero_audio_suppression_reported = 0;
}