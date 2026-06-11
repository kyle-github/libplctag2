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
 * vendor_registry.c — select the eip_vendor_t implementation from an
 * Identity object probe.
 *
 * Selection is based on vendor_id from the CIP Identity object.
 * Unrecognised vendors fall back to eip_vendor_generic (no tag directory).
 */

#include "protocol/eip/vendor/vendor.h"
#include "debug.h"

/* CIP vendor IDs */
#define VENDOR_ID_ROCKWELL 0x0001u  /* Rockwell Automation / Allen-Bradley */

extern const eip_vendor_t eip_vendor_generic;
extern const eip_vendor_t eip_vendor_rockwell;


const eip_vendor_t *eip_vendor_select(const eip_identity_t *id) {
    if(!id) { return &eip_vendor_generic; }

    switch(id->vendor_id) {
        case VENDOR_ID_ROCKWELL:
            pdebug(DEBUG_MODULE_PROTOCOL, DEBUG_INFO, 0,
                   "vendor: Rockwell/Allen-Bradley (vendor_id=0x%04X)",
                   id->vendor_id);
            return &eip_vendor_rockwell;

        default:
            pdebug(DEBUG_MODULE_PROTOCOL, DEBUG_INFO, 0,
                   "vendor: unrecognised vendor_id=0x%04X, using generic",
                   id->vendor_id);
            return &eip_vendor_generic;
    }
}
