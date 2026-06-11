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
 * driver.h — protocol driver vtable.
 *
 * A driver is selected once at plc_open time by matching the "protocol"
 * attribute against driver_vtable_t.scheme.  All behavior differences between
 * protocols (EtherNet/IP, Modbus, …) are expressed solely through this table;
 * there are no protocol-type checks in the shared call paths.
 *
 * plc_path_t (core/path.h, M4) and plc_node_t (later) are forward-declared
 * here so the vtable types compile before those headers exist.
 */

#include "plctag.h"
#include "core/attr.h"
#include "core/value.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for types defined in later phases. */
typedef struct plc_device_t plc_device_t; /* core/device.h */
typedef struct plc_path_t   plc_path_t;   /* core/path.h   (M4) */
typedef struct plc_node_t   plc_node_t;   /* core/node.h   (later) */

typedef struct plc_driver_vtable_t {
    /* Protocol identifier; matched against the "protocol" connection attribute. */
    const char *scheme;

    /* Run the full connection sequence (TCP, session, identity, Forward Open,
       metadata phase 1).  dev->driver_state is uninitialised on entry; on
       success the driver allocates and assigns its own state there. */
    plc_status_t (*open)(plc_device_t *dev, const plc_attr_t *attr, int timeout_ms);

    /* Tear down cleanly (protocol-level close, socket close, free driver_state).
       Called with dev->driver_state valid; must tolerate partial open states
       (open may have failed partway through). */
    void (*close)(plc_device_t *dev);

    /* Resolve a parsed path + trailing index to a typed, sized, addressable
       node.  Fetches metadata on demand.  (M4) */
    plc_status_t (*resolve)(plc_device_t *dev, const plc_path_t *path, int index,
                            plc_node_t *out_node, int timeout_ms);

    /* Transfer a resolved node.  (M5) */
    plc_status_t (*read)(plc_device_t *dev, const plc_node_t *node, plc_value_t *out, int timeout_ms);
    plc_status_t (*write)(plc_device_t *dev, const plc_node_t *node, const plc_value_t *val, int timeout_ms);

    /* Root tag enumeration — return tag count / interned tag name.  (M6) */
    size_t       (*get_tag_count)(plc_device_t *dev);
    const char  *(*get_tag_name)(plc_device_t *dev, size_t index);

    /* Coalesced multi-element transfer for flush.  (M7)
     * flat_index_start is the first element; out/vals[0..elem_count-1] hold results. */
    plc_status_t (*read_range)(plc_device_t *dev, const char *tag_name,
                               uint32_t flat_index_start, uint16_t elem_count,
                               plc_value_t *out, int timeout_ms);
    plc_status_t (*write_range)(plc_device_t *dev, const char *tag_name,
                                uint32_t flat_index_start, uint16_t elem_count,
                                const plc_value_t *vals, int timeout_ms);
} plc_driver_vtable_t;

#ifdef __cplusplus
}
#endif
