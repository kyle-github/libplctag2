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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/device.h"
#include "core/cache.h"
#include "core/staged.h"
#include "debug.h"

#define SCRATCH_SIZE (65536u) /* 64 KB per-request scratch arena */


plc_status_t plc_device_create(plc_device_t **out) {
    if(!out) { return PLC_STATUS_ERR_NULL_PTR; }

    plc_device_t *dev = calloc(1, sizeof(*dev));
    if(!dev) { return PLC_STATUS_ERR_NO_MEM; }

    plc_status_t rc = plc_mutex_init(&dev->lock);
    if(rc != PLC_STATUS_OK) {
        free(dev);
        return rc;
    }

    rc = arena_init(&dev->scratch, SCRATCH_SIZE);
    if(rc != PLC_STATUS_OK) {
        plc_mutex_destroy(&dev->lock);
        free(dev);
        return rc;
    }

    rc = cache_create(&dev->value_cache);
    if(rc != PLC_STATUS_OK) {
        arena_free(&dev->scratch);
        plc_mutex_destroy(&dev->lock);
        free(dev);
        return rc;
    }

    rc = staged_create(&dev->staged_queue);
    if(rc != PLC_STATUS_OK) {
        cache_destroy(dev->value_cache);
        arena_free(&dev->scratch);
        plc_mutex_destroy(&dev->lock);
        free(dev);
        return rc;
    }

    dev->refcount    = 1;
    dev->last_status = PLC_STATUS_OK;

    *out = dev;
    return PLC_STATUS_OK;
}


void plc_device_destroy(plc_device_t *dev) {
    if(!dev) { return; }

    staged_destroy(dev->staged_queue);
    cache_destroy(dev->value_cache);
    arena_free(&dev->scratch);
    plc_mutex_destroy(&dev->lock);
    free(dev);
}


void plc_device_set_error(plc_device_t *dev, plc_status_t status, const char *fmt, ...) {
    if(!dev) { return; }

    dev->last_status = status;

    va_list va;
    va_start(va, fmt);
    vsnprintf(dev->last_error, sizeof(dev->last_error), fmt, va);
    va_end(va);

    dev->last_error[sizeof(dev->last_error) - 1] = '\0';
}
