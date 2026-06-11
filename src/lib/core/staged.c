/***************************************************************************
 *   Copyright (C) 2026 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *   This software is available under the MIT license.                     *
 ***************************************************************************/

#include <stdlib.h>
#include <string.h>

#include "core/staged.h"

#define INITIAL_CAP 64u

plc_status_t staged_create(staged_queue_t **out) {
    if(!out) { return PLC_STATUS_ERR_NULL_PTR; }
    staged_queue_t *q = calloc(1, sizeof(*q));
    if(!q) { return PLC_STATUS_ERR_NO_MEM; }
    q->ops = calloc(INITIAL_CAP, sizeof(staged_op_t));
    if(!q->ops) { free(q); return PLC_STATUS_ERR_NO_MEM; }
    q->cap = INITIAL_CAP;
    *out = q;
    return PLC_STATUS_OK;
}

void staged_destroy(staged_queue_t *q) {
    if(!q) { return; }
    free(q->ops);
    free(q);
}

static plc_status_t staged_push(staged_queue_t *q, const staged_op_t *op) {
    if(q->count == q->cap) {
        size_t new_cap = q->cap * 2u;
        staged_op_t *tmp = realloc(q->ops, new_cap * sizeof(staged_op_t));
        if(!tmp) { return PLC_STATUS_ERR_NO_MEM; }
        q->ops = tmp;
        q->cap = new_cap;
    }
    q->ops[q->count++] = *op;
    return PLC_STATUS_OK;
}

plc_status_t staged_enqueue_read(staged_queue_t *q, const char *tag_name, uint32_t flat_index) {
    if(!q || !tag_name) { return PLC_STATUS_ERR_NULL_PTR; }

    /* Deduplicate: if there's already a read for this (tag, index), don't add another. */
    for(size_t i = 0; i < q->count; i++) {
        staged_op_t *op = &q->ops[i];
        if(op->kind == STAGE_READ && op->flat_index == flat_index &&
           strncmp(op->tag_name, tag_name, METADATA_MAX_TAG_NAME) == 0) {
            return PLC_STATUS_OK;
        }
    }

    staged_op_t op = { .kind = STAGE_READ, .flat_index = flat_index };
    strncpy(op.tag_name, tag_name, sizeof(op.tag_name) - 1u);
    return staged_push(q, &op);
}

plc_status_t staged_enqueue_write(staged_queue_t *q, const char *tag_name, uint32_t flat_index,
                                   const plc_value_t *val) {
    if(!q || !tag_name || !val) { return PLC_STATUS_ERR_NULL_PTR; }

    /* Update existing staged write for same (tag, index). */
    for(size_t i = 0; i < q->count; i++) {
        staged_op_t *op = &q->ops[i];
        if(op->kind == STAGE_WRITE && op->flat_index == flat_index &&
           strncmp(op->tag_name, tag_name, METADATA_MAX_TAG_NAME) == 0) {
            op->write_val = *val;
            return PLC_STATUS_OK;
        }
    }

    staged_op_t op = { .kind = STAGE_WRITE, .flat_index = flat_index, .write_val = *val };
    strncpy(op.tag_name, tag_name, sizeof(op.tag_name) - 1u);
    return staged_push(q, &op);
}

void staged_remove(staged_queue_t *q, const char *tag_name_prefix) {
    if(!q) { return; }
    bool remove_all = (!tag_name_prefix || !*tag_name_prefix);
    size_t out = 0;
    for(size_t i = 0; i < q->count; i++) {
        bool matches = remove_all ||
            strncmp(q->ops[i].tag_name, tag_name_prefix, strlen(tag_name_prefix)) == 0;
        if(!matches) {
            q->ops[out++] = q->ops[i];
        }
    }
    q->count = out;
}

void staged_clear(staged_queue_t *q) {
    if(q) { q->count = 0; }
}
