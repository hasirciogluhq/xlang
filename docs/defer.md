# `defer`

`defer stmt` registers a statement to run when the **enclosing block is left**, not where it is
written. It keeps cleanup next to the code that needs it and runs on every way out of the block.

```
fn work(m: *Mutex) -> i32 {
    lock(m)
    defer unlock(m)        // only registered here; runs when `work`'s body is left

    if broken() { return -1 }   // unlock(m) runs, then the function returns
    step()
    return 0                    // unlock(m) runs after `0` is computed
}
```

## Syntax

```
defer <statement>
```

The statement is an expression statement (usually a call), an assignment (`x = v`, `x += v`,
`x++`) or a `{ ... }` block. Control statements such as `if` and `while` are accepted too.

```
defer free(p)
defer count += 1
defer {
    flush(&out)
    close(fd)
}
```

## When it runs

A deferred statement runs on every way out of the block it was registered in:

| Exit | Runs? |
|------|-------|
| Control falls off the end of the block | yes |
| `return` anywhere inside the block (also from nested blocks) | yes |
| `break` / `continue` that leaves the block | yes |
| Labeled `break outer` / `continue outer` crossing the block | yes |
| `panic`, a trap, `exit`/process termination, a `noreturn` asm | no |

The scope is the **block**, not the function. This differs from Go (function scope) and matches
Zig:

```
fn f() {
    {
        defer a()
    }                  // a() runs here
    b()                // after a()
}
```

## Order

Deferred statements of one block run in **reverse order of registration** (last in, first out).
Blocks unwind from the innermost outwards.

```
{
    defer print("1")
    defer print("2")
    defer print("3")
}                       // prints 3, 2, 1
```

## Loops

A `defer` in a loop body belongs to the body block, so it runs at the end of **every iteration**,
and also on `continue` and `break` of that iteration:

```
for i in 0..4 {
    defer acc += 100
    if i == 1 { continue }   // acc += 100 runs, then the step (i++)
    if i == 3 { break }      // acc += 100 runs, then the loop is left
    acc += 1
}
```

With `continue`, deferred statements run **before** the loop step (`for` step or the condition
re-test). A labeled jump runs the deferred statements of every block between the jump and the
target loop, innermost first.

## Return values

`return v` computes `v` **before** the deferred statements run. The deferred statements cannot
change what is returned:

```
fn f() -> i64 {
    local n: i64 = 1
    defer n = 100
    return n          // returns 1; n is 100 afterwards but nobody sees it
}
```

(Through a pointer the deferred statement can still change the pointee; only the value being
returned is fixed.)

## Registration is lexical

Deferred statements are expanded at compile time at every exit **that appears after the `defer`
in the source**. A `defer` that is textually after an early `return` is not registered at that
`return`:

```
fn f(p: *ui8) -> i32 {
    if p == nil { return -1 }   // nothing deferred yet
    defer release(p)
    ...
    return 0                    // release(p) runs
}
```

A `defer` inside an `if`/`else`/`switch` arm or a loop body lives in that block only. There is no
conditional registration at run time.

## What is evaluated when

- **Names** are resolved where the `defer` is written (the variable that is in scope there).
- **Values** are read when the deferred statement runs, not when it is registered:

```
local x: i64 = 1
defer use(x)     // uses x as it is at block exit
x = 2            // use(2)
```

To capture a value now, copy it into a local first and defer a statement that uses the copy.

## Restrictions

A deferred statement cannot contain, at its top level:

- `return`, `break`, `continue`
- `local` (a declaration)
- another `defer`

These are `E0709` ("this statement cannot be deferred"). A deferred `{ ... }` block may declare
locals of its own.

## How it works

The compiler lowers a `defer` once and clones the lowered statement at each exit (end of block,
`return`, `break`, `continue`). There is no runtime defer stack, no allocation and no cost beyond
the duplicated code. Diagnostics inside a deferred statement are reported once.

Consequences:

- a `defer` in a block with many exits grows the code by one copy per exit;
- a deferred statement is ordinary code, so it must not depend on being run exactly once per
  *call* (it runs once per exit path taken, which is one at run time).

## Notes

- `defer` is not an exception/unwind mechanism: there are no exceptions in xl, and `panic` does
  not run deferred statements.
- There is no RAII or destructor concept; `defer` is the replacement for scope guards
  (`lock` + `defer unlock`, `alloc` + `defer free`, `open` + `defer close`).
