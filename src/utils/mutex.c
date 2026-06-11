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

#include "mutex.h"
#include "debug.h"

#if defined(_WIN32) || defined(_WIN64)

plc_status_t plc_mutex_init(plc_mutex_t *m) {
    if(!m) { return PLC_STATUS_ERR_NULL_PTR; }
    InitializeSRWLock(&m->srw);
    return PLC_STATUS_OK;
}

void plc_mutex_lock   (plc_mutex_t *m) { AcquireSRWLockExclusive(&m->srw); }
void plc_mutex_unlock (plc_mutex_t *m) { ReleaseSRWLockExclusive(&m->srw); }
void plc_mutex_destroy(plc_mutex_t *m) { (void)m; /* SRWLock has no destroy */ }

#else /* POSIX */

plc_status_t plc_mutex_init(plc_mutex_t *m) {
    if(!m) { return PLC_STATUS_ERR_NULL_PTR; }
    int rc = pthread_mutex_init(&m->m, NULL);
    if(rc != 0) {
        pdebug(DEBUG_MODULE_PLATFORM, DEBUG_ERROR, 0, "pthread_mutex_init failed: %d", rc);
        return PLC_STATUS_ERR_INTERNAL;
    }
    return PLC_STATUS_OK;
}

void plc_mutex_lock   (plc_mutex_t *m) { pthread_mutex_lock(&m->m); }
void plc_mutex_unlock (plc_mutex_t *m) { pthread_mutex_unlock(&m->m); }
void plc_mutex_destroy(plc_mutex_t *m) { if(m) { pthread_mutex_destroy(&m->m); } }

#endif
