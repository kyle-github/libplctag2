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
 * Arena lifetime rule for eip_send_rr_data:
 *
 *   The caller resets s->io_arena and builds cip_req in it.
 *   eip_send_rr_data builds the CPF/EIP wrapper on top of whatever is
 *   already in s->io_arena (no reset at entry), sends the packet, then
 *   resets s->io_arena after socket_write_all so the caller may re-use it
 *   for the response.  The response body is allocated from the 'a' arena
 *   argument, which may be s->io_arena itself (since it is empty by then).
 *
 *   Result: callers can pass &s->io_arena for both the request build and
 *   the response arena without aliasing problems.
 */

#include <stdlib.h>
#include <string.h>

#include "protocol/eip/eip.h"
#include "mutex.h"
#include "debug.h"

/* --- EIP encapsulation command codes --- */
#define EIP_CMD_REGISTER_SESSION   0x0065u
#define EIP_CMD_UNREGISTER_SESSION 0x0066u
#define EIP_CMD_SEND_RR_DATA       0x006Fu
#define EIP_CMD_SEND_UNIT_DATA     0x0070u

#define EIP_ENCAP_HDR_SIZE 24u

/* CPF item type IDs */
#define CPF_ITEM_NULL_ADDR   0x0000u
#define CPF_ITEM_UNCONN_DATA 0x00B2u
#define CPF_ITEM_CONN_ADDR   0x00A1u
#define CPF_ITEM_CONN_DATA   0x00B1u


/* ---- Static helpers ---- */

/*
 * Append a 24-byte encapsulation header + payload in s->io_arena and send.
 * The caller must have already reset s->io_arena and built payload in it.
 * On success the full packet is in the kernel send buffer; s->io_arena still
 * holds all allocations (caller is responsible for resetting).
 */
static plc_status_t send_encap(eip_session_t *s, uint16_t cmd,
                                Bytes payload, int timeout_ms) {
    Bytes hdr = bytes_pack(&s->io_arena, BYTES_LE,
        (uint16_t)cmd,
        (uint16_t)payload.len,
        (uint32_t)s->session_handle,
        (uint32_t)0,                    /* status: always 0 in requests */
        (uint64_t)s->sender_context++,
        (uint32_t)0);                   /* options */
    if(bytes_is_null(hdr)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes pkt = bytes_concat(&s->io_arena, hdr, payload);
    if(bytes_is_null(pkt)) { return PLC_STATUS_ERR_NO_MEM; }

    return socket_write_all(s->sock, pkt.data, pkt.len, timeout_ms);
}

/*
 * Read and validate a 24-byte encapsulation response header from the socket.
 * Writes back session_handle and payload_len via optional out-pointers.
 * Returns ERR_IO if encap-level status != 0 or cmd != expected_cmd.
 */
static plc_status_t recv_encap_hdr(eip_session_t *s, uint16_t expected_cmd,
                                    uint32_t *out_session_handle,
                                    uint16_t *out_payload_len,
                                    int timeout_ms) {
    uint8_t buf[EIP_ENCAP_HDR_SIZE];
    plc_status_t rc = socket_read_n(s->sock, buf, sizeof(buf), timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    Bytes b = bytes_from_buf(buf, sizeof(buf));
    uint16_t cmd, plen;
    uint32_t session_handle, status;
    bytes_unpack(b, BYTES_LE, &cmd, &plen, &session_handle, &status, BYTES_SKIP(12));

    if(status != 0) {
        pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0,
               "encap error status 0x%04X (cmd 0x%04X)", status, cmd);
        return PLC_STATUS_ERR_IO;
    }
    if(cmd != expected_cmd) {
        pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0,
               "unexpected encap cmd 0x%04X (expected 0x%04X)", cmd, expected_cmd);
        return PLC_STATUS_ERR_IO;
    }

    if(out_session_handle) { *out_session_handle = session_handle; }
    if(out_payload_len)    { *out_payload_len    = plen; }
    return PLC_STATUS_OK;
}


/* ---- Public API ---- */

plc_status_t eip_session_create(eip_session_t **out, const char *host, uint16_t port) {
    if(!out || !host) { return PLC_STATUS_ERR_NULL_PTR; }

    eip_session_t *s = calloc(1, sizeof(*s));
    if(!s) { return PLC_STATUS_ERR_NO_MEM; }

    s->sock = PLC_INVALID_SOCKET;
    strncpy(s->host, host, sizeof(s->host) - 1);
    s->port = port ? port : EIP_DEFAULT_PORT;

    plc_status_t rc = arena_init(&s->io_arena, 65536);
    if(rc != PLC_STATUS_OK) {
        free(s);
        return rc;
    }

    *out = s;
    return PLC_STATUS_OK;
}


void eip_session_destroy(eip_session_t *s) {
    if(!s) { return; }
    socket_close(&s->sock);
    arena_free(&s->io_arena);
    free(s);
}


plc_status_t eip_connect(eip_session_t *s, int timeout_ms) {
    if(!s) { return PLC_STATUS_ERR_NULL_PTR; }
    pdebug(DEBUG_MODULE_ENIP, DEBUG_INFO, 0, "connecting to %s:%u", s->host, s->port);

    plc_status_t rc = socket_tcp_open(&s->sock);
    if(rc != PLC_STATUS_OK) { return rc; }

    rc = socket_tcp_connect(s->sock, s->host, s->port, timeout_ms);
    if(rc != PLC_STATUS_OK) {
        socket_close(&s->sock);
    }
    return rc;
}


plc_status_t eip_register_session(eip_session_t *s, int timeout_ms) {
    if(!s) { return PLC_STATUS_ERR_NULL_PTR; }

    arena_reset(&s->io_arena);

    /* Payload: EIP protocol version = 1, options flags = 0 */
    Bytes payload = bytes_pack(&s->io_arena, BYTES_LE,
        (uint16_t)1,
        (uint16_t)0);
    if(bytes_is_null(payload)) { return PLC_STATUS_ERR_NO_MEM; }

    /* session_handle must be 0 when requesting a new session */
    uint32_t prev_handle = s->session_handle;
    s->session_handle = 0;
    plc_status_t rc = send_encap(s, EIP_CMD_REGISTER_SESSION, payload, timeout_ms);
    s->session_handle = prev_handle;
    if(rc != PLC_STATUS_OK) { return rc; }

    /* Read response; session_handle is assigned by the target */
    uint32_t assigned_handle = 0;
    uint16_t plen = 0;
    rc = recv_encap_hdr(s, EIP_CMD_REGISTER_SESSION, &assigned_handle, &plen, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    if(assigned_handle == 0) {
        pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0, "server returned session handle 0");
        return PLC_STATUS_ERR_IO;
    }

    /* Drain the echoed payload (protocol version + options, 4 bytes) */
    if(plen > 0) {
        uint8_t drain[8];
        size_t n = plen < sizeof(drain) ? plen : sizeof(drain);
        socket_read_n(s->sock, drain, n, timeout_ms); /* best-effort drain */
    }

    s->session_handle = assigned_handle;
    pdebug(DEBUG_MODULE_ENIP, DEBUG_INFO, 0,
           "session registered: handle=0x%08X", s->session_handle);
    return PLC_STATUS_OK;
}


plc_status_t eip_unregister_session(eip_session_t *s) {
    if(!s || s->session_handle == 0) { return PLC_STATUS_OK; }

    arena_reset(&s->io_arena);

    /* No payload: pass bytes_null(); send_encap handles len=0 correctly. */
    send_encap(s, EIP_CMD_UNREGISTER_SESSION, bytes_null(), 200); /* best-effort */
    s->session_handle = 0;
    return PLC_STATUS_OK;
}


plc_status_t eip_send_rr_data(eip_session_t *s, Bytes cip_req,
                               Arena *a, Bytes *cip_resp, int timeout_ms) {
    if(!s || !cip_resp) { return PLC_STATUS_ERR_NULL_PTR; }

    /*
     * The caller has already reset s->io_arena and built cip_req in it.
     * We extend s->io_arena with the CPF wrapper and encap header on top,
     * send the packet, THEN reset s->io_arena so the caller's 'a' (which
     * may alias s->io_arena) is empty and ready for the response.
     */

    /* CPF payload prefix: interface_handle(4) + timeout(2) +
       item_count(2) + null_addr(4) + unc_data_hdr(4) */
    Bytes cpf_prefix = bytes_pack(&s->io_arena, BYTES_LE,
        (uint32_t)0,                         /* interface handle (CIP = 0) */
        (uint16_t)0,                         /* timeout (0 = unlimited) */
        (uint16_t)2,                         /* item count */
        (uint16_t)CPF_ITEM_NULL_ADDR,
        (uint16_t)0,                         /* null address item length */
        (uint16_t)CPF_ITEM_UNCONN_DATA,
        (uint16_t)cip_req.len);
    if(bytes_is_null(cpf_prefix)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes enc_payload = bytes_concat(&s->io_arena, cpf_prefix, cip_req);
    if(bytes_is_null(enc_payload)) { return PLC_STATUS_ERR_NO_MEM; }

    plc_status_t rc = send_encap(s, EIP_CMD_SEND_RR_DATA, enc_payload, timeout_ms);

    /* Bytes are now in the kernel send buffer; reclaim s->io_arena. */
    arena_reset(&s->io_arena);

    if(rc != PLC_STATUS_OK) { return rc; }

    /* Read encapsulation response header */
    uint16_t resp_plen = 0;
    rc = recv_encap_hdr(s, EIP_CMD_SEND_RR_DATA, NULL, &resp_plen, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    if(resp_plen == 0) {
        *cip_resp = bytes_null();
        return PLC_STATUS_OK;
    }

    /* Allocate response body from the caller's arena (may be s->io_arena) */
    Bytes body = bytes_alloc(a, resp_plen);
    if(bytes_is_null(body)) { return PLC_STATUS_ERR_NO_MEM; }

    rc = socket_read_n(s->sock, body.data, resp_plen, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    /* Parse CPF reply: interface_handle(4) + timeout(2) + item_count(2) */
    uint32_t iface;
    uint16_t to, item_count;
    Bytes rest = bytes_unpack(body, BYTES_LE, &iface, &to, &item_count);
    if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }

    /* Walk items; find unconnected data (type 0x00B2) */
    for(uint16_t i = 0; i < item_count; i++) {
        uint16_t type_id, item_len;
        rest = bytes_unpack(rest, BYTES_LE, &type_id, &item_len);
        if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }

        if(type_id == CPF_ITEM_UNCONN_DATA) {
            *cip_resp = bytes_slice(rest, 0, item_len);
            return PLC_STATUS_OK;
        }
        rest = bytes_skip(rest, item_len);
    }

    pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0,
           "SendRRData reply: no unconnected data item in CPF");
    return PLC_STATUS_ERR_IO;
}


plc_status_t eip_send_unit_data(eip_session_t *s, Bytes cip_req,
                                 Arena *a, Bytes *cip_resp, int timeout_ms) {
    if(!s || !cip_resp) { return PLC_STATUS_ERR_NULL_PTR; }
    if(!s->conn_open)   { return PLC_STATUS_ERR_IO; }

    /*
     * Arena convention: same as eip_send_rr_data.
     * Caller has reset s->io_arena and built cip_req in it.
     * We extend with CPF connected framing, send, reset, then read the reply.
     *
     * CPF layout for SendUnitData:
     *   interface_handle  uint32 (0)
     *   timeout           uint16 (0)
     *   item_count        uint16 (2)
     *   -- connected address item --
     *   type   uint16 (0x00A1)
     *   length uint16 (4)
     *   conn_id uint32 (T->O connection ID)
     *   -- connected data item --
     *   type   uint16 (0x00B1)
     *   length uint16 (2 + cip_req.len)
     *   seq    uint16 (per-connection sequence counter)
     *   data   bytes  (cip_req)
     */
    uint16_t seq = s->conn_seq++;
    uint16_t data_item_len = (uint16_t)((uint16_t)2u + (uint16_t)cip_req.len);

    Bytes cpf_prefix = bytes_pack(&s->io_arena, BYTES_LE,
        (uint32_t)0,                         /* interface handle */
        (uint16_t)0,                         /* timeout */
        (uint16_t)2,                         /* item count */
        (uint16_t)CPF_ITEM_CONN_ADDR,
        (uint16_t)4u,                        /* connected address item length */
        (uint32_t)s->t2o_conn_id,            /* T->O connection ID */
        (uint16_t)CPF_ITEM_CONN_DATA,
        (uint16_t)data_item_len,
        (uint16_t)seq);
    if(bytes_is_null(cpf_prefix)) { return PLC_STATUS_ERR_NO_MEM; }

    Bytes enc_payload = bytes_concat(&s->io_arena, cpf_prefix, cip_req);
    if(bytes_is_null(enc_payload)) { return PLC_STATUS_ERR_NO_MEM; }

    plc_status_t rc = send_encap(s, EIP_CMD_SEND_UNIT_DATA, enc_payload, timeout_ms);

    /* Bytes in kernel buffer; reclaim s->io_arena for the response. */
    arena_reset(&s->io_arena);

    if(rc != PLC_STATUS_OK) { return rc; }

    /* Read encapsulation response header */
    uint16_t resp_plen = 0;
    rc = recv_encap_hdr(s, EIP_CMD_SEND_UNIT_DATA, NULL, &resp_plen, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    if(resp_plen == 0) {
        *cip_resp = bytes_null();
        return PLC_STATUS_OK;
    }

    /* Allocate response body from caller's arena (may alias s->io_arena) */
    Bytes body = bytes_alloc(a, resp_plen);
    if(bytes_is_null(body)) { return PLC_STATUS_ERR_NO_MEM; }

    rc = socket_read_n(s->sock, body.data, resp_plen, timeout_ms);
    if(rc != PLC_STATUS_OK) { return rc; }

    /* Parse CPF: interface_handle(4) + timeout(2) + item_count(2) */
    uint32_t iface;
    uint16_t to, item_count;
    Bytes rest = bytes_unpack(body, BYTES_LE, &iface, &to, &item_count);
    if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }

    /* Walk items; find connected data (0x00B1) */
    for(uint16_t i = 0; i < item_count; i++) {
        uint16_t type_id, item_len;
        rest = bytes_unpack(rest, BYTES_LE, &type_id, &item_len);
        if(bytes_is_null(rest)) { return PLC_STATUS_ERR_IO; }

        if(type_id == CPF_ITEM_CONN_DATA) {
            /* Skip the 2-byte sequence count prefix */
            if(item_len < 2u) { return PLC_STATUS_ERR_IO; }
            *cip_resp = bytes_slice(rest, 2u, (size_t)(item_len - 2u));
            return PLC_STATUS_OK;
        }
        rest = bytes_skip(rest, item_len);
    }

    pdebug(DEBUG_MODULE_ENIP, DEBUG_WARN, 0,
           "SendUnitData reply: no connected data item in CPF");
    return PLC_STATUS_ERR_IO;
}
