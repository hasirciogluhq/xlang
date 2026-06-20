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
13. [Concurrency and sync](#concurrency-and-sync)
14. [HTTP server](#http-server-libshttp)
15. [Networking and fetch](#networking-and-fetch)
16. [JSON parsing](#json-parsing)
17. [File I/O](#file-io)
18. [Syscall](#syscall)
19. [External linking](#external-linking)
20. [Compiler builtins](#compiler-builtins)
21. [Testing](#testing)
22. [Limitations](#limitations)

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

### Interface

Port/adapter style contracts (Go/TypeScript inspired):

```xlang
interface Greeter {
    Greet(name: string): int32
}

struct ConsoleGreeter {
    prefix: string
}

fn Greet(g: ConsoleGreeter, name: string): int32 {
    print("%s %s", g.prefix, name)
    return 0
}

fn main() {
    local c: ConsoleGreeter = new ConsoleGreeter { prefix = "hello" }
    local g = c as Greeter
    return 0
}
```

Use `as InterfaceName` to cast a struct to an interface handle.

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

xlang resolves modules from:

1. **`libs/`** — importable standard library (`json`, `http`, `process`, …)
2. **`runtime/`** — linked automatically; also importable (`scheduler`, `net`)
3. **Relative paths** — `import foo from ./foo`
4. **`XLANG_PATH`** — colon-separated extra search directories

### Directory packages

A folder under `libs/` is a **package** — no barrel file required:

```
libs/http/router.xlang
```

```xlang
import * as http from http          // merges http/*
import Router from http             // submodule http/router
import router from http/router      // single file
```

### Import syntax

```xlang
import * as json from json                    // namespace — json.parse(...)
import expect from test                        // selective symbol import
import router from http/router                 // submodule as namespace (path with /)
import * as http, Router from http             // mixed clause import
import json from json                          // same-name namespace alias
from json import parse                         // legacy selective import
```

**Rules**

- `import * as X from M` — exported symbols available as `X.Name`
- `import Symbol from M` — import a single exported function or global (e.g. `expect` from `test`)
- `import Name from M` — submodule (`http/router`) when `M/Name` exists, or exported symbol
- `import alias from M/path` — prefix import when module path contains `/`
- `from M import a, b` — legacy selective import (same as clause form)
- Bare `import M` merges all symbols (including private) into the current file

Selective imports pull only the named exported symbols (and supporting structs/globals) into the current module — private symbols cannot be imported.

### libs layout

| Module | Description |
|--------|-------------|
| `json` | `parse`, typed field accessors (`data.Int(key)`) |
| `http` | Package merging `http/router` + `http/server` |
| `http/router` | Gin-style `Context`, `r.Use`, `r.Get`, `ListenAndServe` |
| `http/server` | TCP transport — `Conn`, `ServerInfo`, `Listen`, `AcceptOnce` |
| `test` | Vitest-style `expect`, `expectFn`, `fail` |
| `process` | fork, pipe, fd, env, `file_read` |
| `file` (runtime) | `ReadAll`, `Write`, `OpenRead`, `Exists`, stream I/O |
| `sync` (runtime) | `Lock`, `RWLock`, `AtomicInt` — method-style API |

See [vscode/README.md](../vscode/README.md) for IDE completion of imported modules.

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
    go worker(1, "alpha")
    go worker(2, "beta")
    wait_all()
    return 0
}
```

**Important:** `spawn` / `go` only accept a **bound call** — arguments are given inside the call:

```xlang
go job_worker(1, "ok")       // ✓ shorthand
spawn(job_worker(1, "ok"))   // ✓ explicit
spawn(job_worker, 1, "ok")   // ✗ (old API, not supported)
```

`go expr` is syntactic sugar for `spawn(expr)`.

The compiler automatically generates a **thunk** (parameterless `i32()` wrapper) for each `spawn(...)`; worker threads run this thunk.

### Scheduler internals (runtime)

- `array SpawnTask` — FIFO queue
- `mutex` + `cond` — worker wake-up / `wait_all` synchronization
- `worker_loop` — takes tasks from queue, runs `invoke0(entry)`
- `init_scheduler()` — starts CPU-1 worker threads (min 1)

Syscalls only for: `cpu_count`, `mutex_*`, `cond_*`, `start_thread`.

---

## Concurrency and sync

Mutex, reader-writer lock, and atomic types live in `runtime/sync.xlang`. Logic is pure xlang; only minimal OS/LLVM bridges are used (`mutex_*`, `cond_*`, `atomic_*`, `sleep_ms`).

```xlang
import sync from sync

struct SharedState {
    count: AtomicInt
    guard: Lock
}

fn main() {
    local st: SharedState = new SharedState {
        count = sync.NewAtomicInt(0),
        guard = sync.NewLock()
    }
    st.guard.Lock()
    st.count.FetchAdd(1)
    st.guard.Unlock()
    return 0
}
```

### Lock (mutex)

| Method | Description |
|--------|-------------|
| `NewLock()` | Create embeddable `Lock` |
| `l.Lock()` | Block until acquired |
| `l.Unlock()` | Release; clears timeout |
| `l.SetTimeout(ms)` | Auto-release after `ms` **while held** (returns 0 if not held) |
| `l.TryLock()` | Non-blocking acquire |
| `l.IsHeld()` | Returns `1` while held |

### RWLock

| Method | Description |
|--------|-------------|
| `NewRWLock()` | Reader-writer lock |
| `rw.ReadLock()` / `rw.ReadUnlock()` | Shared read access |
| `rw.WriteLock()` / `rw.WriteUnlock()` | Exclusive write access |
| `rw.SetWriteTimeout(ms)` | Auto-release write lock while held |
| `rw.WriteIsHeld()` | Returns `1` while write held |

### AtomicInt / AtomicBool

| Method | Description |
|--------|-------------|
| `NewAtomicInt(n)` / `NewAtomicBool(v)` | Heap atomic cell |
| `a.Load()` / `a.Store(v)` | Atomic load/store |
| `a.FetchAdd(delta)` | Fetch-and-add (`AtomicInt`) |
| `a.CompareExchange(exp, des)` | CAS (`AtomicInt`) |

Syscalls (internal bridge only): `atomic_*`, `mutex_*`, `cond_*`, `sleep_ms`.

See `examples/sync_lock.xlang` for `go` + mutex + atomic counter.

---

## HTTP server (libs/http)

Gin-inspired minimal router split into two modules:

- **`http/router`** — routing, middleware, `Context`, HTTP parsing, `ListenAndServe`
- **`http/server`** — raw TCP I/O (`Conn`, `ServerInfo`, `Listen`, `AcceptOnce`)

Import the package namespace or submodules directly:

```xlang
import * as http from http          // merged package
import router from http/router      // router-only (tests)
```

Handlers receive **`Context`** (request + response + per-request stash). No module globals.

```xlang
import * as http from http

fn auth(ctx: Context) {
    local user = new User { name = "admin" }
    ctx.Bind("auth.user", ref(user))
    return 0
}

fn handle_me(ctx: Context) {
    local user = ctx.Ref("auth.user") as User
    ctx.JSON(200, "{\"name\":\"" + user.name + "\"}")
    return 0
}

fn main() {
    local r = http.NewRouter()
    r.Use(auth)
    r.Get("/me", handle_me)
    r.ListenAndServe("127.0.0.1", 8080, on_listen)
    return 0
}
```

### Router

| Method | Description |
|--------|-------------|
| `NewRouter()` | New router |
| `r.Use(mw)` | Middleware `fn(ctx: Context)` — runs before each handler |
| `r.Get/Post/...` | Register routes |
| `r.Group(prefix)` | Shared prefix + middleware chain |
| `r.DispatchRequest(method, path) → Context` | Test/dispatch |
| `r.ListenAndServe(host, port, on_listen)` | TCP server |

### Context (handler)

| Method | Description |
|--------|-------------|
| `ctx.Param(name)` | Path param |
| `ctx.Method()` / `ctx.Path()` | Request info |
| `ctx.String(status, body)` | Plain text response |
| `ctx.JSON(status, body)` | JSON response |
| `ctx.HTML(status, body)` | HTML response |
| `ctx.Put(key, val)` | Stash string (middleware → handler) |
| `ctx.Get(key)` | Read stashed string |
| `ctx.Bind(key, ref(obj))` | Attach struct handle |
| `ctx.Ref(key) as Type` | Load struct from stash |

Compiler: `ref(obj)` → int64, `handle as User` loads struct from handle.

---

## Networking and fetch

HTTP/HTTPS client logic lives in `runtime/net.xlang`. Syscalls bridge TCP/TLS I/O to the OS (OpenSSL for HTTPS).

| API | Description |
|-----|-------------|
| `fetch(url: string)` | HTTP GET; returns `FetchResponse` |

```xlang
import * as json from json

struct FetchResponse {
    status: int32   // HTTP status code (e.g. 200)
    ok: int32       // 1 if 2xx, else 0
    body: string    // response body
}

fn main() {
    local resp = fetch("https://jsonplaceholder.typicode.com/todos/1")
    print("status: %d", resp.status)
    local data = json.parse(resp.body)
    print("title: %s", data.String("title"))
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

JSON helpers live in `libs/json.xlang` (pure xlang, no syscall). Parse once, then read fields via method-style accessors on the `Json` object.

| API | Description |
|-----|-------------|
| `json.parse(text)` | Parse JSON string → `Json` object |
| `data.Int(key)` | Object integer field |
| `data.String(key)` | Object string field (unquoted) |
| `data.Bool(key)` | Object bool field (`true` → 1) |
| `data.Has(key)` | 1 if key exists |
| `data.Null(key)` | 1 if field is `null` |
| `data.Get(key)` | Raw `JsonValue` wrapper |
| `value.AsInt()` / `AsString()` / `AsBool()` / `IsNull()` | Coerce `JsonValue` |

```xlang
import * as json from json

fn main() {
    local resp = fetch("https://api.example.com/data")
    local data = json.parse(resp.body)
    local name = data.String("name")
    local age = data.Int("age")
    local nested = data.Get("meta").AsString()
    return 0
}
```

---

## File I/O

High-level file operations live in `runtime/file.xlang`. Backed by C++ fstream syscalls — not exposed directly to user code.

```xlang
import * as file from file

fn main() {
    file.Write("out.txt", "hello")
    local body = file.ReadAll("out.txt")
    print("%s", body)

    local f = file.OpenRead("out.txt")
    local chunk = file.Read(f)
    file.Close(f)

    if file.Exists("out.txt") != 0 {
        print("size: %d", file.Size("out.txt"))
    }
    return 0
}
```

### API (runtime/file)

| Function | Description |
|----------|-------------|
| `ReadAll(path)` | Read entire file into string |
| `Write(path, data)` | Overwrite file |
| `Append(path, data)` | Append to file |
| `Exists(path)` | 1 if file exists |
| `Size(path)` | File size in bytes |
| `Open(path, mode)` | Open handle (`File` struct) |
| `OpenRead(path)` / `OpenWrite(path)` / `OpenAppend(path)` | Convenience openers |
| `Read(f)` / `WriteStream(f, data)` | Stream read/write on open handle |
| `Close(f)` | Close handle |
| `IsOpen(f)` | 1 if handle is valid |

Internal syscalls (bridge only): `file_open`, `file_close`, `file_read_path`, `file_write_path`, `file_exists`, `file_size`, `file_read_handle`, `file_write_handle`.

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
| `invoke0(entry)` | `int64` fn ptr → call with no args |
| `ref(obj)` | Struct pointer as `int64` (context stash) |
| `invoke1(entry, ctx)` | `int64` fn ptr → call with one arg (`Context`, `ServerInfo`, …) |
| `array_len(arr)` | Array length |
| `array_push(arr, val)` | Append to end |
| `array_get(arr, i)` | Read element by index |
| `array_pop(arr)` | Pop from end |
| `array_pop_front(arr)` | Take and remove from front |
| `str_len(s)` | String length |
| `str_eq(a, b)` | 1 if equal, else 0 |
| `str_byte(s, i)` | Byte at index (0–255), or -1 |
| `str_find(haystack, needle)` | Index of substring, or -1 |
| `str_sub(s, start, len)` | Substring copy |
| `str_from_int(n)` | Format int32 as decimal string |

`invoke0` — scheduler worker loop. `invoke1` — HTTP handlers (`fn(ctx: Context)`) and listen callbacks (`fn(info: ServerInfo)`). `ref(obj)` + `handle as Type` for context struct stash.

---

## Testing

Builtin test runner (Vitest-like). Test files must end with **`.test.xlang`** (like `filename.test.ts`). Do **not** define `main` — use `Test*` functions instead; the runner injects a synthetic harness.

```bash
xlank test
xlank test test/xlang
xlank test test/xlang/json.test.xlang
```

Each file is compiled and run individually. The C++ runner discovers all exported `Test*` functions and calls them via `test_run_one`. Output example:

```
 RUN  test/xlang/json.test.xlang

 ✓ TestJsonParseInt
 ✓ TestJsonParseString

 Test Files  1 passed (1)
      Tests  2 passed (2)
```

### Import

```xlang
import expect from test
import * as json from json
```

Selective `import expect from test` pulls only the assertion API — not the whole test module.

### Assertion API (`libs/test.xlang`)

| Function | Description |
|----------|-------------|
| `expect(actual)` | Start assertion chain (int32, int64, or string) |
| `expectFn(entry)` | Assert on function pointer |
| `expect(actual).toEqual(expected)` | Equality (int or string) |
| `expect(v).toBeTrue()` / `.toBeFalse()` | Truthiness |
| `expectFn(fn).toThrow()` | Function must panic |
| `expectFn(fn).toError()` | Function must return error |
| `expect(actual).not().toEqual(...)` | Negated assertion |
| `fail(msg)` | Fail current test |

```xlang
import expect from test
import * as json from json

fn TestJsonParseInt() {
    local data = json.parse("{\"userId\": 7}")
    expect(data.Int("userId")).toEqual(7)
    return 0
}
```

Function names must start with `Test` (Go-style). Each test function returns `0` on success; failed assertions set an internal flag read by the harness.

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
| `examples/fetch.xlang` | fetch HTTP GET + json.parse |
| `examples/http_server.xlang` | HTTP router + ListenAndServe |
| `examples/sync_lock.xlang` | Lock + AtomicInt + go spawn |
| `examples/interfaces.xlang` | interface + struct + as cast |
| `test/main.xlang` + `test/lib.xlang` | external link |
| `test/xlang/*.test.xlang` | runtime tests (`xlank test`) |

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

Runtime compilation: `runtime/runtime.xlang` is built as a separate `.o` and linked into user programs. Additional runtime modules (`scheduler`, `net`, `file`, `sync`) are resolved via import and linked as needed.
