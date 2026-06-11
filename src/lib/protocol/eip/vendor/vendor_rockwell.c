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
 * vendor_rockwell.c — Rockwell/Allen-Bradley EtherNet/IP behaviour.
 *
 * Tag listing (list_tags):
 *   Uses CIP service 0x55 (Get_Instance_Attribute_List) on the Logix
 *   Symbol object class 0x6B.  Requested attributes:
 *     1 = Symbol Name  (SHORT_STRING: uint8 len + chars, no null)
 *     2 = Symbol Type  (uint16 CIP type descriptor)
 *     7 = Dimensions   (3 × uint32, all 0 for scalars)
 *
 *   The PLC returns as many entries as fit in the response PDU.
 *   Paging continues (incrementing the starting instance) until the PLC
 *   returns CIP status 0x06 ("path segment error" = no more instances).
 *
 *   The EPATH for each request includes s->cip_route (the backplane / port
 *   segments to the CPU) prepended to the class/instance segment, so it
 *   works both for direct-connect controllers and for backplane-routed ones.
 */

#include <string.h>

#include "protocol/eip/vendor/vendor.h"
#include "protocol/eip/cip.h"
#include "debug.h"

/* Logix Symbol class and attribute IDs */
#define LOGIX_CLASS_SYMBOL  0x6Bu
#define LOGIX_ATTR_NAME     0x0001u
#define LOGIX_ATTR_TYPE     0x0002u
#define LOGIX_ATTR_DIMS     0x0007u

/* CIP status returned by Logix when there are no more symbol instances */
#define CIP_STATUS_END_OF_INSTANCES 0x06u


/*
 * Build an EPATH that routes through s->cip_route and then addresses
 * class_id / inst_id.  Allocated in s->io_arena.
 */
static Bytes routed_epath(eip_session_t *s, uint16_t class_id,
                           uint32_t inst_id) {
    Bytes route = bytes_from_buf(s->cip_route, s->cip_route_len);
    Bytes ci    = cip_epath_class_inst(&s->io_arena, class_id, inst_id);
    if(bytes_is_null(ci)) { return bytes_null(); }
    return bytes_concat(&s->io_arena, route, ci);
}


/*
 * Parse one page of Get_Instance_Attribute_List response data into *cache.
 * Returns the instance ID of the last successfully parsed entry (or
 * *start_id - 1 if nothing was parsed), so the caller can page forward.
 *
 * Per-entry layout:
 *   uint32  instance_id
 *   uint8   name_len
 *   char    name[name_len]   (no null terminator, no alignment padding)
 *   uint16  type_code
 *   uint32  dim[0]
 *   uint32  dim[1]
 *   uint32  dim[2]
 */
static uint32_t parse_tag_page(Bytes body, uint32_t prev_last_id,
                                metadata_cache_t *cache) {
    Bytes rest = body;
    uint32_t last_id = prev_last_id;

    while(rest.len >= 4u) {
        uint32_t inst_id = 0;
        Bytes after_id = bytes_unpack(rest, BYTES_LE, &inst_id);
        if(bytes_is_null(after_id)) { break; }

        uint8_t name_len = 0;
        Bytes after_len = bytes_unpack(after_id, BYTES_LE, &name_len);
        if(bytes_is_null(after_len)) { break; }
        if(after_len.len < (size_t)name_len) { break; }

        /* Copy name into a local buffer (not null-terminated in the packet) */
        char name_buf[METADATA_MAX_TAG_NAME];
        size_t copy = (size_t)name_len < sizeof(name_buf) - 1u
                    ? (size_t)name_len
                    : sizeof(name_buf) - 1u;
        memcpy(name_buf, after_len.data, copy);
        name_buf[copy] = '\0';

        Bytes after_name = bytes_skip(after_len, (size_t)name_len);
        if(bytes_is_null(after_name)) { break; }

        /* Attributes 2 and 7 follow the name with no padding */
        uint16_t type_code = 0;
        uint32_t dim0 = 0, dim1 = 0, dim2 = 0;
        Bytes after_attrs = bytes_unpack(after_name, BYTES_LE,
                                         &type_code, &dim0, &dim1, &dim2);
        if(bytes_is_null(after_attrs)) { break; }

        /* Commit this entry */
        metadata_tag_t tag;
        memset(&tag, 0, sizeof(tag));
        strncpy(tag.name, name_buf, sizeof(tag.name) - 1u);
        tag.type_id     = type_code;
        tag.dims[0]     = dim0;
        tag.dims[1]     = dim1;
        tag.dims[2]     = dim2;
        tag.instance_id = inst_id;

        if(metadata_cache_add(cache, &tag) != PLC_STATUS_OK) {
            pdebug(DEBUG_MODULE_CIP, DEBUG_WARN, 0,
                   "tag list: metadata_cache_add OOM, stopping early");
            break;
        }

        last_id = inst_id;
        rest = after_attrs;
    }

    return last_id;
}


static plc_status_t rockwell_list_tags(eip_session_t *s,
                                        metadata_cache_t *cache,
                                        int timeout_ms) {
    if(!s || !cache) { return PLC_STATUS_ERR_NULL_PTR; }

    pdebug(DEBUG_MODULE_CIP, DEBUG_INFO, 0, "Rockwell: listing tags");

    uint32_t start_id  = 1u;
    uint32_t last_id   = 0u;
    size_t   total     = 0u;
    bool     done      = false;

    while(!done) {
        arena_reset(&s->io_arena);

        /* EPATH: backplane route (if any) + class 0x6B, instance = start_id */
        Bytes epath = routed_epath(s, LOGIX_CLASS_SYMBOL, start_id);
        if(bytes_is_null(epath)) { return PLC_STATUS_ERR_NO_MEM; }

        /* Request data: attribute count + attribute IDs */
        Bytes req_data = bytes_pack(&s->io_arena, BYTES_LE,
            (uint16_t)3u,
            (uint16_t)LOGIX_ATTR_NAME,
            (uint16_t)LOGIX_ATTR_TYPE,
            (uint16_t)LOGIX_ATTR_DIMS);
        if(bytes_is_null(req_data)) { return PLC_STATUS_ERR_NO_MEM; }

        Bytes req = cip_build_request(&s->io_arena,
                                      CIP_GET_INSTANCE_ATTR_LIST,
                                      epath, req_data);
        if(bytes_is_null(req)) { return PLC_STATUS_ERR_NO_MEM; }

        Bytes cip_resp = bytes_null();
        plc_status_t rc = eip_send_rr_data(s, req, &s->io_arena,
                                            &cip_resp, timeout_ms);
        if(rc != PLC_STATUS_OK) { return rc; }

        cip_reply_t reply;
        rc = cip_parse_reply(cip_resp, &reply);
        if(rc != PLC_STATUS_OK) { return rc; }

        if(reply.status == CIP_STATUS_END_OF_INSTANCES) {
            done = true; /* no more instances — final partial page */
            /* reply.data may still have the last batch of entries */
        } else if(reply.status != 0u) {
            pdebug(DEBUG_MODULE_CIP, DEBUG_WARN, 0,
                   "tag list: unexpected CIP status 0x%02X ext 0x%04X",
                   reply.status, reply.ext_status);
            return cip_status_to_plc(reply.status, reply.ext_status);
        }

        size_t before = cache->count;
        last_id = parse_tag_page(reply.data, start_id - 1u, cache);
        size_t got = cache->count - before;
        total += got;

        pdebug(DEBUG_MODULE_CIP, DEBUG_DETAIL, 0,
               "tag list: got %zu entries (start=%u, last=%u)",
               got, start_id, last_id);

        if(got == 0u || done) {
            done = true;
        } else {
            start_id = last_id + 1u;
        }
    }

    pdebug(DEBUG_MODULE_CIP, DEBUG_INFO, 0,
           "Rockwell: tag list complete, %zu tags", total);
    return PLC_STATUS_OK;
}


const eip_vendor_t eip_vendor_rockwell = {
    .name      = "Rockwell/Allen-Bradley",
    .list_tags = rockwell_list_tags,
};
