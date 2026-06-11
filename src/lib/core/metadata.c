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

#include <stdlib.h>
#include <string.h>

#include "core/metadata.h"
#include "debug.h"

#define INITIAL_CAP 64u

plc_status_t metadata_cache_create(metadata_cache_t **out) {
    if(!out) { return PLC_STATUS_ERR_NULL_PTR; }

    metadata_cache_t *c = calloc(1, sizeof(*c));
    if(!c) { return PLC_STATUS_ERR_NO_MEM; }

    c->tags = calloc(INITIAL_CAP, sizeof(metadata_tag_t));
    if(!c->tags) {
        free(c);
        return PLC_STATUS_ERR_NO_MEM;
    }
    c->cap = INITIAL_CAP;

    *out = c;
    return PLC_STATUS_OK;
}


void metadata_cache_destroy(metadata_cache_t *cache) {
    if(!cache) { return; }
    free(cache->tags);
    free(cache);
}


plc_status_t metadata_cache_add(metadata_cache_t *cache,
                                 const metadata_tag_t *t) {
    if(!cache || !t) { return PLC_STATUS_ERR_NULL_PTR; }

    if(cache->count == cache->cap) {
        size_t new_cap = cache->cap * 2u;
        metadata_tag_t *p = realloc(cache->tags,
                                     new_cap * sizeof(metadata_tag_t));
        if(!p) {
            pdebug(DEBUG_MODULE_PROTOCOL, DEBUG_ERROR, 0,
                   "metadata_cache_add: realloc failed (cap=%zu)", new_cap);
            return PLC_STATUS_ERR_NO_MEM;
        }
        cache->tags = p;
        cache->cap  = new_cap;
    }

    cache->tags[cache->count++] = *t;
    return PLC_STATUS_OK;
}


const metadata_tag_t *metadata_cache_find(const metadata_cache_t *cache,
                                           const char *name) {
    if(!cache || !name) { return NULL; }
    for(size_t i = 0; i < cache->count; i++) {
        if(strcmp(cache->tags[i].name, name) == 0) {
            return &cache->tags[i];
        }
    }
    return NULL;
}
