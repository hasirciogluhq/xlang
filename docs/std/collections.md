---
title: Collections
---

The standard library has one collection: `Vec<T>`, a growable array in `std/collections`.

```xlang
from "std" import Vec, println

@entry fn main() -> i32 {
    local v: Vec<i64> = Vec<i64>()
    v.push(10)
    v.push(20)
    v.push(30)
    println("length {}", v.length())
    println("first {}", *v.first())
    println("last {}", *v.last())
    v.set(1, 25)
    println("after set {}", *v.get(1))
    return 0
}
```

## Storage and ownership

- `Vec<T>` stores its elements **bitwise in slots**, in memory allocated through `currentAllocator`.
- The `Vec` owns the elements below its length, and each one is dropped exactly once: when it is replaced, removed or truncated, and when the `Vec` itself is dropped.
- `get`, `first` and `last` return a **pointer** to an element, so reading an element never copies it. `first` and `last` panic when the `Vec` is empty.
- `set` drops the old element and stores the new one. `push`, `insert` and `set` take their value **by move**.
- `pop`, `remove` and `swapRemove` return the removed element **by value**, so the caller owns it and drops it.
- Growth allocates `max(needed, 2 * capacity, 4)` slots and moves the existing elements into them.

## API

| Method | Meaning |
|---|---|
| `init()` | an empty `Vec`, no allocation |
| `length()`, `capacity()`, `isEmpty()` | size, allocated slots, emptiness |
| `get(i)`, `first()`, `last()` | a pointer to an element |
| `set(i, v)` | replace element `i`, dropping the old one |
| `push(v)` | append |
| `pop()` | remove and return the last element |
| `insert(i, v)`, `remove(i)`, `swapRemove(i)` | insert before `i`; remove at `i` keeping order; remove at `i` by swapping in the last element |
| `truncate(n)`, `clear()` | keep the first `n` elements; remove all |
| `reserve(n)` | ensure room for `n` elements |

`vecClone<T>(*Vec<T>)` returns a copy with the same elements in the same order. It copies **only** when the element type needs no drop, because a bitwise copy of an element that needs drop would drop it twice; a droppable element type panics.

## Panics

An index outside the elements panics with `Vec: index I out of range for length L`. `pop` on an empty `Vec` panics with `Vec: pop from empty vec`, and `first`/`last` with `Vec: first of empty vec` / `Vec: last of empty vec`.

## Sum types: `Option<T>` and `Result<T, E>`

`std/collections` also holds the two standard sum types. Both are ordinary generic payload enums, so a `switch` over
their variants is exhaustive and the live payload is dropped exactly once.

```xlang
from "std/collections/option" import Option
from "std/collections/result" import Result

enum Option<T> {
    None
    Some(T)
}

enum Result<T, E> {
    Ok(T)
    Err(E)
}
```

There is no `?` operator in the language yet, so early exit is written out:

```xlang
local r: Result<i32, MyErr> = step()
switch r {
    case Result.Err(e) { return Result.Err(e) }
    case Result.Ok(v) { /* continue with v */ }
}
```

The helpers are `optionOrElse`, `optionIsSome`, `optionTake` for `Option<T>` and `resultOrElse`, `resultIsOk`,
`resultTake`, `resultTakeErr` for `Result<T, E>`; each `...Take` writes the payload through an `out` pointer and returns
whether that variant was live.
