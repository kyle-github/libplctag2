<!--
  Copyright (C) 2026 by Kyle Hayes - kyle.hayes@gmail.com
  This software is available under the MIT license.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
-->

# new_api4 worked examples

These exercise the node API in `new_api4.h` to validate that it can express the
common cases. `await_resolved` and `plc_sleep` are illustrative shims; a real
app would wait on a callback + condition variable instead of polling.

```c
/* Metadata resolves asynchronously after plc_path_to_node; block until known. */
static plc_status_t await_resolved(plc_node_t n, int timeout_ms) {
    int waited = 0;
    while (plc_node_status(n) == PLC_STATUS_PENDING && waited < timeout_ms) {
        plc_sleep(10);
        waited += 10;
    }
    return plc_node_status(n);
}
```

## 1. List all tags

Pure metadata walk — no value reads, so it costs only the type-tree fetch.

```c
static void print_node(plc_node_t n, int depth) {
    char name[128] = {0};
    char type[64]  = {0};
    plc_node_name(n, name, sizeof name);
    plc_node_t def = plc_node_type_info(n);                 /* type definition node */
    if (def != PLC_INVALID_NODE) { plc_node_name(def, type, sizeof type); } /* "MOTOR", "DINT", ... */

    const char *label = name[0] ? name : "[elem]";
    if (plc_node_type(n) == PLC_VAL_ARRAY) {
        printf("%*s%-24s array[%zu] %s\n", depth * 2, "", label,
               plc_node_child_count(n), type);
    } else {
        printf("%*s%-24s %-6d %s\n", depth * 2, "", label,
               (int)plc_node_type(n), type);
    }

    size_t kids = plc_node_child_count(n);
    for (size_t i = 0; i < kids; i++) {
        print_node(plc_node_child(n, i), depth + 1);
    }
}

int main(void) {
    plc_node_t dev = plc_open("enip-tcp://10.1.2.3/1/5", 5000);
    if (dev == PLC_INVALID_NODE) { return 1; }

    plc_node_t tags = plc_path_to_node(dev, "tags");
    if (await_resolved(tags, 5000) != PLC_STATUS_OK) {
        char err[256]; plc_last_error(dev, err, sizeof err);
        fprintf(stderr, "resolve failed: %s\n", err);
        plc_close(dev);
        return 1;
    }

    size_t n = plc_node_child_count(tags);
    for (size_t i = 0; i < n; i++) {
        print_node(plc_node_child(tags, i), 0);   /* recurses into UDTs/arrays */
    }

    plc_close(dev);
    return 0;
}
```

## 2. Read an array, update every other element (prefetch + write coalescing)

One coalesced read in, local read-modify-write, one coalesced write out.

```c
plc_node_t arr = plc_path_to_node(dev, "tags/PumpSpeeds");   /* DINT[100] */
if (await_resolved(arr, 2000) != PLC_STATUS_OK) { /* handle */ }

/* Hold the prefetched values for the whole transaction (no mid-loop refetch). */
plc_node_set_read_policy(arr, UINT32_MAX, /*auto_refresh=*/false);
/* Defer + coalesce the element writes into a single device write. */
plc_node_set_write_policy(arr, /*write_delay_ms=*/50);

/* Pull the entire array once, in one (possibly fragmented) request. */
if (plc_prefetch(arr, 2000) != PLC_STATUS_OK) { /* handle */ }

size_t n = plc_node_child_count(arr);
for (size_t i = 0; i < n; i += 2) {           /* every other element */
    plc_node_t e = plc_node_child(arr, i);
    int64_t v = plc_node_get_int(e);          /* served from cache, no IO */
    plc_node_set_int(e, v + 10);              /* marks dirty; write-back */
}

/* Force the coalesced batch out now rather than waiting for the 50 ms timer. */
plc_flush(arr, 2000);                         /* single write of all dirty elements */
```

Without `plc_prefetch` the first `plc_node_get_int` would still coalesce the
whole-subtree fetch (read policy covers the subtree), but prefetch makes the
load explicit and lets you block until it lands. Without `write_delay` each
`plc_node_set_int` would be its own device write; with it they batch, and
`plc_flush` is the manual "send now."

## 3. C# async/await over the callbacks

The node cursor is a `ulong` and doubles as the correlation token, so the bridge
is a dictionary lookup — no per-request handle bookkeeping.

```csharp
static class Plc {
    const string LIB = "plctag2";
    [DllImport(LIB)] public static extern ulong plc_open(string s, int t);
    [DllImport(LIB)] public static extern int   plc_close(ulong dev);
    [DllImport(LIB)] public static extern ulong plc_path_to_node(ulong b, string p);
    [DllImport(LIB)] public static extern long  plc_node_get_int(ulong n);
    [DllImport(LIB)] public static extern int   plc_node_set_int(ulong n, long v);
    [DllImport(LIB)] public static extern int   plc_prefetch(ulong n, int t);
    [DllImport(LIB)] public static extern int   plc_flush(ulong n, int t);

    public delegate void NodeCb(ulong dev, ulong node, int evt, int op, int status, long tsUs, IntPtr user);
    [DllImport(LIB)] public static extern int plc_node_set_callback(ulong n, NodeCb cb, IntPtr user);

    public const int EVENT_VALUE_UPDATED = 2;   // PLC_EVENT_VALUE_UPDATED
    public const int EVENT_WRITE_DONE    = 3;   // PLC_EVENT_WRITE_DONE
}

sealed class PlcConnection : IDisposable {
    readonly ulong _dev;
    // One in-flight async op per node; the library cache coalesces duplicates.
    readonly ConcurrentDictionary<ulong, object> _pending = new();
    // Keep the delegate rooted for the connection's lifetime, or the GC frees it.
    readonly Plc.NodeCb _cb;

    public PlcConnection(string url) {
        _dev = Plc.plc_open(url, 5000);
        if (_dev == 0) throw new InvalidOperationException("open failed");
        _cb = OnEvent;
    }

    public Task<long> ReadIntAsync(string path) {
        ulong node = Plc.plc_path_to_node(_dev, path);
        // RunContinuationsAsynchronously: don't run the awaiter on the job thread.
        var tcs = new TaskCompletionSource<long>(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[node] = tcs;
        Plc.plc_node_set_callback(node, _cb, IntPtr.Zero);
        Plc.plc_prefetch(node, 0);              // 0 = kick async; completes via callback
        return tcs.Task;
    }

    public Task WriteIntAsync(string path, long v) {
        ulong node = Plc.plc_path_to_node(_dev, path);
        var tcs = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        _pending[node] = tcs;
        Plc.plc_node_set_callback(node, _cb, IntPtr.Zero);
        Plc.plc_node_set_int(node, v);
        Plc.plc_flush(node, 0);                 // 0 = kick async; completes on WRITE_DONE
        return tcs.Task;
    }

    // Runs on the library job thread. Find the awaiter by node and complete it.
    void OnEvent(ulong dev, ulong node, int evt, int op, int status, long tsUs, IntPtr user) {
        if (!_pending.TryRemove(node, out var pending)) return;

        if (evt == Plc.EVENT_VALUE_UPDATED && pending is TaskCompletionSource<long> r) {
            if (status == 0) r.SetResult(Plc.plc_node_get_int(node));
            else r.SetException(new PlcException(status));
        } else if (evt == Plc.EVENT_WRITE_DONE && pending is TaskCompletionSource w) {
            if (status == 0) w.SetResult();
            else w.SetException(new PlcException(status));
        }
    }

    public void Dispose() => Plc.plc_close(_dev);
}

// usage:
//   using var plc = new PlcConnection("enip-tcp://10.1.2.3/1/5");
//   long rpm = await plc.ReadIntAsync("tags/PumpSpeed");
//   await plc.WriteIntAsync("tags/PumpSpeed", rpm + 100);
```

Bridge notes:
- The `ulong` node is the correlation token — no request-id table needed.
- The callback fires on the library job thread; `RunContinuationsAsynchronously`
  keeps the `await` continuation off that thread so a slow awaiter can't stall
  finalization or other callbacks.
- Keep the `NodeCb` delegate rooted (a field here) for the connection lifetime.
- This bridge allows one outstanding async op per node. Concurrent awaits of the
  same node would need a per-node list; the cache already coalesces duplicate
  reads, so it is rarely needed.

## 4. Blocking read then blocking write (C)

The default policies make a plain getter blocking and a `plc_flush` block for the
ack — no callbacks needed.

```c
plc_node_t tag = plc_path_to_node(dev, "tags/PumpSpeed");      /* DINT */
if (await_resolved(tag, 2000) != PLC_STATUS_OK) { /* handle */ }

/* Blocking read: default read policy is retention 0 + auto_refresh false, so the
   value is always "stale" and the getter fetches, blocking up to the timeout. */
int64_t rpm = plc_node_get_int(tag);
if (plc_node_status(tag) != PLC_STATUS_OK) {
    char err[256]; plc_last_error(dev, err, sizeof err);
    fprintf(stderr, "read failed: %s\n", err);
    return;
}

/* Blocking write: the setter marks the node dirty; plc_flush with a non-zero
   timeout sends it and blocks until the device acknowledges. */
plc_node_set_int(tag, rpm + 100);
if (plc_flush(tag, 2000) != PLC_STATUS_OK) {
    char err[256]; plc_last_error(dev, err, sizeof err);
    fprintf(stderr, "write failed: %s\n", err);
}
```

## 5. Async read then async write (C)

Same tag, non-blocking: kick the IO with `timeout == 0` and let the callback
chain read → write. Everything runs off the job thread.

```c
static void rw_chain(plc_node_t dev, plc_node_t node, plc_event_type_t ev,
                     plc_op_t op, plc_status_t st, int64_t ts_us, void *user) {
    if (ev == PLC_EVENT_VALUE_UPDATED) {            /* async read landed */
        if (st != PLC_STATUS_OK) { fprintf(stderr, "read failed: %d\n", st); return; }
        int64_t rpm = plc_node_get_int(node);       /* from cache, no IO */
        plc_node_set_int(node, rpm + 100);
        plc_flush(node, 0);                          /* 0 = async; WRITE_DONE later */
    } else if (ev == PLC_EVENT_WRITE_DONE) {         /* async write acked */
        printf("write %s @ %lld us\n", st == PLC_STATUS_OK ? "ok" : "failed",
               (long long)ts_us);
    }
}

/* kick it off */
plc_node_t tag = plc_path_to_node(dev, "tags/PumpSpeed");
plc_node_set_callback(tag, rw_chain, NULL);
plc_prefetch(tag, 0);    /* 0 = async read; VALUE_UPDATED fires when it lands */
/* return to your event loop; the callback drives the read-then-write sequence */
```

## 6. Whole tag waited on, only some leaves change

Subscribe to the entire array as one coalesced poll, but ask for change events
only. Each tick reads all elements in a single packet; the library diffs every
leaf and delivers `VALUE_CHANGED` only for the few that moved. The callback sits
on the array (the "whole tag"), and changed-leaf events bubble up to it, naming
the specific element.

```c
static void on_elem_change(plc_node_t dev, plc_node_t node, plc_event_type_t ev,
                           plc_op_t op, plc_status_t st, int64_t ts_us, void *user) {
    /* `node` is the specific child leaf that changed; plc_node_parent(node) is
       the array we attached the callback to. */
    char name[64] = {0};
    plc_node_name(node, name, sizeof name);          /* e.g. "[7]" */
    printf("%s = %lld  @ %lld us\n", name,
           (long long)plc_node_get_int(node), (long long)ts_us);
}

plc_node_t arr = plc_path_to_node(dev, "tags/PumpSpeeds");      /* DINT[100] */
if (await_resolved(arr, 2000) != PLC_STATUS_OK) { /* handle */ }

plc_node_set_read_policy(arr, /*retention_ms=*/100, /*auto_refresh=*/true); /* coalesced poll */
plc_node_set_event_mask (arr, PLC_EVENT_VALUE_CHANGED);                     /* edge-triggered */
plc_node_set_callback   (arr, on_elem_change, NULL);
/* Every 100 ms: one request for all 100 elements; unchanged elements cost only a
   memcmp and never wake the app; the handful that changed each fire one event. */
```

Note the leverage: you wait on one node (the array) and configure one policy, yet
get per-element notifications without a callback on every leaf, and without the
job thread doing work for the elements that did not change.

