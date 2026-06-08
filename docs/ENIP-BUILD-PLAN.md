# EtherNet/IP Build Plan — First Implementation

**Date:** 2026-06-07
**Status:** Plan. Maps the first concrete code for libplctag2 from the public API
(`src/include/plctag.h`) down to the wire, starting with generic EtherNet/IP and a
Rockwell vendor module for tag enumeration.

This document lists the files to create, the functions in each (signatures +
descriptions), the connection sequence, and the path-resolution flow. It does not
contain implementation code.

---

## 1. Scope and milestones

**In scope (first implementation):**

- Generic EtherNet/IP transport: TCP connect, session registration, identity,
  Forward Open (Extended with fallback to legacy), unconnected and connected CIP
  messaging.
- A **vendor abstraction** so manufacturer-specific behavior (Rockwell tag
  enumeration / UDT decoding, OMRON later) lives behind vtables.
- Metadata cache: root tag list (phase 1), lazily fetched UDT/template definitions.
- Path parsing and descent that turns a tag path into a typed, sized, addressable
  node.

**Milestones (each independently testable):**

| # | Milestone | Outcome |
|---|-----------|---------|
| M0 | Core scaffolding | Handles, device, driver vtable, attr parsing, API wiring (stubs return `PLC_STATUS_ERR_NOT_SUPPORTED`). |
| M1 | Session + identity | TCP connect, register session, read Identity object. |
| M2 | Connection open | Forward Open Extended, fallback to legacy Forward Open; Forward Close on teardown. |
| M3 | Metadata phase 1 | Rockwell vendor enumerates root tag names + IDs + type codes into the cache. |
| M4 | Path → type/size | `plc_get_type` / `plc_get_child_count` resolve a path by descending cached metadata, fetching UDTs on demand. |
| M5 (later) | Read/write | Value transfer for resolved nodes; value conversions; bit RMW. |
| M6 (later) | Async + threading | Background per-connection I/O thread, staged ops, `plc_poll_events`. |

This plan details **M0–M4**. M5/M6 are sketched at the end so the interfaces
designed now leave room for them.

---

## 2. Architectural rules

1. **Two vtable layers, no PLC-type branching.** There must be **no
   `if (plc_is_rockwell)` / `switch (vendor)`** anywhere in the call paths.
   Polymorphism is expressed only through function-pointer tables:
   - `plc_driver_vtable_t` — protocol/transport (EtherNet/IP now, Modbus later),
     selected once by the connection-string scheme.
   - `eip_vendor_t` — manufacturer specifics within EtherNet/IP (generic, Rockwell,
     OMRON), selected once after identity by a registry probe.
2. **Selection happens once, at the edges.** A registry maps a key (scheme, or
   identity) to a vtable pointer that is then stored on the device/session.
   Everything downstream calls through the pointer.
3. **Generic first.** The generic EtherNet/IP path (encapsulation, session,
   Forward Open, explicit CIP) is vendor-neutral. Tag *semantics* (enumeration,
   symbolic read/write, native type decoding) are vendor responsibilities. The
   generic vendor returns `PLC_STATUS_ERR_NOT_SUPPORTED` for enumeration; the
   Rockwell vendor implements it.
4. **Build wire buffers with `Bytes`/`Arena`.** All encode/decode uses
   `src/utils/bytes.h` + `src/utils/arena.h`. A per-request scratch arena is reset
   between operations.
5. **Log through `pdebug`** with the module enum already defined in
   `src/utils/debug.h` (`DEBUG_MODULE_ENIP`, `_CIP`, `_TAG`, `_PATH`, `_SOCKET`,
   `_PROTOCOL`).

---

## 3. Layering

```
            public API  (src/include/plctag.h)
                 │
   api/api.c     │  validate args, resolve handle, sync/async dispatch
                 ▼
   core/handle   handle ↔ plc_device_t (generational, ABA-safe, refcounted)
                 │
   core/device   plc_device_t  ── driver vtable ─┐
                 │                                │
   core/path     parse path string               │
   core/metadata cache: tags, UDTs, type tree    │
   core/value    internal tagged value           │
                 ▼                                ▼
   protocol/driver.h            plc_driver_vtable_t  (scheme → driver)
                 │
   protocol/eip/eip_driver.c    implements the vtable for "eip"
                 │   owns connection sequence + vendor selection
                 ▼
   protocol/eip/{eip,cip,identity,connection}   generic EtherNet/IP + CIP
                 │
   protocol/eip/vendor/*        eip_vendor_t  (identity → vendor)
                 │
   utils/platform               TCP socket, mutex, monotonic time
```

---

## 4. Directory layout (new files)

```
src/utils/
  platform.h / platform.c        sockets, mutex, time (M0)

src/lib/
  api/
    api.c                        public API entry points (M0, extended each phase)
  core/
    attr.h / attr.c              connection-string / attribute parsing (M0)
    handle.h / handle.c          generational handle table + refcount (M0)
    device.h / device.c          plc_device_t lifecycle (M0)
    value.h / value.c            internal value type + conversions (M0/M5)
    path.h / path.c              tag-path parser (M4)
    metadata.h / metadata.c      tag/UDT cache + descent helpers (M3/M4)
  protocol/
    driver.h                     plc_driver_vtable_t (M0)
    driver_registry.h / .c       scheme → driver (M0)
    eip/
      eip.h / eip.c              encapsulation, session, send_rr/unit_data (M1)
      cip.h / cip.c              CIP request build / reply parse, EPATH (M1)
      identity.h / identity.c    Identity object read (M1)
      connection.h / connection.c  Forward Open (ext + legacy) / Forward Close (M2)
      eip_driver.h / eip_driver.c   driver vtable impl + connect orchestration + resolve (M1–M4)
      vendor/
        vendor.h                 eip_vendor_t vtable + selection API (M3)
        vendor_registry.c        identity → vendor probe (M3)
        vendor_generic.c         generic EIP vendor (M3)
        vendor_rockwell.h / .c    Rockwell tag list + template/UDT + type map (M3/M4)
        vendor_omron.c           OMRON stub, registered but minimal (later)
```

`src/lib/CMakeLists.txt` already globs `*.c` recursively, so new files under
`src/lib/` are picked up automatically. `src/utils/CMakeLists.txt` lists files
explicitly — add `platform.c` there.

---

## 5. Core scaffolding (M0)

### 5.1 `src/utils/platform.h` / `.c`

Cross-platform TCP + sync primitives. Blocking I/O for now; the wake/event
construct for async (M6) is noted but not built yet.

```c
#if defined(_WIN32) || defined(_WIN64)
typedef SOCKET socket_t;
#define PLC_INVALID_SOCKET INVALID_SOCKET
#else
typedef int socket_t;
#define PLC_INVALID_SOCKET (-1)
#endif

/* --- TCP --- */
/* Open a TCP socket (not yet connected). Returns the socket in *out. */
plc_status_t socket_tcp_open(socket_t *out);

/* Resolve host and connect, honoring timeout_ms (0 disables the connect timeout). */
plc_status_t socket_tcp_connect(socket_t s, const char *host, uint16_t port, int timeout_ms);

/* Write the whole buffer or fail; loops over partial writes. */
plc_status_t socket_write_all(socket_t s, const uint8_t *buf, size_t len, int timeout_ms);

/* Read exactly len bytes into buf (used for fixed-size headers); *got <= len on timeout/EOF. */
plc_status_t socket_read_n(socket_t s, uint8_t *buf, size_t len, size_t *got, int timeout_ms);

/* Close and invalidate. */
void socket_close(socket_t s);

/* One-time process init/teardown (WSAStartup on Windows; no-op elsewhere). */
plc_status_t socket_lib_init(void);
void socket_lib_term(void);

/* --- Mutex --- */
typedef struct plc_mutex_t plc_mutex_t;   /* opaque; SRWLOCK / pthread_mutex inside */
plc_status_t plc_mutex_init(plc_mutex_t *m);
void plc_mutex_lock(plc_mutex_t *m);
void plc_mutex_unlock(plc_mutex_t *m);
void plc_mutex_destroy(plc_mutex_t *m);

/* --- Time --- */
int64_t time_ms(void);   /* monotonic milliseconds for timeouts */
```

### 5.2 `core/attr.h` / `.c`

Parse the `plc_open` connection string into typed attributes. Accept the
libplctag-style `key=value&key=value` form, e.g.
`protocol=eip&gateway=10.0.0.10&path=1,0&plc=controllogix`.

```c
typedef struct plc_attr_t plc_attr_t;   /* opaque key/value bag, arena-backed */

/* Parse a connection string. Returns NULL on syntax error (sets *status). */
plc_attr_t *plc_attr_parse(const char *connect_str, plc_status_t *status);

/* Typed getters with defaults. */
const char *plc_attr_get_str(const plc_attr_t *a, const char *key, const char *def);
int         plc_attr_get_int(const plc_attr_t *a, const char *key, int def);

void plc_attr_free(plc_attr_t *a);
```

Keys consumed at this stage: `protocol`/`scheme` (driver selection), `gateway`
(host), `port` (default 44818), `path` (CIP route segments to the CPU).

### 5.3 `core/handle.h` / `.c`

Generational handle table. `plc_dev_handle_t` packs an index + generation so a
stale handle never resolves to a reused slot (ABA-safe). Resolution takes a
reference; release drops it; close marks dead and frees when the last reference
goes.

```c
typedef struct plc_device_t plc_device_t;

plc_status_t      handle_table_init(void);
void              handle_table_term(void);

/* Insert dev, return a fresh handle (index+generation). */
plc_dev_handle_t  handle_alloc(plc_device_t *dev);

/* Resolve + take a reference. Returns NULL if stale/dead. Pair with handle_release(). */
plc_device_t     *handle_acquire(plc_dev_handle_t h);
void              handle_release(plc_device_t *dev);

/* Mark dead (no new acquires); device is freed when refcount hits zero. */
plc_status_t      handle_close(plc_dev_handle_t h);
```

### 5.4 `core/device.h` / `.c`

```c
struct plc_device_t {
    plc_dev_handle_t          handle;
    const plc_driver_vtable_t *driver;     /* protocol vtable */
    void                      *driver_state; /* e.g. eip_session_t* */
    plc_mutex_t                lock;        /* serializes API ops (api_mutex pattern) */
    int                        refcount;
    plc_status_t               last_status;
    char                       last_error[256];
    Arena                      scratch;     /* per-request scratch, reset each op */
};

/* Allocate + zero a device, init lock and scratch arena. */
plc_status_t plc_device_create(plc_device_t **out);

/* Driver close (if any), free metadata, destroy lock/arena, free device. */
void plc_device_destroy(plc_device_t *dev);

/* Record a formatted last-error string for plc_get_last_error. */
void plc_device_set_error(plc_device_t *dev, plc_status_t status, const char *fmt, ...);
```

### 5.5 `core/value.h` / `.c`

```c
typedef struct {
    plc_value_type_t type;
    union {
        int64_t  i;
        double   d;
        bool     b;
        Bytes    bytes;   /* also backs strings */
    } as;
} plc_value_t;

/* Range-check + convert app input into the native width implied by node type. */
plc_status_t value_from_int(plc_value_t *v, int64_t in, plc_value_type_t target, size_t width);
plc_status_t value_from_double(plc_value_t *v, double in, plc_value_type_t target);
/* Sentinels for the read accessors (INT64_MIN / NAN / false / NULL). */
int64_t value_int_sentinel(void);
double  value_double_sentinel(void);
```

### 5.6 `protocol/driver.h`

The protocol vtable. Implemented per scheme.

```c
typedef struct plc_driver_vtable_t {
    const char *scheme;   /* "eip" */

    /* Establish the connection per the attributes (runs the full connect sequence). */
    plc_status_t (*open)(plc_device_t *dev, const plc_attr_t *attr, int timeout_ms);

    /* Tear down (Forward Close, unregister session, close socket). */
    void (*close)(plc_device_t *dev);

    /* Resolve a parsed path (+ trailing index) to a typed, sized node. May fetch
       metadata on demand. */
    plc_status_t (*resolve)(plc_device_t *dev, const plc_path_t *path, int index,
                            plc_node_t *out_node, int timeout_ms);

    /* M5: transfer a resolved node. */
    plc_status_t (*read)(plc_device_t *dev, const plc_node_t *node, plc_value_t *out, int timeout_ms);
    plc_status_t (*write)(plc_device_t *dev, const plc_node_t *node, const plc_value_t *val, int timeout_ms);
} plc_driver_vtable_t;
```

### 5.7 `protocol/driver_registry.h` / `.c`

```c
/* Look up a driver by scheme. Returns NULL if unknown. No branching in callers:
   the returned pointer carries all behavior. */
const plc_driver_vtable_t *plc_driver_for_scheme(const char *scheme);
```

The registry is a static array of `&eip_driver_vtable` (and later
`&modbus_driver_vtable`). `protocol/eip/eip_driver.c` exports
`const plc_driver_vtable_t eip_driver_vtable`.

### 5.8 `api/api.c`

Each public function: validate args → `handle_acquire` → lock device → reset
scratch → delegate to driver vtable → unlock → `handle_release`. M0 wires
`plc_open`/`plc_close` to the driver registry and leaves accessors returning
sentinels until their phase lands.

```c
plc_dev_handle_t plc_open(const char *connect_str, int timeout_ms);
plc_status_t     plc_close(plc_dev_handle_t dev);
plc_status_t     plc_get_type(plc_dev_handle_t dev, const char *path, int index, int timeout_ms, plc_value_type_t *out);
plc_status_t     plc_get_child_count(plc_dev_handle_t dev, const char *path, int timeout_ms, int *out_count);
/* ... remaining accessors delegate to driver->read/->write in M5 ... */
```

`plc_open` flow: `plc_attr_parse` → `plc_driver_for_scheme(scheme)` →
`plc_device_create` → `driver->open(dev, attr, timeout)` → `handle_alloc`.

---

## 6. Generic EtherNet/IP (M1)

### 6.1 `protocol/eip/eip.h` / `.c`

Encapsulation layer and session. The 24-byte encapsulation header and CPF
(Common Packet Format) framing live here.

```c
typedef struct eip_session_t eip_session_t;
struct eip_session_t {
    socket_t            sock;
    uint32_t            session_handle;   /* from RegisterSession */
    uint64_t            sender_context;   /* incremented per request */
    char                host[128];
    uint16_t            port;
    uint8_t             cip_route[64];    /* parsed "path" attr, appended to Forward Open */
    size_t              cip_route_len;
    eip_identity_t      identity;
    const eip_vendor_t *vendor;           /* selected after identity (M3) */
    bool                conn_open;        /* Forward Open succeeded */
    uint32_t            o2t_conn_id;      /* originator→target connection id */
    uint32_t            t2o_conn_id;
    uint16_t            conn_serial;
    uint16_t            conn_seq;         /* connected sequence count */
    metadata_cache_t   *metadata;         /* M3 */
    Arena               io_arena;         /* request/response scratch */
};

plc_status_t eip_session_create(eip_session_t **out, const char *host, uint16_t port);
void         eip_session_destroy(eip_session_t *s);

/* TCP connect. */
plc_status_t eip_connect(eip_session_t *s, int timeout_ms);

/* RegisterSession (cmd 0x0065); stores session_handle. */
plc_status_t eip_register_session(eip_session_t *s, int timeout_ms);

/* UnRegisterSession (cmd 0x0066); best-effort. */
plc_status_t eip_unregister_session(eip_session_t *s);

/* Unconnected explicit messaging: wrap cip_req in SendRRData (cmd 0x006F) +
   CPF (null address + unconnected data items); return the CIP reply payload. */
plc_status_t eip_send_rr_data(eip_session_t *s, Bytes cip_req, Arena *a, Bytes *cip_resp, int timeout_ms);

/* Connected messaging: wrap cip_req in SendUnitData (cmd 0x0070) + CPF
   (connected address item with t2o_conn_id + sequenced data item). M2+. */
plc_status_t eip_send_unit_data(eip_session_t *s, Bytes cip_req, Arena *a, Bytes *cip_resp, int timeout_ms);
```

Static helpers: `encode_encap_header`, `parse_encap_header`,
`send_encap` (write header+payload, read reply header, read reply body),
`encode_cpf_unconnected`, `parse_cpf`.

### 6.2 `protocol/eip/cip.h` / `.c`

CIP message construction and reply parsing, independent of vendor.

```c
typedef struct {
    uint8_t  service;       /* reply service (request service | 0x80) */
    uint8_t  status;        /* general status (0 = success) */
    uint16_t ext_status;    /* first additional status word, if any */
    Bytes    data;          /* response payload after the status block */
} cip_reply_t;

/* Build a CIP request: service byte, request path (EPATH), request data. */
Bytes cip_build_request(Arena *a, uint8_t service, Bytes epath, Bytes data);

/* Parse a CIP reply, splitting status from payload. */
plc_status_t cip_parse_reply(Bytes resp, cip_reply_t *out);

/* Map a CIP general status to plc_status_t (0 → OK, else an ERR_*). */
plc_status_t cip_status_to_plc(uint8_t cip_status, uint16_t ext_status);

/* --- EPATH builders --- */
/* Logical segment class/instance, e.g. Identity = class 0x01, instance 1. */
Bytes cip_epath_class_inst(Arena *a, uint16_t class_id, uint32_t instance_id);
/* Logical class/instance/attribute. */
Bytes cip_epath_class_inst_attr(Arena *a, uint16_t class_id, uint32_t instance_id, uint16_t attr_id);
/* ANSI symbolic segment for a tag name ("MyTag"); used by symbolic read/write. */
Bytes cip_epath_symbolic(Arena *a, const char *name);
```

Common service codes referenced (named constants in `cip.h`):
`CIP_GET_ATTR_ALL 0x01`, `CIP_GET_ATTR_SINGLE 0x0E`,
`CIP_GET_ATTR_LIST 0x03`, `CIP_GET_INSTANCE_ATTR_LIST 0x55`,
`CIP_READ_TAG 0x4C`, `CIP_WRITE_TAG 0x4D`, `CIP_READ_TEMPLATE 0x4C`(template class),
`CIP_FWD_OPEN 0x54`, `CIP_FWD_OPEN_EX 0x5B`, `CIP_FWD_CLOSE 0x4E`.

### 6.3 `protocol/eip/identity.h` / `.c`

```c
typedef struct {
    uint16_t vendor_id;
    uint16_t device_type;
    uint16_t product_code;
    uint16_t revision;       /* major<<8 | minor */
    uint16_t status_word;
    uint32_t serial_number;
    char     product_name[64];
} eip_identity_t;

/* Get_Attributes_All on the Identity object (class 0x01, instance 1) via
   eip_send_rr_data; parse into *out. */
plc_status_t eip_get_identity(eip_session_t *s, eip_identity_t *out, int timeout_ms);
```

---

## 7. Connection open (M2)

### 7.1 `protocol/eip/connection.h` / `.c`

Forward Open through the Connection Manager (class 0x06, instance 1). Try the
Extended (Large) variant first; on a CIP error indicating the service/size is
unsupported, fall back to legacy Forward Open. The fallback is **not** a PLC-type
check — it is driven by the reply status of the attempt.

```c
typedef struct {
    uint32_t o2t_rpi_us;     /* requested packet interval, microseconds */
    uint32_t t2o_rpi_us;
    uint16_t conn_size;      /* connected payload size (4002 large, 504 legacy) */
    uint8_t  priority;
    uint8_t  timeout_ticks;
} eip_conn_params_t;

/* Issue one Forward Open variant. extended=true → 0x5B/Large, false → 0x54.
   On success, fills s->o2t_conn_id / t2o_conn_id / conn_serial and sets conn_open. */
plc_status_t eip_forward_open(eip_session_t *s, bool extended, const eip_conn_params_t *p, int timeout_ms);

/* Try extended; if it fails with an unsupported/too-large status, retry legacy.
   Returns OK if either variant connects. */
plc_status_t eip_open_connection(eip_session_t *s, int timeout_ms);

/* Forward Close (0x4E) if a connection is open; best-effort on teardown. */
plc_status_t eip_forward_close(eip_session_t *s, int timeout_ms);
```

The connection path data is the Connection Manager EPATH followed by the CPU
route (`s->cip_route`) ending at the Message Router (class 0x02, instance 1).

---

## 8. Vendor abstraction + metadata phase 1 (M3)

### 8.1 `protocol/eip/vendor/vendor.h`

The manufacturer vtable. This is the **only** place PLC-family behavior diverges.

```c
typedef struct eip_vendor_t {
    const char *name;   /* "generic", "rockwell", "omron" */

    /* Registry probe: does this vendor handle the given identity? */
    bool (*matches)(const eip_identity_t *id);

    /* Metadata phase 1: enumerate root tags into s->metadata.
       Generic returns PLC_STATUS_ERR_NOT_SUPPORTED. */
    plc_status_t (*list_tags)(eip_session_t *s, int timeout_ms);

    /* On-demand: load a UDT/template definition (members, offsets, size) into
       the cache, keyed by udt_id. Generic returns NOT_SUPPORTED. */
    plc_status_t (*fetch_udt)(eip_session_t *s, uint16_t udt_id, int timeout_ms);

    /* Map a native CIP type code to the generic vocabulary + element width.
       For structs, sets *out_type = PLC_VAL_STRUCT and *out_udt_id. */
    plc_status_t (*map_type)(uint16_t cip_type, plc_value_type_t *out_type,
                             size_t *out_size, uint16_t *out_udt_id);

    /* M5: build the CIP request to read/write a resolved node, and decode the
       reply payload into a value. */
    plc_status_t (*encode_read)(eip_session_t *s, const plc_node_t *n, Arena *a, Bytes *out_req);
    plc_status_t (*encode_write)(eip_session_t *s, const plc_node_t *n, const plc_value_t *v, Arena *a, Bytes *out_req);
    plc_status_t (*decode_value)(eip_session_t *s, const plc_node_t *n, Bytes resp, plc_value_t *out);
} eip_vendor_t;

/* Probe the registry (calls each vendor's matches() in priority order; generic
   matches last as the fallback). Never returns NULL. */
const eip_vendor_t *eip_vendor_select(const eip_identity_t *id);
```

### 8.2 `protocol/eip/vendor/vendor_registry.c`

```c
/* Ordered list: { &rockwell_vendor, &omron_vendor, &generic_vendor }.
   eip_vendor_select walks it and returns the first whose matches() is true.
   generic_vendor.matches() always returns true. */
```

### 8.3 `protocol/eip/vendor/vendor_generic.c`

Exports `const eip_vendor_t generic_vendor`. `matches` → true. `list_tags` /
`fetch_udt` → `PLC_STATUS_ERR_NOT_SUPPORTED`. `map_type` → the standard CIP
elementary types (`0x00C1 BOOL`, `0x00C2 SINT`, `0x00C3 INT`, `0x00C4 DINT`,
`0x00C5 LINT`, `0x00CA REAL`, `0x00CB LREAL`, `0x00D0..` strings, etc.).
M5 encode/decode use symbolic Read/Write Tag service for explicitly named tags.

### 8.4 `protocol/eip/vendor/vendor_rockwell.h` / `.c`

Exports `const eip_vendor_t rockwell_vendor`. `matches` → identity vendor_id == 1
(Rockwell/Allen-Bradley). Implements:

```c
/* Symbol object (class 0x6B), Get_Instance_Attribute_List (0x55), requesting
   attr 1 (name) and 2 (type). Iterates instances in chunks, resuming from the
   last returned instance id until the reply status is not "partial". Adds an
   md_tag_t per non-system tag to s->metadata. */
static plc_status_t rockwell_list_tags(eip_session_t *s, int timeout_ms);

/* Template object (class 0x6C). 1) Get_Attribute_List for the template's
   definition size + member count + structure handle. 2) Read Template (0x4C)
   to pull the member array + member-name string blob. Parse into an md_udt_t
   (fields with name/type/offset/dims) and add to the cache. Recurses via
   fetch_udt for nested struct members. */
static plc_status_t rockwell_fetch_udt(eip_session_t *s, uint16_t udt_id, int timeout_ms);

/* Rockwell type-code conventions: bit 0x8000 set → struct (low bits = template
   id); bits 0x6000 → array dimension count; low byte → elementary type. */
static plc_status_t rockwell_map_type(uint16_t cip_type, plc_value_type_t *out_type,
                                      size_t *out_size, uint16_t *out_udt_id);
```

Plus static parse helpers: `parse_symbol_entry`, `parse_template_attrs`,
`parse_template_members`, `decode_member_name`.

---

## 9. Metadata cache + path resolution (M3/M4)

### 9.1 `core/metadata.h` / `.c`

```c
typedef struct {
    char             name[128];
    uint32_t         instance_id;   /* symbol instance id */
    uint16_t         cip_type;      /* native type code */
    plc_value_type_t type;          /* mapped generic type */
    uint16_t         udt_id;        /* nonzero if struct */
    int32_t          dims[3];
    uint8_t          dim_count;
    size_t           elem_size;     /* bytes per element */
} md_tag_t;

typedef struct {
    char             name[128];
    uint16_t         cip_type;
    plc_value_type_t type;
    uint16_t         udt_id;        /* nonzero if member is a struct */
    size_t           offset;        /* byte offset within the structure */
    int32_t          dims[3];
    uint8_t          dim_count;
    size_t           elem_size;
} md_field_t;

typedef struct {
    uint16_t    udt_id;
    char        name[128];
    size_t      size;               /* instance size in bytes */
    md_field_t *fields;
    size_t      field_count;
} md_udt_t;

typedef struct metadata_cache_t metadata_cache_t;

plc_status_t  md_cache_create(metadata_cache_t **out);
void          md_cache_destroy(metadata_cache_t *c);

md_tag_t     *md_cache_add_tag(metadata_cache_t *c);          /* returns a slot to fill */
md_tag_t     *md_cache_find_tag(metadata_cache_t *c, const char *name);
size_t        md_cache_tag_count(metadata_cache_t *c);
md_tag_t     *md_cache_tag_at(metadata_cache_t *c, size_t i); /* for root enumeration */

md_udt_t     *md_cache_add_udt(metadata_cache_t *c, uint16_t udt_id);
md_udt_t     *md_cache_find_udt(metadata_cache_t *c, uint16_t udt_id);
```

Backing storage: dynamic arrays (or a small hashtable for tag names) owned by the
cache; the cache lives for the device lifetime and is freed in `eip_session_destroy`.

### 9.2 `core/path.h` / `.c`

Parse the path string into segments. The trailing `int index` from the API is
applied to the final node during descent (see §9.3), not stored here.

```c
typedef struct {
    char     name[128];     /* identifier segment ("" for a pure index segment) */
    int32_t  indices[3];    /* array subscripts present on this segment */
    uint8_t  index_count;
    bool     is_bit;        /* trailing ".N" bit selector */
    uint32_t bit;
} plc_path_seg_t;

typedef struct {
    plc_path_seg_t segs[16];
    size_t         seg_count;
} plc_path_t;

/* Parse "my_array[42].field1.4" into segments. Empty string → zero segments
   (the device root). Returns PLC_STATUS_ERR_BAD_PATH on malformed input. */
plc_status_t plc_path_parse(const char *path, plc_path_t *out);
```

### 9.3 Resolution — `plc_node_t` and `eip_resolve`

The resolved, addressable node returned by `driver->resolve`:

```c
typedef struct {
    plc_value_type_t type;        /* PLC_VAL_* */
    uint16_t         cip_type;    /* native type code */
    size_t           elem_size;   /* bytes per element */
    size_t           total_size;  /* bytes of the whole node */

    bool             is_array;
    int32_t          dims[3];
    uint8_t          dim_count;

    /* addressing for read/write (M5) */
    const char      *symbolic;    /* full symbolic path, arena-owned */
    uint32_t         instance_id; /* root symbol instance id */
    size_t           byte_offset; /* offset within the root structure */
    bool             is_bit;
    uint32_t         bit;
} plc_node_t;
```

`eip_driver.c` implements:

```c
/* driver->resolve. Parse already done by caller (api passes plc_path_t).
   1. If path is empty → node describes the device root (type STRUCT, child
      count = root tag count).
   2. Look up segs[0].name in the metadata cache (md_cache_find_tag). If absent
      and the vendor enumerates, it is an invalid path → PLC_STATUS_ERR_BAD_PATH.
   3. Seed a cursor from the root tag (type, cip_type, udt_id, dims, elem_size,
      symbolic = tag name, instance_id, byte_offset = 0).
   4. Apply segs[0] array subscripts to the cursor (reduce dims, advance offset
      for the addressing model used at read time).
   5. For each remaining segment:
        - cursor must be a struct → ensure its UDT is loaded
          (md_cache_find_udt; else vendor->fetch_udt), look up the field by name,
          advance byte_offset += field.offset, adopt field type/dims, extend the
          symbolic path with ".field".
        - apply that segment's array subscripts.
        - a trailing bit selector sets is_bit/bit and requires an integer cursor.
   6. Apply the API trailing `index` as a subscript/child selector of the final
      node (array element, or—at the root—the index-th tag).
   7. Emit plc_node_t (type, sizes, addressing). */
static plc_status_t eip_resolve(plc_device_t *dev, const plc_path_t *path, int index,
                                plc_node_t *out_node, int timeout_ms);
```

Helper statics in `eip_driver.c` (or `metadata.c`): `md_ensure_udt`,
`md_descend_field`, `md_apply_index`, `md_node_child_count`.

`plc_get_type` returns `out_node.type`; `plc_get_child_count` returns the
outermost array dimension, or the struct field count, or 0 for scalars (via
`md_node_child_count`).

---

## 10. Connection sequence → functions

`eip_driver_open(dev, attr, timeout)` orchestrates, all through vtables/registries:

```
1. socket_lib_init()                              (once, idempotent)
2. host/port/route ← plc_attr_get_* ; parse "path" into cip_route
3. eip_session_create(&s, host, port)
4. eip_connect(s, t)                              ── TCP connect
5. eip_register_session(s, t)                     ── EIP session
6. eip_get_identity(s, &s->identity, t)           ── identify the PLC
7. s->vendor = eip_vendor_select(&s->identity)    ── pick vendor vtable (no if/switch)
8. eip_open_connection(s, t)                      ── Forward Open Ext, fallback legacy
9. md_cache_create(&s->metadata)
   s->vendor->list_tags(s, t)                     ── metadata phase 1 (root tags)
10. dev->driver_state = s ; connection ready
```

Teardown `eip_driver_close(dev)`: `eip_forward_close` → `eip_unregister_session`
→ `socket_close` → `md_cache_destroy` → `eip_session_destroy`.

---

## 11. Path lookup → functions (the resolve flow)

For a call like `plc_get_type(dev, "my_array[42].field1.4", 0, t, &ty)`:

```
api.c:        handle_acquire → lock → plc_path_parse("my_array[42].field1.4")
driver:       eip_resolve(dev, &path, index=0, &node, t)
  cache:        md_cache_find_tag("my_array")            → root md_tag_t
  vendor:       (if struct field needed) vendor->fetch_udt(field.udt_id)
  descend:      my_array → [42] → .field1 → .4 (bit)
                accumulate type, elem_size, total_size, byte_offset, symbolic
api.c:        *out = node.type ; unlock → handle_release
```

The detailed metadata (UDT of `my_array`'s element, and any nested UDT that
`field1` belongs to) is fetched on first descent and cached for the device
lifetime, so repeat resolutions are pure cache walks.

---

## 12. CMake changes

- `src/utils/CMakeLists.txt`: add `platform.c` to the `plctag2_utils` sources.
  Add `ws2_32` link on Windows for the socket code (the lib already links it; the
  util lib needs it too for the socket symbols, or keep sockets in `src/lib`).
- `src/lib/CMakeLists.txt`: no change needed — `GLOB_RECURSE *.c` already picks up
  `api/`, `core/`, `protocol/**`. (Re-run CMake after adding files so the glob is
  re-evaluated.)
- Ensure `src/lib` private include dir (already set to `${CMAKE_CURRENT_SOURCE_DIR}`)
  so `#include "core/device.h"` / `"protocol/eip/eip.h"` resolve.

---

## 13. Testing approach

- **Unit (no network)** under `src/tests/unit/`:
  - `path.c` parser: valid/invalid paths, bit selectors, multi-dim subscripts.
  - `cip.c` EPATH builders + `cip_parse_reply` against captured byte vectors.
  - `metadata.c` descent over a hand-built cache (no socket): type/size/offset.
  - vendor `map_type` for Rockwell/generic type codes.
- **Integration** under `src/tests/integration/` (guarded by an env var with a
  gateway address): full connect sequence → identity → Forward Open → list_tags →
  resolve a known tag's type/size.
- **Tools** under `src/tools/`: a `plc_tag_list` CLI that opens a device and prints
  the root tag enumeration — exercises M1–M4 end to end.

Captured byte vectors for unit tests can be lifted from the
`~/Projects/libplctag/src/poc/ab_server_fiber` server fixtures.

---

## 14. Deferred (M5/M6) — interfaces already accommodate

- **Read/write (M5):** `driver->read/->write` + `vendor->encode_read/encode_write/
  decode_value`; bit access is read-modify-write of the containing element;
  `value.c` does range-checked conversions and sentinel handling.
- **Async + threading (M6):** per-connection background thread owning the socket,
  a request queue, `socket_wait_event`/wake-pipe in `platform.c`, staged ops on
  `timeout == 0`, and `plc_poll_events`. The synchronous M1–M5 code becomes the
  body the worker thread runs; the `api_mutex`/refcount/handle design already
  isolates callers from the worker.
- **Other vendors:** `vendor_omron.c` fills in OMRON enumeration/type decoding;
  registered in the vendor registry with its own `matches()` — no changes to any
  call site.
- **Other protocols:** a `modbus_driver_vtable` registered by scheme — no changes
  to `api.c`.
```
