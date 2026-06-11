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

#include <string.h>

#include "protocol/driver_registry.h"
#include "debug.h"

/*
 * Static registry of protocol drivers.  Add new driver vtable pointers here
 * as they are implemented.  The array is NULL-terminated; order does not
 * matter for correctness.
 */

extern const plc_driver_vtable_t eip_driver_vtable;

static const plc_driver_vtable_t *registry[] = {
    &eip_driver_vtable,
    NULL,
};


const plc_driver_vtable_t *plc_driver_for_scheme(const char *scheme) {
    if(!scheme) { return NULL; }

    for(size_t i = 0; registry[i]; i++) {
        if(strcmp(registry[i]->scheme, scheme) == 0) {
            pdebug(DEBUG_MODULE_LIB, DEBUG_DETAIL, 0, "driver found for scheme \"%s\"", scheme);
            return registry[i];
        }
    }

    pdebug(DEBUG_MODULE_LIB, DEBUG_WARN, 0, "no driver registered for scheme \"%s\"", scheme);
    return NULL;
}
