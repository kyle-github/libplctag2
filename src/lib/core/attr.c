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

#include "core/attr.h"
#include "debug.h"

#define MAX_PAIRS 32

struct plc_attr_t {
    char  *buf;                  /* owns the duped connection string */
    char  *keys[MAX_PAIRS];
    char  *values[MAX_PAIRS];
    size_t count;
};

plc_attr_t *plc_attr_parse(const char *connect_str, plc_status_t *status) {
    plc_status_t dummy;
    if(!status) { status = &dummy; }

    if(!connect_str) {
        *status = PLC_STATUS_ERR_NULL_PTR;
        return NULL;
    }

    pdebug(DEBUG_MODULE_LIB, DEBUG_DETAIL, 0, "parsing \"%s\"", connect_str);

    plc_attr_t *a = calloc(1, sizeof(*a));
    if(!a) {
        *status = PLC_STATUS_ERR_NO_MEM;
        return NULL;
    }

    a->buf = strdup(connect_str);
    if(!a->buf) {
        free(a);
        *status = PLC_STATUS_ERR_NO_MEM;
        return NULL;
    }

    /* Walk the buffer in-place, splitting on '&' then '='. */
    char *p = a->buf;
    while(*p && a->count < MAX_PAIRS) {
        /* Token spans until next '&' or end-of-string. */
        char *token = p;
        while(*p && *p != '&') { p++; }
        if(*p == '&') { *p++ = '\0'; }

        /* Split token on first '='. */
        char *eq = strchr(token, '=');
        if(!eq) {
            /* Malformed pair — skip. */
            pdebug(DEBUG_MODULE_LIB, DEBUG_WARN, 0, "skipping malformed attribute \"%s\"", token);
            continue;
        }

        *eq = '\0';
        a->keys[a->count]   = token;
        a->values[a->count] = eq + 1;
        a->count++;
    }

    *status = PLC_STATUS_OK;
    return a;
}

const char *plc_attr_get_str(const plc_attr_t *a, const char *key, const char *def) {
    if(!a || !key) { return def; }

    for(size_t i = 0; i < a->count; i++) {
        if(strcmp(a->keys[i], key) == 0) { return a->values[i]; }
    }

    return def;
}

int plc_attr_get_int(const plc_attr_t *a, const char *key, int def) {
    const char *s = plc_attr_get_str(a, key, NULL);
    if(!s) { return def; }

    char *end;
    long v = strtol(s, &end, 10);
    if(end == s || *end != '\0') { return def; }

    return (int)v;
}

void plc_attr_free(plc_attr_t *a) {
    if(!a) { return; }
    free(a->buf);
    free(a);
}
