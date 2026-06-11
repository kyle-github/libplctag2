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

#include "thread.h"
#include "debug.h"

#if defined(_WIN32) || defined(_WIN64)

#    include <stdlib.h>

struct thread_tramp { plc_thread_func_t fn; void *arg; };

static DWORD WINAPI thread_trampoline(LPVOID p) {
    struct thread_tramp *tr = (struct thread_tramp *)p;
    plc_thread_func_t fn = tr->fn;
    void *arg = tr->arg;
    free(tr);
    fn(arg);
    return 0;
}

plc_status_t plc_thread_create(plc_thread_t *t, plc_thread_func_t fn, void *arg) {
    struct thread_tramp *tr = malloc(sizeof(*tr));
    if(!tr) { return PLC_STATUS_ERR_NO_MEM; }
    tr->fn  = fn;
    tr->arg = arg;
    t->h = CreateThread(NULL, 0, thread_trampoline, tr, 0, NULL);
    if(!t->h) { free(tr); return PLC_STATUS_ERR_INTERNAL; }
    return PLC_STATUS_OK;
}

void plc_thread_join(plc_thread_t *t) {
    if(t->h) { WaitForSingleObject(t->h, INFINITE); CloseHandle(t->h); t->h = NULL; }
}

void plc_thread_sleep_ms(int ms) { if(ms > 0) { Sleep((DWORD)ms); } }

#else /* POSIX */

#    include <errno.h>
#    include <time.h>

plc_status_t plc_thread_create(plc_thread_t *t, plc_thread_func_t fn, void *arg) {
    int rc = pthread_create(&t->t, NULL, fn, arg);
    if(rc != 0) {
        pdebug(DEBUG_MODULE_PLATFORM, DEBUG_ERROR, 0, "pthread_create failed: %d", rc);
        return PLC_STATUS_ERR_INTERNAL;
    }
    return PLC_STATUS_OK;
}

void plc_thread_join(plc_thread_t *t) { pthread_join(t->t, NULL); }

void plc_thread_sleep_ms(int ms) {
    if(ms <= 0) { return; }
    struct timespec ts = {
        .tv_sec  = ms / 1000,
        .tv_nsec = (long)(ms % 1000) * 1000000L,
    };
    while(nanosleep(&ts, &ts) == -1 && errno == EINTR) { /* restart on signal */ }
}

#endif
