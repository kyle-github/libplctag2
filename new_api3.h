#pragma once

/*
 * Try #3 at a new API, full async?
 */

typedef uint64_t plc_value_conn_t; /* opaque connection value for promise-based API */
typedef uint64_t plc_promise_t;    /* opaque promise handle */


typedef void (*plc_promise_callback_t)(plc_promise_t promise, void *user_data);


plc_status_t plc_promise_free(plc_promise_t promise);

plc_promise_t plc_open_conn(const char *connect_str, void *user_data, uint32_t timeout_ms);
plc_promise_t plc_close_conn(plc_value_conn_t conn, void *user_data /* ????? */, uint32_t timeout_ms);
plc_status_t plc_get_last_error(plc_value_conn_t conn);

plc_promise_type_t plc_promise_type(plc_promise_t promise);
plc_status_t plc_promise_status(plc_promise_t promise);
void *plc_promise_user_data(plc_promise_t promise);
plc_status_t plc_promise_free(plc_promise_t promise);

/* get scalar values from a promise, strings and byte strings are owned by the library */
int64_t plc_promise_result_int(plc_promise_t promise);
double plc_promise_result_double(plc_promise_t promise);
bool plc_promise_result_bool(plc_promise_t promise);
const char *plc_promise_result_string(plc_promise_t promise);
const uint8_t *plc_promise_result_bytes(plc_promise_t promise);

/* batches */
plc_batch_t plc_batch_create(plc_value_conn_t conn);
plc_status_t plc_batch_add_promise(plc_batch_t batch, plc_promise_t promise);
plc_promise_t plc_batch_execute(plc_batch_t batch, uint32_t timeout_ms);
/* clean up by freeing the promise */

/* reads */

/* if retention is zero then do the operation and wait for it to complete.  The timeout is set on the connection handle. */
plc_promise_t plc_read_int(plc_value_conn_t conn, const char *path, uint32_t index, bool async, uint32_t retention_ms);
plc_promise_t plc_read_double(plc_value_conn_t conn, const char *path, uint32_t index, bool async, uint32_t retention_ms);
plc_promise_t plc_read_bool(plc_value_conn_t conn, const char *path, uint32_t index, bool async, uint32_t retention_ms);
plc_promise_t plc_read_string(plc_value_conn_t conn, const char *path, uint32_t index, bool async, uint32_t retention_ms);
plc_promise_t plc_read_bytes(plc_value_conn_t conn, const char *path, uint32_t index, bool async, uint32_t retention_ms);

/* writes -- is retention useful here?  How do we delay writes? */
plc_promise_t plc_write_int(plc_value_conn_t conn, const char *path, uint32_t index, int64_t value, bool async);
plc_promise_t plc_write_double(plc_value_conn_t conn, const char *path, uint32_t index, double value, bool async);
plc_promise_t plc_write_bool(plc_value_conn_t conn, const char *path, uint32_t index, bool value, bool async);
plc_promise_t plc_write_string(plc_value_conn_t conn, const char *path, uint32_t index, const char *value, bool async);
plc_promise_t plc_write_bytes(plc_value_conn_t conn, const char *path, uint32_t index, const uint8_t *value, size_t size,
                              bool async);

/*
 * TBD
 *
 * - Is the bool flag sufficient for controlling async vs sync behavior?
 *
 * - what does async=true, retention=0 mean?
 *
 * - should prefetch be a separate function? It can be done via plc_read_bytes() with retention > 0, but maybe it would be
 * cleaner to have a separate function for it?  You can prefetch to get a buffer to read or write.  This makes me think
 * that it would be cleaner to have a separate function.
 *
 * - what is the guaranteed ordering of operations?  We probably should make sure that operations are executed in order per
 * connection, but do we need to guarantee that a write will be executed after a read that was issued before it?  Or can we allow
 * the implementation to reorder operations for performance reasons?  If we allow reordering then we need to make sure that the
 * API is clear about this and that there are ways for the caller to enforce ordering if they need to.
 *
 * - Should the implementation be allowed to consolidate and reorder operations?  We should be able to prefetch a full array or
 * struct if the caller isses a plc_read_bytes() for the whole thing with a retention time.  Then the caller can do smaller
 * reads/writes for individual elements and the implementation can satisfy those from the prefetched data.  This would be a big
 * performance win for large arrays and structs make sure that the API is clear about it and that there are ways for the caller to
 * disable it if they need to.
 *
 * - I am leaning toward only guaranteeing that the order of operations is the order in which they
 * are issued and not reordering them unless there is a real performance benefit shown through testing.
 *
 * - what is the minimal amount of thread safety required?  Nothing should crash if promises are used from multiple threads,
 * but do we need to guarantee that operations on the same connection are thread safe?  Do we need ot guarantee an ordering
 * of operations across threads?  Leaning toward only guaranteeing the order of operations if they all come from the same thread.
 *
 * - if different threads add promises to the same batch do we need to guarantee an ordering of those promises within the batch?
 *
 * - should we have a TTL for promises? That would ensure eventual clean up if the app forgets one.  Some languages do not have
 * finalizers so we probably need a defense in depth for resources.
 */


/* how do we handle path creation? */


/* promise value type is C string, does this prefetch?  It could. */
plc_promise_t plc_path_open(plc_value_conn_t conn, const char *path, bool async);

/* the index works for arrays and structs */
plc_promise_t plc_path_add_child(plc_promise_t path_promise, uint32_t child_index, bool async);

/* clean up by closing the promise */
