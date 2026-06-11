#pragma once

/***************************************************************************
 *   Copyright (C) 2026 by Kyle Hayes                                      *
 *   Author Kyle Hayes  kyle.hayes@gmail.com                               *
 *                                                                         *
 *   This software is available under the MIT license.                     *
 ***************************************************************************/

/*
 * path.h — parsed tag path.
 *
 * Supports paths of the form:
 *   "TagName"           — top-level tag (n_subs = 0)
 *   "TagName[5]"        — 1-D subscript
 *   "TagName[2][3]"     — 2-D subscripts (up to 3 levels)
 *
 * Struct field access ("Tag.Field") is not yet supported (M4+/UDT).
 */

#include "plctag.h"
#include "core/metadata.h"   /* METADATA_MAX_TAG_NAME */

#ifdef __cplusplus
extern "C" {
#endif

struct plc_path_t {
    char tag_name[METADATA_MAX_TAG_NAME];
    int  subs[3];  /* subscripts parsed from the path string */
    int  n_subs;   /* number of valid entries in subs[] */
};
typedef struct plc_path_t plc_path_t;

/* Parse path_str into *out.  Returns ERR_BAD_PATH if the string is
   malformed or the tag name exceeds METADATA_MAX_TAG_NAME. */
plc_status_t path_parse(const char *path_str, plc_path_t *out);

#ifdef __cplusplus
}
#endif
