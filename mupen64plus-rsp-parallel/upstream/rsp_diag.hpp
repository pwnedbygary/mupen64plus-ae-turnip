#pragma once

#include <stdint.h>

namespace RSP
{
namespace Diagnostics
{
/*
 * The core supplies this callback only for an explicitly DD-enabled game.
 * The observer is called synchronously by the emulation thread; callback
 * registration and reset happen at plugin/ROM lifecycle boundaries before
 * that thread runs.
 */
using DebugCallback = void (*)(void *, int, const char *);

void set_callback(DebugCallback callback, void *context);
bool enabled();

bool trace_jit(uintptr_t host_start, uintptr_t host_end,
               unsigned imem_start_pc, unsigned instruction_count,
               uint64_t existing_region_hash, const char *event);
bool trace_jit_range_unavailable(uintptr_t host_entry);
bool trace_jit_compile_words(uintptr_t host_start, unsigned imem_start_pc,
                             const uint32_t *words, unsigned word_count);

bool trace_rsp_entry(uint64_t task_hash, uint64_t imem_hash,
                     uint32_t task_type, uint32_t sp_pc);
void rsp_return();
} // namespace Diagnostics
} // namespace RSP
