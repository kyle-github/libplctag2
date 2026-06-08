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
 * Fixed-size bump allocator.  All allocations are sequential; the only
 * "free" is arena_reset() which resets the cursor to zero.
 *
 * Copied from ~/Projects/data_table and modified:
 *   - arena_init() returns util_err_t instead of panicking on malloc failure.
 *   - arena_alloc() returns NULL on overflow instead of calling exit().
 *   - arena_free() no longer performs direct stream output.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "plctag.h"

/*
 * Per-reset-cycle usage statistics.  Attach to an arena with arena_set_stats();
 * pass NULL to disable collection entirely.  arena_reset() samples arena.length
 * just before clearing the cursor, so each sample equals the peak usage for
 * that request cycle (bump allocators never free, so length == peak).
 */
typedef struct {
    size_t reset_count; /* number of non-empty reset cycles measured */
    size_t use_min;     /* minimum usage at reset time (bytes) */
    size_t use_max;     /* maximum usage at reset time (bytes) */
    size_t use_total;   /* sum of all samples (for average) */
} ArenaStats;

typedef struct {
    uint8_t *buffer;
    size_t length;
    size_t capacity;
    size_t high_water; /* peak usage across all resets */
    ArenaStats *stats; /* optional; NULL disables stats gathering */
} Arena;

/* Initialize arena with a fixed size. */
extern plc_status_t arena_init(Arena *out, size_t size);

/* Attach (or detach with NULL) a stats collector.  Clears the stats struct on attach. */
extern void arena_set_stats(Arena *a, ArenaStats *stats);

/* Allocate size bytes from arena.  Returns NULL if out of space; caller must check. */
extern void *arena_alloc(Arena *a, size_t size);

/* Pointer to next free byte (for single-pass pack-then-commit). */
extern uint8_t *arena_current(Arena *a);

/* Bytes remaining in arena. */
extern size_t arena_remaining(Arena *a);

/* Advance arena cursor by n bytes.  Caller must ensure n <= arena_remaining(). */
extern void arena_commit(Arena *a, size_t n);

/* Reset arena cursor to zero without freeing the backing buffer.
 * If stats are attached, samples arena.length before clearing. */
extern void arena_reset(Arena *a);

/* Save current cursor position. */
extern size_t arena_save(Arena *a);

/* Restore arena cursor to a previously saved position. */
extern void arena_restore(Arena *a, size_t saved);

/* Free arena backing buffer. */
extern void arena_free(Arena *a);

#ifdef __cplusplus
}
#endif
