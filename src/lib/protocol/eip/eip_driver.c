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
 * eip_driver.c — implements plc_driver_vtable_t for the "eip" scheme.
 *
 * M1 connection sequence (eip_driver_open):
 *   1. Parse gateway / port / path from attr.
 *   2. eip_session_create
 *   3. eip_connect            (TCP)
 *   4. eip_register_session   (EIP session)
 *   5. eip_get_identity       (CIP Identity object)
 *   6. Store session in dev->driver_state.
 *
 * Steps 7–9 (vendor select, Forward Open, metadata) are M2/M3.
 */

#include <stdlib.h>
#include <string.h>
#include <float.h>

#include "protocol/eip/eip_driver.h"
#include "protocol/eip/eip.h"
#include "protocol/eip/cip.h"
#include "protocol/eip/identity.h"
#include "protocol/eip/connection.h"
#include "protocol/eip/vendor/vendor.h"
#include "core/attr.h"
#include "core/device.h"
#include "core/metadata.h"
#include "core/path.h"
#include "core/node.h"
#include "core/value.h"
#include "debug.h"

/* ---- CIP route parser ---- */

/*
 * Parse a path string of the form "port,link[,port,link,...]" into raw CIP
 * port segment bytes stored in buf (max buf_size bytes).
 *
 * Each (port, link) pair encodes as:
 *   link is decimal integer: [0x00|port, link_byte]  (2 bytes)
 *   link is a string (IP):   [0x10|port, len, chars..., pad]
 *
 * Returns the number of bytes written, or 0 on parse error.
 */
static size_t parse_cip_route(const char *path, uint8_t *buf, size_t buf_size) {
    if(!path || !*path) { return 0; }

    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    size_t out = 0;
    char *tok = tmp;

    while(*tok) {
        /* --- port token --- */
        char *comma = strchr(tok, ',');
        if(!comma) { break; } /* need at least one more token */
        *comma = '\0';

        int port = (int)strtol(tok, NULL, 10);
        tok = comma + 1;

        /* --- link token --- */
        comma = strchr(tok, ',');
        if(comma) { *comma = '\0'; }

        /* Decide: is the link token a pure integer? */
        char *end = NULL;
        long link_int = strtol(tok, &end, 10);
        bool is_int = (end != tok && (*end == '\0'));

        if(is_int) {
            /* Simple byte link address: [0x00|port, link] */
            if(out + 2 > buf_size) { return 0; }
            buf[out++] = (uint8_t)(port & 0x0F);
            buf[out++] = (uint8_t)(link_int & 0xFF);
        } else {
            /* String link address (e.g., IP): [0x10|port, len, chars..., pad] */
            size_t len = strlen(tok);
            size_t needed = 2 + len + (len & 1u); /* header + chars + optional pad */
            if(out + needed > buf_size) { return 0; }
            buf[out++] = (uint8_t)(0x10u | (port & 0x0F));
            buf[out++] = (uint8_t)(len & 0xFF);
            memcpy(buf + out, tok, len);
            out += len;
            if(len & 1u) { buf[out++] = 0; } /* pad to even */
        }

        tok = comma ? comma + 1 : tok + strlen(tok);
    }

    return out;
}


/* ---- Driver vtable implementation ---- */

static plc_status_t eip_driver_open(plc_device_t *dev, const plc_attr_t *attr,
                                     int timeout_ms) {
    const char *host = plc_attr_get_str(attr, "gateway", NULL);
    if(!host || !*host) {
        pdebug(DEBUG_MODULE_PROTOCOL, DEBUG_ERROR, 0,
               "missing 'gateway' in connection string");
        return PLC_STATUS_ERR_BAD_PATH;
    }

    int port_int = plc_attr_get_int(attr, "port", (int)EIP_DEFAULT_PORT);
    uint16_t port = (uint16_t)(port_int > 0 ? (unsigned)port_int : EIP_DEFAULT_PORT);

    const char *path = plc_attr_get_str(attr, "path", "");

    eip_session_t *s = NULL;
    plc_status_t rc = eip_session_create(&s, host, port);
    if(rc != PLC_STATUS_OK) { return rc; }

    /* Parse CIP route from the "path" attribute */
    s->cip_route_len = parse_cip_route(path, s->cip_route, sizeof(s->cip_route));
    if(*path && s->cip_route_len == 0) {
        pdebug(DEBUG_MODULE_PROTOCOL, DEBUG_WARN, 0,
               "could not parse path \"%s\"; proceeding without route", path);
    }

    rc = eip_connect(s, timeout_ms);
    if(rc != PLC_STATUS_OK) { goto fail; }

    rc = eip_register_session(s, timeout_ms);
    if(rc != PLC_STATUS_OK) { goto fail; }

    rc = eip_get_identity(s, &s->identity, timeout_ms);
    if(rc != PLC_STATUS_OK) { goto fail; }

    s->vendor = eip_vendor_select(&s->identity);

    rc = eip_open_connection(s, timeout_ms);
    if(rc != PLC_STATUS_OK) { goto fail; }

    rc = metadata_cache_create(&s->metadata);
    if(rc != PLC_STATUS_OK) { goto fail; }

    rc = s->vendor->list_tags(s, s->metadata, timeout_ms);
    if(rc != PLC_STATUS_OK) { goto fail; }

    dev->driver_state = s;
    pdebug(DEBUG_MODULE_PROTOCOL, DEBUG_INFO, 0,
           "EIP connection ready: %s:%u \"%s\"",
           host, port, s->identity.product_name);
    return PLC_STATUS_OK;

fail:
    eip_session_destroy(s);
    return rc;
}


static void eip_driver_close(plc_device_t *dev) {
    if(!dev || !dev->driver_state) { return; }

    eip_session_t *s = (eip_session_t *)dev->driver_state;

    metadata_cache_destroy(s->metadata);
    s->metadata = NULL;
    eip_forward_close(s, 2000);
    eip_unregister_session(s);
    eip_session_destroy(s);
    dev->driver_state = NULL;
}


/* ---- CIP type helpers ---- */

static plc_value_type_t cip_to_plc_type(uint16_t t) {
    if(t & 0x8000u) { return PLC_VAL_STRUCT; }
    switch(t & 0xFFu) {
        case 0xC1:                                  return PLC_VAL_BOOL;
        case 0xC2: case 0xC3: case 0xC4: case 0xC5:
        case 0xC6: case 0xC7: case 0xC8: case 0xC9:
        case 0xD0: case 0xD1: case 0xD2: case 0xD3: return PLC_VAL_INT;
        case 0xCA: case 0xCB:                        return PLC_VAL_DOUBLE;
        default:                                     return PLC_VAL_UNKNOWN;
    }
}

static uint32_t cip_elem_size(uint16_t t) {
    switch(t & 0xFFu) {
        case 0xC1: case 0xC2: case 0xC6: case 0xD0: return 1;
        case 0xC3: case 0xC7: case 0xD1:             return 2;
        case 0xC4: case 0xC8: case 0xCA: case 0xD2:  return 4;
        case 0xC5: case 0xC9: case 0xCB: case 0xD3:  return 8;
        default:                                      return 0;
    }
}

static uint32_t count_dims(const metadata_tag_t *tag) {
    if(tag->dims[0] == 0) { return 0; }
    if(tag->dims[1] == 0) { return 1; }
    if(tag->dims[2] == 0) { return 2; }
    return 3;
}


/* ---- M4: resolve ---- */

static plc_status_t eip_driver_resolve(plc_device_t *dev,
                                        const plc_path_t *path, int index,
                                        plc_node_t *out_node, int timeout_ms) {
    (void)index; (void)timeout_ms; /* index already embedded in path->subs by api.c */
    if(!dev || !path || !out_node) { return PLC_STATUS_ERR_NULL_PTR; }

    eip_session_t *s = (eip_session_t *)dev->driver_state;
    if(!s || !s->metadata) { return PLC_STATUS_ERR_NOT_SUPPORTED; }

    const metadata_tag_t *tag = metadata_cache_find(s->metadata, path->tag_name);
    if(!tag) {
        pdebug(DEBUG_MODULE_PATH, DEBUG_WARN, 0, "tag \"%s\" not found", path->tag_name);
        return PLC_STATUS_ERR_BAD_PATH;
    }

    uint32_t n_dims = count_dims(tag);

    /* Compute flat element index from path subscripts. */
    uint32_t flat = 0;
    if(n_dims >= 1 && path->n_subs >= 1) {
        flat = (uint32_t)path->subs[0];
        if(n_dims >= 2 && path->n_subs >= 2) {
            flat = flat * tag->dims[1] + (uint32_t)path->subs[1];
        }
        if(n_dims >= 3 && path->n_subs >= 3) {
            flat = flat * tag->dims[2] + (uint32_t)path->subs[2];
        }
    }

    /* Determine the node type: if path subscripts fully index into all dims,
       it's a scalar element; if not (or tag is scalar), it's the array/scalar
       root. */
    bool fully_indexed = (n_dims == 0) || ((uint32_t)path->n_subs >= n_dims);
    plc_value_type_t elem_type = cip_to_plc_type(tag->type_id);

    memset(out_node, 0, sizeof(*out_node));
    strncpy(out_node->tag_name, tag->name, sizeof(out_node->tag_name) - 1u);
    out_node->native_type = tag->type_id;
    out_node->elem_size   = cip_elem_size(tag->type_id);
    out_node->flat_index  = flat;
    out_node->elem_type   = elem_type;
    out_node->n_dims      = (int)n_dims;
    out_node->subs_used   = path->n_subs;
    out_node->dims[0]     = (n_dims >= 1u) ? tag->dims[0] : 0u;
    out_node->dims[1]     = (n_dims >= 2u) ? tag->dims[1] : 0u;
    out_node->dims[2]     = (n_dims >= 3u) ? tag->dims[2] : 0u;

    if(fully_indexed || n_dims == 0) {
        out_node->type = elem_type;
    } else {
        out_node->type = PLC_VAL_ARRAY;
    }

    pdebug(DEBUG_MODULE_PATH, DEBUG_DETAIL, 0,
           "resolve \"%s\" subs=%d flat=%u type=%d elem_size=%u",
           path->tag_name, path->n_subs, flat,
           (int)out_node->type, out_node->elem_size);
    return PLC_STATUS_OK;
}


/* ---- M5: helpers ---- */

/* Build the EPATH for a tag access: route + symbolic + optional element seg. */
static Bytes build_tag_epath(eip_session_t *s, const plc_node_t *node) {
    Bytes route = bytes_from_buf(s->cip_route, s->cip_route_len);
    Bytes sym   = cip_epath_symbolic(&s->io_arena, node->tag_name);
    if(bytes_is_null(sym)) { return bytes_null(); }

    if(node->flat_index > 0u) {
        Bytes elem = cip_epath_element(&s->io_arena, node->flat_index);
        if(bytes_is_null(elem)) { return bytes_null(); }
        return bytes_concat(&s->io_arena, route, sym, elem);
    }
    return bytes_concat(&s->io_arena, route, sym);
}

/* Decode raw CIP element bytes into plc_value_t. */
static plc_status_t decode_elem(Bytes data, uint16_t native, plc_value_t *out) {
    uint8_t tc = (uint8_t)(native & 0xFFu);
    switch(tc) {
        case 0xC1: { uint8_t  v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_BOOL; out->as.b=(v!=0); return PLC_STATUS_OK; }
        case 0xC2: { uint8_t  v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_INT; out->as.i=(int64_t)(int8_t)v; return PLC_STATUS_OK; }
        case 0xC3: { uint16_t v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_INT; out->as.i=(int64_t)(int16_t)v; return PLC_STATUS_OK; }
        case 0xC4: { uint32_t v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_INT; out->as.i=(int64_t)(int32_t)v; return PLC_STATUS_OK; }
        case 0xC5: { uint64_t v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_INT; out->as.i=(int64_t)v; return PLC_STATUS_OK; }
        case 0xC6: case 0xD0: {
                      uint8_t  v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_INT; out->as.i=(int64_t)v; return PLC_STATUS_OK; }
        case 0xC7: case 0xD1: {
                      uint16_t v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_INT; out->as.i=(int64_t)v; return PLC_STATUS_OK; }
        case 0xC8: case 0xD2: {
                      uint32_t v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_INT; out->as.i=(int64_t)v; return PLC_STATUS_OK; }
        case 0xC9: case 0xD3: {
                      uint64_t v=0; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_INT; out->as.i=(int64_t)v; return PLC_STATUS_OK; }
        case 0xCA: { float    v=0.0f; if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_DOUBLE; out->as.d=(double)v; return PLC_STATUS_OK; }
        case 0xCB: { double   v=0.0;  if(bytes_is_null(bytes_unpack(data,BYTES_LE,&v))) break;
                      out->type=PLC_VAL_DOUBLE; out->as.d=v; return PLC_STATUS_OK; }
        default: return PLC_STATUS_ERR_NOT_SUPPORTED;
    }
    return PLC_STATUS_ERR_IO;
}

/* Encode plc_value_t to raw element bytes in s->io_arena. */
static plc_status_t encode_elem(eip_session_t *s, const plc_value_t *val,
                                 uint16_t native, Bytes *out) {
    uint8_t tc = (uint8_t)(native & 0xFFu);
    int64_t i  = (val->type == PLC_VAL_INT)  ? val->as.i : 0;
    double  d  = (val->type == PLC_VAL_DOUBLE) ? val->as.d : 0.0;
    int64_t b  = (val->type == PLC_VAL_BOOL)  ? (val->as.b ? 1 : 0) : 0;

    switch(tc) {
        case 0xC1:
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint8_t)(b ? 0xFF : 0x00));
            break;
        case 0xC2:
            if(i < INT8_MIN || i > INT8_MAX) { return PLC_STATUS_ERR_OUT_OF_RANGE; }
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint8_t)(int8_t)i);
            break;
        case 0xC3:
            if(i < INT16_MIN || i > INT16_MAX) { return PLC_STATUS_ERR_OUT_OF_RANGE; }
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint16_t)(int16_t)i);
            break;
        case 0xC4:
            if(i < INT32_MIN || i > INT32_MAX) { return PLC_STATUS_ERR_OUT_OF_RANGE; }
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint32_t)(int32_t)i);
            break;
        case 0xC5:
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint64_t)i);
            break;
        case 0xC6: case 0xD0:
            if(i < 0 || i > UINT8_MAX) { return PLC_STATUS_ERR_OUT_OF_RANGE; }
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint8_t)i);
            break;
        case 0xC7: case 0xD1:
            if(i < 0 || i > UINT16_MAX) { return PLC_STATUS_ERR_OUT_OF_RANGE; }
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint16_t)i);
            break;
        case 0xC8: case 0xD2:
            if(i < 0 || i > (int64_t)UINT32_MAX) { return PLC_STATUS_ERR_OUT_OF_RANGE; }
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint32_t)i);
            break;
        case 0xC9: case 0xD3:
            *out = bytes_pack(&s->io_arena, BYTES_LE, (uint64_t)i);
            break;
        case 0xCA:
            *out = bytes_pack(&s->io_arena, BYTES_LE, (float)d);
            break;
        case 0xCB:
            *out = bytes_pack(&s->io_arena, BYTES_LE, (double)d);
            break;
        default:
            return PLC_STATUS_ERR_NOT_SUPPORTED;
    }
    if(bytes_is_null(*out)) { return PLC_STATUS_ERR_NO_MEM; }
    return PLC_STATUS_OK;
}


/* ---- M5: read ---- */

static plc_status_t eip_driver_read(plc_device_t *dev,
                                     const plc_node_t *node,
                                     plc_value_t *out, int timeout_ms) {
    if(!dev || !node || !out) { return PLC_STATUS_ERR_NULL_PTR; }
    if(node->type == PLC_VAL_ARRAY || node->type == PLC_VAL_STRUCT ||
       node->type == PLC_VAL_UNKNOWN) {
        return PLC_STATUS_ERR_NOT_SUPPORTED;
    }

    eip_session_t *s = (eip_session_t *)dev->driver_state;
    if(!s) { return PLC_STATUS_ERR_NULL_PTR; }

    arena_reset(&s->io_arena);

    Bytes epath = build_tag_epath(s, node);
    if(bytes_is_null(epath)) { return PLC_STATUS_ERR_NO_MEM; }

    /* CIP Read Tag Service (0x4C): data = element_count (uint16 = 1) */
    Bytes req_data = bytes_pack(&s->io_arena, BYTES_LE, (uint16_t)1u);
    if(bytes_is_null(req_data)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes req = cip_build_request(&s->io_arena, CIP_READ_TAG, epath, req_data);
    if(bytes_is_null(req)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes cip_resp = bytes_null();
    plc_status_t rc = eip_send_rr_data(s, req, &s->io_arena, &cip_resp, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    cip_reply_t reply;
    rc = cip_parse_reply(cip_resp, &reply);
    if(rc != PLC_STATUS_OK) { return rc; }
    if(reply.status != 0u) { return cip_status_to_plc(reply.status, reply.ext_status); }

    /* Read Tag response data: type_code (uint16) + element bytes */
    uint16_t resp_type = 0;
    Bytes rest = bytes_unpack(reply.data, BYTES_LE, &resp_type);
    if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }

    return decode_elem(rest, node->native_type, out);
}


/* ---- M5: write ---- */

static plc_status_t eip_driver_write(plc_device_t *dev,
                                      const plc_node_t *node,
                                      const plc_value_t *val, int timeout_ms) {
    if(!dev || !node || !val) { return PLC_STATUS_ERR_NULL_PTR; }
    if(node->type == PLC_VAL_ARRAY || node->type == PLC_VAL_STRUCT ||
       node->type == PLC_VAL_UNKNOWN) {
        return PLC_STATUS_ERR_NOT_SUPPORTED;
    }

    eip_session_t *s = (eip_session_t *)dev->driver_state;
    if(!s) { return PLC_STATUS_ERR_NULL_PTR; }

    arena_reset(&s->io_arena);

    Bytes epath = build_tag_epath(s, node);
    if(bytes_is_null(epath)) { return PLC_STATUS_ERR_NO_MEM; }

    /* Encode value to raw bytes */
    Bytes raw = bytes_null();
    plc_status_t rc = encode_elem(s, val, node->native_type, &raw);
    if(rc != PLC_STATUS_OK) { return rc; }

    /* CIP Write Tag Service (0x4D): type_code (uint16) + elem_count (uint16) + data */
    Bytes hdr = bytes_pack(&s->io_arena, BYTES_LE,
                            (uint16_t)node->native_type,
                            (uint16_t)1u);
    if(bytes_is_null(hdr)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes req_data = bytes_concat(&s->io_arena, hdr, raw);
    if(bytes_is_null(req_data)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes req = cip_build_request(&s->io_arena, CIP_WRITE_TAG, epath, req_data);
    if(bytes_is_null(req)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes cip_resp = bytes_null();
    rc = eip_send_rr_data(s, req, &s->io_arena, &cip_resp, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    cip_reply_t reply;
    rc = cip_parse_reply(cip_resp, &reply);
    if(rc != PLC_STATUS_OK) { return rc; }
    if(reply.status != 0u) { return cip_status_to_plc(reply.status, reply.ext_status); }

    return PLC_STATUS_OK;
}


/* ---- M6: tag enumeration ---- */

static size_t eip_driver_get_tag_count(plc_device_t *dev) {
    eip_session_t *s = (eip_session_t *)dev->driver_state;
    return (s && s->metadata) ? s->metadata->count : 0u;
}

static const char *eip_driver_get_tag_name(plc_device_t *dev, size_t index) {
    eip_session_t *s = (eip_session_t *)dev->driver_state;
    if(!s || !s->metadata || index >= s->metadata->count) { return NULL; }
    return s->metadata->tags[index].name;
}


/* ---- M7: multi-element read / write ---- */

/* Look up metadata and build a node for (tag_name, flat_index_start). */
static plc_status_t make_node_flat(eip_session_t *s, const char *tag_name,
                                    uint32_t flat_index, plc_node_t *out) {
    const metadata_tag_t *tag = metadata_cache_find(s->metadata, tag_name);
    if(!tag) { return PLC_STATUS_ERR_BAD_PATH; }

    uint32_t n_dims = count_dims(tag);
    plc_value_type_t elem_type = cip_to_plc_type(tag->type_id);

    memset(out, 0, sizeof(*out));
    strncpy(out->tag_name, tag->name, sizeof(out->tag_name) - 1u);
    out->native_type = tag->type_id;
    out->elem_size   = cip_elem_size(tag->type_id);
    out->flat_index  = flat_index;
    out->elem_type   = elem_type;
    out->type        = elem_type;
    out->n_dims      = (int)n_dims;
    out->dims[0]     = (n_dims >= 1u) ? tag->dims[0] : 0u;
    out->dims[1]     = (n_dims >= 2u) ? tag->dims[1] : 0u;
    out->dims[2]     = (n_dims >= 3u) ? tag->dims[2] : 0u;
    return PLC_STATUS_OK;
}

static plc_status_t eip_driver_read_range(plc_device_t *dev, const char *tag_name,
                                           uint32_t flat_index_start, uint16_t elem_count,
                                           plc_value_t *out, int timeout_ms) {
    if(!dev || !tag_name || !out || elem_count == 0) { return PLC_STATUS_ERR_NULL_PTR; }

    eip_session_t *s = (eip_session_t *)dev->driver_state;
    if(!s) { return PLC_STATUS_ERR_NULL_PTR; }

    plc_node_t node;
    plc_status_t rc = make_node_flat(s, tag_name, flat_index_start, &node);
    if(rc != PLC_STATUS_OK) { return rc; }

    if(node.elem_size == 0) { return PLC_STATUS_ERR_NOT_SUPPORTED; }

    arena_reset(&s->io_arena);

    Bytes epath = build_tag_epath(s, &node);
    if(bytes_is_null(epath)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes req_data = bytes_pack(&s->io_arena, BYTES_LE, (uint16_t)elem_count);
    if(bytes_is_null(req_data)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes req = cip_build_request(&s->io_arena, CIP_READ_TAG, epath, req_data);
    if(bytes_is_null(req)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes cip_resp = bytes_null();
    rc = eip_send_rr_data(s, req, &s->io_arena, &cip_resp, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    cip_reply_t reply;
    rc = cip_parse_reply(cip_resp, &reply);
    if(rc != PLC_STATUS_OK) { return rc; }
    if(reply.status != 0u) { return cip_status_to_plc(reply.status, reply.ext_status); }

    /* Response: type_code (uint16) + elem_count * elem_size bytes */
    uint16_t resp_type = 0;
    Bytes rest = bytes_unpack(reply.data, BYTES_LE, &resp_type);
    if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }

    for(uint16_t i = 0; i < elem_count; i++) {
        Bytes elem = bytes_slice(rest, (size_t)i * node.elem_size, node.elem_size);
        if(bytes_is_null(elem)) { return PLC_STATUS_ERR_IO; }
        rc = decode_elem(elem, node.native_type, &out[i]);
        if(rc != PLC_STATUS_OK) { return rc; }
    }
    return PLC_STATUS_OK;
}

static plc_status_t eip_driver_write_range(plc_device_t *dev, const char *tag_name,
                                            uint32_t flat_index_start, uint16_t elem_count,
                                            const plc_value_t *vals, int timeout_ms) {
    if(!dev || !tag_name || !vals || elem_count == 0) { return PLC_STATUS_ERR_NULL_PTR; }

    eip_session_t *s = (eip_session_t *)dev->driver_state;
    if(!s) { return PLC_STATUS_ERR_NULL_PTR; }

    plc_node_t node;
    plc_status_t rc = make_node_flat(s, tag_name, flat_index_start, &node);
    if(rc != PLC_STATUS_OK) { return rc; }

    if(node.elem_size == 0) { return PLC_STATUS_ERR_NOT_SUPPORTED; }

    arena_reset(&s->io_arena);

    Bytes epath = build_tag_epath(s, &node);
    if(bytes_is_null(epath)) { return PLC_STATUS_ERR_NO_MEM; }

    /* Encode all values into a contiguous buffer. */
    Bytes all_raw = bytes_null();
    bool first = true;
    for(uint16_t i = 0; i < elem_count; i++) {
        Bytes raw = bytes_null();
        rc = encode_elem(s, &vals[i], node.native_type, &raw);
        if(rc != PLC_STATUS_OK) { return rc; }
        all_raw = first ? raw : bytes_concat(&s->io_arena, all_raw, raw);
        first = false;
    }

    Bytes hdr = bytes_pack(&s->io_arena, BYTES_LE,
                            (uint16_t)node.native_type,
                            (uint16_t)elem_count);
    if(bytes_is_null(hdr)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes req_data = bytes_concat(&s->io_arena, hdr, all_raw);
    if(bytes_is_null(req_data)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes req = cip_build_request(&s->io_arena, CIP_WRITE_TAG, epath, req_data);
    if(bytes_is_null(req)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes cip_resp = bytes_null();
    rc = eip_send_rr_data(s, req, &s->io_arena, &cip_resp, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    cip_reply_t reply;
    rc = cip_parse_reply(cip_resp, &reply);
    if(rc != PLC_STATUS_OK) { return rc; }
    if(reply.status != 0u) { return cip_status_to_plc(reply.status, reply.ext_status); }
    return PLC_STATUS_OK;
}


const plc_driver_vtable_t eip_driver_vtable = {
    .scheme        = "eip",
    .open          = eip_driver_open,
    .close         = eip_driver_close,
    .resolve       = eip_driver_resolve,
    .read          = eip_driver_read,
    .write         = eip_driver_write,
    .get_tag_count = eip_driver_get_tag_count,
    .get_tag_name  = eip_driver_get_tag_name,
    .read_range    = eip_driver_read_range,
    .write_range   = eip_driver_write_range,
};
