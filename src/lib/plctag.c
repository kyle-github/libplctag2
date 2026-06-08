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

#include "plctag.h"

plc_dev_handle_t plc_open(const char *connect_str, int timeout_ms) {
    (void)connect_str;
    (void)timeout_ms;
    return PLC_INVALID_HANDLE;
}

plc_status_t plc_close(plc_dev_handle_t dev) {
    (void)dev;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

plc_status_t plc_get_type(plc_dev_handle_t dev, const char *path, int index, int timeout_ms, plc_value_type_t *out_type) {
    (void)dev; (void)path; (void)index; (void)timeout_ms; (void)out_type;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

plc_status_t plc_get_child_count(plc_dev_handle_t dev, const char *path, int timeout_ms, int *out_count) {
    (void)dev; (void)path; (void)timeout_ms; (void)out_count;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

char *plc_get_path(plc_dev_handle_t dev, const char *path, int index) {
    (void)dev; (void)path; (void)index;
    return NULL;
}

int64_t plc_read_int(plc_dev_handle_t dev, const char *path, int index, int timeout_ms) {
    (void)dev; (void)path; (void)index; (void)timeout_ms;
    return 0;
}

double plc_read_double(plc_dev_handle_t dev, const char *path, int index, int timeout_ms) {
    (void)dev; (void)path; (void)index; (void)timeout_ms;
    return 0.0;
}

int plc_read_bool(plc_dev_handle_t dev, const char *path, int index, int timeout_ms) {
    (void)dev; (void)path; (void)index; (void)timeout_ms;
    return 0;
}

const char *plc_read_string(plc_dev_handle_t dev, const char *path, int index, int timeout_ms) {
    (void)dev; (void)path; (void)index; (void)timeout_ms;
    return NULL;
}

plc_bytes_t plc_read_bytes(plc_dev_handle_t dev, const char *path, int index, int timeout_ms) {
    (void)dev; (void)path; (void)index; (void)timeout_ms;
    return (plc_bytes_t){ NULL, 0 };
}

plc_status_t plc_write_int(plc_dev_handle_t dev, const char *path, int index, int timeout_ms, int64_t value) {
    (void)dev; (void)path; (void)index; (void)timeout_ms; (void)value;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

plc_status_t plc_write_double(plc_dev_handle_t dev, const char *path, int index, int timeout_ms, double value) {
    (void)dev; (void)path; (void)index; (void)timeout_ms; (void)value;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

plc_status_t plc_write_bool(plc_dev_handle_t dev, const char *path, int index, int timeout_ms, int value) {
    (void)dev; (void)path; (void)index; (void)timeout_ms; (void)value;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

plc_status_t plc_write_string(plc_dev_handle_t dev, const char *path, int index, int timeout_ms, const char *value) {
    (void)dev; (void)path; (void)index; (void)timeout_ms; (void)value;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

plc_status_t plc_write_bytes(plc_dev_handle_t dev, const char *path, int index, int timeout_ms, plc_bytes_t value) {
    (void)dev; (void)path; (void)index; (void)timeout_ms; (void)value;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

plc_status_t plc_get_status(plc_dev_handle_t dev, const char *path, int index) {
    (void)dev; (void)path; (void)index;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

void plc_free(void *ptr) {
    (void)ptr;
}

plc_status_t plc_get_last_error(plc_dev_handle_t dev, char *buf, size_t buf_len) {
    (void)dev; (void)buf; (void)buf_len;
    return PLC_STATUS_ERR_NOT_SUPPORTED;
}

const char *plc_status_str(plc_status_t status) {
    switch(status) {
        case PLC_STATUS_OK:                return "OK";
        case PLC_STATUS_PENDING:           return "PENDING";
        case PLC_STATUS_ERR_NULL_PTR:      return "ERR_NULL_PTR";
        case PLC_STATUS_ERR_BAD_HANDLE:    return "ERR_BAD_HANDLE";
        case PLC_STATUS_ERR_BAD_PATH:      return "ERR_BAD_PATH";
        case PLC_STATUS_ERR_TYPE_MISMATCH: return "ERR_TYPE_MISMATCH";
        case PLC_STATUS_ERR_OUT_OF_RANGE:  return "ERR_OUT_OF_RANGE";
        case PLC_STATUS_ERR_TIMEOUT:       return "ERR_TIMEOUT";
        case PLC_STATUS_ERR_IO:            return "ERR_IO";
        case PLC_STATUS_ERR_NO_MEM:        return "ERR_NO_MEM";
        case PLC_STATUS_ERR_NOT_SUPPORTED: return "ERR_NOT_SUPPORTED";
        case PLC_STATUS_ERR_INTERNAL:      return "ERR_INTERNAL";
        default:                           return "UNKNOWN";
    }
}
