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
 * cip.h — CIP message construction, reply parsing, and EPATH builders.
 * Independent of vendor and of the EIP encapsulation layer.
 */

#include <stdint.h>
#include "plctag.h"
#include "arena.h"
#include "bytes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- CIP service codes --- */
#define CIP_GET_ATTR_ALL           0x01u
#define CIP_GET_ATTR_LIST          0x03u
#define CIP_GET_ATTR_SINGLE        0x0Eu
#define CIP_SET_ATTR_SINGLE        0x10u
#define CIP_GET_INSTANCE_ATTR_LIST 0x55u
#define CIP_FWD_CLOSE              0x4Eu
#define CIP_READ_TAG               0x4Cu
#define CIP_WRITE_TAG              0x4Du
#define CIP_FWD_OPEN               0x54u
#define CIP_FWD_OPEN_EX            0x5Bu

/* Bit set on service byte in all CIP replies. */
#define CIP_REPLY_FLAG             0x80u

/* --- Parsed CIP reply --- */
typedef struct {
    uint8_t  service;       /* request service | CIP_REPLY_FLAG */
    uint8_t  status;        /* general status byte (0 = success) */
    uint16_t ext_status;    /* first additional status word; 0 if none */
    Bytes    data;          /* payload after the status block */
} cip_reply_t;

/* Build a CIP request: [service(1), path_size_words(1), epath..., data...].
   epath.len must be even. Returns bytes_null() on arena OOM. */
Bytes        cip_build_request(Arena *a, uint8_t service, Bytes epath, Bytes data);

/* Parse a CIP reply buffer into *out.  Returns ERR_IO if buf is too short. */
plc_status_t cip_parse_reply(Bytes buf, cip_reply_t *out);

/* Map a CIP general status to plc_status_t. */
plc_status_t cip_status_to_plc(uint8_t cip_status, uint16_t ext_status);

/* --- EPATH builders ---
 * All return bytes_null() on arena OOM. */

/* Logical class + instance (2 or 3 segments). */
Bytes cip_epath_class_inst(Arena *a, uint16_t class_id, uint32_t instance_id);

/* Logical class + instance + attribute. */
Bytes cip_epath_class_inst_attr(Arena *a, uint16_t class_id, uint32_t instance_id, uint16_t attr_id);

/* ANSI extended symbolic segment for a tag name, padded to even length. */
Bytes cip_epath_symbolic(Arena *a, const char *name);

/* Logical element segment for array subscript (Read/Write Tag EPATH). */
Bytes cip_epath_element(Arena *a, uint32_t element);

#ifdef __cplusplus
}
#endif
