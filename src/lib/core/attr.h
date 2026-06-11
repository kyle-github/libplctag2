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
 * attr.h — connection-string attribute bag.
 *
 * Parses "key=value&key=value" connection strings into a flat typed bag.
 * Lifetime: create in plc_open, free before returning the handle.
 *
 * Keys consumed by the EtherNet/IP driver:
 *   protocol / scheme   driver selection (default "eip")
 *   gateway             host name or IP address
 *   port                TCP port (default 44818)
 *   path                CIP route, e.g. "1,0"
 *   plc                 PLC type hint, e.g. "controllogix" (optional)
 */

#include <stddef.h>

#include "plctag.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct plc_attr_t plc_attr_t; /* opaque */

/* Parse a connection string.  Returns NULL and sets *status on syntax error.
   The returned bag is heap-allocated; free it with plc_attr_free. */
extern plc_attr_t  *plc_attr_parse(const char *connect_str, plc_status_t *status);

/* Return the value for key, or def if the key is absent. */
extern const char  *plc_attr_get_str(const plc_attr_t *a, const char *key, const char *def);

/* Return the integer value of key, or def if absent or non-numeric. */
extern int          plc_attr_get_int(const plc_attr_t *a, const char *key, int def);

/* Free all memory owned by the attribute bag. */
extern void         plc_attr_free(plc_attr_t *a);

#ifdef __cplusplus
}
#endif
