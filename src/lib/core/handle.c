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

#include <stdint.h>
#include <string.h>

#include "core/handle.h"
#include "core/device.h"
#include "mutex.h"
#include "debug.h"

/*
 * Handle encoding:
 *   bits [63:32]  generation counter (32-bit; wraps, but collision prob is tiny)
 *   bits [31:0]   table index
 */
#define HANDLE_INDEX(h)         ((uint32_t)((h) & 0xFFFFFFFFu))
#define HANDLE_GEN(h)           ((uint32_t)((h) >> 32))
#define MAKE_HANDLE(idx, gen)   (((uint64_t)(gen) << 32) | (uint64_t)(idx))

#define MAX_DEVICES 64

typedef struct {
    plc_device_t *dev;
    uint32_t      gen; /* current generation of this slot */
} slot_t;

static slot_t      table[MAX_DEVICES];
static plc_mutex_t table_lock = PLC_MUTEX_INIT;
static int         table_ready = 0;


plc_status_t handle_table_init(void) {
    plc_mutex_lock(&table_lock);
    if(!table_ready) {
        memset(table, 0, sizeof(table));
        table_ready = 1;
    }
    plc_mutex_unlock(&table_lock);
    return PLC_STATUS_OK;
}


void handle_table_term(void) {
    plc_mutex_lock(&table_lock);
    table_ready = 0;
    plc_mutex_unlock(&table_lock);
}


plc_dev_handle_t handle_alloc(plc_device_t *dev) {
    if(!dev) { return PLC_INVALID_HANDLE; }

    plc_mutex_lock(&table_lock);

    plc_dev_handle_t h = PLC_INVALID_HANDLE;

    for(uint32_t i = 0; i < MAX_DEVICES; i++) {
        if(!table[i].dev) {
            table[i].gen++;
            if(table[i].gen == 0) { table[i].gen = 1; } /* skip 0 so handle != PLC_INVALID_HANDLE */
            table[i].dev = dev;
            h = MAKE_HANDLE(i, table[i].gen);
            dev->handle = h;
            break;
        }
    }

    plc_mutex_unlock(&table_lock);

    if(h == PLC_INVALID_HANDLE) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_ERROR, 0, "handle table full (max %d devices)", MAX_DEVICES);
    }

    return h;
}


plc_device_t *handle_acquire(plc_dev_handle_t h) {
    if(h == PLC_INVALID_HANDLE) { return NULL; }

    uint32_t idx = HANDLE_INDEX(h);
    uint32_t gen = HANDLE_GEN(h);

    if(idx >= MAX_DEVICES) { return NULL; }

    plc_mutex_lock(&table_lock);

    plc_device_t *dev = NULL;
    if(table[idx].dev && table[idx].gen == gen && !table[idx].dev->dead) {
        table[idx].dev->refcount++;
        dev = table[idx].dev;
    }

    plc_mutex_unlock(&table_lock);

    return dev;
}


void handle_release(plc_device_t *dev) {
    if(!dev) { return; }

    plc_mutex_lock(&table_lock);
    dev->refcount--;
    int should_destroy = (dev->refcount <= 0);
    plc_mutex_unlock(&table_lock);

    if(should_destroy) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_DETAIL, (int64_t)dev->handle, "destroying device");
        plc_device_destroy(dev);
    }
}


plc_device_t *handle_close(plc_dev_handle_t h) {
    if(h == PLC_INVALID_HANDLE) { return NULL; }

    uint32_t idx = HANDLE_INDEX(h);
    uint32_t gen = HANDLE_GEN(h);

    if(idx >= MAX_DEVICES) { return NULL; }

    plc_mutex_lock(&table_lock);

    plc_device_t *dev = NULL;
    if(table[idx].dev && table[idx].gen == gen && !table[idx].dev->dead) {
        dev = table[idx].dev;
        dev->dead = 1;
        table[idx].dev = NULL; /* slot is free for reuse immediately */
    }

    plc_mutex_unlock(&table_lock);

    return dev; /* caller holds the handle ref (refcount unchanged) */
}
