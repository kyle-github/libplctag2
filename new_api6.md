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

# new_api6 — the simple blocking API, with a result object

Connect, read a tag, close — pylogix-shaped and blocking. The read returns a
`plc_result_t`: an **owned, modifiable object** that carries the value(s), kind,
status, and element access. Because it is a real object (not a borrowed slot),
the same handle grows from a one-shot read into a writable array, a live
subscription, or an async operation — without ever re-specifying the tag.

## The driving problem (matching pylogix, then climbing past it)

- **Floor — pylogix parity.** A monitor must: connect; `Read(tag) -> Response`
  with `.Value / .Status / .TagName`; `Write(tag, val)`; read arrays; read a
  list of tags in one request; enumerate the tag list. Stateless, blocking,
  throwaway result.
- **What pylogix does not do well.** Live change monitoring is a hand-rolled
  poll loop; there is no async pipeline; writing back a whole modified array is
  awkward.
- **The bet.** Make the Response a persistent, owned, **modifiable** object,
  `plc_result_t`. Then *one* handle serves four jobs without re-stating the tag:
  1. **one-shot read** (blocking) — pylogix parity;
  2. **writable buffer** — read an array, edit elements, write it back in one
     coalesced request;
  3. **live value** — subscribe, and the result updates itself in place;
  4. **async operation** — a pending result you `wait` on or get a callback for.
- **The cost.** The result is owned, so you `plc_result_free` it. We trade the
  absolute three-line minimum (a borrowed slot) for one type that spans blocking,
  async, and subscription with no parallel API.
- **The seam.** `plc_result_t` is the cursor model's blocking-friendly cousin.
  The plain read needs no threads; the moment you add auto-update / async /
  callbacks you pull in the same background IO + job threads as the cursor API
  (new_api5). The simple layer grows *outward into* that layer, not down onto it.

## The simple read

```c
plc_t        plc = plc_connect("10.0.0.1", 5000);
plc_result_t r   = plc_read(plc, "PumpSpeed");
int64_t      rpm = plc_result_get_int(r, 0);   /* element 0 = the scalar */
plc_result_free(r);
plc_close(plc);
```

An owned result costs the `plc_result_free` line versus a borrowed handle — that
is the price of an object you can also modify, write, subscribe, or await.

## Handles, status, kind

```c
typedef struct plc_conn   *plc_t;        /* opaque connection; NULL == invalid */
typedef struct plc_result *plc_result_t; /* owned read/write result; free it    */

typedef enum {
    PLC_OK            =  0,
    PLC_PENDING       =  1,   /* async result not yet complete */
    PLC_ERR           = -1,
    PLC_ERR_TIMEOUT   = -2,
    PLC_ERR_CONN      = -3,
    PLC_ERR_NOT_FOUND = -4,
    PLC_ERR_TYPE      = -5,
} plc_status_t;

typedef enum {
    PLC_KIND_UNKNOWN = 0,
    PLC_KIND_BOOL,
    PLC_KIND_INT,
    PLC_KIND_FLOAT,
    PLC_KIND_STRING,
    PLC_KIND_ARRAY,
    PLC_KIND_STRUCT,
} plc_kind_t;
```

## Connect / close

```c
/* path: bare host ("10.0.0.1") or routed URL ("enip-tcp://10.0.0.1/1/0").
   Blocks up to timeout_ms. NULL on failure. */
plc_t        plc_connect(const char *path, int timeout_ms);
plc_status_t plc_close  (plc_t plc);
```

## Read and inspect the result

```c
plc_result_t plc_read  (plc_t plc, const char *tag);              /* blocking; whole tag    */
plc_result_t plc_read_n(plc_t plc, const char *tag, size_t count);/* up to count elements   */
void         plc_result_free(plc_result_t r);

plc_kind_t   plc_result_kind  (plc_result_t r);   /* element kind (INT, FLOAT, STRUCT, ...) */
plc_status_t plc_result_status(plc_result_t r);   /* this operation's status                */
size_t       plc_result_size  (plc_result_t r);   /* total bytes                            */
size_t       plc_result_count (plc_result_t r);   /* element count; 1 for a scalar          */
```

## Access elements (index 0 is the scalar)

```c
int64_t      plc_result_get_int   (plc_result_t r, size_t index);   /* any int width -> i64 */
double       plc_result_get_f64   (plc_result_t r, size_t index);
float        plc_result_get_f32   (plc_result_t r, size_t index);
bool         plc_result_get_bool  (plc_result_t r, size_t index);
int          plc_result_get_string(plc_result_t r, size_t index, char *buf, size_t len);
```

Arrays just iterate the same accessors:

```c
plc_result_t r = plc_read(plc, "PumpSpeeds");        /* DINT[100] */
for (size_t i = 0; i < plc_result_count(r); i++) {
    printf("%zu = %lld\n", i, (long long)plc_result_get_int(r, i));
}
plc_result_free(r);
```

## Modify and write back

Because the result is mutable, you edit it in place and push it out — one
coalesced write for a whole array:

```c
plc_status_t plc_result_set_int   (plc_result_t r, size_t index, int64_t value);
plc_status_t plc_result_set_f64   (plc_result_t r, size_t index, double  value);
plc_status_t plc_result_set_f32   (plc_result_t r, size_t index, float   value);
plc_status_t plc_result_set_bool  (plc_result_t r, size_t index, bool    value);
plc_status_t plc_result_set_string(plc_result_t r, size_t index, const char *s);

plc_status_t plc_result_write(plc_result_t r);   /* push the modified result to its tag */
```

```c
plc_result_t r = plc_read(plc, "PumpSpeeds");
for (size_t i = 0; i < plc_result_count(r); i += 2) {
    plc_result_set_int(r, i, plc_result_get_int(r, i) + 10);
}
plc_result_write(r);                              /* one request for all the edits */
plc_result_free(r);
```

## Simple writes (no result needed)

```c
plc_status_t plc_write_int   (plc_t plc, const char *tag, int64_t value);
plc_status_t plc_write_f64   (plc_t plc, const char *tag, double  value);
plc_status_t plc_write_f32   (plc_t plc, const char *tag, float   value);
plc_status_t plc_write_bool  (plc_t plc, const char *tag, bool    value);
plc_status_t plc_write_string(plc_t plc, const char *tag, const char *value);
```

## Errors

```c
plc_result_t r = plc_read(plc, "PumpSpeed");
if (plc_result_status(r) != PLC_OK) {
    fprintf(stderr, "read failed: %s\n", plc_last_error(plc));
}
```

```c
plc_status_t plc_last_status(plc_t plc);           /* last connection-level call (e.g. connect) */
const char  *plc_last_error (plc_t plc);
const char  *plc_strerror   (plc_status_t status);
```

## Growing outward

The same flat shape for the rest of the pylogix surface:

```c
/* many tags in one coalesced request -> one result per tag (caller frees each) */
plc_status_t plc_read_list(plc_t plc, const char *const *tags, plc_result_t *out, size_t count);

/* enumerate controller tags */
int plc_tag_count(plc_t plc);
int plc_tag_name (plc_t plc, int index, char *buf, size_t len);
int plc_tag_type (plc_t plc, int index, char *buf, size_t len);
```

And the growth the result object unlocks — these are where the blocking core
gains the background IO + job threads of the cursor layer:

```c
/* AUTO-UPDATE / SUBSCRIPTION: re-read into this result every interval_ms.
   The result mutates in place; read current values any time. 0 stops it. */
plc_status_t plc_result_set_update(plc_result_t r, int interval_ms);

/* ASYNC: non-blocking read -- the result starts PLC_PENDING. */
plc_result_t plc_read_async (plc_t plc, const char *tag);
plc_status_t plc_result_wait(plc_result_t r, int timeout_ms);   /* block until complete */

/* CALLBACK: fire when the result updates (async completion or subscription tick).
   Runs on the job thread; do not block. */
typedef void (*plc_result_cb_t)(plc_result_t r, void *user);
plc_status_t plc_result_on_update(plc_result_t r, plc_result_cb_t cb, void *user);
```

Live-monitor sketch — read once, then let it keep itself fresh and notify on change:

```c
plc_result_t r = plc_read(plc, "PumpSpeed");
plc_result_on_update(r, on_change, ctx);
plc_result_set_update(r, 100);     /* refresh every 100 ms, callback on each update */
/* ... run ... */
plc_result_set_update(r, 0);       /* stop */
plc_result_free(r);
```

## Mapping to pylogix

| pylogix | new_api6 |
|---|---|
| `comm = PLC('10.0.0.1')` | `plc_connect("10.0.0.1", 5000)` |
| `r = comm.Read('Tag')` | `plc_result_t r = plc_read(plc, "Tag");` |
| `r.Value` | `plc_result_get_int(r, 0)` (or `_f64`/`_bool`/…) |
| `r.Status` | `plc_result_status(r)` |
| `r.Value` (array) | `plc_result_count` + `plc_result_get_*` |
| `comm.Read('Arr[0]', 10)` | `plc_read_n(plc, "Arr", 10)` |
| `comm.Read(['a','b'])` | `plc_read_list(plc, names, out, 2)` |
| `comm.Write('Tag', 50)` | `plc_write_int(plc, "Tag", 50)` |
| (no equivalent) | `plc_result_set_*` + `plc_result_write` (edit + write back) |
| (hand-rolled loop) | `plc_result_set_update` + `plc_result_on_update` |
| `comm.GetTagList()` | `plc_tag_count` + `plc_tag_name` |

## Design notes

- **Owned result.** `plc_read*` returns a result you `plc_result_free`. That one
  extra line buys an object you can index, modify, write back, subscribe, and
  await — one type across all four modes.
- **The result remembers its origin** (connection + tag), which is what makes
  `plc_result_write`, `plc_result_set_update`, and re-reads possible without
  restating the tag.
- **Blocking core needs no threads; growth does.** Plain `plc_read`/`plc_write`
  are synchronous and thread-free. `set_update` / `read_async` / `on_update` pull
  in the background IO + job threads — the same machinery as new_api5. That is
  the deliberate seam between the two layers.
- **Per-result status.** Trust `plc_result_status`; a getter on an error or
  pending result returns `0 / 0.0 / false`.

## Open questions

- Should `plc_result_t` be literally the cursor handle of new_api5, so a program
  graduates from blocking reads to caching/introspection with the same object?
- `plc_read_list` ownership: N owned results (free each), or one container result
  with N children and a single free?
- Does `plc_result_write` re-read first to detect conflicts, or blindly push the
  edited buffer (last-writer-wins)?
- Threading: one thread per `plc_t`, or interleavable blocking calls? (Subscription
  callbacks already imply a background thread touching the result — locking TBD.)
