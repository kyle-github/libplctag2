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
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- DLL visibility ---- */
#if defined(_WIN32) || defined(_WIN64)
#   if defined(PLCTAG_EXPORT)
#       define PLCTAG_API __declspec(dllexport)
#   elif defined(PLCTAG_IMPORT)
#       define PLCTAG_API __declspec(dllimport)
#   else
#       define PLCTAG_API
#   endif
#else
#   define PLCTAG_API __attribute__((visibility("default")))
#endif

/* ---- Handles ---- */
#define PLC_INVALID_HANDLE ((plc_dev_handle_t)0)

typedef uint64_t plc_dev_handle_t;

/* ---- Status codes ---- */
typedef enum plc_status_t {
    PLC_STATUS_OK               =  0,
    PLC_STATUS_PENDING          =  1,
    PLC_STATUS_ERR_NULL_PTR     = -1,
    PLC_STATUS_ERR_BAD_HANDLE   = -2,
    PLC_STATUS_ERR_BAD_PATH     = -3,
    PLC_STATUS_ERR_TYPE_MISMATCH= -4,
    PLC_STATUS_ERR_OUT_OF_RANGE = -5,
    PLC_STATUS_ERR_TIMEOUT      = -6,
    PLC_STATUS_ERR_IO           = -7,
    PLC_STATUS_ERR_NO_MEM       = -8,
    PLC_STATUS_ERR_NOT_SUPPORTED= -9,
    PLC_STATUS_ERR_INTERNAL     = -100,
} plc_status_t;

/* ---- Value types ---- */
typedef enum plc_value_type_t {
    PLC_VAL_UNKNOWN = 0,
    PLC_VAL_STRUCT,
    PLC_VAL_ARRAY,
    PLC_VAL_BOOL,
    PLC_VAL_INT,
    PLC_VAL_DOUBLE,
    PLC_VAL_STRING,
    PLC_VAL_BYTES,
} plc_value_type_t;

/* ---- Device open/close/status ---- */
PLCTAG_API plc_dev_handle_t plc_open  (const char *connect_str, int timeout_ms);
PLCTAG_API plc_status_t     plc_close (plc_dev_handle_t dev);
PLCTAG_API plc_status_t     plc_status(plc_dev_handle_t dev); /* PLC_STATUS_CONN_* */

/* Connection state codes returned by plc_status() and device events. */
#define PLC_STATUS_CONN_UP             100
#define PLC_STATUS_CONN_DOWN           101
#define PLC_STATUS_CONN_DISCONNECTING  102
#define PLC_STATUS_CONN_CONNECTING     103
#define PLC_STATUS_CONN_IDLE_WAIT      104
#define PLC_STATUS_CONN_ERR_WAIT       105

/* ---- Memory management ---- */
PLCTAG_API void plc_free(void *ptr); /* free anything returned with explicit ownership */

/* ---- Metadata / enumeration (M6) ---- */
PLCTAG_API size_t          plc_get_count(plc_dev_handle_t dev, const char *path, int index, int timeout_ms); /* child count; 0 = scalar */
PLCTAG_API plc_value_type_t plc_get_type (plc_dev_handle_t dev, const char *path, int index, int timeout_ms);
PLCTAG_API size_t          plc_get_size (plc_dev_handle_t dev, const char *path, int index, int timeout_ms); /* element bytes; 0 = unknown */
PLCTAG_API plc_status_t    plc_get_name (plc_dev_handle_t dev, const char *path, int index,
                                          char *buf, size_t buf_len,
                                          int timeout_ms); /* NULL name (array elem) => buf[0]='\0', returns OK */
PLCTAG_API char           *plc_get_path (plc_dev_handle_t dev, const char *path, int index, int timeout_ms); /* caller frees via plc_free */

/* ---- Reads ---- */
PLCTAG_API int64_t      plc_read_int   (plc_dev_handle_t dev, const char *path, int index, int timeout_ms); /* INT64_MIN on error */
PLCTAG_API double       plc_read_double(plc_dev_handle_t dev, const char *path, int index, int timeout_ms); /* NAN on error       */
PLCTAG_API bool         plc_read_bool  (plc_dev_handle_t dev, const char *path, int index, int timeout_ms); /* false on error     */
PLCTAG_API plc_status_t plc_read_string(plc_dev_handle_t dev, const char *path, int index,
                                         char *buf, size_t buf_len, size_t *actual_len,
                                         int timeout_ms);
PLCTAG_API plc_status_t plc_read_bytes (plc_dev_handle_t dev, const char *path, int index,
                                         uint8_t *buf, size_t buf_len, size_t *actual_len,
                                         int timeout_ms);

/* ---- Writes ---- */
PLCTAG_API plc_status_t plc_write_int   (plc_dev_handle_t dev, const char *path, int index, int64_t v,       int timeout_ms);
PLCTAG_API plc_status_t plc_write_double(plc_dev_handle_t dev, const char *path, int index, double  v,       int timeout_ms);
PLCTAG_API plc_status_t plc_write_bool  (plc_dev_handle_t dev, const char *path, int index, bool    v,       int timeout_ms);
PLCTAG_API plc_status_t plc_write_string(plc_dev_handle_t dev, const char *path, int index, const char *s,   int timeout_ms);
PLCTAG_API plc_status_t plc_write_bytes (plc_dev_handle_t dev, const char *path, int index,
                                          const uint8_t *b, size_t len,           int timeout_ms);

/* ---- Flush staged (timeout == 0) reads and writes (M7) ---- */
PLCTAG_API plc_status_t plc_flush(plc_dev_handle_t dev, const char *path, int timeout_ms);

/* ---- Subscriptions (M8) ---- */
PLCTAG_API plc_status_t plc_subscribe  (plc_dev_handle_t dev, const char *path, int read_interval_ms);
PLCTAG_API plc_status_t plc_unsubscribe(plc_dev_handle_t dev, const char *path);

/* ---- Events (M8) ---- */
typedef struct {
    char             path[256]; /* "" = device/connection event */
    int              index;
    plc_value_type_t type;
    plc_status_t     status;
} plc_event_t;

/* Drain pending events; returns event count (0 = timeout), -1 = error. */
PLCTAG_API int plc_poll_events(plc_dev_handle_t dev, plc_event_t *events,
                                size_t max_events, int timeout_ms);

/* ---- Diagnostics ---- */
PLCTAG_API plc_status_t plc_get_last_error(plc_dev_handle_t dev, char *buf, size_t buf_len);
PLCTAG_API const char  *plc_status_str(plc_status_t status);

#ifdef __cplusplus
}
#endif
