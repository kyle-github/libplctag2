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

# new_api5 — the value / metadata model

Renames **node → value** and separates the two notions of "type":

- **kind** — the closed category of a value: `BOOL, INT, FLOAT, STRING, STRUCT`
  (plus `UNKNOWN`). A small fixed set, cached in the value cursor bits.
- **type** — the concrete named type behind a metadata handle: its name
  (`"DINT"`, `"MyStruct"`), size, alignment, members, offsets, and dimensions.

Note what is *not* a kind: **raw bytes** (see below). Arrays *are* a kind.

## Two handle types

```c
typedef uint64_t plc_value_t;     /* a cursor into a device's value tree */
typedef uint64_t plc_metadata_t;  /* a type descriptor */

#define PLC_INVALID_VALUE    ((plc_value_t)0)
#define PLC_INVALID_METADATA ((plc_metadata_t)0)
```

Both are opaque pass-by-copy cursors — nothing to free. A `plc_value_t` is
instance-bound (a value plus an absolute location in the root tag buffer). A
`plc_metadata_t` describes a type.

**Sharing:** a UDT/struct *is* a type — its member layout is canonical and shared
by every instance. An **array is a kind but not a shared type**: it is a
dimensioned value. The PLC protocol itself returns an *element* type plus many
values. So array metadata carries the dimensions (per-value) plus an *element*
metadata (`plc_metadata_get_element`); the element's struct layout is shared, the
dimensions are not.

## kind: the cursor-bit category

```c
typedef enum plc_value_kind_t {
    PLC_KIND_UNKNOWN = 0,
    PLC_KIND_BOOL,
    PLC_KIND_INT,
    PLC_KIND_FLOAT,    /* REAL or LREAL; width via metadata size / f32 vs f64 accessor */
    PLC_KIND_STRING,
    PLC_KIND_ARRAY,
    PLC_KIND_STRUCT,
} plc_value_kind_t;

plc_value_kind_t plc_value_get_kind(plc_value_t v);  /* cheap: straight from the cursor bits */
```

`ARRAY` is a category just like `INT`: the bits tell you *that* it is an array,
not its element type or shape — exactly as `INT` tells you it is an integer but
not whether it is i8/i16/i32/i64. You learn the details only by querying metadata
(`get_element`, `num_dimensions`, `dimension_range`; `size`/`type_name` for an
int's width). A `DINT[10]` value has kind `ARRAY`; a `DINT` element of it has kind
`INT`. Seven kinds use 3 bits with headroom. `plc_value_get_kind` is the renamed
`plc_node_type` ("kind" now names exactly what the bits hold).

## Renames at a glance

| was                        | now                              |
|----------------------------|----------------------------------|
| `plc_node_t`               | `plc_value_t`                    |
| `plc_node_type_t` / `PLC_VAL_*` | `plc_value_kind_t` / `PLC_KIND_*` |
| `plc_node_type()`          | `plc_value_get_kind()`           |
| `plc_path_to_node()`       | `plc_path_to_value()`            |
| `plc_node_child[_count]()` | `plc_value_child[_count]()`      |
| `plc_node_get_bytes()`     | `plc_value_get_raw_bytes()`      |
| `plc_node_get_double()`    | `plc_value_get_f32/f64()`        |
| `plc_node_type_info/_name`, `plc_node_size` | *removed* (use metadata) |
| `PLC_KIND_BYTES`           | *removed* (raw byte view, not a kind) |

## Value API

```c
/* lifecycle -- the device root is a value (a struct; see "The root") */
plc_value_t  plc_open (const char *connect_str, int timeout_ms);
plc_status_t plc_close(plc_value_t dev);

/* resolve + navigate (instances) */
plc_value_t  plc_path_to_value (plc_value_t base, const char *path);
plc_value_t  plc_value_parent  (plc_value_t v);
plc_value_t  plc_value_child   (plc_value_t v, size_t i);
size_t       plc_value_child_count(plc_value_t v);

/* identity */
plc_value_kind_t plc_value_get_kind  (plc_value_t v);            /* cheap, cursor bits */
plc_status_t     plc_value_get_status(plc_value_t v);            /* PENDING until resolved */
plc_status_t     plc_value_get_name  (plc_value_t v, char *buf, size_t len);
plc_status_t     plc_value_get_path  (plc_value_t v, char *buf, size_t len);
plc_metadata_t   plc_value_metadata  (plc_value_t v);           /* bridge to the type world */

/* advanced: absolute location in the root tag buffer (escape hatch) */
uint32_t     plc_value_get_byte_offset(plc_value_t v);
uint32_t     plc_value_get_bit_offset (plc_value_t v);          /* bit within that byte, 0..7 */

/* read (serve from cache per read policy, else fetch/block) */
int64_t      plc_value_get_int(plc_value_t v);                  /* any int width, widened to i64 */
float        plc_value_get_f32(plc_value_t v);
double       plc_value_get_f64(plc_value_t v);
bool         plc_value_get_bool(plc_value_t v);
plc_status_t plc_value_get_string(plc_value_t v, char *buf, size_t len, size_t *actual);

/* RAW byte VIEW of this value's storage (scalar bytes, whole array, whole struct).
   This is a raw reinterpretation of the buffer, not an element type -- an array of
   int8/uint8 is an INT array, not "bytes". */
plc_status_t plc_value_get_raw_bytes(plc_value_t v, uint8_t *buf, size_t len, size_t *actual);

/* write (dirty + write-back per write policy / plc_flush) */
plc_status_t plc_value_set_int (plc_value_t v, int64_t x);
plc_status_t plc_value_set_f32 (plc_value_t v, float  x);
plc_status_t plc_value_set_f64 (plc_value_t v, double x);
plc_status_t plc_value_set_bool(plc_value_t v, bool   x);
plc_status_t plc_value_set_string(plc_value_t v, const char *s);
plc_status_t plc_value_set_raw_bytes(plc_value_t v, const uint8_t *b, size_t len);

/* manual IO */
plc_status_t plc_prefetch(plc_value_t scope, int timeout_ms);   /* one-shot pull */
plc_status_t plc_flush   (plc_value_t scope, int timeout_ms);   /* one-shot push */

/* policies (set once, inherited by subtree; nearest-ancestor-wins, root = default) */
plc_status_t plc_value_set_read_policy       (plc_value_t v, uint32_t retention_ms, bool auto_refresh);
plc_status_t plc_value_set_metadata_retention(plc_value_t v, uint32_t retention_ms, bool auto_refresh);
plc_status_t plc_value_set_write_policy      (plc_value_t v, uint32_t write_delay_ms);

/* per-value data + events (unchanged but renamed) */
void        *plc_value_get_user_data(plc_value_t v);
plc_status_t plc_value_set_user_data(plc_value_t v, void *user_data);
plc_status_t plc_value_set_event_mask(plc_value_t v, uint32_t mask);

typedef void (*plc_value_cb_t)(plc_value_t dev, plc_value_t v,
                               plc_event_type_t event, plc_op_t op,
                               plc_status_t status, int64_t timestamp_us, void *user_data);
plc_status_t plc_value_set_callback(plc_value_t v, plc_value_cb_t cb, void *user_data);
```

**Why `int` has one accessor but `float` has two:** every int width widens to
`i64` losslessly, so one `get_int` covers `SINT/INT/DINT/LINT`. Floats do not —
`f64 -> f32` truncates — so `f32` and `f64` are distinct (`PLC_KIND_FLOAT` plus
the metadata size tells you which to use; a read can always use `f64`).

## Metadata API

Metadata navigation mirrors value navigation. A member's name and offset are
properties of the member's own metadata — no parent-indexed getters. `child()` is
**struct members only**; an array's shape comes from the dimension calls and its
element type from `get_element` (no array "children").

```c
/* identity */
plc_value_kind_t plc_metadata_get_kind     (plc_metadata_t m);                 /* same enum as values */
plc_status_t     plc_metadata_get_type_name(plc_metadata_t m, char *buf, size_t len); /* "DINT","MyStruct"; for ARRAY, the element type name */
plc_status_t     plc_metadata_get_name     (plc_metadata_t m, char *buf, size_t len); /* member name; "" at top */
size_t           plc_metadata_get_size     (plc_metadata_t m);                 /* bytes (whole-array bytes for ARRAY) */
uint32_t         plc_metadata_get_offset   (plc_metadata_t m);                 /* BITS, relative to parent */

/* arrays: shape + element type (kind == PLC_KIND_ARRAY) */
uint32_t         plc_metadata_get_num_dimensions (plc_metadata_t m);           /* 0 if not array; UINT32_MAX unknown */
uint32_t         plc_metadata_get_dimension_range(plc_metadata_t m, uint32_t dim); /* 0-based; 0 if no such dim */
plc_metadata_t   plc_metadata_get_element        (plc_metadata_t m);           /* element type metadata */

/* navigate struct members */
size_t           plc_metadata_child_count(plc_metadata_t m); /* struct: members; else 0 */
plc_metadata_t   plc_metadata_child      (plc_metadata_t m, size_t i);
plc_metadata_t   plc_metadata_parent     (plc_metadata_t m);

/* optional sugar */
plc_metadata_t   plc_metadata_child_by_name(plc_metadata_t m, const char *name);
size_t           plc_metadata_get_alignment(plc_metadata_t m);
```

`get_size` is the bytes of the type the metadata describes — a scalar's width, a
struct's size, or an ARRAY's whole-array bytes (one element is
`plc_metadata_get_size(plc_metadata_get_element(m))`). Offsets split by where they
are meaningful: `plc_metadata_get_offset` is type-relative (a member within its
struct); `plc_value_get_byte_offset/bit_offset` is the absolute location of a
concrete element. Relative offsets compose down a path; the library computes the
absolute.

## The root

`plc_open` returns the device root **value**, and it behaves like a struct: you
can take its metadata and navigate its children. The root carries more than type
counts — identity, status, and telemetry are ordinary readable values:

```
<root struct, name = connect URL>
    identity   struct  -- vendor_id, device_type, product_code, revision,
                          serial_number, product_name
    status     struct  -- connection state, last error
    telemetry  struct  -- packet counts, retries, round-trip times, ...
    tags       struct  -- the controller tags, by name
```

So identity is just a value read:

```c
int64_t vendor = plc_value_get_int(plc_path_to_value(dev, "identity.vendor_id"));
char product[128];
plc_value_get_string(plc_path_to_value(dev, "identity.product_name"), product, sizeof product, NULL);
```

(Enumerating the *type catalog* — UDT definitions independent of any tag — is a
metadata concern; see open questions.)

## Worked example

`MyStruct { i32 a; REAL b; STRING c[10]; MyOtherStruct d; }`,
`MyOtherStruct { i16 x; bool y[15]; }`, tag `Tank` of type `MyStruct`.

```c
/* "[10]" for an ARRAY, "" otherwise */
static void dims_str(plc_metadata_t m, char *out, size_t n) {
    out[0] = '\0';
    if (plc_metadata_get_kind(m) != PLC_KIND_ARRAY) { return; }
    uint32_t nd = plc_metadata_get_num_dimensions(m);
    if (nd == UINT32_MAX) { snprintf(out, n, "[?]"); return; }
    for (uint32_t d = 0; d < nd; d++) {
        char b[16]; snprintf(b, sizeof b, "[%u]", plc_metadata_get_dimension_range(m, d));
        strncat(out, b, n - strlen(out) - 1);
    }
}

static void describe(plc_metadata_t m, int depth);   /* lists a struct's members */

static void describe_member(plc_metadata_t mem, int depth) {
    char name[256] = {0}, type[256] = {0}, dims[64];
    plc_metadata_get_name(mem, name, sizeof name);
    plc_metadata_get_type_name(mem, type, sizeof type);  /* element type name when ARRAY */
    dims_str(mem, dims, sizeof dims);
    uint32_t off = plc_metadata_get_offset(mem);         /* bits, relative to the struct */

    printf("%*s%-6s %s%s @ bit %u (byte %u", depth * 2, "", name, type, dims, off, off / 8);
    if (off % 8) { printf(".%u", off % 8); }             /* packed BOOL */
    printf(")\n");

    /* drill into a struct member, or a struct *element* of an array member */
    plc_value_kind_t k = plc_metadata_get_kind(mem);
    if (k == PLC_KIND_STRUCT) {
        describe(mem, depth + 1);
    } else if (k == PLC_KIND_ARRAY &&
               plc_metadata_get_kind(plc_metadata_get_element(mem)) == PLC_KIND_STRUCT) {
        describe(plc_metadata_get_element(mem), depth + 1);
    }
}

static void describe(plc_metadata_t m, int depth) {     /* m is a struct type */
    size_t mc = plc_metadata_child_count(m);
    for (size_t i = 0; i < mc; i++) { describe_member(plc_metadata_child(m, i), depth); }
}

int main(void) {
    plc_value_t dev  = plc_open("enip-tcp://10.1.2.3/1/5", 5000);
    plc_value_t tank = plc_path_to_value(dev, "tags.Tank");        /* MyStruct */
    if (await_resolved(tank, 2000) != PLC_STATUS_OK) { return 1; }

    plc_metadata_t m = plc_value_metadata(tank);
    char tn[256] = {0}; plc_metadata_get_type_name(m, tn, sizeof tn);
    printf("%s:\n", tn);
    describe(m, 1);

    /* a concrete packed BOOL value + the advanced raw offset */
    plc_value_t y7 = plc_path_to_value(dev, "tags.Tank.d.y[7]");
    printf("Tank.d.y[7] = %s  @ byte %u bit %u\n",
           plc_value_get_bool(y7) ? "true" : "false",
           plc_value_get_byte_offset(y7), plc_value_get_bit_offset(y7));

    plc_close(dev);
    return 0;
}
```

Output shape (`c` is a 1-D array of STRING, `y` a 1-D array of BOOL):

```
MyStruct:
  a      DINT @ bit 0 (byte 0)
  b      REAL @ bit 32 (byte 4)
  c      STRING[10] @ bit 64 (byte 8)
  d      MyOtherStruct @ bit 736 (byte 92)
    x    INT @ bit 0 (byte 0)
    y    BOOL[15] @ bit 16 (byte 2)
Tank.d.y[7] = false  @ byte <abs> bit 7
```

## Simplifications / orthogonality

1. **kind vs type, named.** Category is `kind`; specific named type is
   `type_name`. `plc_node_type` (a category) becomes `plc_value_get_kind`.
2. **Array is a kind, but not a shared type.** `PLC_KIND_ARRAY` is a category
   like `INT`: the bits say it is an array; the element type and shape come from
   metadata (`get_element`, `num_dimensions`, `dimension_range`) just as an int's
   width does. No array "children" — `child()` is struct members. Struct layout
   stays shared; array shape stays per-value. Matches the PLC protocol (element
   type + N values).
3. **Raw bytes is a view, not a kind.** `plc_value_get_raw_bytes` reinterprets a
   value's storage. `PLC_KIND_BYTES` is gone; `int8/uint8` arrays are INT arrays.
4. **Metadata navigation mirrors value navigation** (`child`/`child_count`/
   `name`/`parent`); member name and offset live on the member metadata.
5. **Offsets sit where they mean something** — relative on metadata, absolute on
   the value.
6. **The root is just a struct value**, so identity / status / telemetry are
   plain value reads — no special device-info API.
7. **`f32`/`f64` name the widths** (vs vague `float`/`double`); `int` stays single
   because integer widening is lossless.

## Open questions

- Multi-dimensional ordering: confirm `dimension_range(0)` is the outermost
  dimension and row-major element layout.
- `num_dimensions == UINT32_MAX` (unknown): when does this occur — only before
  metadata resolves, or also for protocols that don't report shape up front?
- Type-catalog enumeration (UDT defs with no instance): expose a
  `plc_metadata_type_by_name(dev, name)` / iterator, or reach them only through a
  value of that type?
