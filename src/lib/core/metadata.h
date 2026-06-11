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
 * metadata.h — per-device tag-list cache.
 *
 * Filled by the vendor-specific list_tags hook during device open and held
 * for the device lifetime.  Protocol-neutral: the EIP vendor layer writes
 * it; the path-resolution layer (M4) reads it.
 *
 * element_size is set to 0 here; M4 derives it from type_id.
 */

#include <stddef.h>
#include <stdint.h>

#include "plctag.h"

#ifdef __cplusplus
extern "C" {
#endif

#define METADATA_MAX_TAG_NAME 128u

typedef struct {
    char     name[METADATA_MAX_TAG_NAME]; /* null-terminated tag name */
    uint16_t type_id;      /* native CIP type descriptor (0 if unknown)  */
    uint32_t dims[3];      /* array dimensions, outermost first; 0 = unused */
    uint32_t instance_id;  /* protocol-level instance / address           */
} metadata_tag_t;

/* Opaque cache: dynamically-grown flat array of metadata_tag_t entries.
   Forward-declared in eip.h so eip_session_t can hold a pointer to it. */
struct metadata_cache_t {
    metadata_tag_t *tags;
    size_t          count;
    size_t          cap;
};
typedef struct metadata_cache_t metadata_cache_t;

/* Allocate and zero-initialise a cache with a small initial capacity. */
plc_status_t metadata_cache_create(metadata_cache_t **out);

/* Free the cache and all tag storage.  No-op if cache is NULL. */
void metadata_cache_destroy(metadata_cache_t *cache);

/* Append a copy of *t to the cache.  Doubles capacity on overflow. */
plc_status_t metadata_cache_add(metadata_cache_t *cache, const metadata_tag_t *t);

/* Linear search by exact name.  Returns NULL if not found. */
const metadata_tag_t *metadata_cache_find(const metadata_cache_t *cache,
                                           const char *name);

#ifdef __cplusplus
}
#endif
