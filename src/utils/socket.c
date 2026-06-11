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

#include <stdio.h>
#include <string.h>

#include "socket.h"
#include "clock.h"
#include "debug.h"

/* ---- Platform shims ---- */

#if defined(_WIN32) || defined(_WIN64)

#    define SOCK_ERRNO       ((int)WSAGetLastError())
#    define WOULD_BLOCK(e)   ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
#    define SEND_FLAGS       0
#    define RECV_FLAGS       0

typedef int socklen_t_t;

static plc_status_t set_nonblocking(socket_t s, int nonblocking) {
    u_long mode = (u_long)nonblocking;
    if(ioctlsocket(s, FIONBIO, &mode) != 0) { return PLC_STATUS_ERR_IO; }
    return PLC_STATUS_OK;
}

#else /* POSIX */

#    include <errno.h>
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <unistd.h>

#    define SOCK_ERRNO       errno
#    define WOULD_BLOCK(e)   ((e) == EINPROGRESS || (e) == EWOULDBLOCK)
#    define SEND_FLAGS       MSG_NOSIGNAL
#    define RECV_FLAGS       0
#    define SOCKET_ERROR     (-1)
#    define closesocket(s)   close(s)

typedef socklen_t socklen_t_t;

static plc_status_t set_nonblocking(socket_t s, int nonblocking) {
    int flags = fcntl(s, F_GETFL, 0);
    if(flags < 0) { return PLC_STATUS_ERR_IO; }

    if(nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if(fcntl(s, F_SETFL, flags) < 0) { return PLC_STATUS_ERR_IO; }
    return PLC_STATUS_OK;
}

#endif

/* ---- Helpers ---- */

/*
 * Wait for socket readability or writability using select.
 * check_read and check_write may both be true.
 * Returns PLC_STATUS_OK if the event arrives within deadline_ms,
 * PLC_STATUS_ERR_TIMEOUT if the deadline expires, PLC_STATUS_ERR_IO on error.
 */
static plc_status_t socket_wait(socket_t s, int check_read, int check_write, int64_t deadline_ms) {
    for(;;) {
        int64_t now = time_ms();
        int64_t remaining = deadline_ms - now;

        if(remaining < 0) { remaining = 0; }

        struct timeval tv;
        tv.tv_sec  = (long)(remaining / 1000);
        tv.tv_usec = (int)((remaining % 1000) * 1000);

        fd_set rset, wset, eset;
        FD_ZERO(&rset);
        FD_ZERO(&wset);
        FD_ZERO(&eset);

        if(check_read) { FD_SET(s, &rset); }
        if(check_write) { FD_SET(s, &wset); }
        FD_SET(s, &eset);

        int rc = select((int)(s + 1), &rset, &wset, &eset, &tv);

        if(rc < 0) {
            int err = SOCK_ERRNO;
#if !defined(_WIN32) && !defined(_WIN64)
            if(err == EINTR) { continue; }
#endif
            pdebug(DEBUG_MODULE_SOCKET, DEBUG_WARN, 0, "select failed: %d", err);
            return PLC_STATUS_ERR_IO;
        }

        if(rc == 0) { return PLC_STATUS_ERR_TIMEOUT; }

        if(check_read && FD_ISSET(s, &rset)) { return PLC_STATUS_OK; }
        if(check_write && FD_ISSET(s, &wset)) { return PLC_STATUS_OK; }

        /* eset only — treat as error */
        return PLC_STATUS_ERR_IO;
    }
}

/* ---- Public API ---- */

plc_status_t socket_lib_init(void) {
#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    if(rc != 0) {
        pdebug(DEBUG_MODULE_SOCKET, DEBUG_ERROR, 0, "WSAStartup failed: %d", rc);
        return PLC_STATUS_ERR_IO;
    }
#endif
    return PLC_STATUS_OK;
}

void socket_lib_term(void) {
#if defined(_WIN32) || defined(_WIN64)
    WSACleanup();
#endif
}

plc_status_t socket_tcp_open(socket_t *out) {
    if(!out) { return PLC_STATUS_ERR_NULL_PTR; }

    socket_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(s == PLC_INVALID_SOCKET) {
        pdebug(DEBUG_MODULE_SOCKET, DEBUG_ERROR, 0, "socket() failed: %d", SOCK_ERRNO);
        return PLC_STATUS_ERR_IO;
    }

    /* Disable Nagle: EtherNet/IP sends fixed-format frames; coalescing adds
       latency without reducing message count. */
    int nodelay = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, (socklen_t_t)sizeof(nodelay));

    *out = s;
    return PLC_STATUS_OK;
}

plc_status_t socket_tcp_connect(socket_t s, const char *host, uint16_t port, int timeout_ms) {
    if(!host) { return PLC_STATUS_ERR_NULL_PTR; }

    pdebug(DEBUG_MODULE_SOCKET, DEBUG_INFO, 0, "connecting to %s:%u (timeout %d ms)", host, port, timeout_ms);

    struct addrinfo hints;
    struct addrinfo *result = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    int rc = getaddrinfo(host, port_str, &hints, &result);
    if(rc != 0 || !result) {
        pdebug(DEBUG_MODULE_SOCKET, DEBUG_WARN, 0, "getaddrinfo(%s) failed: %d", host, rc);
        return PLC_STATUS_ERR_IO;
    }

    plc_status_t status = PLC_STATUS_ERR_IO;

    for(struct addrinfo *rp = result; rp; rp = rp->ai_next) {
        if(timeout_ms > 0) {
            if(set_nonblocking(s, 1) != PLC_STATUS_OK) { continue; }
        }

        int crc = connect(s, rp->ai_addr, (socklen_t_t)rp->ai_addrlen);

        if(crc == 0) {
            /* Immediate connect (loopback or cached route). */
            status = PLC_STATUS_OK;
        } else {
            int err = SOCK_ERRNO;
            if(WOULD_BLOCK(err) && timeout_ms > 0) {
                int64_t deadline = time_ms() + (int64_t)timeout_ms;
                plc_status_t ws = socket_wait(s, 1, 1, deadline);
                if(ws == PLC_STATUS_OK) {
                    /* Check whether the connection actually succeeded. */
                    int so_err = 0;
                    socklen_t_t len = (socklen_t_t)sizeof(so_err);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&so_err, &len);
                    status = (so_err == 0) ? PLC_STATUS_OK : PLC_STATUS_ERR_IO;
                } else {
                    status = ws;
                }
            }
        }

        if(timeout_ms > 0) { set_nonblocking(s, 0); }

        if(status == PLC_STATUS_OK) { break; }
    }

    freeaddrinfo(result);

    if(status == PLC_STATUS_OK) {
        pdebug(DEBUG_MODULE_SOCKET, DEBUG_INFO, 0, "connected to %s:%u", host, port);
    } else {
        pdebug(DEBUG_MODULE_SOCKET, DEBUG_WARN, 0, "connection to %s:%u failed", host, port);
    }

    return status;
}

plc_status_t socket_write_all(socket_t s, const uint8_t *buf, size_t len, int timeout_ms) {
    if(!buf) { return PLC_STATUS_ERR_NULL_PTR; }

    int64_t deadline = (timeout_ms > 0) ? time_ms() + (int64_t)timeout_ms : INT64_MAX;
    size_t sent = 0;

    while(sent < len) {
        if(time_ms() > deadline) { return PLC_STATUS_ERR_TIMEOUT; }

        if(timeout_ms > 0) {
            plc_status_t ws = socket_wait(s, 0, 1, deadline);
            if(ws != PLC_STATUS_OK) { return ws; }
        }

#if defined(_WIN32) || defined(_WIN64)
        int n = send(s, (const char *)(buf + sent), (int)(len - sent), SEND_FLAGS);
#else
        ssize_t n = send(s, buf + sent, len - sent, SEND_FLAGS);
#endif

        if(n <= 0) {
            pdebug(DEBUG_MODULE_SOCKET, DEBUG_WARN, 0, "send failed: %d", SOCK_ERRNO);
            return PLC_STATUS_ERR_IO;
        }

        sent += (size_t)n;
    }

    return PLC_STATUS_OK;
}

plc_status_t socket_read_n(socket_t s, uint8_t *buf, size_t len, int timeout_ms) {
    if(!buf) { return PLC_STATUS_ERR_NULL_PTR; }

    int64_t deadline = (timeout_ms > 0) ? time_ms() + (int64_t)timeout_ms : INT64_MAX;
    size_t got = 0;

    while(got < len) {
        if(time_ms() > deadline) { return PLC_STATUS_ERR_TIMEOUT; }

        if(timeout_ms > 0) {
            plc_status_t ws = socket_wait(s, 1, 0, deadline);
            if(ws != PLC_STATUS_OK) { return ws; }
        }

#if defined(_WIN32) || defined(_WIN64)
        int n = recv(s, (char *)(buf + got), (int)(len - got), RECV_FLAGS);
#else
        ssize_t n = recv(s, buf + got, len - got, RECV_FLAGS);
#endif

        if(n < 0) {
            pdebug(DEBUG_MODULE_SOCKET, DEBUG_WARN, 0, "recv failed: %d", SOCK_ERRNO);
            return PLC_STATUS_ERR_IO;
        }

        if(n == 0) {
            pdebug(DEBUG_MODULE_SOCKET, DEBUG_WARN, 0, "connection closed by peer");
            return PLC_STATUS_ERR_IO;
        }

        got += (size_t)n;
    }

    return PLC_STATUS_OK;
}

void socket_close(socket_t *s) {
    if(!s || *s == PLC_INVALID_SOCKET) { return; }
    closesocket(*s);
    *s = PLC_INVALID_SOCKET;
}
