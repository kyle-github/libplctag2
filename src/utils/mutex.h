#pragma once

/***************************************************************************
 *   Copyright (C) 2026 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This software is available under the MIT license.                     *
 *                                                                         *
 *   Permission is hereby granted, free of charge, to any person obtaining *
 *   a copy of this software and associated documentation files (the       *
 *   "Software"), to deal in the Software without restriction, including   *
 *   without limitation the rights to use, copy, modify, merge, publish,   *
 *   distribute, sublicense, and/or sell copies of the Software, and to    *
 *   permit persons to whom the Software is furnished to do so, subject to *
 *   the following conditions:                                             *
 *                                                                         *
 *   The above copyright notice and this permission notice shall be        *
 *   included in all copies or substantial portions of the Software.       *
 *                                                                         *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       *
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    *
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                 *
 *   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS   *
 *   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN    *
 *   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN     *
 *   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE      *
 *   SOFTWARE.                                                             *
 ***************************************************************************/

/*
 * mutex.h — non-recursive exclusive mutex, platform-abstracted.
 *
 * Windows: SRWLOCK (exclusive mode only; no destroy needed).
 * POSIX:   pthread_mutex_t.
 *
 * Static initialiser: plc_mutex_t m = PLC_MUTEX_INIT;
 * Heap-allocated:     plc_mutex_init(&m);
 */

#include "plctag.h"

#if defined(_WIN32) || defined(_WIN64)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>

typedef struct { SRWLOCK srw; } plc_mutex_t;

#    define PLC_MUTEX_INIT { SRWLOCK_INIT }

#else /* POSIX */
#    include <pthread.h>

typedef struct { pthread_mutex_t m; } plc_mutex_t;

#    define PLC_MUTEX_INIT { PTHREAD_MUTEX_INITIALIZER }

#endif

#ifdef __cplusplus
extern "C" {
#endif

plc_status_t plc_mutex_init   (plc_mutex_t *m);
void         plc_mutex_lock   (plc_mutex_t *m);
void         plc_mutex_unlock (plc_mutex_t *m);
void         plc_mutex_destroy(plc_mutex_t *m); /* no-op on Windows */

#ifdef __cplusplus
}
#endif
