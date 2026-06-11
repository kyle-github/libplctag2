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
 * handle.h — generational handle table.
 *
 * plc_dev_handle_t encodes [63:32] generation + [31:0] table index.
 * Stale handles (referring to a reused or already-closed slot) are detected
 * by the generation mismatch, making the scheme ABA-safe.
 *
 * Refcount semantics:
 *   - handle_alloc:   device starts with refcount = 1 (the "handle ref").
 *   - handle_acquire: bumps refcount; returns NULL for stale / dead handles.
 *   - handle_release: decrements refcount; calls plc_device_destroy when it
 *                     hits zero.
 *   - handle_close:   marks the device dead, removes it from the table, and
 *                     returns the device pointer (still holding refcount = 1).
 *                     The caller must drive driver teardown and then call
 *                     handle_release to drop the final reference.
 *
 * All table mutations are serialized by a single internal mutex.
 */

#include "plctag.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration; full definition in device.h. */
typedef struct plc_device_t plc_device_t;

/* One-time init / teardown (idempotent; safe to call from plc_open). */
extern plc_status_t  handle_table_init(void);
extern void          handle_table_term(void);

/* Insert dev into a free slot; returns the new handle.
   Returns PLC_INVALID_HANDLE if the table is full. */
extern plc_dev_handle_t handle_alloc(plc_device_t *dev);

/* Validate h and increment refcount.  Returns NULL if h is stale or dead.
   Every successful acquire must be paired with a handle_release. */
extern plc_device_t    *handle_acquire(plc_dev_handle_t h);

/* Decrement refcount.  Calls plc_device_destroy when it reaches zero. */
extern void             handle_release(plc_device_t *dev);

/* Mark the device dead, remove it from the table, and return it.
   Returns NULL if h is already dead / invalid.  The caller receives the
   handle ref (refcount unchanged); it must call handle_release when done. */
extern plc_device_t    *handle_close(plc_dev_handle_t h);

#ifdef __cplusplus
}
#endif
