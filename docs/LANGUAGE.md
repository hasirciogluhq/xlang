# xlang Language Reference

This document describes the syntax, types, module system, and runtime API of the xlang programming language.

---

## Table of contents

1. [Overview](#overview)
2. [Program structure](#program-structure)
3. [Types](#types)
4. [Variables](#variables)
5. [Functions](#functions)
6. [Control flow](#control-flow)
7. [Expressions and operators](#expressions-and-operators)
8. [Struct and memory](#struct-and-memory)
9. [Array](#array)
10. [Modules and import](#modules-and-import)
11. [Print](#print)
12. [Scheduler and spawn](#scheduler-and-spawn)
13. [Syscall](#syscall)
14. [External linking](#external-linking)
15. [Compiler builtins](#compiler-builtins)
16. [Limitations](#limitations)

---

## Overview

xlang is a statically typed language compiled with LLVM. Design goals:

- **Self-hosting runtime** — scheduler, print, and queue logic are written in xlang
- **Syscall = OS bridge** — scheduler logic is not a syscall; only pthread, sysconf, etc.
- **Simple syntax** — C/Go blend; `fn`, `local`, `struct`, `import`

Every runnable program (by default) is linked with the runtime so `print`, `spawn`, and `wait_all` are available.

---

## Program structure

A `.xlang` file may contain at the top level:

```xlang
import scheduler                    // module import
from math import add, mul           // selective import

struct Point { x: int32, y: int32 } // struct definition

counter = 0                          // global variable
queue: array SpawnTask               // typed global (init optional)

declare syscall sleep_ms(ms)         // OS bridge declaration
declare external fn helper(x)        // external symbol (link time)

export fn exported_fn() { ... }      // exported function
fn internal_fn() { ... }             // file-local function

fn main() {                          // entry point
    ...
    return 0
}
```

**Entry point:** `fn main()` — parameterless or typed, may return `int32`.

---

## Types

| Syntax | Description |
|--------|-------------|
| `int`, `int32` | 32-bit signed integer (default numeric type) |
| `int64` | 64-bit signed integer |
| `bigint` | 128-bit integer (LLVM i128) |
| `float`, `float32` | 32-bit float |
| `double`, `float64` | 64-bit float |
| `bool` | boolean |
| `char` | 8-bit character |
| `string` | UTF-8 string (`i8*` ABI) |
| `void` | Return type (rare) |
| `StructName` | User-defined struct |
| `*T` | Pointer (`*int32`, `*Point`) |
| `array T` | Growable array (element type `T`) |

Type annotation:

```xlang
local x: int32 = 10
local name: string = "ali"
local p: Point = new Point { x = 1, y = 2 }
```

If no annotation is given, the default type is **`int32`**.

---

## Variables

### Global

```xlang
counter = 0
limit: int32 = 100
spawn_queue: array SpawnTask
export counter = 0    // exported with export
```

### Local

```xlang
local i = 0
local msg: string = "hello"
```

Local variables are declared with `local` in a function body; type inference comes from the assignment expression.

### Assignment

```xlang
counter = counter + 1
p.x = 42
arr[i] = value
```

---

## Functions

### Definition

```xlang
fn add(a: int32, b: int32) {
    return a + b
}

fn greet(name: string) {
    print("hello, %s", name)
    return 0
}
```

If no return type is specified, the default is `int32`:

```xlang
fn foo(): int32 { return 0 }
```

### Overload

Same name, different parameter types:

```xlang
export fn process(x: int32) { print(x) }
export fn process(s: string) { print(s) }
```

The compiler selects the correct overload at call site based on argument types.

### Variadic

```xlang
export fn print(...) {
    return 0
}
```

`...` at the end; fixed parameters + unlimited extra arguments. Runtime `print` is defined this way.

### Export / External

| Modifier | Meaning |
|----------|---------|
| `export` | Importable from other modules |
| `external` | Implementation in another `.o` file; declaration only |
| `declare` | No definition (syscall or external prototype) |

```xlang
export fn add(a, b) { return a + b }

declare external fn zamazokka(x)

declare syscall sleep_ms(ms)
```

---

## Control flow

### if / else

```xlang
if x > 0 {
    print("positive")
} else {
    print("non-positive")
}
```

### while

```xlang
while i < 10 {
    print(i)
    i = i + 1
}

while true {
    // infinite loop (worker loop, etc.)
}
```

---

## Expressions and operators

### Arithmetic

`+` `-` `*` `/` — for int and float; mixed types promote to float.

### Comparison

`==` `!=` `<` `>` `<=` `>=`

### Logical

`&&` `||` — no short-circuit (simple codegen)

### Strings

```xlang
local s = "hello" + " world"
local t = "count: " + 42          // int → string conversion
print("User: %s", name)
print("score: %d", 100)
print("x=%d y=%d", p.x, p.y)
```

String literal: `"..."`  
Escapes: `\"`, `\\`, `\n` (lexer support)

### Literals

```xlang
42          // int
3.14        // float
true false  // bool
null        // for pointer / string / struct
```

### Function call

```xlang
add(1, 2)
p.x
obj.field
```

---

## Struct and memory

### Definition

```xlang
struct Point {
    x: int32
    y: int32
}
```

### Creation and field access

```xlang
local p: Point = new Point { x = 10, y = 20 }
print(p.x)
p.y = 30
delete p
```

`new StructName { field = expr, ... }` allocates on the heap.  
`delete expr` frees heap memory (struct pointer).

---

## Array

Used as a dynamic, front-pop queue or growable buffer.

```xlang
local items: array int32
items = new array int32

array_push(items, 42)
local n = array_len(items)
local first = array_pop_front(items)
```

Array of struct elements:

```xlang
spawn_queue: array SpawnTask
spawn_queue = new array SpawnTask
array_push(spawn_queue, task)
```

Array runtime is codegen'd by the compiler as `%array.hdr`.

---

## Modules and import

### Full module import

```xlang
import scheduler
// all symbols exported by scheduler are merged
```

### Alias

```xlang
import math as m
```

### Selective import

```xlang
from math import add
from math import add as sum
```

### Module resolution

1. Directory of the importing file: `modul.xlang`
2. Directories in the `XLANG_PATH` environment variable

Same directory with `math.xlang` + `hello.xlang`:

```xlang
// math.xlang
export fn add(a, b) { return a + b }

// hello.xlang
from math import add
fn main() { print(add(1, 2)) }
```

**Rule:** `import modul` (no alias) merges all symbols (including private). `from modul import x` only imports `export` symbols.

---

## Print

Single variadic function; no separate `print_int` / `print_str`.

```xlang
print("hello world")              // single string → with newline
print(42)                         // single int
print("fmt: %d", x)               // format + arguments
print("a=%s b=%d", name, id)      // multiple arguments
print("count: " + 42)             // string concat
```

Implementation lowers to `printf` in the compiler; runtime `export fn print(...)` is a stub for signature/link compatibility.

---

## Scheduler and spawn

Go-routine-like concurrency. **All scheduling logic** lives in `runtime/scheduler.xlang`.

### API (runtime export)

| Function | Description |
|----------|-------------|
| `spawn(bound_call)` | Enqueues a call with bound arguments |
| `wait_all()` | Waits until queue and in-flight work finish |
| `cpu()` | CPU core count |
| `add_worker()` | Starts an additional worker thread |

### Using spawn

```xlang
fn worker(id: int32, tag: string) {
    print("job %d: %s", id, tag)
    return 0
}

fn main() {
    spawn(worker(1, "alpha"))
    spawn(worker(2, "beta"))
    wait_all()
    return 0
}
```

**Important:** `spawn` only accepts a **bound call** — arguments are given inside the call:

```xlang
spawn(job_worker(1, "ok"))   // ✓
spawn(job_worker, 1, "ok") // ✗ (old API, not supported)
```

The compiler automatically generates a **thunk** (parameterless `i32()` wrapper) for each `spawn(...)`; worker threads run this thunk.

### Scheduler internals (runtime)

- `array SpawnTask` — FIFO queue
- `mutex` + `cond` — worker wake-up / `wait_all` synchronization
- `worker_loop` — takes tasks from queue, runs `invoke0(entry)`
- `init_scheduler()` — starts CPU-1 worker threads (min 1)

Syscalls only for: `cpu_count`, `mutex_*`, `cond_*`, `start_thread`.

---

## Networking and fetch

HTTP/HTTPS client logic lives in `runtime/net.xlang`. Syscalls bridge TCP/TLS I/O to the OS (OpenSSL for HTTPS).

| API | Description |
|-----|-------------|
| `fetch(url: string)` | HTTP GET; returns `FetchResponse` |

```xlang
struct FetchResponse {
    status: int32   // HTTP status code (e.g. 200)
    ok: int32       // 1 if 2xx, else 0
    body: string    // response body
}

fn main() {
    local resp = fetch("https://jsonplaceholder.typicode.com/todos/1")
    print("status: %d", resp.status)
    print("title: %s", json_get_string(resp.body, "title"))
    return 0
}
```

Supported URLs:

| Scheme | Default port |
|--------|----------------|
| `http://host/path` | 80 |
| `https://host/path` | 443 |
| `http://host:8080/path` | custom |

Net syscalls (used internally by runtime, not directly by user code):

```xlang
declare syscall net_tcp_connect(host: string, port: int32): int64
declare syscall net_tls_connect(host: string, port: int32): int64
declare syscall net_send(fd: int64, data: string): int32
declare syscall net_tls_send(fd: int64, data: string): int32
declare syscall net_recv(fd: int64, max: int32): string
declare syscall net_tls_recv(fd: int64, max: int32): string
declare syscall net_close(fd: int64): int32
declare syscall net_tls_close(fd: int64): int32
```

---

## JSON parsing

JSON helpers live in `runtime/json.xlang` (pure xlang, no syscall).

| API | Description |
|-----|-------------|
| `json_get_string(json, key)` | Object string field (unquoted) |
| `json_get_int(json, key)` | Object integer field |
| `json_get_bool(json, key)` | Object bool field (`true` → 1) |
| `json_has_key(json, key)` | 1 if key exists |
| `json_is_null(json, key)` | 1 if field is `null` |
| `json_get_field(json, key)` | Raw `JsonResult { ok, value, next }` |
| `json_unescape(raw)` | Unescape JSON string contents |

```xlang
local resp = fetch("https://api.example.com/data")
local name = json_get_string(resp.body, "name")
local age = json_get_int(resp.body, "age")
```

---

## Syscall

Declaration for OS / kernel access. Implementation in C++ (`syscalls.cpp`) → LLVM IR.

```xlang
declare syscall sleep_ms(ms)
declare syscall random_range(min, max)
declare syscall cpu_count(): int32
declare syscall mutex_init(): int64
```

User code does not see pthread or libc directly; only `declare syscall` names are used.

Return type is optional:

```xlang
declare syscall foo()           // returns int32 (default)
declare syscall bar(): int64
```

**NOT syscalls:** scheduler logic (`spawn` queue, worker loop) — those live in the xlang runtime.

---

## External linking

You can compile another xlang module as an object and link it.

```bash
xlank build lib.xlang --build=lib -o lib.o
xlank build main.xlang lib.o -o app
xlank run main.xlang lib.o
```

```xlang
// lib.xlang
export fn zamazokka(arg) {
    print(arg)
}

// main.xlang
declare external fn zamazokka(str)

fn main() {
    zamazokka(1978)
}
```

`declare external fn` — implementation in `lib.o`; must match the `export` symbol name.

---

## Compiler builtins

Not syscalls; codegen special cases:

| Name | Description |
|------|-------------|
| `print(...)` | Variadic printf (see above) |
| `invoke0(entry)` | `int64` function pointer → parameterless call |
| `array_len(arr)` | Array length |
| `array_push(arr, val)` | Append to end |
| `array_pop_front(arr)` | Take and remove from front |
| `str_len(s)` | String length |
| `str_eq(a, b)` | 1 if equal, else 0 |
| `str_byte(s, i)` | Byte at index (0–255), or -1 |
| `str_find(haystack, needle)` | Index of substring, or -1 |
| `str_sub(s, start, len)` | Substring copy |

`invoke0` is used by the scheduler worker loop to run task entries.

---

## Limitations

Early version; known constraints:

- No lambda / closure (`spawn` bound call + compiler thunk instead)
- No generics / templates
- No `for` loop (only `while`)
- `spawn` thunk with struct arguments still limited
- Type inference is limited (from assignment in `local`, otherwise default `int32`)
- Error messages still evolving
- No debugger / GC; manual `delete`

---

## Example files

| File | Topic |
|------|-------|
| `examples/strings.xlang` | print, format, concat |
| `examples/hello.xlang` | import, global, local |
| `examples/types.xlang` | struct, new, delete |
| `examples/math.xlang` | export module |
| `examples/scheduler.xlang` | spawn, wait_all |
| `examples/fetch.xlang` | fetch HTTP GET |
| `test/main.xlang` + `test/lib.xlang` | external link |
| `test/xlang/*.test.xlang` | runtime tests (`xlank test`) |

---

## Testing

Builtin test runner (Vitest-like). Test files must end with **`.test.xlang`** (like `filename.test.ts`).

```bash
xlank test
xlank test test/xlang
```

Each file is compiled and run individually. Output example:

```
 RUN  test/xlang/json.test.xlang

 ✓ json > reads integer fields
 ✓ json > reads string fields

 Test Files  1 passed (1)
      Tests  2 passed (2)
```

API (`runtime/test.xlang`):

| Function | Description |
|----------|-------------|
| `describe(name)` | Group name for following `it` calls |
| `it(name, fn)` | Run test function (pass fn ref, not call) |
| `expect_eq(a, b)` | `int32` equality |
| `expect_str_eq(a, b)` | string equality |
| `expect_true(v)` / `expect_false(v)` | truthiness |
| `expect_ne(a, b)` | `int32` inequality |
| `test_summary()` | Print summary; return failure count (use as `main` exit) |

```xlang
fn test_user_id() {
    local body = "{\"userId\": 1}"
    expect_eq(json_get_int(body, "userId"), 1)
    return 0
}

fn main() {
    describe("json")
    it("reads userId", test_user_id)
    return test_summary()
}
```

---

## Compilation pipeline (summary)

```
.xlang → tokenize → parse → AST
       → module merge (import)
       → codegen → LLVM IR
       → syscall IR inject
       → clang -c → .o
       → clang link (+ runtime.o) → executable
```

Runtime compilation: `runtime/runtime.xlang` (+ `import scheduler`, `import net`) is built as a separate `.o` and linked into user programs.
