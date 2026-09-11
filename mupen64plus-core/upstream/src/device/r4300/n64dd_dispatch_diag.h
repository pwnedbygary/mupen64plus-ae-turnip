/*
 * Narrow, observational 64DD dispatcher diagnostics.
 *
 * This interface deliberately has no guest-state mutation.  The implementation
 * is inert unless a DD disk is attached and keeps bounded in-memory rings which
 * the existing CPU watchdog can append to its diagnostic dump.
 */
#ifndef M64P_DEVICE_R4300_N64DD_DISPATCH_DIAG_H
#define M64P_DEVICE_R4300_N64DD_DISPATCH_DIAG_H

#include <stdint.h>
#include <stdio.h>

struct r4300_core;

enum n64dd_dispatch_diag_phase
{
    N64DD_DISPATCH_DIAG_PRE = 1,
    N64DD_DISPATCH_DIAG_POST = 2,
    N64DD_DISPATCH_DIAG_DELAY_PRE = 3,
    N64DD_DISPATCH_DIAG_DELAY_POST = 4
};

/* Called around one instruction by the pure and cached interpreters. */
void n64dd_dispatch_diag_step(struct r4300_core* r4300, uint32_t pc,
                              unsigned phase);

/* Called at a new-dynarec interrupt/sample boundary. */
void n64dd_dispatch_diag_boundary(struct r4300_core* r4300, uint32_t pc);

/*
 * Called after a CPU memory write has completed.  physical_address is the
 * canonical RDRAM address used by the memory handler; source_pc is the
 * instruction PC when the caller has one (zero permits the helper to derive
 * it from the CPU mode).
 */
void n64dd_dispatch_diag_store(struct r4300_core* r4300,
                               uint32_t virtual_address,
                               uint32_t physical_address,
                               unsigned width,
                               uint64_t value,
                               uint64_t mask,
                               uint32_t source_pc);

/* Called by both interpreter and new-dynarec ERET implementations. */
void n64dd_dispatch_diag_eret(struct r4300_core* r4300, uint32_t pc,
                              uint32_t epc);

/* Source PC for a memory write made by the current interpreter instruction. */
uint32_t n64dd_dispatch_diag_current_pc(struct r4300_core* r4300);

/* Appends the bounded rings and coverage counters to a watchdog FILE. */
void n64dd_dispatch_diag_dump(FILE* file);

#endif /* M64P_DEVICE_R4300_N64DD_DISPATCH_DIAG_H */