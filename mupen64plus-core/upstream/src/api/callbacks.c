/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-core - api/callbacks.c                                    *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2009 Richard Goedeken                                   *
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

/* This file contains the Core functions for handling callbacks to the
 * front-end application
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "api/m64p_frontend.h"
#include "callbacks.h"
#include "m64p_types.h"

/* local variables */
static ptr_DebugCallback pDebugFunc = NULL;
static ptr_StateCallback pStateFunc = NULL;
static void *            DebugContext = NULL;
static void *            StateContext = NULL;
/* Set only at callback registration, before emulation threads run. */
static int dd_startup_diagnostics = 0;
static unsigned int dd_startup_remaining = 0;
/*
 * DDSTART3 deliberately uses a separate callback budget.  Startup messages
 * retain their DDSTART1 cap, but cannot consume the observations needed after
 * the guest begins issuing DD commands.  PI DMA starts and unpaired PI
 * completion boundaries have separate classes so ordinary cartridge traffic
 * cannot consume the DD DMA-start observations.  DDSTART4 BM observations
 * have a larger bounded window sized for a block handshake; prior retries
 * can consume it, so complete-block coverage is not guaranteed.
 * DDSTART5 context records are intentionally outside both the aggregate and
 * per-kind budgets above.  They have a small, independently reset class
 * budget so a late progress boundary remains observable.
 * DDSTART6 scheduler records are also independent: the scheduler snapshot is
 * a read-only, signature-gated RDRAM observation and must not be starved by
 * the earlier trace classes or by DDSTART5.
 */
enum
{
    DD_TRACE_TOTAL_BUDGET = 628,
    DD_TRACE_REGISTER_COMMAND_BUDGET = 24,
    DD_TRACE_REGISTER_READ_BUDGET = 24,
    DD_TRACE_PI_DMA_BUDGET = 20,
    DD_TRACE_PI_BOUNDARY_BUDGET = 20,
    DD_TRACE_INTERRUPT_BUDGET = 16,
    DD_TRACE_PROGRESS_BUDGET = 12,
    DD_TRACE_BM_HANDSHAKE_BUDGET = 512,
    DD_TRACE_CONTEXT_BUDGET = 24,
    DD_TRACE_SCHEDULER_BUDGET = 64,
    DD_TRACE_EARLY_SAMPLES = 8
};

static unsigned int dd_trace_remaining = 0;
static unsigned int dd_trace_kind_remaining[DD_TRACE_KIND_COUNT];
static unsigned int dd_trace_kind_seen[DD_TRACE_KIND_COUNT];
static unsigned int dd_trace_context_remaining = 0;
static unsigned int dd_trace_context_seen = 0;
static unsigned int dd_trace_scheduler_remaining = 0;
static unsigned int dd_trace_scheduler_seen = 0;

static int reserve_dd_trace(unsigned int *remaining)
{
    unsigned int available = __atomic_load_n(remaining, __ATOMIC_RELAXED);

    do {
        if (available == 0)
            return 0;
    } while (!__atomic_compare_exchange_n(remaining, &available,
                available - 1, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED));

    return 1;
}

static int reserve_dd_trace_context(void)
{
    return reserve_dd_trace(&dd_trace_context_remaining);
}

static int power_of_two(unsigned int value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static int power_of_four(unsigned int value)
{
    return power_of_two(value) && (value & UINT32_C(0x55555555)) != 0;
}

static unsigned int dd_trace_kind_budget(enum dd_startup_trace_kind kind)
{
    static const unsigned int budgets[DD_TRACE_KIND_COUNT] = {
        DD_TRACE_REGISTER_COMMAND_BUDGET,
        DD_TRACE_REGISTER_READ_BUDGET,
        DD_TRACE_PI_DMA_BUDGET,
        DD_TRACE_PI_BOUNDARY_BUDGET,
        DD_TRACE_INTERRUPT_BUDGET,
        DD_TRACE_PROGRESS_BUDGET,
        DD_TRACE_BM_HANDSHAKE_BUDGET
    };

    return ((unsigned int)kind < DD_TRACE_KIND_COUNT) ? budgets[kind] : 0;
}

int DdStartupDiagnosticsEnabled(void)
{
    return dd_startup_diagnostics;
}

void DdStartupDiagnosticsTrace(enum dd_startup_trace_kind kind,
                               enum dd_startup_trace_mode mode,
                               const char *message, ...)
{
    char msgbuf[512];
    va_list args;
    unsigned int ordinal;
    int prefix_length = 0;

    if (!DdStartupDiagnosticsEnabled() || (unsigned int)kind >= DD_TRACE_KIND_COUNT
            || message == NULL)
        return;

    ordinal = __atomic_add_fetch(&dd_trace_kind_seen[kind], 1, __ATOMIC_RELAXED);
    if (mode == DD_TRACE_SPARSE && ordinal > DD_TRACE_EARLY_SAMPLES
            && !power_of_two(ordinal))
        return;
    if (mode == DD_TRACE_PROGRESS_SPARSE
            && ordinal != 1 && !power_of_four(ordinal))
        return;
    if (!reserve_dd_trace(&dd_trace_kind_remaining[kind])
            || !reserve_dd_trace(&dd_trace_remaining))
        return;

    if (kind == DD_TRACE_PROGRESS || kind == DD_TRACE_BM_HANDSHAKE) {
        prefix_length = snprintf(msgbuf, sizeof(msgbuf), "ordinal=%u ",
                ordinal);
        if (prefix_length < 0)
            return;
        if ((size_t)prefix_length >= sizeof(msgbuf))
            prefix_length = sizeof(msgbuf) - 1;
    }
    va_start(args, message);
    vsnprintf(msgbuf + prefix_length, sizeof(msgbuf) - prefix_length,
            message, args);
    va_end(args);

    /*
     * This is intentionally not DebugMessage(): its 256-message startup
     * budget must not hide the later sparse trace.  The callback is still the
     * existing synchronous core callback; no queue, allocation, or file I/O
     * is introduced.
     */
    (*pDebugFunc)(DebugContext, M64MSG_INFO, msgbuf);
}

unsigned int DdStartupDiagnosticsNextProgressOrdinal(void)
{
    if (!DdStartupDiagnosticsEnabled())
        return 0;

    /*
     * The caller invokes this immediately before the existing progress
     * trace, on the emulation thread.  The trace itself increments this
     * class's seen counter; this read therefore predicts that trace's
     * ordinal without adding a second progress counter.
     */
    return __atomic_load_n(&dd_trace_kind_seen[DD_TRACE_PROGRESS],
            __ATOMIC_RELAXED) + 1;
}

int DdStartupDiagnosticsTraceContext(const char *message, ...)
{
    char msgbuf[512];
    va_list args;
    unsigned int ordinal;
    int prefix_length;

    if (!DdStartupDiagnosticsEnabled() || message == NULL
            || !reserve_dd_trace_context())
        return 0;

    ordinal = __atomic_add_fetch(&dd_trace_context_seen, 1, __ATOMIC_RELAXED);
    prefix_length = snprintf(msgbuf, sizeof(msgbuf), "record=%u ", ordinal);
    if (prefix_length < 0)
        return 0;
    if ((size_t)prefix_length >= sizeof(msgbuf))
        prefix_length = sizeof(msgbuf) - 1;

    va_start(args, message);
    vsnprintf(msgbuf + prefix_length, sizeof(msgbuf) - (size_t)prefix_length,
            message, args);
    va_end(args);

    /*
     * DDSTART5 is an explicitly gated, synchronous callback observation.
     * The context and code snapshot is prepared by the emulation thread
     * before this call; this function performs no allocation or I/O.
     */
    (*pDebugFunc)(DebugContext, M64MSG_INFO, msgbuf);
    return 1;
}

int DdStartupDiagnosticsTraceScheduler(const char *message, ...)
{
    char msgbuf[512];
    va_list args;
    unsigned int ordinal;
    int prefix_length;

    if (!DdStartupDiagnosticsEnabled() || message == NULL
            || !reserve_dd_trace(&dd_trace_scheduler_remaining))
        return 0;

    ordinal = __atomic_add_fetch(&dd_trace_scheduler_seen, 1, __ATOMIC_RELAXED);
    prefix_length = snprintf(msgbuf, sizeof(msgbuf), "record=%u ", ordinal);
    if (prefix_length < 0)
        return 0;
    if ((size_t)prefix_length >= sizeof(msgbuf))
        prefix_length = sizeof(msgbuf) - 1;

    va_start(args, message);
    vsnprintf(msgbuf + prefix_length, sizeof(msgbuf) - (size_t)prefix_length,
            message, args);
    va_end(args);

    /*
     * DDSTART6 is a bounded synchronous callback just like DDSTART5.  The
     * emulation thread prepares the snapshot and this function only formats
     * and emits it; no allocation, guest write, or file I/O is introduced.
     */
    (*pDebugFunc)(DebugContext, M64MSG_INFO, msgbuf);
    return 1;
}

/* global Functions for use by the Core */
m64p_error SetDebugCallback(ptr_DebugCallback pFunc, void *Context)
{
    unsigned int i;

    pDebugFunc = pFunc;
    DebugContext = Context;
    const char *dd_option = getenv("M64P_DD_STARTUP_DIAGNOSTICS");
    dd_startup_diagnostics = pFunc != NULL && dd_option != NULL
        && dd_option[0] == '1' && dd_option[1] == '\0';
    __atomic_store_n(&dd_startup_remaining, 256, __ATOMIC_RELAXED);
    __atomic_store_n(&dd_trace_remaining, DD_TRACE_TOTAL_BUDGET, __ATOMIC_RELAXED);
    __atomic_store_n(&dd_trace_context_remaining, DD_TRACE_CONTEXT_BUDGET,
            __ATOMIC_RELAXED);
    __atomic_store_n(&dd_trace_context_seen, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&dd_trace_scheduler_remaining, DD_TRACE_SCHEDULER_BUDGET,
            __ATOMIC_RELAXED);
    __atomic_store_n(&dd_trace_scheduler_seen, 0, __ATOMIC_RELAXED);
    for (i = 0; i < DD_TRACE_KIND_COUNT; ++i) {
        __atomic_store_n(&dd_trace_kind_remaining[i],
                dd_trace_kind_budget((enum dd_startup_trace_kind)i),
                __ATOMIC_RELAXED);
        __atomic_store_n(&dd_trace_kind_seen[i], 0, __ATOMIC_RELAXED);
    }
    if (dd_startup_diagnostics)
        DebugMessage(M64MSG_INFO, "DDSTART1 native: support64dd=true; INFO/WARNING/ERROR only; limit=256");
    return M64ERR_SUCCESS;
}

m64p_error SetStateCallback(ptr_StateCallback pFunc, void *Context)
{
    pStateFunc = pFunc;
    StateContext = Context;
    return M64ERR_SUCCESS;
}

void DebugMessage(int level, const char *message, ...)
{
  char msgbuf[512];
  va_list args;

  if (pDebugFunc == NULL)
      return;

  unsigned int remaining = 0;
  if (dd_startup_diagnostics) {
      if (level != M64MSG_ERROR && level != M64MSG_WARNING && level != M64MSG_INFO)
          return;
      remaining = __atomic_load_n(&dd_startup_remaining, __ATOMIC_RELAXED);
      do {
          if (remaining == 0)
              return;
      } while (!__atomic_compare_exchange_n(&dd_startup_remaining, &remaining,
                  remaining - 1, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
      /* Reserve the final slot for an explicit end-of-coverage marker. */
      if (remaining == 1) {
          (*pDebugFunc)(DebugContext, M64MSG_INFO,
              "DDSTART1 limit reached; further core messages suppressed, not evidence of a stall");
          return;
      }
  }

  va_start(args, message);
  vsnprintf(msgbuf, 512, message, args);

  (*pDebugFunc)(DebugContext, level, msgbuf);

  va_end(args);
}

void StateChanged(m64p_core_param param_type, int new_value)
{
    if (pStateFunc == NULL)
        return;

    (*pStateFunc)(StateContext, param_type, new_value);
}


