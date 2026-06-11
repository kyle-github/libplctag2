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
 * Forward Open / Forward Close for class-3 explicit messaging connections.
 *
 * Both services are sent via unconnected messaging (eip_send_rr_data) to the
 * Connection Manager (class 0x06, instance 1).
 *
 * Connection path inside the Forward Open data = s->cip_route + Message Router
 * path (class 0x02, instance 1).  This routes from the EtherNet/IP gateway
 * through the backplane (if any) to the CPU, then addresses the Message Router
 * object there.
 *
 * Arena convention: follows the eip_send_rr_data caller contract — the caller
 * resets s->io_arena and builds the request there before calling send_rr_data.
 */

#include <stdint.h>

#include "protocol/eip/connection.h"
#include "protocol/eip/cip.h"
#include "debug.h"

/* Vendor ID / serial used in all our Forward Open / Close requests.
   Must be consistent within the same connection lifetime. */
#define ORIG_VENDOR_ID     ((uint16_t)0x1337u)
#define ORIG_SERIAL_NUMBER ((uint32_t)0xDEADBEEFu)

/* Connection timeout: priority=0, time_tick=10 (2^10 ms = 1024 ms) × 5 ticks ≈ 5 s */
#define CONN_PRIORITY_TIME_TCK ((uint8_t)0x0Au)
#define CONN_TIMEOUT_TICKS     ((uint8_t)0x05u)

/* RPI (requested packet interval) in microseconds.  2 s is safely long for
   explicit messaging — the actual cycle is driven by the application. */
#define CONN_RPI_US ((uint32_t)2000000u)

/* Connection timeout multiplier: 3 = 32×  (timeout = RPI × 4 × 32) */
#define CONN_TIMEOUT_MULTIPLIER ((uint8_t)0x03u)

/* Transport type/trigger for class-3 explicit, server, application-triggered. */
#define TRANSPORT_CLASS3_SERVER ((uint8_t)0xA3u)

/*
 * Connection parameters for standard Forward Open (uint16 field):
 *   bits [15]:    exclusive owner (0)
 *   bits [14:13]: connection type — P2P (10)
 *   bit  [12]:    priority — low (0)
 *   bits [11:9]:  reserved (but 0x4200 sets bit 9; matches known-good captures)
 *   bits [8:0]:   max PDU size in bytes
 */
#define CONN_PARAMS_STD_BASE ((uint16_t)0x4200u)
#define CONN_PARAMS_STD_SIZE ((uint16_t)500u)

/*
 * Connection parameters for Extended Forward Open (uint32 field):
 *   bit  [31]:    exclusive owner (0)
 *   bits [30:29]: connection type — P2P (10)
 *   bit  [28]:    priority — low (0)
 *   bits [27:16]: reserved
 *   bits [15:0]:  max PDU size in bytes (up to 65535)
 */
#define CONN_PARAMS_EX_BASE ((uint32_t)0x40000000u)
#define CONN_PARAMS_EX_SIZE ((uint32_t)4002u)

/* CIP path to the Message Router on the target CPU (class 0x02, instance 1). */
static const uint8_t k_msg_router_path[] = { 0x20u, 0x02u, 0x24u, 0x01u };


/*
 * Build the connection path: s->cip_route (may be empty) + Message Router path.
 * Result is allocated in s->io_arena (which the caller must have reset).
 * Returns bytes_null() only on arena OOM.
 */
static Bytes build_conn_path(eip_session_t *s) {
    Bytes route  = bytes_from_buf(s->cip_route, s->cip_route_len);
    Bytes mr     = bytes_from_buf(k_msg_router_path, sizeof(k_msg_router_path));
    return bytes_concat(&s->io_arena, route, mr);
}


/*
 * Send a single Forward Open attempt.  extended=true → service 0x5B with
 * 32-bit connection params; extended=false → service 0x54 with 16-bit params.
 * On success, fills s->o2t_conn_id, t2o_conn_id, conn_serial, conn_open.
 */
static plc_status_t try_forward_open(eip_session_t *s, bool extended,
                                     int timeout_ms) {
    arena_reset(&s->io_arena);

    Bytes conn_path = build_conn_path(s);
    if(bytes_is_null(conn_path)) { return PLC_STATUS_ERR_NO_MEM; }

    uint8_t conn_path_words = (uint8_t)(conn_path.len / 2u);
    uint16_t serial = ++s->conn_serial;

    Bytes data;
    if(extended) {
        uint32_t o2t_p = CONN_PARAMS_EX_BASE | CONN_PARAMS_EX_SIZE;
        uint32_t t2o_p = CONN_PARAMS_EX_BASE | CONN_PARAMS_EX_SIZE;
        data = bytes_pack(&s->io_arena, BYTES_LE,
            (uint8_t) CONN_PRIORITY_TIME_TCK,
            (uint8_t) CONN_TIMEOUT_TICKS,
            (uint32_t)0,                    /* O->T conn ID: 0 = target assigns */
            (uint32_t)0,                    /* T->O conn ID: 0 = target assigns */
            (uint16_t)serial,
            (uint16_t)ORIG_VENDOR_ID,
            (uint32_t)ORIG_SERIAL_NUMBER,
            (uint8_t) CONN_TIMEOUT_MULTIPLIER,
            BYTES_SKIP(3),                  /* reserved */
            (uint32_t)CONN_RPI_US,
            (uint32_t)o2t_p,
            (uint32_t)CONN_RPI_US,
            (uint32_t)t2o_p,
            (uint8_t) TRANSPORT_CLASS3_SERVER,
            (uint8_t) conn_path_words,
            conn_path);
    } else {
        uint16_t o2t_p = (uint16_t)(CONN_PARAMS_STD_BASE | CONN_PARAMS_STD_SIZE);
        uint16_t t2o_p = (uint16_t)(CONN_PARAMS_STD_BASE | CONN_PARAMS_STD_SIZE);
        data = bytes_pack(&s->io_arena, BYTES_LE,
            (uint8_t) CONN_PRIORITY_TIME_TCK,
            (uint8_t) CONN_TIMEOUT_TICKS,
            (uint32_t)0,
            (uint32_t)0,
            (uint16_t)serial,
            (uint16_t)ORIG_VENDOR_ID,
            (uint32_t)ORIG_SERIAL_NUMBER,
            (uint8_t) CONN_TIMEOUT_MULTIPLIER,
            BYTES_SKIP(3),
            (uint32_t)CONN_RPI_US,
            (uint16_t)o2t_p,
            (uint32_t)CONN_RPI_US,
            (uint16_t)t2o_p,
            (uint8_t) TRANSPORT_CLASS3_SERVER,
            (uint8_t) conn_path_words,
            conn_path);
    }
    if(bytes_is_null(data)) { return PLC_STATUS_ERR_NO_MEM; }

    /* Forward Open targets the Connection Manager: class 0x06, instance 1 */
    Bytes epath = cip_epath_class_inst(&s->io_arena, 0x06u, 1u);
    if(bytes_is_null(epath)) { return PLC_STATUS_ERR_NO_MEM; }

    uint8_t service = extended ? CIP_FWD_OPEN_EX : CIP_FWD_OPEN;
    Bytes req = cip_build_request(&s->io_arena, service, epath, data);
    if(bytes_is_null(req)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes cip_resp = bytes_null();
    plc_status_t rc = eip_send_rr_data(s, req, &s->io_arena, &cip_resp, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    cip_reply_t reply;
    rc = cip_parse_reply(cip_resp, &reply);
    if(rc != PLC_STATUS_OK) { return rc; }

    if(reply.status != 0) {
        pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0,
               "Forward Open %s: CIP status=0x%02X ext=0x%04X",
               extended ? "Extended" : "Standard",
               reply.status, reply.ext_status);
        return cip_status_to_plc(reply.status, reply.ext_status);
    }

    /*
     * Success reply data (all LE):
     *   O->T conn ID  uint32
     *   T->O conn ID  uint32
     *   serial        uint16
     *   orig_vendor   uint16   (echo)
     *   orig_serial   uint32   (echo)
     *   O->T actual RPI uint32
     *   T->O actual RPI uint32
     *   ...
     */
    uint32_t resp_o2t, resp_t2o;
    uint16_t resp_serial;
    Bytes rest = bytes_unpack(reply.data, BYTES_LE,
                              &resp_o2t, &resp_t2o, &resp_serial);
    if(bytes_is_null(rest)) {
        pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0,
               "Forward Open reply too short");
        return PLC_STATUS_ERR_IO;
    }

    s->o2t_conn_id = resp_o2t;
    s->t2o_conn_id = resp_t2o;
    s->conn_serial = resp_serial;
    s->conn_open   = true;

    pdebug(DEBUG_MODULE_ENIP, DEBUG_INFO, 0,
           "Forward Open %s: o2t=0x%08X t2o=0x%08X serial=0x%04X",
           extended ? "Extended" : "Standard",
           s->o2t_conn_id, s->t2o_conn_id, (unsigned)s->conn_serial);
    return PLC_STATUS_OK;
}


/* ---- Public API ---- */

plc_status_t eip_open_connection(eip_session_t *s, int timeout_ms) {
    if(!s) { return PLC_STATUS_ERR_NULL_PTR; }

    /* Try Extended first (large PDU support) */
    plc_status_t rc = try_forward_open(s, /*extended=*/true, timeout_ms);
    if(rc == PLC_STATUS_OK) { return PLC_STATUS_OK; }

    pdebug(DEBUG_MODULE_ENIP, DEBUG_INFO, 0,
           "Extended Forward Open failed (rc=%d), falling back to standard", (int)rc);

    return try_forward_open(s, /*extended=*/false, timeout_ms);
}


plc_status_t eip_forward_close(eip_session_t *s, int timeout_ms) {
    if(!s || !s->conn_open) { return PLC_STATUS_OK; }

    arena_reset(&s->io_arena);

    Bytes conn_path = build_conn_path(s);
    if(bytes_is_null(conn_path)) {
        /* OOM: clear state anyway so caller can proceed */
        s->conn_open   = false;
        s->o2t_conn_id = 0;
        s->t2o_conn_id = 0;
        return PLC_STATUS_ERR_NO_MEM;
    }

    uint8_t conn_path_words = (uint8_t)(conn_path.len / 2u);

    /*
     * Forward Close data (all LE):
     *   priority_time_tck  uint8
     *   time_out_ticks     uint8
     *   connection_serial  uint16   (must match Forward Open)
     *   orig_vendor_id     uint16
     *   orig_serial_number uint32
     *   conn_path_size     uint8    (in words)
     *   reserved           uint8
     *   conn_path          bytes
     */
    Bytes data = bytes_pack(&s->io_arena, BYTES_LE,
        (uint8_t) CONN_PRIORITY_TIME_TCK,
        (uint8_t) CONN_TIMEOUT_TICKS,
        (uint16_t)s->conn_serial,
        (uint16_t)ORIG_VENDOR_ID,
        (uint32_t)ORIG_SERIAL_NUMBER,
        (uint8_t) conn_path_words,
        (uint8_t) 0,                    /* reserved */
        conn_path);
    if(bytes_is_null(data)) {
        s->conn_open   = false;
        s->o2t_conn_id = 0;
        s->t2o_conn_id = 0;
        return PLC_STATUS_ERR_NO_MEM;
    }

    Bytes epath = cip_epath_class_inst(&s->io_arena, 0x06u, 1u);
    if(bytes_is_null(epath)) {
        s->conn_open   = false;
        s->o2t_conn_id = 0;
        s->t2o_conn_id = 0;
        return PLC_STATUS_ERR_NO_MEM;
    }

    Bytes req = cip_build_request(&s->io_arena, CIP_FWD_CLOSE, epath, data);
    if(bytes_is_null(req)) {
        s->conn_open   = false;
        s->o2t_conn_id = 0;
        s->t2o_conn_id = 0;
        return PLC_STATUS_ERR_NO_MEM;
    }

    Bytes cip_resp = bytes_null();
    plc_status_t rc = eip_send_rr_data(s, req, &s->io_arena, &cip_resp, timeout_ms);

    /* Clear connection state regardless of send result */
    s->conn_open   = false;
    s->o2t_conn_id = 0;
    s->t2o_conn_id = 0;

    if(rc != PLC_STATUS_OK) { return rc; }

    cip_reply_t reply;
    rc = cip_parse_reply(cip_resp, &reply);
    if(rc == PLC_STATUS_OK && reply.status != 0) {
        pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0,
               "Forward Close: CIP status=0x%02X (non-fatal)", reply.status);
    }

    pdebug(DEBUG_MODULE_ENIP, DEBUG_INFO, 0, "connection closed");
    return PLC_STATUS_OK;
}
