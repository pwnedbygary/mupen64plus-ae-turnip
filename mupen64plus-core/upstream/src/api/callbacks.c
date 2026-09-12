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

int DdStartupDiagnosticsEnabled(void)
{
    return dd_startup_diagnostics;
}

/* global Functions for use by the Core */
m64p_error SetDebugCallback(ptr_DebugCallback pFunc, void *Context)
{
    pDebugFunc = pFunc;
    DebugContext = Context;
    const char *dd_option = getenv("M64P_DD_STARTUP_DIAGNOSTICS");
    dd_startup_diagnostics = pFunc != NULL && dd_option != NULL
        && dd_option[0] == '1' && dd_option[1] == '\0';
    __atomic_store_n(&dd_startup_remaining, 256, __ATOMIC_RELAXED);
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


