#pragma once

/***************************************************************************
 *   Copyright (C) 2026 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This software is available under the MIT license.                     *
 ***************************************************************************/

/*
 * node.h — a fully-resolved tag reference produced by driver->resolve.
 *
 * Carries everything driver->read and driver->write need to do I/O without
 * touching the metadata cache again.
 */

#include <stdint.h>
#include "plctag.h"
#include "core/metadata.h"   /* METADATA_MAX_TAG_NAME */

#ifdef __cplusplus
extern "C" {
#endif

struct plc_node_t {
    plc_value_type_t  type;        /* type for the caller (may be ARRAY) */
    plc_value_type_t  elem_type;   /* element type when type == PLC_VAL_ARRAY */
    uint16_t          native_type; /* raw CIP type descriptor */
    uint32_t          elem_size;   /* bytes per element */
    uint32_t          flat_index;  /* flat array element offset */
    uint32_t          dims[3];     /* original tag dimensions from metadata */
    int               n_dims;      /* number of valid dims entries */
    int               subs_used;   /* subscripts already applied in path */
    char              tag_name[METADATA_MAX_TAG_NAME];
};
typedef struct plc_node_t plc_node_t;

#ifdef __cplusplus
}
#endif
