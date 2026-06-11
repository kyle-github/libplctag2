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
 * vendor.h — per-manufacturer EtherNet/IP behaviour vtable.
 *
 * The vendor is selected once after Identity is read (eip_vendor_select).
 * It handles features that differ across manufacturers within EtherNet/IP:
 *   - tag enumeration (Logix class-0x6B listing vs. no-op for generic devices)
 *   - (M4) UDT / template fetching
 *
 * eip_vendor_t is a const object; all state lives in eip_session_t.
 */

#include "protocol/eip/eip.h"
#include "core/metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The full definition of eip_vendor_t; forward-declared in eip.h. */
struct eip_vendor_t {
    const char *name;

    /* Enumerate all top-level tags and populate *cache.
       Called once during eip_driver_open, after Forward Open.
       May be a no-op for devices without a tag directory. */
    plc_status_t (*list_tags)(eip_session_t *s, metadata_cache_t *cache,
                               int timeout_ms);

    /* M4: fetch UDT template (type_id) and add to *cache.
    plc_status_t (*get_udt)(eip_session_t *s, metadata_cache_t *cache,
                             uint16_t type_id, int timeout_ms); */
};

/* Select the vendor implementation from the identity probe result.
   Always returns a non-NULL pointer (falls back to the generic vendor). */
const eip_vendor_t *eip_vendor_select(const eip_identity_t *id);

#ifdef __cplusplus
}
#endif
