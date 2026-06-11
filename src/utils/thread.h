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
 * thread.h — thread create/join and sleep, platform-abstracted.
 *
 * Windows: CreateThread / WaitForSingleObject / Sleep.
 * POSIX:   pthread_create / pthread_join / nanosleep.
 */

#include "plctag.h"

#if defined(_WIN32) || defined(_WIN64)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>

typedef struct { HANDLE h; } plc_thread_t;

#else /* POSIX */
#    include <pthread.h>

typedef struct { pthread_t t; } plc_thread_t;

#endif

typedef void *(*plc_thread_func_t)(void *arg);

#ifdef __cplusplus
extern "C" {
#endif

/* Spawn a thread that runs fn(arg).  Returns ERR_INTERNAL on OS failure. */
plc_status_t plc_thread_create  (plc_thread_t *t, plc_thread_func_t fn, void *arg);

/* Block until the thread exits.  Call exactly once per successful create. */
void         plc_thread_join    (plc_thread_t *t);

/* Sleep at least ms milliseconds (restarts on EINTR). */
void         plc_thread_sleep_ms(int ms);

#ifdef __cplusplus
}
#endif
