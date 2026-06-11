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
 * socket.h — cross-platform TCP sockets only.  No other HAL code here.
 *
 * socket_t is a SOCKET (Windows) or int (POSIX).  All blocking I/O uses
 * select-based deadline tracking so the caller controls timeouts uniformly.
 *
 * The M6 readiness / wake-pipe helpers for the async I/O model will also
 * land in this file when that phase is implemented.
 */

#include <stddef.h>
#include <stdint.h>

#include "plctag.h"

#if defined(_WIN32) || defined(_WIN64)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>

typedef SOCKET socket_t;
#    define PLC_INVALID_SOCKET INVALID_SOCKET

#else /* POSIX */

typedef int socket_t;
#    define PLC_INVALID_SOCKET (-1)

#endif

#ifdef __cplusplus
extern "C" {
#endif

/* One-time process init / teardown.  WSAStartup/WSACleanup on Windows; no-op
   on POSIX.  Must be called before any other socket function. */
extern plc_status_t socket_lib_init(void);
extern void         socket_lib_term(void);

/* Open a TCP socket (not yet connected).  On success *out holds the socket. */
extern plc_status_t socket_tcp_open(socket_t *out);

/* Resolve host (name or IP string) and connect.  timeout_ms == 0 means no
   connect-phase timeout (system default); > 0 bounds the connection attempt. */
extern plc_status_t socket_tcp_connect(socket_t s, const char *host, uint16_t port, int timeout_ms);

/* Write exactly len bytes.  Loops on partial sends.  timeout_ms applied per
   send attempt via select; 0 means use the OS default send timeout. */
extern plc_status_t socket_write_all(socket_t s, const uint8_t *buf, size_t len, int timeout_ms);

/* Read exactly len bytes into buf.  Returns PLC_STATUS_ERR_IO on EOF before
   len bytes arrive, or PLC_STATUS_ERR_TIMEOUT if the deadline expires. */
extern plc_status_t socket_read_n(socket_t s, uint8_t *buf, size_t len, int timeout_ms);

/* Close and invalidate.  Safe to call on PLC_INVALID_SOCKET. */
extern void socket_close(socket_t *s);

#ifdef __cplusplus
}
#endif
