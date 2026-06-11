/***************************************************************************
 *   Copyright (C) 2026 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *   This software is available under the MIT license.                     *
 ***************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "core/cache.h"

#define INITIAL_CAP 64u

plc_status_t cache_create(value_cache_t **out) {
    if(!out) { return PLC_STATUS_ERR_NULL_PTR; }
    value_cache_t *c = calloc(1, sizeof(*c));
    if(!c) { return PLC_STATUS_ERR_NO_MEM; }
    c->entries = calloc(INITIAL_CAP, sizeof(cache_entry_t));
    if(!c->entries) { free(c); return PLC_STATUS_ERR_NO_MEM; }
    c->cap = INITIAL_CAP;
    *out = c;
    return PLC_STATUS_OK;
}

void cache_destroy(value_cache_t *c) {
    if(!c) { return; }
    free(c->entries);
    free(c);
}

cache_entry_t *cache_lookup(value_cache_t *c, const char *tag_name, uint32_t flat_index) {
    if(!c || !tag_name) { return NULL; }
    for(size_t i = 0; i < c->count; i++) {
        cache_entry_t *e = &c->entries[i];
        if(e->valid && e->flat_index == flat_index &&
           strncmp(e->tag_name, tag_name, METADATA_MAX_TAG_NAME) == 0) {
            return e;
        }
    }
    return NULL;
}

plc_status_t cache_put(value_cache_t *c, const char *tag_name, uint32_t flat_index,
                        const plc_value_t *val, int64_t now_ms) {
    if(!c || !tag_name || !val) { return PLC_STATUS_ERR_NULL_PTR; }

    /* Update existing entry if found. */
    cache_entry_t *e = cache_lookup(c, tag_name, flat_index);
    if(e) {
        e->value        = *val;
        e->last_read_ms = now_ms;
        e->valid        = true;
        return PLC_STATUS_OK;
    }

    /* Find an invalid (reusable) slot. */
    for(size_t i = 0; i < c->count; i++) {
        if(!c->entries[i].valid) {
            e = &c->entries[i];
            goto fill;
        }
    }

    /* Grow if needed. */
    if(c->count == c->cap) {
        size_t new_cap = c->cap * 2u;
        cache_entry_t *tmp = realloc(c->entries, new_cap * sizeof(cache_entry_t));
        if(!tmp) { return PLC_STATUS_ERR_NO_MEM; }
        c->entries = tmp;
        c->cap     = new_cap;
    }
    e = &c->entries[c->count++];

fill:
    strncpy(e->tag_name, tag_name, sizeof(e->tag_name) - 1u);
    e->tag_name[sizeof(e->tag_name) - 1u] = '\0';
    e->flat_index   = flat_index;
    e->value        = *val;
    e->last_read_ms = now_ms;
    e->valid        = true;
    return PLC_STATUS_OK;
}

void cache_invalidate_tag(value_cache_t *c, const char *tag_name) {
    if(!c || !tag_name) { return; }
    for(size_t i = 0; i < c->count; i++) {
        if(strncmp(c->entries[i].tag_name, tag_name, METADATA_MAX_TAG_NAME) == 0) {
            c->entries[i].valid = false;
        }
    }
}
