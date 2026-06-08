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
 * Fixed-size bump allocator implementation.
 * Copied from ~/Projects/data_table and modified to return errors instead
 * of calling exit().
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"

/* Portable substitute for max_align_t: a union of all fundamental types whose
 * alignment must be respected.  max_align_t from <stddef.h> is C11 but older
 * MSVC toolchains may not provide it even with /std:c11. */
typedef union {
    char c;
    short s;
    int i;
    long l;
    long long ll;
    float f;
    double d;
    long double ld;
    void *p;
} arena_max_align_t;

void arena_set_stats(Arena *a, ArenaStats *stats) {
    if(!a) { return; }
    a->stats = stats;
    if(stats) {
        memset(stats, 0, sizeof(*stats));
        stats->use_min = SIZE_MAX;
    }
}


plc_status_t arena_init(Arena *out, size_t size) {
    if(!out) { return PLC_STATUS_ERR_NULL_PTR; }

    out->buffer = (uint8_t *)malloc(size);
    if(!out->buffer) {
        out->length = 0;
        out->capacity = 0;
        out->high_water = 0;
        return PLC_STATUS_ERR_NO_MEM;
    }

    out->length = 0;
    out->capacity = size;
    out->high_water = 0;

    return PLC_STATUS_OK;
}


void *arena_alloc(Arena *a, size_t size) {
    if(!a || !a->buffer) { return NULL; }

    /* Align the cursor to the strictest fundamental alignment, matching malloc. */
    size_t align = _Alignof(arena_max_align_t);
    size_t padding = (align - (a->length % align)) % align;

    if(a->length + padding + size > a->capacity) { return NULL; }

    a->length += padding;
    void *ptr = a->buffer + a->length;
    a->length += size;

    if(a->length > a->high_water) { a->high_water = a->length; }

    return ptr;
}


void arena_reset(Arena *a) {
    if(!a) { return; }
    if(a->stats && a->length > 0) {
        ArenaStats *s = a->stats;
        s->reset_count++;
        if(a->length < s->use_min) { s->use_min = a->length; }
        if(a->length > s->use_max) { s->use_max = a->length; }
        s->use_total += a->length;
    }
    a->length = 0;
}


uint8_t *arena_current(Arena *a) {
    if(!a || !a->buffer) { return NULL; }
    return a->buffer + a->length;
}


size_t arena_remaining(Arena *a) {
    if(!a || !a->buffer) { return 0; }
    return a->capacity - a->length;
}


void arena_commit(Arena *a, size_t n) {
    if(!a) { return; }
    a->length += n;
    if(a->length > a->high_water) { a->high_water = a->length; }
}


size_t arena_save(Arena *a) { return a ? a->length : 0; }


void arena_restore(Arena *a, size_t saved) {
    if(a && saved <= a->capacity) { a->length = saved; }
}


void arena_free(Arena *a) {
    if(!a) { return; }

    free(a->buffer);
    a->buffer = NULL;
    a->length = 0;
    a->capacity = 0;
    a->high_water = 0;
}
