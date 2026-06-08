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

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "debug.h"

/*
 * Platform primitives needed locally: a statically-initializable lock, a
 * thread-local qualifier, and a wall-clock microsecond timer.  Kept here so the
 * debug system has no dependency on a wider platform layer.
 */
#if defined(_WIN32) || defined(_WIN64)
#    include <windows.h>

typedef SRWLOCK plc_lock_t;
#    define PLC_LOCK_INIT SRWLOCK_INIT
#    define THREAD_LOCAL __declspec(thread)

static void lock_acquire(plc_lock_t *l) { AcquireSRWLockExclusive(l); }
static void lock_release(plc_lock_t *l) { ReleaseSRWLockExclusive(l); }

static int64_t time_us(void) {
    FILETIME ft;
    ULARGE_INTEGER uli;

    GetSystemTimePreciseAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    /* FILETIME counts 100ns intervals since 1601; shift to the Unix epoch. */
    return (int64_t)((uli.QuadPart / 10) - 11644473600000000LL);
}

static void localtime_safe(const time_t *epoch, struct tm *out) { localtime_s(out, epoch); }

#else
#    include <pthread.h>
#    include <sys/time.h>

typedef pthread_mutex_t plc_lock_t;
#    define PLC_LOCK_INIT PTHREAD_MUTEX_INITIALIZER
#    define THREAD_LOCAL __thread

static void lock_acquire(plc_lock_t *l) { pthread_mutex_lock(l); }
static void lock_release(plc_lock_t *l) { pthread_mutex_unlock(l); }

static int64_t time_us(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);

    return (int64_t)tv.tv_sec * 1000000LL + (int64_t)tv.tv_usec;
}

static void localtime_safe(const time_t *epoch, struct tm *out) { localtime_r(epoch, out); }
#endif


/* Per-module levels, indexed directly by debug_module_t. */
static uint8_t debug_module_levels[DEBUG_MODULE_COUNT];
static int global_debug_level = DEBUG_NONE;

static plc_lock_t thread_num_lock = PLC_LOCK_INIT;
static uint32_t thread_num = 1;
static THREAD_LOCAL uint32_t this_thread_num = 0;

static plc_lock_t logger_lock = PLC_LOCK_INIT;
static void (*log_callback_func)(int64_t id, int debug_level, const char *message) = NULL;

static plc_lock_t stderr_lock = PLC_LOCK_INIT;
static int stderr_buffering_initialized = 0;
static uint64_t log_call_count = 0;


static const char *debug_module_names[DEBUG_MODULE_COUNT] = {
    [DEBUG_MODULE_LIB] = "LIB",         [DEBUG_MODULE_INIT] = "INIT",
    [DEBUG_MODULE_UTILS] = "UTILS",     [DEBUG_MODULE_PLATFORM] = "PLATFORM",
    [DEBUG_MODULE_SOCKET] = "SOCKET",   [DEBUG_MODULE_PROTOCOL] = "PROTOCOL",
    [DEBUG_MODULE_ENIP] = "ENIP",       [DEBUG_MODULE_CIP] = "CIP",
    [DEBUG_MODULE_MODBUS] = "MODBUS",   [DEBUG_MODULE_TAG] = "TAG",
    [DEBUG_MODULE_PATH] = "PATH",       [DEBUG_MODULE_SYSTEM] = "SYSTEM",
};

static const char *debug_level_names[DEBUG_END] = {"NONE", "ERROR", "WARN", "INFO", "DETAIL", "SPEW"};


const char *debug_module_name(debug_module_t module) {
    if((unsigned)module < DEBUG_MODULE_COUNT && debug_module_names[module]) { return debug_module_names[module]; }
    return "UNKNOWN";
}


bool debug_is_enabled(debug_module_t module, int level) {
    return level > DEBUG_NONE && (unsigned)module < DEBUG_MODULE_COUNT && level <= (int)debug_module_levels[module];
}


int debug_set_level(int level) {
    int old_level = global_debug_level;

    global_debug_level = level;

    /* Mirror the global level into every module so the hot-path check is one read. */
    for(int i = 0; i < DEBUG_MODULE_COUNT; i++) { debug_module_levels[i] = (uint8_t)level; }

    return old_level;
}


int debug_get_level(void) { return global_debug_level; }


void debug_module_set_level(debug_module_t module, int level) {
    if((unsigned)module < DEBUG_MODULE_COUNT) { debug_module_levels[module] = (uint8_t)level; }
}


int debug_module_get_level(debug_module_t module) {
    if((unsigned)module < DEBUG_MODULE_COUNT) { return debug_module_levels[module]; }
    return DEBUG_NONE;
}


void debug_set_all_modules(int level) {
    for(int i = 0; i < DEBUG_MODULE_COUNT; i++) { debug_module_levels[i] = (uint8_t)level; }
}


static uint32_t get_thread_id(void) {
    if(!this_thread_num) {
        lock_acquire(&thread_num_lock);
        this_thread_num = thread_num;
        thread_num++;
        lock_release(&thread_num_lock);
    }

    return this_thread_num;
}


static void ensure_stderr_buffering(void) {
    if(!stderr_buffering_initialized) {
        lock_acquire(&stderr_lock);
        if(!stderr_buffering_initialized) {
            setvbuf(stderr, NULL, _IOFBF, 8192);
            stderr_buffering_initialized = 1;
        }
        lock_release(&stderr_lock);
    }
}


void pdebug_impl(const char *func, int line_num, int debug_level, debug_module_t module, int64_t id, const char *templ, ...) {
    va_list va;
    struct tm t;
    time_t epoch;
    int64_t epoch_us;
    int remainder_us;
    const char *module_name = debug_module_name(module);
    const char *level_name = (debug_level >= 0 && debug_level < DEBUG_END) ? debug_level_names[debug_level] : "?";
    char prefix[1000]; /* MAGIC */
    char output[1000];

    ensure_stderr_buffering();

    epoch_us = time_us();
    epoch = (time_t)(epoch_us / 1000000);
    remainder_us = (int)(epoch_us % 1000000);

    localtime_safe(&epoch, &t);

    /* Compose the line template; the user's template becomes the format tail. */
    snprintf(prefix, sizeof(prefix), "%04d-%02d-%02d %02d:%02d:%02d.%06d thread(%u) id(%" PRId64 ") [%s] %s %s:%d %s\n",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, remainder_us, get_thread_id(), id,
             module_name, level_name, func, line_num, templ);
    prefix[sizeof(prefix) - 1] = 0;

    va_start(va, templ);
    vsnprintf(output, sizeof(output), prefix, va);
    va_end(va);

    if(log_callback_func) {
        log_callback_func(id, debug_level, output);
    } else {
        fputs(output, stderr);

        /* Flush on errors or periodically to bound latency without thrashing. */
        log_call_count++;
        if(debug_level <= DEBUG_ERROR || (log_call_count % 100) == 0) { fflush(stderr); }
    }
}


#define COLUMNS (16)

void pdebug_dump_bytes_impl(const char *func, int line_num, int debug_level, debug_module_t module, int64_t id, uint8_t *data,
                            int count) {
    int max_row, row, column;
    char row_buf[(COLUMNS * 3) + 5 + 1];

    max_row = (count + (COLUMNS - 1)) / COLUMNS;

    for(row = 0; row < max_row; row++) {
        int offset = (row * COLUMNS);
        int row_offset = 0;

        row_offset = snprintf(&row_buf[0], sizeof(row_buf), "%05d", offset);

        for(column = 0; column < COLUMNS && ((row * COLUMNS) + column) < count && row_offset < (int)sizeof(row_buf); column++) {
            offset = (row * COLUMNS) + column;
            row_offset += snprintf(&row_buf[row_offset], sizeof(row_buf) - (size_t)row_offset, " %02x", data[offset]);
        }

        row_buf[sizeof(row_buf) - 1] = 0;

        pdebug_impl(func, line_num, debug_level, module, id, "%s", row_buf);
    }
}


plc_status_t debug_register_logger(void (*log_callback_func_arg)(int64_t id, int debug_level, const char *message)) {
    plc_status_t rc = PLC_STATUS_OK;

    lock_acquire(&logger_lock);
    if(!log_callback_func) {
        log_callback_func = log_callback_func_arg;
    } else {
        rc = PLC_STATUS_ERR_INTERNAL;
    }
    lock_release(&logger_lock);

    return rc;
}


plc_status_t debug_unregister_logger(void) {
    plc_status_t rc = PLC_STATUS_OK;

    lock_acquire(&logger_lock);
    if(log_callback_func) {
        log_callback_func = NULL;
    } else {
        rc = PLC_STATUS_ERR_INTERNAL;
    }
    lock_release(&logger_lock);

    return rc;
}


void debug_flush(void) {
    if(!log_callback_func) { fflush(stderr); }
}
