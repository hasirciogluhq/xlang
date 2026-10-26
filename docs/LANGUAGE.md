# xlang Language Reference

This document describes the syntax, types, module system and compiler-provided features of xlang as implemented today. Source files use the extension `.xl`. The standard library lives in `std/` and is written in xlang itself; the compiler gives no special treatment to names inside it.

---

## Table of contents

1. [Overview](#overview)
2. [Program structure](#program-structure)
3. [Types](#types)
4. [Variables and globals](#variables-and-globals)
5. [Pointers and conversions](#pointers-and-conversions)
6. [Functions](#functions)
7. [Control flow](#control-flow)
8. [Expressions and operators](#expressions-and-operators)
9. [Structs, classes, interfaces and enums](#structs-classes-interfaces-and-enums)
10. [Memory and `new`](#memory-and-new)
11. [Modules and import](#modules-and-import)
12. [Attributes, `declare` and external linking](#attributes-declare-and-external-linking)
13. [Conditional compilation, constants and macros](#conditional-compilation-constants-and-macros)
14. [Compiler builtins](#compiler-builtins)
15. [Standard library](#standard-library)
16. [Testing](#testing)
17. [Command line](#command-line)
18. [Limitations](#limitations)

---

## Overview

xlang is a low-level, statically typed language compiled with LLVM. There is no libc and no C runtime: the compiler knows only the language, the CPU primitives and a closed list of builtins, and everything else (printing, memory allocation, `memcpy`, threads, sockets, HTTP, TLS) is ordinary xlang code in `std/`.

Design points:

- Direct control: raw pointers, `syscall`, inline assembly and atomics are part of the language; higher-level APIs are opt-in imports.
- No generics, no tuples and no multiple return values.
- Safety: `*T` is unchecked; array indexing is bounds-checked by default (`--safety=checked`) and can be switched off with `--safety=unchecked`.
- No borrow checker, no garbage collector, no exceptions.

---

## Program structure

A `.xl` file contains top-level declarations only: `fn`, `global`, `struct`, `class`, `interface`, `impl`, `enum`, `type`, `define`, `import`/`from`, `export ... from` re-exports, macro invocations and calls such as `staticAssert(...)`. Declarations end at a line break; a semicolon is not used. Comments are `// line`, `/* block */` and `/// doc`.

```
from "std" import print, println, printInt

struct Point { x: i32, y: i32 }

global counter: i64 = 0

export fn add(a: i32, b: i32) -> i32 {
    return a + b
}

fn main() -> i32 {
    local p: Point = Point { x: 1, y: 2 }
    println("hello")
    printInt(add(p.x, p.y) as i64)
    println("")
    return 0
}
```

**Entry points:** the OS entry symbol (`_start`, or `mainCRTStartup` on Windows) runs the registered `start` first and then `main`, and the process exits with `main`'s `int32` result (0 when `main` returns nothing).

- `main` is the user program's entry: `fn main()` with no parameters, optionally returning `i32` (any integer return type is used as the exit status).
- `start` is the boot hook: `register(start) fn start()` with no parameters and no return type. The stdlib (`std/runtime`) defines it as an empty function; with `--no-std` no `start` is registered and nothing runs before `main`.
- No entry stub is emitted when the program has no `main`.
- Entry stubs exist for Linux and macOS on x86_64 and aarch64, and for Windows on x86_64.

---

## Types

| Syntax | Description |
|--------|-------------|
| `i8`, `i16`, `i32`, `i64` | Signed integers |
| `ui8`, `ui16`, `ui32`, `ui64` | Unsigned integers |
| `isize`, `usize` | Pointer-sized signed / unsigned integers |
| `f32`, `f64` | Floating point |
| `bool` | `true` / `false` |
| `void` | No value (the return type of a function without `->`) |
| `*T` | Pointer to `T` (`*ui8`, `*Point`, `*void`) |
| `fn(A, B) -> R` | Function pointer |
| `[T; N]` | Fixed-size array; `N` is a compile-time constant |
| `str` | Built-in read-only view `{ ptr: *ui8, len: usize }`; it never owns its bytes |
| `Name` | A `struct`, `class`, `interface` or `enum` |

There is no owned string type in the language; `String` lives in the standard library. There are no character literals; write the byte value (`97`). `type Name = T` declares a transparent alias.

Integer literals may be decimal or hexadecimal (`0xff`); float literals look like `1.5` or `1.5e3`. Escapes in string literals include `\n`, `\0` and `\x1b`.

### Strings

A string literal takes its type from the place it is written in:

| Context | Type | Bytes live in | Mutable |
|---|---|---|---|
| expected `*ui8` (or an enum over `*ui8`) | `*ui8` | `.rodata`, NUL-terminated | no; writing through it is undefined behavior |
| expected `[ui8; N]` | `[ui8; N]` copy, zero-padded | the array | yes |
| anything else, including no expected type | `str` | `.rodata`, with a trailing NUL that is not in `len` | no |

`local buf: [ui8; 8] = "Jones"` copies the bytes into the array; a literal longer than `N` is error `E0717`. A `str` literal's `len` is the number of decoded bytes, so `"Jo\0nes".len` is 6. The `str` fields are `ptr` (type `*ui8`) and `len` (type `usize`); both are read-only. `s[i]` reads one byte and, under `--safety=checked`, panics when `i >= len`. Assigning through a `str` (`s[i] = x`, `s.len = n`) is error `E0716`. `==` and `!=` are not defined on `str`; use `strEq` from the standard library. A `str` converts to `*ui8` only through `.ptr`; `as` cannot convert it either.

The `std/str` module provides `strEq`, `strFromC` (view of a NUL-terminated C string), `strFromParts` (view of `n` bytes at a pointer) and `strSlice` (checked sub-range). A `str` view does not keep its bytes alive; the owner of the bytes must outlive it.

---

## Variables and globals

Locals are declared with `local`; the type is written after a colon:

```
local i: i32 = 0
local buf: [ui8; 64]          // no initializer
local q: *Point = &p
local n = 3                   // type inferred from the initializer
```

Globals use `global` with a mandatory type. Their initializer must be a compile-time constant; without one the global is zero-initialized:

```
global counter: i64 = 10
global table: [i32; 4]
global banner: *ui8 = "xlang"
```

A `const` is a named compile-time constant that cannot be written. It is allowed at module level (with `export`) and as a statement in any block. The type is optional; without it, the type is the default type of the literal value (integers `i32`, or `i64` when the value does not fit; floats `f64`; `bool`; strings `str`). A module-level `const` without a type must have a literal value. The initializer must be a compile-time constant: literals, arithmetic and casts on them, `define` constants, arrays and struct literals of such values, and `&global`. Reading another variable or `const` in an initializer is error `E0715`.

```
const LIMIT: i32 = 7
const GREETING = "hi"              // str
const TABLE: [ui8; 4] = "abc"       // copied bytes, zero-padded
```

Every `const` is emitted as read-only data. Assigning to a `const`, or to an element or field of one, is error `E0716`. `&LIMIT` is allowed and yields an ordinary pointer; writing through it is undefined behavior. A block-level `const` is visible only in its block; it cannot be deferred.

Assignment is `target = value` and `target op= value` for `+ - * / % & | ^ << >>`. `x++` and `x--` are statements, not expressions.

---

## Pointers and conversions

- `&x` takes an address, `*p` dereferences, `p.field` reaches a field through a pointer or a value, `p[i]` indexes a pointer or an array.
- `nil` is the null pointer.
- Conversions are explicit with `as`: `x as i64`, `p as *ui8`, `(addr as usize)`, `n as *void`.
- Implicit conversions: same-signedness integer widening only. A signed integer converting to a float warns (`W0601`). Unsigned to signed (and the reverse) is an error (`E0601`). Narrowing always needs `as`.
- An `unsafe { ... }` block allows implicit integer conversions that change signedness when the target is the same width or wider, and unsigned to float (still `W0601`); narrowing always needs `as`.
- Arithmetic on fixed-width integers wraps (`u + 10` with `u: ui8 = 250` gives 4). Division by zero and out-of-range array indexes call the registered `panic` under `--safety=checked`.

---

## Functions

```
fn name(a: i32, b: *ui8) -> i64 { ... }
fn log(msg: *ui8) { ... }            // no `->` means void
fn apply(f: fn(i32) -> i32, v: i32) -> i32 { return f(v) }
```

- Overloading: functions may share a name when their parameter types differ. Defining the same parameter types twice is `E0401`. `main`, `register`ed functions, `export(c)` and `declare` functions cannot be overloaded (`E0405`). Non-virtual methods of one class may overload each other; `virtual`/`override` methods cannot (`E0405`).
- A call picks the overload with the lowest total argument cost: an exact type costs 0; an untyped integer literal costs 1 for `i64` and 2 for any other integer type; an untyped float literal costs 1 for `f64` and 2 for `f32`; a string literal costs 0 for `str`, 1 for `*ui8` and 2 for `[ui8; N]`; `nil` costs 1 for a pointer; integer widening costs 1; wrapping a value in an interface costs 1; an integer to float conversion costs 2; other conversions cost 3. An argument that does not convert makes the overload unavailable. A tie is `E0403`; no available overload is `E0404` and lists the signatures.
- A function name used as a value is chosen by the expected `fn(...)` type. Without one, an overloaded name is `E0403`.
- Overloaded functions are named with their parameter types in the link name, even under `--mangle=none`.
- There are no variadic functions and no default arguments. (`syscall` is the one builtin with a variable argument count.)
- A function with a return type must end every path in `return`, otherwise `W0701`.
- `export fn` makes a function visible to importers; `export(c) fn` additionally keeps the C symbol name unmangled. Names are mangled by default (`--mangle=none` disables it).
- Methods belong to classes and `impl` blocks (see below); `self` is the first parameter.
- Naming convention: types `PascalCase`, functions, variables and fields `camelCase`.

---

## Control flow

```
if cond { ... } else if other { ... } else { ... }

while cond { ... }
do { ... } while cond
for local i: i64 = 0; i < 10; i++ { ... }
for i in 0..10 { ... }                    // upper bound exclusive, evaluated once

outer: for i in 0..5 {
    for j in 0..5 {
        if j == 3 { continue outer }
        if i == 3 { break outer }
    }
}

switch v {
    case 1, 2 { return 10 }
    case Color.Red { return 1 }
    default { return -1 }
}

defer cleanup(p)
```

- Conditions need no parentheses; bodies always use braces.
- `switch` works on integers, bools and enums, has no fallthrough, and a `break` inside it leaves the enclosing loop.
- `defer stmt` runs when the enclosing block is left; see [defer.md](defer.md).
- `cond ? a : b` is the conditional expression.

---

## Expressions and operators

- Arithmetic: `+ - * / %`. Bitwise: `& | ^ ~ << >>`, with Go-style precedence (`& << >>` bind like `*`, `| ^` like `+`). A shift count of at least the bit width gives 0 (`>>` of a negative signed value gives -1).
- Comparison: `== != < <= > >=`. Logical: `&& || !` (short-circuit).
- Struct literal: `Point { x: 1, y: 2 }`. Array literal: `[1, 2, 3]`.
- Calls: `f(a, b)`, method calls `obj.method(a)`, macro calls `name!(...)`.
- `sizeOf(T)`, `alignOf(T)` and `offsetOf(T, field)` are compile-time constants.

---

## Structs, classes, interfaces and enums

```
struct Rect { w: i32, h: i32 }

interface Shape { fn area(self) -> i32 }
impl Shape for Rect {
    fn area(self) -> i32 { return self.w * self.h }
}

class Animal {
    legs: i32
    virtual fn sound(self) -> i32 { return 1 }
}
class Dog : Animal {
    tail: i32
    override fn sound(self) -> i32 { return 2 }
}

enum Color: i32 { Red, Green = 5, Blue }     // underlying type defaults to i32
```

- A `struct` holds data only (no methods). The default layout is xlang's own; `@repr(c)` selects C layout.
- A `class` has fields, methods, single inheritance (`class Dog : Animal`) and a vptr; `virtual` methods are overridden with `override`. A `*Dog` converts to `*Animal` with `as`.
- An `interface` is a set of method signatures. A value of interface type is a fat pointer (data pointer plus table); `impl Interface for Type` provides the methods, and a value of that type passes where the interface is expected. Interfaces are nominal: without the `impl` the use is error `E0504`.
- Enum members are accessed as `Color.Green` and converted to integers with `as`.

---

## Memory and `new`

`new T { ... }` allocates through the registered allocator and returns `*T`; `new(a) T { ... }` allocates through an explicit allocator `a` (a pointer to a type with `impl Allocator`, or an `Allocator` value). `Allocator` is the builtin interface `alloc(self, size: usize, align: usize) -> *void` / `free(self, p: *void)`. Without a registered allocator `new` without `(a)` is error `E0501`.

The stdlib registers a default heap allocator when the `default-allocator` feature is on (the default); `free(p)` and `setAllocator(a)` come from `std/mem`. `malloc`/`free`/`memcpy` are not builtins: they are stdlib code (`std/mem`, `std/memory`).

`register(role) decl` makes a declaration global and exported under a role. The roles are `panic` (`fn(msg: *ui8)` with no return type; the compiler's failure paths call it) `allocator` and `start` (the boot hook described under entry points). Only one registration per role is allowed (`E0502`).

---

## Modules and import

A module path `"x"` resolves to `x.xl` or `x/index.xl`. `std` and `std/x` come from the stdlib root; other paths are searched from the importing file's directory, then the root file's directory.

```
import "std"                                // module as a namespace
import "std/mem" as mem
from "std" import print, println            // selective import
import print, println from "std"            // same, other order
from "std" import printInt as pi            // rename
import * from "std"                         // bring every export in
import * as sys from "std/sys"              // namespace alias
```

Re-exports:

```
export * from "memory"
export * from "memory" except strlen
export * as sys from "sys"
export { sysClockSeconds, sysRandom } from "sys"
```

- Only `export`ed declarations are visible to importers.
- Two exports or imports of the same name are an error; the same entity reaching a module by two paths is warning `W0401`.
- `import "std"` gives the whole standard library; the compiler itself loads the default stdlib unless `--no-std` or `--std=<dir>` is given.

---

## Attributes, `declare` and external linking

`declare` prefixes any declaration that is defined somewhere else. `@library("name")` names a native OS library for a declared function:

```
@library("ntdll") declare fn NtTerminateProcess(handle: i64, status: i32) -> i32
declare fn helper(x: i32) -> i32          // resolved at link time from another object
```

- A `declare`d function not defined in the compilation produces `W0301`.
- Native libraries are used only where the OS requires them (the Windows `std/sys/windows.xl` imports from ntdll, ws2_32, bcrypt and crypt32). On Windows the linker driver generates import libraries from the `@library` declarations; Linux and macOS programs are linked with no libraries at all (`@library` has no effect there).
- Prefer the raw `syscall(nr, args...)` builtin to a library call. Imports of OS libraries stay in `std/sys/<os>.xl` behind `#if os == "..."`.
- Other attributes: `@repr(c)` (C layout), `@allow(CODE)` (silence a warning for the item).
- `--emit=header` writes a C header for exported items; `--emit=static|shared|obj` produce a static archive, shared library or object.

See [LINKING.md](LINKING.md) for how objects are linked.

---

## Conditional compilation, constants and macros

```
#if os == "linux" && arch == "x86_64"
define SYS_WRITE = 1
#elif os == "macos"
define SYS_WRITE = 0x2000004
#else
#error "unsupported target"
#endif

#ifdef featureDefaultAllocator
...
#endif
```

- Directives are `#if`, `#elif`, `#else`, `#endif`, `#ifdef`, `#ifndef` and `#error`; the condition is a normal expression. Inactive regions are only lexed.
- Predefined names: `os`, `arch`, `pointerWidth`, `endian`, `debug`, `optLevel`, and `featureXxx` for each stdlib feature. `-D NAME[=VALUE]` and `-U NAME` set or remove user definitions.
- `define NAME = expr` is a compile-time constant.
- `define f(x: expr) { ... }` defines a hygienic macro; parameter kinds are `expr`, `type`, `ident`, `block` and `items`. A macro is invoked as `f!(...)`. There is no stringification.
- `staticAssert(cond)` fails the build when `cond` is false.

---

## Compiler builtins

The global namespace holds only a closed list of builtins plus `register`ed names:

- `syscall(nr, args...)`: raw system call; arguments are integers or pointers; returns `i64` (negative `errno` style). The maximum argument count is checked against the target.
- `__builtin_atomicLoad`, `__builtin_atomicStore`, `__builtin_atomicXchg`, `__builtin_atomicCas`, `__builtin_atomicAdd` and `__builtin_fence`. The memory order is a compile-time `MemoryOrder` value (`MemoryOrder.acquire`, `MemoryOrder.release`, ...).
- `__builtin_asm("...", out(reg) x, ...)`: inline assembly.
- `sizeOf`, `alignOf`, `offsetOf`, `staticAssert`, `nil`.
- Registered `panic` and `allocator` (and `start`).

`print`, `alloc`-style helpers, `memcpy`, `assert` and the rest are stdlib functions that must be imported.

---

## Standard library

All of it is in `std/`; `import "std"` or `from "std" import name` brings it in, and `from "std/<module>" import name` imports one module. Modules (see each file for its API):

| Module | Contents |
|--------|----------|
| `std/io` | `print`, `println`, `eprint`, `printInt`, `printUInt` |
| `std/assert` | `assert!(cond)` macro (panics on failure) |
| `std/memory` | `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, mapping helpers |
| `std/mem` | default heap allocator, `free`, `setAllocator` |
| `std/math` | `abs`, `min`, `max`, `minInt`, `maxInt`, `sqrt` |
| `std/thread`, `std/sync`, `std/sched` | threads, `Mutex`/`Cond`/`RwLock`/`Once`, cooperative scheduler (`spawn`, `waitAll`, `yield`) |
| `std/fs`, `std/json` | whole-file helpers; read-only flat JSON lookups |
| `std/dns`, `std/tls`, `std/http` | name resolution, TLS 1.3 client/server, HTTP server and client (see [http.md](http.md)) |
| `std/sha256`, `std/sha512`, `std/hmac`, `std/aesgcm`, `std/chacha20poly1305`, `std/x25519`, `std/rsa`, `std/ecdsa`, `std/bignum`, `std/x509`, `std/pem` | crypto and certificate code used by TLS |
| `std/cpu`, `std/gpu`, `std/sys` | CPU helpers; GPU access; the per-OS raw system layer |
| `std/runtime` | the registered `panic` and `start` |
| `std/testing` | test framework |

Memory-related defaults can be changed with `--std-features` (for example `-default-allocator`); each feature becomes a `featureXxx` constant for `#if`.

---

## Testing

`xlang test` finds `*.test.xl` files, builds and runs each one, and fails a file when its exit status is not 0. A test file uses `std/testing`:

```
from "std/testing" import Suite, T, suiteInit, test, suiteRun, expectEq

fn addWorks(t: *T) { expectEq(t, 1 + 2, 3) }

fn main() -> i32 {
    local s: Suite
    suiteInit(&s)
    test(&s, "add works", addWorks)
    return suiteRun(&s)
}
```

Matchers are typed functions (`expect`, `expectTrue`, `expectEq`, `expectEqUInt`, `expectEqStr`, `expectNear`, `expectGt`, `expectNil`, ...). There is no discovery and no chained matcher syntax. The repository's own end-to-end programs live in `test/e2e/`.

---

## Command line

```
xlang build file.xl [-o out] [--emit=exe|obj|ir|bc|header|static|shared]
xlang run file.xl
xlang check file.xl
xlang parse file.xl
xlang layout file.xl [Type]
xlang test [patterns...]
xlang --explain E0501
```

Common flags: `-D`/`-U`, `--target <triple>`, `-O0..3`, `-g`, `--cpu`, `--mangle=xl|none`, `--safety=checked|unchecked`, `--no-std`, `--std <dir>`, `--std-features`, `--allow`/`--deny`/`--warn-all`/`--allow-all`, `--incremental`, `--diag-format=text|json`.

Diagnostics have codes: `E` for errors, `W` for warnings (for example `E0501`, `W0402`); `--explain CODE` prints the long explanation, and warnings are controlled with `--allow`, `--deny` and `@allow`.

---

## Limitations

Open decisions and intentional incompleteness are recorded in `KNOWN-LIMITATIONS.md` (no generics or tuples, flat read-only JSON, TLS 1.3 only, and so on).
