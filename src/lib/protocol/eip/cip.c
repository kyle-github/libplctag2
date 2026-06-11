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

#include "protocol/eip/cip.h"
#include "debug.h"


Bytes cip_build_request(Arena *a, uint8_t service, Bytes epath, Bytes data) {
    uint8_t path_size_words = (uint8_t)(epath.len / 2);
    Bytes prefix = bytes_pack(a, BYTES_LE,
        (uint8_t)service,
        (uint8_t)path_size_words);
    if(bytes_is_null(prefix)) { return bytes_null(); }
    return bytes_concat(a, prefix, epath, data);
}


plc_status_t cip_parse_reply(Bytes buf, cip_reply_t *out) {
    if(!out) { return PLC_STATUS_ERR_NULL_PTR; }
    if(buf.len < 4) {
        pdebug(DEBUG_MODULE_CIP, DEBUG_WARN, 0, "CIP reply too short: %zu bytes", buf.len);
        return PLC_STATUS_ERR_IO;
    }

    uint8_t service, reserved, gen_status, add_size;
    Bytes rest = bytes_unpack(buf, BYTES_LE,
        &service, &reserved, &gen_status, &add_size);
    if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }

    uint16_t ext_status = 0;
    if(add_size > 0) {
        rest = bytes_unpack(rest, BYTES_LE, &ext_status);
        if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }
        /* skip any remaining additional status words beyond the first */
        if(add_size > 1) {
            rest = bytes_skip(rest, (size_t)(add_size - 1u) * 2u);
            if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }
        }
    }

    out->service    = service;
    out->status     = gen_status;
    out->ext_status = ext_status;
    out->data       = rest;
    return PLC_STATUS_OK;
}


plc_status_t cip_status_to_plc(uint8_t cip_status, uint16_t ext_status) {
    (void)ext_status;
    switch(cip_status) {
        case 0x00: return PLC_STATUS_OK;
        case 0x08: return PLC_STATUS_ERR_NOT_SUPPORTED; /* service not supported */
        case 0x14:
        case 0x20:
        case 0x26: return PLC_STATUS_ERR_BAD_PATH;      /* invalid path / too large */
        case 0x01: return PLC_STATUS_ERR_IO;             /* connection failure */
        default:   return PLC_STATUS_ERR_IO;
    }
}


/* --- EPATH builders --- */

Bytes cip_epath_class_inst(Arena *a, uint16_t class_id, uint32_t instance_id) {
    Bytes cls;
    if(class_id <= 0xFF) {
        cls = bytes_pack(a, BYTES_LE, (uint8_t)0x20, (uint8_t)class_id);
    } else {
        cls = bytes_pack(a, BYTES_LE, (uint8_t)0x21, (uint8_t)0, (uint16_t)class_id);
    }
    if(bytes_is_null(cls)) { return bytes_null(); }

    Bytes inst;
    if(instance_id <= 0xFF) {
        inst = bytes_pack(a, BYTES_LE, (uint8_t)0x24, (uint8_t)instance_id);
    } else if(instance_id <= 0xFFFF) {
        inst = bytes_pack(a, BYTES_LE, (uint8_t)0x25, (uint8_t)0, (uint16_t)instance_id);
    } else {
        inst = bytes_pack(a, BYTES_LE, (uint8_t)0x26, (uint8_t)0, (uint32_t)instance_id);
    }
    if(bytes_is_null(inst)) { return bytes_null(); }

    return bytes_concat(a, cls, inst);
}


Bytes cip_epath_class_inst_attr(Arena *a, uint16_t class_id, uint32_t instance_id,
                                 uint16_t attr_id) {
    Bytes ci = cip_epath_class_inst(a, class_id, instance_id);
    if(bytes_is_null(ci)) { return bytes_null(); }

    Bytes attr;
    if(attr_id <= 0xFF) {
        attr = bytes_pack(a, BYTES_LE, (uint8_t)0x30, (uint8_t)attr_id);
    } else {
        attr = bytes_pack(a, BYTES_LE, (uint8_t)0x31, (uint8_t)0, (uint16_t)attr_id);
    }
    if(bytes_is_null(attr)) { return bytes_null(); }

    return bytes_concat(a, ci, attr);
}


Bytes cip_epath_symbolic(Arena *a, const char *name) {
    if(!name || !*name) { return bytes_null(); }

    size_t len = strlen(name);
    /* ANSI extended symbol: 0x91, len_byte, name_chars, [pad] */
    Bytes prefix = bytes_pack(a, BYTES_LE, (uint8_t)0x91, (uint8_t)len);
    if(bytes_is_null(prefix)) { return bytes_null(); }

    Bytes str = bytes_from_buf((const uint8_t *)name, len);
    Bytes seg  = bytes_concat(a, prefix, str);
    if(bytes_is_null(seg)) { return bytes_null(); }

    return bytes_pad_even(a, seg);
}


Bytes cip_epath_element(Arena *a, uint32_t element) {
    if(element <= 0xFFu) {
        return bytes_pack(a, BYTES_LE, (uint8_t)0x28, (uint8_t)element);
    } else if(element <= 0xFFFFu) {
        return bytes_pack(a, BYTES_LE, (uint8_t)0x29, (uint8_t)0, (uint16_t)element);
    } else {
        return bytes_pack(a, BYTES_LE, (uint8_t)0x2A, (uint8_t)0, (uint32_t)element);
    }
}
