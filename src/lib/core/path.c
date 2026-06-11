/***************************************************************************
 *   Copyright (C) 2026 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This software is available under the MIT license.                     *
 ***************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "core/path.h"

plc_status_t path_parse(const char *path_str, plc_path_t *out) {
    if(!path_str || !out) { return PLC_STATUS_ERR_NULL_PTR; }
    memset(out, 0, sizeof(*out));

    /* Find end of tag name (stops at '[' or '\0'). */
    const char *p = path_str;
    while(*p && *p != '[') { p++; }
    size_t name_len = (size_t)(p - path_str);
    if(name_len == 0 || name_len >= METADATA_MAX_TAG_NAME) {
        return PLC_STATUS_ERR_BAD_PATH;
    }
    memcpy(out->tag_name, path_str, name_len);
    out->tag_name[name_len] = '\0';

    /* Parse zero or more [N] subscript blocks. */
    while(*p == '[' && out->n_subs < 3) {
        p++; /* skip '[' */
        char *end = NULL;
        long val = strtol(p, &end, 10);
        if(end == p || *end != ']' || val < 0) {
            return PLC_STATUS_ERR_BAD_PATH;
        }
        out->subs[out->n_subs++] = (int)val;
        p = end + 1; /* skip ']' */
    }

    return PLC_STATUS_OK;
}
