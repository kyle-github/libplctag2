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
 * eip.h — EtherNet/IP encapsulation layer and session.
 *
 * Covers the 24-byte encapsulation header, session registration, and
 * unconnected / connected explicit messaging (CPF framing).
 *
 * M3 fields (vendor, metadata) are present as forward-declared pointers and
 * remain NULL until that phase is implemented.
 * M2 fields (conn_open, connection ids) are present but not used until M2.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "plctag.h"
#include "arena.h"
#include "bytes.h"
#include "socket.h"
#include "protocol/eip/identity.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations for M3 types not yet defined. */
typedef struct eip_vendor_t    eip_vendor_t;
typedef struct metadata_cache_t metadata_cache_t;

/* EIP well-known port (TCP). */
#define EIP_DEFAULT_PORT 44818u

typedef struct eip_session_t {
    socket_t             sock;
    uint32_t             session_handle;  /* assigned by RegisterSession */
    uint64_t             sender_context;  /* incremented per request */

    char                 host[128];
    uint16_t             port;

    /* CIP routing path to the CPU (port segments, encoded bytes).
       Populated from the "path" connection attribute. Used in M2 Forward Open. */
    uint8_t              cip_route[64];
    size_t               cip_route_len;

    eip_identity_t       identity;        /* filled by eip_get_identity (M1) */

    /* M3: vendor vtable selected after identity probe. NULL until M3. */
    const eip_vendor_t  *vendor;

    /* M2: connected messaging state. */
    bool                 conn_open;
    uint32_t             o2t_conn_id;
    uint32_t             t2o_conn_id;
    uint16_t             conn_serial;
    uint16_t             conn_seq;

    /* M3: tag / UDT cache. NULL until M3. */
    metadata_cache_t    *metadata;

    Arena                io_arena;  /* request/response scratch; reset per operation */
} eip_session_t;


/* Allocate and zero-initialise a session; open the io_arena (64 KB).
   socket is left invalid; call eip_connect next. */
plc_status_t eip_session_create(eip_session_t **out, const char *host, uint16_t port);

/* Free the session (closes socket if still open, frees io_arena). */
void         eip_session_destroy(eip_session_t *s);

/* Open a TCP connection to s->host:s->port. */
plc_status_t eip_connect(eip_session_t *s, int timeout_ms);

/* Send RegisterSession (0x0065); fills s->session_handle. */
plc_status_t eip_register_session(eip_session_t *s, int timeout_ms);

/* Send UnRegisterSession (0x0066); best-effort (called on teardown). */
plc_status_t eip_unregister_session(eip_session_t *s);

/* Wrap cip_req in SendRRData (0x006F) + CPF (unconnected items), send,
   receive the reply, and return the inner CIP reply bytes in *cip_resp
   (allocated from arena a). */
plc_status_t eip_send_rr_data(eip_session_t *s, Bytes cip_req,
                               Arena *a, Bytes *cip_resp, int timeout_ms);

/* M2: connected messaging (SendUnitData, 0x0070). Stub until M2. */
plc_status_t eip_send_unit_data(eip_session_t *s, Bytes cip_req,
                                Arena *a, Bytes *cip_resp, int timeout_ms);

#ifdef __cplusplus
}
#endif
