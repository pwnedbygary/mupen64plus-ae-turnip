/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus - rsp_core.c                                              *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "rsp_core.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "device/memory/memory.h"
#include "device/dd/dd_cmd_watch.h"
#include "device/r4300/r4300_core.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rcp/rdp/rdp_core.h"
#include "device/rcp/ri/ri_controller.h"
#include "device/rdram/rdram.h"
#include "main/main.h"
#if defined(PROFILE)
#include "main/profile.h"
#endif
#include "plugin/plugin.h"
#include "api/callbacks.h"

/*
 * DDSTART11 is deliberately a core-side observer.  It records only the
 * CPU-to-IMEM form of an SP DMA, after the normal DMA descriptor has been
 * captured and before the normal copy starts.  In particular, this must not
 * become another DMA implementation: all address masking, copying, and
 * completion scheduling remain in do_sp_dma().
 */
enum
{
    DD_IMEM_DMA_RECORD_LIMIT = 256,
    DD_IMEM_DMA_DEDUP_LIMIT = 512,
    DD_IMEM_DMA_SAMPLE_BYTES = 16,
    /*
     * DDSTART13 is a per-RSP-lifecycle launch stream, separate from the
     * legacy 256-message DebugMessage budget and from all other trace
     * classes.  A normal native run has fewer than 900 audio entries; leave
     * room for a complete run without making the diagnostic unbounded.
     */
    DD_SP_LAUNCH_RECORD_LIMIT = 2048
};

struct dd_imem_dma_identity
{
    int used;
    uint32_t raw_memaddr;
    uint32_t raw_dramaddr;
    uint32_t raw_rd_len;
    uint32_t raw_wr_len;
    uint32_t programmed_dir;
    uint32_t programmed_memaddr;
    uint32_t programmed_dramaddr;
    uint32_t programmed_length;
    uint64_t payload_hash;
    uint64_t before_hash;
    uint64_t after_hash;
};

struct dd_imem_dma_observation
{
    int active;
    int valid;
    unsigned int sequence;
    uint32_t raw_memaddr;
    uint32_t raw_dramaddr;
    uint32_t raw_rd_len;
    uint32_t raw_wr_len;
    uint32_t programmed_dir;
    uint32_t programmed_memaddr;
    uint32_t programmed_dramaddr;
    uint32_t programmed_length;
    unsigned int decoded_length;
    unsigned int decoded_count;
    unsigned int decoded_skip;
    unsigned int memaddr;
    unsigned int dramaddr;
    unsigned int dest_bank;
    size_t dest_start;
    size_t total_bytes;
    size_t imem_start;
    size_t imem_end;
    unsigned int probe_memaddr;
    size_t probe_bytes;
    unsigned int payload_sample_count;
    unsigned int before_sample_count;
    unsigned int after_sample_count;
    unsigned char payload_sample[DD_IMEM_DMA_SAMPLE_BYTES];
    unsigned char before_sample[DD_IMEM_DMA_SAMPLE_BYTES];
    unsigned char after_sample[DD_IMEM_DMA_SAMPLE_BYTES];
    uint64_t payload_hash;
    uint64_t before_hash;
    uint64_t after_hash;
    unsigned char *spmem;
};

static struct dd_imem_dma_identity dd_imem_dma_identities[DD_IMEM_DMA_DEDUP_LIMIT];
static unsigned int dd_imem_dma_sequence;
static unsigned int dd_imem_dma_record_count;
static int dd_imem_dma_session_active;
static int dd_imem_dma_record_exhausted;
static int dd_imem_dma_dedup_exhausted;
static unsigned int dd_sp_launch_sequence;
static unsigned int dd_sp_launch_remaining;
static int dd_sp_launch_exhausted;

static uint64_t dd_imem_dma_hash_init(void)
{
    return UINT64_C(14695981039346656037);
}

static uint64_t dd_imem_dma_hash_byte(uint64_t hash, unsigned char value)
{
    return (hash ^ value) * UINT64_C(1099511628211);
}

static uint64_t dd_imem_dma_hash_u32(uint64_t hash, uint32_t value)
{
    unsigned int i;

    for (i = 0; i < 4; ++i)
        hash = dd_imem_dma_hash_byte(hash, (unsigned char)(value >> (i * 8)));
    return hash;
}

static uint64_t dd_imem_dma_hash_u64(uint64_t hash, uint64_t value)
{
    unsigned int i;

    for (i = 0; i < 8; ++i)
        hash = dd_imem_dma_hash_byte(hash, (unsigned char)(value >> (i * 8)));
    return hash;
}

static void dd_imem_dma_reset_observer(void)
{
    memset(dd_imem_dma_identities, 0, sizeof(dd_imem_dma_identities));
    dd_imem_dma_sequence = 0;
    dd_imem_dma_record_count = 0;
    dd_imem_dma_session_active = 0;
    dd_imem_dma_record_exhausted = 0;
    dd_imem_dma_dedup_exhausted = 0;
}

static int dd_imem_dma_identity_equal(
    const struct dd_imem_dma_identity *identity,
    const struct dd_imem_dma_observation *observation)
{
    return identity->raw_memaddr == observation->raw_memaddr
        && identity->raw_dramaddr == observation->raw_dramaddr
        && identity->raw_rd_len == observation->raw_rd_len
        && identity->raw_wr_len == observation->raw_wr_len
        && identity->programmed_dir == observation->programmed_dir
        && identity->programmed_memaddr == observation->programmed_memaddr
        && identity->programmed_dramaddr == observation->programmed_dramaddr
        && identity->programmed_length == observation->programmed_length
        && identity->payload_hash == observation->payload_hash
        && identity->before_hash == observation->before_hash
        && identity->after_hash == observation->after_hash;
}

static uint64_t dd_imem_dma_identity_hash(
    const struct dd_imem_dma_observation *observation)
{
    uint64_t hash = dd_imem_dma_hash_init();

    hash = dd_imem_dma_hash_u32(hash, observation->raw_memaddr);
    hash = dd_imem_dma_hash_u32(hash, observation->raw_dramaddr);
    hash = dd_imem_dma_hash_u32(hash, observation->raw_rd_len);
    hash = dd_imem_dma_hash_u32(hash, observation->raw_wr_len);
    hash = dd_imem_dma_hash_u32(hash, observation->programmed_dir);
    hash = dd_imem_dma_hash_u32(hash, observation->programmed_memaddr);
    hash = dd_imem_dma_hash_u32(hash, observation->programmed_dramaddr);
    hash = dd_imem_dma_hash_u32(hash, observation->programmed_length);
    hash = dd_imem_dma_hash_u64(hash, observation->payload_hash);
    hash = dd_imem_dma_hash_u64(hash, observation->before_hash);
    return dd_imem_dma_hash_u64(hash, observation->after_hash);
}

/*
 * Return 1 for a newly inserted identity, 0 for a repeated identity, and -1
 * when the bounded table is full.  The table is intentionally larger than the
 * output budget: repeated frame DMA traffic therefore does not spend the
 * output budget or emit an exhaustion message on every frame.
 */
static int dd_imem_dma_claim_identity(
    const struct dd_imem_dma_observation *observation)
{
    uint64_t hash = dd_imem_dma_identity_hash(observation);
    unsigned int start = (unsigned int)(hash % DD_IMEM_DMA_DEDUP_LIMIT);
    unsigned int i;

    for (i = 0; i < DD_IMEM_DMA_DEDUP_LIMIT; ++i) {
        unsigned int index = (start + i) % DD_IMEM_DMA_DEDUP_LIMIT;
        struct dd_imem_dma_identity *identity = &dd_imem_dma_identities[index];

        if (!identity->used) {
            identity->used = 1;
            identity->raw_memaddr = observation->raw_memaddr;
            identity->raw_dramaddr = observation->raw_dramaddr;
            identity->raw_rd_len = observation->raw_rd_len;
            identity->raw_wr_len = observation->raw_wr_len;
            identity->programmed_dir = observation->programmed_dir;
            identity->programmed_memaddr = observation->programmed_memaddr;
            identity->programmed_dramaddr = observation->programmed_dramaddr;
            identity->programmed_length = observation->programmed_length;
            identity->payload_hash = observation->payload_hash;
            identity->before_hash = observation->before_hash;
            identity->after_hash = observation->after_hash;
            return 1;
        }
        if (dd_imem_dma_identity_equal(identity, observation))
            return 0;
    }

    return -1;
}

static void dd_imem_dma_emit_sample(char *text, size_t text_size,
                                    const unsigned char *sample,
                                    unsigned int sample_count)
{
    unsigned int i;

    if (text_size == 0)
        return;
    text[0] = '\0';
    for (i = 0; i < sample_count && (size_t)(i * 2 + 2) <= text_size; ++i)
        (void)snprintf(text + i * 2, text_size - i * 2, "%02x", sample[i]);
}

static void dd_imem_dma_emit_message(const char *message)
{
    void *context = NULL;
    ptr_DdStartupDiagnosticsCallback callback =
        DdStartupDiagnosticsGetCallback(&context);

    if (callback != NULL)
        (*callback)(context, M64MSG_INFO, message);
}

/*
 * P08c-writer (2/2): the command-buffer state AT SUBMISSION.
 *
 * The fetch trace bounds the zeroing to a ~21 ms window between two
 * consecutive audio entries, and the remaining question is binary: was the
 * buffer already empty when the guest submitted this task, or was it valid
 * and then emptied before the microcode fetched it?  This observer answers
 * exactly that and nothing more: for an audio task under the per-game DD
 * policy it hashes the command buffer named by the task words (12/13 =
 * data_ptr/data_size, the corrected P07-C field map) and emits one line per
 * entry, with the count of non-zero words so "all zero" is directly
 * readable rather than inferred from a hash constant.
 *
 * It observes no stores and changes nothing: no register, no memory, no
 * scheduling.  Gated on the DD diagnostics callback (the same channel the
 * DDSTART11 observer uses) AND the per-game DD policy, so a DD-disabled
 * session emits nothing.
 *
 * Coverage limit: this reports the buffer's state when the task is
 * submitted.  It cannot see the writes that produced that state — the
 * dynarec's fast path is inlined into generated code — so a store-level
 * attribution still needs the fast-path instrumentation the work-packages
 * guide describes.  What it does give is the branch: which side of the
 * submission the emptiness appears on.
 */
void dd_cmd_entry_hash_observe(struct rsp_core* sp)
{
    const uint32_t *task_words;
    struct rdram *rdram;
    uint32_t data_ptr;
    uint32_t data_size;
    uint32_t words;
    uint32_t i;
    uint32_t nonzero_words = 0;
    uint64_t hash = 0xcbf29ce484222325ull; /* FNV-1, word-wise */
    char message[192];

    if (!DdStartupDiagnosticsEnabled() || !DdRuntimePolicyGet())
        return;
    if (sp == NULL || sp->mi == NULL || sp->mi->r4300 == NULL
        || sp->mi->r4300->rdram == NULL || sp->mi->r4300->rdram->dram == NULL)
        return; /* fail closed, as dd_imem_dma_source_is_valid does */

    rdram = sp->mi->r4300->rdram;
    task_words = sp->mem + (0xfc0 / 4);
    data_ptr = task_words[12] & 0x1FFFFFFFu; /* KSEG0/KSEG1 -> physical */
    /* A misaligned data_ptr hashes the words CONTAINING it: the read range is
     * [data_ptr & ~3, (data_ptr & ~3) + data_size), while the emitted field
     * echoes the value the task carried.  RSP DMA DRAM addresses are 8-byte
     * aligned in practice, so this is documentation, not a defect. */
    data_size = task_words[13];
    if (data_size == 0 || (data_size & 3u) != 0)
        return;
    if ((uint64_t) data_ptr + (uint64_t) data_size > (uint64_t) rdram->dram_size)
        return; /* outside RDRAM: nothing the guest could have written */

    words = data_size / 4;
    for (i = 0; i < words; ++i)
    {
        const uint32_t word = rdram->dram[(data_ptr >> 2) + i];
        if (word != 0)
            ++nonzero_words;
        hash = (hash * 0x100000001b3ull) ^ word;
    }

    (void) snprintf(message, sizeof(message),
        "DDSTART12 RSP cmd_entry data_ptr=0x%08x data_size=0x%08x "
        "hash=0x%016llx nonzero_words=%u words=%u",
        data_ptr, data_size, (unsigned long long) hash, nonzero_words, words);
    dd_imem_dma_emit_message(message);
}

/*
 * P08c-writer (part 3): observe the guest's actual SP launch boundary.
 *
 * This is deliberately at the hardware-facing write_rsp_regs ->
 * update_sp_status seam, after the status write has cleared HALT and before
 * do_SP_Task runs.  It is not a CPU-store observer and it has no exact guest
 * PC: the callback API does not carry one, and inventing a PC here would make
 * the evidence misleading.  The record is limited to audio tasks under the
 * explicit DD policy.  DMA-busy/full status is retained in the record so it
 * is not mistaken for a clean launch; the pre-existing status implementation
 * continues to execute its normal path unchanged.
 *
 * The task descriptor is copied into the message in full.  For a valid,
 * aligned RDRAM range, the command buffer is hashed with the same word-wise
 * FNV-1 scheme as DDSTART12 and its non-zero word count is emitted.  Invalid
 * backing/range/shape data still emits the full descriptor with
 * buffer_valid=0, rather than silently reading outside RDRAM.
 */
static void dd_sp_launch_reset_observer(void)
{
    dd_sp_launch_sequence = 0;
    dd_sp_launch_remaining = DD_SP_LAUNCH_RECORD_LIMIT;
    dd_sp_launch_exhausted = 0;
}

static void dd_sp_launch_observe(struct rsp_core *sp,
                                 uint32_t status_before,
                                 uint32_t status_after)
{
    const uint32_t *task_words;
    struct rdram *rdram = NULL;
    ptr_DdStartupDiagnosticsCallback callback;
    void *context = NULL;
    uint32_t data_ptr = 0;
    uint32_t data_size = 0;
    uint32_t buffer_words = 0;
    uint32_t nonzero_words = 0;
    uint64_t buffer_hash = 0;
    uint32_t descriptor[16];
    unsigned int sequence;
    unsigned int i;
    int buffer_valid = 0;
    unsigned int dma_busy;
    unsigned int dma_full;
    /*
     * Sixteen labeled descriptor words plus the buffer summary are larger
     * than the legacy DebugMessage payload in the worst case.  Keep this
     * below the callback fixtures' 1024-byte capture and format it in one
     * bounded stack buffer so the final words/summary cannot be truncated.
     */
    char message[1024];

    /*
     * Keep this gate ahead of all descriptor/RDRAM work.  In particular,
     * DD-off sessions do not pay for a task snapshot or hash.
     */
    if (!DdStartupDiagnosticsEnabled() || !DdRuntimePolicyGet())
        return;

    callback = DdStartupDiagnosticsGetCallback(&context);
    if (callback == NULL)
        return;

    if (sp == NULL || sp->mem == NULL)
        return;

    task_words = sp->mem + (0xfc0 / 4);
    for (i = 0; i < 16; ++i)
        descriptor[i] = task_words[i];

    /*
     * The task type check is intentionally after the gate but before any
     * RDRAM walk: non-audio SP launches are outside this DD command-buffer
     * diagnostic and must not consume its per-session sequence/budget.
     */
    if (descriptor[0] != 2u)
        return;

    /*
     * Preserve the first 2048 records, then report one explicit exhaustion
     * marker on the next qualifying launch.  Non-audio launches do not
     * consume the budget or trigger the marker.
     */
    if (dd_sp_launch_remaining == 0)
    {
        if (!dd_sp_launch_exhausted)
        {
            (void) snprintf(message, sizeof(message),
                "DDSTART13 RSP launch_exhausted sequence=%u records=%u limit=%u",
                dd_sp_launch_sequence + 1, dd_sp_launch_sequence,
                DD_SP_LAUNCH_RECORD_LIMIT);
            dd_sp_launch_exhausted = 1;
            (*callback)(context, M64MSG_INFO, message);
        }
        return;
    }

    if (sp->mi != NULL && sp->mi->r4300 != NULL
            && sp->mi->r4300->rdram != NULL)
    {
        rdram = sp->mi->r4300->rdram;
        data_ptr = descriptor[12] & 0x1FFFFFFFu;
        data_size = descriptor[13];
        if (rdram->dram != NULL && (data_ptr & 3u) == 0
                && data_size != 0 && (data_size & 3u) == 0
                && (uint64_t)data_ptr + (uint64_t)data_size
                    <= (uint64_t)rdram->dram_size)
        {
            const uint32_t *buffer = rdram->dram + (data_ptr >> 2);

            buffer_words = data_size / 4;
            buffer_hash = UINT64_C(14695981039346656037);
            for (i = 0; i < buffer_words; ++i)
            {
                if (buffer[i] != 0)
                    ++nonzero_words;
                buffer_hash = (buffer_hash * UINT64_C(1099511628211))
                    ^ buffer[i];
            }
            buffer_valid = 1;
        }
    }

    sequence = ++dd_sp_launch_sequence;
    --dd_sp_launch_remaining;
    dd_cmd_watch_task_entry(descriptor, sequence, buffer_valid, nonzero_words);
    dma_busy = ((status_before | status_after) & SP_STATUS_DMA_BUSY) != 0;
    dma_full = ((status_before | status_after) & SP_STATUS_DMA_FULL) != 0;
    (void) snprintf(message, sizeof(message),
        "DDSTART13 RSP launch sequence=%u type=%u "
        "status_before=0x%08" PRIx32 " status_after=0x%08" PRIx32
        " busy=%u dma_busy=%u dma_full=%u clear_halt=1 descriptor={"
        "w0=0x%08" PRIx32 " w1=0x%08" PRIx32
        " w2=0x%08" PRIx32 " w3=0x%08" PRIx32
        " w4=0x%08" PRIx32 " w5=0x%08" PRIx32
        " w6=0x%08" PRIx32 " w7=0x%08" PRIx32
        " w8=0x%08" PRIx32 " w9=0x%08" PRIx32
        " w10=0x%08" PRIx32 " w11=0x%08" PRIx32
        " w12=0x%08" PRIx32 " w13=0x%08" PRIx32
        " w14=0x%08" PRIx32 " w15=0x%08" PRIx32 "}"
        " buffer={valid=%d data_ptr=0x%08" PRIx32
        " data_size=0x%08" PRIx32 " hash=0x%016" PRIx64
        " nonzero_words=%u words=%u}",
        sequence, descriptor[0], status_before, status_after,
        dma_busy || dma_full, dma_busy, dma_full,
        descriptor[0], descriptor[1], descriptor[2], descriptor[3],
        descriptor[4], descriptor[5], descriptor[6], descriptor[7],
        descriptor[8], descriptor[9], descriptor[10], descriptor[11],
        descriptor[12], descriptor[13], descriptor[14], descriptor[15],
        buffer_valid, data_ptr, data_size, buffer_hash, nonzero_words,
        buffer_words);
    (*callback)(context, M64MSG_INFO, message);
}

static void dd_imem_dma_emit_exhaustion(const char *reason,
                                        unsigned int sequence,
                                        unsigned int count,
                                        unsigned int limit)
{
    char message[192];

    (void)snprintf(message, sizeof(message),
        "DDSTART11 IMEMDMA exhaustion reason=%s sequence=%u records=%u limit=%u",
        reason, sequence, count, limit);
    dd_imem_dma_emit_message(message);
}

static int dd_imem_dma_source_is_valid(const struct rdram *rdram,
                                       uint32_t dramaddr,
                                       unsigned int length,
                                       unsigned int count,
                                       unsigned int skip)
{
    uint64_t current = dramaddr;
    unsigned int row;

    if (rdram == NULL || rdram->dram == NULL)
        return 0;

    /*
     * DMA reads each source byte through (address ^ S8).  A row can therefore
     * touch up to three bytes past its logical end when its start is not
     * word-aligned.  Check that conservative bound before any probe read.
     */
    for (row = 0; row < count; ++row) {
        if (current > UINT32_MAX
                || current >= rdram->dram_size
                || (uint64_t)length + 3 > rdram->dram_size - current)
            return 0;
        current += (uint64_t)length + skip;
    }
    return 1;
}

static void dd_imem_dma_hash_payload(const unsigned char *dram,
                                     uint32_t dramaddr,
                                     unsigned int length,
                                     unsigned int count,
                                     unsigned int skip,
                                     uint64_t *hash,
                                     unsigned char *sample,
                                     unsigned int *sample_count)
{
    uint64_t current = dramaddr;
    unsigned int row;
    unsigned int copied = 0;
    unsigned int i;

    *hash = dd_imem_dma_hash_init();
    for (row = 0; row < count; ++row) {
        for (i = 0; i < length; ++i) {
            unsigned char value = dram[((uint32_t)(current + i)) ^ S8];

            *hash = dd_imem_dma_hash_byte(*hash, value);
            if (copied < DD_IMEM_DMA_SAMPLE_BYTES)
                sample[copied++] = value;
        }
        current += (uint64_t)length + skip;
    }
    *sample_count = copied;
}

static void dd_imem_dma_hash_destination(const unsigned char *spmem,
                                         unsigned int memaddr,
                                         size_t total_bytes,
                                         uint64_t *hash,
                                         unsigned char *sample,
                                         unsigned int *sample_count)
{
    size_t i;
    unsigned int copied = 0;

    *hash = dd_imem_dma_hash_init();
    for (i = 0; i < total_bytes; ++i) {
        unsigned char value = spmem[(memaddr + i) ^ S8];

        *hash = dd_imem_dma_hash_byte(*hash, value);
        if (copied < DD_IMEM_DMA_SAMPLE_BYTES)
            sample[copied++] = value;
    }
    *sample_count = copied;
}

static void dd_imem_dma_begin(struct rsp_core *sp,
                              const struct sp_dma *dma,
                              unsigned int length,
                              unsigned int count,
                              unsigned int skip,
                              unsigned int memaddr,
                              unsigned int dramaddr,
                              unsigned char *spmem,
                              struct dd_imem_dma_observation *observation)
{
    size_t destination_end;
    int source_valid;
    int destination_valid;
    int imem_intersects;

    if (!DdStartupDiagnosticsEnabled())
        return;

    memset(observation, 0, sizeof(*observation));
    if (!dd_imem_dma_session_active) {
        dd_imem_dma_reset_observer();
        dd_imem_dma_session_active = 1;
    }

    if (dma->dir != SP_DMA_WRITE)
        return;

    observation->sequence = ++dd_imem_dma_sequence;
    observation->raw_memaddr = sp->regs[SP_MEM_ADDR_REG];
    observation->raw_dramaddr = sp->regs[SP_DRAM_ADDR_REG];
    observation->raw_rd_len = sp->regs[SP_RD_LEN_REG];
    observation->raw_wr_len = sp->regs[SP_WR_LEN_REG];
    observation->programmed_dir = dma->dir;
    observation->programmed_memaddr = dma->memaddr;
    observation->programmed_dramaddr = dma->dramaddr;
    observation->programmed_length = dma->length;
    observation->decoded_length = length;
    observation->decoded_count = count;
    observation->decoded_skip = skip;
    observation->memaddr = memaddr;
    observation->dramaddr = dramaddr;
    observation->dest_bank = dma->memaddr & 0x1000;
    observation->dest_start = observation->dest_bank + memaddr;
    observation->total_bytes = (size_t)length * count;
    observation->spmem = spmem;

    destination_end = observation->dest_start + observation->total_bytes;
    imem_intersects = observation->dest_start < 0x2000
        && destination_end > 0x1000
        && destination_end >= observation->dest_start;
    if (!imem_intersects)
        return;

    observation->active = 1;
    observation->imem_start = observation->dest_start < 0x1000
        ? 0x1000 : observation->dest_start;
    observation->imem_end = destination_end > 0x2000
        ? 0x2000 : destination_end;
    observation->probe_memaddr =
        (unsigned int)(observation->imem_start - observation->dest_bank);
    observation->probe_bytes = observation->imem_end - observation->imem_start;

    /*
     * Keep raw/programmed metadata even when a probe range is not safe.  The
     * actual DMA remains responsible for its existing behavior; diagnostics
     * simply mark payload/IMEM samples skipped rather than reading outside
     * allocated RDRAM or SP memory.
     */
    destination_valid = sp != NULL && sp->mem != NULL
        && observation->dest_start <= SP_MEM_SIZE;
    if (destination_valid)
        destination_valid = destination_end >= observation->dest_start
            && destination_end <= SP_MEM_SIZE;

    source_valid = sp != NULL && sp->ri != NULL
        && dd_imem_dma_source_is_valid(sp->ri->rdram, dramaddr, length,
                                       count, skip);
    observation->valid = source_valid && destination_valid;
    if (observation->valid) {
        dd_imem_dma_hash_payload(
            (const unsigned char *)sp->ri->rdram->dram, dramaddr, length,
            count, skip, &observation->payload_hash,
            observation->payload_sample, &observation->payload_sample_count);
        dd_imem_dma_hash_destination(
            spmem, observation->probe_memaddr, observation->probe_bytes,
            &observation->before_hash, observation->before_sample,
            &observation->before_sample_count);
    }
}

static void dd_imem_dma_finish(struct dd_imem_dma_observation *observation)
{
    char payload_sample[DD_IMEM_DMA_SAMPLE_BYTES * 2 + 1];
    char before_sample[DD_IMEM_DMA_SAMPLE_BYTES * 2 + 1];
    char after_sample[DD_IMEM_DMA_SAMPLE_BYTES * 2 + 1];
    char message[512];
    int identity;
    unsigned int record;
    size_t destination_end;

    if (!observation->active || !DdStartupDiagnosticsEnabled())
        return;

    if (observation->valid) {
        dd_imem_dma_hash_destination(
            observation->spmem, observation->probe_memaddr,
            observation->probe_bytes, &observation->after_hash,
            observation->after_sample, &observation->after_sample_count);
    }

    identity = dd_imem_dma_claim_identity(observation);
    if (identity < 0) {
        if (!dd_imem_dma_dedup_exhausted) {
            dd_imem_dma_dedup_exhausted = 1;
            dd_imem_dma_emit_exhaustion("dedup-table", observation->sequence,
                                        DD_IMEM_DMA_DEDUP_LIMIT,
                                        DD_IMEM_DMA_DEDUP_LIMIT);
        }
        return;
    }
    if (identity == 0)
        return;
    if (dd_imem_dma_record_count >= DD_IMEM_DMA_RECORD_LIMIT) {
        if (!dd_imem_dma_record_exhausted) {
            dd_imem_dma_record_exhausted = 1;
            dd_imem_dma_emit_exhaustion("record-budget", observation->sequence,
                                        dd_imem_dma_record_count,
                                        DD_IMEM_DMA_RECORD_LIMIT);
        }
        return;
    }

    record = ++dd_imem_dma_record_count;
    destination_end = observation->dest_start + observation->total_bytes;
    dd_imem_dma_emit_sample(payload_sample, sizeof(payload_sample),
                            observation->payload_sample,
                            observation->valid
                                ? observation->payload_sample_count : 0);
    dd_imem_dma_emit_sample(before_sample, sizeof(before_sample),
                            observation->before_sample,
                            observation->valid
                                ? observation->before_sample_count : 0);
    dd_imem_dma_emit_sample(after_sample, sizeof(after_sample),
                            observation->after_sample,
                            observation->valid
                                ? observation->after_sample_count : 0);
    (void)snprintf(message, sizeof(message),
        "DDSTART11 RSP CPU->IMEM seq=%u rec=%u dir=%u "
        "raw={m=0x%08" PRIx32 " d=0x%08" PRIx32
        " rd=0x%08" PRIx32 " wr=0x%08" PRIx32 "}"
        " prog={m=0x%08" PRIx32 " d=0x%08" PRIx32
        " l=0x%08" PRIx32 "} decoded={l=%u c=%u s=%u} "
        "dest={bank=%s range=[0x%04zx,0x%04zx)"
        " imem=[0x%04zx,0x%04zx) probe=%s} "
        "payload={h=0x%016" PRIx64 " s=%s} "
        "before={h=0x%016" PRIx64 " s=%s} "
        "after={h=0x%016" PRIx64 " s=%s}",
        observation->sequence, record, observation->programmed_dir,
        observation->raw_memaddr, observation->raw_dramaddr,
        observation->raw_rd_len, observation->raw_wr_len,
        observation->programmed_memaddr, observation->programmed_dramaddr,
        observation->programmed_length, observation->decoded_length,
        observation->decoded_count, observation->decoded_skip,
        observation->dest_bank == 0x1000 ? "IMEM" : "DMEM",
        observation->dest_start, destination_end,
        observation->imem_start, observation->imem_end,
        observation->valid ? "valid" : "skipped",
        observation->payload_hash, payload_sample,
        observation->before_hash, before_sample,
        observation->after_hash, after_sample);
    dd_imem_dma_emit_message(message);
}

static void do_sp_dma(struct rsp_core* sp, const struct sp_dma* dma)
{
    unsigned int i,j;

    unsigned int l = dma->length;

    unsigned int length = ((l & 0xfff) | 7) + 1;
    unsigned int count = ((l >> 12) & 0xff) + 1;
    unsigned int skip = ((l >> 20) & 0xfff);

    unsigned int memaddr = dma->memaddr & 0xff8;
    unsigned int dramaddr = dma->dramaddr & 0xfffff8;

    unsigned char *spmem = (unsigned char*)sp->mem + (dma->memaddr & 0x1000);
    unsigned char *dram = (unsigned char*)sp->ri->rdram->dram;
    struct dd_imem_dma_observation dd_observation;
    int dd_observe = DdStartupDiagnosticsEnabled();

    if (dd_observe)
        dd_imem_dma_begin(sp, dma, length, count, skip, memaddr, dramaddr,
                          spmem, &dd_observation);

    if (dma->dir == SP_DMA_READ)
    {
        for(j=0; j<count; j++) {
            for(i=0; i<length; i++) {
                dram[dramaddr^S8] = spmem[memaddr^S8];
                memaddr++;
                dramaddr++;
            }

            post_framebuffer_write(&sp->dp->fb, dramaddr - length, length);
            dramaddr+=skip;
        }
    }
    else
    {
        for(j=0; j<count; j++) {
            pre_framebuffer_read(&sp->dp->fb, dramaddr);

            for(i=0; i<length; i++) {
                spmem[memaddr^S8] = dram[dramaddr^S8];
                memaddr++;
                dramaddr++;
            }
            dramaddr+=skip;
        }
    }

    if (dd_observe)
        dd_imem_dma_finish(&dd_observation);

    /* schedule end of dma event */
    cp0_update_count(sp->mi->r4300);
    add_interrupt_event(&sp->mi->r4300->cp0, RSP_DMA_EVT, (count * length) / 8);
}

static void fifo_push(struct rsp_core* sp, uint32_t dir)
{
    if (sp->regs[SP_DMA_FULL_REG])
    {
        DebugMessage(M64MSG_WARNING, "RSP DMA attempted but FIFO queue already full.");
        return;
    }

    if (sp->regs[SP_DMA_BUSY_REG])
    {
        sp->fifo[1].dir = dir;
        sp->fifo[1].length = dir == SP_DMA_READ ? sp->regs[SP_WR_LEN_REG] : sp->regs[SP_RD_LEN_REG];
        sp->fifo[1].memaddr = sp->regs[SP_MEM_ADDR_REG];
        sp->fifo[1].dramaddr = sp->regs[SP_DRAM_ADDR_REG];
        sp->regs[SP_DMA_FULL_REG] = 1;
        sp->regs[SP_STATUS_REG] |= SP_STATUS_DMA_FULL;
    }
    else
    {
        sp->fifo[0].dir = dir;
        sp->fifo[0].length = dir == SP_DMA_READ ? sp->regs[SP_WR_LEN_REG] : sp->regs[SP_RD_LEN_REG];
        sp->fifo[0].memaddr = sp->regs[SP_MEM_ADDR_REG];
        sp->fifo[0].dramaddr = sp->regs[SP_DRAM_ADDR_REG];
        sp->regs[SP_DMA_BUSY_REG] = 1;
        sp->regs[SP_STATUS_REG] |= SP_STATUS_DMA_BUSY;

        do_sp_dma(sp, &sp->fifo[0]);
    }
}

static void fifo_pop(struct rsp_core* sp)
{
    if (sp->regs[SP_DMA_FULL_REG])
    {
        sp->fifo[0].dir = sp->fifo[1].dir;
        sp->fifo[0].length = sp->fifo[1].length;
        sp->fifo[0].memaddr = sp->fifo[1].memaddr;
        sp->fifo[0].dramaddr = sp->fifo[1].dramaddr;
        sp->regs[SP_DMA_FULL_REG] = 0;
        sp->regs[SP_STATUS_REG] &= ~SP_STATUS_DMA_FULL;

        do_sp_dma(sp, &sp->fifo[0]);
    }
    else
    {
        sp->regs[SP_DMA_BUSY_REG] = 0;
        sp->regs[SP_STATUS_REG] &= ~SP_STATUS_DMA_BUSY;
    }
}

static void update_sp_status(struct rsp_core* sp, uint32_t w)
{
    const uint32_t status_before = sp->regs[SP_STATUS_REG];

    /* clear / set halt */
    if (w & 0x1) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_HALT;
    if (w & 0x2) sp->regs[SP_STATUS_REG] |= SP_STATUS_HALT;

    /* clear broke */
    if (w & 0x4) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_BROKE;

    /* clear SP interrupt */
    if (w & 0x8)
    {
        clear_rcp_interrupt(sp->mi, MI_INTR_SP);
    }
    /* set SP interrupt */
    if (w & 0x10)
    {
        signal_rcp_interrupt(sp->mi, MI_INTR_SP);
    }

    /* clear / set single step */
    if (w & 0x20) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SSTEP;
    if (w & 0x40) sp->regs[SP_STATUS_REG] |= SP_STATUS_SSTEP;

    /* clear / set interrupt on break */
    if (w & 0x80) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_INTR_BREAK;
    if (w & 0x100) sp->regs[SP_STATUS_REG] |= SP_STATUS_INTR_BREAK;

    /* clear / set signal 0 */
    if (w & 0x200) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG0;
    if (w & 0x400) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG0;

    /* clear / set signal 1 */
    if (w & 0x800) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG1;
    if (w & 0x1000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG1;

    /* clear / set signal 2 */
    if (w & 0x2000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG2;
    if (w & 0x4000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG2;

    /* clear / set signal 3 */
    if (w & 0x8000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG3;
    if (w & 0x10000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG3;

    /* clear / set signal 4 */
    if (w & 0x20000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG4;
    if (w & 0x40000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG4;

    /* clear / set signal 5 */
    if (w & 0x80000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG5;
    if (w & 0x100000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG5;

    /* clear / set signal 6 */
    if (w & 0x200000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG6;
    if (w & 0x400000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG6;

    /* clear / set signal 7 */
    if (w & 0x800000) sp->regs[SP_STATUS_REG] &= ~SP_STATUS_SIG7;
    if (w & 0x1000000) sp->regs[SP_STATUS_REG] |= SP_STATUS_SIG7;

    if (sp->rsp_task_locked && (get_event(&sp->mi->r4300->cp0.q, SP_INT))) return;
    if (!(w & 0x1) && !(w & 0x4) && !sp->rsp_task_locked)
        return;

    if (!(sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)))
    {
        const uint32_t status_after = sp->regs[SP_STATUS_REG];

        /*
         * This is the one production path where the guest has just cleared
         * an asserted HALT and the core is about to execute the task.  The
         * observer records DMA busy/full bits as they actually appear in the
         * status instead of changing or filtering the existing execution
         * decision.  It remains strictly before the unchanged do_SP_Task
         * call.
         */
        if ((w & SP_STATUS_HALT) != 0
                && (status_before & SP_STATUS_HALT) != 0
                && DdStartupDiagnosticsEnabled()
                && DdRuntimePolicyGet())
        {
            dd_sp_launch_observe(sp, status_before, status_after);
        }
        do_SP_Task(sp);
    }
}

void init_rsp(struct rsp_core* sp,
              uint32_t* sp_mem,
              struct mi_controller* mi,
              struct rdp_core* dp,
              struct ri_controller* ri)
{
    dd_sp_launch_reset_observer();
    dd_cmd_watch_reset();
    if (DdStartupDiagnosticsEnabled())
        dd_imem_dma_reset_observer();
    sp->mem = sp_mem;
    sp->mi = mi;
    sp->dp = dp;
    sp->ri = ri;
}

void poweron_rsp(struct rsp_core* sp)
{
    dd_sp_launch_reset_observer();
    dd_cmd_watch_reset();
    if (DdStartupDiagnosticsEnabled())
        dd_imem_dma_reset_observer();
    memset(sp->mem, 0, SP_MEM_SIZE);
    memset(sp->regs, 0, SP_REGS_COUNT*sizeof(uint32_t));
    memset(sp->regs2, 0, SP_REGS2_COUNT*sizeof(uint32_t));
    memset(sp->fifo, 0, SP_DMA_FIFO_SIZE*sizeof(struct sp_dma));

    sp->rsp_task_locked = 0;
    sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
    sp->regs[SP_STATUS_REG] = 1;
}


void read_rsp_mem(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t addr = rsp_mem_address(address);

    *value = sp->mem[addr];
}

void write_rsp_mem(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t addr = rsp_mem_address(address);

    masked_write(&sp->mem[addr], value, mask);
}


void read_rsp_regs(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg(address);

    *value = sp->regs[reg];

    if (reg == SP_SEMAPHORE_REG)
    {
        sp->regs[SP_SEMAPHORE_REG] = 1;
    }
}

void write_rsp_regs(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg(address);

    switch(reg)
    {
    case SP_STATUS_REG:
        update_sp_status(sp, value & mask);
    case SP_DMA_FULL_REG:
    case SP_DMA_BUSY_REG:
        return;
    }

    masked_write(&sp->regs[reg], value, mask);

    switch(reg)
    {
    case SP_RD_LEN_REG:
        fifo_push(sp, SP_DMA_WRITE);
        break;
    case SP_WR_LEN_REG:
        fifo_push(sp, SP_DMA_READ);
        break;
    case SP_SEMAPHORE_REG:
        sp->regs[SP_SEMAPHORE_REG] = 0;
        break;
    }
}


void read_rsp_regs2(void* opaque, uint32_t address, uint32_t* value)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg2(address);

    *value = sp->regs2[reg];
}

void write_rsp_regs2(void* opaque, uint32_t address, uint32_t value, uint32_t mask)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    uint32_t reg = rsp_reg2(address);

    masked_write(&sp->regs2[reg], value, mask);
}

void do_SP_Task(struct rsp_core* sp)
{
    uint32_t save_pc = sp->regs2[SP_PC_REG] & ~0xfff;

    uint32_t sp_delay_time;

    if (sp->mem[0xfc0/4] == 1)
    {
        unprotect_framebuffers(&sp->dp->fb);

        //gfx.processDList();
        sp->regs2[SP_PC_REG] &= 0xfff;
#if defined(PROFILE)
        timed_section_start(TIMED_SECTION_GFX);
#endif
        rsp.doRspCycles(0xffffffff);
#if defined(PROFILE)
        timed_section_end(TIMED_SECTION_GFX);
#endif
        sp->regs2[SP_PC_REG] |= save_pc;
        new_frame();

        if (sp->mi->regs[MI_INTR_REG] & MI_INTR_DP)
        {
            sp->mi->regs[MI_INTR_REG] &= ~MI_INTR_DP;
            if (sp->dp->dpc_regs[DPC_STATUS_REG] & DPC_STATUS_FREEZE) {
                sp->dp->do_on_unfreeze |= DELAY_DP_INT;
            } else {
                cp0_update_count(sp->mi->r4300);
                add_interrupt_event(&sp->mi->r4300->cp0, DP_INT, 4000);
            }
        }
        sp_delay_time = 1000;

        protect_framebuffers(&sp->dp->fb);
    }
    else if (sp->mem[0xfc0/4] == 2)
    {
        /* P08c-writer 2/2: record the command buffer's state at submission. */
        dd_cmd_entry_hash_observe(sp);
        //audio.processAList();
        sp->regs2[SP_PC_REG] &= 0xfff;
#if defined(PROFILE)
        timed_section_start(TIMED_SECTION_AUDIO);
#endif
        rsp.doRspCycles(0xffffffff);
#if defined(PROFILE)
        timed_section_end(TIMED_SECTION_AUDIO);
#endif
        sp->regs2[SP_PC_REG] |= save_pc;

        sp_delay_time = 4000;
    }
    else
    {
        sp->regs2[SP_PC_REG] &= 0xfff;
        rsp.doRspCycles(0xffffffff);
        sp->regs2[SP_PC_REG] |= save_pc;

        sp_delay_time = 0;
    }

    sp->rsp_task_locked = 0;
    sp->mi->r4300->cp0.interrupt_unsafe_state &= ~INTR_UNSAFE_RSP;
    if ((sp->regs[SP_STATUS_REG] & (SP_STATUS_HALT | SP_STATUS_BROKE)) == 0)
    {
        sp->rsp_task_locked = 1;
        sp->mi->r4300->cp0.interrupt_unsafe_state |= INTR_UNSAFE_RSP;
        sp->mi->regs[MI_INTR_REG] |= MI_INTR_SP;
    }
    if (sp->mi->regs[MI_INTR_REG] & MI_INTR_SP)
    {
        cp0_update_count(sp->mi->r4300);
        add_interrupt_event(&sp->mi->r4300->cp0, SP_INT, sp_delay_time);
        sp->mi->regs[MI_INTR_REG] &= ~MI_INTR_SP;
    }

    sp->regs[SP_STATUS_REG] &=
        ~(SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT);
}

void rsp_interrupt_event(void* opaque)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;

    if (!sp->rsp_task_locked)
    {
        sp->regs[SP_STATUS_REG] |=
            SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT;
    }

    if ((sp->regs[SP_STATUS_REG] & SP_STATUS_INTR_BREAK) != 0)
    {
        raise_rcp_interrupt(sp->mi, MI_INTR_SP);
    }
}

void rsp_end_of_dma_event(void* opaque)
{
    struct rsp_core* sp = (struct rsp_core*)opaque;
    fifo_pop(sp);
}
