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

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plctag.h"
#include "core/attr.h"
#include "core/cache.h"
#include "core/device.h"
#include "core/handle.h"
#include "core/metadata.h"
#include "core/node.h"
#include "core/path.h"
#include "core/staged.h"
#include "core/value.h"
#include "mutex.h"
#include "clock.h"
#include "thread.h"
#include "protocol/driver_registry.h"
#include "socket.h"
#include "debug.h"

/* ---- Internal helpers ---- */

static plc_device_t *api_lock(plc_dev_handle_t h) {
    plc_device_t *dev = handle_acquire(h);
    if(!dev) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_WARN, 0, "bad or stale handle %" PRIu64, h);
        return NULL;
    }
    plc_mutex_lock(&dev->lock);
    arena_reset(&dev->scratch);
    return dev;
}

static void api_unlock(plc_device_t *dev) {
    plc_mutex_unlock(&dev->lock);
    handle_release(dev);
}

/* ---- M8: event queue helpers (called under dev->lock) ---- */

static void event_push(plc_device_t *dev, const char *path, int index,
                        plc_value_type_t type, plc_status_t status) {
    event_queue_t *q = &dev->events;
    if(q->count >= PLC_EVENT_QUEUE_SIZE) {
        /* Drop oldest event to make room. */
        q->tail  = (q->tail + 1u) % PLC_EVENT_QUEUE_SIZE;
        q->count--;
    }
    plc_event_t *e = &q->entries[q->head];
    strncpy(e->path, path ? path : "", sizeof(e->path) - 1u);
    e->path[sizeof(e->path) - 1u] = '\0';
    e->index  = index;
    e->type   = type;
    e->status = status;
    q->head  = (q->head + 1u) % PLC_EVENT_QUEUE_SIZE;
    q->count++;
}

/* ---- M8: subscription background thread ---- */

static void *subscription_thread(void *arg) {
    plc_device_t *dev = (plc_device_t *)arg;

    while(!dev->stopping) {
        int64_t now       = time_ms();
        int64_t min_next  = now + 100;

        plc_mutex_lock(&dev->lock);

        for(size_t i = 0; i < dev->subscriptions.count; i++) {
            plc_subscription_t *sub = &dev->subscriptions.subs[i];
            if(!sub->active) { continue; }

            if(now >= sub->next_read_ms) {
                plc_node_t   node = {0};
                plc_value_t  val  = {0};
                plc_status_t rc   = PLC_STATUS_ERR_NOT_SUPPORTED;

                if(dev->driver && dev->driver->resolve) {
                    plc_path_t p;
                    rc = path_parse(sub->path, &p);
                    if(rc == PLC_STATUS_OK) {
                        rc = dev->driver->resolve(dev, &p, 0, &node, 1000);
                    }
                    if(rc == PLC_STATUS_OK && dev->driver->read) {
                        rc = dev->driver->read(dev, &node, &val, 1000);
                        if(rc == PLC_STATUS_OK) {
                            cache_put(dev->value_cache, node.tag_name, node.flat_index, &val, now);
                        }
                    }
                }

                event_push(dev, sub->path, 0,
                           (rc == PLC_STATUS_OK) ? node.type : PLC_VAL_UNKNOWN, rc);
                sub->next_read_ms = now + sub->interval_ms;
                now = time_ms();
            }

            if(sub->next_read_ms < min_next) { min_next = sub->next_read_ms; }
        }

        plc_mutex_unlock(&dev->lock);

        int64_t sleep_ms = min_next - time_ms();
        if(sleep_ms > 100) { sleep_ms = 100; }
        if(sleep_ms > 0)   { plc_thread_sleep_ms((int)sleep_ms); }
        else               { plc_thread_sleep_ms(10); }
    }

    return NULL;
}

/* ---- plc_open / plc_close ---- */

plc_dev_handle_t plc_open(const char *connect_str, int timeout_ms) {
    pdebug(DEBUG_MODULE_LIB, DEBUG_INFO, 0, "opening \"%s\"", connect_str ? connect_str : "(null)");

    if(!connect_str) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_ERROR, 0, "NULL connect_str");
        return PLC_INVALID_HANDLE;
    }

    socket_lib_init();
    handle_table_init();

    plc_status_t rc;

    plc_attr_t *attr = plc_attr_parse(connect_str, &rc);
    if(!attr) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_ERROR, 0, "failed to parse connection string: %s",
               plc_status_str(rc));
        return PLC_INVALID_HANDLE;
    }

    const char *scheme = plc_attr_get_str(attr, "protocol",
                         plc_attr_get_str(attr, "scheme", "eip"));

    const plc_driver_vtable_t *driver = plc_driver_for_scheme(scheme);
    if(!driver || !driver->open) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_WARN, 0, "no driver for scheme \"%s\"", scheme);
        plc_attr_free(attr);
        return PLC_INVALID_HANDLE;
    }

    plc_device_t *dev = NULL;
    rc = plc_device_create(&dev);
    if(rc != PLC_STATUS_OK) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_ERROR, 0, "device create failed: %s", plc_status_str(rc));
        plc_attr_free(attr);
        return PLC_INVALID_HANDLE;
    }

    dev->driver = driver;

    plc_dev_handle_t h = handle_alloc(dev);
    if(h == PLC_INVALID_HANDLE) {
        plc_attr_free(attr);
        plc_device_destroy(dev);
        return PLC_INVALID_HANDLE;
    }

    rc = driver->open(dev, attr, timeout_ms);
    plc_attr_free(attr);

    if(rc != PLC_STATUS_OK) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_WARN, 0, "driver open failed: %s", plc_status_str(rc));
        plc_device_t *dead = handle_close(h);
        handle_release(dead);
        return PLC_INVALID_HANDLE;
    }

    pdebug(DEBUG_MODULE_LIB, DEBUG_INFO, (int64_t)h, "device open");
    return h;
}


plc_status_t plc_close(plc_dev_handle_t dev_handle) {
    pdebug(DEBUG_MODULE_LIB, DEBUG_INFO, (int64_t)dev_handle, "closing device");

    plc_device_t *dev = handle_close(dev_handle);
    if(!dev) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_WARN, 0, "bad or already-closed handle %" PRIu64, dev_handle);
        return PLC_STATUS_ERR_BAD_HANDLE;
    }

    /* Stop the subscription background thread before tearing down the driver. */
    if(dev->bg_running) {
        dev->stopping = true;
        plc_thread_join(&dev->bg_thread);
        dev->bg_running = false;
    }

    if(dev->driver && dev->driver->close) { dev->driver->close(dev); }

    handle_release(dev);
    return PLC_STATUS_OK;
}


plc_status_t plc_status(plc_dev_handle_t dev_handle) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }
    /* For now report UP when the device has a driver and no error; DOWN otherwise. */
    plc_status_t st = (dev->driver && dev->last_status == PLC_STATUS_OK)
                    ? (plc_status_t)PLC_STATUS_CONN_UP
                    : (plc_status_t)PLC_STATUS_CONN_DOWN;
    api_unlock(dev);
    return st;
}


/* ---- Diagnostics ---- */

plc_status_t plc_get_last_error(plc_dev_handle_t dev_handle, char *buf, size_t buf_len) {
    if(!buf || buf_len == 0) { return PLC_STATUS_ERR_NULL_PTR; }

    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }

    strncpy(buf, dev->last_error, buf_len - 1);
    buf[buf_len - 1] = '\0';
    plc_status_t rc = dev->last_status;

    api_unlock(dev);
    return rc;
}


const char *plc_status_str(plc_status_t status) {
    switch(status) {
        case PLC_STATUS_OK:                return "OK";
        case PLC_STATUS_PENDING:           return "PENDING";
        case PLC_STATUS_ERR_NULL_PTR:      return "ERR_NULL_PTR";
        case PLC_STATUS_ERR_BAD_HANDLE:    return "ERR_BAD_HANDLE";
        case PLC_STATUS_ERR_BAD_PATH:      return "ERR_BAD_PATH";
        case PLC_STATUS_ERR_TYPE_MISMATCH: return "ERR_TYPE_MISMATCH";
        case PLC_STATUS_ERR_OUT_OF_RANGE:  return "ERR_OUT_OF_RANGE";
        case PLC_STATUS_ERR_TIMEOUT:       return "ERR_TIMEOUT";
        case PLC_STATUS_ERR_IO:            return "ERR_IO";
        case PLC_STATUS_ERR_NO_MEM:        return "ERR_NO_MEM";
        case PLC_STATUS_ERR_NOT_SUPPORTED: return "ERR_NOT_SUPPORTED";
        case PLC_STATUS_ERR_INTERNAL:      return "ERR_INTERNAL";
        default:                           return "UNKNOWN";
    }
}


void plc_free(void *ptr) { free(ptr); }


/* ---- Path resolve helpers ---- */

/*
 * Resolve path+index into a plc_node_t via driver->resolve.
 *
 * for_io=true  (read/write): index is always used as an element subscript,
 *               even when 0.
 * for_io=false (query):      index is only applied when non-zero so that
 *               `plc_get_type(dev, "arr", 0, …)` returns ARRAY, not element.
 */
static plc_status_t api_resolve(plc_device_t *dev, const char *path_str,
                                 int index, bool for_io,
                                 plc_node_t *node, int timeout_ms) {
    if(!dev->driver || !dev->driver->resolve) {
        return PLC_STATUS_ERR_NOT_SUPPORTED;
    }

    plc_path_t p;
    plc_status_t rc = path_parse(path_str ? path_str : "", &p);
    if(rc != PLC_STATUS_OK) { return rc; }

    if(for_io) {
        if(p.n_subs < 3) { p.subs[p.n_subs++] = index; }
    } else {
        if(index != 0 && p.n_subs < 3) { p.subs[p.n_subs++] = index; }
    }

    return dev->driver->resolve(dev, &p, 0, node, timeout_ms);
}

/* Resolve then do a live read from the PLC. */
static plc_status_t api_do_read(plc_device_t *dev, const char *path, int index,
                                 int timeout_ms, plc_value_t *out) {
    plc_node_t node;
    plc_status_t rc = api_resolve(dev, path, index, /*for_io=*/true, &node, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }
    if(!dev->driver->read) { return PLC_STATUS_ERR_NOT_SUPPORTED; }
    rc = dev->driver->read(dev, &node, out, timeout_ms);
    if(rc == PLC_STATUS_OK) {
        cache_put(dev->value_cache, node.tag_name, node.flat_index, out, time_ms());
    }
    return rc;
}

/* Resolve then write to the PLC. */
static plc_status_t api_do_write(plc_device_t *dev, const char *path, int index,
                                  int timeout_ms, const plc_value_t *val) {
    plc_node_t node;
    plc_status_t rc = api_resolve(dev, path, index, /*for_io=*/true, &node, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }
    if(!dev->driver->write) { return PLC_STATUS_ERR_NOT_SUPPORTED; }
    rc = dev->driver->write(dev, &node, val, timeout_ms);
    if(rc == PLC_STATUS_OK) {
        cache_put(dev->value_cache, node.tag_name, node.flat_index, val, time_ms());
    }
    return rc;
}

/* Resolve; if timeout==0, stage a read and return cached value (or sentinel). */
static plc_status_t api_cached_read(plc_device_t *dev, const char *path, int index,
                                     int timeout_ms, plc_value_t *out) {
    if(timeout_ms != 0) {
        return api_do_read(dev, path, index, timeout_ms, out);
    }

    /* Async path: look up cache first. */
    plc_node_t node;
    plc_status_t rc = api_resolve(dev, path, index, /*for_io=*/true, &node, 0);
    if(rc != PLC_STATUS_OK) { return rc; }

    cache_entry_t *e = cache_lookup(dev->value_cache, node.tag_name, node.flat_index);
    if(e && e->valid) {
        *out = e->value;
        return PLC_STATUS_OK;
    }

    /* Cache miss: stage a read for the next flush. */
    staged_enqueue_read(dev->staged_queue, node.tag_name, node.flat_index);
    return PLC_STATUS_PENDING;
}

/* Stage a write (timeout==0) or do it immediately. */
static plc_status_t api_cached_write(plc_device_t *dev, const char *path, int index,
                                      int timeout_ms, const plc_value_t *val) {
    if(timeout_ms != 0) {
        return api_do_write(dev, path, index, timeout_ms, val);
    }

    plc_node_t node;
    plc_status_t rc = api_resolve(dev, path, index, /*for_io=*/true, &node, 0);
    if(rc != PLC_STATUS_OK) { return rc; }

    return staged_enqueue_write(dev->staged_queue, node.tag_name, node.flat_index, val);
}


/* ---- M6: enumeration ---- */

size_t plc_get_count(plc_dev_handle_t dev_handle, const char *path, int index,
                     int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return 0u; }

    size_t count = 0u;

    if(!path || !*path) {
        /* Root: return total number of top-level tags. */
        if(dev->driver && dev->driver->get_tag_count) {
            count = dev->driver->get_tag_count(dev);
        }
    } else {
        plc_node_t node;
        plc_status_t rc = api_resolve(dev, path, index, /*for_io=*/false, &node, timeout_ms);
        if(rc == PLC_STATUS_OK && node.type == PLC_VAL_ARRAY &&
           node.subs_used < node.n_dims) {
            count = (size_t)node.dims[node.subs_used];
        }
    }

    api_unlock(dev);
    return count;
}


plc_value_type_t plc_get_type(plc_dev_handle_t dev_handle, const char *path, int index,
                               int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_VAL_UNKNOWN; }

    plc_value_type_t t = PLC_VAL_UNKNOWN;
    plc_node_t node;
    plc_status_t rc = api_resolve(dev, path, index, /*for_io=*/false, &node, timeout_ms);
    if(rc == PLC_STATUS_OK) { t = node.type; }

    api_unlock(dev);
    return t;
}


size_t plc_get_size(plc_dev_handle_t dev_handle, const char *path, int index,
                    int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return 0u; }

    size_t sz = 0u;
    plc_node_t node;
    plc_status_t rc = api_resolve(dev, path, index, /*for_io=*/false, &node, timeout_ms);
    if(rc == PLC_STATUS_OK) { sz = (size_t)node.elem_size; }

    api_unlock(dev);
    return sz;
}


plc_status_t plc_get_name(plc_dev_handle_t dev_handle, const char *path, int index,
                           char *buf, size_t buf_len, int timeout_ms) {
    (void)timeout_ms;
    if(!buf || buf_len == 0) { return PLC_STATUS_ERR_NULL_PTR; }
    buf[0] = '\0';

    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }

    if(!path || !*path) {
        const char *name = (dev->driver && dev->driver->get_tag_name)
                         ? dev->driver->get_tag_name(dev, (size_t)index)
                         : NULL;
        if(name) {
            strncpy(buf, name, buf_len - 1u);
            buf[buf_len - 1u] = '\0';
        }
    }
    /* Array elements and scalars have no names — buf stays empty. */

    api_unlock(dev);
    return PLC_STATUS_OK;
}


char *plc_get_path(plc_dev_handle_t dev_handle, const char *path, int index,
                   int timeout_ms) {
    (void)timeout_ms;
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return NULL; }

    char *result = NULL;

    if(!path || !*path) {
        /* Root child: return interned tag name duplicated for the caller. */
        const char *name = (dev->driver && dev->driver->get_tag_name)
                         ? dev->driver->get_tag_name(dev, (size_t)index)
                         : NULL;
        if(name) { result = strdup(name); }
    } else {
        /* Array child: append subscript to existing path. */
        plc_node_t node;
        plc_status_t rc = api_resolve(dev, path, 0, /*for_io=*/false, &node, 0);
        if(rc == PLC_STATUS_OK && node.type == PLC_VAL_ARRAY) {
            char buf[256 + 32];
            snprintf(buf, sizeof(buf), "%s[%d]", path, index);
            result = strdup(buf);
        }
    }

    api_unlock(dev);
    return result;
}


/* ---- M5: reads ---- */

int64_t plc_read_int(plc_dev_handle_t dev_handle, const char *path, int index,
                     int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return value_int_sentinel(); }

    plc_value_t v = {0};
    plc_status_t rc = api_cached_read(dev, path, index, timeout_ms, &v);
    int64_t result = (rc == PLC_STATUS_OK && v.type == PLC_VAL_INT)
                   ? v.as.i : value_int_sentinel();

    api_unlock(dev);
    return result;
}


double plc_read_double(plc_dev_handle_t dev_handle, const char *path, int index,
                       int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return value_double_sentinel(); }

    plc_value_t v = {0};
    plc_status_t rc = api_cached_read(dev, path, index, timeout_ms, &v);
    double result = (rc == PLC_STATUS_OK && v.type == PLC_VAL_DOUBLE)
                  ? v.as.d : value_double_sentinel();

    api_unlock(dev);
    return result;
}


bool plc_read_bool(plc_dev_handle_t dev_handle, const char *path, int index,
                   int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return false; }

    plc_value_t v = {0};
    plc_status_t rc = api_cached_read(dev, path, index, timeout_ms, &v);
    bool result = (rc == PLC_STATUS_OK && v.type == PLC_VAL_BOOL) ? v.as.b : false;

    api_unlock(dev);
    return result;
}


plc_status_t plc_read_string(plc_dev_handle_t dev_handle, const char *path, int index,
                              char *buf, size_t buf_len, size_t *actual_len,
                              int timeout_ms) {
    (void)path; (void)index; (void)timeout_ms; (void)buf; (void)buf_len; (void)actual_len;
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }
    api_unlock(dev);
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}


plc_status_t plc_read_bytes(plc_dev_handle_t dev_handle, const char *path, int index,
                             uint8_t *buf, size_t buf_len, size_t *actual_len,
                             int timeout_ms) {
    (void)path; (void)index; (void)timeout_ms; (void)buf; (void)buf_len; (void)actual_len;
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }
    api_unlock(dev);
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}


/* ---- M5: writes (value before timeout_ms per design doc) ---- */

plc_status_t plc_write_int(plc_dev_handle_t dev_handle, const char *path, int index,
                           int64_t value, int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }

    plc_value_t v = { .type = PLC_VAL_INT, .as = { .i = value } };
    plc_status_t rc = api_cached_write(dev, path, index, timeout_ms, &v);

    api_unlock(dev);
    return rc;
}


plc_status_t plc_write_double(plc_dev_handle_t dev_handle, const char *path, int index,
                              double value, int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }

    plc_value_t v = { .type = PLC_VAL_DOUBLE, .as = { .d = value } };
    plc_status_t rc = api_cached_write(dev, path, index, timeout_ms, &v);

    api_unlock(dev);
    return rc;
}


plc_status_t plc_write_bool(plc_dev_handle_t dev_handle, const char *path, int index,
                            bool value, int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }

    plc_value_t v = { .type = PLC_VAL_BOOL, .as = { .b = value } };
    plc_status_t rc = api_cached_write(dev, path, index, timeout_ms, &v);

    api_unlock(dev);
    return rc;
}


plc_status_t plc_write_string(plc_dev_handle_t dev_handle, const char *path, int index,
                              const char *value, int timeout_ms) {
    (void)path; (void)index; (void)timeout_ms; (void)value;
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }
    api_unlock(dev);
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}


plc_status_t plc_write_bytes(plc_dev_handle_t dev_handle, const char *path, int index,
                             const uint8_t *b, size_t len, int timeout_ms) {
    (void)path; (void)index; (void)timeout_ms; (void)b; (void)len;
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }
    api_unlock(dev);
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}


/* ---- M7: flush ---- */

typedef struct { uint32_t flat_index; plc_value_t val; } write_entry_t;

static int flat_index_cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int write_entry_cmp(const void *a, const void *b) {
    return flat_index_cmp_u32(&((const write_entry_t *)a)->flat_index,
                               &((const write_entry_t *)b)->flat_index);
}

plc_status_t plc_flush(plc_dev_handle_t dev_handle, const char *path, int timeout_ms) {
    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }
    if(!dev->driver) { api_unlock(dev); return PLC_STATUS_ERR_NOT_SUPPORTED; }

    staged_queue_t *q = dev->staged_queue;
    plc_status_t   rc = PLC_STATUS_OK;
    size_t         qn = q->count;

    if(qn == 0) { api_unlock(dev); return PLC_STATUS_OK; }

    bool flush_all = (!path || !*path);

    /* Temporary arrays sized to the queue. */
    uint32_t     *read_idx  = malloc(qn * sizeof(uint32_t));
    bool         *done      = calloc(qn, sizeof(bool));
    write_entry_t *write_buf = malloc(qn * sizeof(write_entry_t));

    if(!read_idx || !done || !write_buf) {
        free(read_idx); free(done); free(write_buf);
        api_unlock(dev);
        return PLC_STATUS_ERR_NO_MEM;
    }

    /* --- Pass 1: staged reads --- */
    for(size_t i = 0; i < qn; i++) {
        staged_op_t *op = &q->ops[i];
        if(done[i] || op->kind != STAGE_READ) { continue; }
        if(!flush_all && strncmp(op->tag_name, path, METADATA_MAX_TAG_NAME) != 0) { continue; }

        const char *tname  = op->tag_name;
        size_t      n_idx  = 0;

        for(size_t j = i; j < qn; j++) {
            staged_op_t *op2 = &q->ops[j];
            if(done[j] || op2->kind != STAGE_READ) { continue; }
            if(strncmp(op2->tag_name, tname, METADATA_MAX_TAG_NAME) != 0) { continue; }
            if(!flush_all && strncmp(op2->tag_name, path, METADATA_MAX_TAG_NAME) != 0) { continue; }
            read_idx[n_idx++] = op2->flat_index;
            done[j] = true;
        }

        qsort(read_idx, n_idx, sizeof(uint32_t), flat_index_cmp_u32);

        size_t run_start = 0;
        while(run_start < n_idx) {
            size_t run_end = run_start + 1u;
            while(run_end < n_idx && read_idx[run_end] == read_idx[run_end - 1u] + 1u) {
                run_end++;
            }

            uint16_t count = (uint16_t)(run_end - run_start);
            uint32_t fstart = read_idx[run_start];

            plc_value_t *vals = malloc((size_t)count * sizeof(plc_value_t));
            if(!vals) { rc = PLC_STATUS_ERR_NO_MEM; goto cleanup; }

            if(dev->driver->read_range) {
                plc_status_t rrc = dev->driver->read_range(dev, tname, fstart, count, vals, timeout_ms);
                if(rrc == PLC_STATUS_OK) {
                    int64_t now = time_ms();
                    for(uint16_t k = 0; k < count; k++) {
                        cache_put(dev->value_cache, tname, fstart + k, &vals[k], now);
                    }
                } else if(rc == PLC_STATUS_OK) {
                    rc = rrc;
                }
            }
            free(vals);
            run_start = run_end;
        }
    }

    /* --- Pass 2: staged writes --- */
    memset(done, 0, qn * sizeof(bool));

    for(size_t i = 0; i < qn; i++) {
        staged_op_t *op = &q->ops[i];
        if(done[i] || op->kind != STAGE_WRITE) { continue; }
        if(!flush_all && strncmp(op->tag_name, path, METADATA_MAX_TAG_NAME) != 0) { continue; }

        const char *tname  = op->tag_name;
        size_t      n_wr   = 0;

        for(size_t j = i; j < qn; j++) {
            staged_op_t *op2 = &q->ops[j];
            if(done[j] || op2->kind != STAGE_WRITE) { continue; }
            if(strncmp(op2->tag_name, tname, METADATA_MAX_TAG_NAME) != 0) { continue; }
            if(!flush_all && strncmp(op2->tag_name, path, METADATA_MAX_TAG_NAME) != 0) { continue; }
            write_buf[n_wr].flat_index = op2->flat_index;
            write_buf[n_wr].val        = op2->write_val;
            n_wr++;
            done[j] = true;
        }

        qsort(write_buf, n_wr, sizeof(write_entry_t), write_entry_cmp);

        size_t run_start = 0;
        while(run_start < n_wr) {
            size_t run_end = run_start + 1u;
            while(run_end < n_wr &&
                  write_buf[run_end].flat_index == write_buf[run_end - 1u].flat_index + 1u) {
                run_end++;
            }

            uint16_t    count  = (uint16_t)(run_end - run_start);
            uint32_t    fstart = write_buf[run_start].flat_index;
            plc_value_t *vals  = malloc((size_t)count * sizeof(plc_value_t));
            if(!vals) { rc = PLC_STATUS_ERR_NO_MEM; goto cleanup; }

            for(uint16_t k = 0; k < count; k++) {
                vals[k] = write_buf[run_start + k].val;
            }

            if(dev->driver->write_range) {
                plc_status_t wrc = dev->driver->write_range(dev, tname, fstart, count, vals, timeout_ms);
                if(wrc != PLC_STATUS_OK && rc == PLC_STATUS_OK) { rc = wrc; }
            }
            free(vals);
            run_start = run_end;
        }
    }

cleanup:
    free(read_idx);
    free(done);
    free(write_buf);

    /* Remove all flushed ops from the queue. */
    staged_remove(q, flush_all ? NULL : path);

    api_unlock(dev);
    return rc;
}


/* ---- M8: subscriptions ---- */

plc_status_t plc_subscribe(plc_dev_handle_t dev_handle, const char *path, int read_interval_ms) {
    if(!path || !*path || read_interval_ms <= 0) { return PLC_STATUS_ERR_NULL_PTR; }

    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }

    plc_status_t rc = PLC_STATUS_OK;
    subscription_list_t *sl = &dev->subscriptions;

    /* Update existing subscription for this path. */
    for(size_t i = 0; i < sl->count; i++) {
        if(sl->subs[i].active &&
           strncmp(sl->subs[i].path, path, sizeof(sl->subs[i].path)) == 0) {
            sl->subs[i].interval_ms = read_interval_ms;
            goto done;
        }
    }

    /* Find an inactive slot. */
    plc_subscription_t *slot = NULL;
    for(size_t i = 0; i < sl->count; i++) {
        if(!sl->subs[i].active) { slot = &sl->subs[i]; break; }
    }
    if(!slot) {
        if(sl->count >= PLC_MAX_SUBSCRIPTIONS) {
            rc = PLC_STATUS_ERR_NOT_SUPPORTED;
            goto done;
        }
        slot = &sl->subs[sl->count++];
    }

    strncpy(slot->path, path, sizeof(slot->path) - 1u);
    slot->path[sizeof(slot->path) - 1u] = '\0';
    slot->interval_ms  = read_interval_ms;
    slot->next_read_ms = time_ms();
    slot->active       = true;

    /* Start background thread on first subscription. */
    if(!dev->bg_running) {
        dev->stopping   = false;
        plc_status_t tr = plc_thread_create(&dev->bg_thread, subscription_thread, dev);
        if(tr == PLC_STATUS_OK) {
            dev->bg_running = true;
        } else {
            slot->active = false;
            rc = tr;
        }
    }

done:
    api_unlock(dev);
    return rc;
}


plc_status_t plc_unsubscribe(plc_dev_handle_t dev_handle, const char *path) {
    if(!path) { return PLC_STATUS_ERR_NULL_PTR; }

    plc_device_t *dev = api_lock(dev_handle);
    if(!dev) { return PLC_STATUS_ERR_BAD_HANDLE; }

    subscription_list_t *sl = &dev->subscriptions;
    for(size_t i = 0; i < sl->count; i++) {
        if(sl->subs[i].active &&
           strncmp(sl->subs[i].path, path, sizeof(sl->subs[i].path)) == 0) {
            sl->subs[i].active = false;
            break;
        }
    }

    api_unlock(dev);
    return PLC_STATUS_OK;
}


int plc_poll_events(plc_dev_handle_t dev_handle, plc_event_t *events,
                    size_t max_events, int timeout_ms) {
    if(!events || max_events == 0) { return -1; }

    int64_t deadline = time_ms() + (int64_t)timeout_ms;

    for(;;) {
        plc_device_t *dev = api_lock(dev_handle);
        if(!dev) { return -1; }

        event_queue_t *q = &dev->events;
        size_t n = 0;
        while(n < max_events && q->count > 0) {
            events[n++] = q->entries[q->tail];
            q->tail     = (q->tail + 1u) % PLC_EVENT_QUEUE_SIZE;
            q->count--;
        }

        api_unlock(dev);

        if(n > 0) { return (int)n; }
        if(timeout_ms == 0) { return 0; }
        if(time_ms() >= deadline) { return 0; }

        plc_thread_sleep_ms(10);
    }
}
