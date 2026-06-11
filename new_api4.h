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
 * Node-based, dynamically-typed API (Python-like).
 *
 * Everything is a plc_node_t -- a single opaque, pass-by-copy *cursor* that
 * names one node in a device's tree.  A node can be:
 *   - a path or a value (resolved lazily; see "pending" below),
 *   - a scalar, an array, or a struct,
 *   - the device itself (the root node).
 * A node has a type, an optional name, an optional parent, zero or more
 * children, and an optional attached user-data pointer and callback.
 *
 * Cursor encoding (illustrative; tunable -- the bit fields are never visible to
 * the application, so the split can change freely between releases):
 *     bits [63:61]  value type  (3 bits; cached copy of plc_value_type_t)
 *     bits [60:53]  device id   (handle-table slot)
 *     bits [52:45]  generation  (invalidates stale cursors after reuse/edit)
 *     bits [44:29]  tag id      (0 = the device's own schema tree; >=1 = tags)
 *     bits [28: 0]  node index  (deterministic DFS index in that tree)
 *
 * The cached type lets the type-specific getters/setters validate the node type
 * by masking the cursor -- no table lookup and no lock -- which removes a whole
 * class of error without synchronization.  (A still-PENDING node carries
 * PLC_VAL_UNKNOWN in these bits until its metadata resolves; the fast-path check
 * treats UNKNOWN as "defer to plc_node_status".)
 *
 * Because a cursor is just a uint64_t it marshals trivially across every FFI
 * boundary and doubles as the correlation token for async completion.  The
 * cursor is an *identifier*, not storage: per-node mutable state (cached value,
 * dirty flag, pending status, user data, callback) lives in a per-device table
 * that is populated lazily, only for nodes the app actually touches.  So there
 * is nothing for the caller to free.
 *
 * Lifetime / safety:  the device is reference counted internally (cf. libplctag
 * rc).  Every API call acquires the device for its duration, so a concurrent
 * plc_close() cannot pull storage out from under an in-flight call.  String and
 * bytes accessors are caller-buffer (below) so no library-owned pointer can
 * dangle across a close, and buffers can be reused across reads.
 *
 * Pending:  resolving a path (plc_path_to_node) returns immediately with a node
 * whose status is PLC_STATUS_PENDING until the IO thread has fetched its type
 * info.  plc_node_status() reports this; a callback fires PLC_EVENT_RESOLVED when
 * done.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- DLL visibility ---- */
#if defined(_WIN32) || defined(_WIN64)
    #if defined(PLCTAG_EXPORT)
        #define PLCTAG_API __declspec(dllexport)
    #elif defined(PLCTAG_IMPORT)
        #define PLCTAG_API __declspec(dllimport)
    #else
        #define PLCTAG_API
    #endif
#else
    #define PLCTAG_API __attribute__((visibility("default")))
#endif

/* ---- The one handle type ---- */
typedef uint64_t plc_node_t;       /* opaque cursor; the device root is also a node */

#define PLC_INVALID_NODE ((plc_node_t)0) /* reserved: 0 is never a valid node */

/* ---- Status codes ---- */
typedef enum plc_status_t {
    PLC_STATUS_OK            = 0,
    PLC_STATUS_PENDING       = 1,  /* node not yet resolved, or IO in flight */
    PLC_STATUS_ERR           = -1,
    PLC_STATUS_TIMEOUT       = -2,
    PLC_STATUS_NOT_FOUND     = -3, /* path / child index does not exist */
    PLC_STATUS_TYPE_MISMATCH = -4,
    PLC_STATUS_STALE         = -5, /* cursor generation no longer matches */
    PLC_STATUS_NOT_SUPPORTED = -6,
} plc_status_t;

/* ---- Value types ---- */
typedef enum plc_value_type_t {
    PLC_VAL_UNKNOWN = 0,
    PLC_VAL_STRUCT,
    PLC_VAL_ARRAY,
    PLC_VAL_BOOL,
    PLC_VAL_INT,
    PLC_VAL_DOUBLE,
    PLC_VAL_STRING,
    PLC_VAL_BYTES,
} plc_value_type_t;

/* ---- Connection state ---- */
typedef enum plc_conn_state_t {
    PLC_CONN_DOWN = 0,
    PLC_CONN_CONNECTING,
    PLC_CONN_UP,
    PLC_CONN_IDLE_WAIT,
    PLC_CONN_ERR_WAIT,
    PLC_CONN_DISCONNECTING,
} plc_conn_state_t;

/* ======================================================================== */
/* Device (the root node)                                                   */
/* ======================================================================== */
/*
 * plc_open returns the device's root node.  Its name is the connect URL.  It is
 * a struct whose children form the device schema:
 *
 *     tags       struct  -- children are the controller tags (by name)
 *     types      struct  -- type definitions (UDTs and built-in scalars), by type
 *                           name; each is a type-info node (see plc_node_type_info)
 *     identity   struct  -- vendor_id, device_type, product_code, revision,
 *                           serial_number, product_name
 *     telemetry  struct  -- packet counts, retries, round-trip times, ...
 *     status     struct  -- connection state and last error
 *
 * e.g.  plc_path_to_node(dev, "tags/MyTag/Field")  or
 *       plc_path_to_node(dev, "identity").
 */
PLCTAG_API plc_node_t   plc_open (const char *connect_str, int timeout_ms); /* PLC_INVALID_NODE on error */
PLCTAG_API plc_status_t plc_close(plc_node_t dev);

PLCTAG_API plc_conn_state_t plc_status    (plc_node_t dev);
PLCTAG_API plc_status_t     plc_last_error(plc_node_t dev, char *buf, size_t buf_len);

/* ======================================================================== */
/* Resolving and navigating nodes                                           */
/* ======================================================================== */
/*
 * Resolve a path relative to `base` (the device root, or any struct/array node).
 * Returns a (possibly PENDING) node, or PLC_INVALID_NODE if the path is
 * malformed.  Does not block on type resolution -- see "Pending".  Caching,
 * refresh, and write behaviour are node properties (plc_node_set_read_policy,
 * plc_node_set_metadata_retention, plc_node_set_write_policy), not call-time
 * parameters, so the app configures a node once instead of repeating hints.
 */
PLCTAG_API plc_node_t plc_path_to_node(plc_node_t base, const char *path);
PLCTAG_API plc_node_t plc_node_parent (plc_node_t node); /* PLC_INVALID_NODE at the root */
PLCTAG_API plc_node_t plc_node_child  (plc_node_t node, size_t i); /* PLC_INVALID_NODE if oob/scalar */
PLCTAG_API size_t     plc_node_child_count(plc_node_t node);     /* 0 if scalar/unknown */

/* ---- Metadata ---- */
PLCTAG_API plc_status_t     plc_node_status(plc_node_t node); /* PENDING until resolved */
PLCTAG_API plc_value_type_t plc_node_type  (plc_node_t node); /* kind (STRUCT/INT/...); cheap, from cursor bits */
PLCTAG_API size_t           plc_node_size  (plc_node_t node); /* element bytes; 0 if unknown */
PLCTAG_API plc_status_t     plc_node_name  (plc_node_t node, char *buf, size_t buf_len); /* this node's own name */
PLCTAG_API plc_status_t     plc_node_to_path(plc_node_t node, char *buf, size_t buf_len); /* full path */

/* The node's TYPE DEFINITION: the single, shared definition node for this node's
 * type, under the device "types" schema.  Every instance of a type returns the
 * SAME type-info node.  Returns a node for UDTs and built-in scalars;
 * PLC_INVALID_NODE only on error / unresolved.
 *
 * A type-info node conforms to a common schema -- a STRUCT with well-known
 * fields, read with the ordinary plc_node_* calls:
 *
 *     name     STRING          the type name ("MOTOR", "DINT", ...)
 *     kind     INT             plc_value_type_t of the type
 *     size     INT             encoded size in bytes
 *     fields   ARRAY of field  present when kind == STRUCT (the UDT members)
 *     element  STRING          present when kind == ARRAY: element type name
 *     dims     ARRAY of INT     present when kind == ARRAY: dimension sizes
 *
 * Each "fields" element is itself a STRUCT:
 *
 *     name     STRING          field name ("PRE")
 *     type     STRING          field type name; resolve under "types" to reach
 *                              the shared definition (one definition per type)
 *     offset   INT             bit offset within the struct -- in BITS, so packed
 *                              BOOLs are representable (byte-aligned => mult. of 8)
 *
 * plc_node_name(def) equals the "name" field (it is the def's key under "types"),
 * so a node's two names are plc_node_name(node) for its own and
 * plc_node_name(plc_node_type_info(node)) for its type's. */
PLCTAG_API plc_node_t       plc_node_type_info(plc_node_t node);

/* ---- Caching / refresh / write policies (node properties; set once, inherited
       by subtree -- nearest ancestor wins, the device root sets the default) ---- */
/*
 * Read policy -- FRESHNESS, not eviction.  A cached value is retained until a
 * newer fetch replaces it (or the node is released / device closed); it is never
 * discarded merely because time passed.  retention_ms only decides, at access
 * time, whether the held copy is fresh enough to serve or must be refetched:
 *     retention_ms == 0          -> always fetch (every read hits the device)
 *     retention_ms == UINT32_MAX -> fetch once, then serve cache ~forever
 * On a whole-array/struct node the policy covers the subtree and the fetch is
 * coalesced into one request.
 *
 * auto_refresh selects pull vs push (hence sync vs async):
 *     false -> lazy: a stale read blocks and refetches (synchronous "value now")
 *     true  -> the library proactively keeps the node within retention_ms by
 *              polling and fires PLC_EVENT_VALUE_UPDATED -- i.e. a subscription.
 *              Reads then serve the always-fresh cache without blocking; a read
 *              before the first poll lands reports PLC_STATUS_PENDING.
 * (auto_refresh = true is the subscription mechanism; there is no separate
 * subscribe call.)
 */
PLCTAG_API plc_status_t plc_node_set_read_policy(plc_node_t node, uint32_t retention_ms, bool auto_refresh);

/*
 * Metadata (type tree) policy -- same shape as the read policy, different
 * resource, with one extra consequence: an auto_refresh that picks up an online
 * edit can restructure the type tree, bump the tag generation, and thereby
 * invalidate outstanding cursors (they begin returning PLC_STATUS_STALE).  A data
 * refresh never invalidates a cursor.  Metadata is fetched automatically on first
 * resolve; defaults are long retention / auto_refresh = false.  retention_ms == 0
 * and UINT32_MAX carry the same meanings as above.
 */
PLCTAG_API plc_status_t plc_node_set_metadata_retention(plc_node_t node, uint32_t retention_ms, bool auto_refresh);

/*
 * Write policy -- a write-back coalescing window (not caching).  After a set the
 * library holds the write up to write_delay_ms, batching further sets on the
 * subtree into one device write (e.g. set on an array tag to merge per-element
 * updates into a single CIP write).  plc_flush forces the pending batch out now.
 *     write_delay_ms == 0 -> write-through (each set sent promptly)
 * Completion and errors arrive via PLC_EVENT_WRITE_DONE / plc_last_error, since a
 * delayed write has no return value to carry them.
 */
PLCTAG_API plc_status_t plc_node_set_write_policy(plc_node_t node, uint32_t write_delay_ms);

/* ---- Per-node user data ---- */
PLCTAG_API plc_status_t plc_node_set_user_data(plc_node_t node, void *user_data);
PLCTAG_API void        *plc_node_get_user_data(plc_node_t node);

/* ======================================================================== */
/* Scalar reads / writes                                                    */
/* ======================================================================== */
/* Getters serve from cache when fresh (per the node's read policy).  Under
   auto_refresh = false a stale read blocks up to the connection timeout to
   refetch; under auto_refresh = true reads never block but may report PENDING
   before the first poll.  On error/type mismatch they return 0 / 0.0 / false;
   call plc_node_status(). */
PLCTAG_API int64_t plc_node_get_int   (plc_node_t node);
PLCTAG_API double  plc_node_get_double(plc_node_t node);
PLCTAG_API bool    plc_node_get_bool  (plc_node_t node);

/* Caller-provided buffers (no library-owned pointers; reusable across reads).
   actual_len (out, optional) is the full length even if it exceeds buf_len. */
PLCTAG_API plc_status_t plc_node_get_string(plc_node_t node, char *buf, size_t buf_len, size_t *actual_len);
PLCTAG_API plc_status_t plc_node_get_bytes (plc_node_t node, uint8_t *buf, size_t buf_len, size_t *actual_len);

/* Setters mark the node dirty; the write is sent per the node's write policy
   (immediately if write-through, else coalesced) or forced out by plc_flush. */
PLCTAG_API plc_status_t plc_node_set_int   (plc_node_t node, int64_t v);
PLCTAG_API plc_status_t plc_node_set_double(plc_node_t node, double  v);
PLCTAG_API plc_status_t plc_node_set_bool  (plc_node_t node, bool    v);
PLCTAG_API plc_status_t plc_node_set_string(plc_node_t node, const char *s);
PLCTAG_API plc_status_t plc_node_set_bytes (plc_node_t node, const uint8_t *b, size_t len);

/* ======================================================================== */
/* Manual IO: prefetch (pull now) and flush (push now)                      */
/* ======================================================================== */
/*
 * These are the one-shot counterparts to the automatic read/write policies:
 *
 *            one-shot (manual)   automatic
 *     pull   plc_prefetch        read_policy auto_refresh
 *     push   plc_flush           write_policy write_delay
 *
 * plc_prefetch loads a whole subtree into cache NOW in one coalesced request
 * (fragmented transparently if it exceeds a packet), so subsequent element reads
 * are served locally.  On an array/struct it reads all children; on a leaf, that
 * one value.  timeout_ms > 0 blocks until the data lands; timeout_ms == 0 kicks
 * the fetch and returns immediately (completion via PLC_EVENT_VALUE_UPDATED).
 * Prefetch only pays off if the node's read retention covers the work window
 * (otherwise a getter will refetch) -- set a generous retention, or UINT32_MAX,
 * for a read-modify-write transaction.
 */
PLCTAG_API plc_status_t plc_prefetch(plc_node_t scope, int timeout_ms);

/* Force pending writes out now, overriding any write-delay.  `scope` is a
   subtree: a leaf flushes one node; an array/struct flushes all dirty leaves
   beneath it (coalesced); the device root flushes everything.  timeout_ms == 0
   kicks the IO and returns immediately. */
PLCTAG_API plc_status_t plc_flush(plc_node_t scope, int timeout_ms);

/* ======================================================================== */
/* Events                                                                   */
/* ======================================================================== */
/*
 * A callback may be attached to ANY node.  When the IO thread produces an event
 * for a node, it bubbles up the parent chain and is delivered to the FIRST node
 * that has a callback, then stops.  So a callback on a struct catches reads of
 * its members; only if no ancestor handles it does it reach the device root.
 * Because callbacks are node-scoped, an app subscribes them to exactly the
 * subtrees it cares about, which keeps the event volume per callback low -- so a
 * single event per callback invocation is fine in practice (no batch fan-out).
 *
 * Threading model (library-wide, not per-connection):
 *   - ONE IO thread services the sockets of all devices.  In testing a single
 *     thread comfortably drives many sockets; the target is low hundreds, and
 *     PLCs are slow at networking, so this is expected to be ample.
 *   - ONE job thread runs work that may block or take a while: invoking
 *     callbacks, finalizing refcount-zero objects (cf. libplctag's finalizer
 *     thread, generalized), and similar deferred jobs.
 * Delivery does NOT happen on the IO thread: it enqueues an event job; the job
 * thread drains the queue and invokes callbacks one at a time, in order, so a
 * slow callback backs up the job queue but never stalls IO.  Callbacks may do
 * moderate work but should not block indefinitely (they share the job thread
 * with finalization).  If the queue overflows, a single PLC_EVENT_OVERFLOW is
 * delivered to the device root and intermediate events are dropped.
 *
 * (Two threads total is the starting point to validate; if the job thread ever
 * becomes a bottleneck it can grow into a small pool without API changes.)
 *
 * The user_data passed to the callback is the node's attached user data
 * (plc_node_set_user_data), so set_callback's user_data and set_user_data are
 * the same slot.
 *
 * Change events:  on each refresh the library compares the incoming leaf value
 * to the cached one (a memcmp it can do because the old value is still in the
 * slot).  PLC_EVENT_VALUE_UPDATED fires whenever a read lands; PLC_EVENT_VALUE_
 * CHANGED fires only when the value differs -- edge- vs level-triggered.  Change
 * is detected at the leaf and bubbles like any other event, so a callback on a
 * struct learns which member changed.  The first successful read establishes the
 * baseline: it fires UPDATED but never CHANGED (so startup does not look like a
 * change).  Use plc_node_set_event_mask to receive only the classes you want.
 *
 * Wire-phase events (latency):  the four WRITE_START/WRITE_END/READ_START/
 * READ_END events fire on the device root, one set per request/response packet
 * on the socket, stamped on the IO thread so they measure true wire timing (not
 * job-thread delivery delay).  Every exchange sends a request then receives a
 * response, so the order is WRITE_START -> WRITE_END -> READ_START -> READ_END
 * whether the logical operation was a tag read or a tag write.  Note the
 * asymmetry: WRITE_START..WRITE_END brackets the active send, while READ_START..
 * READ_END brackets waiting-for-then-receiving the response -- READ_START is when
 * the connection begins waiting, before any bytes arrive.  Hence:
 *     send time  = WRITE_END - WRITE_START
 *     turnaround = READ_END  - READ_START   (mostly the device thinking)
 *     round trip = READ_END  - WRITE_START
 * A coalesced packet covering many tags yields one set of wire events on the
 * root but a per-tag VALUE_UPDATED/WRITE_DONE for each value.  STARTs carry
 * status PLC_STATUS_PENDING.  Wire events are off by default
 * (PLC_EVENT_MASK_DEFAULT); OR PLC_EVENT_WIRE_PHASES into the mask to enable.
 */
typedef enum plc_op_t {
    PLC_OP_NONE  = 0,
    PLC_OP_READ  = 1,
    PLC_OP_WRITE = 2,
} plc_op_t;

/* Bit flags, so plc_node_set_event_mask can OR them; also used singly as the
   callback's `event` argument. */
typedef enum plc_event_type_t {
    /* Node / value lifecycle -- fire on the tag node */
    PLC_EVENT_RESOLVED      = 0x001, /* a pending node's type became known    (op NONE)  */
    PLC_EVENT_VALUE_UPDATED = 0x002, /* a read landed (data maybe unchanged)  (op READ)  */
    PLC_EVENT_VALUE_CHANGED = 0x004, /* read value differs from last known    (op READ)  */
    PLC_EVENT_WRITE_DONE    = 0x008, /* a logical write was acknowledged      (op WRITE) */

    /* Connection wire phases -- fire on the device root, one set per packet */
    PLC_EVENT_WRITE_START   = 0x010, /* began sending a request packet        (op WRITE) */
    PLC_EVENT_WRITE_END     = 0x020, /* request packet fully sent             (op WRITE) */
    PLC_EVENT_READ_START    = 0x040, /* began waiting for the response        (op READ)  */
    PLC_EVENT_READ_END      = 0x080, /* response packet fully received        (op READ)  */

    /* System */
    PLC_EVENT_STATE_CHANGE  = 0x100, /* connection state changed (device root)(op NONE)  */
    PLC_EVENT_OVERFLOW      = 0x200, /* event queue overflowed; events lost   (op NONE)  */
} plc_event_type_t;

#define PLC_EVENT_WIRE_PHASES (PLC_EVENT_WRITE_START | PLC_EVENT_WRITE_END | \
                               PLC_EVENT_READ_START  | PLC_EVENT_READ_END)
#define PLC_EVENT_MASK_ALL     (0xFFFFFFFFu) /* every event class, incl. wire phases */
/* Default: everything except the high-volume per-packet wire-phase events. */
#define PLC_EVENT_MASK_DEFAULT (PLC_EVENT_MASK_ALL & ~PLC_EVENT_WIRE_PHASES)

/* dev          = device root node; node = the node the event originated on.
   timestamp_us = monotonic microseconds captured WHEN the event occurred (on the
                  IO thread for wire phases), not when the callback runs.  Because
                  delivery can be delayed by the job queue, always use this -- not
                  a clock read inside the callback -- to compute latencies.  Same
                  reference for every event on a device; for differencing only,
                  not wall-clock. */
typedef void (*plc_node_cb_t)(plc_node_t dev, plc_node_t node,
                              plc_event_type_t event, plc_op_t op,
                              plc_status_t status, int64_t timestamp_us,
                              void *user_data);

PLCTAG_API plc_status_t plc_node_set_callback(plc_node_t node, plc_node_cb_t cb, void *user_data);

/* Restrict which event classes are delivered for this node (bitwise-OR of
   plc_event_type_t).  The library still polls and diffs, but only enqueues a
   job for events in the mask -- e.g. set PLC_EVENT_VALUE_CHANGED alone for an
   edge-triggered subscription, so unchanged polls cost only a memcmp and never
   wake the app.  Default is PLC_EVENT_MASK_DEFAULT (all classes except the
   per-packet wire phases); OR in PLC_EVENT_WIRE_PHASES to measure latency. */
PLCTAG_API plc_status_t plc_node_set_event_mask(plc_node_t node, uint32_t mask);

/* ======================================================================== */
/* Diagnostics                                                              */
/* ======================================================================== */
PLCTAG_API const char *plc_status_str(plc_status_t status); /* static literal; no free */

#ifdef __cplusplus
}
#endif

/*
 * Enumeration sketch (cursors only -- nothing to free):
 *
 *   static void walk(plc_node_t n, int depth) {
 *       char name[128];
 *       plc_node_name(n, name, sizeof(name));
 *       printf("%*s%s : %d\n", depth*2, "", name[0] ? name : "[]", (int)plc_node_type(n));
 *       size_t c = plc_node_child_count(n);
 *       for (size_t i = 0; i < c; i++) walk(plc_node_child(n, i), depth + 1);
 *   }
 *   void list_tags(plc_node_t dev) { walk(plc_path_to_node(dev, "tags"), 0); }
 *
 * Open design points:
 *   - Cursor bit budget: 3/8/8/16/29 (type/device/gen/tag/node); a metadata
 *     auto_refresh that catches a UDT online-edit must bump the tag generation
 *     to retire old cursors (-> PLC_STATUS_STALE).
 *   - String write race: one thread reading while another sets the same node.
 *     Caller-buffer reads remove the dangling-pointer hazard; the slot still
 *     needs a lock/CAS so a concurrent set swaps storage cleanly.
 *   - Job thread vs. app-driven pump: a plc_pump_events(dev, timeout) could let
 *     the app run callbacks on its own thread instead of the library job thread
 *     (useful for UI-thread affinity). TBD whether to offer both.
 *   - Change-event deadband: PLC_EVENT_VALUE_CHANGED uses exact compare; REAL/
 *     LREAL noise may want plc_node_set_change_deadband(node, double) to suppress
 *     sub-band changes.  Default exact (band 0); add when needed.
 */
