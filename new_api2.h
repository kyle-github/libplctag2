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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- DLL visibility ---- */
#if defined(_WIN32) || defined(_WIN64)
    #if defined(PLCTAG_EXPORT)
        #define PLCTAG_API __declspec(dllexport)
    #elif defined(PLCTAG_IMPORT)
        #define PLCTAG_API __declspec(dllimport)
    #else
        #define PLCTAG_API
    #endif
#else
    #define PLCTAG_API __attribute__((visibility("default")))
#endif

/* ---- Handles ---- */
#define PLC_INVALID_HANDLE ((plc_conn_handle_t)0)

typedef uint64_t plc_conn_handle_t;
typedef uint64_t plc_batch_handle_t;


/* ---- Status codes ---- */
typedef enum plc_status_t {
    PLC_STATUS_OK = 0,
    PLC_STATUS_PENDING,
    PLC_STATUS_ERR_NULL_PTR,
    PLC_STATUS_ERR_BAD_HANDLE,
    PLC_STATUS_ERR_BAD_PATH,
    PLC_STATUS_ERR_TYPE_MISMATCH,
    PLC_STATUS_ERR_OUT_OF_RANGE,
    PLC_STATUS_ERR_TIMEOUT,
    PLC_STATUS_ERR_IO,
    PLC_STATUS_ERR_NO_MEM,
    PLC_STATUS_ERR_NOT_SUPPORTED,
    PLC_STATUS_ERR_INTERNAL,
} plc_status_t;


typedef enum plc_operation_t {
    PLC_OP_CONN_DOWN,
    PLC_OP_CONN_DISCONNECTING,
    PLC_OP_CONN_UP,
    PLC_OP_CONN_CONNECTING,
    PLC_OP_CONN_IDLE_WAIT,
    PLC_OP_CONN_ERR_WAIT,

    PLC_OP_READ_START = 100,
    PLC_OP_READ_COMPLETE,
    PLC_OP_WRITE_START,
    PLC_OP_WRITE_COMPLETE,
} plc_operation_t;

/* ---- Value types ---- */
typedef enum plc_value_type_t {
    PLC_VAL_UNKNOWN = 0,
    PLC_VAL_CONNECTION,
    PLC_VAL_STRUCT,
    PLC_VAL_ARRAY,
    PLC_VAL_BOOL,
    PLC_VAL_INT,
    PLC_VAL_DOUBLE,
    PLC_VAL_STRING,
    PLC_VAL_BYTES,
} plc_value_type_t;

/* ---- Event Callback, the path is library owned data ----*/

/*
 * The library will call the event callback for connection status changes and for tag events such as read/write completions. The
 * callback is executed on the IO thread and must not block. If you need to do more work in response to an event, copy the data
 * you need and dispatch it to another thread or queue for processing.  All pointers returned by the plc_conn_get_event_* APIs
 * are library owned and valid until the next event callback, so if you need to keep any of the data around you must copy it
 * before the next event callback.
 *
 * Special values for event results:
 *
 *   Events from the connection:
 *     Path is the connection URL string,
 *     Index is UINT32_MAX.
 *     Type is PLC_VAL_CONNECTION.
 *     Op is PLC_OP_CONN_*.
 *     Status is the new connection status (PLC_STATUS_CONN_*) or an error.
 *     User data is the user data pointer passed to plc_conn_open for the connection that triggered the event.
 *
 *   Events from tag paths:
 *     Path is the tag path that triggered the event.
 *     Index is 0 for scalar tags or UINT32_MAX for aggregate tags. For array elements or struct members the index is the
 * element/member index. Type is the tag value type. Op is the operation that triggered the event, e.g. PLC_OP_READ for a read
 * completion event or PLC_OP_WRITE for a write completion event. Status is the result of the operation that triggered the event.
 *     User data is the user data pointer passed to plc_conn_open for the connection that triggered the event.
 */

typedef void (*plc_event_callback_t)(plc_conn_handle_t conn, uint32_t num_events);

PLCTAG_API const char *plc_conn_get_event_path(
    plc_conn_handle_t conn, uint32_t index,
    const char *err_val); /* for connection events returns the URL string, for tag events returns the tag path; library owned
                             string, valid until next event callback */
PLCTAG_API uint32_t
    plc_conn_get_event_index(plc_conn_handle_t conn, uint32_t index,
                             uint32_t err_val); /* for connection events returns UINT32_MAX, for tag events returns the tag index
                                                   that triggered the event, e.g. array element index or struct member index */
PLCTAG_API plc_value_type_t plc_conn_get_event_type(
    plc_conn_handle_t conn, uint32_t index,
    plc_value_type_t err_val); /* for connection events returns PLC_VAL_CONNECTION, for tag events returns the tag value type */
PLCTAG_API plc_operation_t plc_conn_get_event_op(
    plc_conn_handle_t conn, uint32_t index,
    plc_operation_t
        err_val); /* for connection events returns PLC_OP_CONN_*, for tag events returns the operation that triggered the event,
                     e.g. PLC_OP_READ for a read completion event or PLC_OP_WRITE for a write completion event */
PLCTAG_API plc_status_t plc_conn_get_event_status(
    plc_conn_handle_t conn, uint32_t index,
    plc_status_t err_val); /* for connection events returns the new connection status or error, for tag events returns the
                              operation result or PLC_STATUS_PENDING if the operation is still in progress */
PLCTAG_API void *plc_conn_get_event_user_data(
    plc_conn_handle_t conn,
    uint32_t index); /* returns the user data pointer passed to plc_conn_open for the connection that triggered the event */

/*
 * NOTES:
 *   A conn handle is a local connection reference.  The actual connection and underlying IO thread may be shared between
 *   multiple conn handles.  The library manages this internally and transparently. The connection status is per-conn handle
 *   and is reported via plc_conn_conn_status() and the event callback.
 *
 *   User applications must execute the callback as quickly as possible and not block. The callback is executed on the IO thread
 *   and blocking it will block all IO and other callbacks. If you need to do more work in response to an event, copy the data you
 *   need and dispatch it to another thread or queue for processing.
 *
 *   The connect_url_str is a URL_style string:
 *     enip-tcp://10.1.2.3:44818/1/5 -- ControlLogix CPU in slot 5 of the chassis.
 *     enip-tcp://10.1.2.3:44818 - Micro800, SLC, MicroLogix, PLC5, or anything that will respond to get identity.
 *     ** FIXME ** how do we do broadcast or unicast via UDP?
 *     enip-udp://10.1.2.0:2222/24 -- UDP broadcast to all devices on the 10.1.2 subnet, with a 2222 listen port for responses.
 */

/* ---- Connection open/close/status ---- */
PLCTAG_API plc_conn_handle_t plc_conn_open(const char *connect_url_str, plc_event_callback_t callback, void *user_data,
                                           uint32_t timeout_ms); /* timeout is for all operations */
PLCTAG_API plc_status_t plc_conn_close(plc_conn_handle_t conn);
PLCTAG_API plc_status_t plc_conn_conn_status(plc_conn_handle_t conn); /* PLC_STATUS_CONN_* */
PLCTAG_API plc_status_t plc_conn_last_op_status(plc_conn_handle_t conn);

/*
 * Unless stated otherwise all returned pointers transer ownership to the caller.
 *
 * The special value UINT32_MAX for index is used to refer to the aggregate tag itself, e.g. for a
 * struct tag it refers to the struct as a whole, for an array tag it refers to the array as a whole.
 * For scalar tags the index is always 0.
 *
 * TBD:
 *   - how do we handle the connection object itself?
 *   - can we get the URL back?
 *   - how do we enumerate tags?
 *   - can we get the number of tags and the tag paths for a connection?
 */

/* ---- Metadata / enumeration (M6) ---- */
PLCTAG_API size_t plc_get_count(plc_conn_handle_t conn, const char *path, uint32_t index,
                                size_t err_val); /* child count; 0 = scalar */
PLCTAG_API plc_value_type_t plc_get_type(plc_conn_handle_t conn, const char *path, uint32_t index, plc_value_type_t err_val);
PLCTAG_API size_t plc_get_size(plc_conn_handle_t conn, const char *path, uint32_t index,
                               size_t err_val); /* size in bytes, SIZE_MAX for unknown */
PLCTAG_API char *plc_get_name(plc_conn_handle_t conn, const char *path, uint32_t index, const char *err_val);
PLCTAG_API char *plc_get_path(plc_conn_handle_t conn, const char *path, uint32_t index, const char *err_val);

/* ---- Reads ---- */
PLCTAG_API int64_t plc_read_int(plc_conn_handle_t conn, const char *path, uint32_t index, int64_t err_val,
                                uint32_t retention_time_ms);
PLCTAG_API double plc_read_double(plc_conn_handle_t conn, const char *path, uint32_t index, double err_val,
                                  uint32_t retention_time_ms);
PLCTAG_API bool plc_read_bool(plc_conn_handle_t conn, const char *path, uint32_t index, bool err_val, uint32_t retention_time_ms);
PLCTAG_API char *plc_read_string(plc_conn_handle_t conn, const char *path, uint32_t index, const char *err_val,
                                 uint32_t retention_time_ms);

/*
 * TBD - can we use this to prefetch data?  I.e fetch and entire array with a 100ms retention time and then subsequent reads of
 * the array elements will be fast and not require additional IO until the retention time expires?
 *
 * Should we have a prefetch API instead that allows fetching and caching data with a specified retention time, and then the read
 * APIs will return cached data if available and not expired, otherwise they will fetch fresh data?
 */
PLCTAG_API uint8_t *plc_read_bytes(plc_conn_handle_t conn, const char *path, uint32_t index, const uint8_t *err_val,
                                   uint32_t retention_time_ms);

/* ---- Writes, input pointer are all caller owned ---- */
PLCTAG_API plc_status_t plc_write_int(plc_conn_handle_t conn, const char *path, uint32_t index, int64_t v);
PLCTAG_API plc_status_t plc_write_double(plc_conn_handle_t conn, const char *path, uint32_t index, double v);
PLCTAG_API plc_status_t plc_write_bool(plc_conn_handle_t conn, const char *path, uint32_t index, bool v);
PLCTAG_API plc_status_t plc_write_string(plc_conn_handle_t conn, const char *path, uint32_t index, const char *s);
PLCTAG_API plc_status_t plc_write_bytes(plc_conn_handle_t conn, const char *path, uint32_t index, const uint8_t *b, size_t len);

/* ---- Subscriptions (M8) ---- */
PLCTAG_API plc_status_t plc_subscribe(plc_conn_handle_t conn, const char *path, uint32_t read_interval_ms);
PLCTAG_API plc_status_t plc_unsubscribe(plc_conn_handle_t conn, const char *path);


/* ---- Batches ----*/

/*** **FIXME** Should there only be one batch per connection?  Why would you need more than one batch or do them out of order? */
PLCTAG_API plc_batch_handle_t plc_batch_begin(plc_conn_handle_t conn);
PLCTAG_API plc_status_t plc_batch_end(plc_batch_handle_t batch);

/* ---- Diagnostics, string is _LIBRARY_ owned ---- */
PLCTAG_API const char *plc_status_str(plc_status_t status);

#ifdef __cplusplus
}
#endif
