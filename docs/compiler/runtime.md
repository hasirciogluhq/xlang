---
title: Runtime
---

The compiler owns the language, types, layout, CPU primitives and compile-time facts. Everything that happens at run time is stdlib code: memory acquisition, the allocation policy, panic text and abort, I/O, threads, formatting. The only seam between the two is a `register(role)` function whose signature the compiler checks structurally.

## Roles

`register(role) decl` makes a declaration global and exported under a role. There are four.

| Role | Signature | Called by |
|---|---|---|
| `panic` | `fn(msg: *ui8)`, no return | every compiler failure path: an explicit `panic`, division by zero, a failed array bound, a format mismatch |
| `alloc` | `fn(size: usize, align: usize) -> *void` | `new T` |
| `free` | `fn(p: *void)`, no return | `delete p` |
| `unwind` | `fn(i32, *usize, *ui8)` | the landing pads of `try` |

A declaration that does not fit its role is `E0503`; `register(start)` is an unknown role, also `E0503`.

Registrations override by group. The stdlib is group 1 and the user's program is group 2:

- a user registration of a role replaces the stdlib one;
- a stdlib registration never replaces a user one;
- two registrations of one role in the same group are `E0502`.

Using a feature whose role is unregistered is `E0501`: `new T` without `alloc`, `delete p` without `free`, `try` without `unwind`.

## Panic

```xlang
panic("something went wrong\n")
```

The stdlib `panic` writes the message to stderr and exits the process with status 134. A program that registers its own `panic` replaces this. `panic` never returns, so it may be used anywhere a value is expected.

## Unwinding and `try`

A panic inside a `try` does not end the process. It continues in the `catch` block after the `defer` statements and drops of every frame between the panic and the `try` have run, innermost first, and the program goes on.

```xlang
try {
    work()
} catch e {
    println("failed: {}", e)
}
```

- `e` is a `*ui8` pointing at the panic message. It points into a buffer owned by the `try` (255 bytes plus a NUL), so copy it to keep it past the `catch`.
- `catch` without a name is allowed. `panic(e)` inside a `catch` rethrows to the next enclosing `try`.
- Leaving the body normally, or with `return`, `break` or `continue`, skips the `catch`.

The compiler only emits landing pads, `try` snapshots and the pad table. The walker is stdlib code in `std/runtime/unwind`, registered with the `unwind` role.

Unwinding walks frame pointers. A frame of foreign code (a C library callback, for example) therefore cannot be unwound, and every stack entry point must zero the frame pointer. See [Known limitations]({{ site.baseurl }}/known-limitations.html) for the remaining gaps.

## Memory

The compiler does not know allocators. It knows two registered functions: `new T` calls `alloc` with the size and alignment of `T`, and `delete p` calls `free`. `new(p) T` is placement into memory you already have and allocates nothing.

The stdlib registers `alloc` and `free` when the `default-allocator` feature is on (the default). They forward to `currentAllocator`, a plain exported global of `std/mem` whose type is the `Allocator` interface:

```xlang
interface Allocator {
    fn alloc(self, size: usize, align: usize) -> *void
    fn free(self, p: *void)
}
```

`Allocator` is ordinary stdlib code, not a builtin, and the compiler never names it. A program changes where memory comes from in either of two ways:

```xlang
// 1. replace the allocator the stdlib forwards to
@init fn boot() {
    currentAllocator = MyArena()
}

// 2. register its own roles (a user `alloc` must be paired with a user `free`)
register(alloc) fn myAlloc(size: usize, align: usize) -> *void { ... }
register(free) fn myFree(p: *void) { ... }
```

`memcpy`, `malloc` and `free` are not builtins; they are stdlib code in `std/mem`. `String` and `Vec<T>` allocate through `currentAllocator`, not through the registered `alloc`, so replacing the registered roles alone does not redirect them. The scheduler in `std/thread/sched` uses its own heap and is not redirected either.

## Features

`--std-features` selects what the stdlib includes. Each feature becomes a `featureXxx` constant that `#if` can test, for example `featureDefaultAllocator`. Turning `-default-allocator` off removes the default `alloc` and `free` registrations, so a program must register its own.
