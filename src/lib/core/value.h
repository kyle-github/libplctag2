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
 * value.h — internal tagged value type and conversion helpers.
 *
 * plc_value_t is the library-internal representation of a read or write value.
 * It is distinct from the public API parameters (raw int64_t / double / etc.);
 * the conversion functions below bridge the two and apply range checking.
 *
 * Sentinel values match the public API contract (plc_read_int → INT64_MIN on
 * error, plc_read_double → NAN, etc.).
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "plctag.h"
#include "bytes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    plc_value_type_t type;
    union {
        int64_t i;
        double  d;
        bool    b;
        Bytes   bytes; /* backing for both PLC_VAL_STRING and PLC_VAL_BYTES */
    } as;
} plc_value_t;

/* Convert an int64_t from the public API into an internal value, range-checking
   against the native element width (1, 2, 4, or 8 bytes) and signedness implied
   by target.  Returns PLC_STATUS_ERR_OUT_OF_RANGE if the value does not fit. */
extern plc_status_t value_from_int(plc_value_t *v, int64_t in, plc_value_type_t target, size_t width);

/* Convert a double from the public API into an internal value. */
extern plc_status_t value_from_double(plc_value_t *v, double in, plc_value_type_t target);

/* Sentinels returned by the read accessors on error / not-ready. */
static inline int64_t value_int_sentinel(void) { return INT64_MIN; }
static inline double  value_double_sentinel(void) { return (double)NAN; }

#ifdef __cplusplus
}
#endif
