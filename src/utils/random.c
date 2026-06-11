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

#include "random.h"
#include <stdint.h>
#include <time.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#    include <stdlib.h>

uint64_t random_u64(uint64_t upper_bound) {
    uint64_t n = 0;
    arc4random_buf(&n, sizeof(n));
    return upper_bound ? n % upper_bound : 0;
}

#elif defined(__linux__)

#    include <stdlib.h>
#    include <sys/random.h>

uint64_t random_u64(uint64_t upper_bound) {
    uint64_t n = 0;
    if(upper_bound == 0) { return 0; }
    if(getrandom(&n, sizeof(n), GRND_NONBLOCK) < (ssize_t)sizeof(n)) {
        /* Entropy pool not ready yet — seed from time and XOR in rand(). */
        srand((unsigned int)((uint64_t)time(NULL) ^ n));
        for(size_t i = 0; i < sizeof(n); i++) {
            ((uint8_t *)&n)[i] ^= (uint8_t)(rand() % 256);
        }
    }
    return n % upper_bound;
}

#elif defined(_WIN32) || defined(_WIN64)

#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <bcrypt.h>

uint64_t random_u64(uint64_t upper_bound) {
    uint64_t n = 0;
    if(BCryptGenRandom(NULL, (PUCHAR)&n, sizeof(n),
                       BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return RANDOM_U64_ERROR;
    }
    return upper_bound ? n % upper_bound : 0;
}

#else
#    error "random.c: no suitable random source for this platform"
#endif
