#pragma once

/***************************************************************************
 *   Copyright (C) 2026 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *   This software is available under the MIT license.                     *
 ***************************************************************************/

/*
 * staged.h — queue of deferred (timeout == 0) reads and writes (M7).
 *
 * Staged reads are turned into coalesced CIP ReadTag(N) calls on flush.
 * Staged writes are batched into CIP WriteTag(N) calls similarly.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "plctag.h"
#include "core/metadata.h"   /* METADATA_MAX_TAG_NAME */
#include "core/value.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { STAGE_READ = 0, STAGE_WRITE } stage_kind_t;

typedef struct {
    char        tag_name[METADATA_MAX_TAG_NAME];
    uint32_t    flat_index;
    stage_kind_t kind;
    plc_value_t  write_val; /* only valid when kind == STAGE_WRITE */
} staged_op_t;

typedef struct staged_queue_t {
    staged_op_t *ops;
    size_t       count;
    size_t       cap;
} staged_queue_t;

plc_status_t staged_create(staged_queue_t **out);
void         staged_destroy(staged_queue_t *q);

plc_status_t staged_enqueue_read(staged_queue_t *q, const char *tag_name, uint32_t flat_index);
plc_status_t staged_enqueue_write(staged_queue_t *q, const char *tag_name, uint32_t flat_index,
                                   const plc_value_t *val);

/* Remove all ops whose tag_name matches the given prefix (empty = remove all). */
void staged_remove(staged_queue_t *q, const char *tag_name_prefix);

/* Remove already-processed ops by compacting out slots marked !valid internally. */
void staged_clear(staged_queue_t *q);

#ifdef __cplusplus
}
#endif
