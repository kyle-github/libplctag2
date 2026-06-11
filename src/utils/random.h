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
 * random.h — cryptographically adequate random integers.
 *
 * Windows:       BCryptGenRandom (Vista+; link -lbcrypt).
 * macOS / BSD:   arc4random_buf (no extra link needed).
 * Linux:         getrandom(2) (kernel 3.17+), fallback to time+rand XOR.
 *
 * RANDOM_U64_ERROR is returned on unrecoverable OS failure (Windows only).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RANDOM_U64_ERROR (UINT64_MAX)

/* Return a random uint64_t in [0, upper_bound).
 * Returns RANDOM_U64_ERROR if the OS call fails (check before use on Windows).
 * upper_bound == 0 returns 0. */
uint64_t random_u64(uint64_t upper_bound);

/* Convenience: random uint32_t in [0, upper_bound). */
static inline uint32_t random_u32(uint32_t upper_bound) {
    if(upper_bound == 0) { return 0; }
    return (uint32_t)(random_u64((uint64_t)upper_bound));
}

#ifdef __cplusplus
}
#endif
