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

#include <string.h>

#include "protocol/eip/identity.h"
#include "protocol/eip/eip.h"
#include "protocol/eip/cip.h"
#include "debug.h"

/*
 * Identity object Get_Attributes_All reply layout (all LE):
 *   vendor_id    uint16
 *   device_type  uint16
 *   product_code uint16
 *   rev_major    uint8
 *   rev_minor    uint8
 *   status       uint16
 *   serial_number uint32
 *   name_len     uint8
 *   product_name name_len bytes (NOT null-terminated)
 *   state        uint8   (optional; absent on some devices)
 */

plc_status_t eip_get_identity(eip_session_t *s, eip_identity_t *out, int timeout_ms) {
    if(!s || !out) { return PLC_STATUS_ERR_NULL_PTR; }

    /*
     * We use s->io_arena for the CIP request (caller convention) and also
     * pass it as the response arena.  eip_send_rr_data resets s->io_arena
     * after sending, so after the call the response is at offset 0 of
     * s->io_arena and remains valid until the next arena_reset.
     */
    arena_reset(&s->io_arena);

    Bytes epath = cip_epath_class_inst(&s->io_arena, 0x01, 1);
    if(bytes_is_null(epath)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes req = cip_build_request(&s->io_arena, CIP_GET_ATTR_ALL, epath, bytes_null());
    if(bytes_is_null(req)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes cip_resp = bytes_null();
    plc_status_t rc = eip_send_rr_data(s, req, &s->io_arena, &cip_resp, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    cip_reply_t reply;
    rc = cip_parse_reply(cip_resp, &reply);
    if(rc != PLC_STATUS_OK) { return rc; }

    if(reply.status != 0) {
        pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0,
               "Get_Attributes_All (Identity): status=0x%02X ext=0x%04X",
               reply.status, reply.ext_status);
        return cip_status_to_plc(reply.status, reply.ext_status);
    }

    uint16_t vendor_id, device_type, product_code, status_word;
    uint8_t  rev_major, rev_minor, name_len;
    uint32_t serial_number;

    Bytes rest = bytes_unpack(reply.data, BYTES_LE,
        &vendor_id, &device_type, &product_code,
        &rev_major, &rev_minor,
        &status_word,
        &serial_number,
        &name_len);
    if(bytes_is_null(rest)) {
        pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0, "Identity reply too short");
        return PLC_STATUS_ERR_IO;
    }

    size_t copy_len = name_len < sizeof(out->product_name) - 1u
                    ? (size_t)name_len
                    : sizeof(out->product_name) - 1u;

    memset(out, 0, sizeof(*out));
    out->vendor_id      = vendor_id;
    out->device_type    = device_type;
    out->product_code   = product_code;
    out->revision_major = rev_major;
    out->revision_minor = rev_minor;
    out->status_word    = status_word;
    out->serial_number  = serial_number;
    if(rest.data && copy_len > 0) {
        memcpy(out->product_name, rest.data, copy_len);
    }
    out->product_name[copy_len] = '\0';

    pdebug(DEBUG_MODULE_ENIP, DEBUG_INFO, 0,
           "Identity: vendor=%u type=%u code=%u rev=%u.%u serial=0x%08X \"%s\"",
           vendor_id, device_type, product_code,
           rev_major, rev_minor, serial_number, out->product_name);

    return PLC_STATUS_OK;
}
