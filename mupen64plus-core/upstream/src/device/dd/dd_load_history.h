#ifndef M64P_DEVICE_DD_LOAD_HISTORY_H
#define M64P_DEVICE_DD_LOAD_HISTORY_H

#include <stddef.h>
#include <stdint.h>

/*
 * This is deliberately a small completion history, rather than a DMA trace.
 * A producer can issue many PI transfers before the owner reaches the
 * transition boundary where the evidence is useful.
 */
#define DD_LOAD_HISTORY_CAPACITY 16u
#define DD_LOAD_HISTORY_SAMPLE_BYTES 32u

/*
 * Keep the accepted load-clear call/entry observations bounded without
 * allowing them to consume the separately reserved zero-audio observation.
 * Every accepted flush can emit the complete 16-record ring plus one summary.
 */
#define DD_LOAD_HISTORY_EARLY_FLUSH_LIMIT 7u
#define DD_LOAD_HISTORY_ZERO_AUDIO_FLUSH_LIMIT 1u

/*
 * A transfer is filled at the PI handler boundary.  The before sample is
 * taken before the production handler is entered and the after sample is
 * taken after it returns.  `requested_length` is the length supplied to that
 * handler; the handler has no byte-count return channel, so it must not be
 * described as an exact completed length.
 */
struct dd_load_history_dma
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
    uint8_t after_out_of_bounds;
    uint8_t active;
};

/*
 * These two calls bracket handler->dma_write().  They are intentionally
 * observer-only: the production handler remains the operation that mutates
 * RDRAM and determines PI timing.
 */
void dd_load_history_pi_dma_begin(struct dd_load_history_dma *transfer,
                                  const uint8_t *dram,
                                  size_t dram_size,
                                  uint32_t cart_addr,
                                  uint32_t dram_addr,
                                  uint32_t requested_length);
void dd_load_history_pi_dma_complete(struct dd_load_history_dma *transfer,
                                     const uint8_t *dram,
                                     size_t dram_size);

/*
 * The DD command/load owner calls reset at the start of a session.  It calls
 * flush at a narrow transition boundary (for example a clear-entry or a
 * zero-audio boundary).  Flush emits only the current recent ring.
 */
void dd_load_history_reset(void);
void dd_load_history_flush(const char *reason);

#endif /* M64P_DEVICE_DD_LOAD_HISTORY_H */