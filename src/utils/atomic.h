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
 * atomic.h — thin, header-only wrapper around C11 <stdatomic.h>.
 *
 * Provides named aliases for the atomic types we actually use so that call
 * sites don't need to spell out _Atomic directly, and so a future fallback
 * (MSVC pre-C11, exotic embedded toolchains) has a single point to patch.
 *
 * Usage:
 *   plc_atomic_bool flag;
 *   plc_atomic_init(&flag, false);
 *   plc_atomic_store(&flag, true);
 *   if(plc_atomic_load(&flag)) { ... }
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

typedef _Atomic bool     plc_atomic_bool;
typedef _Atomic int32_t  plc_atomic_i32;
typedef _Atomic uint32_t plc_atomic_u32;
typedef _Atomic int64_t  plc_atomic_i64;

#define plc_atomic_init(obj, val)        atomic_init(obj, val)
#define plc_atomic_load(obj)             atomic_load(obj)
#define plc_atomic_store(obj, val)       atomic_store(obj, val)
#define plc_atomic_fetch_add(obj, delta) atomic_fetch_add(obj, delta)
#define plc_atomic_fetch_sub(obj, delta) atomic_fetch_sub(obj, delta)
#define plc_atomic_compare_exchange(obj, expected, desired) \
    atomic_compare_exchange_strong(obj, expected, desired)
