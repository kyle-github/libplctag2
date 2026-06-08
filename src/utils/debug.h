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
 * Leveled, per-module debug logging.  Self-contained: depends only on the
 * public status codes in plctag.h and the C runtime.  Thread-safe.
 *
 * Adapted from the libplctag utils/debug system.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "plctag.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Verbosity levels, lowest to highest. */
typedef enum {
    DEBUG_NONE = 0,
    DEBUG_ERROR = 1,  /* fatal for the whole application */
    DEBUG_WARN = 2,   /* serious problem that will not crash the app */
    DEBUG_INFO = 3,   /* high-level normal-operation output */
    DEBUG_DETAIL = 4, /* low-level normal-operation output, not high frequency */
    DEBUG_SPEW = 5,   /* extremely high-frequency output */
    DEBUG_END = 6,
} debug_level_t;

/*
 * Subsystem identifiers.  Each module has its own independently settable level.
 * Keep DEBUG_MODULE_COUNT last.
 */
typedef enum {
    DEBUG_MODULE_LIB = 0,    /* library core / public API */
    DEBUG_MODULE_INIT,       /* startup and teardown */
    DEBUG_MODULE_UTILS,      /* utility code (arena, bytes, etc.) */
    DEBUG_MODULE_PLATFORM,   /* platform abstraction */
    DEBUG_MODULE_SOCKET,     /* socket / transport layer */
    DEBUG_MODULE_PROTOCOL,   /* protocol-neutral dispatch */
    DEBUG_MODULE_ENIP,       /* EtherNet/IP encapsulation */
    DEBUG_MODULE_CIP,        /* CIP */
    DEBUG_MODULE_MODBUS,     /* Modbus */
    DEBUG_MODULE_TAG,        /* tag / value handling */
    DEBUG_MODULE_PATH,       /* path parsing and descent */
    DEBUG_MODULE_SYSTEM,     /* system-level operations */
    DEBUG_MODULE_COUNT,
} debug_module_t;

/* Look up the printable name of a module ("UTILS", "ENIP", ...). */
extern const char *debug_module_name(debug_module_t module);

/* Set every module to the given level.  Returns the previous global level. */
extern int debug_set_level(int level);
extern int debug_get_level(void);

/* Per-module level control. */
extern void debug_module_set_level(debug_module_t module, int level);
extern int debug_module_get_level(debug_module_t module);
extern void debug_set_all_modules(int level);

/* True when a message at (module, level) would be emitted. */
extern bool debug_is_enabled(debug_module_t module, int level);

extern void pdebug_impl(const char *func, int line_num, int debug_level, debug_module_t module, int64_t id, const char *templ,
                        ...);

extern void pdebug_dump_bytes_impl(const char *func, int line_num, int debug_level, debug_module_t module, int64_t id,
                                   uint8_t *data, int count);

#if defined(_WIN32) && defined(_MSC_VER)
/* MinGW on Windows already provides __func__. */
#    define __func__ __FUNCTION__
#endif

/* Compile-time ceiling: calls above this level are removed by the preprocessor. */
#ifndef PLC_COMPILE_DEBUG_LEVEL
#    define PLC_COMPILE_DEBUG_LEVEL DEBUG_DETAIL
#endif

#define pdebug(module, dbg, id, ...)                                                                                      \
    do {                                                                                                                  \
        if((dbg) <= PLC_COMPILE_DEBUG_LEVEL) {                                                                           \
            if(debug_is_enabled((module), (dbg))) { pdebug_impl(__func__, __LINE__, (dbg), (module), (id), __VA_ARGS__); } \
        }                                                                                                                 \
    } while(0)

#define pdebug_dump_bytes(module, dbg, id, d, c)                                              \
    do {                                                                                      \
        if((dbg) <= PLC_COMPILE_DEBUG_LEVEL) {                                               \
            if(debug_is_enabled((module), (dbg))) {                                           \
                pdebug_dump_bytes_impl(__func__, __LINE__, (dbg), (module), (id), (d), (c)); \
            }                                                                                 \
        }                                                                                     \
    } while(0)

/*
 * Install a callback that receives every formatted log line instead of stderr.
 * Returns PLC_STATUS_ERR_INTERNAL if a logger is already registered.
 */
extern plc_status_t debug_register_logger(void (*log_callback_func)(int64_t id, int debug_level, const char *message));
extern plc_status_t debug_unregister_logger(void);

/* Flush any buffered stderr output. */
extern void debug_flush(void);

#ifdef __cplusplus
}
#endif
