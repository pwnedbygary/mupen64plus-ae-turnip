/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-core - api/callbacks.h                                    *
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

/* This file contains the definitions for callback functions which will be
 * called from the other Core modules
 */

#if !defined(API_CALLBACKS_H)
#define API_CALLBACKS_H

#include "m64p_frontend.h"
#include "m64p_types.h"

#if defined(__GNUC__)
#define ATTR_FMT(fmtpos, attrpos) __attribute__ ((format (printf, fmtpos, attrpos)))
#else
#define ATTR_FMT(fmtpos, attrpos)
#endif

/* Functions for use by the Core, to send information back to the front-end app */
extern m64p_error SetDebugCallback(ptr_DebugCallback pFunc, void *Context);
extern int DdStartupDiagnosticsEnabled(void);
enum dd_startup_trace_kind
{
    DD_TRACE_REGISTER_COMMAND = 0,
    DD_TRACE_REGISTER_READ,
    DD_TRACE_PI_DMA,
    DD_TRACE_PI_BOUNDARY,
    DD_TRACE_INTERRUPT,
    DD_TRACE_PROGRESS,
    DD_TRACE_BM_HANDSHAKE,
    DD_TRACE_KIND_COUNT
};

enum dd_startup_trace_mode
{
    DD_TRACE_EARLY = 0,
    DD_TRACE_SPARSE,
    DD_TRACE_PROGRESS_SPARSE
};

extern void DdStartupDiagnosticsTrace(enum dd_startup_trace_kind kind,
                                      enum dd_startup_trace_mode mode,
                                      const char *message, ...) ATTR_FMT(3,4);
/*
 * Return the progress ordinal which the next DDSTART3 progress trace will
 * receive.  This is used only at the existing emulation-thread boundary to
 * select the two DDSTART5 observation points; it does not advance the
 * counter.
 */
extern unsigned int DdStartupDiagnosticsNextProgressOrdinal(void);
/*
 * DDSTART5 has its own bounded record class.  It deliberately does not use
 * the aggregate DDSTART3/DDSTART4 budget, so late context evidence remains
 * available after earlier classes fill.
 */
extern int DdStartupDiagnosticsTraceContext(const char *message, ...)
    ATTR_FMT(1,2);
/*
 * DDSTART6 has its own 64-record session budget.  It is deliberately
 * separate from DDSTART1/DDSTART3/DDSTART4 and DDSTART5, and is reset by
 * every callback registration.
 */
extern int DdStartupDiagnosticsTraceScheduler(const char *message, ...)
    ATTR_FMT(1,2);
extern m64p_error SetStateCallback(ptr_StateCallback pFunc, void *Context);
extern void       DebugMessage(int level, const char *message, ...) ATTR_FMT(2,3);
extern void       StateChanged(m64p_core_param param_type, int new_value);

#endif /* API_CALLBACKS_H */

