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
 * connection.h — CIP explicit-messaging connection (class 3) via Forward Open.
 *
 * Sends Forward Open Extended (0x5B) first; falls back to standard (0x54).
 * The connection path in the request is  s->cip_route + Message Router (0x02/1).
 * On success: s->conn_open, s->o2t_conn_id, s->t2o_conn_id, s->conn_serial set.
 */

#include "protocol/eip/eip.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open a class-3 explicit connection via Forward Open.
   Sets s->conn_open=true and fills the connection IDs on success. */
plc_status_t eip_open_connection(eip_session_t *s, int timeout_ms);

/* Send Forward Close and clear connection state.
   Best-effort: clears conn_open regardless of PLC response. */
plc_status_t eip_forward_close(eip_session_t *s, int timeout_ms);

#ifdef __cplusplus
}
#endif
