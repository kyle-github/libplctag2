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

#include <inttypes.h>
#include <string.h>

#include "core/value.h"
#include "debug.h"

/*
 * Compute the inclusive signed and unsigned ranges for a native element of
 * the given byte width (1, 2, 4, or 8).  Used by value_from_int to reject
 * out-of-range writes before they hit the wire.
 */
static void width_ranges(size_t width, int64_t *smin, int64_t *smax, uint64_t *umax) {
    switch(width) {
        case 1: *smin = -128;           *smax = 127;           *umax = 255U;               break;
        case 2: *smin = -32768;         *smax = 32767;         *umax = 65535U;             break;
        case 4: *smin = INT32_MIN;      *smax = INT32_MAX;     *umax = UINT32_MAX;         break;
        default:*smin = INT64_MIN;      *smax = INT64_MAX;     *umax = UINT64_MAX;         break;
    }
}


plc_status_t value_from_int(plc_value_t *v, int64_t in, plc_value_type_t target, size_t width) {
    if(!v) { return PLC_STATUS_ERR_NULL_PTR; }
    if(target != PLC_VAL_INT && target != PLC_VAL_BOOL) {
        return PLC_STATUS_ERR_TYPE_MISMATCH;
    }

    int64_t  smin, smax;
    uint64_t umax;
    width_ranges(width, &smin, &smax, &umax);

    /*
     * PLC unsigned types (UINT, UDINT, …) are exposed to the caller as int64_t.
     * A value in the range [0, umax] is always safe; a negative value or one
     * above umax is not.  We accept the full signed range too so that callers
     * can bit-pattern-cast a ULINT value without false rejection.
     */
    if(in < smin || in > (int64_t)umax) {
        pdebug(DEBUG_MODULE_LIB, DEBUG_WARN, 0,
               "value %" PRId64 " out of range for width %zu", in, width);
        return PLC_STATUS_ERR_OUT_OF_RANGE;
    }

    v->type = target;
    v->as.i = in;
    return PLC_STATUS_OK;
}


plc_status_t value_from_double(plc_value_t *v, double in, plc_value_type_t target) {
    if(!v) { return PLC_STATUS_ERR_NULL_PTR; }
    if(target != PLC_VAL_DOUBLE) { return PLC_STATUS_ERR_TYPE_MISMATCH; }

    v->type = target;
    v->as.d = in;
    return PLC_STATUS_OK;
}
