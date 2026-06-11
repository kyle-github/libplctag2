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
 * device.h — plc_device_t lifetime management.
 *
 * plc_device_t is the central per-connection object.  It is allocated by
 * plc_device_create, inserted into the handle table, and freed by
 * plc_device_destroy once all references drop to zero.
 *
 * Locking disciplines:
 *   dev->lock       serialises concurrent API calls on the same device.
 *                   Held only for the duration of a single API operation.
 *   refcount / dead are protected by the handle table's internal mutex, not
 *                   by dev->lock.  See handle.h for the acquire/release contract.
 */

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>

#include "plctag.h"
#include "arena.h"
#include "mutex.h"
#include "thread.h"
#include "protocol/driver.h"
#include "core/cache.h"
#include "core/staged.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Event queue (M8) ---- */
/* plc_event_t is defined in plctag.h (the public header). */
#define PLC_EVENT_QUEUE_SIZE 64u

typedef struct {
    plc_event_t entries[PLC_EVENT_QUEUE_SIZE];
    size_t      head;   /* next write position */
    size_t      tail;   /* next read position  */
    size_t      count;  /* entries currently stored */
} event_queue_t;

/* ---- Subscription list (M8) ---- */
#define PLC_MAX_SUBSCRIPTIONS 32u

typedef struct {
    char    path[256];
    int     interval_ms;
    int64_t next_read_ms;
    bool    active;
} plc_subscription_t;

typedef struct {
    plc_subscription_t subs[PLC_MAX_SUBSCRIPTIONS];
    size_t             count;
} subscription_list_t;

/* ---- Device ---- */

struct plc_device_t {
    plc_dev_handle_t          handle;        /* own handle (for logging) */
    const plc_driver_vtable_t *driver;       /* protocol vtable; NULL until open */
    void                      *driver_state; /* driver-private; NULL until open */
    plc_mutex_t                lock;         /* api_mutex: serialises API calls */
    int                        refcount;     /* under handle table lock */
    bool                       dead;         /* under handle table lock */
    plc_status_t               last_status;
    char                       last_error[256];
    Arena                      scratch;      /* per-request scratch; reset each op */

    /* M7: value cache and staged-op queue */
    value_cache_t             *value_cache;
    staged_queue_t            *staged_queue;

    /* M8: subscription background thread */
    subscription_list_t        subscriptions;
    event_queue_t              events;
    volatile bool              stopping;     /* set before joining bg_thread */
    bool                       bg_running;   /* true if bg_thread was started */
    plc_thread_t               bg_thread;
};

/* Allocate and zero-initialise a device; init lock and 64 KB scratch arena.
   refcount starts at 1 (the "handle ref"). */
extern plc_status_t plc_device_create(plc_device_t **out);

/* Free all resources (lock, scratch arena, device struct).  Do not call while
   any thread still holds a reference. */
extern void plc_device_destroy(plc_device_t *dev);

/* Record a formatted last-error string.  Truncates silently at 255 chars. */
extern void plc_device_set_error(plc_device_t *dev, plc_status_t status, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
