#pragma once

/***************************************************************************
 *   Copyright (C) 2026 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *   This software is available under the MIT license.                     *
 ***************************************************************************/

/*
 * cache.h — per-device value cache (M7).
 *
 * Keyed by (tag_name, flat_index).  Linear search is acceptable given that
 * the cache grows proportionally to staged-op batch size, which is typically
 * in the hundreds, not thousands.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "plctag.h"
#include "core/metadata.h"   /* METADATA_MAX_TAG_NAME */
#include "core/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char         tag_name[METADATA_MAX_TAG_NAME];
    uint32_t     flat_index;
    plc_value_t  value;
    int64_t      last_read_ms; /* 0 = never populated */
    bool         valid;
} cache_entry_t;

typedef struct value_cache_t {
    cache_entry_t *entries;
    size_t         count;
    size_t         cap;
} value_cache_t;

plc_status_t  cache_create(value_cache_t **out);
void          cache_destroy(value_cache_t *c);

/* Returns existing entry or NULL.  Does NOT create new entries. */
cache_entry_t *cache_lookup(value_cache_t *c, const char *tag_name, uint32_t flat_index);

/* Insert or update entry.  Returns ERR_NO_MEM if allocation fails. */
plc_status_t  cache_put(value_cache_t *c, const char *tag_name, uint32_t flat_index,
                         const plc_value_t *val, int64_t now_ms);

/* Invalidate all entries for a given tag name. */
void          cache_invalidate_tag(value_cache_t *c, const char *tag_name);

#ifdef __cplusplus
}
#endif
