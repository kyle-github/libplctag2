# PLC Access Library (libplctag2) Coding Guidelines and Rules

## Philosophy

You are an AI-first software engineer. Assume all code will be written and maintained by LLMs, not humans. Optimize for model reasoning, regeneration, and debugging — not human aesthetics. Favor deterministic, testable behavior. Produce code that is predictable, debuggable, and easy for future LLMs to rewrite or extend.

## File Structure

### Header Files (.h)

Order of sections:

1. `#pragma once` (must be the very first line)
2. Copyright and license comment block
3. File description comment
4. System includes
5. Local includes (non-system includes from this project)
6. Constant #defines, enums
7. Forward declarations for opaque types
8. typedefs for non-struct types
9. struct types
10. Declarations of exported global data
11. Function declarations (extern)
12. One blank line at end of file

### Code Files (.c)

Order of sections:

1. Copyright and license comment block
2. File description comment
3. System includes
4. Local includes (non-system includes from this project)
5. Local/internal constant #defines, enums
6. Forward declarations for types used later
7. typedefs for non-struct types
8. struct types
9. Definitions of global/static data
10. Forward declarations of all static functions
11. Definitions of all public functions
12. Definitions of all static functions
13. One blank line at end of file

### Copyright and License Header

Every .h and .c file must start with this header (after `#pragma once` in .h files):

```c
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
```

## Types

### Integer Types

Use only explicitly sized integers:

- `uint8_t`/`int8_t`
- `uint16_t`/`int16_t`
- `uint32_t`/`int32_t`
- `uint64_t`/`int64_t`

**Never use:**

- `int` - use enum type name, `size_t` for indices, `ssize_t` for counts that can be negative, or `bool` for flags
- `short` - use explicitly-sized integers instead
- `char` - only for character strings; make no assumptions about signedness
- Use `uint8_t` for byte values
- Avoid `ssize_t` - not portable

**Exceptions:**

- When calling standard API functions that require `int`
- Platform-specific types: `socket_t`, `socklen_t`, `nfds_t` - typedef these for cross-platform compatibility

### Enums

typedef and use the enum type name for parameters and return values to clearly signal what is expected.

### Indexing

Use `size_t` for general indexing. Only use smaller unsigned integer types when space optimization is critical.

## Functions

### Return Values

- **Chainable functions**: Return simple status value (e.g., `bool`), use "out" parameter (pointer) for actual value
- **Non-chainable functions**: Return the thing created (socket, pointer to memory, etc.)
- **Functions with possible errors**: Ensure return value and error codes are differentiable; ideally use error field in data structure

### Failure Handling

- On failure, roll back app state to entry point
- Use standard status codes from `src/include/plctag.h` (`plc_status_t` enum):
  - `PLC_STATUS_PENDING` (1) - Operation pending
  - `PLC_STATUS_OK` (0) - Success
  - `PLC_STATUS_ERR_*` - Error codes (negative values): `PLC_STATUS_ERR_NULL_PTR`, `PLC_STATUS_ERR_NO_MEM`, `PLC_STATUS_ERR_TIMEOUT`, `PLC_STATUS_ERR_BAD_PATH`, etc.
- Use `plc_status_t` for the function result type when returning a status code

### Declarations

- **Internal functions**: Declare as `static` near top of .c file in forward declarations section
- **External functions**: Declare as `extern` in .h file only. Never use `extern` declarations outside the singular .h file
- Use `static inline` only for very short functions in header files

### Control Flow

- Keep control flow linear and simple
- Use small-to-medium functions; avoid deeply nested logic
- Pass state explicitly; avoid globals

## Comments

Use old-style C comments: `/* */` (not `//`)

Multi-line block format:

```c
/*
 * A long comment.
 * With many lines
 * of text.
 */
```

Comment only to note invariants, assumptions, or external requirements. Use descriptive-but-simple names to reduce need for comments.

## Logging

Use the logging system from `src/utils/debug.h`.

### Log Levels

From `src/utils/debug.h`:

- `DEBUG_NONE` (0) - No logging
- `DEBUG_ERROR` (1) - Fatal for whole application (e.g., malloc returning NULL)
- `DEBUG_WARN` (2) - Serious problems that won't crash the app
- `DEBUG_INFO` (3) - High-level normal operation debug output
- `DEBUG_DETAIL` (4) - Low-level normal operation debug output, not high frequency
- `DEBUG_SPEW` (5) - Extremely high-frequency debug output (normally not used)

### Debug Modules

Each module has an independently settable level. Modules are enumerated in
`src/utils/debug.h` (`debug_module_t`):

- `DEBUG_MODULE_LIB` - Library core / public API
- `DEBUG_MODULE_INIT` - Startup and teardown
- `DEBUG_MODULE_UTILS` - Utility code (arena, bytes, etc.)
- `DEBUG_MODULE_PLATFORM` - Platform abstraction
- `DEBUG_MODULE_SOCKET` - Socket / transport layer
- `DEBUG_MODULE_PROTOCOL` - Protocol-neutral dispatch
- `DEBUG_MODULE_ENIP` - EtherNet/IP encapsulation
- `DEBUG_MODULE_CIP` - CIP
- `DEBUG_MODULE_MODBUS` - Modbus
- `DEBUG_MODULE_TAG` - Tag / value handling
- `DEBUG_MODULE_PATH` - Path parsing and descent
- `DEBUG_MODULE_SYSTEM` - System-level operations

Set levels per module with `debug_module_set_level()`, or all at once with
`debug_set_all_modules()`. A message is emitted only when its level is at or
below the module's configured level (and at or below the compile-time ceiling
`PLC_COMPILE_DEBUG_LEVEL`).

### Logging Pattern

The `id` argument (3rd) is a `int64_t` correlation id: pass the device handle
when available, otherwise `0`.

```c
plc_status_t my_func(int64_t param) {
    /* use 0 or a handle/id if available */
    pdebug(DEBUG_MODULE_UTILS, DEBUG_INFO, 0, "Starting"); /* optionally with arguments */

    /* code ... */
    for(size_t j = 0; j < BIG_NUM; j++) {
        pdebug(DEBUG_MODULE_UTILS, DEBUG_SPEW, 0, "msg");
    }

    if(status == BAD) {
        pdebug(DEBUG_MODULE_UTILS, DEBUG_WARN, 0, "BAD status");
        return PLC_STATUS_ERR_INTERNAL;
    }

    pdebug(DEBUG_MODULE_UTILS, DEBUG_DETAIL, 0, "starting some small part of my_func()");

    pdebug(DEBUG_MODULE_UTILS, DEBUG_INFO, 0, "Done"); /* can add terminating condition if no error */

    return PLC_STATUS_OK;
}
```

Emit detailed, structured logs at key boundaries. Make errors explicit and informative.

## Code Organization and Style

### const

Only use `const` when passing character strings that won't be changed, or passing static constant strings. Avoid `const` otherwise.

### static and extern

Use `static` or `extern` explicitly. Do not leave bare functions or variables at global level.

- Use `extern` in header files
- Use `extern` for exported functions/variables in .c files
- Use `static` as much as possible

### Header Guards

Use `#pragma once` (at very first line of .h file). Never use `#ifndef XXX/#define XXX/#endif`.

### Globals

Do not use globals unless there is no other way. Pass parameters instead.

## Standard Library Usage

### Core Utilities

- Status codes: `src/include/plctag.h` - `plc_status_t` enum
- Logging: `src/utils/debug.h` - `pdebug()` macro with module/level support
- Arena allocator: `src/utils/arena.h` - fixed-size bump allocator; `arena_reset()`
  reclaims everything at once. Use per-request scratch memory for packet building.
- Packet encode/decode: `src/utils/bytes.h` - `Bytes` (pointer/length) plus
  type-safe `bytes_pack()` / `bytes_unpack()` (C11 `_Generic`, explicit endianness
  via `BYTES_LE` / `BYTES_BE`). Allocations come from an `Arena`. This is the
  preferred mechanism for all on-the-wire protocol encoding and decoding.

Planned (not yet present): a platform abstraction (`src/utils/platform.h`) for
time, threading, and sockets.

## Architecture Principles

### Structure

- Use consistent, predictable project layout
- Group code by feature/screen; minimize shared utilities
- Create simple, obvious entry points
- Identify shared structure before scaffolding multiple files
- Use framework-native composition patterns
- Duplication requiring same fix in multiple places is a code smell

### Design

- Prefer flat, explicit code over abstractions or deep hierarchies
- Avoid clever patterns, metaprogramming, unnecessary indirection
- Minimize coupling so files can be safely regenerated
- Write code so any file/module can be rewritten from scratch without breaking the system
- Prefer clear, declarative configuration (JSON/YAML)

### Platform

- Use platform conventions directly and simply
- All code must compile without change on: Linux, macOS, BSD, Windows
- For simple name changes: use #define for platform-specific names (do centrally, not at call sites)
- For platform-specific logic: inline ifdef if 1-2 places; wrapper function if more

### Quality

- Keep tests simple and focused on observable behavior
- Follow existing patterns when extending/refactoring
- Prefer full-file rewrites over micro-edits unless told otherwise

## Naming

Use descriptive-but-simple names. Make intent obvious without excessive verbosity.

## Error Handling

Make errors explicit and informative. Always check return values and handle failures appropriately.
